"""Shared helpers for the paper's result scripts (perf profiles, cactus, tables).

Keeping colours, marks, aggregation and the ILP reader in one place guarantees
that e.g. method 'ilp' has the SAME Dark2 colour in the performance profile and
in the cactus plot.
"""

import math
import os
import sys
from collections import defaultdict

# --------------------------------------------------------------------------- #
# Palette / marks
# --------------------------------------------------------------------------- #
# ColorBrewer "Dark2" (8 classes), RGB.
DARK2 = [
    (27, 158, 119),
    (217, 95, 2),
    (117, 112, 179),
    (231, 41, 138),
    (102, 166, 30),
    (230, 171, 2),
    (166, 118, 29),
    (102, 102, 102),
]

# Mark shapes as (pgfplots mark, extra mark options), cycled if a figure plots
# more series than there are shapes.  EVERY entry must be a fillable shape, so
# that each can be drawn in any of the MARK_FILLS below -- filling is what tells
# a reduced configuration from its unreduced counterpart, and a shape that
# ignores `fill` would collapse the two into the same mark.  This rules out
# pgfplots' line-drawn marks (star, asterisk, x, +, |, -, Mercedes ...), which
# have no interior at all.  Ordered by how well the shapes stay apart at the
# ~1.6pt used in the plots: circle/square/triangle/diamond/down-triangle/
# pentagon first, then the down-pentagon; oplus/otimes degrade into a blob and
# come last.  (No halfcircle*: pgfplots draws it as a FULL circle with half the
# disc filled, which reads as a filling rather than a shape and collides with
# the MARK_FILLS axis.)
MARK_SHAPES = [
    ("*", ""),                        # circle
    ("square*", ""),
    ("triangle*", ""),                # up triangle
    ("diamond*", ""),
    ("triangle*", "rotate=180"),      # down triangle
    ("pentagon*", ""),
    ("pentagon*", "rotate=180"),      # down pentagon
    ("oplus*", ""),
    ("otimes*", ""),
]

# Backwards-compatible alias: the bare shape names.  New code should go through
# mark_style() / config_mark_style(), which also pick a filling.
MARKS = [m for m, _ in MARK_SHAPES]

# The three fillings a shape can be drawn with.  The outline always keeps the
# series' colour, so colour still identifies a series while the filling
# separates ones that share a shape: solid, half-tinted, hollow.  A tint rather
# than pgfplots' half* marks, because half* exists only for circle/square/
# diamond whereas a tint works for every shape.
MARK_FILLS = {
    "solid": "fill={c}",
    "half": "fill={c}!35",
    "open": "fill=white",
}


def _render_mark(shape_index, fill, color, size, extra=""):
    """The pgfplots ``mark=..., mark size=..., mark options={...}`` fragment.

    `extra` is appended INSIDE mark options, so it overrides the defaults set
    here (later keys win in a TikZ option list).  It must go in the same braces:
    a separate ``mark options/.append style={...}`` alongside an explicit
    ``mark options={...}`` does not reach the mark, which silently loses the
    opacity/outline the caller asked for.
    """
    shape, shape_opts = MARK_SHAPES[shape_index % len(MARK_SHAPES)]
    shape_opts = f"{shape_opts}, " if shape_opts else ""
    extra = f", {extra}" if extra else ""
    return (f"mark={shape}, mark size={size}pt, "
            f"mark options={{solid, {shape_opts}draw={color}, line width=0.6pt, "
            f"{MARK_FILLS[fill].format(c=color)}{extra}}}")


def mark_style(index, color, size=1.6, extra="", fill=None):
    """Mark for series `index` of a figure with no with/without pairing.

    By default shape and filling cycle independently, so consecutive series
    differ in both -- three fillings times |MARK_SHAPES| visually separable
    series.  Pass an explicit `fill` ("solid"/"half"/"open") when the figure
    already separates its series by colour and shape and the filling would only
    make some of them look washed out.  Configuration figures should use
    config_mark_style() instead.
    """
    if fill is None:
        fill = list(MARK_FILLS)[index % len(MARK_FILLS)]
    return _render_mark(index, fill, color, size, extra)

