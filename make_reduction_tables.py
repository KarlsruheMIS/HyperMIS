#!/usr/bin/env python3
"""Tables and figures for the reduction-rules section.

Two data sources in results/RED/:

  config_stats.tsv  one row per (graph, config, seed).  Columns:
      graph algo n m e rn rm re offset time seed
    where n/m are original #nodes/#(hyper)edges, e the avg edge size, rn/rm/re
    the remaining counts/size after reduction, offset the weight fixed by
    reductions, time the reduce time.  'algo' is reduceX (mapping below).  Used
    for the ABLATION table (each rule alone vs. all-rules-except-this) and the
    size-vs-time figure.

  stats.tsv         full-pipeline (all rules) per-rule breakdown.
    Columns: graph seed red_n red_m time reduction
    where red_n/red_m are #nodes/#edges removed BY that rule and time the time
    spent in it.  Used for the CONTRIBUTION table + scatters.  NOTE: it does not
    record incidences, so a per-rule *pipeline* size (m*e) share cannot be built
    from it -- the size-vs-time figure uses the single-rule configs instead.

reduceX mapping (src/MIS_algorithm.cpp): the seven rules are numbered in
full-pipeline application order.  Config k (1-7) applies the k-th rule alone,
config 8 is the full pipeline, and config 8+k drops the k-th rule. See the
ABLATION table below for the rule-to-config layout.

Output (paper fragment): tables/ablation.tex, the ablation and the pipeline
contribution in one table.
"""

import math
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paper_plot_common import (  # noqa: E402
    amean, rule_ref, emit_fragment, geo_mean_std, mean_std, read_rows, tab_path,
)

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR = os.path.join(HERE, "results", "RED")

# Shift for the (small) reduction times when aggregating across instances.
RED_TIME_SHIFT = 0.01

# Ablation: (plain rule name, only-config, disable-config); the display name is
# derived from the rule via rule_ref() so it matches Section 4.
#
# Config layout matches src/MIS_algorithm.cpp: 15 configs numbered contiguously
# in full-pipeline application order:
#   Singles  1-7 : one rule alone
#     1 edge_size, 2 node_degree_one, 3 edge_domination, 4 fast_node_domination,
#     5 node_domination, 6 twin, 7 unconfined
#   Full     8   : all seven rules
#   Disable  9-15: config 8+k drops the k-th rule
#     9 no edge_size, 10 no node_degree_one, 11 no edge_domination,
#     12 no fast_node_domination, 13 no node_domination, 14 no twin,
#     15 no unconfined
# (rule, ONLY config, DISABLE config)
ABLATION = [
    ("edge_size",            "reduce1", "reduce9"),
    ("node_degree_one",      "reduce2", "reduce10"),
    ("edge_domination",      "reduce3", "reduce11"),
    ("fast_node_domination", "reduce4", "reduce12"),
    ("node_domination",      "reduce5", "reduce13"),
    ("twin",                 "reduce6", "reduce14"),
    ("unconfined",           "reduce7", "reduce15"),
]
ALL_CONFIG = "reduce8"

# Full-pipeline rules as they appear in stats.tsv (application order).
PIPELINE_ORDER = [rule for rule, _, _ in ABLATION]

# Reduction-name aliases: some result files spell a rule with an alternate token;
# map it to the canonical name so every run aggregates into one row.
REDUCTION_ALIASES = {
    "sunflower": "fast_node_domination",
    "edge_degree_one": "edge_size",  # size-one edge rule, folded into edge_size
}


