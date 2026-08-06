#!/bin/bash
# KaMIS graph reductions on the clique expansion, and the ILP on top. Resumable.
#   gred.tsv  : KaMIS graph reductions on the clique expansion (graph_reduction_
#               comparison -r15) over the FULL set.
#   grilp.tsv : graph-ILP after those reductions (-i) over the solvable subset.
# A non-zero exit (e.g. the -i verify failure) discards the line and is logged,
# so a wrong solution never lands in the results.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"
source "$HERE/lib.sh"
init_mem
init_failures

# graph_reduction_comparison emits TWO different line shapes (see its --help):
#   default : name graphred<rules> n m gn gm rn rm offset time seed
#   -i      : name ILP size time opt seed
# so the two blocks need two different headers. GROW selects which one is in
# force; every row this script writes -- real or sentinel -- must match it,
# because purge_incomplete DELETES rows whose column count differs from the
# header's (a mismatch silently discards the whole block on the next rerun).
GRC_H='graph\talgo\tn\tm\tgn\tgm\trn\trm\toffset\ttime\tseed\tmem'
ILP_H='graph\talgo\tsize\ttime\topt\tseed\tmem'

# The binary labels its own line "graphred<rules>"; mirror that for failure rows
# so a filter on the algo column sees them too.
_grc_label() {
  [[ "$GROW" == ilp ]] && { printf 'ILP'; return; }
  local r; r=$(sed -n 's/.*-r *\([0-9]\{1,\}\).*/\1/p' <<<"$GFLAGS")
  printf 'graphred%s' "${r:-15}"
}

# Failure row in the shape GROW demands. The result columns carry the sentinel;
# time/mem are what was measured before the job died. args: name seed sentinel
_emit_grc_fail_row() {
  if [[ "$GROW" == ilp ]]; then     # graph algo size time opt seed mem
    printf '%s\t%s\t%s\t%s\t0\t%s\t%s\n' \
      "$1" "$(_grc_label)" "$3" "$_C_ELAPSED" "$2" "$_C_MEM"
  else                              # graph algo n m gn gm rn rm offset time seed mem
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$1" "$(_grc_label)" "$3" "$3" "$3" "$3" "$3" "$3" "$3" "$_C_ELAPSED" "$2" "$_C_MEM"
  fi
}

# Uses exported GFLAGS / EXP. graph_reduction_comparison prints its own labelled
# line; we append peak memory. A failed run still emits a sentinel row, so the
# (graph,seed) group counts as settled and is not retried on every rerun --
# without it an instance that reliably blows MEM_LIMIT is attempted forever.
w_grc() {
  local seed="$1" inst="$2" name; name=$(basename "$inst")
  run_capture 0 -- "$GRC" $GFLAGS -g "$inst" -s "$seed"
  case "$_C_STATUS" in
    SUCCESS) printf '%s\t%s\n' "$_C_OUT" "$_C_MEM" ;;
    OOM)     _emit_grc_fail_row "$name" "$seed" "$OOM_SIZE"
             record_failure "$EXP" graph "$seed" "$name" "$_C_RC" oom   "$_C_ERR" ;;
    TIMEOUT) _emit_grc_fail_row "$name" "$seed" "$CRASH_SIZE"
             record_failure "$EXP" graph "$seed" "$name" "$_C_RC" timeout "$_C_ERR" ;;
    *)       _emit_grc_fail_row "$name" "$seed" "$CRASH_SIZE"
             record_failure "$EXP" graph "$seed" "$name" "$_C_RC" error "$_C_ERR" ;;
  esac
}
export -f w_grc _grc_label _emit_grc_fail_row

mapfile -t FULL     < <(ls "$HG_FULL"/*)
mapfile -t SOLVABLE < <(ls "$HG_SOLVABLE"/*)

echo "== Graph reductions on the clique expansion (gred) =="
export EXP=gred GFLAGS="-r15 -k $K" GROW=grc
run_block gred "$RES/RED/gred.tsv" "$GRC_H" 1 w_grc SEEDS "${FULL[@]}"

echo "== Graph-ILP after graph reductions (grilp) =="
export EXP=grilp GFLAGS="-i -r15 -k 100 -t $K" GROW=ilp
run_block grilp "$RES/ILP/grilp.tsv" "$ILP_H" 1 w_grc SEEDS "${SOLVABLE[@]}"

echo "All requested blocks complete."
