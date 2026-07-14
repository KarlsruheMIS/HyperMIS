cmake_minimum_required(VERSION 3.13)
project(MISHYPER)
set(CMAKE_CXX_STANDARD 17)

# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)
# -DNDEBUG compiles out assert() — notably the two full-graph
# assert(hypergraph_validate(...)) calls in run_reduce, which otherwise cost
# ~5% of the reduction time. Drop it (or build Debug) when debugging.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
# Link-time optimization: lets the hot cross-TU calls (e.g. hypergraph_get_neighborhood,
# called tens of millions of times) inline into the reduction rules.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(GUROBI_HOME /home/ernestineg/gurobi/gurobi1203/linux64)
set(GRB_LICENSE_FILE /home/ernestineg/)
include_directories(${GUROBI_HOME}/include)
link_directories(${GUROBI_HOME}/lib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/datastructure)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/meta)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/utils)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/app)

set(LIBFILES
    src/MIS_algorithm.cpp
    src/reductions.cpp
    src/config.cpp
    src/hypergraph.cpp
)
add_library(libfiles OBJECT ${LIBFILES})


add_executable(run_reduce app/run_reduce.cpp)
add_executable(run_ilp app/run_ilp.cpp src/ILP_solver.cpp)
add_executable(hypergraph_to_graph app/hypergraph_to_graph.cpp)

target_link_libraries(run_reduce libfiles)
target_link_libraries(hypergraph_to_graph libfiles)
target_link_libraries(run_ilp libfiles gurobi_c++ gurobi120)

set(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath,${GUROBI_HOME}/lib")
