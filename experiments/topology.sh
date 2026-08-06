#!/bin/bash
# ---------------------------------------------------------------------------
# experiments/topology.sh -- CPU pinning for reproducible timings.
#
# Sourced by config.sh; also runnable standalone to print the slot table:
#
#     experiments/topology.sh                 # show the layout for the configured mode
#     PIN_MODE=core N_JOBS=32 experiments/topology.sh
#
# WHY: an unpinned job is migrated by the scheduler across L3 domains (CCXs) and
# may be co-scheduled with another job on the two SMT threads of one physical
# core. Both cost 20-60% wall time at random, which shows up as "the same
# deterministic run takes different times on different seeds". Pinning each
# parallel slot to a fixed, disjoint set of physical cores inside ONE L3 domain
# removes both effects, so a timing difference means an algorithmic difference.
#
# Everything is read from /sys at runtime -- no machine-specific constants. On
# this AMD EPYC 7702P that yields 16 L3 domains (CCXs) x 4 physical cores.
#
# Provides:
#   topo_detect                 fill TOPO_DOM[] / TOPO_DOM_ALL[]
#   topo_build_slots MODE JOBS  fill PIN_SETS[]  (one `taskset -c` mask per slot)
#   topo_init                   the above + export PIN_SETS_STR + one summary line
#   topo_print                  full human-readable table
# ---------------------------------------------------------------------------

# ---- detection ------------------------------------------------------------

