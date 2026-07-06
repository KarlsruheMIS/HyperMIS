#!/bin/bash

res=results
hypergraphs=hypergraphs

red_file="$res/RED/stats.tsv"
cred_file="$res/RED/config_stats.tsv"

t=3600
n=8
SEEDS=(1 21 203 1002)
REDUCTION_CONFIGS=(1 2 3 4 5 6 7 8 9 10 11 12 13)

##### REDUCTIONS
echo -e "graph\t seed\t red_n\t red_m\t time\t reduction" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_reduce -g {2} -t "${t}" -s {1} -e ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"

echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed" > "${cred_file}"
parallel -j "${n}" --noswap --delay 1 -k ./build/run_reduce -g {3} -t "${t}" -s {1} -r {2} ::: "${SEEDS[@]}" ::: "${REDUCTION_CONFIGS[@]}" ::: "${hypergraphs}"/* >> "${cred_file}"
