#pragma once

#include "config.h"
#include "hypergraph.h"
#include "minNodeHeap.h"
#include "reductions.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

class MISH_algorithm {
public: 
	enum IS_status { not_set, included, excluded };

private:

    friend edge_degree_one_reduction;
    friend node_degree_one_reduction;
    friend twin_reduction;
    friend fast_node_domination_reduction;
    friend node_domination_reduction;
    friend unconfined_reduction;
    friend edge_domination_reduction;

    struct hgraph_status {
        NodeID n = 0;
        NodeID m = 0;
        NodeID remaining_nodes = 0;
        NodeID remaining_edges = 0;
        NodeID IS_size = 0;
        hypergraph* hgraph;
		std::vector<IS_status> node_status;
		std::vector<bool> edge_status;
		std::vector<reduction_ptr> reductions;

		hgraph_status() = default;

        hgraph_status(hypergraph* hgr) :
			n(hgr->n), m(hgr->m), remaining_nodes(n), remaining_edges(m), hgraph(hgr), node_status(n, IS_status::not_set), edge_status(m, true) {
		}
    };

    size_t active_reduction_index;
    std::vector<size_t> reduction_map;

public:
    hgraph_status status;
    std::chrono::high_resolution_clock::time_point start_time;
    fast_set node_set;
    fast_set node_set2;
    fast_set edge_set;

    NodeID* edge_vec;
    NodeID* node_vec;
    NodeID* node_vec2;

    // for reduction effect experiments:
    std::vector<NodeID> n_reduced;
    std::vector<NodeID> m_reduced;
    std::vector<double> t_reduced;

    MISH_algorithm(hypergraph* hgr);
    ~MISH_algorithm();

	void set(NodeID hn, IS_status is_status);
    void init_reduction_step();
    void reduce_graph();
    hypergraph* build_reduced_hypergraph(hypergraph* g, std::vector<NodeID>& remap,std::vector<bool>& sol);

    void remove_edge(NodeID e);
    void add_next_level_node(NodeID v);
    void add_next_level_edge(NodeID e);
    void add_next_level_nodes_of_edge(NodeID e);
    void add_next_level_neighborhood(NodeID v);
};