# --------------------------------------------------------------------------- #
# Aggregation
# --------------------------------------------------------------------------- #
def aggregate_configs(path):
    """reduceX -> {'remn'|'remm'|'rems'|'time'|'offset': {graph: value}, 'ngraphs'}.

    Per (config, graph): remaining fractions/offset are the arithmetic mean over
    seeds, while `time` is the per-seed minimum -- the reduction is deterministic,
    so its run-to-run time spread is pure measurement noise and the minimum is
    the cleanest estimate (min-of-k).  Remaining fractions are kept per graph
    (not pre-averaged) so the table can report a spread and paired leave-one-out
    deltas.
    """
    _, rows = read_rows(path)
    byag = defaultdict(lambda: defaultdict(
        lambda: {"remn": [], "remm": [], "rems": [], "reme": [],
                 "time": [], "offset": []}))
    for r in rows:
        try:
            n, m, e = float(r["n"]), float(r["m"]), float(r["e"])
            rn, rm, re = float(r["rn"]), float(r["rm"]), float(r["re"])
            t, off = float(r["time"]), float(r["offset"])
        except (KeyError, ValueError):
            continue
        d = byag[r["algo"]][r["graph"]]
        if n > 0:
            d["remn"].append(rn / n)
        if m > 0:
            d["remm"].append(rm / m)
        if m * e > 0:
            # size = #incidences (pins) ~ edges x avg edge size = m*e.
            d["rems"].append((rm * re) / (m * e))
        if n + m > 0:
            # elements = nodes + (hyper)edges, the unit used throughout the table.
            d["reme"].append((rn + rm) / (n + m))
        d["time"].append(t)
        d["offset"].append(off)

    out = {}
    for algo, graphs in byag.items():
        perg = {"remn": {}, "remm": {}, "rems": {}, "reme": {},
                "time": {}, "offset": {}}
        for g, d in graphs.items():
            for key in perg:
                if d[key]:
                    perg[key][g] = min(d[key]) if key == "time" else amean(d[key])
        perg["ngraphs"] = len(graphs)
        out[algo] = perg
    return out


def rem_stat(cfg, algo, key):
    """(mean %, std %) of a remaining fraction over graphs."""
    return mean_std([v * 100 for v in cfg[algo][key].values()])


def time_stat(cfg, algo):
    """(shifted geomean, geometric-std factor) of reduce time over graphs."""
    return geo_mean_std(list(cfg[algo]["time"].values()), RED_TIME_SHIFT)


def delta_stat(cfg, disable, key):
    """(mean, std) of the paired per-graph increase disable-minus-all (in %)."""
    allc = cfg[ALL_CONFIG][key]
    dis = cfg[disable][key]
    common = set(dis) & set(allc)
    return mean_std([(dis[g] - allc[g]) * 100 for g in common])


def delta_time_sum(cfg, disable):
    """Total reduce-time increase disable-minus-all summed over graphs [s].

    The per-instance differences are dominated by a few large instances, so the
    table reports their sum rather than a mean.  Signed like the remaining-% delta:
    a positive value means removing the rule makes the pipeline slower -- i.e. the
    rule saves time overall -- and a negative one that the rule is a net time cost
    the pipeline pays for its reduction power."""
    allc = cfg[ALL_CONFIG]["time"]
    dis = cfg[disable]["time"]
    common = set(dis) & set(allc)
    return sum(dis[g] - allc[g] for g in common)


