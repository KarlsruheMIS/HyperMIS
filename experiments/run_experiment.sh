#!/bin/bash
# Reduction experiments (nred/fred/red) + clique blow-up on the FULL set, and the
#   ILP / graph-ILP blocks (ilp, rilp, nrilp, frilp, gfilp, gfrilp) on the
#   ILP-solvable subset. Rerun any time: only the missing
#   (instance,seed) runs execute; OOM/crashes are recorded in results/FAILURES.tsv.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"
source "$HERE/lib.sh"
init_mem
init_failures

RED_H='graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed\tmem'
ILP_H='graph\talgo\tsize\ttime\topt\tseed\tmem'
# clique_blowup emits its own 9-field line (no time/mem): original vs reduced
# hyperedge counts (m/rm) and their clique-expansion edge counts (gm/rgm).
CLIQUE_H='graph\talgo\tn\tm\trn\trm\tgm\trgm\tseed'

# ---- workers --------------------------------------------------------------
# Reduction worker. Uses exported RFLAGS / EXP. No natural null row for a
# reduction OOM, so OOM (like any error) is logged and retried on the next run.
w_reduce() {
  local seed="$1" inst="$2" name; name=$(basename "$inst")
  run_capture 0 -- "$RUN_REDUCE" $RFLAGS -g "$inst" -t "$T" -s "$seed"
  case "$_C_STATUS" in
    SUCCESS) printf '%s\t%s\n' "$_C_OUT" "$_C_MEM" ;;
    OOM)     record_failure "$EXP" reduce "$seed" "$name" "$_C_RC" oom   "$_C_ERR" ;;
    *)       record_failure "$EXP" reduce "$seed" "$name" "$_C_RC" error "$_C_ERR" ;;
  esac
}
# ILP worker. Uses exported IFLAGS / EXP. OOM -> a schema-valid unsolved row
# (size 0, opt 0) so it counts as done and is not retried; other errors log+retry.
w_ilp() {
  local seed="$1" inst="$2" name; name=$(basename "$inst")
  run_capture 0 -- "$RUN_ILP" $IFLAGS -g "$inst" -t "$T" -s "$seed"
  case "$_C_STATUS" in
    SUCCESS) printf '%s\t%s\n' "$_C_OUT" "$_C_MEM" ;;
    OOM)     printf '%s\tILP\t0\t%s\t0\t%s\t%s\n' "$name" "$T" "$seed" "$_C_MEM"
             record_failure "$EXP" ILP "$seed" "$name" "$_C_RC" oom "$_C_ERR" ;;
    *)       record_failure "$EXP" ILP "$seed" "$name" "$_C_RC" error "$_C_ERR" ;;
  esac
}
# Clique-blow-up worker. Same interface as run_reduce (-d on-demand neighborhoods,
# -r8 = full pipeline, -k kernel budget); the app already prints a schema-valid line,
# so on success we pass it through verbatim. No natural null row, so OOM/crash is
# logged and retried on the next run (like w_reduce).
w_clique() {
  local seed="$1" inst="$2" name; name=$(basename "$inst")
  run_capture 0 -- "$CLIQUE_BLOWUP" -d -r8 -g "$inst" -k "$K" -s "$seed"
  case "$_C_STATUS" in
    SUCCESS) printf '%s\n' "$_C_OUT" ;;
    OOM)     record_failure clique clique "$seed" "$name" "$_C_RC" oom   "$_C_ERR" ;;
    *)       record_failure clique clique "$seed" "$name" "$_C_RC" error "$_C_ERR" ;;
  esac
}
export -f w_reduce w_ilp w_clique

mapfile -t FULL     < <(ls "$HG_FULL"/*)
mapfile -t SOLVABLE < <(ls "$HG_SOLVABLE"/*)

# ---- reductions (full set) -----------------------------------------------
echo "== Reduction experiments =="
export EXP=nred RFLAGS='-n'
run_block nred "$RES/RED/nred.tsv" "$RED_H" 1 w_reduce SEEDS "${FULL[@]}"
export EXP=fred RFLAGS='-d'
run_block fred "$RES/RED/fred.tsv" "$RED_H" 1 w_reduce SEEDS "${FULL[@]}"
export EXP=red  RFLAGS=''
run_block red  "$RES/RED/red.tsv"  "$RED_H" 1 w_reduce SEEDS "${FULL[@]}"

# ---- clique blow-up (full set) -------------------------------------------
echo "== Clique blow-up experiments =="
run_block clique "$RES/RED/clique.tsv" "$CLIQUE_H" 1 w_clique SEEDS "${FULL[@]}"

# ---- graph ILP (solvable subset) -----------------------------------------
echo "== Graph-ILP experiments =="
export EXP=gfilp  IFLAGS='-d -r0 -e'
run_block gfilp  "$RES/ILP/gfilp.tsv"  "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
export EXP=gfrilp IFLAGS='-d -e'
run_block gfrilp "$RES/ILP/gfrilp.tsv" "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"

# ---- ILP / reduced-ILP (solvable subset) ----------------------------------
# Already complete; resumable, so these skip finished runs and only fill gaps.
echo "== ILP experiments =="
export EXP=ilp   IFLAGS='-r0';     run_block ilp   "$RES/ILP/ilp.tsv"   "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
echo "== Reduced-ILP experiments =="
export EXP=rilp  IFLAGS='';        run_block rilp  "$RES/ILP/rilp.tsv"  "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
export EXP=nrilp IFLAGS='-n';      run_block nrilp "$RES/ILP/nrilp.tsv" "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
export EXP=frilp IFLAGS='-d';      run_block frilp "$RES/ILP/frilp.tsv" "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"

echo "All requested blocks complete."
