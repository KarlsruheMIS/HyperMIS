#!/bin/bash

res=results
hypergraphs=../../test_instances/hypergraphs

red_file=$res/RED/stats.csv

t=3600
n=20
SEEDS=(1 21 203 1002)

##### REDUCTIONS
echo "graph,seed,red_n,red_m,time,reduction" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_reduce -g {2} -t "${t}" -s {1} -e ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"
