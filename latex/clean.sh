#!/usr/bin/env bash
#
# Remove regenerable temporary files from the repository:
#   - Python bytecode caches (__pycache__/, *.pyc), repo-wide
#   - pgfplots data written by the plotting scripts (latex/data/plot_data/*.dat,
#     *.tsv), which make_all_paper.py rebuilds from results/
#   - LaTeX build products of the reproducibility document (latex/main.*)
#
# Tracked source is left untouched: the plotting scripts, the *.tex fragments,
# preamble.tex, README files, and the raw results/*.tsv inputs.
#
# Usage:  latex/clean.sh        (safe to run from anywhere)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # the latex/ directory
REPO="$(dirname "$HERE")"                              # code-repo root

# 1. Python caches, anywhere under the repo.
find "$REPO" -type d -name __pycache__ -prune -exec rm -rf {} +
find "$REPO" -type f -name '*.pyc' -delete

# 2. Generated pgfplots data (rebuilt by make_all_paper.py).
rm -rf "$HERE/data/plot_data"

# 3. LaTeX build products of latex/main.tex (rebuilt by latexmk).
rm -f "$HERE"/main.{aux,log,out,fls,fdb_latexmk,synctex.gz,toc,bbl,blg} "$HERE"/main.pdf

echo "Cleaned: __pycache__, *.pyc, latex/data/plot_data, latex/main build products."
