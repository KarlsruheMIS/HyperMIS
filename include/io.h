#pragma once

#include <cstring>
#include <fstream>
#include <iostream>

#include "hypergraph.h"

// write solution to file such that each line corresponds to the vertex with line number = ID
// 1 means the vertex is in the MIS
static inline void writeSolutionToFile(std::vector<bool>& sol, const std::string& filename) {
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing.\n";
        return;
    }

    for (bool value : sol) {
        outFile << (value ? '1' : '0');
        outFile << '\n';
    }
    outFile.close();
}



static inline void writeGraphToFile(hypergraph* g, const std::string& filename) {
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing.\n";
        return;
    }
    outFile << g->m << " " << g->n << "\n";
    for (NodeID e = 0; e < g->m; e++)
    {
        for (NodeID i = 0; i < g->Ed[e]; i++)
        {
            outFile << g->E[e][i] +1 << " ";
        }
        outFile << "\n";
    }

    outFile.close();
}
