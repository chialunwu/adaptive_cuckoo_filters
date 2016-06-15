import subprocess as sp
import sys


if len(sys.argv) > 1:
	br = [int(sys.argv[1])]
else:
	br = [4, 8, 12, 16, 20, 24, 28, 32]

# Compile the programs
for bits in br:
	# CF
	sp.call("sed -i 's/const size_t bits_per_tag = .*/const size_t bits_per_tag = %d;/g' example/test_big_cf.cc" % bits, shell=True)
	sp.call("make test_big_cf", shell=True)
	sp.call("mv test_big_cf test_big_cf_%d" % (bits), shell=True)
	
	sp.call("sed -i 's/size_t sht_max_buckets = .*/size_t sht_max_buckets = %d;/g' example/test_big_cf.cc" % 10, shell=True)
	sp.call("make test_big_cf", shell=True)
	sp.call("mv test_big_cf test_big_cf_%d_nc" % (bits), shell=True)
	sp.call("sed -i 's/size_t sht_max_buckets = .*/size_t sht_max_buckets = %d;/g' example/test_big_cf.cc" % 0, shell=True)

	#ACF
	sp.call("sed -i 's/const size_t bits_per_tag = .*/const size_t bits_per_tag = %d;/g' example/test_split_cf_adaptive.cc" % bits, shell=True)
	sp.call("make test_split_cf_adaptive", shell=True)
	sp.call("mv test_split_cf_adaptive test_split_cf_adaptive_%d" % (bits), shell=True)

	sp.call("sed -i 's/InsertNC(nc_hash/\/\/InsertNC(nc_hash/g' src/adaptive_cuckoofilter.h", shell=True)
	sp.call("make test_split_cf_adaptive", shell=True)
	sp.call("mv test_split_cf_adaptive test_split_cf_adaptive_%d_no_nc" % (bits), shell=True)
	sp.call("sed -i 's/\/\/InsertNC(nc_hash/InsertNC(nc_hash/g' src/adaptive_cuckoofilter.h", shell=True)


