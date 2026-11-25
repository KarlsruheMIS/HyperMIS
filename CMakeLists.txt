cmake_minimum_required(VERSION 3.13)
project(MISHYPER)
set(CMAKE_CXX_STANDARD 17)

# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

# ---- Gurobi -----------------------------------------------------------------
# Pass -DGUROBI_HOME=/path/to/gurobiXXXX/linux64 or set the GUROBI_HOME
# environment variable. Common install locations are searched as a fallback.
set(GUROBI_HOME "$ENV{GUROBI_HOME}" CACHE PATH "Gurobi installation directory (contains include/ and lib/)")

# If no path was given, fall back to the newest install in a common location.
if(NOT GUROBI_HOME)
    file(GLOB GUROBI_CANDIDATES /opt/gurobi*/linux64 /usr/local/gurobi*/linux64)
    list(SORT GUROBI_CANDIDATES ORDER DESCENDING)
    foreach(cand IN LISTS GUROBI_CANDIDATES)
        if(EXISTS "${cand}/include/gurobi_c++.h")
            set(GUROBI_HOME "${cand}" CACHE PATH "Gurobi installation directory" FORCE)
            break()
        endif()
    endforeach()
endif()

# Search only inside the chosen installation so versions are never mixed.
find_path(GUROBI_INCLUDE_DIR gurobi_c++.h HINTS "${GUROBI_HOME}" PATH_SUFFIXES include NO_DEFAULT_PATH)
find_library(GUROBI_CXX_LIBRARY NAMES gurobi_c++ HINTS "${GUROBI_HOME}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
find_library(GUROBI_LIBRARY NAMES gurobi gurobi130 gurobi120 gurobi110 gurobi100 gurobi95 gurobi91
    HINTS "${GUROBI_HOME}" PATH_SUFFIXES lib NO_DEFAULT_PATH)

if(NOT GUROBI_INCLUDE_DIR OR NOT GUROBI_CXX_LIBRARY OR NOT GUROBI_LIBRARY)
    message(FATAL_ERROR
        "Gurobi was not found.\n"
        "Tell CMake where it is installed (the directory containing include/ and lib/), e.g.\n"
        "    cmake -DGUROBI_HOME=/opt/gurobi1203/linux64 ..\n"
        "or set the GUROBI_HOME environment variable. Remember to delete the build/ "
        "directory contents (or CMakeCache.txt) if a previous run failed.")
endif()

get_filename_component(GUROBI_LIB_DIR "${GUROBI_LIBRARY}" DIRECTORY)
message(STATUS "Gurobi include: ${GUROBI_INCLUDE_DIR}")
message(STATUS "Gurobi libs:    ${GUROBI_CXX_LIBRARY}, ${GUROBI_LIBRARY}")
include_directories(${GUROBI_INCLUDE_DIR})

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
add_executable(clique_blowup app/clique_blowup.cpp)

target_link_libraries(run_reduce libfiles)
target_link_libraries(hypergraph_to_graph libfiles)
target_link_libraries(clique_blowup libfiles)
target_link_libraries(run_ilp libfiles ${GUROBI_CXX_LIBRARY} ${GUROBI_LIBRARY})

set(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath,${GUROBI_LIB_DIR}")

add_executable(graph_reduction_comparison
    graph_comparison/graph_reduction_comparison.cpp
    graph_comparison/graph_reductions.cpp
    src/ILP_solver.cpp)
target_include_directories(graph_reduction_comparison PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/graph_comparison)
target_link_libraries(graph_reduction_comparison libfiles ${GUROBI_CXX_LIBRARY} ${GUROBI_LIBRARY})
