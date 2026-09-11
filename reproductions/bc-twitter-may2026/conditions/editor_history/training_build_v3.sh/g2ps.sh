#!/bin/bash

logs=(
	## GAPBS (twitter.sg)
	"data/bc-twitter.sg/bc-twitter.sg.parquet"
	#"data/bfs-twitter.sg/bfs-twitter.sg.parquet"
	"data/pr-twitter.sg/pr-twitter.sg.parquet"

	## GAPBS (kron.sg)
	"data/bc-kron.sg/bc-kron.sg.parquet"
	#"data/bfs-kron.sg/bfs-kron.sg.parquet"
	"data/pr-kron.sg/pr-kron.sg.parquet"

	## XSBench
	"data/XSBench/XSBench.parquet"

	"data/faiss_10M/faiss_10M.parquet"

	## NPB D size programs
	"data/mg.D.x/mg.D.x.parquet"

	## LULESH
	#"data/lulesh2.0_s400/lulesh2.0_s400.parquet"

	## DuckDB benchmarks
	"data/DuckDB-TPCH-sf100/DuckDB-TPCH-sf100.parquet"
	##"data/DuckDB-TPCDS-sf100/DuckDB-TPCDS-sf100.parquet"
)

rewards=("99" "95" "90")

#rewards=("90")

for reward in "${rewards[@]}"; do
	for log_file in "${logs[@]}"; do
		echo "Training model from ${log_file} with discounted_reward_${reward}"
		python3.10 all_models_v3.py "${log_file}" "${reward}"
		rm -rf smac3*
	done
done