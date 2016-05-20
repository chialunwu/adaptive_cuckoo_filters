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

#define STRING_2

using namespace::cuckoofilter;
using namespace::std;

namespace adaptive_cuckoofilters {
    enum Status {
		Found = 0,
		NotFound = 1,
		NoFilter = 2,
        Nothing = 3,
		NeedRebuild = 4,
		NeedShrink = 5
    };

	enum NCType {
		NUMERIC = 10,
		STRING = 11
	};

	// nc: Negative cache - small hash table
	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
    class AdaptiveCuckooFilters {
		// Storage of items
		CuckooFilterInterface **filter;
		CuckooFilter<ItemType, bits_per_tag> *dummy_filter;

		size_t item_bytes;
		size_t max_filters;
		size_t single_cf_size;

		// True negative cache
		NCType nc[nc_buckets];
		size_t nc_type;

		// Statistics counters
		size_t *rebuild_time;
		size_t *fpp;
		size_t *lookup;
		size_t *lookup_old;
		size_t *insert_keys;
		size_t *insert_keys_old;

		size_t overall_fpp, all_lookup, all_lookup_old;
		size_t  num_grow, num_shrink, true_neg;

		// Memory
		size_t cur_mem;
		size_t mem_budget;

		// Temp variables
		size_t raw_index, t_nc_hash, t_status;
		uint32_t index, tag, hash1;
		
		// Prevent the just growed filter from shrink
		uint32_t pinned_filter;

		inline bool CompareNC(size_t idx, const ItemType& item) {
#ifdef STRING_2
			string s(item);
			return nc[idx].compare(s) == 0;
#else
			return item == nc[idx];	
#endif
		}

		inline void InsertNC(size_t idx, const ItemType& item) {
			// TODO: Won't this cuase memory leak?
			nc[idx] = item;
		}

		inline void ClearNC(size_t idx) {
#ifdef STRING_2
			nc[idx].clear();
#else
			nc[idx] = 0;
#endif
		}

		vector<string> &split(const string &s, char delim, vector<string> &elems) {
			stringstream ss(s);
			string item;
			while (getline(ss, item, delim)) {
				elems.push_back(item);
			}
			return elems;
		}


		vector<string> split(const string &s, char delim) {
			vector<string> elems;
			split(s, delim, elems);
			return elems;
		}

public:
		explicit AdaptiveCuckooFilters(size_t b, size_t f, size_t c, size_t mem):overall_fpp(0), cur_mem(0), num_grow(0), num_shrink(0), true_neg(0), all_lookup(0), all_lookup_old(0) {
			max_filters = f;
			single_cf_size = c;
			item_bytes = b;

			filter = new CuckooFilterInterface *[max_filters];
			dummy_filter = new CuckooFilter<ItemType, bits_per_tag>(4, false);

			rebuild_time = new size_t[max_filters];
			fpp = new size_t[max_filters];
			lookup = new size_t[max_filters];
			lookup_old = new size_t[max_filters];	
			insert_keys = new size_t[max_filters];
			insert_keys_old = new size_t[max_filters];


			for(size_t i=0; i<max_filters; i++){
				fpp[i] = 0;
				lookup[i] = 0;
				lookup_old[i] = 0;
				insert_keys[i] = 0;
				insert_keys_old[i] = 0;
				rebuild_time[i] = 0;
				filter[i] = new CuckooFilter<ItemType, bits_per_tag> (single_cf_size, false);
				cur_mem += filter[i]->SizeInBytes();
			}

/*			cout << SizeInBytes() << endl;
			for(size_t i=0; i<max_filters; i++){
				delete filter[i];
				filter[i] = new CuckooFilter<ItemType, 4> (single_cf_size);
			}
			cout << SizeInBytes() << endl;*/
			// End Test shrink
			NCType s;
			if(typeid(s) == typeid(string))
				nc_type = STRING;
			else
				nc_type = NUMERIC;

			mem_budget = mem - FixedSizeInBytes();
		}

		~AdaptiveCuckooFilters() {
			for(size_t i=0; i < max_filters; i++) {
				delete filter[i];
			}
			delete filter;
		}
		
		bool Add(const ItemType& item, uint32_t *hash1);
		bool Delete(const ItemType& item);

