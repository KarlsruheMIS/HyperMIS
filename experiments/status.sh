#!/bin/bash
# Read-only status of the whole experiment pipeline: for every output file it
# shows how many (graph,seed) groups are complete, how many are still missing,
# and how many failures were logged -- so you can see what a rerun would do
# before launching it. Usage: experiments/status.sh
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"
source "$HERE/lib.sh"

nfull=$(ls "$HG_FULL" 2>/dev/null | wc -l)
nsolv=$(ls "$HG_SOLVABLE" 2>/dev/null | wc -l)
nseed=${#SEEDS[@]}

# name | relative-file | expect | instance-set(full|solvable)
EXPERIMENTS=(
  "nred|RED/nred.tsv|1|full"
  "fred|RED/fred.tsv|1|full"
  "red|RED/red.tsv|1|full"
  "gred|RED/gred.tsv|1|full"
  "clique|RED/clique.tsv|1|full"
  "stats|RED/stats.tsv|any|full"
  "config_stats|RED/config_stats.tsv|sub:${#CONFIGS[@]}|full"
  "ilp|ILP/ilp.tsv|1|solvable"
  "rilp|ILP/rilp.tsv|1|solvable"
  "nrilp|ILP/nrilp.tsv|1|solvable"
  "frilp|ILP/frilp.tsv|1|solvable"
  "gfilp|ILP/gfilp.tsv|1|solvable"
  "gfrilp|ILP/gfrilp.tsv|1|solvable"
  "grilp|ILP/grilp.tsv|1|solvable"
  "bmatching|GRAPH/bmatching.tsv|1|solvable"
  "rbmatching|GRAPH/rbmatching.tsv|1|solvable"
  "struction|GRAPH/struction.tsv|1|solvable"
  "rstruction|GRAPH/rstruction.tsv|1|solvable"
  "vc_solver|GRAPH/vc_solver.tsv|1|solvable"
  "rvc_solver|GRAPH/rvc_solver.tsv|1|solvable"
  "satreduce|GRAPH/satreduce.tsv|1|solvable"
  "rsatreduce|GRAPH/rsatreduce.tsv|1|solvable"
)

# Directory holding the instances of a set name (full|solvable).
_set_dir() { [[ "$1" == full ]] && echo "$HG_FULL" || echo "$HG_SOLVABLE"; }

# Both counters below IGNORE rows whose graph is not in the experiment's declared
# instance set. Result files outlive the instance set they were produced from
# (e.g. the ILP/GRAPH files still carry rows for the 75-instance full set, from
# before the solvable subset was cut down to 52). Counting those rows made
# PRESENT exceed TOTAL, which clamped MISSING to 0 and reported COMPLETE without
# ever checking that the instances actually in the set were covered.
# Each prints "present<TAB>stale": stale = well-formed rows for out-of-set graphs.

# Number of complete (graph,seed) groups in a file (>= threshold rows each).
count_present() {
  local f="$1" expect="$2" set="$3"
  [[ -s "$f" ]] || { printf '0\t0\n'; return; }
  local hdr gc sc nc thr
  hdr=$(head -1 "$f"); nc=$(_ncols "$f")
  gc=$(_col_index "$hdr" graph); sc=$(_col_index "$hdr" seed)
  [[ -z "$gc" || -z "$sc" ]] && { printf '0\t0\n'; return; }
  [[ "$expect" == any ]] && thr=1 || thr=$expect
  awk -F'\t' -v gc="$gc" -v sc="$sc" -v nc="$nc" -v thr="$thr" '
    NR==FNR { inset[$0]=1; next }
    FNR>1 && NF==nc { g=$gc; s=$sc; gsub(/^[ \t]+|[ \t]+$/,"",g); gsub(/^[ \t]+|[ \t]+$/,"",s);
      if (!(g in inset)) { stale++; next }
      c[g SUBSEP s]++ }
    END { p=0; for (k in c) if (c[k] >= thr) p++; printf "%d\t%d\n", p, stale+0 }
  ' <(ls -1 "$(_set_dir "$set")" 2>/dev/null) "$f"
}

# Number of distinct (graph,seed,algo) well-formed rows -- for files where each
# config is its own resumable unit (config_stats).
count_present_sub() {
  local f="$1" set="$2"
  [[ -s "$f" ]] || { printf '0\t0\n'; return; }
  local hdr gc sc kc nc
  hdr=$(head -1 "$f"); nc=$(_ncols "$f")
  gc=$(_col_index "$hdr" graph); sc=$(_col_index "$hdr" seed); kc=$(_col_index "$hdr" algo)
  [[ -z "$gc" || -z "$sc" || -z "$kc" ]] && { printf '0\t0\n'; return; }
  awk -F'\t' -v gc="$gc" -v sc="$sc" -v kc="$kc" -v nc="$nc" '
    NR==FNR { inset[$0]=1; next }
    FNR>1 && NF==nc { g=$gc; s=$sc; k=$kc;
      gsub(/^[ \t]+|[ \t]+$/,"",g); gsub(/^[ \t]+|[ \t]+$/,"",s); gsub(/^[ \t]+|[ \t]+$/,"",k);
      if (!(g in inset)) { stale++; next }
      seen[g SUBSEP s SUBSEP k]=1 }
    END { printf "%d\t%d\n", length(seen), stale+0 }
  ' <(ls -1 "$(_set_dir "$set")" 2>/dev/null) "$f"
}

# fail_count EXPERIMENT REASON SET
# Like the row counters above, this ignores failures logged for graphs outside
# the experiment's current instance set. FAILURES.tsv is append-only and outlives
# any instance set, so without the filter a block over 2 instances could report
# 36 OOMs -- all of them for graphs it no longer runs on.
fail_count() {
  [[ -s "$FAILURES" ]] || { echo 0; return; }
  awk -F'\t' -v e="$2" -v r="$3" '
    NR==FNR { inset[$0]=1; next }
    FNR>1 && $2==e && $7==r && ($3 in inset) { c++ }
    END { print c+0 }
  ' <(ls -1 "$(_set_dir "$1")" 2>/dev/null) "$FAILURES"
}

printf '%-14s %-11s %8s %8s %8s %6s %6s %6s  %s\n' \
       EXPERIMENT SET TOTAL PRESENT MISSING OOM ERR TMO STATUS
printf '%s\n' "--------------------------------------------------------------------------------"
any_missing=0
declare -a stale_notes=()
for row in "${EXPERIMENTS[@]}"; do
  IFS='|' read -r name rel expect set <<<"$row"
  file="$RES/$rel"
  if [[ "$set" == full ]]; then ninst=$nfull; else ninst=$nsolv; fi
  if [[ "$expect" == sub:* ]]; then
    nsub=${expect#sub:}
    total=$(( ninst * nseed * nsub ))
    IFS=$'\t' read -r present stale < <(count_present_sub "$file" "$set")
  else
    total=$(( ninst * nseed ))
    IFS=$'\t' read -r present stale < <(count_present "$file" "$expect" "$set")
  fi
  (( stale > 0 )) && stale_notes+=("$name: $stale row(s) for graphs no longer in the $set set")
  missing=$(( total - present )); (( missing < 0 )) && missing=0
  oom=$(fail_count "$set" "$name" oom); err=$(fail_count "$set" "$name" error); tmo=$(fail_count "$set" "$name" timeout)
  if [[ ! -s "$file" ]]; then st="- (no file)";
  elif [[ "$missing" -eq 0 ]]; then st="COMPLETE";
  else st="INCOMPLETE"; any_missing=1; fi
  printf '%-14s %-11s %8s %8s %8s %6s %6s %6s  %s\n' \
         "$name" "$set" "$total" "$present" "$missing" "$oom" "$err" "$tmo" "$st"
done
printf '%s\n' "--------------------------------------------------------------------------------"
echo "instances: full=$nfull  solvable=$nsolv   seeds=$nseed"
if (( ${#stale_notes[@]} > 0 )); then
  echo
  echo "Rows ignored (graph not in the experiment's instance set -- harmless leftovers"
  echo "from an earlier, larger set; they are counted by no column above):"
  printf '  %s\n' "${stale_notes[@]}"
fi
if [[ -s "$FAILURES" ]]; then
  echo
  echo "Last failures ($FAILURES):"
  tail -n 5 "$FAILURES" | awk -F'\t' '{printf "  %s  %-12s %-30s seed=%-4s %s: %s\n",$1,$2,$3,$5,$7,substr($8,1,60)}'
fi
[[ "$any_missing" -eq 1 ]] && echo || echo "All output files complete."