# Extra qualitative colours appended after DARK2 when more than eight solver
# configurations must be told apart in one figure.  Kept clearly distinct (in
# hue) from the DARK2 entries that the co-plotted configs already use, so the
# performance profiles / cactus plots stay legible with the SOTA graph and
# matching solvers added.
EXTRA_COLORS = [
    (31, 120, 180),   # blue
    (227, 26, 28),    # red
    (23, 190, 207),   # cyan
    (152, 78, 163),   # purple
    (255, 127, 0),    # bright orange
    (65, 105, 105),   # slate
    (128, 0, 38),     # dark red -- the hue furthest from every colour above
]

# The colour of every solver configuration that can appear anywhere, so a
# configuration keeps the same look in every figure regardless of which subset a
# given figure plots.  Keyed by NAME rather than by position: adding, removing or reordering a
# configuration then leaves every other one's colour untouched.  struction and
# rstruction deliberately reuse the hypergraph ILP's green / orange -- those
# configurations are never co-plotted -- while the matching, vertex-cover,
# SAT-and-reduce and graph-reduction configs take EXTRA_COLORS, chosen to stay
# distinct from the DARK2 hues their co-plotted neighbours use.
CONFIG_COLORS = {
    "nrilp":      DARK2[1],
    "frilp":      DARK2[3],
    "rilp":       DARK2[4],
    "ilp":        DARK2[5],
    "gfilp":      DARK2[6],
    "gfrilp":     DARK2[7],
    "struction":  DARK2[0],
    "rstruction": DARK2[1],
    "bmatching":  EXTRA_COLORS[0],
    "rbmatching": EXTRA_COLORS[1],
    "vc_solver":  EXTRA_COLORS[2],
    "rvc_solver": EXTRA_COLORS[3],
    "satreduce":  EXTRA_COLORS[4],
    "rsatreduce": EXTRA_COLORS[5],
    "grilp":      EXTRA_COLORS[6],
}

# Every configuration that can appear anywhere.  nilp/filp are absent on purpose:
# the neighborhood mode only governs how the REDUCTION obtains neighborhoods, so
# without reductions all three are the same run, and every reduced configuration
# is measured against the one unreduced baseline `ilp`.
ALL_ILP_CONFIGS = list(CONFIG_COLORS)

# The approaches compared in the performance profiles and the cactus plots, one
# per entry: the ILP on the hypergraph and on its clique expansion, the SOTA
# graph solvers struction and SatAndReduce and a vertex-cover solver on the
# clique expansion, and the SOTA (b-)matching solver on the transposed (dual)
# hypergraph.  Each is a VARIANT LIST of the same approach, ordered
#     (without our reductions, with them, third variant or None)
# and drawn with one mark SHAPE per approach and one FILLING per variant --
# hollow, solid, half-tinted (see MARK_FILLS / config_mark_style).  So shape
# names the approach and filling names the setting, which is what lets the
# clique-expansion column hold three related configurations: expand and solve
# (gfilp), reduce the hypergraph first (gfrilp), and reduce the expansion itself
# with the graph rules (grilp).  grilp is a variant of that same approach rather
# than an approach of its own: it shares gfilp as its unreduced baseline.
ILP_METHOD_GROUPS = [
    ("ilp", "frilp", None),
    ("gfilp", "gfrilp", "grilp"),
    ("struction", "rstruction", None),
    ("satreduce", "rsatreduce", None),
    ("vc_solver", "rvc_solver", None),
    ("bmatching", "rbmatching", None),
]

# Pair view, for the places that are strictly two-column (Table A.1's per-instance
# base/reduced columns); the third variant has no place there.
ILP_METHOD_PAIRS = [(a, b) for a, b, _ in ILP_METHOD_GROUPS]

# Legend/plot order is COLUMN-MAJOR BY VARIANT: the whole unreduced row, then the
# whole reduced row, then the third row.  With `legend columns=LEGEND_COLUMNS`
# that puts one approach per column with its variants stacked underneath each
# other.  A None is a blank cell (see legend_filler) and MUST be emitted, or
# every later entry shifts one column left and the stacking breaks -- so a
# configuration with no runs yet also leaves its cell blank rather than closing
# the gap.  Trailing blanks in the last row are dropped, since nothing follows
# them to misalign.
LEGEND_COLUMNS = len(ILP_METHOD_GROUPS)


def _legend_rows():
    rows = [[g[i] for g in ILP_METHOD_GROUPS] for i in range(3)]
    while rows and not any(rows[-1]):
        rows.pop()
    if rows:  # drop trailing blanks of the final row only
        while rows[-1] and rows[-1][-1] is None:
            rows[-1].pop()
    return rows


