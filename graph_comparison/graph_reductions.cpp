/******************************************************************************
 * graph_reductions.cpp
 *
 * The four rule bodies are copied from KaMIS
 * (KaMIS/lib/mis/kernel/branch_and_reduce_algorithm.cpp: deg1Reduction line 549,
 * dominateReduction line 582, twinReduction line 633, unconfinedReduction line
 * 700) and kept as close to the originals as possible so the comparison is
 * against KaMIS's actual rules. The deviations are:
 *
 *   - twinReduction: upstream calls compute_fold() when the twins' shared
 *     neighborhood is independent. Folding is disabled here, so that case is
 *     skipped and only the non-independent case (both twins into the IS) applies.
 *   - unconfinedReduction: the two `if (REDUCTION >= 3) { ... packing ... }`
 *     blocks are gone. They only fed the branch-and-bound packing constraints;
 *     the rule itself only ever excludes a vertex.
 *   - set(): upstream's crt / vRestore bookkeeping existed for backtracking and
 *     is dropped.
 *   - the debug fprintf/Stat lines are dropped.
 *****************************************************************************/

#include "graph_reductions.h"

#include <algorithm>
#include <cassert>
#include <chrono>

graph_reducer::graph_reducer(uint32_t num_nodes, const long long *V, const uint32_t *E)
    : adj(num_nodes), x(num_nodes, -1), n((int)num_nodes), rn((int)num_nodes),
      used((int)num_nodes * 2), que((size_t)num_nodes * 2, 0),
      level((size_t)num_nodes * 2, 0), iter((size_t)num_nodes * 2, 0)
{
    for (int v = 0; v < n; v++)
    {
        adj[v].reserve(V[v + 1] - V[v]);
        for (long long e = V[v]; e < V[v + 1]; e++)
            adj[v].push_back((int)E[e]);
    }
}

int graph_reducer::deg(int v) const
{
    int d = 0;
    for (int u : adj[v])
        if (x[u] < 0)
            d++;
    return d;
}

void graph_reducer::set(int v, int a)
{
    assert(x[v] < 0);
    x[v] = a;
    rn--;
    if (a == 0)
    {
        // v joins the IS, so every undecided neighbor is excluded
        for (int u : adj[v])
            if (x[u] < 0)
            {
                x[u] = 1;
                rn--;
            }
    }
}

bool graph_reducer::deg1Reduction()
{
    int oldn = rn;
    std::vector<int> &deg = iter;
    int qt = 0;
    used.clear();
    for (int v = 0; v < n; v++)
        if (x[v] < 0)
        {
            deg[v] = n == rn ? (int)adj[v].size() : this->deg(v);
            if (deg[v] <= 1)
            {
                que[qt++] = v;
                used.add(v);
            }
        }
    while (qt > 0)
    {
        int v = que[--qt];
        if (x[v] >= 0)
            continue;
        assert(deg[v] <= 1);
        for (int u : adj[v])
            if (x[u] < 0)
            {
                for (int w : adj[u])
                    if (x[w] < 0)
                    {
                        deg[w]--;
                        if (deg[w] <= 1 && used.add(w))
                            que[qt++] = w;
                    }
            }
        set(v, 0);
    }
    return oldn != rn;
}

bool graph_reducer::dominateReduction()
{
    int oldn = rn;
    for (int v = 0; v < n; v++)
        if (x[v] < 0)
        {
            used.clear();
            used.add(v);
            for (int u : adj[v])
                if (x[u] < 0)
                    used.add(u);
            for (int u : adj[v])
                if (x[u] < 0)
                {
                    for (int w : adj[u])
                    {
                        if (x[w] < 0 && !used.get(w))
                            goto loop;
                    }
                    set(v, 1);
                    break;
                loop:;
                }
        }
    return oldn != rn;
}

bool graph_reducer::twinReduction()
{
    int oldn = rn;
    std::vector<int> &vUsed = iter;
    int uid = 0;
    std::vector<int> NS(3, 0);
    for (int i = 0; i < n; i++)
        vUsed[i] = 0;
    for (int v = 0; v < n; v++)
        if (x[v] < 0 && deg(v) == 3)
        {
            int p = 0;
            for (int u : adj[v])
                if (x[u] < 0)
                {
                    NS[p++] = u;
                    uid++;
                    for (int w : adj[u])
                        if (x[w] < 0 && w != v)
                        {
                            if (p == 1)
                                vUsed[w] = uid;
                            else if (vUsed[w] == uid - 1)
                            {
                                vUsed[w]++;
                                if (p == 3 && deg(w) == 3)
                                {
                                    uid++;
                                    for (int z : NS)
                                        vUsed[z] = uid;
                                    bool ind = true;
                                    for (int z : NS)
                                        for (int a : adj[z])
                                            if (x[a] < 0 && vUsed[a] == uid)
                                                ind = false;
                                    if (ind)
                                    {
                                        // upstream folds here (compute_fold({v, w}, NS));
                                        // folding is disabled, so the twins stay in the kernel
                                    }
                                    else
                                    {
                                        set(v, 0);
                                        set(w, 0);
                                    }
                                    goto loop;
                                }
                            }
                        }
                }
        loop:;
        }
    return oldn != rn;
}

