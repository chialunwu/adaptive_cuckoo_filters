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

// Global variables
const size_t max_filters = 20000;
const size_t sht_max_buckets = 10;
const size_t single_cf_size = 100;
const size_t bits_per_tag = 8;
const size_t bytes_per_item = 256;

bool adaptive = true;
bool rebuild = true;
bool shrink = true;

vector<vector<string> > busc_table(max_filters);

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
    map<string, int> mapping_table;
    map<string, int>::iterator iter;  

	// The filter !!!
    AdaptiveCuckooFilters<char*, string, sht_max_buckets, max_filters, single_cf_size, bits_per_tag> acf(bytes_per_item);
    // For simulation, we have to get the hash to store the original keys
    CuckooFilter<char*, bits_per_tag> *dummy_filter = new CuckooFilter<char*, bits_per_tag>(single_cf_size);;


    ifstream infile(argv[1]);
    string line;

	// Temp variables
	size_t index, raw_index;
	uint32_t tag, hash1;
	size_t r_index, sht_hash;
	size_t status;
	uint32_t vic_hash;


    size_t num_inserted = 0;
    size_t total_queries = 0;
    size_t false_queries = 0;
    size_t true_negative = 0;


    // Timing
    struct  timeval start;
    struct  timeval end;   
    unsigned  long insert_t=0, lookup_t=0;

	char str[bytes_per_item];


    while (getline(infile, line)){
			vector<string> t1 = split(line, ' ');
			string type = t1[0];
			vector <string> t2 = split(t1[1], ':');
		//	cout << record << '\n';

    	    bzero(str, bytes_per_item);

			if(type == "BoscI:"){
			   if(t2.size() >= 3){
				string record = t2[0]+t2[2];
				strcpy(str, record.c_str());
				
				if(mapping_table.find(record) == mapping_table.end()){
					mapping_table[record] = 1;

					gettimeofday(&start,NULL);	
					assert(acf.Add(str) == true);
					num_inserted++;
					gettimeofday(&end,NULL);

					insert_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;

					// Insert info busc table
					dummy_filter->GenerateIndexTagHash(str, bytes_per_item, true, &raw_index, &index, &tag);
					hash1 = raw_index % max_filters;
					busc_table[hash1].push_back(record);
				}
			   }
			}else if(type == "BoscS:[An]"){
			   if(t2.size() >=2 ){
					string record = t2[0]+t2[1];
					strcpy(str, record.c_str());
					bool true_negative_flag = (mapping_table.find(record) == mapping_table.end());

					// Simulate get data from disk
					acf.Lookup(str, &status, &hash1, &r_index, &sht_hash);
					vector<char *> sv(busc_table[hash1].size());
					char ts[single_cf_size][bytes_per_item];
					for(size_t i=0; i< busc_table[hash1].size(); i++){
			            bzero(ts[i], bytes_per_item);
            			strcpy(ts[i], busc_table[hash1][i].c_str());
						sv[i] = (char *)ts[i];
					}	

					gettimeofday(&start,NULL);
					if(acf.Lookup(str, &status, &hash1, &r_index, &sht_hash) == adaptive_cuckoofilters::Found
						&& true_negative_flag){
						false_queries++;
						if(acf.AdaptToFalsePositive(str, status, hash1, r_index, sht_hash) ==
						   adaptive_cuckoofilters::NeedRebuild) {
//							acf.GrowFilter(hash1, sv);
//							vic_hash = acf.GetVictimFilter();
//							acf.ShrinkFilter(vic_hash, sv);
						}
					}
					gettimeofday(&end,NULL);
					//num_lookup ++;
					lookup_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
					total_queries++;
			   }
			}
    }    

	// Check true positive correctness
	/*
	for(size_t i=0;i<max_filters;i++){
		for(size_t j=0; j< busc_table[i].size(); j++){
			bzero(str, 256);
			strcpy(str, busc_table[i][j].c_str());
			size_t s = acf.Lookup(str, &status, &hash1, &r_index, &sht_hash);
			assert(s == adaptive_cuckoofilters::Found || s == adaptive_cuckoofilters::NoFilter);
		}
	} */ 
 
    // Output the size of the filter in bytes
    cout << "Insert MOPS : " << (float)num_inserted/insert_t << "\n";
    cout << "Lookup MOPS : " << (float)total_queries/lookup_t << "\n";
    cout << "Filter size(Bytes) : " << acf.SizeInBytes() << " bytes\n";
    cout << "false positive rate : "
              << 100.0 * false_queries / total_queries
              << "%\n\n";

//    cout << "Filter count : " << filter_count << "\n";
    cout << "Inserted items : " << num_inserted << '\n';
    cout << "Total queries : " << total_queries << '\n';
    cout << "True negative : " << true_negative << '\n';
    // Output the measured false positive rate

    return 0;
 }