LEGEND_ORDER = [m for row in _legend_rows() for m in row]
ILP_METHODS = [m for m in LEGEND_ORDER if m is not None]

# Configuration -> the plain solver it runs, i.e. its group's unreduced variant.
# Used where the preprocessing is already named by another column and only the
# solver belongs in the cell.  rilp/nrilp are not plotted (so they head no group)
# but are the same ILP under a different neighborhood mode.
SOLVER_BASE = {v: g[0] for g in ILP_METHOD_GROUPS for v in g if v is not None}
SOLVER_BASE.update({"rilp": "ilp", "nrilp": "ilp"})


def solver_macro(method):
    """LaTeX macro naming a configuration's SOLVER, without the R prefix."""
    return cfg_macro(SOLVER_BASE.get(method, method))


def legend_plot_order(available):
    """LEGEND_ORDER with unavailable configurations turned into blank cells.

    Yields the methods to plot in legend order, with None wherever the legend
    needs an empty cell to keep each approach in its own column.
    """
    have = set(available)
    return [m if m in have else None for m in LEGEND_ORDER]


def legend_filler():
    """One blank legend cell, so the column below it still lines up.

    \\addlegendimage adds a legend entry WITHOUT a plot, which is what this needs:
    an empty \\addplot instead leaves a phantom series behind -- pgfplots draws it
    from the cycle list (a stray coloured mark in the middle of the legend) and
    the entry count then no longer matches the plots, which drops the last real
    entry off the legend entirely.
    """
    return ("    \\addlegendimage{empty legend}\n"
            "    \\addlegendentry{}")


def config_mark_style(method, color, size=1.6, fallback_index=0, extra=""):
    """Mark for a solver configuration: shape per approach, filling per variant.

    The variant order of ILP_METHOD_GROUPS maps onto the fillings hollow (without
    our reductions), solid (with them) and half-tinted (a third variant of the
    same approach, currently only grilp).

    Keyed by the configuration NAME rather than its position, so a missing run
    (which shortens the plotted method list) cannot silently shift every other
    configuration onto a different mark.
    """
    for i, variants in enumerate(ILP_METHOD_GROUPS):
        for variant, fill in zip(variants, ("open", "solid", "half")):
            if variant is not None and method == variant:
                return _render_mark(i, fill, color, size, extra)
    return mark_style(fallback_index, color, size, extra)

# Configurations whose result tsv lives outside results/ILP.  read_ilp_method_
# values resolves each method's file through this map, falling back to the
# input_dir passed in.  The struction, vertex-cover, SAT-and-reduce and
# (b-)matching runs are graph-solver results and live in results/GRAPH, but
# share the (graph, algo, size, time, opt, seed, mem) schema.
GRAPH_METHODS = {"struction", "rstruction", "bmatching", "rbmatching",
                 "vc_solver", "rvc_solver", "satreduce", "rsatreduce"}


# --------------------------------------------------------------------------- #
# Means
# --------------------------------------------------------------------------- #
def geomean(values):
    """Geometric mean; values must be strictly positive."""
    return math.exp(sum(math.log(v) for v in values) / len(values))


def shifted_geomean(values, shift):
    """Shifted geometric mean exp(mean(ln(v+shift))) - shift (Achterberg).

    Robust for aggregating times/memory across heterogeneous instances: a shift
    of ~1s (time) or ~1MB (memory) stops sub-second / tiny instances from
    dominating while remaining insensitive to a single huge instance.
    """
    return math.exp(sum(math.log(v + shift) for v in values) / len(values)) - shift


def amean(values):
    return sum(values) / len(values)


def min_by_graph(rows, key="time"):
    """Per-graph minimum of column `key` over the repeated (seed) runs.

    The reduction is deterministic -- the reduced instance is the same on every
    seed and only the measured time/memory fluctuates with system noise (cache
    state, turbo, parallel contention).  The minimum over the repeated runs is
    therefore the cleanest estimate of the true cost (min-of-k), which is what
    all reduction time/memory reporting uses.
    """
    best = {}
    for r in rows:
        g = r.get("graph")
        if g is None:
            continue
        try:
            v = float(r[key])
        except (KeyError, ValueError, TypeError):
            continue
        if g not in best or v < best[g]:
            best[g] = v
    return best


