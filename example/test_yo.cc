/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "cuckoofilter.h"
#include "hash_functions.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>

#include <sys/time.h>
#include <unistd.h>

using cuckoofilter::CuckooFilter;
using namespace std;

int main(int argc, char** argv) {
    size_t total_items  = 12345678;
    size_t sht_max_buckets = 0;

    // Timing
    struct  timeval start;
    struct  timeval end;
    unsigned  long insert_t=0, lookup_t=0;

    uint32_t hash1=0;

    // Create a cuckoo filter where each item is of type size_t and
    // use 12 bits for each item:
    //    CuckooFilter<size_t, 12> filter(total_items);
    // To enable semi-sorting, define the storage of cuckoo filter to be
    // PackedTable, accepting keys of size_t type and making 13 bits
    // for each key:
    //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);

    CuckooFilter<char*, 12> filter(total_items);
	
	size_t index, raw_index;
	uint32_t tag;

	int i1 = 123;
	int i2 = 123;
	string s1("123");
	string s2("124");

	filter.GenerateIndexTagHash("123", 3, true, &raw_index, &index, &tag);
	filter.Add(index, tag);
	cout << index << '/' << tag << endl;
	filter.GenerateIndexTagHash("123", 3, true, &raw_index, &index, &tag);
	cout << index << '/' << tag << endl;
	assert(filter.Contain(index, tag, &raw_index) == cuckoofilter::Ok);

    return 0;
 }
