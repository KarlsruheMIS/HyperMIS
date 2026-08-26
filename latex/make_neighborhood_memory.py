#!/usr/bin/env python3
"""Cost of reducing an instance, per strategy.

All four strategies run the same rule set; they differ only in how the instance
is represented and how vertex neighborhoods are obtained, so the *reduction
result* is (nearly) fixed and what varies is the cost:

    recompute        = red.tsv   (recompute neighborhoods every time)
    precompute       = nred.tsv  (-n: build the whole neighborhood array up front)
    on-demand        = fred.tsv  (-d: store a vertex's neighborhood only when a
                                  reduction asks for it, patched in place)
    graph reductions = gred.tsv  (materialize the clique expansion, then run the
                                  graph reduction engine with the same rule types)

The clique expansion is just another representation of the same instance, so it
is reported as a fourth strategy rather than in a separate table.

The three hypergraph strategies are *meant* to be a pure time/memory trade-off,
leaving the same reduced instance.  That is not exactly true, so the agreement is
measured rather than assumed: the differing instances are counted and reported,
and they concentrate on instances where the reduction is cut off by the time
limit at different points.

Everything is aggregated over the instances that ALL four strategies completed,
so the rows are directly comparable.  Per graph a value is the per-seed minimum
(min-of-k); across graphs we use the shifted geometric mean.

Output (paper tree):
  figures/neighborhood_mem.tex  (figure, fig:neighborhood-mem)

The per-strategy numbers live in the merged table emitted by
make_strategy_table.py, which pairs them with the ILP cost on the same rows.
"""

import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    RESULTS_DIR,
    AXIS_OPEN, LEGEND_ABOVE, mark_style, emit_fragment, fig_path, geo_mean_std,
    read_rows, same_reduction_config, shifted_geomean, tab_path,
)

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR = os.path.join(RESULTS_DIR, "RED")

KB_PER_MB = 1024.0
TIME_SHIFT = 0.01   # s
MEM_SHIFT = 1.0     # MB
RN_SHIFT = 1.0      # vertices

# (legend label, colour key, file stem, is_hypergraph).  The labels are the bare
# \rmode macros the strategy table uses, down to the star superscript that marks
# the clique-expansion route -- spelling the modes out again would make the
# legend wider than the column, and the table right above the figure already
# introduces them.  The caption glosses them in one line for a reader who meets
# the figure first.
STRATEGIES = [
    ("\\rmode{r}", "recompute", "red", True),
    ("\\rmode{p}", "precompute", "nred", True),
    ("\\rmode{d}", "ondemand", "fred", True),
    ("\\rmode[\\star]{G}", "nored", "gred", False),
]

# Plain-English name per strategy, used only in this script's stdout summary --
# there the LaTeX macro would be unreadable.
DESCRIPTIONS = {
    "recompute": "recompute", "precompute": "precompute",
    "ondemand": "on-demand", "nored": "graph reductions",
}
# All four routes go in the time/memory scatter.  The clique row is not a fourth
# neighborhood mode -- it reduces a different object (the expansion) with a
# different engine -- but the cost it is being compared on, time and peak memory
# to reduce the same instance, is exactly the same quantity, so it belongs in the
# same picture.  The caption says which of the four is the odd one out; the head-
# to-head remaining-vertex counts printed below say what it costs in reduction
# quality.


def read_strategy(stem):
    """graph -> {'time', 'mem', 'rn'}; time/mem per-seed minimum (min-of-k).

    The reduction is deterministic, so the run-to-run spread in time and peak
    memory is pure measurement noise and the minimum over seeds is the cleanest
    estimate of the true cost.  rn (remaining vertices) is likewise deterministic
    and taken from the first row seen for that graph.
    """
    _, rows = read_rows(os.path.join(INPUT_DIR, f"{stem}.tsv"))
    bg = defaultdict(lambda: {"time": [], "mem": [], "rn": None})
    for r in rows:
        try:
            t, mem, rn = float(r["time"]), float(r["mem"]), float(r["rn"])
        except (KeyError, ValueError):
            continue
        if t > 0 and mem > 0:
            bg[r["graph"]]["time"].append(t)
            bg[r["graph"]]["mem"].append(mem / KB_PER_MB)
            if bg[r["graph"]]["rn"] is None:
                bg[r["graph"]]["rn"] = rn
    return {g: {"time": min(d["time"]), "mem": min(d["mem"]),
                "rn": d["rn"]}
            for g, d in bg.items() if d["time"] and d["mem"]}


