#!/usr/bin/env python3
"""Hypergraph reductions vs. clique expansion + graph reductions (same rules).

Compares reducing the hypergraph directly against first materializing the clique
expansion and then running the graph reduction engine with the same rule types.

Data sources (both are raw run files, no derived sidecar):
    results/RED/fred.tsv   hypergraph reductions (on-demand, the paper's default)
      graph algo n m e rn rm re offset time seed mem
    results/RED/gred.tsv   clique expansion + graph reductions
      graph algo n m gn gm rn rm offset time seed mem

`rn` is the number of remaining (unreduced) vertices on both sides and refers to
the same original vertex set, so the two are directly comparable.  `gm` is the
edge count *after* the clique expansion, which quantifies the blow-up.  The
reduction result is deterministic across seeds, so sizes are taken from the
first seed and time/memory are the per-seed minimum (min-of-k).

This script emits no paper artifact: the aggregate comparison lives in the merged
per-strategy table (make_strategy_table.py, which lists the clique expansion as a
fourth reduction strategy).  It only prints the summary numbers quoted in the
hypergraph-vs-clique paragraph of sec/exp_reductions.tex, so it is run by hand
when those numbers need refreshing rather than as part of make_all_paper.py.

Instances missing from either side (e.g. the clique expansion crashing on a
graph that is too dense to materialize) are excluded from the aggregates and
reported separately -- they are themselves a result.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    read_rows, same_reduction_config, shifted_geomean,
)

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR = os.path.join(HERE, "results", "RED")
# On-demand is the default hypergraph strategy (see Section 5.2), so the
# comparison against the clique-expansion route is measured against it.
HYPER_TSV = os.path.join(INPUT_DIR, "fred.tsv")
GRAPH_TSV = os.path.join(INPUT_DIR, "gred.tsv")

# TIME_SHIFT matches make_strategy_table/make_neighborhood_memory (reduction
# times are small), so the on-demand/graph reduction times reported here agree
# with the strategy table in Section 5.2 rather than reading differently.
RN_SHIFT, TIME_SHIFT, MEM_SHIFT = 1.0, 0.01, 1.0
TIME_LIMIT = 3600.0


def aggregate(path, size_keys):
    """Per graph: sizes from seed one, time/memory minimised over the seeds.

    The reduction is deterministic, so the run-to-run spread in time and peak
    memory is pure measurement noise; the per-seed minimum is the cleanest
    estimate of the true cost (min-of-k).

    A run that hit the memory bound or died is written with negative sentinel
    sizes (-1 crash, -2 out of memory); such a graph is dropped so it shows up
    as unavailable on that side rather than as a bogus size.
    """
    if not os.path.exists(path):
        return {}
    _, rows = read_rows(path)
    by_graph = {}
    for r in rows:
        by_graph.setdefault(r["graph"], []).append(r)
    out = {}
    for graph, rs in by_graph.items():
        try:
            rec = {k: float(rs[0][k]) for k in size_keys}
            rec["time"] = min(float(x["time"]) for x in rs)
            rec["mem"] = min(float(x["mem"]) for x in rs) / 1024.0  # MB
        except (KeyError, ValueError):
            continue
        if any(rec[k] < 0 for k in size_keys):
            continue  # out of memory / crashed -- no reduced instance to compare
        out[graph] = rec
    return out


def winner(h, g):
    if h["rn"] < g["rn"]:
        return "hyper"
    if g["rn"] < h["rn"]:
        return "graph"
    return "tie"


def main():
    # The hypergraph side must be the rule set the paper describes; compare its
    # config token against the on-demand run rather than trusting the file.
    # Reference against precompute (nred.tsv), the other run kept current with the
    # rule set; recompute (red.tsv) may lag behind and this script never reads it.
    if not same_reduction_config(
            {"fred.tsv": HYPER_TSV,
             "nred.tsv": os.path.join(INPUT_DIR, "nred.tsv")}):
        print("fred.tsv comes from a different reduction configuration than the "
              "current rule set -- numbers not comparable.", file=sys.stderr)
        return 1

    hyper = aggregate(HYPER_TSV, ["n", "m", "rn", "rm", "offset"])
    graph = aggregate(GRAPH_TSV, ["n", "m", "gn", "gm", "rn", "rm", "offset"])

    common = sorted(set(hyper) & set(graph))
    if not common:
        print("red.tsv / gred.tsv missing or share no instance.", file=sys.stderr)
        return 1

    pairs = [(g, hyper[g], graph[g]) for g in common]
    missing = sorted(set(hyper) - set(graph))

    # Numbers quoted in the hypergraph-vs-clique paragraph of sec/exp_reductions.tex.
    hs = [h for _, h, _ in pairs]
    gs = [g for _, _, g in pairs]
    blow = [g["gm"] / max(g["m"], 1) for _, _, g in pairs]
    print(f"instances compared      : {len(pairs)}")
    if missing:
        print(f"clique side unavailable : {len(missing)} ({', '.join(missing)})")
    print(f"rn  sgm  hyper / graph  : "
          f"{shifted_geomean([r['rn'] for r in hs], RN_SHIFT):.1f} / "
          f"{shifted_geomean([r['rn'] for r in gs], RN_SHIFT):.1f}")
    print(f"t   sgm  hyper / graph  : "
          f"{shifted_geomean([r['time'] for r in hs], TIME_SHIFT):.2f} / "
          f"{shifted_geomean([r['time'] for r in gs], TIME_SHIFT):.2f}")
    print(f"mem sgm  hyper / graph  : "
          f"{shifted_geomean([r['mem'] for r in hs], MEM_SHIFT):.1f} / "
          f"{shifted_geomean([r['mem'] for r in gs], MEM_SHIFT):.1f} MB")
    print(f"peak mem max hyper/graph: "
          f"{max(r['mem'] for r in hs) / 1024:.1f} / "
          f"{max(r['mem'] for r in gs) / 1024:.1f} GB")
    print(f"vertex wins h/g/tie     : "
          f"{sum(1 for _, h, g in pairs if winner(h, g) == 'hyper')} / "
          f"{sum(1 for _, h, g in pairs if winner(h, g) == 'graph')} / "
          f"{sum(1 for _, h, g in pairs if winner(h, g) == 'tie')}")
    print(f"memory wins hyper       : "
          f"{sum(1 for _, h, g in pairs if h['mem'] < g['mem'])}")
    print(f"time wins hyper         : "
          f"{sum(1 for _, h, g in pairs if h['time'] < g['time'])}")
    print(f"edge blow-up sgm / max  : {shifted_geomean(blow, 1.0):.1f}x / "
          f"{max(blow):.0f}x")
    print(f"hypergraph timeouts     : "
          f"{sum(1 for r in hs if r['time'] >= TIME_LIMIT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
