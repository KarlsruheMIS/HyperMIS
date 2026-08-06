#!/bin/bash
#   Graph MIS solvers (KaMIS struction, WeGotYouCovered vc_solver, SAT-and-Reduce
#   vc-bnb) on the clique expansion of each hypergraph, once raw and once after
#   HyperMIS reductions. One TSV line per (instance,seed); resumable + failure log.
#   Which blocks run is controlled at the bottom of this file.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"
source "$HERE/lib.sh"
init_mem
init_failures

mkdir -p "$SCRATCH_BASE" "$RES/GRAPH"
TMP=$(mktemp -d "$SCRATCH_BASE/graph_solver.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
export TMP

HDR='graph\talgo\tsize\ttime\topt\tseed\tmem'

imax() { awk -v a="$1" -v b="$2" 'BEGIN{print (a+0>b+0)?a+0:b+0}'; }

# Build the clique-expansion graph. Sets _BG_OFFSET _BG_RTIME _BG_CMEM and the
# run_capture globals; returns non-zero (and leaves _C_STATUS) on failure.
build_graph() {
  local inst="$1" seed="$2" mode="$3" graph="$4"
  if [[ "$mode" == reduce ]]; then
    run_capture 0 -- "$HG2G" -g "$inst" -d -s "$seed" -t "$T" -o "$graph"
    _BG_OFFSET=$(awk -F'\t' 'END{print $9+0}'  <<<"$_C_OUT")
    _BG_RTIME=$(awk  -F'\t' 'END{print $10+0}' <<<"$_C_OUT")
  else
    run_capture 0 -- "$HG2G" -g "$inst" -o "$graph"
    _BG_OFFSET=0; _BG_RTIME=0
  fi
  _BG_CMEM=$_C_MEM
  [[ "$_C_STATUS" == SUCCESS ]]
}

# METIS (weighted) -> PACE DIMACS (skip weight token, one edge u<v, 1-indexed).
to_dimacs() {
  awk 'NR==1{print "p td "$1" "$2; next}{u=NR-1; for(i=2;i<=NF;i++) if(u<$i) print u" "$i}' "$1" > "$2"
}

# Failure-row conventions (CRASH_SIZE / OOM_SIZE) are defined in lib.sh.

# Emit the sentinel row for a solver that died. args: name algo seed
_emit_crash_row() {
  printf '%s\t%s\t%s\t%s\t0\t%s\t%s\n' "$1" "$2" "$CRASH_SIZE" "$_C_ELAPSED" "$3" "$_C_MEM"
}

# Emit the sentinel row for a run killed by the memory cap. args: name algo seed
_emit_oom_row() {
  printf '%s\t%s\t%s\t%s\t0\t%s\t%s\n' "$1" "$2" "$OOM_SIZE" "$_C_ELAPSED" "$3" "$_C_MEM"
}

# A run that hit the wall budget is a legitimate (non-optimal) result, but it was
# previously recorded nowhere, so status.sh could not show it. args: algo stage seed name
_note_timeout() {
  [[ "$_C_STATUS" == TIMEOUT ]] && record_failure "$1" "$2" "$3" "$4" "$_C_RC" timeout "$_C_ERR"
  return 0
}

# On a build/OOM/error before the solver runs, emit-or-log consistently.
# args: name algo seed mode  -> returns 0 if the caller should continue, 1 to stop.
_handle_build_fail() {
  local name="$1" algo="$2" seed="$3"
  if [[ "$_C_STATUS" == OOM ]]; then
    _emit_oom_row "$name" "$algo" "$seed"
    record_failure "$algo" convert "$seed" "$name" "$_C_RC" oom "$_C_ERR"
  else
    _emit_crash_row "$name" "$algo" "$seed"
    record_failure "$algo" convert "$seed" "$name" "$_C_RC" error "$_C_ERR"
  fi
}

w_struction() {
  local seed="$1" inst="$2" name graph gn size opt smem elapsed total mem
  name=$(basename "$inst"); graph=$(mktemp "$TMP/g.XXXXXX")
  if ! build_graph "$inst" "$seed" "$MODE" "$graph"; then
    _handle_build_fail "$name" "$ALGO" "$seed"; rm -f "$graph"; return
  fi
  gn=$(head -1 "$graph" | awk '{print $1+0}')
  if [[ "$gn" -eq 0 ]]; then
    printf '%s\t%s\t%s\t%s\t1\t%s\t%s\n' "$name" "$ALGO" "$_BG_OFFSET" "$_BG_RTIME" "$seed" "$_BG_CMEM"
    rm -f "$graph"; return
  fi
  run_capture $((T + 120)) -- "$STRUCTION" "$graph" --time_limit="$T" --seed="$seed"
  smem=$_C_MEM; elapsed=$_C_ELAPSED; rm -f "$graph"
  case "$_C_STATUS" in
    SUCCESS|TIMEOUT)
      size=$(awk -F',' '/^[0-9]/{v=$1} END{print v+0}' <<<"$_C_OUT")
      grep -q '%optimal' <<<"$_C_OUT" && opt=1 || opt=0
      _note_timeout "$ALGO" struction "$seed" "$name" ;;
    OOM)  _emit_oom_row "$name" "$ALGO" "$seed"
          record_failure "$ALGO" struction "$seed" "$name" "$_C_RC" oom "$_C_ERR"; return ;;
    *)    _emit_crash_row "$name" "$ALGO" "$seed"
          record_failure "$ALGO" struction "$seed" "$name" "$_C_RC" error "$_C_ERR"; return ;;
  esac
  size=$(awk -v a="$_BG_OFFSET" -v b="$size" 'BEGIN{print a+b}')
  total=$(awk -v a="$_BG_RTIME" -v b="$elapsed" 'BEGIN{print a+b}')
  mem=$(imax "$_BG_CMEM" "$smem")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$ALGO" "$size" "$total" "$opt" "$seed" "$mem"
}

