/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "hashutil.h"
#include "hash_functions.h"
#include "cuckoofilter.h"

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

//compression for object table
typedef struct
{
    uint32_t hash1;
    uint32_t hash2;
} chain_bucket_c;
#define CHAIN_BUCKET_C_LEN      sizeof(chain_bucket_c)

typedef struct
{
    char                is_reload;
    uint8_t         elements_in_chain;
    chain_bucket_c*   ptr_to_chain;
} index_bucket_c;
#define INDEX_BUCKET_C_LEN      sizeof(index_bucket_c)

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
    size_t max_buckets = 20000;

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
  
    // BUSC compression table
    index_bucket_c *index_table_c_ptr = (index_bucket_c*)calloc(max_buckets, INDEX_BUCKET_C_LEN);
    void *tmp;
    int entries_in_chain, replace_idx, elements_in_chain;
    for (int i = 0; i < max_buckets ; i++)
    {
        index_table_c_ptr[i].is_reload=1;
        index_table_c_ptr[i].ptr_to_chain = NULL;
        index_table_c_ptr[i].elements_in_chain = 0;
    }

    // Small hash table storing true negative caused by false positive
    ifstream infile(argv[1]);
    string line;


    uint32_t murhash[4];
    uint32_t hash1=0;
    uint32_t hash2=0;

    size_t num_inserted = 0;
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
			MurmurHash3_x64_128(str, 256, 1384975, murhash);   // Computed in mapping table
			hash1 = murhash[0];
		 	//hash2 = fnv_32a_str(str, FNV1_32A_INIT, 256);
		 	hash2 = murhash[1];

			hash1 = hash1 % max_buckets;
			//cout << type << ' ' << record << endl;
			    elements_in_chain = index_table_c_ptr[hash1].elements_in_chain;
			    replace_idx =-1;
			    for (int i = 0; i < elements_in_chain; i++)
			    {
				if(index_table_c_ptr[hash1].ptr_to_chain[i].hash2==0)
				{
				    replace_idx = i;
				    break;
				}
			    }

			    if (replace_idx == -1)
			    {
				tmp=NULL;
				if ((tmp=realloc(index_table_c_ptr[hash1].ptr_to_chain,
						 (elements_in_chain +1) * CHAIN_BUCKET_C_LEN))!=NULL)
				{
				    index_table_c_ptr[hash1].ptr_to_chain = (chain_bucket_c *)tmp;
				    filter_size += 8;
				}
				else
				{
				    cout << "ERROR\n";
				    return -1;
				}

				index_table_c_ptr[hash1].ptr_to_chain[elements_in_chain].hash2 = hash2;
				index_table_c_ptr[hash1].elements_in_chain ++;
			    }
			    else
			    {
				index_table_c_ptr[hash1].ptr_to_chain[replace_idx].hash2 = hash2;
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
			MurmurHash3_x64_128(str, 256, 1384975, murhash);	// Computed in mapping table
			hash1 = murhash[0];
			//hash2 = fnv_32a_str(str, FNV1_32A_INIT, 256);
			hash2 = murhash[1];
			//Check cuckoo filter
			hash1 = hash1 % max_buckets;
			
			bool flag = false;
			if (index_table_c_ptr[hash1].ptr_to_chain != NULL){
				entries_in_chain = index_table_c_ptr[hash1].elements_in_chain;
				for (int i = 0; i < entries_in_chain; i++)
				{
				    if (index_table_c_ptr[hash1].ptr_to_chain[i].hash2 == hash2)
				    {
					false_queries++;
					flag = true;
				    }
				}
			}
			if(flag == false){
				true_negative++;
			}
			gettimeofday(&end,NULL);
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

    cout << "Inserted items : " << num_inserted << '\n';
    cout << "Total queries : " << total_queries << '\n';
    cout << "True negative : " << true_negative << '\n';
    // Output the measured false positive rate

    return 0;
 }
