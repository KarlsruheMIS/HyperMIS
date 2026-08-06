#!/usr/bin/env python3
"""One table per reduction strategy: what the reduction costs, and what the ILP
then costs on the result.

Every strategy applies the same rule set and differs only in how the instance is
represented and how vertex neighborhoods are obtained, so they share a row space:

    strategy                       reduction run   ILP w/o red.  ILP w/ red.
    recompute                      red.tsv         ilp           rilp
    precompute                     nred.tsv        ilp           nrilp
    on-demand                      fred.tsv        ilp           frilp
    on-demand, clique-expanded ILP fred.tsv        gfilp         gfrilp
    clique exp. + graph reductions gred.tsv        gfilp         grilp

The neighborhood mode is a property of the reduction, so all three hypergraph
rows are measured against the same unreduced ILP; the nilp/filp runs (the same
ILP under a mode that has nothing to act on) are not used.

Note the two clique-related rows are different pipelines and must not be merged.
gfrilp reduces the HYPERGRAPH first (on-demand) and only then clique-expands for
the ILP, so its reduction columns are the on-demand ones.  gred.tsv instead
clique-expands first and runs the graph reduction rules on the result, which the
ILP then solves (grilp); its baseline is gfilp, the same expansion and ILP with
no reductions at all.

The table therefore has three column groups:

  reduction  rn (remaining vertices), reduce time, peak memory
  ILP        solved, run time, peak memory, penalised time over all instances
  gain       speedup and memory factor of the reduced ILP over the unreduced one

Only the reduction-enabled ILP configuration is listed per strategy; its
unreduced counterpart is not a row, it is what the speedup/factor columns are
measured against.

Aggregation bases (stated once in the section's methodology paragraph):
  * reduction columns -- the instances every strategy completed.
  * ILP run time / memory and the gain columns -- the instances solved by both
    settings of that strategy's pair.
  * t all -- every instance solved by any configuration, unsolved ones penalised
    at the time limit (PAR1-style).

A strategy whose solver sweep has not finished keeps its reduction columns and
leaves the solver/gain cells empty (see MIN_COVERAGE), rather than reporting
numbers measured on whichever instances happened to run first.

Output: tables/strategies.tex (table*, tab:strategies) in the paper tree.
"""

import math
import os
import statistics as st
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    GRAPH_METHODS, ILP_METHODS, config_mark_style, cfg_macro, color_name,
    emit_fragment, geo_mean_std,
    read_ilp_method_values, read_rows, read_solver_rows,
    same_reduction_config, shifted_geomean, tab_path,
)

HERE = os.path.dirname(os.path.abspath(__file__))
RED_DIR = os.path.join(HERE, "results", "RED")
ILP_DIR = os.path.join(HERE, "results", "ILP")
GRAPH_DIR = os.path.join(HERE, "results", "GRAPH")

KB_PER_MB = 1024.0
RED_TIME_SHIFT = 0.01   # s -- reduction times are small
TIME_SHIFT = 1.0        # s
MEM_SHIFT = 1.0         # MB
MEM_LIMIT_KB = 32 * 1024 * 1024   # the 32 GB bound of the experimental setup
RN_SHIFT = 1.0          # vertices

