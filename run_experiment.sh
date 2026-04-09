#!/bin/bash

res=results
hypergraphs=./hypergraphs

ilp_file=$res/ILP/ilp.csv
rilp_file=$res/ILP/rilp.csv

red_file=$res/RED/red.csv

t=3600
n=40
SEEDS=(1 21 203 1002)

##### REDUCTIONS
echo "graph,algo,n,m,e,rn,rm,re,offset,time,seed" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_reduce -g {2} -t "${t}" -s {1} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"


#####  ILP
echo "graph,algo,size,time,opt,seed" > "${ilp_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_ilp -r -g {2} -t "${t}" -s {1} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${ilp_file}"

echo "graph,algo,size,time,opt,seed" > "${rilp_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_ilp -g {2} -t "${t}" -s {1} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${rilp_file}"
