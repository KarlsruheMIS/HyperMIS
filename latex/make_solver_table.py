#!/usr/bin/env python3
"""Per-instance solver table for the appendix (tab:overview).

One row per instance that at least one solver configuration solves to
optimality on every seed; one column pair per approach (see
paper_plot_common.ILP_METHOD_PAIRS), reporting the run time without our
reductions ($t$) and with them ($t_\\strong$).  This is the per-instance view
behind the aggregated Table~\\ref{tab:strategies}, so the two use the same
approaches, the same instance universe and the same solved-on-every-seed
criterion.

Data sources (raw run files, no derived sidecar):
    results/ILP/{ilp,frilp,gfilp,gfrilp}.tsv        graph algo size time opt seed mem
    results/GRAPH/{struction,satreduce,vc_solver,bmatching,r*}.tsv   same schema
    results/RED/red.tsv    graph algo n m e rn rm re offset time seed mem
      -- only for the original |H| = n + m the rows are sorted by.

A configuration solves an instance only if every seed reports opt=1 with a
positive metric; anything else (time limit, memory bound, solver crash) prints
as its failure reason (time limit, memory bound, crash).  The optimum column is
the solution size, which we verified is
identical across every configuration that proves optimality.

Output:
    tables/solvers.tex  (landscape table, tab:overview)
"""

import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    RESULTS_DIR,
    GRAPH_METHODS, ILP_METHOD_PAIRS, ILP_METHODS, add_class_prefix, cfg_macro,
    emit_fragment, instance_blowups, read_rows,
    read_solver_rows,
    read_ilp_method_values, tab_path,
)

HERE = os.path.dirname(os.path.abspath(__file__))
ILP_DIR = os.path.join(RESULTS_DIR, "ILP")
GRAPH_DIR = os.path.join(RESULTS_DIR, "GRAPH")
RED_TSV = os.path.join(RESULTS_DIR, "RED", "red.tsv")
FRED_TSV = os.path.join(RESULTS_DIR, "RED", "fred.tsv")
CLIQUE_TSV = os.path.join(RESULTS_DIR, "RED", "clique.tsv")

LABEL = "tab:overview"

# Why a configuration has no time for an instance.  The run files encode this in
# (size, time, mem, opt), so the reasons are recovered rather than guessed:
#   size = -1        the solver aborted with an internal error   -> crash
#   size = -2        the harness recorded an out-of-memory kill  -> memory
#   mem > MEM_LIMIT  the run exceeded the memory bound           -> memory
#   time >= limit    the run hit the time limit                  -> time
#   size > 0 and it finished inside both bounds with opt = 0: it returned a
#                    feasible solution but never proved it optimal -> unproven
#   size = 0 in that same situation: it stopped early with nothing at all, which
#                    leaves a crash as the only explanation
REASON_TEX = {
    "time": "--",
    "memory": "$\\dagger$",
    "crash": "$\\ddagger$",
    "unproven": "$\\circ$",
}
# Priority when a configuration's seeds disagree: report the hardest failure.
REASON_ORDER = ["crash", "memory", "time", "unproven"]

MEM_LIMIT_KB = 32 * 1024 * 1024   # the 32 GB bound of the experimental setup
TIME_LIMIT_S = 3600.0

# Column groups, by the solver's domain: what the approach actually runs on.
# Listed by the UNREDUCED config of each pair (the reduced partner comes from
# ILP_METHOD_PAIRS).  Grouping matters here because the clique expansion is the
# dominant cost for everything in the second group, so the reader should be able
# to compare within a domain before comparing across.
COLUMN_GROUPS = [
    # Solved on the hypergraph itself -- the ILP directly, the b-matching solver
    # on its transpose; neither ever materializes the expansion.
    ("hypergraph", ["ilp", "bmatching"]),
    ("clique expanded graph", ["gfilp", "struction", "satreduce", "vc_solver"]),
]

# Column header per approach.  The long solver names get a two-line stacked
# header so the landscape columns stay narrow; every other approach uses its
# configuration macro (cfg_macro) directly.
HEADER_OVERRIDE = {
    "satreduce": "\\shortstack{\\textsc{SatAnd}\\\\\\textsc{Reduce}}",
    "vc_solver": "\\shortstack{\\textsc{WeGotYou}\\\\\\textsc{Covered}}",
}

