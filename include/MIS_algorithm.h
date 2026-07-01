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

    friend degree_one_reduction;
    friend twin_reduction;
    friend sunflower_reduction;
    friend node_domination_reduction;

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
    size_t last_progress_reduction = 0;
    std::vector<size_t> reduction_map;

public:
    hgraph_status status;
    element_marker<NodeID> edge_marker;
    std::chrono::high_resolution_clock::time_point start_time;
    NodeID num_edge_domination = 0;
    NodeID removed_heu = 0;
    fast_set node_set;
    fast_set edge_set;

    NodeID* node_vec;
    NodeID* node_vec2;

    // for reduction effect experiments:
    std::vector<NodeID> n_reduced;
    std::vector<NodeID> m_reduced;
    std::vector<double> t_reduced;

    static const NodeID RESTRICTION_SIZE = 100000;

    MISH_algorithm(hypergraph* hgr);
    ~MISH_algorithm();

	void set(NodeID hn, IS_status is_status);
    void init_reduction_step();
    void reduce_graph();
    void add_next_level_node(NodeID hn);
    void add_next_level_nodes_of_edge(NodeID he);
    void add_next_level_neighborhood(NodeID hn);
    bool remove_dominating_edges();
    hypergraph* build_reduced_hypergraph(hypergraph* g, std::vector<NodeID>& remap,std::vector<bool>& sol);
};
