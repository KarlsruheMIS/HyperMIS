# Reproducible figures and tables

This directory builds every figure and table from the paper into a single PDF,
straight from the raw experiment data in `../results/`, so the results are
reproducible from this repository alone.

## Build

```sh
# 1. regenerate the fragments + plot data from ../results/*.tsv
cd ..
python3 make_all_paper.py            # writes into latex/ (default output root)

# 2. compile the master document
cd latex
latexmk -pdf main.tex
```

`make_all_paper.py` reads the raw run data in `../results/{RED,ILP,GRAPH}/*.tsv`
and (re)generates:

- `figures/*.tex`, `tables/*.tex` — the paper-ready fragments,
- `tikz/plotcolors.tex` — the plot colours,
- `data/plot_data/*.dat`, `data/plot_data/fred.tsv` — the pgfplots input data,
- `main.tex` — this document, which `\input`s every fragment.

## What is tracked vs. generated

- **Tracked (committed):** the generators (`../make_*.py`, `../paper_plot_common.py`),
  the raw inputs (`../results/{RED,ILP,GRAPH}/*.tsv`), the static `preamble.tex`,
  and the generated `.tex` fragments.
- **Not tracked (regenerated):** `data/plot_data/` (computed numeric data) and the
  LaTeX build products (`*.aux`, `*.pdf`, …). Run step 1 above to recreate them —
  `main.tex` will not compile until `data/plot_data/` exists.

## Targeting the paper instead

Point `PAPER_ROOT` at the paper checkout to write the fragments there instead of
into this tree:

```sh
PAPER_ROOT=/path/to/HypergraphMISReduction-Paper python3 ../make_all_paper.py
```

`preamble.tex` is a self-contained snapshot of the packages and macros the
fragments need (distilled from the paper's `defs.tex`/`paper.tex`); it lets
`main.tex` build without the paper.
