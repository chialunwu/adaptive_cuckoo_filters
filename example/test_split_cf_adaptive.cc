/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "adaptive_cuckoofilter.h"
#include "cuckoofilter.h"

#include "hashutil.h"
#include "hash_functions.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <map>

#include <sys/time.h>
#include <unistd.h>

using adaptive_cuckoofilters::AdaptiveCuckooFilters;
using cuckoofilter::CuckooFilter;
using namespace std;

#define SHRINK_STRING()\
		shrink_status=false;\
		do {\
			uint32_t idx = acf.GetVictimFilter();\
			if(idx == -1) break;\
			vector<char *> sv2(busc_table[idx].size());\
			for(size_t i=0; i< busc_table[idx].size(); i++){\
				bzero(ts[i], bytes_per_item);\
				strcpy(ts[i], busc_table[idx][i].c_str());\
				sv2[i] = (char *)ts[i];\
			}	\
			shrink_status = acf.ShrinkFilter(idx, sv2);\
		}while(!shrink_status);

#define SHRINK()\
		shrink_status=false;\
		do {\
			uint32_t idx = acf.GetVictimFilter();\
			if(idx == -1) break;\
			shrink_status = acf.ShrinkFilter(idx, busc_table[idx]);\
		}while(!shrink_status);

#define GROW_STRING(ghash, noshrink, grow_bucket) \
		vector<char *> sv(busc_table[ghash].size()); \
		for(size_t i=0; i< busc_table[ghash].size(); i++){ \
			memset(ts[i], 0, bytes_per_item); \
			strcpy(ts[i], busc_table[ghash][i].c_str()); \
			sv[i] = (char *)ts[i]; \
		}\
		do{\
			acf_status = acf.GrowFilter(ghash, sv, grow_bucket);\
		}while(acf_status == adaptive_cuckoofilters::NeedRebuild);\
		if(shrink && !noshrink && acf_status == adaptive_cuckoofilters::NeedShrink){\
			SHRINK_STRING();\
		}

#define GROW(ghash, noshrink, grow_bucket)\
		do{\
			acf_status = acf.GrowFilter(ghash, busc_table[ghash], grow_bucket);\
		}while(acf_status == adaptive_cuckoofilters::NeedRebuild);\
		if(shrink && !noshrink && acf_status == adaptive_cuckoofilters::NeedShrink){\
			SHRINK();\
		}

#define INSERT_STRING(ghash) \
		vector<char *> sv(busc_table[ghash].size()); \
		for(size_t j=0; j< busc_table[ghash].size(); j++){ \
			memset(ts[j], 0, bytes_per_item); \
			strcpy(ts[j], busc_table[ghash][j].c_str()); \
			sv[j] = (char *)ts[j]; \
		}\
		if(acf.InsertKeysToFilter(ghash, sv) == adaptive_cuckoofilters::NeedRebuild && false) {\
			do{\
				acf_status = acf.GrowFilter(ghash, sv, false);\
			}while(acf_status == adaptive_cuckoofilters::NeedRebuild);\
		}

#define INSERT(ghash)\
		if(acf.InsertKeysToFilter(ghash, busc_table[ghash]) == adaptive_cuckoofilters::NeedRebuild && false) {\
			do{\
				acf_status = acf.GrowFilter(ghash, busc_table[ghash], false);\
			}while(acf_status == adaptive_cuckoofilters::NeedRebuild);\
		}



/* When Change type, change the following
   bytes_per_item, is_pointer, define STRING, STRING2 (in adaptive_cuckoofilters.h)
*/

void choose_filter_size(size_t total_items, float overhead, size_t bits_tag, size_t &max_filters, size_t &single_cf_size) {
	max_filters = ((overhead*total_items*bits_tag)/(0.95*8))/(32-32*overhead);
	single_cf_size = total_items/max_filters;

//	max_filters = 16384;
//	single_cf_size = 650;
}

void choose_filter_size2(float ratio, size_t total_items, size_t &max_filters, size_t &single_cf_size) {
	max_filters = (size_t)sqrt(total_items/ratio);
	single_cf_size = (size_t)max_filters*ratio;

	cout << max_filters << '/' << single_cf_size << endl;
//	max_filters = 100;
//	single_cf_size = 10000;
}

/*******************************************************************/
#define STRING

// Global variables
size_t max_filters = 0;//1700;
size_t single_cf_size = 0;

#ifdef STRING
size_t total_items = 170000;
size_t mem_budget = 280000;
#else
size_t total_items = 10000000;
size_t mem_budget = 11200000;
#endif

size_t total_lookup = 5000000;

