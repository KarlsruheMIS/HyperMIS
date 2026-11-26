#!/bin/bash
#   SOTA hypergraph b-matching (HeiHGM) as an exact MIS solver via transpose
#   duality (its own reductions, then Gurobi, then unfolding), once raw (bmatching.tsv) and once after HyperMIS reductions
#   (rbmatching.tsv). One TSV line per (instance,seed); resumable + failure log.
#   time = HyperMIS reductions (reduce mode) + building the transpose in memory +
#   b-matching's own algorithm time. No file I/O is counted anywhere: run_reduce
#   stops its clock before writing the kernel, the transposer reports its build
#   only, and bmatching_cli's time_ms excludes reading the dual.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"
source "$HERE/lib.sh"
init_mem
init_failures

mkdir -p "$SCRATCH_BASE" "$RES/GRAPH"
TMP=$(mktemp -d "$SCRATCH_BASE/bmatching.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
export TMP

HDR='graph\talgo\tsize\ttime\topt\tseed\tmem'

imax() { awk -v a="$1" -v b="$2" 'BEGIN{print (a+0>b+0)?a+0:b+0}'; }

# Uses exported MODE (raw|reduce) and ALGO (bmatching|rbmatching).
w_bmatching() {
  local seed="$1" inst="$2" name base src dual
  name=$(basename "$inst")
  base=$(mktemp "$TMP/b.XXXXXX")
  local offset=0 rtime=0 cmem=0 rn total
  src="$inst"

  if [[ "$MODE" == reduce ]]; then
    run_capture 0 -- "$RUN_REDUCE" -g "$inst" -d -k "$KS" -s "$seed" -o "$base.hgr"
    if [[ "$_C_STATUS" != SUCCESS ]]; then
      if [[ "$_C_STATUS" == OOM ]]; then
        printf '%s\t%s\t0\t%s\t0\t%s\t%s\n' "$name" "$ALGO" "$T" "$seed" "$_C_MEM"
        record_failure "$ALGO" reduce "$seed" "$name" "$_C_RC" oom "$_C_ERR"
      else
        record_failure "$ALGO" reduce "$seed" "$name" "$_C_RC" error "$_C_ERR"
      fi
      rm -f "$base" "$base.hgr"; return
    fi
    cmem=$_C_MEM
    offset=$(awk -F'\t' 'END{print $9+0}'  <<<"$_C_OUT")
    rtime=$(awk -F'\t' 'END{print $10+0}' <<<"$_C_OUT")
    rn=$(awk -F'\t' 'END{print $6+0}'      <<<"$_C_OUT")
    src="$base.hgr"
    if [[ "$rn" -eq 0 ]]; then
      # kernel fully solved by reductions: IS = offset
      printf '%s\t%s\t%s\t%s\t1\t%s\t%s\n' "$name" "$ALGO" "$offset" "$rtime" "$seed" "$cmem"
      rm -f "$base" "$src"; return
    fi
  fi

  dual="$base.dual"
  run_capture 0 -- python3 "$TRANSPOSE" "$src" "$dual"
  if [[ "$_C_STATUS" != SUCCESS ]]; then
    record_failure "$ALGO" transpose "$seed" "$name" "$_C_RC" error "$_C_ERR"
    rm -f "$base" "$base.hgr" "$dual"; return
  fi
  # Transpose output: "<dropped isolated vertices> <build seconds>". Only the
  # in-memory build counts towards the total -- reading/writing files does not.
  local pmem=$_C_MEM ptime dropped
  read -r dropped ptime <<<"$_C_OUT"
  offset=$((offset + dropped))

  local dm
  dm=$(awk 'NR==1{print $1; exit}' "$dual")
  if [[ "$dm" -eq 0 ]]; then
    # only isolated vertices left: nothing to solve (the solver needs >= 1 edge)
    total=$(awk -v a="$rtime" -v c="$ptime" 'BEGIN{print a+c}')
    printf '%s\t%s\t%s\t%s\t1\t%s\t%s\n' "$name" "$ALGO" "$offset" "$total" "$seed" "$(imax "$cmem" "$pmem")"
    rm -f "$base" "$base.hgr" "$dual"; return
  fi

  run_capture $((T + 120)) -- "$BM" --graph "$dual" --algorithms reductions,presolved_ilp,unfold --capacity 1 --timeout "$T" --quiet
  local smem=$_C_MEM elapsed=$_C_ELAPSED rc=$_C_RC bmout=$_C_OUT
  rm -f "$base" "$base.hgr" "$dual"

  local mis opt size mem mt ex
  case "$_C_STATUS" in
    SUCCESS)
      # The solver's own algorithm time (time_ms), which excludes loading the
      # input file, rather than the process wall time.
      elapsed=$(awk '/^time_ms:/{print $2/1000}' <<<"$bmout")
      [[ -n "$elapsed" ]] || { record_failure "$ALGO" bmatching "$seed" "$name" "$rc" error "no time_ms in output"; return; }
      mt=$(awk '/^size:/{print $2+0}'  <<<"$bmout")
      ex=$(awk '/^exact:/{print $2}'   <<<"$bmout")
      [[ "$ex" == true ]] && opt=1 || opt=0
      mis=${mt:-0} ;;
    TIMEOUT) mis=0; opt=0 ;;
    OOM)     mis=0; opt=0
             record_failure "$ALGO" bmatching "$seed" "$name" "$rc" oom "$_C_ERR" ;;
    *)       record_failure "$ALGO" bmatching "$seed" "$name" "$rc" error "$_C_ERR"
             return ;;
  esac
  size=$(awk -v a="$offset" -v b="$mis" 'BEGIN{print a+b}')
  total=$(awk -v a="$rtime" -v c="$ptime" -v b="$elapsed" 'BEGIN{print a+c+b}')
  mem=$(imax "$(imax "$cmem" "$pmem")" "$smem")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$ALGO" "$size" "$total" "$opt" "$seed" "$mem"
}
export -f w_bmatching imax

mapfile -t SOLVABLE < <(ls "$HG_SOLVABLE"/*)

echo "== b-matching (transpose + its reductions + exact ilp), no HyperMIS reductions =="
export MODE=raw ALGO=bmatching
run_block bmatching "$RES/GRAPH/bmatching.tsv" "$HDR" 1 w_bmatching SEEDS "${SOLVABLE[@]}"

echo "== b-matching, with HyperMIS reductions =="
export MODE=reduce ALGO=rbmatching
run_block rbmatching "$RES/GRAPH/rbmatching.tsv" "$HDR" 1 w_bmatching SEEDS "${SOLVABLE[@]}"

echo "All requested blocks complete."
