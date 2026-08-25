#!/usr/bin/env python3
"""Section 5.1 artifacts: overall reduction effect on the instance size.

Emits (into PAPER_ROOT):
  tables/reduction_effect.tex   -- tab:reduction_effect, averaged instance
                                   properties before/after the reduction

Source: results/RED/fred.tsv (on-demand, the default strategy).  The reduction
is deterministic across seeds (rn/rm/re are identical, only the time varies), so
sizes come from one row per instance and the reduction time is the per-instance
minimum over seeds (min-of-k, the cleanest estimate of the noise-free cost).
"""

import math
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from paper_plot_common import (  # noqa: E402
    RESULTS_DIR,
    PAPER_ROOT, emit_fragment, min_by_graph, read_rows, same_reduction_config,
    shifted_geomean, tab_path,
)

# On-demand (fred.tsv) is the default reduction strategy used throughout the
# later experiments (see Section 5.2), so the reduction-effect table and its
# prose report it.  It also completes on every instance, whereas the recompute
# run (red.tsv) times out on a few and would report their partial -- larger --
# reduced sizes.
RED_TSV = os.path.join(RESULTS_DIR, "RED", "fred.tsv")
# Reference run for the rule set: a different neighborhood strategy, but the
# same reduction configuration, so its reduceX token is what fred.tsv must carry.
# We use precompute (nred.tsv) rather than recompute (red.tsv) because it is the
# other run kept current with the rule set; a stale red.tsv would spuriously trip
# the config guard here even though this table never reads recompute data.
REF_TSV = os.path.join(RESULTS_DIR, "RED", "nred.tsv")
# Clique-expansion edge counts (gm/rgm) for the blow-up summary row.
CLIQUE_TSV = os.path.join(RESULTS_DIR, "RED", "clique.tsv")
TIME_LIMIT = 3600.0


def clique_blowup_maps():
    """(b_map, br_map): graph -> clique-expansion blow-up, from clique.tsv.

    b = gm/m of the original instance, br = rgm/rm of the reduced kernel (only
    where the kernel is non-empty).  The blow-up is seed-independent, so we keep
    the first row per graph.  Keyed by graph so the table can restrict each blow-up
    aggregate to the fully-reducible or the remaining subgroup."""
    b_map, br_map = {}, {}
    if not os.path.exists(CLIQUE_TSV):
        return b_map, br_map
    _, rows = read_rows(CLIQUE_TSV, warn=False)
    for r in rows:
        g = r["graph"]
        try:
            m, rm = float(r["m"]), float(r["rm"])
            gm, rgm = float(r["gm"]), float(r["rgm"])
        except (KeyError, ValueError):
            continue
        if g not in b_map and m > 0 and gm >= 0:
            b_map[g] = gm / m
        if g not in br_map and rm > 0 and rgm >= 0:
            br_map[g] = rgm / rm
    return b_map, br_map


def hsize(n, m, e):
    """|H| = number of pins Sum_e |e| = m * e (total vertex--hyperedge incidences).

    The standard hypergraph-size measure: it is what a hypergraph file stores (the
    per-hyperedge vertex lists) and what dominates memory and runtime.  n is
    accepted for a uniform (n, m, e) call signature but does not enter the size."""
    return m * e


def placeholder():
    """Valid stand-in table while red.tsv is from a different rule set."""
    return """% PLACEHOLDER
\\begin{table}
  \\caption{Average instance properties before and after our reduction routine.}
  \\label{tab:reduction_effect}
  \\centering
  \\textit{Pending: red.tsv was produced with a different reduction
    configuration than the current rule set.}
\\end{table}"""