# Suffixes stripped from a raw instance filename for display.  Kept minimal on
# purpose: dropping everything after the first dot (as the reduction table does)
# would merge e.g. sat14_UCG-15-10p1 with its .primal variant.
DROP_PARTS = ("hgr", "random100", "rendered", "mtx", "cnf")


def short_name(graph):
    """Printable instance name: drop the format/seed suffixes, keep the rest."""
    parts = [p for p in graph.split(".") if p not in DROP_PARTS]
    return ".".join(parts) or graph


def tex_name(graph):
    # Class first, then the instance name, separated by a space instead of '_'
    # (remaining underscores in the instance name are kept, escaped).
    cls, _, rest = add_class_prefix(short_name(graph)).partition("_")
    disp = cls + (" " + rest.replace("_", "\\_") if rest else "")
    return "\\texttt{" + disp + "}"


def read_optima():
    """graph -> optimum, taken from any configuration that proves optimality."""
    opt = {}
    for m in ILP_METHODS:
        d = GRAPH_DIR if m in GRAPH_METHODS else ILP_DIR
        path = os.path.join(d, f"{m}.tsv")
        if not os.path.exists(path):
            continue
        _, rows = read_rows(path, warn=False)
        for r in rows:
            try:
                if int(r["opt"]) == 1 and float(r["size"]) > 0:
                    opt.setdefault(r["graph"], int(float(r["size"])))
            except (KeyError, ValueError):
                continue
    return opt


def classify(row):
    """Why this single run produced no proven optimum (see REASON_TEX)."""
    try:
        size = float(row["size"])
        time = float(row["time"])
        mem = float(row["mem"])
    except (KeyError, ValueError):
        return "crash"
    if size == -1:
        return "crash"
    if size == -2 or mem > MEM_LIMIT_KB:
        return "memory"
    if time >= TIME_LIMIT_S:
        return "time"
    # Finished inside both bounds without proving optimality.  A positive size
    # means it did return a feasible solution, just no proof; nothing at all
    # means it died.
    return "unproven" if size > 0 else "crash"


def read_failures():
    """method -> graph -> reason, over the runs that did not prove optimality.

    A graph counts as solved only when EVERY seed proves optimality, so a graph
    with a mix of solved and failed seeds still needs a reason: we report the
    hardest failure among its seeds (REASON_ORDER).
    """
    out = {}
    for m in ILP_METHODS:
        d = GRAPH_DIR if m in GRAPH_METHODS else ILP_DIR
        path = os.path.join(d, f"{m}.tsv")
        if not os.path.exists(path):
            continue
        per_graph = defaultdict(set)
        for r in read_solver_rows(path, warn=False):
            try:
                ok = int(r["opt"]) == 1 and float(r["time"]) > 0
            except (KeyError, ValueError):
                ok = False
            if not ok:
                per_graph[r["graph"]].add(classify(r))
        out[m] = {g: next(x for x in REASON_ORDER if x in s)
                  for g, s in per_graph.items()}
    return out


def read_sizes():
    """graph -> |H| = n + m of the ORIGINAL instance, used only for sorting."""
    if not os.path.exists(RED_TSV):
        return {}
    _, rows = read_rows(RED_TSV, warn=False)
    sizes = {}
    for r in rows:
        try:
            sizes.setdefault(r["graph"], int(r["n"]) + int(r["m"]))
        except (KeyError, ValueError):
            continue
    return sizes


def fmt_cell(value, why, row_best):
    """One time cell: the reason marker if unsolved, bold if best in its row.

    Bold marks the fastest configuration for this instance across the whole row,
    so scanning a column shows which approach wins how often.  Ties are all
    bolded; the comparison is on the printed value, so two cells that round to
    the same displayed time are not arbitrarily separated.
    """
    if value is None:
        return REASON_TEX.get(why, REASON_TEX["time"])
    text = f"{value:.2f}"                       # raw, for the best-in-row test
    disp = f"\\numprint{{{text}}}"              # \numprint gives the \, grouping
    return f"\\textbf{{{disp}}}" if row_best is not None and \
        text == f"{row_best:.2f}" else disp


