app="/root/codes/GraphBIG/benchmark/bench_BFS/bfs"
# app="/root/codes/random_access/randomAccess"
instruction=

# ../sniper/record-trace -o randomAccess -- $app
../sniper/record-trace -d 30000000 -o GraphBIG_BFS_amazon0302_30M -- $app --dataset /mnt/panzer/rahbera/graph_datasets/graphBIG/CL-10M-1d8-L5


