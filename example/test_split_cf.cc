/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "cuckoofilter.h"
#include "hashutil.h"
#include "MurmurHash3.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <map>

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
    size_t max_buckets = 1000000;

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
   
    CuckooFilter<char[256], 8> **filter = new CuckooFilter<char[256], 8>*[max_buckets/32];

    ifstream infile(argv[1]);
    string line;

    uint32_t hash1=0;

    size_t num_inserted = 0;
    size_t total_queries = 0;
    size_t false_queries = 0;
    size_t true_negative = 0;

    size_t filter_size = 0;
    size_t filter_count = 0;


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

			MurmurHash3_x86_32(str, 256, 1384975, &hash1);
			hash1 = (hash1 % max_buckets)/32;
			
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
		}
	   }
	}else if(type == "BoscS:[An]"){
	   if(t2.size() >=2 ){
		//cout << line << '\n';
		string record = t2[0]+t2[1];
		if(record.length() < 256){
		    strcpy(str, record.c_str());
		    if(mapping_table.find(record) == mapping_table.end()){
                        MurmurHash3_x86_32(str, 256, 1384975, &hash1);
                        hash1 = (hash1 % max_buckets)/32;

			if (filter[hash1] && filter[hash1]->Contain(str) == cuckoofilter::Ok) {
			    false_queries++;
			    cout << record << endl;
			}else{
			    true_negative++;
			}
		    }
		    total_queries++;
		}
	   }
	}
    }    
    

    // Output the size of the filter in bytes
    
    cout << "Filter size(Bytes) : " << filter_size << " bytes\n";
    cout << "Filter count : " << filter_count << "\n";

    cout << "Inserted items : " << num_inserted << '\n';
    cout << "Total queries : " << total_queries << '\n';
    cout << "True negative : " << true_negative << '\n';
    // Output the measured false positive rate
    cout << "false positive rate is "
              << 100.0 * false_queries / total_queries
              << "%\n";

    return 0;
 }
