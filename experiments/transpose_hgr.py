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

Isolated vertices (in no edge) would become empty dual edges, i.e. empty lines.
The HeiHGM reader skips empty lines and then loops forever at the end of the
file. They are therefore NOT written, and the dual header counts only the
non-empty edges. Every isolated vertex is in each maximum independent set, so
the number of dropped vertices is printed to stdout and the caller must add it
to the solution size.

Output on stdout: "<dropped> <build_seconds>". build_seconds covers ONLY the
in-memory construction of the dual (incidence lists), not reading the input
or writing the output: the experiments count conversion cost without I/O.
"""
import sys
import time
from array import array

import numpy as np


def read_tokens_lines(path):
    """Yield token lists for non-comment, non-blank lines."""
    with open(path) as fh:
        for line in fh:
            s = line.strip()
            if not s or s[0] in "%#":
                continue
            yield s.split()


def read_hgr(path):
    """I/O + parsing: returns (m, n, pins, sizes) with 1-based vertex ids."""
    it = read_tokens_lines(path)
    header = next(it)
    m = int(header[0])              # number of hyperedges
    n = int(header[1])              # number of vertices
    fmt = int(header[2]) if len(header) >= 3 else 0
    skip_weight = fmt in (10, 11)   # leading vertex/edge-weight token per line

    pins = array("q")
    sizes = array("q")
    e = 0
    for toks in it:
        if e >= m:
            break
        e += 1
        verts = [int(t) for t in (toks[1:] if skip_weight else toks)]
        verts = [v for v in verts if v > 0]
        if verts and max(verts) > n:
            raise ValueError(f"{path}: vertex {max(verts)} exceeds declared n={n}")
        pins.extend(verts)
        sizes.append(len(verts))
    if e != m:
        raise ValueError(f"{path}: expected {m} edge lines, found {e}")
    return m, n, np.frombuffer(pins, dtype=np.int64), np.frombuffer(sizes, dtype=np.int64)


def build_dual(m, n, pins, sizes):
    """The transpose itself, in memory: for every vertex the sorted, duplicate-free
    list of (1-based) edges containing it. Returns (edge ids, per-vertex counts)."""
    edge = np.repeat(np.arange(1, m + 1, dtype=np.int64), sizes)
    order = np.lexsort((edge, pins))            # by vertex, then by edge
    v, e = pins[order], edge[order]
    keep = np.ones(len(v), dtype=bool)          # a vertex listed twice in one edge
    keep[1:] = (v[1:] != v[:-1]) | (e[1:] != e[:-1])
    v, e = v[keep], e[keep]
    counts = np.bincount(v, minlength=n + 1)[1:]
    return e, counts


def transpose(in_path, out_path):
    m, n, pins, sizes = read_hgr(in_path)
    t0 = time.perf_counter()
    dual_e, counts = build_dual(m, n, pins, sizes)
    build = time.perf_counter() - t0

    # Dual: one edge per non-isolated original vertex, m vertices (one per
    # original edge).
    nonempty = int(np.count_nonzero(counts))
    with open(out_path, "w") as out:
        out.write(f"{nonempty} {m}\n")
        pos = 0
        for c in counts.tolist():
            if c:
                out.write(" ".join(map(str, dual_e[pos:pos + c].tolist())))
                out.write("\n")
                pos += c
    return n - nonempty, build                  # dropped isolated vertices, build time


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <in.hgr> <out.dual.hgr>")
    dropped, build = transpose(sys.argv[1], sys.argv[2])
    print(f"{dropped} {build:.6f}")
