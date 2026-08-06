#!/bin/bash
# Per-rule and per-config reduction statistics. Resumable.
#   stats.tsv        : per-reduction-rule stats (run_reduce -e -d). Emits SEVERAL
#                      rows per (graph,seed) -> completeness is "any row present".
#   config_stats.tsv : run_reduce -r<c> -d for every c in CONFIGS (config.sh),
#                      one row per config (algo=reduce<c>).
#                      Each (graph,seed,config) is its own resumable unit, so you
#                      can delete a subset of configs and have ONLY those rerun.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"
source "$HERE/lib.sh"
init_mem
init_failures

STATS_H='graph\t seed\t red_n\t red_m\t time\t reduction'
CSTATS_H='graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed'
# CONFIGS comes from config.sh -- shared with status.sh.

w_stats() {
  local seed="$1" inst="$2" name; name=$(basename "$inst")
  run_capture 0 -- "$RUN_REDUCE" -e -d -g "$inst" -t "$T" -s "$seed"
  case "$_C_STATUS" in
    SUCCESS) printf '%s\n' "$_C_OUT" ;;
    OOM)     record_failure stats reduce "$seed" "$name" "$_C_RC" oom   "$_C_ERR" ;;
    *)       record_failure stats reduce "$seed" "$name" "$_C_RC" error "$_C_ERR" ;;
  esac
}

# One config per job: worker gets (seed, inst, config). Emits one row
# (algo=reduce<config>); resume keys on (graph,seed,algo) so only missing
# configs run.
w_cstats() {
  local seed="$1" inst="$2" c="$3" name; name=$(basename "$inst")
  run_capture 0 -- "$RUN_REDUCE" -r"$c" -d -g "$inst" -t "$T" -s "$seed"
  case "$_C_STATUS" in
    SUCCESS) printf '%s\n' "$_C_OUT" ;;
    OOM)     record_failure config_stats "reduce$c" "$seed" "$name" "$_C_RC" oom   "$_C_ERR" ;;
    *)       record_failure config_stats "reduce$c" "$seed" "$name" "$_C_RC" error "$_C_ERR" ;;
  esac
}
export -f w_stats w_cstats

mapfile -t FULL < <(ls "$HG_FULL"/*)

echo "== Per-rule reduction stats =="
run_block stats "$RES/RED/stats.tsv" "$STATS_H" any w_stats SEEDS "${FULL[@]}"

echo "== Per-config reduction stats (r${CONFIGS[0]}..r${CONFIGS[-1]}) =="
run_block_sub config_stats "$RES/RED/config_stats.tsv" "$CSTATS_H" algo reduce \
  w_cstats SEEDS CONFIGS "${FULL[@]}"

echo "All requested blocks complete."
