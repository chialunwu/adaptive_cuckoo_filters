/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _ADAPTIVE_CUCKOO_FILTERS_H_
#define _ADAPTIVE_CUCKOO_FILTERS_H_

#include "cuckoofilter.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <typeinfo>

#include <sys/time.h>
#include <unistd.h>


using cuckoofilter::CuckooFilter;
using namespace::std;

namespace adaptive_cuckoofilters {
    enum Status {
		Found = 0,
		NotFound = 1,
		NoFilter = 2,
        Nothing = 3,
		NeedRebuild = 4
    };

	enum NCType {
		NUMERIC = 10,
		STRING = 11
	};

	// nc: Negative cache - small hash table
	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
    class AdaptiveCuckooFilters {
		// Storage of items
		CuckooFilter<ItemType, min_bits_per_tag> **filter;
		CuckooFilter<ItemType, min_bits_per_tag> *dummy_filter;

		size_t item_bytes;

		// True negative cache
		NCType nc[nc_buckets];
		size_t nc_type;

		// Statistics counters
		size_t rebuild_time[max_filters];
		size_t fpp[max_filters];

		// Temp variables
		size_t index, raw_index;
		uint32_t tag, hash1;

		inline bool CompareNC(size_t idx, const ItemType& item) {
			if(nc_type == STRING) {
				string s(item);
				return nc[idx].compare(s) == 0;
			}else{
				return item == nc[idx];	
			}
		}

		inline void InsertNC(size_t idx, const ItemType& item) {
			nc[idx] = item;
		}

public:
		explicit AdaptiveCuckooFilters(size_t b) {
			filter = new CuckooFilter<ItemType, min_bits_per_tag>*[max_filters];
			dummy_filter = new CuckooFilter<ItemType, min_bits_per_tag>(single_cf_size);

			NCType s;
			if(typeid(s) == typeid(string))
				nc_type = STRING;
			else
				nc_type = NUMERIC;

			item_bytes = b;
		}

		~AdaptiveCuckooFilters() {
			for(size_t i=0; i < max_filters; i++) {
				delete filter[i];
			}
			delete filter;
		}
		
		bool Add(const ItemType& item);
		bool Delete(const ItemType& item);

		Status Lookup(const ItemType& item, size_t *status, uint32_t *hash1, size_t *r_index, size_t *nc_hash);
		Status AdaptToFalsePositive(const ItemType& item, const size_t status, const uint32_t hash1, const size_t r_index, const size_t nc_hash);
		bool GrowFilter(const uint32_t idx, vector<ItemType>& keys);
		bool ShrinkFilter(const uint32_t idx, vector<ItemType>& keys);

		size_t SizeInBytes() {
			// TODO: need real size
			size_t bytes = 0;
			for(size_t i=0; i< max_filters; i++){
				if(filter[i])
					bytes += filter[i]->SizeInBytes();
			}
			return bytes;
		}

		uint32_t GetVictimFilter() {
		    size_t min=100000;
		    uint32_t idx;
		    for(uint32_t i=0;i<max_filters;i++){
				if(fpp[i] < min && filter[i]) {
					min = fpp[i];
					idx = i;
				}
		    }
			return idx;
		}
	};

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, max_filters, single_cf_size, min_bits_per_tag>::Add(
			const ItemType& item) {
		dummy_filter->GenerateIndexTagHash(item, item_bytes, nc_type==STRING, &raw_index, &index, &tag);
		hash1 = raw_index % max_filters;

		// Instantiate the filter if the filter[hash1] is NULL		
		if(!filter[hash1]){
			filter[hash1] = new CuckooFilter<ItemType, min_bits_per_tag> (single_cf_size);
		}
		
		// Add the item
		if (filter[hash1]->Add(index, tag) != cuckoofilter::Ok) {
			return false;
		} else {
			return true;
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, max_filters, single_cf_size, min_bits_per_tag>::Lookup(
			const ItemType& item, size_t *status, uint32_t *hash1, size_t *r_index, size_t *nc_hash) {
		dummy_filter->GenerateIndexTagHash(item, item_bytes, nc_type==STRING, &raw_index, &index, &tag);
		*hash1 = raw_index % max_filters;
		*nc_hash = raw_index % nc_buckets;

		if (filter[*hash1]) {
			*status = filter[*hash1]->Contain(index, tag, r_index);
			if (*status == cuckoofilter::Ok ||
				(*status == cuckoofilter::NotSure && !CompareNC(*nc_hash, item))){
				return Found;
			}else{
				return NotFound;
			}
		}else{
			return NoFilter;
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, max_filters, single_cf_size, min_bits_per_tag>::Delete(
			const ItemType& item) {
		return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, max_filters, single_cf_size, min_bits_per_tag>::
		AdaptToFalsePositive(const ItemType& item, const size_t status, const uint32_t hash1, const size_t r_index, const size_t nc_hash) {
		if (filter[hash1]) {
			fpp[hash1]++;

			if (status == cuckoofilter::Ok) {
				filter[hash1]->AdaptFalsePositive(r_index);
				InsertNC(nc_hash, item);	// Not Sure if this is needed
			} else if (status == cuckoofilter::NotSure) {
				InsertNC(nc_hash, item);
		
				//TODO: the condition to trigger rebuild
				if(fpp[hash1] > 10) {
					return NeedRebuild;
				}
			}
		}
		return Nothing;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, max_filters, single_cf_size, min_bits_per_tag>::GrowFilter(const uint32_t idx, vector<ItemType>& keys) {
	    size_t index, raw_index;
    	uint32_t tag;
		
		delete filter[idx];
		filter[idx] = (CuckooFilter<ItemType, min_bits_per_tag> *)(new CuckooFilter<ItemType, min_bits_per_tag> (single_cf_size*(rebuild_time[idx]+2)));
		for(size_t i=0; i< keys.size(); i++){
			dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
//			cout << "Add: " << index << "/" << tag << endl;
			if (filter[idx]->Add(index, tag) != cuckoofilter::Ok) {
				 cout << "Fail" << endl;
				 return false;
			}
		}
		fpp[idx] = 0;
		rebuild_time[idx]++;
		return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t max_filters, 
			  size_t single_cf_size,
			  size_t min_bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, max_filters, single_cf_size, min_bits_per_tag>::
		ShrinkFilter(const uint32_t idx, vector<ItemType>& keys) {
	    size_t index, raw_index;
    	uint32_t tag;

		delete filter[idx];
		filter[idx] = NULL;
		if(rebuild_time[idx] > 0){
			filter[idx] = (CuckooFilter<ItemType, min_bits_per_tag> *)(new CuckooFilter<ItemType, min_bits_per_tag> (single_cf_size*(rebuild_time[idx])));
			for(size_t i=0; i< keys.size(); i++){
				dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
				if (filter[idx]->Add(index, tag) != cuckoofilter::Ok) {
					 cout << "Fail" << endl;
					 return false;
				}
			}
			fpp[idx] = 0;
			rebuild_time[idx]--;
			return true;
		}
	}
}

#endif
