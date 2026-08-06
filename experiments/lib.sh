#!/bin/bash
# ---------------------------------------------------------------------------
# experiments/lib.sh -- resumable, failure-recording experiment engine.
#
# Sourced by every experiment script (after config.sh). Provides:
#   init_mem / init_failures      one-time setup
#   run_capture                   run one command, capture rc + peak mem + stderr,
#                                 classify SUCCESS|TIMEOUT|OOM|ERROR
#   record_failure                append a row to results/FAILURES.tsv
#   init_out / purge_incomplete / emit_pending   resume bookkeeping
#   run_block                     the resumable parallel driver (no -k, no --halt)
#
# Completion model: each output file is keyed on (graph = column 1, seed = the
# "seed" column). A (graph,seed) group is "complete" when it holds EXPECT rows
# (EXPECT = a number, or the literal "any" = at least one row). On rerun, only
# incomplete groups are (re)run; partially written groups are purged first so
# they are redone cleanly without duplicates.
# ---------------------------------------------------------------------------

# ======================= one-time setup ====================================

# Detect how to enforce MEM_LIMIT on THIS machine (called once at startup).
# Sets _MEM_MODE = systemd | prlimit | none  and _MEM_BYTES (for prlimit).
init_mem() {
  _MEM_MODE=none
  _MEM_BYTES=0
  if [[ -z "$MEM_LIMIT" ]]; then
    export _MEM_MODE _MEM_BYTES
    return
  fi
  if command -v systemd-run >/dev/null 2>&1 \
     && systemd-run --user --scope --quiet -p Description=rerun-memtest true >/dev/null 2>&1; then
    _MEM_MODE=systemd
  elif command -v prlimit >/dev/null 2>&1; then
    _MEM_MODE=prlimit
    _MEM_BYTES=$(_to_bytes "$MEM_LIMIT")
  else
    echo "experiments: MEM_LIMIT=$MEM_LIMIT set but neither 'systemd-run --user' nor" \
         "'prlimit' is usable -> running UNCAPPED." >&2
  fi
  [[ "$_MEM_MODE" != none ]] \
    && echo "experiments: per-job memory cap $MEM_LIMIT enforced via $_MEM_MODE" >&2
  export _MEM_MODE _MEM_BYTES
}

# Human size (200G, 512M, 1024) -> bytes.
_to_bytes() {
  local v="$1" n u
  n=${v//[^0-9.]/}
  u=${v//[0-9. ]/}
  case "${u^^}" in
    K|KB|KIB)  awk -v n="$n" 'BEGIN{printf "%.0f", n*1024}' ;;
    M|MB|MIB)  awk -v n="$n" 'BEGIN{printf "%.0f", n*1024*1024}' ;;
    G|GB|GIB)  awk -v n="$n" 'BEGIN{printf "%.0f", n*1024*1024*1024}' ;;
    T|TB|TIB)  awk -v n="$n" 'BEGIN{printf "%.0f", n*1024*1024*1024*1024}' ;;
    ""|B)      awk -v n="$n" 'BEGIN{printf "%.0f", n}' ;;
    *)         echo 0 ;;
  esac
}

# ---- shared result conventions --------------------------------------------
# A NEGATIVE value in a result column means the run terminated abnormally and
# produced no answer; a non-negative value is a real result, optimal or not.
# FAILURES.tsv carries the same classification in its "reason" column.
#
#   -1  the job DIED (crash / assertion abort / nonzero exit)
#   -2  the job was killed by the per-job MEM_LIMIT
#
# In both cases "time" is how long it ran before dying, so such rows must be
# excluded from -- not averaged into -- runtime or quality aggregates, and "mem"
# is 0 for a memory kill (/usr/bin/time dies with the job, so the true peak is
# unknown; it is ~MEM_LIMIT by definition).
#
# Negative sentinels are used because 0 is a legitimate result (an empty graph, a
# zero reduction offset), so it cannot mark a failure unambiguously. Emitting a
# row at all rather than nothing is what keeps the pipeline resumable: a crashed
# or OOM-killed instance is a settled outcome, not missing work to retry forever.
CRASH_SIZE=-1
OOM_SIZE=-2
export CRASH_SIZE OOM_SIZE

# Create the failures log with a header if it does not exist yet.
init_failures() {
  mkdir -p "$(dirname "$FAILURES")"
  [[ -s "$FAILURES" ]] || printf 'timestamp\texperiment\tgraph\talgo\tseed\texit_code\treason\tstderr\n' > "$FAILURES"
}

# ======================= per-job command runner ============================

