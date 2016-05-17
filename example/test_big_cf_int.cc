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
    size_t total_items  = 2000000;
    size_t sht_max_buckets = 5000;

    // Create a cuckoo filter where each item is of type size_t and
    // use 12 bits for each item:
    //    CuckooFilter<size_t, 12> filter(total_items);
    // To enable semi-sorting, define the storage of cuckoo filter to be
    // PackedTable, accepting keys of size_t type and making 13 bits
    // for each key:
    //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);

    // This is for cheating
    map<size_t, int> mapping_table;
    map<size_t, int>::iterator iter;    

    CuckooFilter<size_t, 8> filter(total_items);
    // Small hash table storing true negative caused by false positive
    size_t sht[sht_max_buckets];

    ifstream infile(argv[1]);
    string line;

    uint32_t hash1=0;

    size_t num_inserted = 0;
    size_t total_queries = 0;
    size_t false_queries = 0;
    size_t true_negative = 0;

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
		size_t record = atoi(t2[0].c_str());
		
		if(mapping_table.find(record) == mapping_table.end()){
			size_t raw_index;
			uint32_t index, tag;

			mapping_table[record] = 1;
			//cout << type << ' ' << record << endl;
			gettimeofday(&start,NULL);

			filter.GenerateIndexTagHash(record, 8, false, &raw_index, &index, &tag);
			//cout << index << '/' << tag << endl;
			if (filter.Add(index, tag) == cuckoofilter::NotEnoughSpace) {
			     cout << "Fail" << endl;
			     break;
			}
			gettimeofday(&end,NULL);
			insert_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;

			num_inserted++;

			//MurmurHash3_x86_32(str, 256, 1, &hash1);
			//cout << hash1 << '\n';
		}
	   }
	}else if(type == "BoscS:[An]"){
	   if(t2.size() >=2 ){
		//cout << line << '\n';
		size_t record = atoi(t2[0].c_str());

		    if(mapping_table.find(record) == mapping_table.end()){
			size_t raw_index, r_index;
			uint32_t index, tag;
			size_t status;

			gettimeofday(&start,NULL);

			filter.GenerateIndexTagHash(record, 8, false, &raw_index, &index, &tag);
			if(sht_max_buckets > 0)
				hash1 = raw_index % sht_max_buckets;

			status  = filter.Contain(index, tag, &r_index);
			if (status == cuckoofilter::Ok){
			    false_queries++;
			    if(sht_max_buckets > 0){
			        filter.AdaptFalsePositive(r_index);
			        sht[hash1] = record;
			    }
			    //cout << r_index << endl;
			} else if (status == cuckoofilter::NotSure){
			    if(sht[hash1] == record){
			        // True negative
				true_negative++;
			    }else{
				false_queries++;
				//cout << record << endl;
				filter.AdaptFalsePositive(r_index);
				sht[hash1] = record;
			    }
			} else {
			    true_negative++;
			}

			gettimeofday(&end,NULL);
			lookup_t += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
		    }
		    total_queries++;
	   }
	}
    }    
    

    // Output the size of the filter in bytes
    cout << "Insert MOPS : " << (float)num_inserted/insert_t << "\n";
    cout << "Lookup MOPS : " << (float)total_queries/lookup_t << "\n";
    cout << "Filter size(Bytes) : " << filter.SizeInBytes() << " bytes\n";
    cout << "false positive rate : "
              << 100.0 * false_queries / (false_queries+true_negative)
              << "%\n\n";

    cout << "Inserted items : " << num_inserted << '\n';
    cout << "Total queries : " << total_queries << '\n';
    cout << "True negative : " << true_negative << '\n';
    // Output the measured false positive rate

    return 0;
 }