def aggregate_contributions(path):
    """rule -> per-rule contribution stats (mean + std) over pipeline instances.

    Per instance shares are normalised by that instance's totals, then averaged
    over instances (equal instance weight, robust to size).  The reduction is
    deterministic, so the per-rule removed counts are identical across seeds and
    only the per-rule time varies: we collapse the seeds to one row per graph,
    keeping the removed counts and taking the per-seed minimum time (min-of-k).
    """
    _, rows = read_rows(path)
    # (graph, seed) -> {rule: (red_n, red_m, time)}
    by_gs = defaultdict(dict)
    for r in rows:
        try:
            rn, rm, t = float(r["red_n"]), float(r["red_m"]), float(r["time"])
        except (KeyError, ValueError):
            continue
        rule = REDUCTION_ALIASES.get(r["reduction"], r["reduction"])
        by_gs[(r["graph"], r["seed"])][rule] = (rn, rm, t)

    # Collapse seeds -> one instance per graph: sizes from any seed (deterministic),
    # time the per-seed minimum.
    per_graph = defaultdict(lambda: defaultdict(list))  # graph -> rule -> [(rn,rm,t)]
    for (g, _s), rules in by_gs.items():
        for rule, val in rules.items():
            per_graph[g][rule].append(val)
    by_inst = {
        g: {rule: (vs[0][0], vs[0][1], min(v[2] for v in vs))
            for rule, vs in rulemap.items()}
        for g, rulemap in per_graph.items()
    }

    # An "element" is a node or a (hyper)edge -- in a hypergraph the two are
    # interchangeable as units of the ILP (variables vs. constraints), so we
    # report a rule's removal in combined elements = nodes + edges.
    ab_e, ab_t = defaultdict(list), defaultdict(list)   # abs elements, time
    sh_e, sh_t = defaultdict(list), defaultdict(list)   # share of instance total
    tpe = defaultdict(list)   # time per removed element (s), where it removed >0
    for rules in by_inst.values():
        tot_e = sum(v[0] + v[1] for v in rules.values())
        tot_t = sum(v[2] for v in rules.values())
        for rule in PIPELINE_ORDER:
            rn, rm, t = rules.get(rule, (0.0, 0.0, 0.0))
            e = rn + rm
            ab_e[rule].append(e)
            ab_t[rule].append(t)
            if tot_e > 0:
                sh_e[rule].append(e / tot_e * 100)
            if tot_t > 0:
                sh_t[rule].append(t / tot_t * 100)
            if e > 0 and t > 0:
                tpe[rule].append(t / e)

    def ms(lst):
        return mean_std(lst) if lst else (0.0, 0.0)

    out = {}
    for rule in PIPELINE_ORDER:
        ae, ae_sd = ms(ab_e[rule])
        at, at_sd = ms(ab_t[rule])
        se, se_sd = ms(sh_e[rule])
        st, st_sd = ms(sh_t[rule])
        tg, tf = geo_mean_std(tpe[rule]) if tpe[rule] else (float("nan"), float("nan"))
        out[rule] = {
            "abs_e": ae, "abs_e_sd": ae_sd, "abs_t": at, "abs_t_sd": at_sd,
            "share_e": se, "share_e_sd": se_sd,
            "share_t": st, "share_t_sd": st_sd,
            "tpe": tg, "tpe_f": tf, "n_tpe": len(tpe[rule]),
        }
    return out, len(by_inst), by_inst


def combined_tpe(by_inst, rules):
    """(geomean, spread factor) of time per removed element, pooling `rules`.

    Per instance the times and element counts of the listed pipeline rules are
    summed first, so a combined row (e.g. degree-one = edge + node) reports one
    honest ratio rather than an average of per-rule ratios.  Lower is cheaper per
    unit of reduction.
    """
    vals = []
    for rulemap in by_inst.values():
        tot_e = tot_t = 0.0
        for rule in rules:
            rn, rm, t = rulemap.get(rule, (0.0, 0.0, 0.0))
            tot_e += rn + rm
            tot_t += t
        if tot_e > 0 and tot_t > 0:
            vals.append(tot_t / tot_e)
    return geo_mean_std(vals) if vals else (float("nan"), float("nan"))


# --------------------------------------------------------------------------- #
# LaTeX tables
# --------------------------------------------------------------------------- #
GRAY_STD = "black!55"


def _unc(pair):
    """Two tabular cells (value, gray ±std) for an r@{}l column pair."""
    mean = 0.0 if abs(pair[0]) < 0.005 else pair[0]  # avoid a "-0.00" display
    return f"{mean:.2f} & \\textcolor{{{GRAY_STD}}}{{$\\pm$\\,{pair[1]:.2f}}}"


def _timex(pair):
    """Two tabular cells (geomean time, gray ×spread factor) for an r@{}l pair.

    Attaches the multiplicative spread factor to its time inline, exactly as
    _unc attaches the additive ±std to a mean -- so the ablation table carries
    no separate $\\times$ columns."""
    return f"{pair[0]:.2f} & \\textcolor{{{GRAY_STD}}}{{$\\times$\\,{pair[1]:.2f}}}"


# Ablation rows and pipeline breakdown are indexed by the same rule names, so a
# row maps to exactly one pipeline rule.  The mapping is kept as a list-valued
# dict because combined_tpe() pools over a set of pipeline rules.
ABLATION_TO_PIPELINE = {rule: [rule] for rule, _, _ in ABLATION}


