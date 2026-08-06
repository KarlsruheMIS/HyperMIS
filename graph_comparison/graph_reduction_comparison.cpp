/******************************************************************************
 * graph_reduction_comparison.cpp
 *
 * Baseline for the hypergraph reduction experiments: clique-expand a hypergraph
 * and kernelize the resulting graph with KaMIS's unweighted degree_one,
 * domination, twin and unconfined rules (see graph_reductions.h), optionally
 * solving the kernel with the same Gurobi ILP run_ilp uses.
 *
 * MIS on the clique expansion is exactly the strong independent set on the
 * hypergraph -- adjacency in the expansion means "shares a hyperedge", which is
 * the constraint the hypergraph ILP enforces -- so sizes are directly comparable
 * with run_ilp, and vertex ids are preserved one-to-one.
 *****************************************************************************/

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "io.h"
#include "ILP_solver.h"
#include "graph_reductions.h"

const char *help = "graph_reduction_comparison --- KaMIS graph reductions on the clique expansion of a hypergraph\n"
                   "\nWithout -i the output is a single tab-separated line:\n"
                   "instance_name\tgraphred<rules>\tn\tm\tgn\tgm\trn\trm\toffset\ttime\tseed\n"
                   "where n,m are the hypergraph's vertices/hyperedges, gn,gm the clique expansion's\n"
                   "vertices/edges, and rn,rm the kernel's.\n"
                   "\nWith -i the output is instead a run_ilp-compatible line:\n"
                   "instance_name\tILP\tsize\ttime\topt\tseed\n"
                   "\n-h \t\tDisplay this help message\n"
                   "-g path* \tPath to the input hypergraph in METIS format\n"
                   "-i \t\tSolve the reduced graph with the ILP and report the IS size\n"
                   "-o path \tPath to store the solution (implies -i) \t default not stored\n"
                   "-r mask \tRules to apply, bitmask: 1 degree_one | 2 domination | 4 twin | 8 unconfined\n"
                   "\t\t(0 disables all reductions) \t\t\t default 15 (all)\n"
                   "-t sec \t\tILP timeout in seconds \t\t\t\t default 3600\n"
                   "-k sec \t\tTime limit for the reductions \t\t\t default 100\n"
                   "-s s \t\tSet a specific random seed \t\t\t default time(NULL)\n"
                   "-n \t\tEnable precomputed neighborhood array\n"
                   "-d \t\tOn-demand neighborhoods\n"
                   "\n* Mandatory input";

static bool verify_strong_is(hypergraph *g, const std::vector<bool> &sol)
{
    for (NodeID e = 0; e < g->m; e++)
    {
        NodeID selected = 0;
        for (NodeID i = 0; i < g->Ed[e]; i++)
            if (sol[g->E[e][i]])
                selected++;
        if (selected > 1)
        {
            std::cerr << "ERROR: hyperedge " << e << " contains " << selected << " selected vertices" << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    char *hypergraph_path = NULL,
         *solution_path = NULL;
    double timeout = 3600;
    double kernel_timeout = 100;
    unsigned rules = RULE_ALL;
    bool run_ilp = false;

    unsigned int seed = time(NULL);
    int command;
    std::string name;

    while ((command = getopt(argc, argv, "hdnig:t:s:k:o:r:")) != -1)
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
        case 'i':
            run_ilp = true;
            break;
        case 'o':
            solution_path = optarg;
            run_ilp = true;
            break;
        case 'r':
            rules = (unsigned)atoi(optarg);
            break;
        case 't':
            timeout = atof(optarg);
            break;
        case 'k':
            kernel_timeout = atof(optarg);
            break;
        case 's':
            seed = atoi(optarg);
            break;
        case '?':
        default:
            return 1;
        }
    }

    if (hypergraph_path == nullptr)
    {
        std::cerr << "Error: no input hypergraph given (-g)" << std::endl;
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

    auto start_time = std::chrono::high_resolution_clock::now();

    if (USE_NEIGHBORHOOD_ARRAY)
    {
        fast_set node_set(g->n);
        hypergraph_build_neighbors(g, &node_set);
    }
    else if (ON_DEMAND_NEIGHBORHOOD)
        hypergraph_init_on_demand(g);

    // Clique expansion: same vertex set and same ids, edges = "shares a hyperedge".
    graph *eg = hypergraph_clique_expansion(g);
    if (eg == NULL)
    {
        std::cerr << "ERROR: could not build the clique expansion of " << name << std::endl;
        return 1;
    }

    graph_reducer reducer((uint32_t)eg->n, eg->V, eg->E);
    reducer.reduce(rules, kernel_timeout);

    std::vector<long long> kernel_V;
    std::vector<uint32_t> kernel_E;
    std::vector<uint32_t> reverse_map;
    reducer.kernel_csr(kernel_V, kernel_E, reverse_map);

    NodeID offset = reducer.offset();
    int exit_code = 0;

    if (!run_ilp)
    {
        std::chrono::duration<double> time = std::chrono::high_resolution_clock::now() - start_time;
        std::printf("%s\tgraphred%u\t%d\t%d\t%lld\t%lld\t%u\t%lld\t%u\t%.5f\t%d\n",
                    name.c_str(), rules, g->n, g->m, eg->n, eg->m / 2,
                    reducer.kernel_n(), reducer.kernel_m(), offset, time.count(), seed);
    }
    else
    {
        std::vector<bool> kernel_sol(reducer.kernel_n(), false);
        std::pair<NodeID, int> ILP_solution = {0, 2}; // an empty kernel is solved optimally

        if (reducer.kernel_n() > 0)
        {
            graph kernel = {(long long)reducer.kernel_n(), (long long)kernel_E.size(),
                            kernel_V.data(), kernel_E.data()};
            ILP_solution = ILP_solver_graphs(&kernel, timeout, start_time, kernel_sol);
        }

        if (ILP_solution.second == ILP_FAILED)
        {
            std::cerr << "Error: the ILP failed on " << name << "; no result reported" << std::endl;
            free(eg->V);
            free(eg->E);
            free(eg);
            hypergraph_free(g);
            return 1;
        }

        std::chrono::duration<double> time = std::chrono::high_resolution_clock::now() - start_time;
        NodeID size = offset + ILP_solution.first;
        std::cout << name << "\tILP\t" << size << "\t" << time.count() << "\t"
                  << (ILP_solution.second == 2) << "\t" << seed << std::endl;

        std::vector<bool> sol(g->n, false);
        reducer.lift(kernel_sol, sol);

        if (!verify_strong_is(g, sol))
            exit_code = 1;

        if (solution_path)
            writeSolutionToFile(sol, solution_path);
    }

    free(eg->V);
    free(eg->E);
    free(eg);
    hypergraph_free(g);

    return exit_code;
}
