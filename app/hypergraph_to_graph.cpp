#include <memory>
#include <vector>
#include <iostream>
#include <filesystem> // C++17 erforderlich
#include <set>
#include <iostream>
#include <cstdlib>
#include <fstream>

#include "hypergraph.h"
#include "fast_set.h"
#include "config.h"
#include "MIS_algorithm.h"
#include "reductions.h"

#include <cassert>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <getopt.h>

const char *help = "Generate clique expansion graph from given hypergraph.\n"
                   "\n-h \t\tDisplay this help message\n"
                   "-g path* \tPath to the input hypergraph in METIS format\n"
                   "-o path \tSpecify path and name for the output graph in METIS format\n"
                   "-n \t\tEnable precomputed neighborhood array (initially computes neighborhoods)\n"
                   "-d \t\tApply HyperMIS reductions first (on-demand neighborhoods), then expand the kernel.\n"
                   "   \t\tPrints a run_reduce-style line: name reduce<cfg> n m avg_e rn rm r_avg_e offset time seed\n"
                   "-r N \t\tReduction config used with -d (default all = 7)\n"
                   "-s N \t\tSeed used with -d\n"
                   "-t/-k sec \tReduction time limit in seconds used with -d (default 100)\n"
                   "-M n \t\tMax vertex degree considered by the reductions (with -d, default 100)\n"
                   "-N n \t\tMax neighborhood size considered by the reductions (with -d, default 200)\n"
                   "\n* Mandatory input";

void clique_expansion_to_file(hypergraph *g, std::string filename)
{
    NodeID *neighbors = (NodeID *)malloc(sizeof(NodeID) * g->n);
    fast_set n_set(g->n);
    NodeID n = g->n;
    NodeID m = g->m;

    std::ostringstream buffer;
    std::ofstream outFile(filename);
    if (!outFile.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << " for writing.\n";
        return;
    }

    // compute number of edges after clique expansion
    NodeID m_graph = 0;

    for (NodeID v = 0; v < n; v++)
    {
        NodeID deg;
        NodeID *n = hypergraph_get_neighborhood(g, v, neighbors, deg, n_set);
        m_graph += deg;

        buffer << "\n 1 "; // for adding weights 1
        for (NodeID i = 0; i < deg; i++)
        {
            buffer << n[i] + 1 << " ";
        }
    }

    outFile << n << " " << m_graph / 2 << " 10";
    outFile << buffer.str();
    outFile.close();

    free(neighbors);
}

int main(int argc, char **argv)
{
    std::string hypergraph_path,
        graph_path;

    int command;

    std::string name;
    std::string graph_name;
    bool graph_name_specified = false;
    bool do_reduce = false;
    unsigned int seed = time(NULL);

    while ((command = getopt(argc, argv, "hndg:o:r:s:t:k:M:N:")) != -1)
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
            do_reduce = true;
            ON_DEMAND_NEIGHBORHOOD = 1;
            USE_NEIGHBORHOOD_ARRAY = 0;
            break;
        case 'r':
            if (optarg)
                REDUCTION_CONFIG = atoi(optarg);
            break;
        case 's':
            seed = atoi(optarg);
            break;
        case 't':
        case 'k':
            TIME_KERNEL_SECONDS = atoi(optarg);
            break;
        case 'M':
            MAX_DEGREE = atoi(optarg);
            break;
        case 'N':
            NEIGHBORS_SIZE = atoi(optarg);
            break;
        case 'g':
            hypergraph_path = optarg;
            name = std::filesystem::path(hypergraph_path).filename().string();
            break;
        case 'o':
            graph_path = optarg;
            graph_name = name;
            graph_name_specified = true;
            break;

        default:
            return 1;
        }
    }

    if (!graph_name_specified)
    {
        graph_path = hypergraph_path + ".graph";
        graph_name = name;
    }

    FILE *hg_file = fopen(hypergraph_path.c_str(), "r");
    if (!hg_file)
    {
        std::cerr << "Error: Could not open hypergraph file " << hypergraph_path << std::endl;
        return 1;
    }
    hypergraph *g = hypergraph_parse(hg_file);
    fclose(hg_file);

    if (do_reduce)
    {
        // apply hypergraph reductions first and then clique-expand the kernel
        double original_avg_e_size = 0.0;
        for (NodeID e = 0; e < g->m; e++)
            original_avg_e_size += g->Ed[e];
        original_avg_e_size = g->m ? original_avg_e_size / g->m : 0.0;

        assert(hypergraph_validate(g));
        MISH_algorithm *mis_alg = new MISH_algorithm(g);
        if (USE_NEIGHBORHOOD_ARRAY)
            hypergraph_build_neighbors(g, &(mis_alg->node_set));
        else if (ON_DEMAND_NEIGHBORHOOD)
            hypergraph_init_on_demand(g);

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
        std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - mis_alg->start_time);

        double avg_e_size = 0.0;
        if (rg->m != 0)
        {
            for (NodeID e = 0; e < rg->m; e++)
                avg_e_size += rg->Ed[e];
            avg_e_size = avg_e_size / rg->m;
        }

        std::printf("%s\treduce%ld\t%d\t%d\t%.2f\t%d\t%d\t%.2f\t%d\t%.5f\t%d\n",
                    name.c_str(), REDUCTION_CONFIG, g->n, g->m, original_avg_e_size,
                    rg->n, rg->m, avg_e_size, mis_alg->status.IS_size, (double)time.count(), seed);

        if (rg->n > 0)
        {
            fast_set node_set(rg->n);
            hypergraph_build_neighbors(rg, &node_set);
            clique_expansion_to_file(rg, graph_path);
        }
        else
        {
            std::ofstream outFile(graph_path);
            outFile << "0 0 10";
            outFile.close();
        }

        hypergraph_free(rg);
        hypergraph_free(g);
        delete mis_alg;
        return 0;
    }

    fast_set node_set(g->n);
    auto start_time = std::chrono::high_resolution_clock::now();
    hypergraph_build_neighbors(g, &node_set);

    clique_expansion_to_file(g, graph_path);
    std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time);
    std::cout << name << " transformed to " << graph_name << ".graph in time " << time.count() << std::endl;
    hypergraph_free(g);
    return 0;
}
