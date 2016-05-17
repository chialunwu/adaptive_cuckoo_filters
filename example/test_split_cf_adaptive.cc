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

/* When Change type, change the following
   bytes_per_item, is_pointer, define STRING, STRING2 (in adaptive_cuckoofilters.h)
*/
//#define STRING

// Global variables
const size_t max_filters = 2000;//1700;
const size_t sht_max_buckets = 10;
const size_t single_cf_size = 800;
const size_t bits_per_tag = 8;
const size_t mem_budget = 2097000;//276000;

bool grow = true;
bool shrink = true;

#ifdef STRING
const size_t bytes_per_item = 256;
bool is_pointer = true;
vector<vector<string> > busc_table(max_filters);
#else
const size_t bytes_per_item = 8;
bool is_pointer = false;
vector<vector<size_t> > busc_table(max_filters);
#endif

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
#ifdef STRING
    map<string, int> mapping_table;
    map<string, int>::iterator iter;  

	// The filter !!!
    AdaptiveCuckooFilters<char*, string, sht_max_buckets, max_filters, single_cf_size, bits_per_tag> acf(bytes_per_item, mem_budget);
    // For simulation, we have to get the hash to store the original keys
    CuckooFilter<char*, bits_per_tag> *dummy_filter = new CuckooFilter<char*, bits_per_tag>(single_cf_size);

	char record[bytes_per_item];	
	char **ts = new char*[1000000];
	for(int i = 0; i < 1000000; ++i)
    	ts[i] = new char[bytes_per_item];
#else
    map<size_t, int> mapping_table;
    map<size_t, int>::iterator iter;  

	// The filter !!!
    AdaptiveCuckooFilters<size_t, size_t, sht_max_buckets, max_filters, single_cf_size, bits_per_tag> acf(bytes_per_item, mem_budget);
    // For simulation, we have to get the hash to store the original keys
    CuckooFilter<size_t, bits_per_tag> *dummy_filter = new CuckooFilter<size_t, bits_per_tag>(single_cf_size);

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

			if(type == "BoscI:"){
			   if(t2.size() >= 3){
#ifdef STRING
				string t_record = t2[0]+t2[2];
				strcpy(record, t_record.c_str());
#else
				record = atoi(t2[0].c_str());
				size_t t_record = record;
#endif
				
				if(mapping_table.find(t_record) == mapping_table.end()){
					mapping_table[t_record] = 1;
					gettimeofday(&start,NULL);

					if(acf.Add(record, &hash1) == false) {
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
					}else{
						num_inserted++;
						gettimeofday(&end,NULL);
						insert_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
					}

					acf_status = acf.Lookup(record, &status, &hash1, &r_index, &sht_hash);
					assert(acf_status == adaptive_cuckoofilters::Found);

					// Insert info busc table
					dummy_filter->GenerateIndexTagHash(record, bytes_per_item, is_pointer, &raw_index, &index, &tag);
					hash1 = raw_index % max_filters;
					busc_table[hash1].push_back(record);
				}
			   }
			}else if(type == "BoscS:[An]"){
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
		}
	}

	acf.DumpStats();
	acf.DumpFilter();
 
    // Output the size of the filter in bytes
    cout << "Insert MOPS : " << (float)num_inserted/insert_t << "\n";
    cout << "Lookup MOPS : " << (float)total_queries/lookup_t << "\n";
 //   cout << "Inserted items : " << num_inserted << '\n';
  //  cout << "Total queries : " << total_queries << '\n';
	cout << "False positive : " << false_queries << '\n';
    cout << "True negative : " << true_negative << "\n\n";
	acf.Info();

    return 0;
 }
