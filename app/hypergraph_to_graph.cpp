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

#include <getopt.h>

const char *help = "Generate clique expansion graph from given hypergraph.\n"
                   "\n-h \t\tDisplay this help message\n"
                   "-g path* \tPath to the input hypergraph in METIS format\n"
                   "-o path \tSpecify path and name for the output graph in METIS format\n"
                   "-n \t\tEnable precomputed neighborhood array (initially computes neighborhoods)\n"
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

    while ((command = getopt(argc, argv, "hng:o:")) != -1)
    {
        switch (command)
        {
        case 'h':
            printf("%s\n", help);
            return 0;
        case 'n':
            USE_NEIGHBORHOOD_ARRAY = 1;
            break;
        case 'g':
            hypergraph_path = optarg;
            name = std::filesystem::path(hypergraph_path).filename().string();
            break;
        case 'o':
            graph_path = optarg;
            graph_name = std::filesystem::path(hypergraph_path).filename().string();
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

    fast_set node_set(g->n);
    auto start_time = std::chrono::high_resolution_clock::now();
    hypergraph_build_neighbors(g, &node_set);

    clique_expansion_to_file(g, graph_path);
    std::chrono::duration<double> time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time);
    std::cout << name << " transformed to " << graph_name << ".graph in time " << time.count() << std::endl;
    return 1;
}
