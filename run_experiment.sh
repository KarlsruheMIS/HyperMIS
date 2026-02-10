res=results
hypergraphs=./hypergraphs

ilp_file=$res/ILP/ilp.csv
rilp_file=$res/ILP/rilp.csv
pilp_file=$res/ILP/pilp.csv

rred_file=$res/RED/rred.csv
pred_file=$res/RED/pred.csv

t=3600
n=100
SEEDS="1 21 203 1002"

##### REDUCTIONS
echo "graph,algo,n,m,e,rn,rm,re,offset,time,seed" > ${rred_file}
parallel -j ${n} --noswap --delay 1 -k ./build/run_reduce -g {2} -t ${t} -s {1} ::: $SEEDS ::: ${hypergraphs}/* >> ${rred_file}

echo "graph,algo,n,m,e,rn,rm,re,offset,time,seed" > ${pred_file}
parallel -j ${n} --noswap --delay 1 -k ./build/run_reduce -g {2} -t ${t} -s {1} -p ::: $SEEDS ::: ${hypergraphs}/* >> ${pred_file}

#####  ILP
echo "graph,algo,size,time,opt,seed" > ${ilp_file}
parallel -j ${n} --noswap --delay 1 -k ./build/run_ilp -g {2} -t ${t} -s {1} ::: $SEEDS ::: ${hypergraphs}/* >> ${ilp_file}

echo "graph,algo,size,time,opt,seed" > ${rilp_file}
parallel -j ${n} --noswap --delay 1 -k ./build/run_ilp -r -g {2} -t ${t} -s {1} ::: $SEEDS ::: ${hypergraphs}/* >> ${rilp_file}

echo "graph,algo,size,time,opt,seed" > ${pilp_file}
parallel -j ${n} --noswap --delay 1 -k ./build/run_ilp -r -p -g {2} -t ${t} -s {1} ::: $SEEDS ::: ${hypergraphs}/* >> ${pilp_file}