def placeholder():
    """Valid stand-in float, used while the strategy runs do not line up.

    Emitted rather than left alone because the fragment on disk is then from a
    different rule set than the one the paper describes: a visibly pending figure
    is safer than a plausible but outdated one.  Keeps the label so every \\ref
    to it still resolves and the build stays clean.
    """
    return """% PLACEHOLDER
\\begin{figure}[t]
  \\centering
  \\caption{Time--memory trade-off of the hypergraph neighborhood strategies
    during reduction.}
  \\textit{Pending: the strategy runs (red/nred/fred.tsv) currently come from
    different reduction configurations, so they are not comparable.}
  \\label{fig:neighborhood-mem}
\\end{figure}"""


def main():
    # The three hypergraph strategies must be the same pipeline under different
    # neighborhood policies; if one of them ran a different reduction config, the
    # comparison is meaningless and the figure is replaced by a pending stub.
    if not same_reduction_config(
            {label: os.path.join(INPUT_DIR, f"{stem}.tsv")
             for label, _, stem, is_hyp in STRATEGIES if is_hyp}):
        emit_fragment(fig_path("neighborhood_mem.tex"), placeholder())
        print("PLACEHOLDER figures/neighborhood_mem.tex", file=sys.stderr)
        return 1

    data = {ckey: read_strategy(stem) for _, ckey, stem, _ in STRATEGIES}
    common = sorted(set.intersection(*(set(d) for d in data.values())))
    ng = len(common)

    # ---- table + per-strategy means ----
    rows_tex, centroids = [], {}
    for label, ckey, _, _ in STRATEGIES:
        d = data[ckey]
        times = [d[g]["time"] for g in common]
        mems = [d[g]["mem"] for g in common]
        rns = [d[g]["rn"] for g in common]
        tg, tf = geo_mean_std(times, TIME_SHIFT)
        mg, mf = geo_mean_std(mems, MEM_SHIFT)
        rg = shifted_geomean(rns, RN_SHIFT)
        centroids[ckey] = (tg, mg)
        rows_tex.append(f"    {label} & {rg:.1f} & {tg:.2f} & {tf:.2f} "
                        f"& {mg:.2f} & {mf:.2f} \\\\")

    # Head-to-head vertex counts, hypergraph (any strategy -- they agree) vs the
    # clique route; quoted in the prose and as a note under the table.
    hyp, cli = data["recompute"], data["nored"]
    h_win = sum(1 for g in common if hyp[g]["rn"] < cli[g]["rn"])
    c_win = sum(1 for g in common if cli[g]["rn"] < hyp[g]["rn"])
    ties = ng - h_win - c_win

    # Do the three hypergraph strategies really leave the same instance?  Check
    # instead of asserting: report how many agree and how many differ materially.
    hyp_keys = [c for _, c, _, ok in STRATEGIES if ok]
    disagree, material = [], []
    for g in common:
        rns = [data[c][g]["rn"] for c in hyp_keys]
        if len(set(rns)) > 1:
            disagree.append(g)
            if (max(rns) - min(rns)) / max(max(rns), 1.0) > 0.01:
                material.append(g)
    n_same = ng - len(disagree)

    n_disagree, n_material = len(disagree), len(material)

    # ---- scatter (per-graph clouds + strategy means), all four routes ----
    allt = [data[c][g]["time"] for _, c, _, _ in STRATEGIES for g in common]
    allm = [data[c][g]["mem"] for _, c, _, _ in STRATEGIES for g in common]
    xlo, xhi = min(allt) * 0.6, max(allt) * 1.6
    ylo, yhi = min(allm) * 0.6, max(allm) * 1.6

    plots = []
    # The clouds carry the legend, not the means: the legend labels a strategy,
    # and what the reader looks for is that strategy's instances.  Taking the
    # entries from the means instead would show a mark the figure only contains
    # three of -- bigger, opaque and black-outlined, i.e. visibly not the marks
    # being labelled.  legend image post style enlarges the legend's copy so the
    # 1.8pt cloud mark stays legible without changing shape, fill or colour.
    for i, (label, ckey, _, _) in enumerate(STRATEGIES):
        # Colour and shape already separate the three strategies, so every one
        # is filled solid -- cycling the filling as well would leave one strategy
        # washed out and one hollow for no added information.
        # The per-graph cloud is deliberately lighter than the means, but not
        # so light that the smaller marks (triangles especially) vanish: keep
        # the outline near-opaque and only fade the fill.  The opacities must
        # ride INSIDE mark options (see _render_mark), or they never reach the
        # mark and the cloud comes out as solid as the centroids.
        mark = mark_style(i, f"c{ckey}", size=1.8, fill="solid",
                          extra="fill opacity=0.55, draw opacity=0.9")
        cloud = " ".join(f"({data[ckey][g]['time']:.4g},{data[ckey][g]['mem']:.4g})"
                         for g in common)
        plots.append(
            f"      \\addplot[only marks, color=c{ckey}, {mark}] "
            f"coordinates {{{cloud}}};")
        plots.append(f"      \\addlegendentry{{{label}}}")
    for i, (label, ckey, _, _) in enumerate(STRATEGIES):
        # Same shape, colour and filling as its cloud, but large and outlined in
        # black, so the mean reads as the summary of the cloud it sits in.
        mark = mark_style(i, f"c{ckey}", size=4, fill="solid",
                          extra="draw=black, line width=0.5pt")
        tg, mg = centroids[ckey]
        plots.append(
            f"      \\addplot[only marks, color=c{ckey}, {mark}, forget plot] "
            f"coordinates {{({tg:.4g},{mg:.4g})}};")
    body = "\n".join(plots)

    figure = f"""\\begin{{figure}}[t]
  \\centering
  \\begin{{tikzpicture}}
    \\begin{{axis}}[
      width=\\linewidth, height=0.72\\linewidth,
      scale only axis=false,
      label style={{font=\\small}}, tick label style={{font=\\small}},
      xmode=log, ymode=log,
      xmin={xlo:.4g}, xmax={xhi:.4g}, ymin={ylo:.4g}, ymax={yhi:.4g},
      xlabel={{reduce time [s]}}, ylabel={{peak memory [MB]}},
      legend cell align=left, {LEGEND_ABOVE},
      legend image post style={{mark size=3pt}},
      grid=both, grid style={{gray!20}}, {AXIS_OPEN},
    ]
{body}
    \\end{{axis}}
  \\end{{tikzpicture}}
  \\caption{{The time--memory trade-off of reducing an instance.  Comparing
    \\rmode{{r}} recompute, \\rmode{{p}} precompute, \\rmode{{d}} on-demand, and
    \\rmode[\\star]{{G}} the clique expansion with graph rules.  Each mark is one
    instance, the large outlined mark is the strategy's shifted geometric
    mean.}}
  \\label{{fig:neighborhood-mem}}
\\end{{figure}}"""

    fig_out = emit_fragment(fig_path("neighborhood_mem.tex"), figure)

    print(f"instances common to all strategies = {ng}")
    for _, ckey, _, _ in STRATEGIES:
        tg, mg = centroids[ckey]
        rg = shifted_geomean([data[ckey][g]["rn"] for g in common], RN_SHIFT)
        print(f"    {DESCRIPTIONS[ckey]:17s} rn={rg:8.1f}  time={tg:7.3g}s  mem={mg:6.0f}MB")
    print(f"    remaining-vertex wins: hypergraph {h_win}, clique {c_win}, ties {ties}")
    print(f"    hypergraph strategies agree exactly on {n_same}/{ng}; "
          f"{n_disagree} differ, {n_material} by >1%")
    for g in material:
        print(f"      material difference: {g}")
    print(f"Wrote {fig_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