w_vc() {
  local seed="$1" inst="$2" name graph dimacs gn vc mis size opt smem elapsed total mem
  name=$(basename "$inst"); graph=$(mktemp "$TMP/g.XXXXXX")
  if ! build_graph "$inst" "$seed" "$MODE" "$graph"; then
    _handle_build_fail "$name" "$ALGO" "$seed"; rm -f "$graph"; return
  fi
  dimacs="$graph.dimacs"; to_dimacs "$graph" "$dimacs"
  run_capture $((T + 120)) -- "$VC" --time_limit="$T" --seed="$seed" < "$dimacs"
  smem=$_C_MEM; elapsed=$_C_ELAPSED; rm -f "$graph" "$dimacs"
  case "$_C_STATUS" in
    SUCCESS|TIMEOUT)
      gn=$(awk '/^s vc/{print $3}' <<<"$_C_OUT")
      vc=$(awk '/^s vc/{print $4}' <<<"$_C_OUT")
      if [[ -n "$vc" && "$_C_STATUS" == SUCCESS ]]; then mis=$((gn - vc)); opt=1; else mis=0; opt=0; fi
      _note_timeout "$ALGO" vc_solver "$seed" "$name" ;;
    OOM)  _emit_oom_row "$name" "$ALGO" "$seed"
          record_failure "$ALGO" vc_solver "$seed" "$name" "$_C_RC" oom "$_C_ERR"; return ;;
    *)    _emit_crash_row "$name" "$ALGO" "$seed"
          record_failure "$ALGO" vc_solver "$seed" "$name" "$_C_RC" error "$_C_ERR"; return ;;
  esac
  size=$(awk -v a="$_BG_OFFSET" -v b="$mis" 'BEGIN{print a+b}')
  total=$(awk -v a="$_BG_RTIME" -v b="$elapsed" 'BEGIN{print a+b}')
  mem=$(imax "$_BG_CMEM" "$smem")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$ALGO" "$size" "$total" "$opt" "$seed" "$mem"
}

