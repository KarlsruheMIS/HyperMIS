#!/bin/bash
#
# Collect every instance that is provably solvable by at least one ILP method
# (opt==1 in any ILP result) into a dedicated folder of symlinks.
#
# This is the set every ILP / exact-solver block in this directory runs on --
# HG_SOLVABLE in config.sh (run_experiment.sh's ILP blocks,
# run_graph_reduction_comparison.sh's grilp, run_graph_solver_experiments.sh and
# run_bmatching_experiments.sh). The reduction blocks keep running on the FULL
# set, HG_FULL.
#
# Paths come from config.sh, so the folder this writes is by construction the
# one the pipeline reads. Re-run whenever the ILP results change; it rebuilds
# the folder from scratch.

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"

res="$RES"
hypergraphs="$HG_FULL"
dest="$HG_SOLVABLE"

# ILP result files with the "graph algo size time opt seed mem" schema (opt = col 5).
# grilp.tsv is EXCLUDED: it comes from graph_reduction_comparison and has no opt column.
opt_files=(
  "$res/ILP/ilp.tsv"
  "$res/ILP/rilp.tsv"
  "$res/ILP/filp.tsv"
  "$res/ILP/frilp.tsv"
  "$res/ILP/nilp.tsv"
  "$res/ILP/nrilp.tsv"
  "$res/ILP/gfilp.tsv"
  "$res/ILP/gfrilp.tsv"
)

hg_abs=$(cd "$hypergraphs" && pwd)
dest_abs=$(cd "$(dirname "$dest")" && pwd)/$(basename "$dest")

# Union of instance names solved to optimality (col5==1) by any method, any seed.
existing=()
for f in "${opt_files[@]}"; do
  [ -f "$f" ] && existing+=("$f")
done
solvable=$(awk -F'\t' 'FNR>1 && $5==1{print $1}' "${existing[@]}" | sort -u)

# This script rebuilds $dest from scratch, so it starts with `rm -rf`. That is
# safe only for a directory it owns -- a folder of symlinks it created itself.
# HG_SOLVABLE now defaults INSIDE the repo, next to the real instances, so guard
# it: refuse to touch the full set, and refuse any directory holding a regular
# file. Deleting the bundled hypergraphs/ because of a mistyped override would be
# unrecoverable for anything not tracked in git.
if [ "$dest_abs" = "$hg_abs" ]; then
  echo "ERROR: HG_SOLVABLE and HG_FULL are the same directory ($dest_abs)." >&2
  echo "       The solvable subset must be its own folder; refusing to delete the full set." >&2
  exit 1
fi
if [ -d "$dest_abs" ] && find "$dest_abs" -maxdepth 1 -type f -print -quit | grep -q .; then
  echo "ERROR: $dest_abs holds regular files, not just the symlinks this script creates." >&2
  echo "       Refusing to 'rm -rf' it. Move it aside or point HG_SOLVABLE elsewhere." >&2
  exit 1
fi

rm -rf "$dest_abs"
mkdir -p "$dest_abs"

# A name that is optimal in the results but absent from the full set is normal
# whenever HG_FULL is smaller than the set the results were produced from (the
# default bundled hypergraphs/ vs. a big external collection). Summarize rather
# than print one line per instance -- 50 warnings buried the actual result.
count=0 missing=0
declare -a missed=()
while read -r name; do
  [ -z "$name" ] && continue
  if [ -e "$hg_abs/$name" ]; then
    ln -s "$hg_abs/$name" "$dest_abs/$name"
    count=$((count + 1))
  else
    missed+=("$name")
    missing=$((missing + 1))
  fi
done <<< "$solvable"

if [ "$missing" -gt 0 ]; then
  echo "note: $missing instance(s) are optimal in the ILP results but not in the full set" >&2
  echo "      ($hg_abs) -- they were measured on a larger collection. First few:" >&2
  printf '        %s\n' "${missed[@]:0:5}" >&2
  [ "$missing" -gt 5 ] && echo "        ... and $((missing - 5)) more" >&2
fi

echo "ILP-solvable instances collected: $count  (missing: $missing)"
echo "  full set:     $hg_abs  ($(ls "$hg_abs" | wc -l) instances)"
echo "  solvable set: $dest_abs  ($count instances)"
