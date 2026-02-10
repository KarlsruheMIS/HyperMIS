#ifndef ILP_SOLVER
#define ILP_SOLVER

#include <set>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include "definitions.h"
#include "graph.h"
#include "MIS_algorithm.h"

typedef MISH_algorithm::IS_status IS_status;

//new hypergraph data structures for LP
using HyperedgeLP = std::set<int>;
using HyperedgeVectorLP = std::vector<HyperedgeLP>;

namespace fs = std::filesystem;

struct HypergraphLP {
    HyperedgeVectorLP hyperedges;
    std::set<int> hypernodes;
    size_t num_hyperedges;
    size_t num_hypernodes;

    HypergraphLP(HyperedgeVectorLP& hes,std::set<int>& hns, size_t num_he, size_t num_hn) : hyperedges(hes), hypernodes(hns), num_hyperedges(num_he), num_hypernodes(num_hn) {}
    HypergraphLP(HyperedgeVectorLP& hes, size_t num_he, size_t num_hn) : hyperedges(hes), num_hyperedges(num_he), num_hypernodes(num_hn) {}

};

std::pair<NodeID, int> ILP_solver(hypergraph *hgr, double time_limit_seconds, std::chrono::_V2::system_clock::time_point start_time, std::vector<bool>& solution, const std::vector<bool>& initial_solution = std::vector<bool>());
std::pair<NodeID, int> ILP_solver_graphs(graph *g, double time_limit_seconds, std::chrono::_V2::system_clock::time_point start_time, std::vector<bool> &solution, const std::vector<bool> &initial_solution = std::vector<bool>());
bool verifier(hypergraph* hgr, std::vector<bool>& IS);
bool verifierMIS(MISH_algorithm* mis_alg, hypergraph* hgr);

#endif //ILP_SOLVER