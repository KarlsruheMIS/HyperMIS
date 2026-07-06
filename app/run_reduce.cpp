#include <memory>
#include <iostream>
#include <filesystem> // C++17 erforderlich
#include <string>
#include <cstdlib>
#include <cassert>
#include <getopt.h>

#include "config.h"
#include "io.h"
#include "MIS_algorithm.h"
#include "reductions.h"
#include "hypergraph.h"

const char *help = "hyperMISReduce --- Data reduction rules for the Maximum Independent Set problem on Hypergraphs\n"
                   "\nThe output of the program without -v is a single line on the form:\n"
                   "instance_name,#vertices,#edges,average_edge_size,#reduced_vertices,#reduced_edges,reduced_avg_edge_size,offset,time,seed\n"
                   "\n-h \t\tDisplay this help message\n"
                   "-r \t\tReduction Config: [0: disable all | 1,...,6 (only degree_one,sunflower, node_domination, edge_domination , twin, unconfinedresp.)| 8,...,13 (disable respective reduction) \t\t\t default all reductions enabled (7)\n"
                   "-v \t\tVerbose mode, output continous updates to STDOUT\n"
                   "-e \t\tExperiment mode, output reduction statistics to STDOUT\n"
                   "-g path* \tPath to the input hypergraph in METIS format\n"
                   "-t sec \t\tTimout in seconds \t\t\t\t default 3600 seconds\n"
                   "-k sec \t\tTimout in seconds for reduction preprocessing \t\t\t\t default 3600 seconds\n"
                   "-s \tUser specific seed\n"
                   "-o path \tPath to the solution\n"
                   "-n \t\tEnable precomputed neighborhood array (initially computes neighborhoods)\n"
                   "\n* Mandatory input";

int main(int argc, char **argv)
{
    char *hypergraph_path = NULL,
         *solution_path = NULL;
    double timeout = 3600;

    unsigned int seed = time(NULL);

    int command;

    std::string name;

    while ((command = getopt(argc, argv, "hnveg:t:s:k:o:r:")) != -1)
    {
        switch (command)
        {
        case 'h':
            printf("%s\n", help);
            return 0;
            break;
        case 'n':
            USE_NEIGHBORHOOD_ARRAY = 1;
            break;
        case 'v':
            VERBOSE = 1;
            break;
        case 'e':
            EXPERIMENT = 1;
            break;
        case 'g':
            hypergraph_path = optarg;
            name = std::filesystem::path(hypergraph_path).filename().string();
            break;
        case 'o':
            solution_path = optarg;
            break;
        case 't':
            timeout = atof(optarg);
            break;
        case 's':
            seed = atoi(optarg);
            break;
        case 'k':
            TIME_KERNEL_SECONDS = atoi(optarg);
            break;
        case 'r':
            if (optarg)
                REDUCTION_CONFIG = atoi(optarg);
            else
                return 0;
            break;
        case '?':
            return 1;

        default:
            return 1;
        }
    }

    if (hypergraph_path == nullptr)
    {
        std::cerr << "Error: Unable to open file " << std::endl;
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
    double original_avg_e_size = 0.0;
    for (NodeID e = 0; e < g->m; e++)
        original_avg_e_size += g->Ed[e];
    original_avg_e_size = original_avg_e_size / g->m;

    // assert(hypergraph_validate(g));
    MISH_algorithm *mis_alg = new MISH_algorithm(g);
    hypergraph_build_neighbors(g, &(mis_alg->node_set));

    mis_alg->reduce_graph();

    std::vector<int> reduced(mis_alg->status.n, 1);
    for (NodeID v = 0; v < g->n; v++)
    {
        if (mis_alg->status.node_status[v] == MISH_algorithm::IS_status::not_set)
            reduced[v] = 0;
    }
    std::vector<NodeID> map;
    std::vector<NodeID> remap;
    remap.reserve(mis_alg->status.remaining_nodes);
    map.reserve(mis_alg->status.n);

    hypergraph *rg = hypergraph_build_reduced(g, map.data(), remap.data(), reduced.data());
    // assert(hypergraph_validate(rg));
    std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - mis_alg->start_time);

    double avg_e_size = 0.0;
    for (NodeID e = 0; e < rg->m; e++)
        avg_e_size += rg->Ed[e];
    avg_e_size = avg_e_size / (rg->m);


    if (EXPERIMENT)
    {
        for (int i = 0; i < mis_alg->status.reductions.size(); i++)
        {
            int j = mis_alg->status.reductions[i]->get_reduction_type();
            std::printf("%s\t%d\t%d\t%d\t%.5f\t%s\n", name.c_str(), seed, mis_alg->n_reduced[j], mis_alg->m_reduced[j], (double)mis_alg->t_reduced[j], mis_alg->status.reductions[i]->get_reduction_name().c_str());
        }

        // std::printf("%s\t%d\t%d\t%d\t%.5f\t%d\n", name.c_str(), seed, g->n - rg->n, g->m - rg->m, (double)time.count(), REDUCTION_CONFIG);
    }
    else
    {
        std::printf("%s\treduce%ld\t%d\t%d\t%.2f\t%d\t%d\t%.2f\t%d\t%.5f\t%d\n", name.c_str(), REDUCTION_CONFIG, g->n, g->m, original_avg_e_size, rg->n, rg->m, avg_e_size, mis_alg->status.IS_size, (double)time.count(), seed);
    }

    if (solution_path)
        writeGraphToFile(rg, solution_path);

    hypergraph_free(rg);
    hypergraph_free(g);
    delete mis_alg;

    return 0;
}
