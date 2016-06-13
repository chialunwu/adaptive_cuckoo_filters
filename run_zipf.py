import subprocess as sp
import re
import sys
import os

def get_filter_size(fname):
	with open(fname, 'r') as f:
		p = "Filter size: (\d+) bytes"
		return int(re.search(p, f.read()).group(1))


bits_per_tag = int(sys.argv[1])
num_experiment = int(sys.argv[2])

num_insert = 200000
num_lookup = 1000000
output_dir = "output"

pattern = "./%s %d %d %d %s > %s"
for i in range(1, 10):
	for j in range(num_experiment):
		print "Start loop %d..." % j
		workload = "../zipf-1.%d-%d.log" % (i, j)
		print workload
		# Run ACF
		program, acn = "test_split_cf_adaptive_%d" % (bits_per_tag), "acf"
		result = "%s/%s_%d-%d-%d.result" % (output_dir, acn, bits_per_tag, i, j)
		sp.call(pattern % (program, num_insert, num_lookup, 0, workload, result), shell=True)
		print "\tacf done."
		mem_budget = get_filter_size(result)
		# Run CF
		program, acn = "test_big_cf_%d" % (bits_per_tag), "cf"
		result = "%s/%s_%d-%d-%d.result" % (output_dir, acn, bits_per_tag, i, j)
		sp.call(pattern % (program, num_insert, num_lookup, mem_budget, workload, result), shell=True)
		print "\tcf done."
		# Run SBF
		program, acn = "split_bloom.py", "sbf"
		result = "%s/%s_%d-%d-%d.result" % (output_dir, acn, bits_per_tag, i, j)
		sp.call(pattern % (program, num_insert, num_lookup, mem_budget, workload, result), shell=True)
		print "\tsbf done."
		# Run BF
		program, acn = "big_bloom.py", "bf"
		result = "%s/%s_%d-%d-%d.result" % (output_dir, acn, bits_per_tag, i, j)
		sp.call(pattern % (program, num_insert, num_lookup, mem_budget, workload, result), shell=True)
		print "\tbf done."

