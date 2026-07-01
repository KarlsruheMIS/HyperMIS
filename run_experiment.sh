#!/bin/bash

res=results
hypergraphs=./hypergraphs

ilp_file=$res/ILP/ilp.csv
rilp_file=$res/ILP/rilp.csv
nilp_file=$res/ILP/nilp.csv
nrilp_file=$res/ILP/nrilp.csv

red_file=$res/RED/red.csv
nred_file=$res/RED/nred.csv

n=1
# SEEDS=(1 21 203 1002)
SEEDS=(0)

run_reduce() {
  local seed="$1"
  local graph="$2"
  local option="$3"

  # run program, capture stdout
  local out

  # run program while capturing memory via /usr/bin/time
  out=$(
    /usr/bin/time -f "%M" \
      ./build/run_reduce "$option" -g "$graph" -t 3600 -s "$seed" \
      2> /tmp/mem.$$
  )

  # read memory (last line of time output)
  local mem
  mem=$(cat /tmp/mem.$$)
  rm -f /tmp/mem.$$

  # output CSV line
  echo "$out,$mem"
}

run_ilp() {
  local seed="$1"
  local graph="$2"
  local option="$3"

  # run program, capture stdout
  local out

  # run program while capturing memory via /usr/bin/time
  out=$(
    /usr/bin/time -f "%M" \
      ./build/run_ilp "$option" -r -g "$graph" -t 3600 -s "$seed" \
      2> /tmp/mem.$$
  )

  # read memory (last line of time output)
  local mem
  mem=$(cat /tmp/mem.$$)
  rm -f /tmp/mem.$$

  # output CSV line
  echo "$out,$mem"
}

run_rilp() {
  local seed="$1"
  local graph="$2"
  local option="$3"

  # run program, capture stdout
  local out

  # run program while capturing memory via /usr/bin/time
  out=$(
    /usr/bin/time -f "%M" \
      ./build/run_ilp "$option" -g "$graph" -t 3600 -s "$seed" \
      2> /tmp/mem.$$
  )

  # read memory (last line of time output)
  local mem
  mem=$(cat /tmp/mem.$$)
  rm -f /tmp/mem.$$

  # output CSV line
  echo "$out,$mem"
}
export -f run_rilp
export -f run_reduce
export -f run_ilp

##### REDUCTIONS
echo "graph,algo,n,m,e,rn,rm,re,offset,time,seed,mem" > "${nred_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} "-n" ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nred_file}"

echo "graph,algo,n,m,e,rn,rm,re,offset,time,seed,mem" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"


#####  ILP
echo "graph,algo,size,time,opt,seed,mem" > "${ilp_file}"
parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${ilp_file}"

echo "graph,algo,size,time,opt,seed,mem" > "${nilp_file}"
parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} "-n" ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nilp_file}"


##### reduced ILP
echo "graph,algo,size,time,opt,seed,mem" > "${rilp_file}"
parallel -j "${n}" --noswap --delay 1 -k run_rilp {1} {2} {} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${rilp_file}"

echo "graph,algo,size,time,opt,seed,mem" > "${nrilp_file}"
parallel -j "${n}" --noswap --delay 1 -k run_rilp {1} {2} "-n" ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nrilp_file}"
