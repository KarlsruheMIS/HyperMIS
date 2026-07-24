#!/bin/bash

res=results
hypergraphs=../../test_instances/hypergraphs

red_file="$res/RED/stats.tsv"
cred_file="$res/RED/config_stats.tsv"

t=3600
n=32
SEEDS=(1 21 203 1002)
# Config layout: single rules, then full, then disable-one. Numbered in
# full-pipeline application order, so config k is the k-th rule and config 9+k
# is full without it.
#   Singles 1-8: 1 edge_size, 2 node_degree_one, 3 simplicial, 4 fast_node_dom,
#               5 edge_dom, 6 node_dom, 7 twin, 8 unconfined
#   Full     9  (all eight rules, in that order)
#   Disable  10-17: 10 no edge_size, 11 no node_degree_one, 12 no simplicial,
#               13 no fast_node_dom, 14 no edge_dom, 15 no node_dom, 16 no twin,
#               17 no unconfined
# (edge_size carries the folded spanning-edge checks; it is the trivial edge rule.)
REDUCTION_CONFIGS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17)

##### REDUCTIONS
echo -e "graph\t seed\t red_n\t red_m\t time\t reduction" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_reduce -g {2} -t "${t}" -s {1} -e -d ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"

echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed" > "${cred_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_reduce -g {3} -t "${t}" -s {1} -r{2} -d ::: "${SEEDS[@]}" ::: "${REDUCTION_CONFIGS[@]}" ::: "${hypergraphs}"/* >> "${cred_file}"
