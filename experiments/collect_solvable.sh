#!/bin/bash
#
# OPTIONAL. Collect every instance that ANY solver has solved to optimality into
# a folder of symlinks.
#
# "Any solver" means any result table that records optimality: the ILP variants,
# the graph-ILP after the KaMIS reductions, the external graph solvers
# (struction, vc_solver, satreduce) and the b-matching route -- raw and reduced.
# An instance counts as solvable as soon as ONE of them proved optimality on ONE
# seed, which is the right criterion: the point of the subset is to exclude
# instances nothing can finish, not to prefer a particular solver.
#
# By default HG_SOLVABLE *is* HG_FULL -- every block runs on every instance,
# which is the only correct starting point, since what is solvable is not known
# until the solvers have been run on everything.
#
# Narrowing is worth it once you have those results and the expensive solver
# comparisons would otherwise spend hours on instances nothing finishes. This
# script builds that subset; it does NOT change what the pipeline reads. It
# prints the one line that does.
#
# Re-run whenever the results change: it rebuilds the folder from scratch.
# Override the destination with SOLVABLE_DIR=/some/path.

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/config.sh"

res="$RES"
hypergraphs="$HG_FULL"
# Its own variable, never HG_SOLVABLE: that one defaults to HG_FULL, and this
# script must never be aimed at the full set (it starts by deleting its target).
dest="${SOLVABLE_DIR:-$REPO_DIR/hypergraphs_solvable}"

hg_abs=$(cd "$hypergraphs" && pwd)
dest_abs=$(cd "$(dirname "$dest")" && pwd)/$(basename "$dest")

# ---------------------------------------------------------------------------
# Which tables count. Discovered from the headers rather than hard-coded, so a
# new solver block is picked up the moment it writes results -- the old fixed
# list silently ignored grilp/struction/vc_solver/satreduce/b-matching, which is
# most of the solvers.
#
# The "opt" column is located BY NAME, never by position. The reduction tables
# are the same shape but mean something else entirely: gred.tsv has `gn` (graph
# vertex count) in the column where the solver tables have `opt`, so a fixed
# index would read a vertex count as an optimality flag and pull in every
# instance. Tables without an `opt` column are skipped.
# ---------------------------------------------------------------------------
declare -a tables=() skipped=()
while IFS= read -r f; do
  [ -s "$f" ] || continue
  if head -1 "$f" | tr '\t' '\n' | sed 's/^[ \t]*//;s/[ \t]*$//' | grep -qx opt; then
    tables+=("$f")
  else
    skipped+=("$(basename "$(dirname "$f")")/$(basename "$f")")
  fi
done < <(find "$res" -mindepth 2 -maxdepth 2 -name '*.tsv' 2>/dev/null | sort)

if [ ${#tables[@]} -eq 0 ]; then
  echo "ERROR: no result table under $res has an 'opt' column -- nothing to derive" >&2
  echo "       the subset from. Run the solvers over the full set first, e.g." >&2
  echo "         experiments/run_experiment.sh" >&2
  exit 1
fi

echo "scanning $(( ${#tables[@]} )) result table(s) for proven-optimal instances:"
for f in "${tables[@]}"; do
  n=$(awk -F'\t' -v OFS='\t' '
    NR==1 { for (i=1;i<=NF;i++) { h=$i; gsub(/^[ \t]+|[ \t]+$/,"",h); if (h=="opt") c=i }
            if (!c) exit; next }
    $c==1 { seen[$1]=1 }
    END { print length(seen) }' "$f")
  printf '  %-28s %s instance(s)\n' "$(basename "$(dirname "$f")")/$(basename "$f")" "${n:-0}"
done

# Union over every table: an instance is solvable if ANY of them marks it opt=1.
solvable=$(awk -F'\t' '
  FNR==1 { c=0; for (i=1;i<=NF;i++) { h=$i; gsub(/^[ \t]+|[ \t]+$/,"",h); if (h=="opt") c=i }
           next }
  c && $c==1 { print $1 }' "${tables[@]}" | sort -u)

# Never replace an existing subset with an empty one.
if [ -z "$solvable" ]; then
  echo "ERROR: no instance is marked optimal (opt=1) in any table under $res --" >&2
  echo "       refusing to write an empty subset." >&2
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
# bundled hypergraphs/ vs. a big external collection). Summarize rather than
# print one line per instance -- 50 warnings buried the actual result.
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

echo
if [ ${#skipped[@]} -gt 0 ]; then
  echo "not scanned (no 'opt' column -- reduction tables, not solver results):"
  printf '  %s\n' "${skipped[@]}"
  echo
fi
if [ "$missing" -gt 0 ]; then
  echo "note: $missing instance(s) are optimal in the results but not in the full set" >&2
  echo "      ($hg_abs) -- they were measured on a larger collection. First few:" >&2
  printf '        %s\n' "${missed[@]:0:5}" >&2
  [ "$missing" -gt 5 ] && echo "        ... and $((missing - 5)) more" >&2
  echo >&2
fi

echo "solvable by at least one solver: $count  (not in the full set: $missing)"
echo "  full set:  $hg_abs  ($(ls "$hg_abs" | wc -l) instances)"
echo "  subset:    $dest_abs  ($count instances)"
echo
echo "Nothing changed yet -- the pipeline still runs on the full set. To use this"
echo "subset for the ILP / exact-solver blocks:"
echo
echo "  export HG_SOLVABLE=$dest_abs"
