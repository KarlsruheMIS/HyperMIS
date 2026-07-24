#!/bin/bash

res=results
# Full instance set -- reduction experiments run on this.
hypergraphs=../../test_instances/hypergraphs
# Subset provably solvable by at least one ILP method (opt=1 in any ILP result).
# ILP and the other exact-solver runs only work on this set; see collect script.
hypergraphs_solvable=../../test_instances/hypergraphs_ilp_solvable

ilp_file=$res/ILP/ilp.tsv
rilp_file=$res/ILP/rilp.tsv
filp_file=$res/ILP/filp.tsv
frilp_file=$res/ILP/frilp.tsv
nilp_file=$res/ILP/nilp.tsv
nrilp_file=$res/ILP/nrilp.tsv

gfilp_file=$res/ILP/gfilp.tsv
gfrilp_file=$res/ILP/gfrilp.tsv

red_file=$res/RED/red.tsv
nred_file=$res/RED/nred.tsv
fred_file=$res/RED/fred.tsv

n=32
t=3600
SEEDS=(1 21 203 1002)

append_mem() {
  local id="$1"

  # read memory
  local mem
  mem=$(cat /tmp/mem."$id")
  rm -f /tmp/mem."$id"
  echo -e "$2\t$mem"
}

run_reduce() {
  local seed="$1"
  local graph="$2"
  shift 2

  local out

  # run program while capturing memory via /usr/bin/time
  out=$(
    /usr/bin/time -f "%M" \
      ./build/run_reduce "$@" -g "$graph" -t "$t" -s "$seed" \
      2> /tmp/mem.$$
  )

  append_mem $$ "$out"
}

run_ilp() {
  local seed="$1"
  local graph="$2"
  shift 2

  local out

  # run program while capturing memory via /usr/bin/time
  out=$(
    /usr/bin/time -f "%M" \
      ./build/run_ilp "$@" -g "$graph" -t "$t" -s "$seed" \
      2> /tmp/mem.$$
  )

  append_mem $$ "$out"
}

export -f run_reduce
export -f run_ilp
export -f append_mem
export t

###### REDUCTIONS
echo "Starting Reduction Experiments"
echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed\tmem" > "${nred_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} -n ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nred_file}"
echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed\tmem" > "${fred_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} -d ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${fred_file}"
echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed\tmem" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} -r9 ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"

#####  ILP
# echo "Starting ILP Experiments"
# echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${ilp_file}"
# parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} -r0 ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${ilp_file}"
# echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${nilp_file}"
# parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} -n -r0 ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${nilp_file}"
# echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${filp_file}"
# parallel -j "${n}" --noswap --delay 1 -k run_ilp {1} {2} -d -r0 ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${filp_file}"

#### reduced ILP
# echo "Starting Reduced ILP Experiments"
# echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${rilp_file}"
# parallel -j "${n}" --noswap --delay 1 -k run_ilp {1} {2} ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${rilp_file}"
# echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${nrilp_file}"
# parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} -n ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${nrilp_file}"
# echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${frilp_file}"
# parallel -j "${n}" --noswap --delay 1 -k run_ilp {1} {2} -d ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${frilp_file}"


#####  GRAPH ILP
# echo "Starting Graph ILP Experiments"
echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${gfilp_file}"
parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} -d -r0 -e ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${gfilp_file}"
echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${gfrilp_file}"
parallel -j "${n}" --noswap --delay 1 -k run_ilp {1} {2} -d -e ::: "${SEEDS[@]}" ::: "${hypergraphs_solvable}"/* >> "${gfrilp_file}"