# (solver domain, strategy label, reduction tsv stem, ILP without red, ILP with
# red).  The table is grouped by the solver domain: the reduced instance is
# either solved directly on the hypergraph, or clique-expanded and solved on the
# graph.  The on-demand reduction appears in both groups -- same reduced
# hypergraph, different downstream solver -- so the two rows share reduction
# columns.  The graph-reductions row instead applies the rules to the clique
# expansion first and runs no ILP.  A None ILP entry means no ILP was run.
# (domain, strategy label, reduction tsv stem, solver without red, solver with
# red).  `domain` groups the rows and picks the row mark: hypergraph rows solve
# the reduced hypergraph directly; graph rows ($\star$) clique-expand first; the
# transpose row ($^{\mathsf{T}}$) solves the strong IS as a (b-)matching on the
# transposed (dual) hypergraph.  The SOTA rows (struction, satreduce, vc,
# bmatching) all take the same on-demand reduced hypergraph as gfrilp -- shown
# once per group.  A row whose reduced counterpart has no runs yet falls back to
# displaying the unreduced solver and leaves the Gain columns empty.
# The neighborhood mode is a property of the REDUCTION, not of the solve, so all
# three hypergraph rows share one baseline: the plain unreduced ILP on the
# hypergraph (ilp).  nilp/filp measured the same configuration under a mode that
# never applies without reductions, so they are not used.
STRATEGIES = [
    ("hypergraph", "recompute", "red", "ilp", "rilp"),
    ("hypergraph", "precompute", "nred", "ilp", "nrilp"),
    ("hypergraph", "on-demand", "fred", "ilp", "frilp"),
    ("transpose", "on-demand", "fred", "bmatching", "rbmatching"),
    ("graph", "on-demand", "fred", "gfilp", "gfrilp"),
    ("graph", "on-demand", "fred", "struction", "rstruction"),
    ("graph", "on-demand", "fred", "satreduce", "rsatreduce"),
    ("graph", "on-demand", "fred", "vc_solver", "rvc_solver"),
    # The clique expansion is reduced with the GRAPH rules and then solved by
    # the ILP (grilp).  Its without-reductions counterpart is gfilp: the same
    # clique expansion, solved by the same ILP, with no reductions at all.
    ("graph", "graph reductions", "gred", "gfilp", "grilp"),
]
# Strategy label -> the subscript that names it in \strong$_x$, the same symbol
# the configuration macros use (see defs.tex), so a row's strategy column and the
# configuration names in the figures spell the preprocessing identically.
MODE = {
    "recompute": "r",
    "precompute": "p",
    "on-demand": "d",
    "graph reductions": "G",
}

# De-duplicated: ilp is now the baseline of all three hypergraph rows.
ILP_ALL = list(dict.fromkeys(m for _, _, _, a, b in STRATEGIES for m in (a, b) if m))

NA = "{--}"


def read_reduction(stem):
    """graph -> {'time', 'mem', 'rn'}; time/mem per-seed minimum (min-of-k).

    The reduction is deterministic, so the run-to-run spread in time and peak
    memory is pure measurement noise and the minimum over seeds is the cleanest
    estimate of the true cost.
    """
    _, rows = read_rows(os.path.join(RED_DIR, f"{stem}.tsv"), warn=False)
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


# A configuration must have runs for at least this fraction of the instances the
# best-covered configuration has, or it counts as still running (see main).
MIN_COVERAGE = 0.9


def covered_instances(method):
    """Instances a configuration has any run for, solved or not."""
    d = GRAPH_DIR if method in GRAPH_METHODS else ILP_DIR
    path = os.path.join(d, f"{method}.tsv")
    if not os.path.exists(path):
        return set()
    return {r["graph"] for r in read_solver_rows(path, warn=False)}


def detect_time_limit():
    """The wall-clock limit the sweep was run with, read off the runs themselves.

    A run that finishes without proving optimality has used up the limit, so the
    smallest such time is the limit.  Only runs that actually ran out of *time*
    qualify: a run killed by the memory limit also reports opt=0, but at whatever
    time it died (size=-2, sometimes only a few minutes in), and taking that as
    the limit would silently under-penalise every unsolved instance in `t all`.
    """
    tmin = math.inf
    for m in ILP_ALL:
        path = os.path.join(ILP_DIR, f"{m}.tsv")
        if not os.path.exists(path):
            continue
        rows = read_solver_rows(path, warn=False)
        for r in rows:
            try:
                if int(r["opt"]) == 0 and float(r["size"]) > 0 \
                        and float(r.get("mem", 0)) <= MEM_LIMIT_KB:
                    tmin = min(tmin, float(r["time"]))
            except (KeyError, ValueError):
                continue
    return round(tmin) if math.isfinite(tmin) else 30


def placeholder():
    """Valid stand-in table, used while the strategy runs do not line up.

    Emitted rather than left alone: the fragment on disk would otherwise report a
    different rule set than the one the paper describes.  Keeps the label so
    every \\ref to it still resolves.
    """
    return """% PLACEHOLDER
\\begin{table*}[t]
  \\centering \\small
  \\caption{Cost of each reduction strategy and of the solver run on its result.}
  \\label{tab:strategies}
  \\textit{Pending: the strategy runs (red/nred/fred.tsv) currently come from
    different reduction configurations, so they are not comparable.}
\\end{table*}"""