def mean_std(values):
    """(arithmetic mean, sample standard deviation).  std=0 for n<2."""
    n = len(values)
    if n == 0:
        return (float("nan"), 0.0)
    mu = sum(values) / n
    if n < 2:
        return (mu, 0.0)
    var = sum((x - mu) ** 2 for x in values) / (n - 1)
    return (mu, math.sqrt(var))


def geo_mean_std(values, shift=0.0):
    """(central, gstd_factor).

    Central value is the (optionally shifted) geometric mean; the spread is the
    geometric standard deviation factor exp(std(ln v)) -- a dimensionless
    multiplier: the mean is roughly within [central/factor, central*factor].
    Factor = 1.0 means no spread.  Computed on raw (unshifted) logs.
    """
    if not values:
        return (float("nan"), float("nan"))
    central = shifted_geomean(values, shift) if shift else geomean(values)
    logs = [math.log(v) for v in values]
    n = len(logs)
    if n < 2:
        return (central, 1.0)
    mu = sum(logs) / n
    var = sum((x - mu) ** 2 for x in logs) / (n - 1)
    return (central, math.exp(math.sqrt(var)))


# --------------------------------------------------------------------------- #
# Instance names
# --------------------------------------------------------------------------- #
# Every instance belongs to a graph class that we show as a name prefix.  Three
# classes already carry it in the raw filename -- sat14, dac2012 and ispd98 (the
# last one in all caps, which we lowercase) -- and everything else is from the
# SuiteSparse collection and gets an "ssmc" prefix that the raw name lacks.
def add_class_prefix(short):
    """Prefix a shortened instance name (see short_name / the datatool label)
    with its graph class, so every name in the paper reads class-first.

    sat14/dac2012 names already start with their class and are returned as-is;
    an ISPD98 name keeps its suffix but the prefix is lowercased to ispd98; any
    other name is a SuiteSparse instance and gets an "ssmc_" prefix prepended.
    """
    low = short.lower()
    if low.startswith("sat14") or low.startswith("dac2012"):
        return short
    if low.startswith("ispd98"):
        return "ispd98" + short[len("ispd98"):]
    return "ssmc_" + short


def instance_blowups(fred_path, clique_path):
    """Per-instance appendix extras shared by Table A.1 and Table A.2.

    Returns {graph: {'time': str, 'b': str, 'br': str}} of formatted strings
    ('' when not available), all for the on-demand reduction (fred.tsv, seed 1)
    and the clique-expansion edge counts (clique.tsv):
        time  reduction time in seconds (2 decimals),
        b     clique-expansion blow-up of the original instance, gm / m,
        br    the same for the reduced kernel, rgm / rm; blank when the kernel is
              empty or clique.tsv's kernel (rn, rm) disagrees with fred's -- the
              same guard the reduction table uses, so both tables agree.
    """
    fred = {}
    fred_time = {}
    if os.path.exists(fred_path):
        _, rows = read_rows(fred_path, warn=False)
        # Sizes are deterministic -> take them from seed 1; the reduction time is
        # the minimum over seeds (min-of-k, see min_by_graph).
        fred_time = min_by_graph(rows, "time")
        for r in rows:
            if r.get("seed") == "1":
                fred.setdefault(r["graph"], r)
    clique = {}
    if os.path.exists(clique_path):
        _, rows = read_rows(clique_path, warn=False)
        for r in rows:
            clique.setdefault(r["graph"], r)
    out = {}
    for g, fr in fred.items():
        # An instance is solved entirely by the reduction when its reduced kernel
        # has no edges left (rm == 0): with no constraints every remaining vertex
        # is independent, so the reduction alone yields the optimum.
        info = {"time": "", "b": "", "br": "", "H": "", "rem": "", "fully": False}
        try:
            info["fully"] = float(fr["rm"]) == 0
        except (KeyError, ValueError):
            pass
        # Hypergraph size |H| = pins = sum_e |e| = m * avg-edge-size, reported in
        # millions, and the share of it remaining after reduction (|H_r| / |H|).
        # |H| is defined for every instance; rem is 0 for a fully reduced one.
        try:
            H = float(fr["m"]) * float(fr["e"])
            Hr = float(fr["rm"]) * float(fr["re"])
            if H > 0:
                info["H"] = f"{H / 1e6:.2f}"
                info["rem"] = f"{100.0 * Hr / H:.1f}"
        except (KeyError, ValueError):
            pass
        if g in fred_time:
            info["time"] = f"{fred_time[g]:.2f}"
        c = clique.get(g)
        if c:
            try:
                m, rm = float(fr["m"]), float(fr["rm"])
                gm, rgm = float(c["gm"]), float(c["rgm"])
                if m > 0 and gm >= 0:
                    info["b"] = f"{gm / m:.2f}"
                if (rm > 0 and rgm >= 0
                        and c.get("rn") == fr["rn"] and c.get("rm") == fr["rm"]):
                    info["br"] = f"{rgm / rm:.2f}"
            except (KeyError, ValueError):
                pass
        out[g] = info
    return out


