/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
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

using cuckoofilter::CuckooFilter;
using namespace std;

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
    size_t max_blocks = 20000;
    size_t sht_max_buckets = 10;
    size_t single_cf_size = 100;

    bool adaptive = true;
    // Create a cuckoo filter where each item is of type size_t and
    // use 12 bits for each item:
    //    CuckooFilter<size_t, 12> filter(total_items);
    // To enable semi-sorting, define the storage of cuckoo filter to be
    // PackedTable, accepting keys of size_t type and making 13 bits
    // for each key:
    //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);

    // This is for cheating
    map<int, int> mapping_table;
    map<int, int>::iterator iter;    
   
    CuckooFilter<int, 4> **filter = new CuckooFilter<int, 4>*[max_blocks];
    CuckooFilter<int, 4> dummy_filter(single_cf_size);


    // Small hash table storing true negative caused by false positive
    int sht[sht_max_buckets];

    ifstream infile(argv[1]);
    string line;

    uint32_t murhash[4];
    uint32_t hash1;
    uint32_t t_hash1=0;

    size_t num_inserted = 0;
    size_t num_lookup = 0;
    size_t total_queries = 0;
    size_t false_queries = 0;
    size_t true_negative = 0;

    size_t filter_size = 0;
    size_t filter_count = 0;

    // Timing
    struct  timeval start;
    struct  timeval end;   
    unsigned  long insert_t=0, lookup_t=0;


    while (getline(infile, line)){
	vector<string> t1 = split(line, ' ');
	string type = t1[0];
	vector <string> t2 = split(t1[1], ':');
//	cout << record << '\n';

	if(type == "BoscI:"){
	   if(t2.size() >= 3){
		//cout << line << '\n';
		int record = atoi(t2[0].c_str());
		
		if(mapping_table.find(record) == mapping_table.end()){
			size_t index, raw_index;
			uint32_t tag;

			mapping_table[record] = 1;

			gettimeofday(&start,NULL);
			
			//MurmurHash3_x86_32(str, 256, 1384975, &hash1);
			//hash1 = hash1 % max_blocks;

			//MurmurHash3_x86_128(str, 256, 1384975, murhash);   // Computed in mapping table
			//hash1 = murhash[0] % max_blocks;

			dummy_filter.GenerateIndexTagHash(record, &raw_index, &index, &tag);
			hash1 = raw_index % max_blocks;
			//cout << type << ' ' << record << endl;
			if(!filter[hash1]){
			    filter[hash1] = new CuckooFilter<int, 4> (single_cf_size);
			    filter_size += filter[hash1]->SizeInBytes();
			    filter_count ++;
			} 
			if (filter[hash1]->Add(record, index, tag) != cuckoofilter::Ok) {
			     cout << "Fail" << endl;
			     break;
			}
			num_inserted++;

			//cout << hash1 << '\n';
			gettimeofday(&end,NULL);
			insert_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
		}
	   }
	}else if(type == "BoscS:[An]"){
	   if(t2.size() >=2 ){
		//cout << line << '\n';
		int record = atoi(t2[0].c_str());
		    if(mapping_table.find(record) == mapping_table.end()){
			size_t index, raw_index, r_index;
			uint32_t tag;
		        size_t status;

			gettimeofday(&start,NULL);
			
			//MurmurHash3_x86_32(str, 256, 1384975, &hash1);

			//MurmurHash3_x86_128(str, 256, 1384975, murhash);	// Computed in mapping table
			//hash1 = murhash[0];

			dummy_filter.GenerateIndexTagHash(record, &raw_index, &index, &tag);
			hash1 = raw_index;

			//cout << hash1 % max_blocks << endl;
			if(adaptive){
				//Check small hash table
				t_hash1 = hash1 % sht_max_buckets;
				//Check cuckoo filter
				hash1 = hash1 % max_blocks;
			
				if (filter[hash1]) {
				    status  = filter[hash1]->Contain(record, index, tag, &r_index);
				    if (status == cuckoofilter::Ok){
					false_queries++;
					filter[hash1]->AdaptFalsePositive(r_index);
					//sht[t_hash1] = record;
					//cout << record << endl;
				    } else if (status == cuckoofilter::NotSure){
					    if(sht[t_hash1] == record){
						// True negative
						true_negative++;
					    }else{
						false_queries++;
						//cout << record << endl;
						//filter[hash1]->AdaptFalsePositive(r_index);
						// Insert record to sht when second false positive
						sht[t_hash1] = record;
					    }
				    } else {
					true_negative++;
				    }
				}else{
				    true_negative++;
				}

			}else{
				//Check cuckoo filter
				hash1 = hash1 % max_blocks;
				if (filter[hash1] && filter[hash1]->Contain(record, index, tag, &r_index) == cuckoofilter::Ok) {
				    false_queries++;
				    //cout << record << endl;
				}else{
				    true_negative++;
				}
			}
			gettimeofday(&end,NULL);
			//num_lookup ++;
			lookup_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
		    }
		    total_queries++;
	   }
	}
    }    
    

    // Output the size of the filter in bytes
    cout << "Insert MOPS : " << (float)num_inserted/insert_t << "\n";
    cout << "Lookup MOPS : " << (float)total_queries/lookup_t << "\n";
    cout << "Filter size(Bytes) : " << filter_size << " bytes\n";
    cout << "false positive rate : "
              << 100.0 * false_queries / total_queries
              << "%\n\n";

    cout << "Filter count : " << filter_count << "\n";
    cout << "Inserted items : " << num_inserted << '\n';
    cout << "Total queries : " << total_queries << '\n';
    cout << "True negative : " << true_negative << '\n';
    // Output the measured false positive rate

    return 0;
 }
