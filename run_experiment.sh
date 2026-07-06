#!/bin/bash

res=results
hypergraphs=./hypergraphs

ilp_file=$res/ILP/ilp.tsv
rilp_file=$res/ILP/rilp.tsv
nilp_file=$res/ILP/nilp.tsv
nrilp_file=$res/ILP/nrilp.tsv

red_file=$res/RED/red.tsv
nred_file=$res/RED/nred.tsv

n=8
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

##### REDUCTIONS
echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed\tmem" > "${nred_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} -n ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nred_file}"

echo -e "graph\talgo\tn\tm\te\trn\trm\tre\toffset\ttime\tseed\tmem" > "${red_file}"
parallel -j "${n}" --noswap --delay 1 -k run_reduce {1} {2} ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${red_file}"


#####  ILP
echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${ilp_file}"
parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} -r0 ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${ilp_file}"

echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${nilp_file}"
parallel -j "${n}"  --noswap --delay 1 -k run_ilp {1} {2} -n -r0 ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nilp_file}"


##### reduced ILP
echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${rilp_file}"
parallel -j "${n}" --noswap --delay 1 -k run_ilp {1} {2} -n ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${rilp_file}"

echo -e "graph\talgo\tsize\ttime\topt\tseed\tmem" > "${nrilp_file}"
parallel -j "${n}" --noswap --delay 1 -k run_ilp {1} {2} -n ::: "${SEEDS[@]}" ::: "${hypergraphs}"/* >> "${nrilp_file}"