# --------------------------------------------------------------------------- #
# TSV reading
# --------------------------------------------------------------------------- #
def read_rows(path, warn=True):
    """Read a tab-separated file with a header row.

    Returns (header, rows) where rows is a list of dicts with whitespace-stripped
    keys and values.  Rows whose column count does not match the header (e.g. a
    solver crash line dumped mid-file) are skipped; with warn=True a one-line
    summary of how many were dropped is printed (set warn=False on repeat reads
    of the same file to avoid duplicate messages).
    """
    rows = []
    skipped = 0
    with open(path) as fh:
        header = [h.strip() for h in fh.readline().rstrip("\n").split("\t")]
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            cols = line.split("\t")
            if len(cols) != len(header):
                skipped += 1
                continue
            rows.append({header[i]: cols[i].strip() for i in range(len(header))})
    if warn and skipped:
        print(f"[warn] {os.path.basename(path)}: skipped {skipped} malformed "
              f"row(s) (column count != header)", file=sys.stderr)
    return header, rows


def reduction_configs(path):
    """The set of reduceX / graphredX tokens a reduction result file contains."""
    _, rows = read_rows(path, warn=False)
    return {r["algo"] for r in rows if "algo" in r}


def same_reduction_config(paths_by_label):
    """True when every listed run used the same reduction configuration.

    The neighborhood strategies are one and the same rule set under different
    neighborhood policies, so their result files must all carry the same reduceX
    token.  The configuration numbering has changed with the rule set before, and
    a sweep launched with a stale -rX flag then silently measures a *different*
    pipeline: the affected strategy looks like it barely reduces rather than like
    a bad run.  Callers use this to refuse to emit an artifact from such a mix.
    """
    seen = {label: reduction_configs(p) for label, p in paths_by_label.items()}
    union = set().union(*seen.values()) if seen else set()
    if len(union) <= 1:
        return True
    print("[error] the reduction runs do not share one configuration:",
          file=sys.stderr)
    for label, cfgs in sorted(seen.items()):
        print(f"          {label:12s} {', '.join(sorted(cfgs)) or '(empty)'}",
              file=sys.stderr)
    print("        rerun the odd one out with the current -r flag "
          "(see run_experiment.sh); the numbering shifts when rules are added.",
          file=sys.stderr)
    return False


# --------------------------------------------------------------------------- #
# ILP instance values (shared by perf-profile and cactus scripts)
# --------------------------------------------------------------------------- #
# Canonical column order of a solver result file.
SOLVER_COLUMNS = ["graph", "algo", "size", "time", "opt", "seed", "mem"]


def read_solver_rows(path, warn=True):
    """Rows of a solver result file, tolerating a wrong header line.

    A generator that copies the header of a different result format writes a
    file whose header names do not match its rows; read_rows would then drop
    every row and the configuration would silently vanish from the paper.  When
    the header does not carry the solver columns but the rows uniformly have
    exactly len(SOLVER_COLUMNS) fields, we re-read them positionally against the
    canonical schema and say so, rather than losing the run.
    """
    header, rows = read_rows(path, warn=False)
    if set(SOLVER_COLUMNS) <= set(header):
        return rows

    recovered = []
    with open(path) as fh:
        fh.readline()  # the unusable header
        for line in fh:
            cols = line.rstrip("\n").split("\t")
            if len(cols) != len(SOLVER_COLUMNS):
                continue
            recovered.append({SOLVER_COLUMNS[i]: cols[i].strip()
                              for i in range(len(SOLVER_COLUMNS))})
    if warn and recovered:
        print(f"[warn] {os.path.basename(path)}: header {header} does not match "
              f"its rows; read {len(recovered)} row(s) positionally as "
              f"{SOLVER_COLUMNS}. Fix the header in the generator.",
              file=sys.stderr)
    return recovered

