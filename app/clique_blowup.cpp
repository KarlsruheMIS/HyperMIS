#include <memory>
#include <iostream>
#include <filesystem> // C++17 required
#include <string>
#include <cstdlib>
#include <cassert>
#include <ctime>
#include <chrono>
#include <getopt.h>

#include "config.h"
#include "io.h"
#include "MIS_algorithm.h"
#include "reductions.h"
#include "hypergraph.h"
#include "fast_set.h"

const char *help =
    "clique_blowup --- clique-expansion blow-up of a hypergraph and its reduced kernel\n"
    "\nOutput is a single tab-separated line:\n"
    "  name  reduce<cfg>  n  m  rn  rm  gm  rgm  seed\n"
    "where m/rm are the hyperedges of the original/reduced instance and gm/rgm the\n"
    "edges of their clique expansions.  Nothing is written to disk.\n"
    "\n-h \t\tDisplay this help message\n"
    "-g path* \tPath to the input hypergraph in METIS format\n"
    "-r N \t\tReduction config (same meaning as run_reduce, default 7 = all)\n"
    "-s N \t\tSeed\n"
    "-t/-k sec \tReduction time limit in seconds (default 100)\n"
    "-n \t\tPrecomputed neighborhood array during reduction\n"
    "-d \t\tOn-demand neighborhoods during reduction\n"
    "-M n \t\tMax vertex degree considered by the reductions (default 100)\n"
    "-N n \t\tMax neighborhood size considered by the reductions (default 200)\n"
    "\n* Mandatory input";

static long clique_expansion_edges(hypergraph *g)
{
    if (g->n == 0 || g->m == 0)
        return 0;
    NodeID *neighbors = (NodeID *)malloc(sizeof(NodeID) * g->n);
    fast_set node_set(g->n);
    long m_graph = 0;
    for (NodeID v = 0; v < g->n; v++)
    {
        NodeID deg = 0;
        hypergraph_get_neighborhood(g, v, neighbors, deg, node_set);
        m_graph += deg;
    }
    free(neighbors);
    return m_graph / 2;
}

int main(int argc, char **argv)
{
    char *hypergraph_path = NULL;
    unsigned int seed = time(NULL);
    int command;
    std::string name;

    while ((command = getopt(argc, argv, "hdng:t:s:k:r:M:N:")) != -1)
    {
        switch (command)
        {
        case 'h':
            printf("%s\n", help);
            return 0;
        case 'n':
            if (!ON_DEMAND_NEIGHBORHOOD)
                USE_NEIGHBORHOOD_ARRAY = 1;
            break;
        case 'd':
            ON_DEMAND_NEIGHBORHOOD = 1;
            USE_NEIGHBORHOOD_ARRAY = 0;
            break;
        case 'g':
            hypergraph_path = optarg;
            name = std::filesystem::path(hypergraph_path).filename().string();
            break;
        case 's':
            seed = atoi(optarg);
            break;
        case 't':
        case 'k':
            TIME_KERNEL_SECONDS = atoi(optarg);
            break;
        case 'r':
            if (optarg)
                REDUCTION_CONFIG = atoi(optarg);
            else
                return 0;
            break;
        case 'M':
            MAX_DEGREE = atoi(optarg);
            break;
        case 'N':
            NEIGHBORS_SIZE = atoi(optarg);
            break;
        case '?':
            return 1;
        default:
            return 1;
        }
    }

    if (hypergraph_path == nullptr)
    {
        std::cerr << "Error: no input hypergraph (-g)" << std::endl;
        return 1;
    }

    FILE *hgr_file = fopen(hypergraph_path, "r");
    if (!hgr_file)
    {
        std::cerr << "Error: Unable to open file " << hypergraph_path << std::endl;
        return 1;
    }
    hypergraph *g = hypergraph_parse(hgr_file);
    fclose(hgr_file);

    assert(hypergraph_validate(g));
    MISH_algorithm *mis_alg = new MISH_algorithm(g);
    if (REDUCE)
    {
        if (USE_NEIGHBORHOOD_ARRAY)
            hypergraph_build_neighbors(g, &(mis_alg->node_set));
        else if (ON_DEMAND_NEIGHBORHOOD)
            hypergraph_init_on_demand(g);
    }

    mis_alg->reduce_graph();

    std::vector<int> reduced(mis_alg->status.n, 1);
    for (NodeID v = 0; v < g->n; v++)
    {
        if (mis_alg->status.node_status[v] == MISH_algorithm::IS_status::not_set)
            reduced[v] = 0;
    }
    std::vector<NodeID> map(mis_alg->status.n, 0);
    std::vector<NodeID> remap(mis_alg->status.remaining_nodes, 0);

    hypergraph *rg = hypergraph_build_reduced(g, map.data(), remap.data(), reduced.data());
    assert(hypergraph_validate(rg));

    // Clique expansion of the REDUCED kernel.
    long reduced_clique_edges = clique_expansion_edges(rg);
    NodeID n = g->n, m = g->m, rn = rg->n, rm = rg->m;

    hypergraph_free(rg);
    hypergraph_free(g);
    delete mis_alg;

    FILE *hgr_file2 = fopen(hypergraph_path, "r");
    if (!hgr_file2)
    {
        std::cerr << "Error: Unable to reopen file " << hypergraph_path << std::endl;
        return 1;
    }
    hypergraph *g0 = hypergraph_parse(hgr_file2);
    fclose(hgr_file2);
    long original_clique_edges = clique_expansion_edges(g0);
    hypergraph_free(g0);

    std::printf("%s\treduce%ld\t%d\t%d\t%d\t%d\t%ld\t%ld\t%d\n",
                name.c_str(), REDUCTION_CONFIG, n, m, rn, rm,
                original_clique_edges, reduced_clique_edges, seed);

    return 0;
}