def main():
    # ILP_METHODS is the flattened pair list with the blank legend slots removed.
    methods = list(ILP_METHODS)
    available, tvals = read_ilp_method_values(ILP_DIR, "time", methods=methods)
    optima = read_optima()
    sizes = read_sizes()
    failures = read_failures()

    universe = sorted({g for gv in tvals.values() for g in gv})
    # Sort alphabetically by the class-prefixed display name, as the caption
    # states, so the table reads in the same order as its names.
    universe.sort(key=lambda g: add_class_prefix(short_name(g)).lower())

    # An approach with no unreduced counterpart (base None) has no column pair
    # here; the table is laid out as base/reduced columns per COLUMN_GROUPS.
    reduced_of = {b: r for b, r in ILP_METHOD_PAIRS if b is not None}
    groups = [(title, [(a, reduced_of[a]) for a in bases
                       if a in available or reduced_of[a] in available])
              for title, bases in COLUMN_GROUPS]
    groups = [(t, ps) for t, ps in groups if ps]
    pairs = [p for _, ps in groups for p in ps]
    if not universe or not pairs:
        print("No solver runs found -- nothing written.", file=sys.stderr)
        return

    # A leading "Instance" group, grouped like the two solver domains: the
    # optimum size, the on-demand reduction time and the clique-expansion blow-up
    # of the original and reduced instance (the same quantities and guard as
    # Table~\ref{tab:reductions_overview}).  The instance name is the row label,
    # outside any group.
    extras = instance_blowups(FRED_TSV, CLIQUE_TSV)
    inst_cols = ["$\\alpha(H)$", "$|H|$\\,[$10^6$]", "rem.\\,\\%",
                 "$b$", "$b_r$", "\\tred{d}"]

    # l for the name, then the instance-property columns (no extra gap after the
    # name, just the default column spacing), then r r per approach.  Approaches
    # inside a group are separated by a thin gap, the groups themselves by a
    # wider one, so the blocks read apart.
    colspec = "l" + "r" * len(inst_cols)
    for _, ps in groups:
        for pi in range(len(ps)):
            gap = "18pt" if pi == 0 else "14pt"
            colspec += f"@{{\\hspace{{{gap}}}}}rr"

    # Three header rows: the group (solver domain), the approach, then
    # without/with.  The instance-property columns are left ungrouped -- no group
    # label and no cmidrule -- so they sit plainly under the "Instances" name
    # column; only the solver domains get a group box.
    head_group = ["", f"\\multicolumn{{{len(inst_cols)}}}{{c}}{{}}"]
    head_top = ["", *[""] * len(inst_cols)]
    head_bot = ["Instances", *inst_cols]
    group_rules = []
    GRAY = "black!55"
    pair_rules = []
    col = 2 + len(inst_cols)   # first solver-time column
    for title, ps in groups:
        span = 2 * len(ps)
        # Gray group title (its cmidrule is grayed in the frag via \arrayrulecolor).
        head_group.append(
            f"\\multicolumn{{{span}}}{{c}}{{\\color{{{GRAY}}}{title}}}")
        # Thinner than the default .03em cmidrule, so the gray group lines stay light.
        group_rules.append(f"\\cmidrule[0.02em](lr){{{col}-{col + span - 1}}}")
        for a, _ in ps:
            # Two-line solver names are shortstacks; single-line names are raised
            # half a line so every name sits vertically centered in the header.
            name = HEADER_OVERRIDE.get(a)
            if name is None:
                name = f"\\raisebox{{0.5\\normalbaselineskip}}{{{cfg_macro(a)}}}"
            head_top.append(f"\\multicolumn{{2}}{{c}}{{{name}}}")
            pair_rules.append(f"\\cmidrule(lr){{{col}-{col + 1}}}")
            head_bot += ["$t$", "\\ttot{d}"]
            col += 2

    body = []
    prev_cls = None
    for g in universe:
        # A little vertical space between graph classes (the class is the prefix
        # of the class-first display name, e.g. dac2012 / ispd98 / sat14 / ssmc).
        cls = add_class_prefix(short_name(g)).split("_", 1)[0]
        if prev_cls is not None and cls != prev_cls:
            body.append("    \\addlinespace[4pt]")
        prev_cls = cls
        times = {m: tvals.get(m, {}).get(g) for p in pairs for m in p}
        solved = [t for t in times.values() if t is not None]
        row_best = min(solved) if solved else None
        ex = extras.get(g, {})
        # When the reduction solves the instance entirely (empty kernel), every
        # reduced configuration does exactly the same work -- just the reduction --
        # so its time IS the reduction time.  We therefore show that single value,
        # in bold, in the reduction column and in every $t_\strong$ column, rather
        # than the near-identical but noisy per-solver measurements.
        red_t = ex.get("time")
        red_bold = (f"\\textbf{{\\numprint{{{red_t}}}}}"
                    if ex.get("fully") and red_t else None)

        def np(s):
            """A formatted number in \\numprint (\\, grouping); '--' if missing."""
            return f"\\numprint{{{s}}}" if s else "--"

        cells = [tex_name(g),
                 np(str(optima[g])) if g in optima else "--",
                 np(ex.get("H")),
                 np(ex.get("rem")),
                 np(ex.get("b")),
                 np(ex.get("br")),
                 red_bold or np(ex.get("time"))]
        for a, b in pairs:
            a_cell = fmt_cell(times[a], failures.get(a, {}).get(g), row_best)
            b_cell = red_bold or fmt_cell(times[b], failures.get(b, {}).get(g),
                                          row_best)
            cells += [a_cell, b_cell]
        # Shade rows the reduction solves entirely (empty kernel).  \rowcolor must
        # lead the row.
        shade = "\\rowcolor{lightgray}" if ex.get("fully") else ""
        body.append("    " + shade + " & ".join(cells) + " \\\\")

    rules = "".join(pair_rules)
    # Gray the group cmidrules, then reset so the pair rules and \midrule stay black.
    group_rule_line = (f"\\arrayrulecolor{{{GRAY}}}" + "".join(group_rules)
                       + "\\arrayrulecolor{black}") if group_rules else ""

    n_solved = {m: len(tvals.get(m, {})) for p in pairs for m in p}
    caption = (
        "Per-instance run time of all methods on solvable instances. The columns "
        "show the optimal solution size $\\alpha(H)$, the hypergraph size $|H|$ "
        "(in millions), the percentage remaining after reduction (rem, \\%), the "
        "clique-expansion blow-up of the original $b$ and the reduced $b_r$ "
        "instance, and the reduction time \\tred{d}. For each approach we "
        "report the time $t$ without and \\ttot{d} with our reduction "
        "preprocessing whenever an instance was solved. We mark exceeded time "
        "limits (3600\\,s $\\mid$ \\mbox{--}), reached memory bounds ($32$\\,GB "
        "$\\mid \\ \\dagger$), and internal errors ($\\ddagger$). All times are in "
        "seconds. All \\ttot{d} include the reduction and in the "
        "clique-expansion group they also include the expansion. Instances with "
        "\\colorbox{lightgray}{shaded} rows are fully solved by reduction. The "
        "fastest configuration per instance is \\textbf{bold}.")

    frag = f"""\\begin{{landscape}}
\\begin{{table}}[p]
  \\centering
  \\caption{{{caption}}}
  \\label{{{LABEL}}}
  \\setlength{{\\tabcolsep}}{{3pt}}
  \\renewcommand{{\\arraystretch}}{{.86}}
  \\begin{{adjustbox}}{{width=\\linewidth, max totalheight=\\textheight, keepaspectratio}}
  \\begin{{tabular}}{{{colspec}}}
    {" & ".join(head_group)} \\\\
    {group_rule_line}
    {" & ".join(head_top)} \\\\
    {rules}
    {" & ".join(head_bot)} \\\\
    \\midrule
{chr(10).join(body)}
    \\bottomrule
  \\end{{tabular}}
  \\end{{adjustbox}}
\\end{{table}}
\\end{{landscape}}"""

    path = emit_fragment(tab_path("solvers.tex"), frag)
    print(f"instances (rows)   : {len(universe)}")
    print(f"approaches (pairs) : {len(pairs)}")
    for m, n in n_solved.items():
        print(f"    {m:12s} solves {n:3d}/{len(universe)}")
    missing = [g for g in universe if g not in sizes]
    if missing:
        print(f"[warn] no |H| (sorted last): {len(missing)} "
              f"({', '.join(short_name(g) for g in missing)})")
    print(f"Wrote {path}")


if __name__ == "__main__":
    main()