def read_ilp_method_values(input_dir, metric_col, methods=ILP_METHODS, warn=True):
    """Return (available_methods, {method: {graph: value}}).

    An instance is a GRAPH; its value is the geometric mean of `metric_col` over
    the graph's seeds.  A method counts a graph as solved only if it has an
    optimal (opt==1), positive-metric row for EVERY seed that graph is run with
    (the union of seeds seen for it across all methods).  So a graph whose run
    crashed on some seeds -- leaving a missing/malformed row -- is correctly
    treated as unsolved, not "solved" on the surviving seeds.
    """
    graph_dir = os.path.join(os.path.dirname(input_dir.rstrip("/")), "GRAPH")
    available = []
    raw = {}  # method -> graph -> {seed: (opt, value)}
    for m in methods:
        base_dir = graph_dir if m in GRAPH_METHODS else input_dir
        path = os.path.join(base_dir, f"{m}.tsv")
        if not os.path.exists(path):
            print(f"[warn] missing file, skipping method: {m}.tsv", file=sys.stderr)
            continue
        rows = read_solver_rows(path, warn=warn)
        if not rows:
            # File exists but holds only a header: the configuration has not been
            # run yet.  Treating it as "ran and solved nothing" would empty the
            # mutually-solved intersection and turn every other method's common
            # time/memory into NaN, so drop it instead.
            print(f"[warn] no runs yet, skipping method: {m}.tsv", file=sys.stderr)
            continue
        available.append(m)
        bygraph = defaultdict(dict)
        for r in rows:
            try:
                seed = r["seed"]
                opt = int(r["opt"])
                v = float(r[metric_col])
            except (ValueError, KeyError):
                continue
            bygraph[r["graph"]][seed] = (opt, v)
        raw[m] = bygraph

    # Expected seed set per graph = union over all methods.
    expected = defaultdict(set)
    for m in available:
        for g, seeds in raw[m].items():
            expected[g].update(seeds.keys())

    values = {}
    for m in available:
        gv = {}
        for g, exp_seeds in expected.items():
            seeds = raw[m].get(g, {})
            vals = []
            ok = bool(exp_seeds)
            for s in exp_seeds:
                if s not in seeds:
                    ok = False
                    break
                opt, v = seeds[s]
                if opt != 1 or v <= 0:
                    ok = False
                    break
                vals.append(v)
            if ok:
                gv[g] = geomean(vals)
        values[m] = gv
    return available, values


# --------------------------------------------------------------------------- #
# LaTeX helpers
# --------------------------------------------------------------------------- #
def color_name(method, prefix="c"):
    """xcolor-safe colour name for a config (underscores are illegal in names).

    Mirrors cfg_macro's underscore stripping so the colour for e.g. vc_solver is
    ``cvcsolver`` both where it is defined and where it is used.
    """
    return f"{prefix}{method.replace('_', '')}"


def color_defs(names, prefix="c", palette=None):
    """One \\definecolor per name.

    `palette` is either a list, assigned by position (cycled), or a {name: rgb}
    mapping, assigned by name -- use the mapping wherever a colour must survive
    the list changing.  Names are sanitised (underscores dropped) via color_name
    so configs like vc_solver yield a legal xcolor name.  Defaults to DARK2.
    """
    palette = palette or DARK2
    out = []
    for i, name in enumerate(names):
        rgb = (palette[name] if isinstance(palette, dict)
               else palette[i % len(palette)])
        r, g, b = rgb
        out.append(f"\\definecolor{{{color_name(name, prefix)}}}{{RGB}}{{{r},{g},{b}}}")
    return "\n".join(out)


# --------------------------------------------------------------------------- #
# Legend placement
# --------------------------------------------------------------------------- #
# House rule for the paper: legends live OUTSIDE the plotting area, centred
# above it and laid out in a single row, so they never cover data points.

# For a stand-alone plot: the legend sits just above its own axis.
def legend_above(columns=-1):
    """Legend centred above the axis; `columns` entries per row (-1 = one row).

    Use a finite value when a single row of entries would be wider than the
    column and get clipped at the page margin.
    """
    return ("legend style={at={(0.5,1.04)}, anchor=south, legend columns="
            f"{columns}" + ", draw=none, fill=none, font=\\scriptsize, "
            "/tikz/every even column/.append style={column sep=6pt}}")


LEGEND_ABOVE = legend_above()


