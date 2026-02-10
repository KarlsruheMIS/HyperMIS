#pragma once

#include <stdio.h>
#include "definitions.h"
#include "fast_set.h"

typedef struct
{
    NodeID n, m;
    NodeID *Vd, *Va, *Ed, *Ea, *Nd, *Na;
    NodeID **V, **E, **N;
} hypergraph;

hypergraph *hypergraph_parse(FILE *f);

void hypergraph_free(hypergraph *g);

void hypergraph_build_neighbors(hypergraph *g, fast_set* fs);

// Modify

void hypergraph_remove_vertex(hypergraph *g, NodeID u);

void hypergraph_remove_edge(hypergraph *g, NodeID e, fast_set* fs, bool dominated=false);

void hypergraph_remove_neighbors(hypergraph *g,  NodeID u, fast_set* fs, fast_set* efs);


// Utility

void hypergraph_sort(hypergraph *g);

bool hypergraph_validate(hypergraph *g);

bool hypergraph_is_neighbor(hypergraph *g, NodeID v, NodeID neighbor);

hypergraph* hypergraph_build_reduced(hypergraph *g,  NodeID* map, NodeID* remap, int* status);

hypergraph *hypergraph_copy(hypergraph *g);