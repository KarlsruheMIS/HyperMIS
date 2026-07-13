#pragma once

#include <stdio.h>
#include "definitions.h"
#include "fast_set.h"

typedef struct
{
    long long n, m;
    long long *V;
    NodeID *E;
} graph;