# --------------------------------------------------------------------------- #
# Axis style
# --------------------------------------------------------------------------- #
# House rule: no full frame around a plot.  Only the left and bottom axis lines
# are drawn, with ticks pointing outwards -- the top and right rules carry no
# information and box the data in.
AXIS_OPEN = "axis lines=left, tick align=outside"

# Shared y-tick-label gutter for the four solverplots panels (perf profiles +
# cactus).  Each panel's `width` includes its y-tick labels, so the wide log
# labels of the cactus panels (10^{-2}) would otherwise push their axis box
# further right than the perf-profile panels' short linear labels (0.2), leaving
# the top and bottom x-axes misaligned.  Reserving one fixed-width, right-aligned
# gutter sized for the widest label makes all four boxes -- and their x-axes --
# line up.  Only these panels use it, so the single neighborhood plot is untouched.
PANEL_YTICK = "yticklabel style={text width=2.3em, align=right}"


# Reduction-rule identifier (as used in the result files) -> the \label of the
# corresponding Reduction environment in sec/data_reductions.tex.  Generated
# tables and figures name rules through this map, so they show the same names as
# Section 4 and hyperlink to the definition -- the raw identifiers do not match
# the paper's wording (e.g. "node_domination" is "Vertex Domination").
RULE_REDUCTION = {
    "edge_size": "edgesizereduction",
    "edge_degree_one": "edgesizereduction",  # alternate token, folded into edge size
    "node_degree_one": "degree-one",
    "degree_one": "degree-one",
    "simplicial": "simplicialreduction",
    "fast_node_domination": "fastdomination",
    "sunflower": "fastdomination",  # alternate token for fast node domination
    "edge_domination": "edgedomreduction",
    "node_domination": "vertexdomination",
    "twin": "twinreduction",
    "unconfined": "unconfinedreduction",
}


def rule_ref(rule, number=False, macro="nameref"):
    """Name a reduction rule by \\<macro> to its definition in Section 4.

    Falls back to the escaped identifier for a rule with no counterpart there.
    With number=True the reduction number is appended, e.g. "Twins (R 4.5)".
    `macro` selects the linking command: the default \\nameref spells the full
    rule name, whereas \\rname (see defs.tex) is the abbreviated + linked form
    used in the space-constrained tables.
    """
    label = RULE_REDUCTION.get(rule)
    if label is None:
        return "\\texttt{" + rule.replace("_", "\\_") + "}"
    if number:
        return f"\\{macro}{{{label}}}~(R\\,\\ref{{{label}}})"
    return f"\\{macro}{{{label}}}"


def cfg_macro(method):
    """LaTeX macro for an ILP configuration name (see defs.tex).

    Generated tables and legends go through this so the configurations are
    typeset identically everywhere and can be restyled from one place.
    Underscores are stripped so e.g. vc_solver maps to the \\cvcsolver macro.
    """
    return f"\\c{method.replace('_', '')}"


def legend_to_name(key, columns=-1):
    """Options that divert a panel's legend into a named box for later \\ref.

    Used when two panels sit side by side and share one legend: the first panel
    collects the entries with these options, the second draws its series with
    `forget plot`, and the caller typesets \\ref{key} centred above both.

    `columns` entries per row (-1 = a single row).  Pass a finite value once a
    single row would be wider than the text block and run off the page -- with
    a dozen configurations it always does.
    """
    return (f"legend to name={key}, legend columns={columns}, "
            f"legend style={{draw=none, fill=none, font=\\scriptsize, "
            f"/tikz/every even column/.append style={{column sep=6pt}}}}")


def shared_legend_row(key):
    """The centred legend line placed above a pair of panels."""
    return f"  \\centerline{{\\ref{{{key}}}}}\n  \\vspace{{2pt}}"


# --------------------------------------------------------------------------- #
# Paper output bridging
# --------------------------------------------------------------------------- #
# Scripts emit paper-ready \input fragments into an output tree so that
# re-running a script regenerates the corresponding figure/table in place.
#
# By DEFAULT that tree is this repository's own latex/ directory, so the repo
# reproduces every figure and table on its own: latex/main.tex \inputs all
# fragments and builds them into one PDF (see latex/preamble.tex for the
# vendored packages/macros the fragments depend on).  Point PAPER_ROOT at the
# paper checkout to target the actual paper instead, e.g.
#     PAPER_ROOT=/home/ernestine/Latex/HypergraphMISReduction-Paper \
#         python3 make_all_paper.py
_REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
REPO_LATEX_ROOT = os.path.join(_REPO_ROOT, "latex")
PAPER_ROOT = os.environ.get("PAPER_ROOT", REPO_LATEX_ROOT)

