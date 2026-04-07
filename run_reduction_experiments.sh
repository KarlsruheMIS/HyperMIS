#!/bin/bash

res=results
hypergraphs=../../test_instances/hypergraphs

red_file1=$res/RED/reduction_hypergraph_graph.csv
red_file2=$res/RED/reduction_graph.csv

t=3600
n=1
SEEDS=(1 21 203 1002)

graph_run()
{
    g=$(echo "${1}" | awk -F'/' '{print $NF}')
    time1=$(./build/hypergraph_to_graph -g "${1}" -o "${1}_graph" | awk '{print $NF}')
    res=$(../KaMIS/mmwis/build/extern/struction/kernelization "${1}_graph" --seed="${2}")
    time2=$(echo "${res}" | awk -F',' '{print $8}')
    red_n=$(echo "${res}" | awk -F',' '{print $4}')
    red_m=$(echo "${res}" | awk -F',' '{print $5}')
    time=$(echo "${time1}" "${time2}" | awk '{print $1 + $2}')
    rm "${1}_graph"
    echo "${g},${2},${red_n},${red_m},${time}"
}

full_run()
{
    # reduce graph with HyperMIS
    g=$(echo "${1}" | awk -F'/' '{print $NF}')
    res1=$(./build/run_reduce -g "${1}" -o "${1}_red")
    time1=$(echo "${res1}" | awk -F',' '{print $10}')

    # expand graph and reduce with struction
    res2=`graph_run ${1}_red ${2}` 
    red_n=$(echo "${res2}" | awk -F',' '{print $3}')
    red_m=$(echo "${res2}" | awk -F',' '{print $4}')
    time2=$(echo "${res2}" | awk -F',' '{print $5}')
    time=$(echo "${time1}" "${time2}" | awk '{print $1 + $2}')
    rm "${1}_red"
    echo "${g},${2},${red_n},${red_m},${time}"
}

export -f graph_run
export -f full_run

echo "graph,seed,red_n,red_m,time,reduction" > "${red_file2}"
parallel -j "${n}" --noswap --delay 1 -k graph_run {2} {1} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file2}"

echo "graph,seed,red_n,red_m,time,reduction" > "${red_file1}"
parallel -j "${n}" --noswap --delay 1 -k full_run {2} {1} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file1}"