# One line per online CPU:  <l3-domain-key> TAB <cpu> TAB <1 if primary thread>
# "primary" = the lowest-numbered thread of its physical core; the other thread
# of that core is an SMT sibling that shares all execution resources with it.
_topo_rows() {
  local dir cpu key sib prim
  for dir in /sys/devices/system/cpu/cpu[0-9]*; do
    cpu=${dir##*/cpu}
    [[ -e "$dir/topology/thread_siblings_list" ]] || continue
    [[ -r "$dir/online" && "$(<"$dir/online")" == 0 ]] && continue
    key=""
    [[ -r "$dir/cache/index3/shared_cpu_list" ]] && key=$(<"$dir/cache/index3/shared_cpu_list")
    [[ -z "$key" && -r "$dir/topology/package_cpus_list" ]] && key=$(<"$dir/topology/package_cpus_list")
    [[ -z "$key" ]] && key=all
    sib=$(<"$dir/topology/thread_siblings_list")
    prim=${sib%%[,-]*}
    printf '%s\t%s\t%s\n' "$key" "$cpu" "$([[ "$cpu" == "$prim" ]] && echo 1 || echo 0)"
  done
}

# TOPO_DOM[i]     = comma list of PRIMARY cpus of L3 domain i  (e.g. "0,1,2,3")
# TOPO_DOM_ALL[i] = comma list of ALL cpus of that domain      (e.g. "0,1,2,3,64,65,66,67")
# Domains are ordered by their lowest cpu id; cpus ascending inside a domain.
topo_detect() {
  TOPO_DOM=(); TOPO_DOM_ALL=()
  local _k prim all
  while IFS=$'\t' read -r _k prim all; do
    TOPO_DOM+=("$prim"); TOPO_DOM_ALL+=("$all")
  done < <(_topo_rows | sort -t$'\t' -k2,2n | awk -F'\t' '
      { if (!($1 in seen)) { seen[$1]=1; order[++n]=$1 }
        all[$1] = ($1 in all ? all[$1] "," : "") $2
        if ($3 == 1) prim[$1] = ($1 in prim ? prim[$1] "," : "") $2 }
      END { for (i = 1; i <= n; i++) print order[i] "\t" prim[order[i]] "\t" all[order[i]] }')
  TOPO_NDOM=${#TOPO_DOM[@]}
  local d a
  TOPO_NPRIM=0; TOPO_NCPU=0
  for d in "${TOPO_DOM[@]}";     do IFS=, read -ra a <<<"$d"; TOPO_NPRIM=$((TOPO_NPRIM + ${#a[@]})); done
  for d in "${TOPO_DOM_ALL[@]}"; do IFS=, read -ra a <<<"$d"; TOPO_NCPU=$((TOPO_NCPU  + ${#a[@]})); done
}

# ---- slot construction ----------------------------------------------------
# Compact "0,1,2,3,64" -> "0-3,64" for readable log lines (taskset accepts both).
_topo_compact() {
  tr ',' '\n' <<<"$1" | sort -n | awk '
    NR==1 { s=$1; p=$1; next }
    $1 == p+1 { p=$1; next }
    { printf "%s%s", (out++ ? "," : ""), (s==p ? s : s "-" p); s=$1; p=$1 }
    END { printf "%s%s\n", (out++ ? "," : ""), (s==p ? s : s "-" p) }'
}

# topo_build_slots MODE NJOBS
#   ccx  : one slot per L3 domain (the whole CCX, 4 physical cores). NJOBS is
#          clamped to the number of domains. Maximum isolation: each job owns a
#          private 16 MiB L3 and can never be migrated out of it.
#   core : NJOBS slots carved out of the CCX-ordered list of physical cores, so
#          consecutive slots fill one CCX before moving to the next. NJOBS=32 ->
#          2 cores per job, 2 jobs per CCX (they share L3 but never a core).
#          NJOBS=64 -> 1 physical core each. Above 64 the SMT siblings are used.
#   off  : no pinning (previous behaviour).
# Fills PIN_SETS[] and sets PIN_NJOBS to the effective job count.
topo_build_slots() {
  local mode="$1" njobs="$2"
  PIN_SETS=(); PIN_WARN=""
  [[ ${TOPO_NDOM:-0} -gt 0 ]] || topo_detect

  case "$mode" in
    off)
      PIN_NJOBS="$njobs"
      return 0 ;;

    ccx)
      if (( njobs > TOPO_NDOM )); then
        PIN_WARN="N_JOBS=$njobs > $TOPO_NDOM L3 domains; clamped to $TOPO_NDOM (use PIN_MODE=core for more)"
        njobs=$TOPO_NDOM
      fi
      local i
      for (( i = 0; i < njobs; i++ )); do
        if [[ "${PIN_USE_SMT:-0}" == 1 ]]; then
          PIN_SETS+=("${TOPO_DOM_ALL[$i]}")
        else
          PIN_SETS+=("${TOPO_DOM[$i]}")
        fi
      done
      PIN_NJOBS=$njobs
      return 0 ;;

    core) ;;
    *)
      PIN_WARN="unknown PIN_MODE='$mode' -- running unpinned"
      PIN_NJOBS="$njobs"
      return 0 ;;
  esac

  # ---- core mode: chunk the CCX-ordered cpu list into NJOBS contiguous slots.
  # Using physical cores only while they last keeps SMT siblings idle, which is
  # what makes per-job timing repeatable; only past that do we hand out siblings.
  local -a cpus=() dom_of=()
  local i d a
  local -a src=("${TOPO_DOM[@]}")
  (( njobs > TOPO_NPRIM )) && src=("${TOPO_DOM_ALL[@]}")
  for (( i = 0; i < ${#src[@]}; i++ )); do
    IFS=, read -ra a <<<"${src[$i]}"
    for d in "${a[@]}"; do cpus+=("$d"); dom_of+=("$i"); done
  done

  local n=${#cpus[@]}
  if (( njobs > n )); then
    PIN_WARN="N_JOBS=$njobs > $n usable cpus; clamped to $n"
    njobs=$n
  fi

  local base=$(( n / njobs )) rem=$(( n % njobs )) off=0 sz s straddle=0
  for (( s = 0; s < njobs; s++ )); do
    sz=$base; (( s < rem )) && sz=$((sz + 1))
    local set="" first_dom="${dom_of[$off]}" whole=1
    for (( i = off; i < off + sz; i++ )); do
      set+="${cpus[$i]},"
      [[ "${dom_of[$i]}" != "$first_dom" ]] && whole=0
    done
    # A slot spanning several domains is fine only if it holds them WHOLE
    # (fewer jobs than domains); a partial overlap means two jobs share a CCX
    # unevenly and the isolation argument breaks down.
    if (( whole == 0 )) && (( sz % (n / TOPO_NDOM) != 0 || off % (n / TOPO_NDOM) != 0 )); then
      straddle=1
    fi
    PIN_SETS+=("${set%,}")
    off=$(( off + sz ))
  done
  (( rem != 0 || straddle == 1 )) && PIN_WARN="N_JOBS=$njobs does not divide the topology evenly \
(${TOPO_NDOM} domains x $(( n / TOPO_NDOM )) cpus); some slots straddle an L3 domain. \
Prefer a divisor/multiple: ${TOPO_NDOM}, $((TOPO_NDOM*2)), $((TOPO_NDOM*4))."
  PIN_NJOBS=$njobs
}

# ---- entry points ---------------------------------------------------------

# Confine the driver (this shell, GNU parallel, and the awk/date/mktemp calls
# run_capture makes around every job) to the cpus NO slot owns -- with SMT
# siblings left free by default, that is exactly the idle sibling threads. Left
# unconfined, the bookkeeping preempts a measured job on its own core, which is
# jitter injected by the harness itself. Skipped when nothing is left over.
# Workers re-widen to their slot via their own explicit `taskset -c`.
_topo_confine_housekeeping() {
  PIN_HOUSEKEEP=""
  [[ ${#PIN_SETS[@]} -gt 0 && "${PIN_HOUSEKEEPING:-1}" == 1 ]] || return 0
  local comp
  comp=$(awk -v all="${TOPO_DOM_ALL[*]}" -v used="${PIN_SETS[*]}" '
    BEGIN { n = split(used, u, /[ ,]+/); for (i = 1; i <= n; i++) taken[u[i]] = 1
            n = split(all, a, /[ ,]+/)
            for (i = 1; i <= n; i++) if (!(a[i] in taken)) printf "%s%s", (c++ ? "," : ""), a[i] }')
  [[ -z "$comp" ]] && return 0
  taskset -cp "$comp" $$ >/dev/null 2>&1 || return 0
  PIN_HOUSEKEEP="$comp"
  export PIN_HOUSEKEEP
}

# topo_init -- called by config.sh. Honours PIN_MODE and N_JOBS (N_JOBS=0/auto
# means "one job per L3 domain"), sets N_JOBS to the effective value, and
# exports PIN_SETS_STR (space-separated masks) for lib.sh:run_capture.
topo_init() {
  topo_detect
  local want="${N_JOBS:-0}"
  [[ "$want" == 0 || "$want" == auto ]] && want=$TOPO_NDOM
  topo_build_slots "${PIN_MODE:-ccx}" "$want"
  N_JOBS="$PIN_NJOBS"
  PIN_SETS_STR="${PIN_SETS[*]}"
  export N_JOBS PIN_SETS_STR
  [[ -n "$PIN_WARN" ]] && echo "experiments: WARNING $PIN_WARN" >&2
  _topo_confine_housekeeping
  if [[ ${#PIN_SETS[@]} -eq 0 ]]; then
    echo "experiments: CPU pinning OFF -- $N_JOBS jobs float across all ${TOPO_NCPU} cpus (timings will be noisy)" >&2
  else
    local ncpu; IFS=, read -ra _a <<<"${PIN_SETS[0]}"; ncpu=${#_a[@]}
    echo "experiments: pinning $N_JOBS jobs (PIN_MODE=${PIN_MODE}) -> $ncpu cpu(s) each," \
         "$TOPO_NDOM L3 domains x $((TOPO_NPRIM / TOPO_NDOM)) physical cores available" >&2
    [[ -n "$PIN_HOUSEKEEP" ]] \
      && echo "experiments: driver confined to $(_topo_compact "$PIN_HOUSEKEEP") (off the measured cores)" >&2
  fi
}

topo_print() {
  topo_detect
  printf 'L3 domains (CCX): %d   physical cores: %d   logical cpus: %d\n\n' \
    "$TOPO_NDOM" "$TOPO_NPRIM" "$TOPO_NCPU"
  local i
  for (( i = 0; i < TOPO_NDOM; i++ )); do
    printf '  domain %-2d  cores %-12s  +SMT %s\n' "$i" \
      "$(_topo_compact "${TOPO_DOM[$i]}")" "$(_topo_compact "${TOPO_DOM_ALL[$i]}")"
  done
  local want="${N_JOBS:-0}"
  [[ "$want" == 0 || "$want" == auto ]] && want=$TOPO_NDOM
  topo_build_slots "${PIN_MODE:-ccx}" "$want"
  printf '\nPIN_MODE=%s  N_JOBS=%s\n' "${PIN_MODE:-ccx}" "$PIN_NJOBS"
  [[ -n "$PIN_WARN" ]] && printf 'WARNING: %s\n' "$PIN_WARN"
  if [[ ${#PIN_SETS[@]} -eq 0 ]]; then
    printf '  (no pinning)\n'; return
  fi
  printf '\n  slot  taskset -c\n'
  for (( i = 0; i < ${#PIN_SETS[@]}; i++ )); do
    printf '  %-4d  %s\n' "$((i + 1))" "$(_topo_compact "${PIN_SETS[$i]}")"
  done
}

# Standalone invocation: print the table and exit.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  set -uo pipefail
  topo_print
fi