float overhead = 0.03;
const size_t bits_per_tag = 12;
float filter_ratio = 0.1;

const size_t sht_max_buckets = 10;

bool grow = true;
bool shrink = true;

bool global_optimize = false;
bool local_optimize = false;
size_t rebuild_period = 100000;
/********************************************************************/

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


int main(int argc, char** argv) {
// Testing area
// Test end
	//mem_budget = total_items/0.95*bits_per_tag/8;

	// Decide the filter size
	//choose_filter_size(total_items, overhead, bits_per_tag, max_filters, single_cf_size);
	choose_filter_size2(filter_ratio, total_items, max_filters, single_cf_size);
	cout << "max_filters: " << max_filters << " single_cf_size: " << single_cf_size << endl;


#ifdef STRING
	const size_t bytes_per_item = 256;
	bool is_pointer = true;
	vector<vector<string> > busc_table(max_filters);
#else
	const size_t bytes_per_item = 8;
	bool is_pointer = false;
	vector<vector<size_t> > busc_table(max_filters);
#endif


#ifdef STRING
    map<string, int> mapping_table;
    map<string, int>::iterator iter;  

	// The filter !!!
    AdaptiveCuckooFilters<char*, string, sht_max_buckets, bits_per_tag> acf(bytes_per_item,max_filters, single_cf_size, mem_budget);
    // For simulation, we have to get the hash to store the original keys
    CuckooFilter<char*, bits_per_tag> *dummy_filter = new CuckooFilter<char*, bits_per_tag>(single_cf_size, false);
	cout << "acf size: " << acf.FilterSizeInBytes() << " bytes\n";

	char record[bytes_per_item];	
	char **ts = new char*[1000000];
	for(int i = 0; i < 1000000; ++i)
    	ts[i] = new char[bytes_per_item];
#else
    map<size_t, int> mapping_table;
    map<size_t, int>::iterator iter;  

	// The filter !!!
    AdaptiveCuckooFilters<size_t, size_t, sht_max_buckets, bits_per_tag> acf(bytes_per_item, max_filters, single_cf_size, mem_budget);
    // For simulation, we have to get the hash to store the original keys
    CuckooFilter<size_t, bits_per_tag> *dummy_filter = new CuckooFilter<size_t, bits_per_tag>(single_cf_size, false);
	cout << "acf size: " << acf.FilterSizeInBytes() << " bytes\n";

	size_t record;	
	size_t *ts = new size_t[1000000];
#endif

    ifstream infile(argv[1]);
    string line;

	// Temp variables
	size_t raw_index;
	uint32_t index, tag, hash1;
	size_t r_index, sht_hash;
	size_t status, acf_status;

    size_t num_inserted = 0;
    size_t total_queries = 0;
    size_t false_queries = 0;
    size_t true_negative = 0;
	size_t true_total_queries = 0;
	size_t insert_fail = 0;
	
	bool shrink_status, bstatus;
	size_t num_grow=0;

    // Timing
    struct  timeval start;
    struct  timeval end;   
    unsigned  long insert_t=0, lookup_t=0;



/*******************************************************************************/
//	acf.LoadStatsToOptimize();
//	acf.LoadFilter();
    while (getline(infile, line)){
			vector<string> t1 = split(line, ' ');
			string type = t1[0];
			vector <string> t2 = split(t1[1], ':');
		//	cout << record << '\n';
#ifdef STRING
    	    bzero(record, bytes_per_item);
#endif

			if(type == "BoscI:" && num_inserted < total_items){
			   if(t2.size() >= 3){
#ifdef STRING
				string t_record = t2[0]+t2[2];
				strcpy(record, t_record.c_str());
#else
				record = atoi(t2[0].c_str());
				size_t t_record = record;
#endif
				
				if(mapping_table.find(t_record) == mapping_table.end()){
					gettimeofday(&start,NULL);

					if(acf.Add(record, &hash1) == false) {
						insert_fail ++;
						//cout <<"Insert to "<<hash1<<" fail, grow the filter\n";
						// Insert Failuer, grow the filter
						// Simulate get data from disk
						do{
#ifdef STRING
							GROW_STRING(hash1, true, true);
#else
							GROW(hash1, true, true);
#endif
							bstatus = acf.Add(record, &hash1);
						}while(bstatus == false);

						//acf_status = acf.Lookup(record, &status, &hash1, &r_index, &sht_hash);
						//assert(acf_status == adaptive_cuckoofilters::Found);
					}else{
						gettimeofday(&end,NULL);
						insert_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;

						//acf_status = acf.Lookup(record, &status, &hash1, &r_index, &sht_hash);
						//assert(acf_status == adaptive_cuckoofilters::Found);
					}
					// Insert info busc table
					mapping_table[t_record] = 1;
					num_inserted++;
					dummy_filter->GenerateIndexTagHash(record, bytes_per_item, is_pointer, &raw_index, &index, &tag);
					hash1 = raw_index % max_filters;
					busc_table[hash1].push_back(record);
				}
			  }
			}else if(type == "BoscS:[An]" && true_total_queries < total_lookup){
			   if(t2.size() >=2){
#ifdef STRING
					string t_record = t2[0]+t2[1];
					strcpy(record, t_record.c_str());
#else
					record = atoi(t2[0].c_str());
					size_t t_record = record;
#endif

					bool true_negative_flag = (mapping_table.find(t_record) == mapping_table.end());
					adaptive_cuckoofilters::Status acf_status;

					/* Global Optimal Rebuild */
					true_total_queries++;
					if(global_optimize && true_total_queries % rebuild_period == 0) {
//					if(acf.FilterSizeInBytes()+acf.FixedSizeInBytes() > mem_budget ) {
						acf.GlobalOptimalFilterSize(NULL, NULL);
						cout << "Global Optimize: " << acf.SizeInBytes() << " bytes\n";
						for(size_t i=0;i<max_filters;i++) {
							//cout << busc_table[i].size() << " keys, capacity:" << acf.getFilterBucket(i)*4 << endl;
#ifdef STRING
							INSERT_STRING(i);
#else
							INSERT(i);
#endif
						}
					}

					if(local_optimize && true_total_queries % 200000 == 0) {
				//		cout << "Incremental Optimize\n";
						cout << "Local Optimize\n";

						for(size_t i=0;i<max_filters;i++) {
							//cout << busc_table[i].size() << " keys, capacity:" << acf.getFilterBucket(i)*4 << endl;
							if(acf.LocalOptimalFilterSize(i)) {
#ifdef STRING
								INSERT_STRING(i);
#else
								INSERT(i);
#endif
							}
						}
					}
					/* End Global Optimal Rebuild */

					if(true_total_queries % rebuild_period == 0) {
						cout << "Fpp: " << 100*((float)false_queries/(false_queries+true_negative)) << " %\n";
					}

					gettimeofday(&start,NULL);
					acf_status = acf.Lookup(record, &status, &hash1, &r_index, &sht_hash);
					if(acf_status == adaptive_cuckoofilters::Found
						&& true_negative_flag){
						false_queries++;
						if(acf.AdaptToFalsePositive(record, status, hash1, r_index, sht_hash) ==
						   adaptive_cuckoofilters::NeedRebuild) {
//							cout << "Grow and shrink\n";
							// Simulate get data from disk
							if(grow){
#ifdef STRING
									GROW_STRING(hash1,false, false);
#else
									GROW(hash1,false, false);
#endif
							}
							//cout<<"Grow "<< hash1 <<" and Shrink "<<vic_hash<<endl;
						}
					}else{
						gettimeofday(&end,NULL);
						lookup_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
						total_queries++;
					}

					if(acf_status == adaptive_cuckoofilters::NotFound && !true_negative_flag){
						assert("False negative!");
					}else if(acf_status == adaptive_cuckoofilters::NotFound && true_negative_flag){
						true_negative++;
					}
			   }
			}
    }    

	acf.DumpStats();
	acf.DumpFilter();
 
    // Output the size of the filter in bytes
    cout << "\nInsert MOPS : " << (float)num_inserted/insert_t << "\n";
    cout << "Lookup MOPS : " << (float)total_queries/lookup_t << "\n";
	cout << "Insert fail : " << insert_fail << endl;
	cout << "Lookup : " << (false_queries + total_queries) << endl;
 //   cout << "Inserted items : " << num_inserted << '\n';
  //  cout << "Total queries : " << total_queries << '\n';
	cout << "False positive : " << false_queries << '\n';
    cout << "True negative : " << true_negative << "\n\n";
	cout << "Fpp: " << 100*((float)false_queries/(false_queries+true_negative)) << " %\n";
	acf.Info();


	// Check true positive correctness	
	for(size_t i=0;i<max_filters;i++){
		for(size_t j=0; j< busc_table[i].size(); j++) {
#ifdef STRING
			bzero(record, 256);
			strcpy(record, busc_table[i][j].c_str());
#else
			record = busc_table[i][j];
#endif
			size_t s = acf.Lookup(record, &status, &hash1, &r_index, &sht_hash);
			assert(s == adaptive_cuckoofilters::Found);
//			if (s != adaptive_cuckoofilters::Found) cout << record << endl;
		}
	}
	cout << "Pass integrity test.\n";

    return 0;
 }