		Status Lookup(const ItemType& item, size_t *status, uint32_t *hash1, size_t *r_index, size_t *nc_hash);
		Status AdaptToFalsePositive(const ItemType& item, const size_t status, const uint32_t hash1, const size_t r_index, const size_t nc_hash);

		Status GrowFilter(const uint32_t idx, vector<ItemType>& keys, bool grow_bucket);
		bool ShrinkFilter(uint32_t idx, vector<ItemType>& keys);

		// This should be better
		void DumpStats() const;
		bool LoadStatsToOptimize();
		// This should be worser
		void DumpFilter() const;
		bool LoadFilter();


		void Info();

		size_t FixedSizeInBytes() {
			// Pointers + stats + negative cache
			return ((4*max_filters)*sizeof(size_t)) + (nc_buckets * item_bytes);
//			return ((max_filters)*sizeof(size_t)) + (nc_buckets * item_bytes);
//			return nc_buckets * item_bytes;
//			return 0;
		}

		size_t FilterSizeInBytes() {
			// TODO: need real size
			size_t bytes = 0;
			for(size_t i=0; i< max_filters; i++){
				if(filter[i] != NULL)
					bytes += filter[i]->SizeInBytes();
			}
			assert(bytes == cur_mem);
			return bytes;
		}

		uint32_t GetVictimFilter() {
			//TODO: Choose the right victim
			// Choose the least lookuped one
		    int min=-1;
		    uint32_t idx=-1;

//cout << "GetVictim:\n";
			// Shrink the previous growed filter first
		    for(uint32_t i=0;i<max_filters;i++){
//cout << i << ". " << filter[i]->num_buckets << " " << filter[i]->fingerprint_size << " " << filter[i]->num_items << " " << lookup[i] << " " << rebuild_time[i] << endl;

				if(pinned_filter != i && filter[i] != NULL && filter[i]->fingerprint_size != 4 &&
					(min == -1 || (int)lookup[i] < min) && rebuild_time[i] > 0) {
					min = lookup[i];
					idx = i;
				}
		    }

			// Seems better
			if(min == -1) {
				for(uint32_t i=0;i<max_filters;i++){
					if(filter[i] != NULL && pinned_filter != i && filter[i]->fingerprint_size != 4 &&
						(min == -1 || (int)lookup[i] < min)) {
						min = lookup[i];
						idx = i;
					}
				}
			}

			if(min == -1) {
				for(uint32_t i=0;i<max_filters;i++){
//					cout << filter[i]->num_buckets << '/' << filter[i]->fingerprint_size << endl;
					if(filter[i] != NULL && pinned_filter != i &&
						(min == -1 || (int)lookup[i] < min)) {
						min = lookup[i];
						idx = i;
					}
				}
			}
//cout << "Choose " << idx << endl;
			pinned_filter = -1;
			assert(min != -1);
			return idx;
		}