def main():
    # Same guard as in make_neighborhood_memory.py: the hypergraph strategies
    # share one rule set, so a file carrying a different reduceX token is a bad
    # run and would show up here as a strategy that reduces far less.  Every
    # column group is anchored on those runs, so the whole table waits.
    if not same_reduction_config(
            {label: os.path.join(RED_DIR, f"{stem}.tsv")
             for dom, label, stem, _, _ in STRATEGIES if dom == "hypergraph"}):
        emit_fragment(tab_path("strategies.tex"), placeholder())
        print("PLACEHOLDER tables/strategies.tex", file=sys.stderr)
        return 1

    red = {stem: read_reduction(stem) for _, _, stem, _, _ in STRATEGIES}
    red_common = sorted(set.intersection(*(set(d) for d in red.values())))
    n_red = len(red_common)

    available, tvals = read_ilp_method_values(ILP_DIR, "time", methods=ILP_ALL)
    _, mvals = read_ilp_method_values(ILP_DIR, "mem", methods=ILP_ALL, warn=False)
    tl = detect_time_limit()

    universe = sorted({g for gv in tvals.values() for g in gv})
    n_any = len(universe)

    # Drop configurations whose sweep is still running.  Coverage is the number
    # of UNIVERSE instances a configuration has any run for -- unlike the solved
    # count it does not drop with difficulty, so a config far below full coverage
    # has simply not finished.  Measuring against the universe rather than
    # against the best-covered file is what makes this safe: the graph solvers
    # are deliberately run only on the instances some configuration can solve,
    # not on all 75, and must not be mistaken for unfinished sweeps.  Reporting a
    # partial config anyway would compare its solved count against the full
    # universe and measure its Gain ratios on whichever instances happened to run
    # first -- which is exactly the easy head of the sweep.
    coverage = {m: len(covered_instances(m) & set(universe)) for m in available}
    partial = [m for m, c in coverage.items() if c < MIN_COVERAGE * n_any]
    for m in partial:
        print(f"[warn] {m}: runs for only {coverage[m]}/{n_any} instances -- "
              f"sweep still in progress, its cells are left empty", file=sys.stderr)
        available.remove(m)

    # Marker each config is drawn with in the perf-profile / cactus figures, keyed
    # the same way (position among the plotted methods), so the table can show the
    # matching icon next to a config token for a visual cross-reference.
    fig_methods, _ = read_ilp_method_values(ILP_DIR, "time", methods=ILP_METHODS,
                                            warn=False)
    config_mark = {m: config_mark_style(m, color_name(m), size=1.7,
                                        fallback_index=i)
                   for i, m in enumerate(fig_methods)}

    rows, pair_sizes = [], []
    for solver, label, stem, base, redm in STRATEGIES:
        d = red[stem]
        # The config shown/aggregated is this strategy's reduction-enabled
        # solver.  We do NOT fall back to the unreduced one when it is missing:
        # every unreduced config is already some row's baseline, so showing it
        # here would print a no-reductions number under a reductions label.  A
        # row whose reduced run is pending stays empty until it lands.
        disp = redm if (redm is not None and redm in available) else None
        rec = {
            "solver": solver, "label": label, "stem": stem, "base": base,
            "red": redm, "disp": disp, "reduced": disp == redm,
            "rn": shifted_geomean([d[g]["rn"] for g in red_common], RN_SHIFT),
            "rt": geo_mean_std([d[g]["time"] for g in red_common], RED_TIME_SHIFT),
            "rm": geo_mean_std([d[g]["mem"] for g in red_common], MEM_SHIFT),
        }
        # Flat central values of the reduction time/memory, so they can go
        # through bf() and be bolded like the other columns.
        rec["rt0"], rec["rm0"] = rec["rt"][0], rec["rm"][0]
        if disp is not None:
            rec["solved"] = len(tvals[disp])
            rec["t_all"] = shifted_geomean(
                [tvals[disp].get(g, tl) for g in universe], TIME_SHIFT)
            # Gain columns need BOTH the reduced and unreduced runs of this
            # strategy; only then is the with/without comparison meaningful.
            if (redm is not None and redm in available
                    and base is not None and base in available):
                common = sorted(set(tvals[base]) & set(tvals[redm]))
                pair_sizes.append(len(common))
                tb = shifted_geomean([tvals[base][g] for g in common], TIME_SHIFT)
                tr = shifted_geomean([tvals[redm][g] for g in common], TIME_SHIFT)
                mb = shifted_geomean(
                    [mvals[base][g] / KB_PER_MB for g in common], MEM_SHIFT)
                mr = shifted_geomean(
                    [mvals[redm][g] / KB_PER_MB for g in common], MEM_SHIFT)
                # Gain: instances the reduction makes solvable that the unreduced
                # solver cannot (a with/without-reduction benefit).
                rec["gained"] = len(set(tvals[redm]) - set(tvals[base]))
                rec.update({"t": tr, "m": mr,
                            "speedup": tb / tr, "factor": mb / mr})
        rows.append(rec)

    def vals(key):
        return [r[key] for r in rows if r.get(key) is not None]

    # Only bold columns whose rows share a common instance set, so "best" is a
    # fair cross-row comparison: solved and t all range over the full universe,
    # and the three Reduction columns (rn, rt0, rm0) over the instances every
    # strategy completed.  The Gain speedup/factor are each measured on that
    # row's OWN mutually-solved subset, so a cross-row max there would compare
    # different, differently-hard instance sets -- those stay unbolded.
    best = {k: (max(vals(k)) if k == "solved" else min(vals(k)))
            for k in ("solved", "t_all", "rn", "rt0", "rm0")
            if vals(k)}

    def bf(rec, key, fmt="{:.2f}"):
        v = rec.get(key)
        if v is None:
            return NA
        text = fmt.format(v)
        return f"{{\\bfseries {text}}}" if key in best and v == best[key] else text

    def marker(m):
        # The config's figure marker, drawn inline so a row matches its curve.
        # Always a fixed-width box, so the configuration names line up whether or
        # not the row's config appears in the figures -- rilp and nrilp do not,
        # and without the box their names would start a marker-width further left
        # than every other row's.
        mk = config_mark.get(m)
        icon = ""
        if mk:
            col = color_name(m)
            icon = (f"\\tikz[baseline=-0.55ex]{{\\draw[{col}] plot[only marks, "
                    f"{mk}] coordinates {{(0,0)}};}}")
        return f"\\makebox[1em][l]{{{icon}}}"

    def cfg_cell(r):
        # This row's full configuration name (reduction prefix + solver, e.g.
        # R_d-Struction), prefixed with its figure marker where the row's
        # configuration is plotted, so a row can be matched to its curve and every
        # solver carries its R_x prefix explicitly.
        m = r.get("disp")
        if m is None:
            return NA
        # Representation marker on the config's R superscript: a star for the
        # clique expansion, T for the transposed hypergraph (same as the \rmode
        # column), passed as the config macro's optional argument.
        dom = {"graph": "\\star", "transpose": "\\mathsf{T}"}.get(r["solver"], "")
        opt = f"[{dom}]" if dom else ""
        return f"{marker(m)}{cfg_macro(m)}{opt}"

    def row_cells(r, share):
        # `share`: this row uses the same reduced instance as the row above (same
        # strategy and stem), so its reduction columns are left blank to avoid
        # repeating identical numbers -- only the downstream solver differs.
        sp = f"{r['speedup']:.2f}" if r.get("speedup") else NA
        fa = f"{r['factor']:.2f}" if r.get("factor") else NA
        gained = f"{r['gained']}" if r.get("gained") is not None else NA
        if share:
            label, rn, rt, rm = "", "{}", "{}", "{}"
        else:
            # The strategy is named exactly as the configurations are, through
            # the same \rmode macro that Section 4 introduces them with: \strong
            # subscripted with the neighborhood mode the reduction ran in.  A
            # superscript star marks a solver running on the clique expansion (a
            # graph), a superscript T the one solving on the transposed (dual)
            # hypergraph; solvers on the hypergraph itself carry neither.
            dom = {"graph": "\\star",
                   "transpose": "\\mathsf{T}"}.get(r["solver"], "")
            opt = f"[{dom}]" if dom else ""
            label = f"\\rmode{opt}{{{MODE[r['label']]}}}"
            rn, rt = bf(r, "rn", "{:.0f}"), bf(r, "rt0")
            rm = bf(r, "rm0")
        return (f"{label} "
                f"& {rn} & {rt} & {rm} "
                f"& {cfg_cell(r)} "
                f"& {bf(r, 'solved', '{:.0f}')} "
                f"& {bf(r, 't_all')} "
                f"& {gained} & {sp} & {fa}")

    # Group consecutive rows by solver domain; \multirow labels the group and a
    # \midrule separates the hypergraph block from the graph block.
    groups = []
    for r in rows:
        if not groups or groups[-1][0] != r["solver"]:
            groups.append((r["solver"], []))
        groups[-1][1].append(r)
    body_lines = []
    for gi, (solver, grp) in enumerate(groups):
        if gi > 0:
            body_lines.append("    \\midrule")
        prev = None
        for ri, r in enumerate(grp):
            share = prev is not None and (r["label"], r["stem"]) == prev
            body_lines.append(f"    {row_cells(r, share)} \\\\")
            prev = (r["label"], r["stem"])
    body = "\n".join(body_lines)

    npair = pair_sizes[0] if pair_sizes else 0
    missing = [r for r in rows
               if r.get("solved") is not None and r.get("speedup") is None]
    miss_note = ""
    if missing:
        parts = ", ".join(f"{r['solver']} {r['label']} ({cfg_macro(r['disp'])})"
                          for r in missing)
        s = "" if len(missing) == 1 else "s"
        miss_note = (f" Row{s} {parts} lack{'s' if not s else ''} a reduced or "
                     f"unreduced counterpart, so the Gain columns there are empty.")

    frag = f"""\\begin{{table*}}[t]
  \\centering
  \\caption{{Reduction and solving cost per strategy on solvable instances. The
    arrows {{\\color{{black!45}}$\\downarrow$}} and {{\\color{{black!45}}$\\uparrow$}}
    mark the better direction, and the best value per column is bold.
    \\emph{{Reduction}} gives the configuration \\rmode{{x}}, the remaining
    vertices, the reduction time $t^{{\\mathrm{{red}}}}$ and the peak memory. A superscript $\\star$
    marks clique expansion and $\\mathsf{{T}}$ marks $H^{{\\mathsf{{T}}}}$.
    \\emph{{Solving}} gives the total number of instances solved and
    \\emph{{$t$ all}}, the run time over all instances with unsolved ones penalised
    at the ${tl:.0f}$\\,s limit.
    \\emph{{Reduction gain}} compares each solver with and without our reductions.
    This includes the number of instances solvable due to reductions
    (\\emph{{\\#solved}}), \\emph{{speedup}} and \\emph{{memory}} giving run time and
    peak-memory improvement factors on instances each method solves both ways. The
    mutually solved set differs per row, so these three columns are not comparable
    across rows. Blank cells repeat the numbers above.{miss_note}}}
  \\label{{tab:strategies}}
  \\setlength{{\\tabcolsep}}{{2.7pt}}\\small
  \\begin{{tabular}}{{l S[table-format=4.0] S[table-format=2.2] S[table-format=3.2] l S[table-format=2.0] S[table-format=3.2] S[table-format=2.0] S[table-format=1.2] S[table-format=1.2]}}
      \\multicolumn{{4}}{{c}}{{reduction}} & \\multicolumn{{3}}{{c}}{{solving}} & \\multicolumn{{3}}{{c}}{{reduction gain}} \\\\
    \\cmidrule(lr){{1-4}} \\cmidrule(lr){{5-7}} \\cmidrule(lr){{8-10}}
    {{\\rmode{{}}}} & {{remaining\\,{{\\color{{black!45}}$\\downarrow$}}}} & {{$t^{{\\mathrm{{red}}}}$\\,[s]\\,{{\\color{{black!45}}$\\downarrow$}}}} & {{mem\\,[MB]\\,{{\\color{{black!45}}$\\downarrow$}}}}
      & {{solver}} & {{solved\\,{{\\color{{black!45}}$\\uparrow$}}}} & {{$t$ all\\,[s]\\,{{\\color{{black!45}}$\\downarrow$}}}}
      & {{\\#solved\\,{{\\color{{black!45}}$\\uparrow$}}}} & {{speedup\\,{{\\color{{black!45}}$\\uparrow$}}}} & {{memory\\,{{\\color{{black!45}}$\\uparrow$}}}} \\\\
    \\midrule
{body}
    \\bottomrule
  \\end{{tabular}}
\\end{{table*}}"""
    path = emit_fragment(
        tab_path("strategies.tex"), frag,
        note=("% NOTE: tabcolsep reduced by hand to 2.7pt; at 4pt the tabular overflows the\n"
              "% text width by ~21pt. Keep this if the table is regenerated."))

    print(f"reduction instances common to all strategies: {n_red}")
    print(f"ILP: {n_any} solved by any; pair-common {npair}; time limit {tl}s")
    for r in rows:
        sp = f"{r['speedup']:.2f}x" if r.get("speedup") else "n/a"
        fa = f"{r['factor']:.2f}x" if r.get("factor") else "n/a"
        solved = r.get("solved", "--")
        print(f"    {r['solver']:10s} {r['label']:16s} rn={r['rn']:8.1f} "
              f"rt={r['rt'][0]:6.2f}s rmem={r['rm'][0]:7.2f}MB | solved={solved} "
              f"t_all={r.get('t_all', float('nan')):.2f}s speedup={sp} factor={fa}")
    print(f"Wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
