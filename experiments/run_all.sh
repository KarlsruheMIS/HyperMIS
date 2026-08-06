#!/bin/bash
# Orchestrator for the whole experiment pipeline.
#
#   experiments/run_all.sh                 show status, then run every experiment
#                                          (missing runs only; complete files are
#                                          left alone unless you confirm a forced
#                                          rerun)
#   experiments/run_all.sh --status        just print the status table and exit
#   experiments/run_all.sh --only NAME     run a single experiment group (see below)
#   experiments/run_all.sh --force         rerun everything from scratch (backs up
#                                          the old results first); prompts unless
#                                          --yes
#   experiments/run_all.sh --yes           don't prompt (non-interactive)
#
# NAME is one of: reductions | stats | graph_reduction | bmatching | graph_solver
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"

declare -A SCRIPTS=(
  [reductions]="run_experiment.sh"
  [stats]="run_reduction_statistics.sh"
  [graph_reduction]="run_graph_reduction_comparison.sh"
  [bmatching]="run_bmatching_experiments.sh"
  [graph_solver]="run_graph_solver_experiments.sh"
)
ORDER=(reductions stats graph_reduction bmatching graph_solver)

ONLY="" FORCE=0 YES=0 STATUS_ONLY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --status) STATUS_ONLY=1 ;;
    --only)   ONLY="$2"; shift ;;
    --force)  FORCE=1 ;;
    --yes|-y) YES=1 ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

echo "===================  PIPELINE STATUS  ==================="
bash "$HERE/status.sh"
echo "========================================================="
[[ "$STATUS_ONLY" -eq 1 ]] && exit 0

if [[ "$FORCE" -eq 1 && "$YES" -ne 1 ]]; then
  read -r -p "FORCE: rerun everything from scratch (old results backed up to *.bak.*)? [y/N] " ans
  [[ "$ans" =~ ^[Yy]$ ]] || { echo "aborted."; exit 1; }
fi

run_one() {
  local key="$1" script="${SCRIPTS[$1]}"
  echo; echo "########  $key  ($script)  ########"
  FORCE="$FORCE" bash "$HERE/$script"
}

if [[ -n "$ONLY" ]]; then
  [[ -n "${SCRIPTS[$ONLY]:-}" ]] || { echo "unknown --only '$ONLY' (choose: ${!SCRIPTS[*]})" >&2; exit 2; }
  run_one "$ONLY"
else
  for key in "${ORDER[@]}"; do run_one "$key"; done
fi

echo; echo "===================  FINAL STATUS  ==================="
bash "$HERE/status.sh"
