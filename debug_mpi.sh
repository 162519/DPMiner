#!/bin/bash
# debug_mpi_mpich.sh
set -e

echo "Building program..."

echo "Starting MPI processes with gdbserver..."
# MPICH使用不同的参数
mpiexec -np 3 -hostfile hosts.txt \
    -launcher ssh \
    -launcher-exec /usr/bin/ssh \
    -genv DISPLAY=$DISPLAY \
    -genv LD_LIBRARY_PATH=$LD_LIBRARY_PATH \
    bash -c "cd $(pwd) && gdbserver :1234 ./bin/miner hash \
    /home/caohaoshuang/pjj/distributed_graph_query/PMiner_v1.1/data/dataDec_web-Google/dataDec \
    /home/caohaoshuang/pjj/distributed_graph_query/PMiner_v1.1/test/no5v.txt \
    /home/caohaoshuang/pjj/distributed_graph_query/PMiner_v1.1/test/no5e.txt \
    /home/caohaoshuang/pjj/distributed_graph_query/PMiner_v1.1/data/dataDec_web-Google/Web-Google.txt"