def ablation_table(cfg, contrib, by_inst, n_inst):
    """Per-rule table: strength in isolation, redundancy, and pipeline share.

    The ablation and the pipeline breakdown are indexed by the same rules, so
    they are one table with three column groups rather than two tables.
    """
    lines = []
    for plain, only, dis in ABLATION:
        disp = rule_ref(plain, number=False, macro="rname")
        on = rem_stat(cfg, only, "reme")
        ot = time_stat(cfg, only)
        dl = delta_stat(cfg, dis, "reme")
        dt = delta_time_sum(cfg, dis)
        parts = [contrib[r] for r in ABLATION_TO_PIPELINE[plain] if r in contrib]
        share_e = sum(c["share_e"] for c in parts)
        share_t = sum(c["share_t"] for c in parts)
        # Time per removed element (geomean over instances, gray spread factor),
        # pooling the pipeline rules that make up this ablation row -- in micro-
        # seconds so the sub-microsecond cheap rules stay readable.
        tg, tf = combined_tpe(by_inst, ABLATION_TO_PIPELINE[plain])
        tpe_cells = ("{--} & {--}" if math.isnan(tg)
                     else _timex((tg * 1e6, tf)))
        lines.append(
            f"    {disp} & {_unc(on)} & {_timex(ot)} "
            f"& {_unc(dl)} & {round(dt)} "
            f"& {share_e:.2f} & {share_t:.2f} "
            f"& {tpe_cells} \\\\")
    body = "\n".join(lines)
    an = rem_stat(cfg, ALL_CONFIG, "reme")
    at = time_stat(cfg, ALL_CONFIG)
    ng = cfg[ALL_CONFIG]["ngraphs"]
    # Overall time per removed element for the full pipeline: pool every rule's
    # removed elements and time, so the all-rules row carries the combined t/elem.
    all_tg, all_tf = combined_tpe(by_inst, PIPELINE_ORDER)
    all_tpe = ("{--} & {--}" if math.isnan(all_tg)
               else _timex((all_tg * 1e6, all_tf)))
    U = "r@{\\,}l"  # value + gray spread pair (see _unc / _timex)
    P = "S[table-format=2.2]"
    DT = "S[table-format=-4.0]"  # signed integer total of the time deltas [s]
    return f"""\\begin{{table*}}[t]
  \\centering \\small
  \\caption{{Per-rule ablation in three parts. An element is a vertex or an edge,
    and the arrows {{\\color{{black!45}}$\\downarrow$}} and {{\\color{{black!45}}$\\uparrow$}}
    mark the better direction. In \\emph{{single rule}} we apply each rule on its
    own and report the fraction of elements left and the time this takes. In
    \\emph{{all $\\setminus$ rule}} we run the full pipeline with that one rule
    disabled and report the difference compared to all rules. For the time we
    give the total $\\sum \\Delta t$ over all ${ng}$ instances, since the effect is
    concentrated on a few large instances. A positive value
    means using the rule saves time. \\emph{{In pipeline}} gives each rule's share
    of the elements removed and of the total reduction time, together with the
    resulting time per removed element.}}
  \\label{{tab:ablation}}
  \\resizebox{{\\textwidth}}{{!}}{{%
  \\begin{{tabular}}{{l {U} {U} {U} {DT} {P} {P} {U}}}
      & \\multicolumn{{4}}{{c}}{{single rule}} & \\multicolumn{{3}}{{c}}{{all $\\setminus$ rule}} & \\multicolumn{{4}}{{c}}{{in pipeline}} \\\\
    \\cmidrule(lr){{2-5}} \\cmidrule(lr){{6-8}} \\cmidrule(lr){{9-12}}
    & \\multicolumn{{2}}{{c}}{{remaining\\,\\%\\,{{\\color{{black!45}}$\\downarrow$}}}} & \\multicolumn{{2}}{{c}}{{$t$\\,[s]\\,{{\\color{{black!45}}$\\downarrow$}}}}
         & \\multicolumn{{2}}{{c}}{{$\\Delta$\\,remaining\\,\\%}} & {{$\\sum \\Delta t$\\,[s]}}
         & {{removed\\,\\%\\,{{\\color{{black!45}}$\\uparrow$}}}} & {{time\\,\\%\\,{{\\color{{black!45}}$\\downarrow$}}}} & \\multicolumn{{2}}{{c}}{{$t$/elem\\,[$\\mu$s]\\,{{\\color{{black!45}}$\\downarrow$}}}} \\\\
    \\midrule
{body}
    \\midrule
    \\emph{{all rules}} & {_unc(an)} & {_timex(at)} & 0 & & 0 & 100.00 & 100.00 & {all_tpe} \\\\
    \\bottomrule
  \\end{{tabular}}}}
\\end{{table*}}"""