bool graph_reducer::unconfinedReduction()
{
    int oldn = rn;
    std::vector<int> &NS = level;
    std::vector<int> &deg = iter;
    for (int v = 0; v < n; v++)
        if (x[v] < 0)
        {
            used.clear();
            used.add(v);
            int p = 1, size = 0;
            for (int u : adj[v])
                if (x[u] < 0)
                {
                    used.add(u);
                    NS[size++] = u;
                    deg[u] = 1;
                }
            bool ok = false;

            while (!ok)
            {
                ok = true;
                for (int i = 0; i < size; i++)
                {
                    int const u = NS[i];
                    if (deg[u] != 1)
                        continue;
                    int z = -1;
                    for (int const w : adj[u])
                        if (x[w] < 0 && !used.get(w))
                        {
                            if (z >= 0)
                            {
                                z = -2;
                                break;
                            }
                            z = w;
                        }
                    if (z == -1)
                    {
                        set(v, 1);
                        goto whileloopend;
                    }
                    else if (z >= 0)
                    {
                        ok = false;
                        used.add(z);
                        p++;
                        for (int w : adj[z])
                            if (x[w] < 0)
                            {
                                if (used.add(w))
                                {
                                    NS[size++] = w;
                                    deg[w] = 1;
                                }
                                else
                                {
                                    deg[w]++;
                                }
                            }
                    }
                }
            }
        whileloopend:
            if (x[v] < 0 && p >= 2)
            {
                used.clear();
                for (int i = 0; i < size; i++)
                    used.add(NS[i]);
                std::vector<int> &vs = que;
                for (int i = 0; i < size; i++)
                {
                    vs[i] = vs[n + i] = -1;
                    int u = NS[i];
                    if (deg[u] != 2)
                        continue;
                    int v1 = -1, v2 = -1;
                    for (int w : adj[u])
                        if (x[w] < 0 && !used.get(w))
                        {
                            if (v1 < 0)
                                v1 = w;
                            else if (v2 < 0)
                                v2 = w;
                            else
                            {
                                v1 = v2 = -1;
                                break;
                            }
                        }
                    if (v1 > v2)
                    {
                        int t = v1;
                        v1 = v2;
                        v2 = t;
                    }
                    vs[i] = v1;
                    vs[n + i] = v2;
                }
                for (int i = 0; i < size; i++)
                    if (vs[i] >= 0 && vs[n + i] >= 0)
                    {
                        int u = NS[i];
                        used.clear();
                        for (int w : adj[u])
                            if (x[w] < 0)
                                used.add(w);
                        for (int j = i + 1; j < size; j++)
                            if (vs[i] == vs[j] && vs[n + i] == vs[n + j] && !used.get(NS[j]))
                            {
                                set(v, 1);
                                goto forloopend;
                            }
                    }
        forloopend:;
            }
        }
    return oldn != rn;
}

void graph_reducer::reduce(unsigned rules, double time_limit_seconds)
{
    auto start = std::chrono::high_resolution_clock::now();
    auto out_of_time = [&]() {
        std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;
        return elapsed.count() > time_limit_seconds;
    };

    for (;;)
    {
        if (rn == 0 || out_of_time())
            break;
        if ((rules & RULE_DEG1) && deg1Reduction())
            continue;
        if ((rules & RULE_DOMINATION) && dominateReduction())
            continue;
        if ((rules & RULE_UNCONFINED) && unconfinedReduction())
            continue;
        if ((rules & RULE_TWIN) && twinReduction())
            continue;
        break;
    }
}

uint32_t graph_reducer::kernel_n() const
{
    return (uint32_t)rn;
}

long long graph_reducer::kernel_m() const
{
    long long arcs = 0;
    for (int v = 0; v < n; v++)
        if (x[v] < 0)
            for (int u : adj[v])
                if (x[u] < 0)
                    arcs++;
    return arcs / 2;
}

uint32_t graph_reducer::offset() const
{
    uint32_t included = 0;
    for (int v = 0; v < n; v++)
        if (x[v] == 0)
            included++;
    return included;
}

void graph_reducer::kernel_csr(std::vector<long long> &V, std::vector<uint32_t> &E,
                               std::vector<uint32_t> &reverse_map) const
{
    // Modelled on KaMIS's convert_adj_lists(), but emitting plain CSR.
    std::vector<uint32_t> mapping(n, UINT32_MAX);
    reverse_map.clear();
    reverse_map.reserve(rn);
    for (int v = 0; v < n; v++)
        if (x[v] < 0)
        {
            mapping[v] = (uint32_t)reverse_map.size();
            reverse_map.push_back((uint32_t)v);
        }

    V.assign(reverse_map.size() + 1, 0);
    E.clear();
    for (size_t i = 0; i < reverse_map.size(); i++)
    {
        V[i] = (long long)E.size();
        size_t begin = E.size();
        for (int u : adj[reverse_map[i]])
            if (x[u] < 0)
                E.push_back(mapping[u]);
        std::sort(E.begin() + begin, E.end());
    }
    V[reverse_map.size()] = (long long)E.size();
}

void graph_reducer::lift(const std::vector<bool> &kernel_sol, std::vector<bool> &full_sol) const
{
    std::vector<uint32_t> reverse_map;
    reverse_map.reserve(rn);
    for (int v = 0; v < n; v++)
        if (x[v] < 0)
            reverse_map.push_back((uint32_t)v);

    assert(kernel_sol.size() == reverse_map.size());
    for (size_t i = 0; i < kernel_sol.size(); i++)
        if (kernel_sol[i])
            full_sol[reverse_map[i]] = true;

    for (int v = 0; v < n; v++)
        if (x[v] == 0)
            full_sol[v] = true;
}
