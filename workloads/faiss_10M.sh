#!/bin/bash

WORKLOAD_ID="faiss_10M"
WORKLOAD_OUTPUT="faiss_10M"
WORKLOAD_COMMAND="${BENCH_ROOT}/big-ann-benchmarks/.venv/bin/python3 ${BENCH_ROOT}/big-ann-benchmarks/data/10M_benchmark.py --threads 16 --index-key HNSW,Flat --stress-mode latency --dataset openai  --n-queries 5000"
WORKLOAD_EXE_NAME="python3"
WORKLOAD_MODEL_BASE="faiss_10M"
WORKLOAD_SYSTEMS="arms hybridtier model logging"
