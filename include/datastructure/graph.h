#pragma once

#include <stdio.h>
#include "definitions.h"
#include "fast_set.h"

typedef struct
{
    NodeID n, m;
    NodeID *V, *E;
} graph;

graph *graph_parse(FILE *f);

void graph_free(graph *g);