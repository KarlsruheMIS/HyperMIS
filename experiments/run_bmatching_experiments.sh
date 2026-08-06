#!/bin/bash
#   SOTA hypergraph b-matching (HeiHGM) as an exact MIS solver via transpose
#   duality, once raw (bmatching.tsv) and once after HyperMIS reductions
#   (rbmatching.tsv). One TSV line per (instance,seed); resumable + failure log.
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
  local offset=0 rtime=0 cmem=0 rn
  src="$inst"

  if [[ "$MODE" == reduce ]]; then
    run_capture 0 -- "$RUN_REDUCE" -g "$inst" -d -k "$T" -s "$seed" -o "$base.hgr"
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
  local pmem=$_C_MEM

  run_capture $((T + 120)) -- "$BM" --graph "$dual" --algorithms ilp_exact --capacity 1 --timeout "$T" --quiet
  local smem=$_C_MEM elapsed=$_C_ELAPSED rc=$_C_RC bmout=$_C_OUT
  rm -f "$base" "$base.hgr" "$dual"

  local mis opt total size mem mt ex
  case "$_C_STATUS" in
    SUCCESS)
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
  total=$(awk -v a="$rtime" -v b="$elapsed" 'BEGIN{print a+b}')
  mem=$(imax "$(imax "$cmem" "$pmem")" "$smem")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$ALGO" "$size" "$total" "$opt" "$seed" "$mem"
}
export -f w_bmatching imax

mapfile -t SOLVABLE < <(ls "$HG_SOLVABLE"/*)

echo "== b-matching (transpose + exact ilp), no reductions =="
export MODE=raw ALGO=bmatching
run_block bmatching "$RES/GRAPH/bmatching.tsv" "$HDR" 1 w_bmatching SEEDS "${SOLVABLE[@]}"

echo "== b-matching, with HyperMIS reductions =="
export MODE=reduce ALGO=rbmatching
run_block rbmatching "$RES/GRAPH/rbmatching.tsv" "$HDR" 1 w_bmatching SEEDS "${SOLVABLE[@]}"

echo "All requested blocks complete."
