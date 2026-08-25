#!/usr/bin/env python3
"""Performance profiles (Dolan & More, 2002) of the ILP results.

An instance is a GRAPH; its per-metric value is the geometric mean over the
graph's seeds, counted only if every seed was solved optimally (see
paper_plot_common.read_ilp_method_values).  For each metric and instance p we
compute per method s the ratio r_{p,s} = value_{p,s} / min_s value_{p,s}; the
profile of s is the fraction of instances with r_{p,s} <= tau.

PANEL LIBRARY: make_solver_plots.py imports this module and calls build_profile,
write_dat and make_panel to build the performance-profile half of the combined
figure fig:solverplots.  This module emits no fragment of its own; its only
direct output is the pgfplots data it writes:
  data/plot_data/pp_{time,mem}_{method}.dat  two-column (tau, frac) pgfplots data
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    RESULTS_DIR,
    cfg_macro, color_name, ILP_METHODS, config_mark_style, LEGEND_COLUMNS, data_path,
    legend_filler, legend_plot_order, legend_to_name,
)

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR = os.path.join(RESULTS_DIR, "ILP")

# (metric key, tsv column, axis label), in PANEL ORDER.  House rule for every
# two-panel figure in the paper: run time on the LEFT, peak memory on the RIGHT.
# Kept as a list, not a dict, so the panel order is stated rather than inherited
# from insertion order -- the captions say "Left: run time, Right: peak memory"
# and must not be able to drift away from the figure.
METRICS = [
    ("time", "time", "run time"),
    ("mem", "mem", "memory"),
]


def build_profile(method_values):
    """method_values: {method: {graph: value}} -> (n, instances, {method: pts})."""
    instances = set()
    for values in method_values.values():
        instances.update(values.keys())
    instances = sorted(instances)
    n = len(instances)

    best = {}
    for g in instances:
        best[g] = min(v[g] for v in method_values.values() if g in v)

    profiles = {}
    for method, values in method_values.items():
        ratios = sorted(values[g] / best[g] for g in values)
        pts = [(1.0, 0.0)]  # anchor baseline at tau=1
        for i, r in enumerate(ratios, start=1):
            pts.append((r, i / n))
        profiles[method] = pts
    return n, instances, profiles


def write_dat(metric, method, pts):
    path = data_path(f"pp_{metric}_{method}.dat")
    with open(path, "w") as fh:
        fh.write("tau\tfrac\n")
        for tau, frac in pts:
            fh.write(f"{tau:.10g}\t{frac:.10g}\n")
    return path


def make_panel(metric, axis_label, methods, collect_legend, key):
    """One performance-profile panel as a ``\\nextgroupplot`` block for the shared
    2x2 groupplot assembled in make_solver_plots.

    Only the panel-specific options live here (scale, limits, labels, legend);
    the common size/style/`scale only axis` sit on the group, which is what keeps
    the four axis boxes -- and their x-axes -- exactly aligned.  The time and
    memory profiles show the same methods, so only the first panel collects the
    legend; it is typeset once, centred above the grid.
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
            f"    \\addplot+[const plot, thick, {forget}color={col},\n"
            f"             {mark}]\n"
            f"      table[x=tau, y=frac, col sep=tab] "
            f"{{data/plot_data/pp_{metric}_{m}.dat}};")
        if collect_legend:
            plots.append(f"    \\addlegendentry{{{cfg_macro(m)}}}")
    body = "\n".join(plots)
    # LEGEND_COLUMNS columns: one approach per column, its variants stacked --
    # unreduced on the first row, reduced below it, third variant below that.
    legend_opts = (legend_to_name(key, columns=LEGEND_COLUMNS) if collect_legend
                   else "legend style={draw=none}")
    return f"""    \\nextgroupplot[
      xmode=log, log basis x=2,
      xmin=1, ymin=0, ymax=1.02,
      xlabel={{$\\tau$ (factor from best {axis_label})}},
      ylabel={{fraction of instances}},
      legend cell align=left, {legend_opts},
    ]
{body}"""


# This module is a PANEL LIBRARY for make_solver_plots.py, which assembles the
# performance-profile panels (together with the cactus panels) into the single
# combined figure fig:solverplots (figures/solver_plots.tex).  It has no
# standalone fragment of its own -- running it directly regenerates that figure.
if __name__ == "__main__":
    import make_solver_plots
    sys.exit(make_solver_plots.main() or 0)
