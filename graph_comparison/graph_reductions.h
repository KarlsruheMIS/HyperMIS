/******************************************************************************
 * graph_reductions.h
 *
 * Unweighted graph reductions ported from KaMIS
 * (KaMIS/lib/mis/kernel/branch_and_reduce_algorithm.{h,cpp}), reduced to the four
 * rules the hypergraph pipeline also has -- degree_one, domination, twin,
 * unconfined -- with folding removed.
 *
 * Dropping folding is what lets this be self-contained: no fold/undo stack
 * (modified.{h,cpp}), no packing constraints, no LP reduction, no branching, and
 * hence no graph_access / mis_config / KaHIP dependency. Without folds the
 * reduction offset is simply the number of vertices set into the IS, and lifting a
 * kernel solution is a plain reverse-map.
 *****************************************************************************/

#ifndef GRAPH_REDUCTIONS_H
#define GRAPH_REDUCTIONS_H

#include <cstdint>
#include <vector>

#include "fast_set.h"

enum graph_rule : unsigned
{
    RULE_DEG1 = 1u,
    RULE_DOMINATION = 2u,
    RULE_TWIN = 4u,
    RULE_UNCONFINED = 8u,
    RULE_ALL = 15u,
};

class graph_reducer
{
public:
    graph_reducer(uint32_t n, const long long *V, const uint32_t *E);

    void reduce(unsigned rules, double time_limit_seconds);

    uint32_t kernel_n() const;
    long long kernel_m() const; // undirected edge count

    void kernel_csr(std::vector<long long> &V, std::vector<uint32_t> &E,
                    std::vector<uint32_t> &reverse_map) const;

    uint32_t offset() const;

    void lift(const std::vector<bool> &kernel_sol, std::vector<bool> &full_sol) const;

private:
    std::vector<std::vector<int>> adj;
    std::vector<int> x; // -1 undecided, 0 in the IS, 1 excluded
    int n;
    int rn; // number of undecided vertices
    fast_set used;
    std::vector<int> que, level, iter; // scratch, sized 2n, as upstream

    int deg(int v) const;
    void set(int v, int a);

    bool deg1Reduction();
    bool dominateReduction();
    bool twinReduction();       // fold branch dropped
    bool unconfinedReduction(); // packing bookkeeping dropped
};

#endif // GRAPH_REDUCTIONS_H
