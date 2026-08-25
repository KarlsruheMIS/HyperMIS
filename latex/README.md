# Reproducible figures and tables

This directory holds the plotting pipeline **and** its output: the Python
generators, the paper-ready LaTeX fragments they produce, and a master document
that builds every figure and table into a single PDF — straight from the raw
experiment data in `../results/`, so the results are reproducible from this
repository alone.

## Build

```sh
cd latex

# 1. regenerate the fragments + plot data from ../results/*.tsv
python3 make_all_paper.py            # writes into this latex/ tree (default output)

# 2. compile the master document
latexmk -pdf main.tex
```

(From the repo root the same is `python3 latex/make_all_paper.py`; paths are
resolved from the script location, not the working directory.)

`make_all_paper.py` reads the raw run data in `../results/{RED,ILP,GRAPH}/*.tsv`
and (re)generates:

- `figures/*.tex`, `tables/*.tex` — the paper-ready fragments,
- `tikz/plotcolors.tex` — the plot colours,
- `data/plot_data/*.dat` — the pgfplots input data,
- `main.tex` — this document, which `\input`s every fragment.

## What is tracked vs. generated

- **Tracked (committed):** the generators (`make_*.py`, `paper_plot_common.py`),
  the raw inputs (`../results/{RED,ILP,GRAPH}/*.tsv`), the static `preamble.tex`,
  and the generated `.tex` fragments.
- **Not tracked (regenerated):** `data/plot_data/` (computed numeric data) and the
  LaTeX build products (`*.aux`, `*.pdf`, …). Run step 1 above to recreate them —
  `main.tex` will not compile until `data/plot_data/` exists.

Remove all regenerable temporary files (Python caches, `data/plot_data/`, LaTeX
build products) with `./clean.sh`.

## Targeting the paper instead

Point `PAPER_ROOT` at the paper checkout to write the fragments there instead of
into this tree:

```sh
PAPER_ROOT=/path/to/HypergraphMISReduction-Paper python3 make_all_paper.py
```

`preamble.tex` is a self-contained snapshot of the packages and macros the
fragments need (distilled from the paper's `defs.tex`/`paper.tex`); it lets
`main.tex` build without the paper.