# CPU mask for this job, as `taskset -c <set>` (or empty when pinning is off).
# PIN_SLOT is the GNU parallel job slot ({%}, 1..N_JOBS, unique among RUNNING
# jobs), PIN_SETS_STR the space-separated slot->cpuset table built by
# topology.sh. Indexing by slot rather than by job means the mask is stable for
# the whole run: slot 3 is always the same cores, whatever instance it holds.
_pin_prefix() {
  _C_PIN=()
  [[ -n "${PIN_SETS_STR:-}" && -n "${PIN_SLOT:-}" ]] || return 0
  local -a sets; read -ra sets <<<"$PIN_SETS_STR"
  (( ${#sets[@]} )) || return 0
  local idx=$(( (PIN_SLOT - 1) % ${#sets[@]} ))
  _C_PIN=(taskset -c "${sets[$idx]}")
}

# run_capture TIMEOUT -- cmd [args...]
#   TIMEOUT: 0 (or empty) = no external timeout; >0 = wrap in `timeout`.
# Runs cmd pinned to this slot's cores, under the memory cap (if any) and
# /usr/bin/time for wall time + peak RSS, keeping the command's real stderr
# separate from the time report. Globals:
#   _C_RC raw exit   _C_OUT stdout   _C_MEM peak RSS KB   _C_ELAPSED wall s
#   _C_ERR stderr snippet   _C_STATUS = SUCCESS | TIMEOUT | OOM | ERROR
run_capture() {
  local tmo="$1"; shift
  [[ "${1:-}" == "--" ]] && shift
  local memf errf rc out t0 t1
  memf=$(mktemp "${SCRATCH_BASE:-/tmp}/rc.mem.XXXXXX")
  errf=$(mktemp "${SCRATCH_BASE:-/tmp}/rc.err.XXXXXX")
  local -a pre=()
  [[ -n "$tmo" && "$tmo" != 0 ]] && pre=(timeout -k 10 "$tmo")
  # Innermost, so the affinity is set on the measured process itself and cannot
  # be reset by systemd-run's scope setup in between.
  local -a _C_PIN=(); _pin_prefix

  t0=$(date +%s.%N)
  case "$_MEM_MODE" in
    systemd)
      out=$("${pre[@]}" systemd-run --user --scope --quiet \
              -p MemoryMax="$MEM_LIMIT" -p MemorySwapMax=0 \
              "${_C_PIN[@]}" /usr/bin/time -o "$memf" -f "%e %M" "$@" 2>"$errf")
      rc=$? ;;
    prlimit)
      out=$("${pre[@]}" prlimit "--as=$_MEM_BYTES" \
              "${_C_PIN[@]}" /usr/bin/time -o "$memf" -f "%e %M" "$@" 2>"$errf")
      rc=$? ;;
    *)
      out=$("${pre[@]}" "${_C_PIN[@]}" /usr/bin/time -o "$memf" -f "%e %M" "$@" 2>"$errf")
      rc=$? ;;
  esac

  t1=$(date +%s.%N)

  _C_RC=$rc
  _C_OUT="$out"
  _C_WALL=$(awk -v a="$t0" -v b="$t1" 'BEGIN{d=b-a; if (d<0) d=0; printf "%.2f", d}')
  _C_ELAPSED=$(tail -1 "$memf" 2>/dev/null | awk 'NR==1{print $1+0}')
  _C_MEM=$(tail -1 "$memf" 2>/dev/null | awk 'NR==1{print $2+0}')
  [[ -z "$_C_ELAPSED" ]] && _C_ELAPSED=0
  [[ -z "$_C_MEM" ]] && _C_MEM=0
  # When the job is KILLED -- wall timeout, or a cgroup/prlimit OOM kill -- the
  # kill lands on /usr/bin/time itself, so it never writes its report and both
  # numbers come back 0. That is how a run that burned the full budget ended up
  # recorded as "0 seconds". Fall back to the shell-measured wall clock, which is
  # always available, so a killed run reports how long it really ran.
  # _C_MEM stays 0 in that case: the peak is genuinely unknown (for an OOM kill
  # it is ~MEM_LIMIT), and 0 is honest where a stale value would not be.
  awk -v e="$_C_ELAPSED" 'BEGIN{exit !(e+0==0)}' && _C_ELAPSED=$_C_WALL
  # stderr snippet: drop the lone "<elapsed> <mem>" time line, keep last 3 real lines.
  _C_ERR=$(grep -vxE '[0-9.]+ [0-9]+' "$errf" 2>/dev/null | tail -3 | tr '\n\t' '  ' | cut -c1-400)
  rm -f "$memf" "$errf"

  if [[ $rc -eq 0 ]]; then
    _C_STATUS=SUCCESS
  elif [[ -n "$tmo" && "$tmo" != 0 && $rc -eq 124 ]]; then
    _C_STATUS=TIMEOUT
  elif _is_oom "$rc"; then
    _C_STATUS=OOM
  else
    _C_STATUS=ERROR
  fi
}

# Was exit code an out-of-memory kill? When a per-job cap is active, a job that
# dies by a signal at the limit is an OOM. systemd's managed-OOM/cgroup kill
# arrives as SIGKILL(137) or SIGTERM(143); prlimit --as makes allocation fail,
# surfacing as abort(134)/segv(139). Without a cap, only a system OOM-kill (137).
_is_oom() {
  local rc="$1"
  case "$_MEM_MODE" in
    systemd) [[ "$rc" -eq 137 || "$rc" -eq 143 ]] && return 0 ;;
    prlimit) [[ "$rc" -eq 137 || "$rc" -eq 134 || "$rc" -eq 139 ]] && return 0 ;;
    *)       [[ "$rc" -eq 137 ]] && return 0 ;;
  esac
  return 1
}