# When set, write standalone \documentclass preview documents (into results/)
# for a quick local pdflatex check instead of bare fragments.
PAPER_PREVIEW = bool(os.environ.get("PAPER_PREVIEW"))

# Canonical colour lists as (names, palette) pairs.  Every script's fragment
# references colours c<name> for names drawn from these lists; write_plot_colors()
# emits one \definecolor per name here, so the names always resolve.  The lists
# are disjoint, so a single concatenation has no duplicate definitions.  The
# config list is coloured by NAME through CONFIG_COLORS; the small lists keep the
# positional DARK2 mapping.
COLOR_LISTS = [
    (["Time"], DARK2),                                 # reduction_tables (tpe box)
    (["recompute", "precompute", "ondemand"], DARK2),  # neighborhood_memory
    (ALL_ILP_CONFIGS, CONFIG_COLORS),                  # perf profiles, cactus, solving
]


def _paper_subdir(*parts):
    d = os.path.join(PAPER_ROOT, *parts)
    os.makedirs(d, exist_ok=True)
    return d


def fig_path(name):
    """Absolute path of a figure fragment in the paper's figures/ dir."""
    return os.path.join(_paper_subdir("figures"), name)


def tab_path(name):
    """Absolute path of a table fragment in the paper's tables/ dir."""
    return os.path.join(_paper_subdir("tables"), name)


def data_path(name):
    """Absolute path of a data file in the paper's data/plot_data/ dir."""
    return os.path.join(_paper_subdir("data", "plot_data"), name)


def emit_fragment(path, body, note=None):
    """Write `body` as a bare LaTeX fragment to be \\input by the paper.

    `body` must be a self-contained float -- \\begin{figure(*)}...\\end{figure(*)}
    or \\begin{table(*)}...\\end{table(*)} -- carrying its own caption and label.
    No \\documentclass, no preamble, no \\definecolor: colours live once in
    tikz/plotcolors.tex (see write_plot_colors) and pgfplots/siunitx setup lives
    in the paper preamble.  Wide artifacts should use the starred float so they
    span both columns of the two-column layout.

    `note` is an optional extra comment block (one or more `%`-prefixed lines,
    no trailing newline needed) recording a hand-tuning that must survive
    regeneration -- it is inserted between the "do not edit" and "Regenerate
    with" header lines.
    """
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    header = "% Auto-generated by the HyperMIS result pipeline -- do not edit.\n"
    if note:
        header += note.rstrip("\n") + "\n"
    header += "% Regenerate with: PAPER_ROOT=<paper> python3 make_<script>.py\n"
    with open(path, "w") as fh:
        fh.write(header + body.rstrip("\n") + "\n")
    return path


def write_plot_colors(path=None):
    """Emit the single tikz/plotcolors.tex that the paper preamble \\inputs once.

    Concatenates color_defs() over every canonical list in COLOR_LISTS so all
    c<name> colours referenced by fragments resolve.
    """
    if path is None:
        path = os.path.join(_paper_subdir("tikz"), "plotcolors.tex")
    blocks = ["% Auto-generated plot colours -- do not edit.",
              "% Source: paper_plot_common.write_plot_colors()."]
    for names, palette in COLOR_LISTS:
        blocks.append(color_defs(names, prefix="c", palette=palette))
    blocks.append(palette_defs())
    with open(path, "w") as fh:
        fh.write("\n".join(blocks) + "\n")
    return path


# Named DARK2 palette shared with the hand-drawn reduction figures
# (tikz/twins, tikz/sunflower) and the observation boxes, so the illustrations
# pick up the same colours as the generated plots.  Keyed by DARK2 index.
PALETTE = {
    "d2green": 0, "d2orange": 1, "d2purple": 2, "d2pink": 3,
    "d2lime": 4, "d2gold": 5, "d2brown": 6, "d2gray": 7,
}


def palette_defs():
    """\\definecolor lines for the named DARK2 palette (see PALETTE)."""
    out = ["% Named DARK2 palette shared with the reduction figures / obs boxes."]
    for name, idx in PALETTE.items():
        r, g, b = DARK2[idx]
        out.append(f"\\definecolor{{{name}}}{{RGB}}{{{r},{g},{b}}}")
    return "\n".join(out)
