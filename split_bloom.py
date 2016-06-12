#!/usr/bin/env python
from pybloom import BloomFilter
import sys
import math
import mmh3


def padding_zero(s, length):
	if len(s) > length:
		return s[:length]
	elif len(s) == length:
		return s
	else:
		return s + (length-len(s))*"0"


def usage(argv0):
	print "Usage: %s (total insert) (total lookup) memory budget)" % argv0

	
class SplitBloomFilter:
	def __init__(self, num_filter, bits, total_items):
		self.num_insert = [0]*num_filter
		self.lookup = [0]*num_filter
		self.total_lookup = 0
		self.fpp = [0]*num_filter
		self.total_fpp = 0

		self.num_filter = num_filter
		self.total_bits = bits
		self.total_items = total_items

		# Save the original keys for rebuild
		self.mapping_table = [[] for i in range(num_filter)]

		# Initiate bloom filters with equal size
		sm = bits/num_filter
		sn = total_items/num_filter
		self.bf = [BloomFilter(sm, sn) for i in range(num_filter)]

		self.target_fpp = [b.target_fpp for b in self.bf]
		self.old_fr = [1.0/num_filter]*num_filter

	def _global_optimal(self, l, m, key, f):
		n = sum(key)
		om = [0]*l	# number of bits per filter
		v1 = 0.0
		for i in range(l):
			tf = f[i] if f[i] > 0 else 1
			v1 += key[i]*(math.log(float(key[i]))-math.log(float(tf)))
		v1 /= n
		ln2 = 1.0/pow(math.log(2.0), 2)
		for i in range(l):
			tf = f[i] if f[i] > 0 else 1
			v2 = math.log(float(tf)) - math.log(float(key[i])) + v1
			if False and v2 < 0:
				# WTF?! The original algorithm has error?
				om[i] = 0
			else:
				om[i] = math.ceil((ln2*v2+(float(m)/n))*key[i])
		return om

	def _inc_optimal(self, i):
		f = self.total_lookup
		fi = self.lookup[i]
		Fri = self.old_fr[i]
		FPti = self.target_fpp[i]
		ni = self.num_insert[i]
		
		ln2 = 1.0/pow(math.log(2.0), 2)
		new_fp = (float(f)/fi)*Fri*FPti
		if new_fp > 1:
			new_fp = 1
			mi = 0
		else:
			mi = -ni*ln2*math.log(new_fp)

		self.target_fpp[i] = new_fp
		self.old_fr[i] = float(fi)/f
		return mi

	def global_rebuild(self):
		#print "Lookup"
		#for e in sbf.lookup:
		#	print e

		opt_m = self._global_optimal(self.num_filter, self.total_bits, self.num_insert, self.lookup)
		self.bf = []
		for i, m in enumerate(opt_m):
			if m > 0:
				self.bf.append(BloomFilter(int(m), len(self.mapping_table[i])))
			else:
				self.bf.append(None)

		# Clear counters or not
		#self.lookup = [0]*self.num_filter

		# Re-insert all keys into the sbf
		for i, m in enumerate(self.mapping_table):
			for n in m:
				if self.bf[i] != None:
					if not is_number:
						self.bf[i].add(padding_zero(n, item_len))
					else:
						self.bf[i].add(int(n))

		self.target_fpp = [b.target_fpp if b is not None else 1.0 for b in self.bf]
		self.old_fr = [float(l)/total_lookup for l in self.lookup]

	def inc_rebuild(self, idx):
		opt_m = self._inc_optimal(idx)
		#print "inc_rebuild: %d" % opt_m
		if opt_m > 0:
			self.bf[idx] = BloomFilter(int(opt_m), len(self.mapping_table[idx]))
		else:
			self.bf[idx] = None

		if self.bf[idx] != None:
			# Re-insert all keys into the bf
			for e in self.mapping_table[idx]:
				if not is_number:
					self.bf[idx].add(padding_zero(e, item_len))
				else:
					self.bf[idx].add(int(e))	
		
	def add(self, item):
		idx = mmh3.hash(str(item)) % self.num_filter

		self.num_insert[idx] += 1
		self.mapping_table[idx].append(item)
		if not is_number:
			self.bf[idx].add(padding_zero(item, item_len))
		else:
			self.bf[idx].add(int(item))
		#print "filter %d: capacity %d, keys %d" % (idx, self.sbf[idx].capacity, self.sbf[idx].count)

	def look_up(self, item, true_negative_flag):
		idx = mmh3.hash(str(item)) % self.num_filter
		self.lookup[idx] += 1
		self.total_lookup += 1
		if self.bf[idx] != None:
			if not is_number:
				rtv = padding_zero(item, item_len) in self.bf[idx]
			else:
				rtv = int(item) in self.bf[idx]
		else:
			rtv = True

		if rtv is True and true_negative_flag:
			self.fpp[idx] += 1
			self.total_fpp += 1
		return rtv

	def size_in_bytes(self):
		sum = 0
		for b in self.bf:
			if b != None:
				sum += b.num_bits
		return sum/8

#############################################################################
total_bytes = 270000
total_items = 170000
num_filters = 4099
total_lookup = 5000000

item_len = 256
is_number = False

rebuild_period = 10000
need_global_rebuild = True
need_inc_rebuild = False
inc_rebuild_count = 0

if len(sys.argv) != 5:
	usage(sys.argv[0])
	sys.exit()
else:
	total_items = int(sys.argv[1])
	total_lookup = int(sys.argv[2])
	total_bytes = int(sys.argv[3])
	fname = sys.argv[4]
##############################################################################
sbf = SplitBloomFilter(num_filters, total_bytes*8, total_items)

print "Initial filter size: %d bytes" % (sbf.size_in_bytes())
print "Avg. bits per item: %f" % (float(sbf.size_in_bytes())*8/total_items)

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
				sbf.add(record)
				assert sbf.look_up(record, False) is True
				mapping_table[record] = 1
				num_inserted += 1
		elif req_type == "BoscS:[An]" and len(l) >= 2 and total_queries < total_lookup:
			record = l[0]+l[1]
			true_negative_flag = record not in mapping_table
			total_queries += 1

			if need_global_rebuild and total_queries % rebuild_period == 0:
				sbf.global_rebuild()
				#print "Global rebuild: %d bytes" % sbf.size_in_bytes()

			if need_inc_rebuild and total_queries % rebuild_period == 0:
				for i in range(sbf.num_filter):
					fp = float(sbf.fpp[i])/sbf.lookup[i] 
					bound = 0.01
					if abs(fp - sbf.target_fpp[i]) > bound:
						sbf.inc_rebuild(i)
						inc_rebuild_count += 1

			if total_queries % rebuild_period == 0:
				print "fpp (%%): %f, mem: %d" % (100*(float(false_positives)/(false_positives+true_negative)), sbf.size_in_bytes())

			status = sbf.look_up(record, true_negative_flag)
			if status is True and true_negative_flag:
				false_positives += 1
			elif status is False and true_negative_flag:
				true_negative += 1

print "\nFPP (%%): %f" % (100*(float(false_positives)/(false_positives+true_negative)))
print "=========================================="
print "Filter size: %d bytes" % sbf.size_in_bytes()
print "Total queries: %d" % total_queries
print "Num inserted: %d" % num_inserted
print "False positive: %d" % false_positives
print "True negative: %d" % true_negative
print "inc_rebuild_count: %d" % inc_rebuild_count

for e in mapping_table:
	assert sbf.look_up(e, False) is True
print "Pass integrity test"