# A config counts as done only if it covers nearly as many graphs as the
# best-covered one.  A sweep in progress writes its first rows immediately, so
# mere presence says nothing -- and a config measured on the handful of instances
# that finished first is measured on the easy head of the sweep.
MIN_COVERAGE = 0.9


def missing_configs(cfg):
    """Ablation configs config_stats.tsv does not (yet) hold complete runs for."""
    need = [ALL_CONFIG] + [c for _, only, dis in ABLATION for c in (only, dis)]
    if not cfg:
        return list(need)
    full = max(c["ngraphs"] for c in cfg.values())
    return [c for c in need
            if c not in cfg or cfg[c]["ngraphs"] < MIN_COVERAGE * full]


def ablation_placeholder():
    """Valid placeholder ablation float used while config_stats.tsv is partial."""
    return """% PLACEHOLDER
\\begin{table*}[t]
  \\centering \\small
  \\caption{Per-rule ablation. \\emph{single rule}: just that rule and nothing
    else; lower remaining \\% means a stronger rule on its
    own. \\emph{all $\\setminus$ rule}: the full pipeline with that rule
    disabled; $\\Delta$ is the \\emph{increase} in what remains vs.\\ all rules
    (larger = more essential / less redundant).}
  \\label{tab:ablation}
  \\textit{Ablation results pending: the per-rule configuration sweep
    (config\\_stats.tsv) is still incomplete.}
\\end{table*}"""


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def main():
    cfg = aggregate_configs(os.path.join(INPUT_DIR, "config_stats.tsv"))
    contrib, n_inst, by_inst = aggregate_contributions(os.path.join(INPUT_DIR, "stats.tsv"))

    # The ablation half needs every "only" / "disable" config plus the all-rules
    # baseline; while a sweep is still running config_stats.tsv holds only part
    # of them, so emit a placeholder table rather than reporting from partial data.
    absent = missing_configs(cfg)
    have_ablation = not absent

    ablation_body = (ablation_table(cfg, contrib, by_inst, n_inst) if have_ablation
                     else ablation_placeholder())
    emit_fragment(
        tab_path("ablation.tex"), ablation_body,
        note=("% NOTE: rule names use \\rname (abbreviated + linked, see defs.tex), not\n"
              "% \\nameref -- the generator should emit \\rname to keep this on regeneration."))

    if have_ablation:
        an = [rem_stat(cfg, ALL_CONFIG, k) for k in ("remn", "remm", "rems")]
        at = time_stat(cfg, ALL_CONFIG)
        print(f"config graphs = {cfg[ALL_CONFIG]['ngraphs']}")
        print(f"all-rules remaining: n={an[0][0]:.1f}%  m={an[1][0]:.1f}%  "
              f"sz={an[2][0]:.1f}%  t={at[0]:.4g}s")
    else:
        print(f"PLACEHOLDER ablation table: config_stats.tsv has no complete "
              f"run for {len(absent)} config(s): {', '.join(absent)}")
        for c in sorted(cfg, key=lambda k: int(k.replace("reduce", ""))):
            print(f"    {c:9s} {cfg[c]['ngraphs']:3d} graph(s)")
    print(f"contribution instances = {n_inst}")
    for plain, _, _ in ABLATION:
        tg, tf = combined_tpe(by_inst, ABLATION_TO_PIPELINE[plain])
        print(f"  {plain:22s} t/elem = {tg * 1e6:7.2f} us  (x{tf:.2f})")
    print("Wrote tables/ablation.tex")


if __name__ == "__main__":
    main()
