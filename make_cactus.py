#!/usr/bin/env python3
"""Cactus / survival plots of solving power for the ILP results.

For each method, the instances (graphs) solved to optimality on every seed are
taken with their geometric-mean value, sorted ascending, and plotted as
(number of instances solved, value).  A method whose curve extends further right
solves more instances; a lower curve is cheaper.  Complements the relative view
in make_perf_profiles.py with an absolute one.

Two metrics, shown side by side with one shared legend (same set of methods):
  run time [s]    -- how long the solved instances take
  peak memory [MB]-- how much memory they need

PANEL LIBRARY: make_solver_plots.py imports this module and calls write_dat and
make_panel to build the cactus half of the combined figure fig:solverplots.
This module emits no fragment of its own; its only direct output is the pgfplots
data it writes:
  data/plot_data/cactus_{time,mem}_{method}.dat  (idx, value) pgfplots data
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    config_mark_style, LEGEND_COLUMNS, cfg_macro, color_name, data_path,
    legend_filler, legend_plot_order, legend_to_name,
)

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR = os.path.join(HERE, "results", "ILP")

KB_PER_MB = 1024.0

# (metric key, tsv column, axis label, scale, ymin), in PANEL ORDER.  House rule
# for every two-panel figure in the paper: run time on the LEFT, peak memory on
# the RIGHT.  Kept as a list, not a dict, so the panel order is stated rather
# than inherited from insertion order -- the caption says "Left: run time,
# Right: peak memory" and must not be able to drift away from the figure.
METRICS = [
    ("time", "time", "run time [s]", 1.0, 0.01),
    ("mem", "mem", "peak memory [MB]", KB_PER_MB, 1.0),
]


def write_dat(metric, method, values):
    path = data_path(f"cactus_{metric}_{method}.dat")
    with open(path, "w") as fh:
        fh.write("idx\tval\n")
        for i, v in enumerate(sorted(values), start=1):
            fh.write(f"{i}\t{v:.10g}\n")
    return path


def make_panel(metric, ylabel, ymin, methods, collect_legend, key):
    """One cactus panel as a ``\\nextgroupplot`` block for the shared 2x2 groupplot
    assembled in make_solver_plots.

    Only panel-specific options live here; the common size/style/`scale only axis`
    sit on the group, which is what keeps the four axis boxes aligned.  Both panels
    show the same methods, so only the first collects the legend.
    """
    plots = []
    # Legend order, not file order, with a blank cell wherever a variant does not
    # exist (or has no runs yet) so every approach stays in its own column.
    for i, m in enumerate(legend_plot_order(methods)):
        if m is None:
            if collect_legend:
                plots.append(legend_filler())
            continue
        col = color_name(m)
        # One shape per approach; filling names the variant (hollow / solid /
        # half = without reductions / with them / graph-reduced expansion).
        mark = config_mark_style(m, col, size=1.6, fallback_index=i)
        forget = "" if collect_legend else "forget plot, "
        plots.append(
            f"    \\addplot+[thick, {forget}color={col}, {mark}]\n"
            f"      table[x=idx, y=val, col sep=tab] "
            f"{{data/plot_data/cactus_{metric}_{m}.dat}};")
        if collect_legend:
            plots.append(f"    \\addlegendentry{{{cfg_macro(m)}}}")
    body = "\n".join(plots)
    # LEGEND_COLUMNS columns: one approach per column, its variants stacked --
    # unreduced on the first row, reduced below it, third variant below that.
    legend_opts = (legend_to_name(key, columns=LEGEND_COLUMNS) if collect_legend
                   else "legend style={draw=none}")
    return f"""    \\nextgroupplot[
      ymode=log, xmin=0, ymin={ymin},
      xlabel={{number of instances solved}},
      ylabel={{{ylabel}}},
      legend cell align=left, {legend_opts},
    ]
{body}"""


# This module is a PANEL LIBRARY for make_solver_plots.py, which assembles the
# cactus panels (together with the performance-profile panels) into the single
# combined figure fig:solverplots (figures/solver_plots.tex).  It has no
# standalone fragment of its own -- running it directly regenerates that figure.
if __name__ == "__main__":
    import make_solver_plots
    sys.exit(make_solver_plots.main() or 0)
