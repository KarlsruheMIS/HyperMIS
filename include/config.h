#ifndef CONFIG_H
#define CONFIG_H

#include "definitions.h"

#include <cstddef> 

extern size_t VERBOSE;
extern size_t EXPERIMENT;
extern size_t REDUCE;

extern size_t TIME_KERNEL_SECONDS;
extern size_t NEIGHBORS_SIZE;
extern size_t MAX_DEGREE;
// node_domination scales MAX_DEGREE and NEIGHBORS_SIZE by this factor so it can
// reach higher-degree / larger-neighborhood vertices than the other rules. It is
// reached in stages (fixpoint at factor 1 first, then a re-sweep of the residual
// at the full factor), which guarantees the kernel never grows vs. factor 1.
extern size_t NODE_DOM_FACTOR;
extern size_t ITERATIONS_UNCONFINED;
extern size_t CONSTANT_UNCONFINED;

extern size_t USE_NEIGHBORHOOD_ARRAY;
extern size_t ON_DEMAND_NEIGHBORHOOD;
extern size_t REDUCTION_CONFIG;

#endif // CONFIG_H