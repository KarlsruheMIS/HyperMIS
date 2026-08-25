#!/usr/bin/env python3
"""Combined performance-profile + cactus figure (fig:solverplots).

One figure* with four panels that share a single legend:
    row 1 : performance profiles -- run time (left), peak memory (right)
    row 2 : cactus plots         -- run time (left), peak memory (right)

The panels are exactly the ones make_perf_profiles.py and make_cactus.py build
(same .dat files, methods, marks, axis options); this script only assembles them
into one float, with the shared legend collected by the top-left panel and
printed above the grid.  make_perf_profiles.py and make_cactus.py are imported
as panel libraries for this.

Outputs (paper tree):
    data/plot_data/pp_{time,mem}_{method}.dat
    data/plot_data/cactus_{time,mem}_{method}.dat
    figures/solver_plots.tex  (figure*, fig:solverplots)
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_cactus as ct          # noqa: E402
import make_perf_profiles as pp   # noqa: E402
from paper_plot_common import (   # noqa: E402
    AXIS_OPEN, PANEL_YTICK, emit_fragment, fig_path, read_ilp_method_values,
    shared_legend_row,
)

# The shared legend is collected once by the top-left panel and \ref'd above the
# grid; every other panel draws its series with `forget plot`.
LEGEND_KEY = "solverlegend"
# The four panels are one pgfplots `groupplot`: every axis BOX is exactly
# AXIS_W x AXIS_H (scale only axis) and placed on a shared 2x2 grid, so the boxes
# -- and hence the x-axes -- line up EXACTLY, regardless of how wide each panel's
# y-tick labels (0.2 vs 10^{-2}) or y-axis label ([s], [MB]) happen to be.  The
# seps are the gaps between boxes: HSEP holds the right column's y labels, VSEP
# the top row's x labels.
AXIS_W = "0.38\\linewidth"
AXIS_H = "0.23\\linewidth"
HSEP = "42pt"
VSEP = "34pt"
# Common options shared by all four panels (size, style, box-only alignment).
GROUP_OPTS = (
    f"group style={{group size=2 by 2, horizontal sep={HSEP}, vertical sep={VSEP}}},\n"
    f"      width={AXIS_W}, height={AXIS_H}, scale only axis,\n"
    f"      label style={{font=\\small}}, tick label style={{font=\\small}},\n"
    f"      {PANEL_YTICK},\n"
    f"      grid=both, grid style={{gray!20}}, {AXIS_OPEN},")


def main():
    # --- row 1: performance profiles (the top-left panel collects the legend) ---
    pp_panels, n_pp = [], None
    for i, (mk, col, label) in enumerate(pp.METRICS):
        methods, mv = read_ilp_method_values(pp.INPUT_DIR, col, warn=(i == 0))
        n_pp, _inst, profiles = pp.build_profile(mv)
        for m in methods:
            pp.write_dat(mk, m, profiles[m])
        pp_panels.append(pp.make_panel(mk, label, methods,
                                       collect_legend=(i == 0), key=LEGEND_KEY))

    # --- row 2: cactus plots (all forget the legend) ---
    ct_panels, solvable = [], set()
    for mk, col, ylabel, scale, ymin in ct.METRICS:
        methods, values = read_ilp_method_values(ct.INPUT_DIR, col, warn=False)
        for m in methods:
            ct.write_dat(mk, m, [v / scale for v in values[m].values()])
        if mk == "time":
            for gv in values.values():
                solvable.update(gv.keys())
        ct_panels.append(ct.make_panel(mk, ylabel, ymin, methods,
                                       collect_legend=False, key=LEGEND_KEY))

    caption = (
        "Performance profiles (top) and cactus plots (bottom) for run time "
        "(left) and peak memory (right) over the solvable instances. In the "
        "profiles, points to the upper-left are better and a curve can only rise "
        "to the fraction of instances that the configuration solves. In the "
        "cactus plots, further right means more instances solved and lower means "
        "cheaper, and each curve ends at the number of instances that the "
        "configuration solves.")

    # Group cells fill row-major: (top-left, top-right, bottom-left, bottom-right).
    frag_body = f"""\\begin{{figure*}}[t]
  \\centering
{shared_legend_row(LEGEND_KEY)}
  \\begin{{tikzpicture}}
    \\begin{{groupplot}}[
      {GROUP_OPTS}
    ]
{pp_panels[0]}
{pp_panels[1]}
{ct_panels[0]}
{ct_panels[1]}
    \\end{{groupplot}}
  \\end{{tikzpicture}}
  \\caption{{{caption}}}
  \\label{{fig:solverplots}}
\\end{{figure*}}"""
    frag = emit_fragment(fig_path("solver_plots.tex"), frag_body)

    print(f"performance-profile instances = {n_pp}")
    print(f"solvable by >=1 method: {len(solvable)}")
    print(f"Wrote {frag}")


if __name__ == "__main__":
    main()
