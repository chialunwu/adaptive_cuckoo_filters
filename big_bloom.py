from pybloom import BloomFilter
import sys


def padding_zero(s, length):
	if len(s) > length:
		return s[:length]
	elif len(s) == length:
		return s
	else:
		return s + (length-len(s))*"0"


def usage(argv0):
	print "Usage: %s (total insert) (total lookup) (memory budget)" % argv0


###########################################################
total_byte = 268416
total_items = 170000
item_len = 256
is_number = False

total_lookup = 5000000
rebuild_period = 100000

if len(sys.argv) != 5:
	usage(sys.argv[0])
	sys.exit()
else:
	total_items = int(sys.argv[1])
	total_lookup = int(sys.argv[2])
	total_byte = int(sys.argv[3])
	fname = sys.argv[4]
##########################################################
bf = BloomFilter(total_byte*8, total_items)

print "Theory error rate: %f %%" % (100*bf.theory_error_rate())
print "Filter size: %d bytes" % (bf.num_bits/8)
print "Avg. bits per item: %f" % (float(bf.num_bits)/total_items)

mapping_table = {}
num_inserted = 0
total_queries = 0
false_positives = 0
true_negative = 0

print "=========================================="
with open(fname, 'r') as f:
	for l in f:
		l = l.strip().split(' ')
		req_type = l[0]
		l = l[1].split(':')		

		if req_type == "BoscI:" and len(l) >= 3:
			record = l[0]+l[2]
			if record not in mapping_table and num_inserted < total_items:
				if not is_number:
					bf.add(padding_zero(record, item_len))
				else:
					bf.add(int(record))
				mapping_table[record] = 1
				num_inserted += 1
		elif req_type == "BoscS:[An]" and len(l) >= 2 and total_queries < total_lookup:
			record = l[0]+l[1]
			true_negative_flag = record not in mapping_table
			total_queries += 1

			if not is_number:
				status = padding_zero(record, item_len) in bf
			else:
				status = int(record) in bf

			if status is True and true_negative_flag:
				false_positives += 1
			elif status is False and true_negative_flag:
				true_negative += 1

			if total_queries % rebuild_period == 0:
				print "fpp (%%): %f" % (100*(float(false_positives)/(false_positives+true_negative)))

print "\nFPP (%%): %f" % (100*(float(false_positives)/(false_positives+true_negative)))
print "=========================================="

for e in mapping_table:
	if not is_number:
		assert padding_zero(e, item_len) in bf
	else:
		assert int(e) in bf


print "Filter size: %d bytes" % (bf.num_bits/8)
print "Total queries: %d" % total_queries
print "Num inserted: %d" % num_inserted
print "False positive: %d" % false_positives
print "True negative: %d" % true_negative

