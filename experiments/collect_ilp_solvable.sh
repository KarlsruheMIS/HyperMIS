#!/bin/bash
#
# OPTIONAL. Collect every instance provably solvable by at least one ILP method
# (opt==1 in any ILP result) into a folder of symlinks.
#
# By default HG_SOLVABLE *is* HG_FULL -- the ILP and exact-solver blocks run on
# every instance, which is the only correct starting point, since which instances
# are solvable is not known until the ILP has been run on all of them.
#
# Narrowing is worth it once you have those results and the expensive solver
# comparisons (struction / vc_solver / satreduce / b-matching) would otherwise
# spend hours on instances no solver finishes. This script builds that subset;
# it does NOT change what the pipeline reads. It prints the one line that does.
#
# Re-run whenever the ILP results change: it rebuilds the folder from scratch.
# Override the destination with SOLVABLE_DIR=/some/path.

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"

res="$RES"
hypergraphs="$HG_FULL"
# Its own variable, never HG_SOLVABLE: that one defaults to HG_FULL, and this
# script must never be aimed at the full set (it starts by deleting its target).
dest="${SOLVABLE_DIR:-$REPO_DIR/hypergraphs_ilp_solvable}"

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

# No ILP results yet (fresh clone). Bail out before touching anything: with no
# file arguments awk reads STDIN, which hangs on a terminal, and the empty result
# would replace a good subset with one that silently disables every ILP block.
if [ ${#existing[@]} -eq 0 ]; then
  echo "ERROR: no ILP result files under $res/ILP -- nothing to derive the subset from." >&2
  echo "       Run the ILP blocks over the full set first:" >&2
  echo "         experiments/run_experiment.sh" >&2
  echo "       That is what produces the results this script reads." >&2
  exit 1
fi

solvable=$(awk -F'\t' 'FNR>1 && $5==1{print $1}' "${existing[@]}" | sort -u)

# Likewise, never replace an existing subset with an empty one.
if [ -z "$solvable" ]; then
  echo "ERROR: no instance is marked optimal (opt=1) in $res/ILP -- refusing to write an" >&2
  echo "       empty subset, which would silently disable every ILP block." >&2
  exit 1
fi

# This script rebuilds $dest from scratch, so it starts with `rm -rf`. That is
# safe only for a directory it owns -- a folder of symlinks it created itself.
# The default destination sits INSIDE the repo, next to the real instances, so
# guard it: refuse to touch the full set, and refuse any directory holding a
# regular file. Deleting the bundled hypergraphs/ because of a mistyped
# SOLVABLE_DIR would be unrecoverable for anything not tracked in git.
if [ "$dest_abs" = "$hg_abs" ]; then
  echo "ERROR: SOLVABLE_DIR and HG_FULL are the same directory ($dest_abs)." >&2
  echo "       The subset must be its own folder; refusing to delete the full set." >&2
  exit 1
fi
if [ -d "$dest_abs" ] && find "$dest_abs" -maxdepth 1 -type f -print -quit | grep -q .; then
  echo "ERROR: $dest_abs holds regular files, not just the symlinks this script creates." >&2
  echo "       Refusing to 'rm -rf' it. Move it aside or set SOLVABLE_DIR elsewhere." >&2
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
echo "  subset:       $dest_abs  ($count instances)"
echo
echo "Nothing changed yet -- the pipeline still runs on the full set. To use this"
echo "subset for the ILP / exact-solver blocks:"
echo
echo "  export HG_SOLVABLE=$dest_abs"