		size_t OptimalFilterSize(uint32_t idx) {
			size_t new_size = 0;
			if(all_lookup_old != 0) {
				new_size = (float)2*insert_keys[idx]/pow(2, filter[idx]->fingerprint_size)*
						(float)lookup[idx]/all_lookup*
						(float)all_lookup_old/lookup_old[idx]*
						filter[idx]->num_buckets*pow(2, filter[idx]->fingerprint_size)/(2*insert_keys_old[idx]);
			}
			insert_keys_old[idx] = insert_keys[idx];
			lookup_old[idx] = lookup[idx];
			all_lookup_old = all_lookup;
		
			return new_size;
		}
	};

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::Add(
			const ItemType& item, uint32_t *hash1) {
		dummy_filter->GenerateIndexTagHash(item, item_bytes, nc_type==STRING, &raw_index, &index, &tag);
		*hash1 = raw_index % max_filters;
		t_nc_hash = raw_index % nc_buckets;

		// Instantiate the filter if the filter[hash1] is NULL, need to read keys from disk	
		if(filter[*hash1] == NULL){
			return false;
		} else {
			// Add the item
			t_status = filter[*hash1]->Add(index, tag);
//cout<<"Add: "<<index<<"/"<<tag<<endl;

			if (t_status == cuckoofilter::Ok) {
				insert_keys[*hash1]++;
				return true;
			} else if(t_status == cuckoofilter::MayClearNC) {
				insert_keys[*hash1]++;
				if(CompareNC(t_nc_hash, item)) {
					//cout << "Clear "<<item<<endl;
					ClearNC(t_nc_hash);
				}
				return true;
			} else {
				return false;
			}
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::Lookup(
			const ItemType& item, size_t *status, uint32_t *hash1, size_t *r_index, size_t *nc_hash) {
//TODO: change the pointer to reference!!
		dummy_filter->GenerateIndexTagHash(item, item_bytes, nc_type==STRING, &raw_index, &index, &tag);
		*hash1 = raw_index % max_filters;
		*nc_hash = raw_index % nc_buckets;

		if (filter[*hash1] != NULL) {
			lookup[*hash1]++;
			all_lookup++;
			*status = filter[*hash1]->Contain(index, tag, r_index);
//cout<<"Lookup: "<<index<<"/"<<tag<<" "<<*status<<endl;
			if (*status == cuckoofilter::Ok ||
				(*status == cuckoofilter::NotSure && !CompareNC(*nc_hash, item))){
				return Found;
			}else{
				true_neg++;
				return NotFound;
			}
		}else{
			// If no filter, always refurn found
			return Found;
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::Delete(
			const ItemType& item) {
		return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		AdaptToFalsePositive(const ItemType& item, const size_t status, const uint32_t hash1, const size_t r_index, const size_t nc_hash) {
		fpp[hash1]++;
		overall_fpp++;

		if (filter[hash1] != NULL) {
			if (status == cuckoofilter::Ok) {
				filter[hash1]->AdaptFalsePositive(r_index);
				InsertNC(nc_hash, item);	// Not Sure if this is needed
			} else if (status == cuckoofilter::NotSure) {
				InsertNC(nc_hash, item);
			}
		}

		//TODO: when to trigger rebuild
		if(fpp[hash1] > 30) {
			return NeedRebuild;
		}
		return Nothing;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::GrowFilter(const uint32_t idx, vector<ItemType>& keys, bool grow_bucket) {
		size_t ori_mem, new_mem, new_size;
		size_t fingerprint_size = bits_per_tag;

		bool force = false;

		pinned_filter = idx;
		num_grow ++;

		if(filter[idx] != NULL) {
			ori_mem = filter[idx]->SizeInBytes();
			do {
				rebuild_time[idx]++;
				//new_size = single_cf_size*pow(2, (rebuild_time[idx]));
				new_size = single_cf_size*(rebuild_time[idx]+1);
			}while(new_size < filter[idx]->num_items);

			if(!grow_bucket && filter[idx]->fingerprint_size+4 <= 16) {
				fingerprint_size = filter[idx]->fingerprint_size+4;
				force = true;
				new_size = filter[idx]->num_buckets;

				//Optimal size
				size_t optimal_new_size = OptimalFilterSize(idx);
				if(optimal_new_size > 0 && optimal_new_size > new_size){
//					fingerprint_size -= 4;
				//	cout << "Optimal Grow - " << new_size << "  " << optimal_new_size << endl;
//					new_size = optimal_new_size;
				}
			}

			delete filter[idx];
			filter[idx] = NULL;	
		} else {
			ori_mem = 0;
			new_size = single_cf_size;
		}

		switch(fingerprint_size){
			case 4:
				filter[idx] = new CuckooFilter<ItemType, 4> (new_size, force);
				break;
			case 8:
				filter[idx] = new CuckooFilter<ItemType, 8> (new_size, force);
				break;
			case 12:
				filter[idx] = new CuckooFilter<ItemType, 12> (new_size, force);
				break;
			case 16:
				filter[idx] = new CuckooFilter<ItemType, 16> (new_size, force);
				break;
			default:
				assert(false);
		}
		
		new_mem = filter[idx]->SizeInBytes();

		// Update statistics
		//cout<<"new_:"<<new_mem<<" ori_mem:"<<ori_mem<<endl;
		//cout<<"new_mem:"<<new_mem<<" ori_mem:"<<ori_mem<<endl;

		assert(new_mem >= ori_mem);

		cur_mem += (new_mem - ori_mem);
		fpp[idx] = 0;

//cout << "Info: Grow filter " << idx << " mem: " << cur_mem << " ,budget: " << mem_budget << endl;

		for(size_t i=0; i< keys.size(); i++){
			dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
			if (filter[idx]->Add(index, tag) != cuckoofilter::Ok) {
				// Shit, bad luck
				return NeedRebuild;
			}
		}
		
		if(cur_mem > mem_budget){
			return NeedShrink;
		} else {
			return Nothing;
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		ShrinkFilter(uint32_t idx, vector<ItemType>& keys) {
//cout << "Shrink Mem: " << cur_mem << endl;
		assert(filter[idx] != NULL);
		pinned_filter = idx;

		size_t ori_mem, new_mem=0;
		ori_mem = filter[idx]->SizeInBytes();

		size_t ori_fp_size = filter[idx]->fingerprint_size;
		size_t new_fp_size = bits_per_tag;

		//TODO:WTF
		//size_t new_size = single_cf_size*pow(2,(rebuild_time[idx]-1));
		//size_t new_size = single_cf_size*rebuild_time[idx];

		// If the filter is in it's minimal state, don't new it
		// If the capacity of the new filter is smaller than the keys size, don't new it
		//
		num_shrink++;
		new_fp_size = ori_fp_size - 4;
		// Just shrink the fingerprint, because this will not read disk
		bool force = true;
		size_t new_size = filter[idx]->num_buckets;


		if(new_fp_size < 4){
			new_fp_size = 4;


			size_t optimal_new_size = OptimalFilterSize(idx);

			if(optimal_new_size > 0 && optimal_new_size < new_size) {
				//new_size = optimal_new_size;
			}
		}

		delete filter[idx];
		filter[idx] = NULL;
		while(true) {
			switch(new_fp_size){
				case 4:
					filter[idx] = new CuckooFilter<ItemType, 4> (new_size, force);
					break;
				case 8:
					filter[idx] = new CuckooFilter<ItemType, 8> (new_size, force);
					break;
				case 12:
					filter[idx] = new CuckooFilter<ItemType, 12> (new_size, force);
					break;
				case 16:
					filter[idx] = new CuckooFilter<ItemType, 16> (new_size, force);
					break;
				default:
					assert(false);
			}
			new_mem = filter[idx]->SizeInBytes();
//cout << new_size << endl;
//cout << "ori_fp: "<<ori_fp_size<<" new_fp: "<<new_fp_size<<endl;
//cout << "new_mem: " << new_mem << " ori_mem: " << ori_mem << endl;
			assert(new_mem <= ori_mem);
			for(size_t i=0; i< keys.size(); i++){
				dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
				if (filter[idx]->Add(index, tag) != cuckoofilter::Ok) {
					 // Just delete the filter
					cout<< "Shrink fail\n";
					//assert(false);
					delete filter[idx];
					filter[idx] = NULL;
					new_mem = 0;
					break;
				}
			}

			if(filter[idx] != NULL) {
				break;
			} else {
				new_size /= 0.9;
			}
		}

		if(rebuild_time[idx] > 0)
			rebuild_time[idx]--;

		// Update stats
		fpp[idx] = 0;
		cur_mem -= (ori_mem - new_mem);

//cout << "Info: Shrink filter " << idx << " mem: " << cur_mem << " ,budget: " << mem_budget << endl;
//cout << "ori_mem: "<<ori_mem<<" new_mem: "<<new_mem<<endl;

//cout << "Shrink - cur_mem: " << cur_mem  << " budget: " << mem_budget << endl;


		if(cur_mem > mem_budget)
			return false;
		else
			return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	void
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		DumpStats() const {
			ofstream f("acf.stat");	
			if (f.is_open()) {
				for(size_t i=0; i<max_filters ;i++){
					f << lookup[i] << ' ' << insert_keys[i] << ' ' <<fpp[i] << endl;
				}
			}
			f.close();
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		LoadStatsToOptimize() {
			size_t mem_budget_bytes = mem_budget; 
			ifstream f("acf.stat");
			string line;
			size_t i=0, b;
			size_t *l = new size_t[max_filters];
			size_t *n = new size_t[max_filters];
			vector<string> tv;
			if (f.is_open()) {
				while (getline (f, line) ) {
					tv = split(line, ' ');
					l[i] = atoi(tv[0].c_str());
					n[i] = atoi(tv[1].c_str());			
					i++;
				}
			}else{
				return false;
			}
			f.close();
			
			double z1=0;
			for(i=0; i<max_filters; i++)
				z1 += sqrt(l[i]*n[i]);
			z1 = pow(((double)(4*bits_per_tag*z1)/(mem_budget_bytes*8)), 2);

			cur_mem = 0;
			size_t sum = 0;
			for(i=0 ;i<max_filters; i++) {
				if(filter[i] != NULL) {
					delete filter[i];
					filter[i] = NULL;
				}

				b = (size_t)sqrt(l[i]*n[i]/z1);
				//cout << b << endl;
				//b = b < 25?25:b;
				sum += b*4*bits_per_tag/8;
				filter[i] = new CuckooFilter<ItemType, bits_per_tag> (b, true);
//				sum += filter[i]->SizeInBytes();
				cur_mem += filter[i]->SizeInBytes();
			}	
			cout << "Theory: " << sum << " Real: " << cur_mem << endl;		
			cout << "Load Done." << endl;
			return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	void
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		DumpFilter() const {
			ofstream f("acf.filter");	
			if (f.is_open()) {
				for(size_t i=0; i<max_filters ;i++){
					if(filter[i] != NULL)
						f << filter[i]->num_buckets << " " << filter[i]->fingerprint_size << endl;
					else
						f << "0 0" << endl;
				}
			}
			f.close();
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		LoadFilter() {
			ifstream f("acf.filter");
			string line;
			size_t i=0, b;
			size_t *bs = new size_t[max_filters];
			size_t *fp = new size_t[max_filters];
			vector<string> tv;

			if (f.is_open()) {
				while (getline (f, line) ) {
					tv = split(line, ' ');
					bs[i] = atoi(tv[0].c_str());
					fp[i] = atoi(tv[1].c_str());
					i++;
				}
			}else{
				return false;
			}
			f.close();
			
			cur_mem = 0;
			for(i=0 ;i<max_filters; i++){
				if(filter[i] != NULL){
					delete filter[i];
					filter[i] = NULL;
				}

				//cout << bs[i] << endl;
				if(bs[i] > 0){
					switch(fp[i]){
						case 4:
							filter[i] = new CuckooFilter<ItemType, 4> (bs[i], true);
							break;
						case 8:
							filter[i] = new CuckooFilter<ItemType, 8> (bs[i], true);
							break;
						case 12:
							filter[i] = new CuckooFilter<ItemType, 12> (bs[i], true);
							break;
						case 16:
							filter[i] = new CuckooFilter<ItemType, 16> (bs[i], true);
							break;
						default:
							assert(false);
					}
					cur_mem += filter[i]->SizeInBytes();
				}
			}			
			cout << "Load Done." << endl;
			return true;
	}


	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	void
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		Info() {
		size_t num_insert=0, num_lookup=0;

		for(size_t i=0; i<max_filters;i++) {
			num_insert += insert_keys[i];
			num_lookup += lookup[i]; 
		}

/*		size_t nc_c=0;
		for(size_t i=0; i< nc_buckets; i++) {
			if(nc[i].length() > 0)
				nc_c++;
		}
		cout << "Negative Cache: " << nc_c << endl;
*/
		cout << "Adaptive Cuckoo Filters Status:\n"
			<< "Size: " << FilterSizeInBytes()+FixedSizeInBytes() << " bytes\n"
			<< "Filter Size: " << FilterSizeInBytes() << " bytes\n"
			<< "Overhead: " << 100*(float)FixedSizeInBytes()/(FilterSizeInBytes()+FixedSizeInBytes()) << " %" << endl
			<< "Filters: " << max_filters << endl << endl
			<< "Insert: " << num_insert << endl
			<< "Lookup: " << num_lookup << endl
			<< "False Pos: " << overall_fpp << endl
			<< "Grow: " << num_grow << endl
			<< "Shrink: " << num_shrink << endl << endl
			<< "fpp: " << 100*(float)overall_fpp/(overall_fpp+true_neg) << " %" << endl
			<< "Disk access: " << (overall_fpp + num_grow) << endl;
		return;
	}

}

#endif
