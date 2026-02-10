#include <memory>
#include <vector>
#include <iostream>
#include <string>
#include <set>

#include "config.h"
#include "io.h"
#include "graph.h"
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
    char *graph_path = NULL,
         *solution_path = NULL;
    double timeout = 3600;


    unsigned int seed = time(NULL);
    int command;
    std::string name;

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
        case 'g':
            graph_path = optarg;
            name = std::filesystem::path(graph_path).filename().string();
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
        case '?':
            return 1;

        default:
            return 1;
        }
    } 


    if (graph_path == nullptr)
    {
        std::cerr << "Error: Unable to open file " << std::endl;
        return 1;
    }

    FILE* hgr_file = fopen(graph_path, "r");
    if (!hgr_file) {
        std::cerr << "Error: Unable to open file " << graph_path << std::endl;
        return 1;
    }
    graph* g = graph_parse(hgr_file);
    fclose(hgr_file);

    std::vector<bool> sol(g->n, false);
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();

    auto ILP_solution =  ILP_solver_graphs(g, timeout, start_time, sol);
    std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time);
    std::cout << name << ",ILP," <<ILP_solution.first << "," << time.count() << "," << (ILP_solution.second==2)  << "," << seed << std::endl;

    if (solution_path)
        writeSolutionToFile(sol, solution_path);

    graph_free(g);
}