# record_failure EXPERIMENT ALGO SEED GRAPH EXIT_CODE REASON [SNIPPET]
# Appends one line (small enough to be an atomic O_APPEND write under -j jobs).
record_failure() {
  local ts
  ts=$(date '+%Y-%m-%d %H:%M:%S')
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$ts" "$1" "$4" "$2" "$3" "$5" "$6" "${7:-}" >> "$FAILURES"
}

# ======================= resume bookkeeping ================================

# 1-based index of a named column in a TAB header line (trims whitespace).
_col_index() {
  awk -F'\t' -v name="$2" '
    { for (i=1;i<=NF;i++) { h=$i; gsub(/^[ \t]+|[ \t]+$/,"",h); if (h==name) { print i; exit } } }
  ' <<<"$1"
}

# Column count of the header line.
_ncols() { awk -F'\t' 'NR==1{print NF; exit}' "$1"; }

# init_out FILE HEADER  -- create with header only if absent/empty (never truncate).
init_out() {
  local f="$1" hdr="$2"
  mkdir -p "$(dirname "$f")"
  [[ -s "$f" ]] || printf '%b\n' "$hdr" > "$f"
}

# purge_incomplete FILE EXPECT
# Drops malformed rows (wrong column count) and any (graph,seed) group holding
# fewer than EXPECT rows, so partially written groups are cleanly redone.
# EXPECT="any" keeps every well-formed row (only garbage is dropped).
purge_incomplete() {
  local f="$1" expect="$2"
  [[ -s "$f" ]] || return 0
  local hdr gc sc nc
  hdr=$(head -1 "$f")
  nc=$(_ncols "$f")
  gc=$(_col_index "$hdr" graph)
  sc=$(_col_index "$hdr" seed)
  if [[ -z "$gc" || -z "$sc" ]]; then
    echo "experiments: WARNING cannot locate graph/seed columns in $f -- skipping purge" >&2
    return 0
  fi
  local tmp="$f.purge.$$"
  if [[ "$expect" == any ]]; then
    awk -F'\t' -v nc="$nc" 'NR==1{print; next} NF==nc{print}' "$f" > "$tmp" && mv "$tmp" "$f"
    return
  fi
  awk -F'\t' -v gc="$gc" -v sc="$sc" -v nc="$nc" -v expect="$expect" '
    NR==1 { print; next }
    NF!=nc { next }
    { g=$gc; s=$sc; gsub(/^[ \t]+|[ \t]+$/,"",g); gsub(/^[ \t]+|[ \t]+$/,"",s);
      k=g SUBSEP s; cnt[k]++; n++; L[n]=$0; K[n]=k }
    END { for (i=1;i<=n;i++) if (cnt[K[i]] >= expect) print L[i] }
  ' "$f" > "$tmp" && mv "$tmp" "$f"
}

