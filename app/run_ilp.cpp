#include <memory>
#include <vector>
#include <iostream>
#include <string>
#include <set>

#include "config.h"
#include "io.h"
#include "MIS_algorithm.h"
#include "reductions.h"
#include "ILP_solver.h"
#include <cstdlib>
#include <getopt.h>
#include <cassert>


const char *help = "hyperMISReduce --- Data reduction rules for the Maximum Independent Set problem on Hypergraphs\n"
                   "\nThe output of the program without -v is a single line on the form:\n"
                   "instance_name,#vertices,#edges,is_weight,time,seed\n"
                   "\n-h \t\tDisplay this help message\n"
                   "-v \t\tVerbose mode, output continous updates to STDOUT\n"
                   "-g path* \tPath to the input hypergraph in METIS format\n"
                   "-o path \tPath to store the best solution found \t\t default not stored\n"
                   "-r \t\t Use reduction preprocessing" 
                   "-t sec \t\tTimout in seconds \t\t\t\t default 3600 seconds\n"
                   "-s s \t\tSet a specific random seed \t\t\t default time(NULL)\n"
                   "-k sec \t\tSet time limit for reduction \t\t\t default 100\n"
                   "\n* Mandatory input";

int main(int argc, char **argv)
{
    char *hypergraph_path = NULL,
         *solution_path = NULL;
    double timeout = 3600;


    unsigned int seed = time(NULL);
    int command;
    std::string name;
    std::string config = "KFast";

    while ((command = getopt(argc, argv, "hvprg:t:s:k:o:")) != -1)
    {
        switch (command)
        {
        case 'h':
            printf("%s\n", help);
            return 0;
        case 'v':
            VERBOSE = 1;
            break;
        case 'p':
            REDUCE = 1;
            UNCONFINED_REDUCE = 1;
            TIME_KERNEL_SECONDS = 100;
            NUM_REMOVED_EDGES = 1000000;
            CONSTANT_UNCONFINED = 5;
            ITERATIONS_UNCONFINED = 20000;
            EDGE_SIZE = 5000;
            config="KStrong";
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
            REDUCE = 1;
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

    FILE* hgr_file = fopen(hypergraph_path, "r");
    if (!hgr_file) {
        std::cerr << "Error: Unable to open file " << hypergraph_path << std::endl;
        return 1;
    }
    hypergraph* g = hypergraph_parse(hgr_file);
    fclose(hgr_file);

    MISH_algorithm* mis_alg = new MISH_algorithm(g);
    hypergraph_build_neighbors(g, &mis_alg->node_set);
    // assert(hypergraph_validate(g));
    std::vector<bool> sol(g->n, false);

    if (REDUCE) {
        mis_alg->reduce_graph();
        NodeID size = mis_alg->status.IS_size;
        std::vector<NodeID> map(g->n,-1);
        std::vector<NodeID> remap;
        if (mis_alg->status.remaining_nodes > 0)
        {
            hypergraph* rg = mis_alg->build_reduced_hypergraph(mis_alg->status.hgraph, map, remap, sol);
            // assert(hypergraph_validate(rg));
            // assert(verifier(g, sol));
            std::vector<bool> red_sol(rg->n, false);

            auto ILP_solution =  ILP_solver(rg, timeout, mis_alg->start_time, red_sol);
            std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - mis_alg->start_time);
            size += ILP_solution.first;
            // assert(verifier(rg, red_sol));
            std::cout << name << ",ILP"<<config<<"," <<size << "," << time.count() << "," << (ILP_solution.second==2)  << "," << seed << std::endl;

            // remap reduced_solution 
            for (int i = 0; i < red_sol.size(); ++i)
                if (red_sol[i])
                    sol[remap[i]] = true;

            NodeID sol_size = 0;
            for (NodeID i = 0; i < g->n; i++)
                if (sol[i])
                    sol_size++;
            hypergraph_free(rg);
        } 
        else
        {
            std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - mis_alg->start_time);
            std::cout << name << ",ILP"<<config<<"," <<size << "," << time.count() << ",1," << seed  << std::endl;
        }

        // add reduced vertices to the solution
        for (int i = 0; i < mis_alg->status.n; ++i)
            if (mis_alg->status.node_status[i] == IS_status::included)
                sol[i] = true;


    } else {
        auto ILP_solution =  ILP_solver(g, timeout, mis_alg->start_time, sol);
        std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - mis_alg->start_time);
        std::cout << name << ",ILP," <<ILP_solution.first << "," << time.count() << "," << (ILP_solution.second==2)  << "," << seed << std::endl;
    }
    if (solution_path)
        writeSolutionToFile(sol, solution_path);

    delete mis_alg;
    hypergraph_free(g);
}