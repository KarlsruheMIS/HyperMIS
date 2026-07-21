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
extern size_t NODE_DOM_FACTOR;

extern size_t USE_NEIGHBORHOOD_ARRAY;
extern size_t ON_DEMAND_NEIGHBORHOOD;
extern size_t REDUCTION_CONFIG;

#endif // CONFIG_H
