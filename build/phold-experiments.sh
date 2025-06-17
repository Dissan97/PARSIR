#!/bin/bash

declare runs=1
declare lookaheads=(0.05)
declare alphas=(0.25) 
declare objects=(1024) 
declare queue=("ORIGIN")
declare numa=("0" "1") 
declare balanced=("1") 
declare models=("phold") 
declare h_loads=(2 200 500)  
declare hotspots=(0.01 0.001) 
balance="balanced"
BALANCE="-BALANCE"
NUMA=""
 
lines="--------------------------------------------------------------"
rm -rf ../bin/simulation/balanced
rm -rf ../bin/simulation/unbalanced
rm -rf ../bin/simulation/phold-logs
mkdir -p ../bin/simulation/balanced
mkdir -p ../bin/simulation/unbalanced
mkdir -p ../bin/simulation/phold-logs

for run in $(seq 1 $runs); do
  for model in "${models[@]}"; do
    for lookahead in "${lookaheads[@]}"; do
      for alpha in "${alphas[@]}"; do
        for object in "${objects[@]}"; do
          for q in "${queue[@]}"; do
          for hp in "${hotspots[@]}"; do
            for hl in "${h_loads[@]}"; do
 #           if [[ "$q" == "ORIGIN" && "$alpha" != "1.0" ]]; then
 #            continue
 #           fi 

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
                config="${model}-${q}${NUMA}${BALANCE}-APH_${alpha}-LAH_${lookahead}-OBJ_${object}-hotspots_${hp}-loads_${hl}"
                echo "Running configuration: $config"
                echo "$lines"
                make "$model" NUM_BARRIER=64 QUEUE_IMPL="$q" NUMA="$n" ALPHA="$alpha" LOOKAHEAD="$lookahead" OBJECTS="$object" UNBALANCE="$b" HL="$hl" HP="$hp" 2>> stderr.log

                bin_path="../bin/simulation/${balance}/PARSIR-simulator-${config}"
                mv ../bin/PARSIR-simulator-${model}-${q}${NUMA}${BALANCE} ${bin_path}
                echo "$lines"
                echo "Simulation for $config"
                ${bin_path} | grep 'Barrier measures\|throughput' >../bin/simulation/phold-logs/${run}_${config}.log
                #echo "$config" > ../bin/simulation/phold-logs/${run}_${config}.log

                echo "$lines"
                echo "Log file created at: ../bin/simulation/phold-logs/${run}_${config}.log"
                echo "$lines"
                    done
                done
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
unset hotspots
unset h_loads
