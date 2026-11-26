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
# Conversion running time, raw vs reduced, for the conversions the solve phases
# use: clique expansion (graph solvers, graph-ILP) and transpose (b-matching).
# Nothing is solved. Every *_time is the in-memory construction ONLY -- no
# reading the input, no writing the output:
#   cexp_time / rcexp_time  clique expansion (adjacency lists) of the raw
#                           hypergraph / of the kernel   (hypergraph_to_graph)
#   tr_time   / rtr_time    transpose (dual incidence lists) of the raw
#                           hypergraph / of the kernel   (transpose_hgr.py)
#   red_time                the HyperMIS reductions producing the kernel (budget KS,
#                           as in every solve phase)
# gm/rgm: edges of the raw/kernel clique expansion. mem/rmem: peak KiB of the raw /
# reduced clique-expansion process. A step that hits MEM_LIMIT is NA (a real
# outcome: that conversion does not fit), and the row still counts as done.
CONV_H='graph\talgo\tn\tm\trn\trm\tgm\trgm\tred_time\tcexp_time\trcexp_time\ttr_time\trtr_time\tseed\tmem\trmem'

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
  run_capture 0 -- "$RUN_ILP" $IFLAGS -g "$inst" -t "$T" -k "$KS" -s "$seed"
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
# Conversion-cost worker. Each step is its own process, so an OOM in one (e.g. the
# raw clique expansion) leaves the others measured.
w_conv() {
  local seed="$1" inst="$2" name n=NA m=NA rn=NA rm=NA gm=NA rgm=NA red=NA
  local cexp=NA rcexp=NA tr=NA rtr=NA mem=NA rmem=NA dir kern
  name=$(basename "$inst")
  dir=$(mktemp -d "$SCRATCH_BASE/conv.XXXXXX"); kern="$dir/kernel.hgr"
  # $1: step name. OOM -> leave NA and log; any other failure -> log, no row.
  _conv_fail() {
    if [[ "$_C_STATUS" == OOM ]]; then
      record_failure conversion "$1" "$seed" "$name" "$_C_RC" oom "$_C_ERR"; return 0
    fi
    record_failure conversion "$1" "$seed" "$name" "$_C_RC" error "$_C_ERR"; rm -rf "$dir"; return 1
  }

  run_capture 0 -- "$HG2G" -g "$inst" -o /dev/null
  if [[ "$_C_STATUS" == SUCCESS ]]; then
    read -r n gm cexp < <(awk '/ in time /{gsub(/[(),]/,""); print $(NF-7), $(NF-5), $(NF-3)}' <<<"$_C_OUT")
    mem=$_C_MEM
  else _conv_fail expand || return; fi

  run_capture 0 -- "$HG2G" -g "$inst" -d -s "$seed" -k "$KS" -o /dev/null
  if [[ "$_C_STATUS" == SUCCESS ]]; then
    read -r m rn rm rgm red rcexp < <(awk -F'\t' 'END{print $4, $6, $7, $12, $13, $14}' <<<"$_C_OUT")
    rmem=$_C_MEM
  else _conv_fail rexpand || return; fi

  run_capture 0 -- python3 "$TRANSPOSE" "$inst" /dev/null
  if [[ "$_C_STATUS" == SUCCESS ]]; then tr=$(awk 'END{print $2}' <<<"$_C_OUT")
  else _conv_fail transpose || return; fi

  # The kernel exactly as rbmatching gets it, then its transpose.
  run_capture 0 -- "$RUN_REDUCE" -g "$inst" -d -k "$KS" -s "$seed" -o "$kern"
  if [[ "$_C_STATUS" == SUCCESS ]]; then
    if [[ $(awk -F'\t' 'END{print $6+0}' <<<"$_C_OUT") -eq 0 ]]; then
      rtr=0                                   # empty kernel: nothing to convert
    else
      run_capture 0 -- python3 "$TRANSPOSE" "$kern" /dev/null
      if [[ "$_C_STATUS" == SUCCESS ]]; then rtr=$(awk 'END{print $2}' <<<"$_C_OUT")
      else _conv_fail rtranspose || return; fi
    fi
  else _conv_fail reduce || return; fi
  rm -rf "$dir"

  local _v
  for _v in "$n" "$gm" "$cexp" "$m" "$rn" "$rgm" "$red" "$rcexp" "$tr" "$rtr"; do
    [[ -n "$_v" ]] || { record_failure conversion parse "$seed" "$name" 0 error "unparsable output"; return; }
  done
  printf '%s\tconversion\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$n" "$m" "$rn" "$rm" "$gm" "$rgm" "$red" "$cexp" "$rcexp" "$tr" "$rtr" "$seed" "$mem" "$rmem"
}
export -f w_reduce w_ilp w_clique w_conv

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

# ---- conversion running time (solvable subset: the instances the solve phases use)
echo "== Conversion running time (clique expansion, transpose), raw vs reduced =="
run_block conversion "$RES/RED/conversion.tsv" "$CONV_H" 1 w_conv SEEDS "${SOLVABLE[@]}"

# ---- ILP / reduced-ILP (solvable subset) ----------------------------------
# Already complete; resumable, so these skip finished runs and only fill gaps.
echo "== ILP experiments =="
export EXP=ilp   IFLAGS='-r0';     run_block ilp   "$RES/ILP/ilp.tsv"   "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
echo "== Reduced-ILP experiments =="
export EXP=rilp  IFLAGS='';        run_block rilp  "$RES/ILP/rilp.tsv"  "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
export EXP=nrilp IFLAGS='-n';      run_block nrilp "$RES/ILP/nrilp.tsv" "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"
export EXP=frilp IFLAGS='-d';      run_block frilp "$RES/ILP/frilp.tsv" "$ILP_H" 1 w_ilp SEEDS "${SOLVABLE[@]}"

echo "All requested blocks complete."
