cmake_minimum_required(VERSION 3.13)
project(MISHYPER)
set(CMAKE_CXX_STANDARD 17)

# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(GUROBI_HOME /opt/gurobi1203/linux64)
set(GRB_LICENSE_FILE /opt/gurobi1203/gurobi.lic)
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
