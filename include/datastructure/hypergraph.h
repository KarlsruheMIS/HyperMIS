#pragma once

#include <stdio.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "definitions.h"
#include "graph.h"
#include "fast_set.h"

typedef struct
{
    NodeID n, m;
    NodeID *Vd, *Va, *Ed, *Ea, *Nd, *Na;
    NodeID **V, **E, **N;
    bool has_neighbors;
    bool on_demand;
    bool *N_valid;
    fast_set *nbr_scratch;
    fast_set *edge_scratch;
    NodeID n_healed;
} hypergraph;

hypergraph *hypergraph_parse(FILE *f);

void hypergraph_free(hypergraph *g);

NodeID *hypergraph_get_neighborhood(hypergraph *g, NodeID u, NodeID *neighborhood, NodeID &deg, fast_set &node_set);
NodeID *hypergraph_get_neighborhood_and_set(hypergraph *g, NodeID u, NodeID *neighborhood, NodeID &deg, fast_set &node_set);

void hypergraph_build_neighbors(hypergraph *g, fast_set *fs);

void hypergraph_init_on_demand(hypergraph *g);

// Modify
void hypergraph_remove_element(NodeID *vec, NodeID &size, NodeID element);
void hypergraph_remove_set(NodeID *vec, NodeID &size, fast_set *set);
void hypergraph_reset(hypergraph *g, NodeID element, int type);

void hypergraph_remove_vertex(hypergraph *g, NodeID u, fast_set* processed);

void hypergraph_remove_edges(hypergraph *g, NodeID *E, NodeID e_size, fast_set *edges, fast_set *nodes);
void hypergraph_remove_size_one_edge(hypergraph *g, NodeID e);
void hypergraph_remove_neighborhood(hypergraph *g, NodeID u, fast_set *nodes, fast_set *processed, fast_set *edges, NodeID *changed);

// Utility
graph *hypergraph_clique_expansion(hypergraph *hg);

bool hypergraph_validate(hypergraph *g);

hypergraph *hypergraph_build_reduced(hypergraph *g, NodeID *map, NodeID *remap, int *status);
