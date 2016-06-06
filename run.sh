TOTAL_INSERT=168093
TOTAL_LOOKUP=2000000

MEMORY_BUDGET=176940
#MEMORY_BUDGET=180000

#MEMORY_BUDGET=265410
#MEMORY_BUDGET=270000

WORKLOAD="../zipf-1.2.log"
OUTPUT="output"

./test_big_cf $TOTAL_INSERT $TOTAL_LOOKUP $MEMORY_BUDGET $WORKLOAD > $OUTPUT/cf.result
echo "test_big_cf done."
./test_split_cf_adaptive $TOTAL_INSERT $TOTAL_LOOKUP $MEMORY_BUDGET $WORKLOAD > $OUTPUT/acf.result
echo "test_split_cf_adaptive done."
python big_bloom.py $TOTAL_INSERT $TOTAL_LOOKUP $MEMORY_BUDGET $WORKLOAD > $OUTPUT/bf.result
echo "big_bloom done."
python split_bloom.py $TOTAL_INSERT $TOTAL_LOOKUP $MEMORY_BUDGET $WORKLOAD > $OUTPUT/sbf.result
echo "split_bloom done."