# emit_pending FILE EXPECT SEEDS_ARRAY_NAME INST...
# Prints TAB-separated "seed<TAB>instance_path" for every combination whose
# (graph,seed) group is not yet complete. Pipe into `parallel --colsep '\t'`.
emit_pending() {
  local f="$1" expect="$2" seedsname="$3"; shift 3
  local -n _seeds="$seedsname"
  local thr; [[ "$expect" == any ]] && thr=1 || thr=$expect
  declare -A cnt=()
  if [[ -s "$f" ]]; then
    local hdr gc sc nc
    hdr=$(head -1 "$f"); nc=$(_ncols "$f")
    gc=$(_col_index "$hdr" graph); sc=$(_col_index "$hdr" seed)
    if [[ -n "$gc" && -n "$sc" ]]; then
      local g s
      while IFS=$'\t' read -r g s; do
        cnt["$g|$s"]=$(( ${cnt["$g|$s"]:-0} + 1 ))
      done < <(awk -F'\t' -v gc="$gc" -v sc="$sc" -v nc="$nc" '
                 NR>1 && NF==nc { g=$gc; s=$sc;
                   gsub(/^[ \t]+|[ \t]+$/,"",g); gsub(/^[ \t]+|[ \t]+$/,"",s);
                   print g"\t"s }' "$f")
    fi
  fi
  local inst g s
  for inst in "$@"; do
    g=$(basename "$inst")
    for s in "${_seeds[@]}"; do
      (( ${cnt["$g|$s"]:-0} < thr )) && printf '%s\t%s\n' "$s" "$inst"
    done
  done
}

# ---- sub-keyed resume: one row per (graph, seed, <subkey column>) ----------
# For files that interleave several algos per (graph,seed), where each
# algo/(graph,seed) is an INDEPENDENTLY resumable unit (config_stats: one config
# per algo=reduce<c>, c in CONFIGS, one row each). Key = (graph, seed, KEYCOL value),
# EXPECT = 1 row per key. This lets you delete a subset of configs and have ONLY
# those rerun -- the folded (graph,seed)-only key cannot express that.

# purge_incomplete_sub FILE KEYCOL
# Drops malformed rows (wrong column count) and de-duplicates by
# (graph,seed,KEYCOL); a single row already is a complete unit, so nothing else
# is purged.
purge_incomplete_sub() {
  local f="$1" keycol="$2"
  [[ -s "$f" ]] || return 0
  local hdr gc sc kc nc
  hdr=$(head -1 "$f"); nc=$(_ncols "$f")
  gc=$(_col_index "$hdr" graph); sc=$(_col_index "$hdr" seed); kc=$(_col_index "$hdr" "$keycol")
  if [[ -z "$gc" || -z "$sc" || -z "$kc" ]]; then
    echo "experiments: WARNING cannot locate graph/seed/$keycol columns in $f -- skipping purge" >&2
    return 0
  fi
  local tmp="$f.purge.$$"
  awk -F'\t' -v gc="$gc" -v sc="$sc" -v kc="$kc" -v nc="$nc" '
    NR==1 { print; next }
    NF!=nc { next }
    { g=$gc; s=$sc; k=$kc;
      gsub(/^[ \t]+|[ \t]+$/,"",g); gsub(/^[ \t]+|[ \t]+$/,"",s); gsub(/^[ \t]+|[ \t]+$/,"",k);
      key=g SUBSEP s SUBSEP k; if (seen[key]++) next; print }
  ' "$f" > "$tmp" && mv "$tmp" "$f"
}

# emit_pending_sub FILE KEYCOL KEYPREFIX SEEDS_ARRAY_NAME SUBKEYS_ARRAY_NAME INST...
# For every (inst, seed, subkey) whose (graph, seed, KEYPREFIX<subkey>) row is
# absent, print TAB-separated "seed<TAB>instance_path<TAB>subkey".
emit_pending_sub() {
  local f="$1" keycol="$2" prefix="$3" seedsname="$4" subname="$5"; shift 5
  local -n _seeds="$seedsname"
  local -n _subs="$subname"
  declare -A have=()
  if [[ -s "$f" ]]; then
    local hdr gc sc kc nc
    hdr=$(head -1 "$f"); nc=$(_ncols "$f")
    gc=$(_col_index "$hdr" graph); sc=$(_col_index "$hdr" seed); kc=$(_col_index "$hdr" "$keycol")
    if [[ -n "$gc" && -n "$sc" && -n "$kc" ]]; then
      local g s k
      while IFS=$'\t' read -r g s k; do
        have["$g|$s|$k"]=1
      done < <(awk -F'\t' -v gc="$gc" -v sc="$sc" -v kc="$kc" -v nc="$nc" '
                 NR>1 && NF==nc { g=$gc; s=$sc; k=$kc;
                   gsub(/^[ \t]+|[ \t]+$/,"",g); gsub(/^[ \t]+|[ \t]+$/,"",s); gsub(/^[ \t]+|[ \t]+$/,"",k);
                   print g"\t"s"\t"k }' "$f")
    fi
  fi
  local inst g s c
  for inst in "$@"; do
    g=$(basename "$inst")
    for s in "${_seeds[@]}"; do
      for c in "${_subs[@]}"; do
        [[ -n "${have["$g|$s|$prefix$c"]:-}" ]] || printf '%s\t%s\t%s\n' "$s" "$inst" "$c"
      done
    done
  done
}

# ======================= the resumable driver ==============================

# run_block NAME OUT HEADER EXPECT WORKER SEEDS_ARRAY_NAME INST...
# Ensures the header, purges partial groups, then runs only the missing
# (seed,instance) combos through `parallel` (no -k so lines flush as they
# finish; no --halt so one failure never stops the batch), appending to OUT.
run_block() {
  local name="$1" out="$2" hdr="$3" expect="$4" worker="$5" seedsname="$6"; shift 6
  local -n _s="$seedsname"
  local ninst=$#
  local total=$(( ${#_s[@]} * ninst ))

  # FORCE=1 -> rerun this block from scratch: back up any existing data, then
  # reset the file to just its header.
  if [[ "${FORCE:-0}" == 1 && -s "$out" && $(wc -l <"$out") -gt 1 ]]; then
    local bak="$out.bak.$(date +%Y%m%d-%H%M%S)"
    cp "$out" "$bak"
    printf '%b\n' "$hdr" > "$out"
    echo ">> [$name] FORCE: backed up previous results to $(basename "$bak"); rerunning all."
  fi

  init_out "$out" "$hdr"
  purge_incomplete "$out" "$expect"

  local list npend
  list=$(emit_pending "$out" "$expect" "$seedsname" "$@")
  npend=$(grep -c . <<<"$list"); [[ -z "$list" ]] && npend=0

  echo ">> [$name]  $((total - npend))/$total groups already complete, running $npend  ->  $(basename "$out")"
  if [[ "$npend" -eq 0 ]]; then
    echo "   [$name] nothing to do."
    return 0
  fi

  printf '%s\n' "$list" \
    | parallel -j "$N_JOBS" --noswap --delay 1 --colsep '\t' \
        "PIN_SLOT={%}" "$worker" {1} {2} >> "$out"

  _block_report "$name" "$out"
}

# run_block_sub NAME OUT HEADER KEYCOL KEYPREFIX WORKER SEEDS_ARRAY_NAME SUBKEYS_ARRAY_NAME INST...
# Like run_block, but each resumable unit is a (graph, seed, subkey) triple
# (one row keyed on the KEYCOL column, value KEYPREFIX<subkey>). The worker is
# invoked as WORKER <seed> <instance> <subkey>. Only absent triples run, so
# deleting a subset of subkeys reruns exactly those.
run_block_sub() {
  local name="$1" out="$2" hdr="$3" keycol="$4" prefix="$5" worker="$6" seedsname="$7" subname="$8"; shift 8
  local -n _s="$seedsname"; local -n _sub="$subname"
  local ninst=$#
  local total=$(( ${#_s[@]} * ${#_sub[@]} * ninst ))

  if [[ "${FORCE:-0}" == 1 && -s "$out" && $(wc -l <"$out") -gt 1 ]]; then
    local bak="$out.bak.$(date +%Y%m%d-%H%M%S)"
    cp "$out" "$bak"
    printf '%b\n' "$hdr" > "$out"
    echo ">> [$name] FORCE: backed up previous results to $(basename "$bak"); rerunning all."
  fi

  init_out "$out" "$hdr"
  purge_incomplete_sub "$out" "$keycol"

  local list npend
  list=$(emit_pending_sub "$out" "$keycol" "$prefix" "$seedsname" "$subname" "$@")
  npend=$(grep -c . <<<"$list"); [[ -z "$list" ]] && npend=0

  echo ">> [$name]  $((total - npend))/$total units already complete, running $npend  ->  $(basename "$out")"
  if [[ "$npend" -eq 0 ]]; then
    echo "   [$name] nothing to do."
    return 0
  fi

  printf '%s\n' "$list" \
    | parallel -j "$N_JOBS" --noswap --delay 1 --colsep '\t' \
        "PIN_SLOT={%}" "$worker" {1} {2} {3} >> "$out"

  _block_report "$name" "$out"
}

# Post-run one-line report: present rows and any failures logged for this block.
_block_report() {
  local name="$1" out="$2"
  local rows fails
  rows=$(($(wc -l < "$out") - 1))
  fails=0
  [[ -s "$FAILURES" ]] && fails=$(awk -F'\t' -v e="$name" 'NR>1 && $2==e{c++} END{print c+0}' "$FAILURES")
  echo "   [$name] done. $rows result rows in $(basename "$out"); $fails failure record(s) for this experiment (see FAILURES.tsv)."
}

# Export everything the worker functions need when GNU parallel spawns them.
export -f run_capture _is_oom record_failure _pin_prefix
