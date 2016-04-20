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
    size_t sht_max_buckets = 1000;

    bool adaptive = true;
    // Create a cuckoo filter where each item is of type size_t and
    // use 12 bits for each item:
    //    CuckooFilter<size_t, 12> filter(total_items);
    // To enable semi-sorting, define the storage of cuckoo filter to be
    // PackedTable, accepting keys of size_t type and making 13 bits
    // for each key:
    //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);

    // This is for cheating
    map<string, int> mapping_table;
    map<string, int>::iterator iter;    
   
    CuckooFilter<char[256], 8> **filter = new CuckooFilter<char[256], 8>*[max_blocks];

    // Small hash table storing true negative caused by false positive
    string *sht = new string[sht_max_buckets];

    ifstream infile(argv[1]);
    string line;

    uint32_t hash1=0;
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

	char str[256];
	bzero(str, 256);

	if(type == "BoscI:"){
	   if(t2.size() >= 3){
		//cout << line << '\n';
		string record = t2[0]+t2[2];
		
		strcpy(str, record.c_str());
		if(mapping_table.find(record) == mapping_table.end()){
			mapping_table[record] = 1;

			gettimeofday(&start,NULL);
			MurmurHash3_x86_32(str, 256, 1384975, &hash1);   // Computed in mapping table
			hash1 = hash1 % max_blocks;
			//cout << type << ' ' << record << endl;
			if(!filter[hash1]){
			    filter[hash1] = new CuckooFilter<char[256], 8> (100);
			    filter_size += filter[hash1]->SizeInBytes();
			    filter_count ++;
			} 
			if (filter[hash1]->Add(str) != cuckoofilter::Ok) {
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
		string record = t2[0]+t2[1];
		if(record.length() < 256){
		    strcpy(str, record.c_str());
		    if(mapping_table.find(record) == mapping_table.end()){
			gettimeofday(&start,NULL);
			MurmurHash3_x86_32(str, 256, 1384975, &hash1);	// Computed in mapping table
			if(adaptive){
				//Check small hash table
				t_hash1 = hash1 % sht_max_buckets;
				if(sht[t_hash1].compare(record) == 0){
					// True negative
					true_negative++;
				}else{
					//Check cuckoo filter
					hash1 = hash1 % max_blocks;
					if (filter[hash1] && filter[hash1]->Contain(str) == cuckoofilter::Ok) {
					    false_queries++;
					    //cout << record << endl;
					    sht[t_hash1] = record;
					}else{
					    true_negative++;
					}
				}
			}else{
				//Check cuckoo filter
				hash1 = hash1 % max_blocks;
				if (filter[hash1] && filter[hash1]->Contain(str) == cuckoofilter::Ok) {
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
