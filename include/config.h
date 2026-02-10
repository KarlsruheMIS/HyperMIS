#ifndef CONFIG_H
#define CONFIG_H

#include <cstddef> 

extern size_t VERBOSE;
extern size_t REDUCE;
extern size_t NUM_THREADS;

extern size_t TIME_KERNEL_SECONDS;
extern size_t NEIGHBORS_SIZE;
extern size_t NUM_REMOVED_EDGES;
extern size_t ITERATIONS_UNCONFINED;
extern size_t CONSTANT_UNCONFINED;
extern size_t UNCONFINED_REDUCE;
extern size_t EDGE_SIZE;
extern size_t TWIN_NEIGHBORHOOD;
extern size_t SUNFLOWER_NEIGHBORHOOD;

extern size_t HEURISTIC_RED;
extern size_t H_EXCLUDE;

extern size_t GREEDY_SIZE;
extern size_t GREEDY_DEGREE;
extern size_t RP;

#endif // CONFIG_H