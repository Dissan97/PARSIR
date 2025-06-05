rm -rf ../bin/balanced
rm -rf ../bin/unbalanced
mkdir -p ../bin/balanced
mkdir -p ../bin/unbalanced
echo "Building all files for 40 cores configuration"
# PCS balanced
make pcs THREADS=40 #ORGINAL
make pcs QUEUE_IMPL=LPTF THREADS=40 #LPTF
make pcs NUMA=1 THREADS=40 # ORGINAL-NUMA
make pcs QUEUE_IMPL=LPTF NUMA=1 THREADS=40 #LPTF-NUMA
#PCS unblanced
make pcs THREADS=40 UNBALANCE=1 #ORGINAL
make pcs QUEUE_IMPL=LPTF THREADS=40 UNBALANCE=1 #LPTF
make pcs NUMA=1 THREADS=40 UNBALANCE=1 # ORGINAL-NUMA
make pcs QUEUE_IMPL=LPTF NUMA=1 THREADS=40 UNBALANCE=1 #LPTF-NUMA
# HIGHWAY
make highway THREADS=40 #ORGINAL
make highway QUEUE_IMPL=LPTF THREADS=40 #LPTF
make highway NUMA=1 THREADS=40 # ORGINAL-NUMA
make highway QUEUE_IMPL=LPTF NUMA=1 THREADS=40 #LPTF-NUMA
# HIGHWAY unblanced
make highway THREADS=40 UNBALANCE=1 #ORGINAL
make highway QUEUE_IMPL=LPTF THREADS=40 UNBALANCE=1 #LPTF
make highway NUMA=1 THREADS=40 UNBALANCE=1 # ORGINAL-NUMA
make highway QUEUE_IMPL=LPTF NUMA=1 THREADS=40 UNBALANCE=1 #LPTF-NUMA

mv ../bin/*UNBALANCE ../bin/unbalanced
mv ../bin/*BALANCE ../bin/balanced
