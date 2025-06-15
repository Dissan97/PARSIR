xX#!/bin/bash

#declare lookaheads=(0.1 0.5 0.8 1.0 2.0)
declare runs=3
declare lookaheads=(0.5 0.8 1.0)
#declare alphas=(0.25 0.5 1.0 2.0 5.0)
declare alphas=(0.5 1.0 2.0)
#declare objects=(512 1024 4096)
declare objects=(1024 4096)
declare queue=("ORIGIN" "LPTF")
declare numa=("0" "1")
declare balanced=("0" "1")
#declare models=("pcs" "highway")
declare models=("pcs" "phold")
balance="balanced"
BALANCE="-BALANCE"
NUMA=""
lines="--------------------------------------------------------------"
rm -rf ../bin/simulation/balanced
rm -rf ../bin/simulation/unbalanced
rm -rf ../bin/simulation/logs
mkdir -p ../bin/simulation/balanced
mkdir -p ../bin/simulation/unbalanced
mkdir -p ../bin/simulation/logs

for run in $(seq 1 $runs); do
  for model in "${models[@]}"; do
    for lookahead in "${lookaheads[@]}"; do
      for alpha in "${alphas[@]}"; do
        for object in "${objects[@]}"; do
          for q in "${queue[@]}"; do
            if [[ "$q" == "ORIGIN" && "$alpha" != "1.0" ]]; then
              continue
            fi 
            if [[ "$model" == "phold" && "$lookahead" != "1.0" && "$object" != "4096" ]]; then continue; fi
            for n in "${numa[@]}"; do
              for b in "${balanced[@]}"; do
                if [[ $b -eq 0 ]]; then
                  balance="balanced"
                  BALANCE="-BALANCE"
                else
                  balance="unbalanced"
                  BALANCE="-UNBALANCE"
                fi
                if [[ $n -eq 1 ]]; then
                  NUMA="-NUMA"
                else
                  NUMA=""
                fi
                echo "$lines"
                config="${model}-${q}${NUMA}${BALANCE}-APH_${alpha}-LAH_${lookahead}-OBJ_${object}"
                echo "Running configuration: $config"
                echo "$lines"
                make "$model" QUEUE_IMPL="$q" NUMA="$n" ALPHA="$alpha" LOOKAHEAD="$lookahead" OBJECTS="$object" UNBALANCE="$b"

                bin_path="../bin/simulation/${balance}/PARSIR-simulator-${config}"
                mv ../bin/PARSIR-simulator-${model}-${q}${NUMA}${BALANCE} ${bin_path}
                echo "$lines"
                echo "Simulation for $config"
                ${bin_path} | grep 'Barrier measures\|throughput' >../bin/simulation/logs/${run}_${config}.log
                echo "$lines"
                echo "Log file created at: ../bin/simulation/logs/${run}_${config}.log"
                echo "$lines"
              done
            done
          done
        done
      done
    done
  done
done

unset lookaheads
unset alphas
unset objects
unset queue
unset numa
unset models
unset balanced
unset runs