w_satreduce() {
  local seed="$1" inst="$2" name graph dimacs gn vc mis size opt smem elapsed total mem
  name=$(basename "$inst"); graph=$(mktemp "$TMP/g.XXXXXX")
  if ! build_graph "$inst" "$seed" "$MODE" "$graph"; then
    _handle_build_fail "$name" "$ALGO" "$seed"; rm -f "$graph"; return
  fi
  gn=$(head -1 "$graph" | awk '{print $1+0}')
  if [[ "$gn" -eq 0 ]]; then
    printf '%s\t%s\t%s\t%s\t1\t%s\t%s\n' "$name" "$ALGO" "$_BG_OFFSET" "$_BG_RTIME" "$seed" "$_BG_CMEM"
    rm -f "$graph"; return
  fi
  dimacs="$graph.dimacs"; to_dimacs "$graph" "$dimacs"
  # vc-bnb has no internal time limit and no seed: the wall timeout is the only limit.
  run_capture "$T" -- "$SATREDUCE" --stats "$dimacs"
  smem=$_C_MEM; elapsed=$_C_ELAPSED; rm -f "$graph" "$dimacs"
  case "$_C_STATUS" in
    SUCCESS|TIMEOUT)
      vc=$(awk '/vertex_cover:/{print $2+0}' <<<"$_C_OUT")
      gn=$(awk '/graph_size:/{print $2+0}'   <<<"$_C_OUT")
      if [[ -n "$vc" ]]; then mis=$((gn - vc)); opt=1; else mis=0; opt=0; fi
      _note_timeout "$ALGO" satreduce "$seed" "$name" ;;
    OOM)  _emit_oom_row "$name" "$ALGO" "$seed"
          record_failure "$ALGO" satreduce "$seed" "$name" "$_C_RC" oom "$_C_ERR"; return ;;
    *)    _emit_crash_row "$name" "$ALGO" "$seed"
          record_failure "$ALGO" satreduce "$seed" "$name" "$_C_RC" error "$_C_ERR"; return ;;
  esac
  size=$(awk -v a="$_BG_OFFSET" -v b="$mis" 'BEGIN{print a+b}')
  total=$(awk -v a="$_BG_RTIME" -v b="$elapsed" 'BEGIN{print a+b}')
  mem=$(imax "$_BG_CMEM" "$smem")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$ALGO" "$size" "$total" "$opt" "$seed" "$mem"
}
export -f build_graph to_dimacs _handle_build_fail _emit_crash_row _emit_oom_row _note_timeout w_struction w_vc w_satreduce imax

mapfile -t SOLVABLE < <(ls "$HG_SOLVABLE"/*)

# ---- all blocks enabled; complete ones are skipped automatically -----------
echo "== struction =="
export MODE=raw    ALGO=struction;  run_block struction  "$RES/GRAPH/struction.tsv"  "$HDR" 1 w_struction SEEDS "${SOLVABLE[@]}"
export MODE=reduce ALGO=rstruction; run_block rstruction "$RES/GRAPH/rstruction.tsv" "$HDR" 1 w_struction SEEDS "${SOLVABLE[@]}"

echo "== vc_solver (WeGotYouCovered) =="
export MODE=raw    ALGO=vc_solver;  run_block vc_solver  "$RES/GRAPH/vc_solver.tsv"  "$HDR" 1 w_vc SEEDS "${SOLVABLE[@]}"
export MODE=reduce ALGO=rvc_solver; run_block rvc_solver "$RES/GRAPH/rvc_solver.tsv" "$HDR" 1 w_vc SEEDS "${SOLVABLE[@]}"

echo "== satreduce (vc-bnb, SAT-and-Reduce) =="
export MODE=raw    ALGO=satreduce;  run_block satreduce  "$RES/GRAPH/satreduce.tsv"  "$HDR" 1 w_satreduce SEEDS "${SOLVABLE[@]}"
export MODE=reduce ALGO=rsatreduce; run_block rsatreduce "$RES/GRAPH/rsatreduce.tsv" "$HDR" 1 w_satreduce SEEDS "${SOLVABLE[@]}"

echo "All requested blocks complete."
