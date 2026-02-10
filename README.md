[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

# hyperMIS

## Installation

You need to have GUROBI installed and set the GUROBI_HOME path in the CMakeLists.txt file.
Then, run the following commands for installation:
```
mkdir build
cd build
cmake ..
make
```
This will produce different executables to run the ILP on hypergraphs or on graphs, as well as only reducing the instance. 

## Program Options

Here are important options to specify for our programs:

| Option | Description | Default | Mandatory |
|-|-|-|-|
| `-h` | Display help information | | |
| `-v` | Verbose mode, shows continuous updates to STDOUT | | |
| `-g path` | Path to the input hypergraph, see input format | | &check; |
| `-o path` | Path to the output for the reduced hypergraph | | |
| `-t sec` | Timeout in seconds | 3600 (1h) | |
| `-s` | User specific input seed | | |
| `-r` | enable fast reductions, only necessary for ILP runs | | |
| `-p` | enable strong reductions | | |

The output of the program without the `-v` option is a single line in the format
```
instance_name,algo,#vertices,#edges,avg_edge_size,#vertices_reduced,#edges_reduced,avg_edge_size_reduced,offset,time,seed
```

## How to Use

Examples of typical use cases are listed below. Change the time limit with `-t`, or add `-r` or `-p` for different reduction configurations as necessary.

## Input Format

Our programs expect hypergraphs in the extended METIS graph format. A graph with **M** edges is stored using **M + 1** lines. The first line lists the number of edges, the number of vertices, and potentially a weight type. 
Each subsequent line first gives the weight (if the input is weighted, ignored by the programs) and then lists the vertices of that edge.

Here is an example of a hypergraph with 4 edges, where the third edge contains all three vertices.

```
4 3
1 3
2 3
1 2 3
1 3
```
For weighted hypergraphs, we additionally pass the weight type (11), the edge weights as described above, as well as further **N** lines listing the vertex weights. 
Here is an example of the same hypergraph with 4 edges and weights added. The edges have weights 20, 30, 40, and 50, while all vertices have a weight of 5.

```
4 3 11
20 1 3
30 2 3
40 1 2 3
50 1 3
5
5
5
```
Notice that vertices are 1-indexed.

The script `run_experiments.sh` runs our code on the instances in the hypergraphs folder and stores the results in a csv file.
