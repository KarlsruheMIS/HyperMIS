#!/usr/bin/env python3
"""Transpose (dualize) a hypergraph in hMETIS/HyperMIS .hgr format.

The dual H* of H = (V, E) swaps vertices and edges:
  - every original edge  e  becomes a dual VERTEX
  - every original vertex v  becomes a dual EDGE  f_v = { e in E : v in e }

Why this is the right transform for the MIS <-> b-matching comparison:
A HyperMIS independent set is a set S of vertices with at most one chosen vertex
per hyperedge (the  sum_{v in e} x_v <= 1  constraint in ILP_solver.cpp). In the
dual, picking vertex v <-> picking dual edge f_v, and "at most one chosen vertex
per edge e" <-> "dual vertex e is covered at most once" -- exactly a b-matching
with capacity 1. Hence  max IS(H) == max cardinality b-matching(H*, b=1),  and
the matching (a set of dual edges) is literally the independent set (a set of
original vertices). See verify_matching_vs_is.sh.

Input parsing mirrors hypergraph_parse() in src/hypergraph.cpp:
  line 1: <#edges> <#vertices> <fmt>
  fmt in {10, 11} => each edge line starts with a weight token that is skipped
  vertices are 1-indexed; '%' / '#' lines are comments.

The dual is written UNWEIGHTED (no fmt token) so b-matching maximizes cardinality.
"""
import sys


def read_tokens_lines(path):
    """Yield token lists for non-comment, non-blank lines."""
    with open(path) as fh:
        for line in fh:
            s = line.strip()
            if not s or s[0] in "%#":
                continue
            yield s.split()


def transpose(in_path, out_path):
    it = read_tokens_lines(in_path)
    header = next(it)
    m = int(header[0])              # number of hyperedges
    n = int(header[1])              # number of vertices
    fmt = int(header[2]) if len(header) >= 3 else 0
    skip_weight = fmt in (10, 11)   # leading vertex/edge-weight token per line

    # incident[v] = sorted list of edge indices (1-based) that contain vertex v
    incident = [[] for _ in range(n + 1)]
    e = 0
    for toks in it:
        if e >= m:
            break
        e += 1
        verts = toks[1:] if skip_weight else toks
        for t in verts:
            v = int(t)
            if v <= 0:
                continue
            if v > n:
                raise ValueError(f"{in_path}: vertex {v} exceeds declared n={n}")
            incident[v].append(e)      # dual vertex = original edge index (1-based)
    if e != m:
        raise ValueError(f"{in_path}: expected {m} edge lines, found {e}")

    # Dual: n edges (one per original vertex), m vertices (one per original edge).
    with open(out_path, "w") as out:
        out.write(f"{n} {m}\n")
        for v in range(1, n + 1):
            out.write(" ".join(map(str, incident[v])))
            out.write("\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <in.hgr> <out.dual.hgr>")
    transpose(sys.argv[1], sys.argv[2])