def main():
    # fred.tsv drives this table (on-demand, the default strategy); a run made
    # with a stale -rX flag would silently understate the reduction, so we check
    # it carries the same reduceX token as the recompute run.
    if not same_reduction_config({"fred.tsv": RED_TSV, "nred.tsv": REF_TSV}):
        emit_fragment(tab_path("reduction_effect.tex"), placeholder())
        print("PLACEHOLDER tables/reduction_effect.tex", file=sys.stderr)
        return 1

    _, all_rows = read_rows(RED_TSV)
    # Sizes are deterministic across seeds -> one row per instance (seed 1); the
    # reduction time is the per-instance minimum over seeds (min-of-k).
    rows = [r for r in all_rows if r.get("seed") == "1"]
    if not rows:
        print(f"[skip] no rows in {RED_TSV}")
        return
    min_time = min_by_graph(all_rows, "time")

    # Split the instances by reduction outcome.  Those the routine removes
    # entirely (empty reduced kernel) are a distinct population -- large-edged,
    # very high blow-up -- and averaging their zeros into the reduced column would
    # understate the kernels that actually remain.  So the table reports the fully
    # reducible instances at original size only, and the remaining ones before and
    # after reduction; the blow-up aggregates likewise restrict to each subgroup.
    full_rows = [r for r in rows if float(r["rn"]) == 0.0]
    rem_rows = [r for r in rows if float(r["rn"]) > 0.0]
    n_full, n_rem = len(full_rows), len(rem_rows)
    b_map, br_map = clique_blowup_maps()

    GRAY = "black!55"

    def geo_sd(vals, shift=1.0):
        logs = [math.log(v + shift) for v in vals]
        return math.exp(st.pstdev(logs))

    def gstats(rs, reduced):
        """Per-metric value lists for one group (original or reduced columns)."""
        p = "r" if reduced else ""
        gn = [float(r[p + "n"]) for r in rs]
        gm = [float(r[p + "m"]) for r in rs]
        ge = [float(r[p + "e"]) for r in rs]
        gH = [hsize(a, b, c) for a, b, c in zip(gn, gm, ge)]
        # ni==0 for the emptied instances in an all-instance reduced row; those
        # have no defined average degree, so drop them from the degree mean.
        gdeg = [mi * ei / ni for ni, mi, ei in zip(gn, gm, ge) if ni > 0]
        src = br_map if reduced else b_map
        gb = [src[r["graph"]] for r in rs if r["graph"] in src]
        return gn, gm, ge, gdeg, gH, gb

    def pair(vals, digits=0):
        """A value + gray +-std cell pair (arithmetic mean)."""
        return (f"\\numprint{{{st.mean(vals):.{digits}f}}} & "
                f"\\textcolor{{{GRAY}}}{{$\\pm$\\,"
                f"\\numprint{{{st.pstdev(vals):.{digits}f}}}}}")

    def hcell(vals):
        """|H| in a single grouped cell: (mean +- std) x 10^6."""
        return (f"$({st.mean(vals) / 1e6:.1f}\\,\\textcolor{{{GRAY}}}"
                f"{{\\pm\\,{st.pstdev(vals) / 1e6:.1f}}})\\times 10^{{6}}$")

    def bcell(vals):
        """Blow-up: shifted geomean + gray multiplicative spread factor."""
        if not vals:
            return "{--} & {}"
        return (f"\\numprint{{{shifted_geomean(vals, 1.0):.2f}}} & "
                f"\\textcolor{{{GRAY}}}{{$\\times$\\,"
                f"\\numprint{{{geo_sd(vals):.2f}}}}}")

    def trow(label, rs, reduced, intensive=True):
        """One table row.  intensive=False dashes the average edge size, degree,
        and blow-up -- undefined (or, over the survivors, merely a copy of the
        remaining-reduced row) for an all-instance reduced aggregate."""
        gn, gm, ge, gdeg, gH, gb = gstats(rs, reduced)
        dash = r"\multicolumn{2}{c}{--}"
        e_cell = pair(ge, 2) if intensive else dash
        d_cell = pair(gdeg, 2) if intensive else dash
        b_cell = bcell(gb) if intensive else dash
        return (f"        {label} & {pair(gn)} & {pair(gm)} & {e_cell} "
                f"& {d_cell} & {hcell(gH)} & {b_cell} \\\\")

    n_all = len(rows)
    # The fully reduced instances have an empty kernel, so their reduced ($H'$)
    # row is all zeros; print it literally (a single centered 0 / dash) rather
    # than a degenerate 0 +- 0 mean.
    zero_row = (f"        fully reducible, $H'$ (${n_full}$)"
                r" & \multicolumn{2}{c}{0} & \multicolumn{2}{c}{0}"
                r" & \multicolumn{2}{c}{--} & \multicolumn{2}{c}{--}"
                r" & \numprint{0} & \multicolumn{2}{c}{--} \\")
    tbl_rows = [
        trow(f"all, $H$ (${n_all}$)", rows, False),
        trow(f"all, $H'$ (${n_all}$)", rows, True, intensive=False),
        r"        \midrule",
        trow(f"remaining, $H$ (${n_rem}$)", rem_rows, False),
        trow(f"remaining, $H'$ (${n_rem}$)", rem_rows, True),
        r"        \midrule",
        trow(f"fully reducible, $H$ (${n_full}$)", full_rows, False),
        zero_row,
    ]

    caption = (
        r"Instance properties before ($H$) and after ($H'$) our reduction routine. The first"
        r" two rows aggregate all $" f"{n_all}" r"$ instances; below, we split them"
        r" into the $" f"{n_full}" r"$ \emph{fully reducible} instances, which"
        r" preprocessing solves outright, and the $" f"{n_rem}" r"$ \emph{remaining}"
        r" ones before and after reduction. Columns: vertices $n$, edges $m$, average"
        r" edge size $\bar e$, average degree $\bar d$, size $|H|$, and the"
        r" clique-expansion blow-up $b$. We dash $\bar e$,"
        r" $\bar d$, and $b$ for the all-instance reduced row, as they are undefined"
        r" on the fully reduced instances.")

    colspec = r"l r@{\,}l r@{\,}l r@{\,}l r@{\,}l c r@{\,}l"
    header = (r"        & \multicolumn{2}{c}{$n$} & \multicolumn{2}{c}{$m$}"
              r" & \multicolumn{2}{c}{$\bar e$} & \multicolumn{2}{c}{$\bar d$}"
              r" & $|H|$ & \multicolumn{2}{c}{$b$} \\")
    body = [
        r"\begin{table*}[t]",
        r"    \centering",
        r"    \small",
        f"    \\caption{{{caption}}}",
        r"    \label{tab:reduction_effect}",
        r"    \setlength{\tabcolsep}{5pt}",
        r"    \resizebox{\textwidth}{!}{%",
        f"    \\begin{{tabular}}{{{colspec}}}",
        header,
        r"        \midrule",
    ] + tbl_rows + [
        r"        \bottomrule",
        r"    \end{tabular}}",
        r"\end{table*}",
    ]
    emit_fragment(tab_path("reduction_effect.tex"), "\n".join(body))

    # --- prose numbers for sec/exp_reductions.tex ---
    def col(key):
        return [float(r[key]) for r in rows]
    n, m, e = col("n"), col("m"), col("e")
    rn, rm, re = col("rn"), col("rm"), col("re")
    H = [hsize(*x) for x in zip(n, m, e)]
    Hr = [hsize(*x) for x in zip(rn, rm, re)]
    rem = [100.0 * b / a for a, b in zip(H, Hr)]
    t = [min_time[r["graph"]] for r in rows]
    full = n_full

    # Numbers quoted in the prose of sec/exp_data_reductions.tex.
    slowest = max(rows, key=lambda r: min_time[r["graph"]])
    print(f"PAPER_ROOT = {PAPER_ROOT}")
    print(f"instances                 : {len(rows)}")
    print(f"fully reduced (empty H)   : {full}")
    print(f"reduced by factor >= 2    : {sum(1 for x in rem if x <= 50)}")
    print(f"mean remaining |H| (mor)  : {st.mean(rem):.1f} %")
    print(f"remaining |H| (rom, table): {100.0 * sum(Hr) / sum(H):.1f} %  <- 0.5/2.6, headline")
    print(f"reduced in < 1 s          : {sum(1 for x in t if x < 1)}")
    print(f"t > 1 s and > 50 % left   : {sum(1 for a, b in zip(t, rem) if a > 1 and b > 50)}")
    print(f"median time               : {st.median(t):.2f} s")
    print(f"shifted geomean time (s=1): {shifted_geomean(t, 1.0):.2f} s")
    print(f"timeouts (>= {TIME_LIMIT:.0f} s)     : {sum(1 for x in t if x >= TIME_LIMIT)}")
    print(f"slowest                   : {slowest['graph']} ({min_time[slowest['graph']]:.1f} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
