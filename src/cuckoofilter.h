/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _CUCKOO_FILTER_H_
#define _CUCKOO_FILTER_H_

#include "debug.h"
#include "hashutil.h"
#include "packedtable.h"
#include "printutil.h"
#include "singletable.h"

#include "hash_functions.h"

#include <cassert>

using namespace std;

namespace cuckoofilter {
	// status returned by a cuckoo filter operation
	enum Status {
		Ok = 0,
		NotFound = 1,
		NotEnoughSpace = 2,
		NotSupported = 3,
		NotSure = 4,	// Not sure if the positive is true/false positive -> Need to check the stash
		MayClearNC = 5 // May need to clear the previous false positive but newly added item
	};

	// maximum number of cuckoo kicks before claiming failure
	const size_t kMaxCuckooCount = 500;

	
	class CuckooFilterInterface {
public:
			// Number of items stored
			size_t  num_items;

		size_t capacity;
		size_t fingerprint_size;
		
		virtual size_t SizeInBytes() const {return 0;}
		virtual Status Add(const uint32_t i, const uint32_t tag) {return Ok;}
		virtual Status Contain(const uint32_t i, const uint32_t tag, size_t* index) const{return Ok;}
		virtual void AdaptFalsePositive(const size_t i){}
	};


	// A cuckoo filter class exposes a Bloomier filter interface,
	// providing methods of Add, Delete, Contain. It takes three
	// template parameters:
	//   ItemType:  the type of item you want to insert
	//   bits_per_item: how many bits each item is hashed into
	//   TableType: the storage of table, SingleTable by default, and
	// PackedTable to enable semi-sorting 
	template <typename ItemType,
			  size_t bits_per_item,
			  template<size_t> class TableType = SingleTable>
	class CuckooFilter: public CuckooFilterInterface {
public:
		// Storage of items
		TableType<bits_per_item> *table_;

		typedef struct {
			size_t index;
			uint32_t tag;
			bool used;
		} VictimCache;

		VictimCache victim_;

//public:
		inline size_t IndexHash(uint32_t hv) const {
			return hv % table_->num_buckets;
		}

		inline uint32_t TagHash(uint32_t hv) const {
			uint32_t tag;
			tag = hv & ((1ULL << bits_per_item) - 1);
			tag += (tag == 0);
			return tag;
		}

		inline void GenerateIndexTagHash(const ItemType &item,
										 size_t* index,
										 uint32_t* tag) const {

			std::string hashed_key = HashUtil::SHA1Hash((const char*) &item,
												   sizeof(item));
			uint64_t hv = *((uint64_t*) hashed_key.c_str());

			*index = IndexHash((uint32_t) (hv >> 32));
			*tag   = TagHash((uint32_t) (hv & 0xFFFFFFFF));
		}


		inline void GenerateIndexTagHash(const ItemType &item,
					 const size_t item_bytes,
					 bool item_is_pointer,
					 size_t* raw_index,
										 uint32_t* index,
										 uint32_t* tag) const {
/*
			std::string hashed_key = HashUtil::SHA1Hash((const char*) &item,
												   sizeof(item));
			uint64_t hv = *((uint64_t*) hashed_key.c_str());

		*raw_index = (uint32_t) (hv & 0xFFFFFFFF) ;
			*index = IndexHash((uint32_t) (hv >> 32));
			*tag   = TagHash((uint32_t) (hv & 0xFFFFFFFF));
*/
				uint32_t murhash[4];
				if(item_is_pointer) {
					MurmurHash3_x64_128((const void*)item, item_bytes, 1384975, murhash);
				}else{
					MurmurHash3_x64_128(&item, item_bytes, 1384975, murhash);
				}
				*raw_index = (size_t)murhash[0];
				*index = murhash[1];
				*tag = murhash[2];
		}

		inline size_t AltIndex(const size_t index, const uint32_t tag) const {
			// NOTE(binfan): originally we use:
			// index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
			// now doing a quick-n-dirty way:
			// 0x5bd1e995 is the hash constant from MurmurHash2
			return IndexHash((uint32_t) (index ^ (tag * 0x5bd1e995)));
		}

		Status AddImpl(const size_t i, const uint32_t tag);

		// load factor is the fraction of occupancy
		double LoadFactor() const {
			return 1.0 * Size()  / table_->SizeInTags();
		}

		double BitsPerItem() const {
			return 8.0 * table_->SizeInBytes() / Size();
		}

//	public:
		explicit CuckooFilter(const size_t max_num_keys) {
			size_t assoc = 4;
			size_t num_buckets = upperpower2(max_num_keys / assoc);
			double frac = (double) max_num_keys / num_buckets / assoc;
			if (frac > 0.96) {
				num_buckets <<= 1;
			}
			victim_.used = false;
			table_  = new TableType<bits_per_item>(num_buckets);
			//capacity = assoc * num_buckets;
			fingerprint_size = bits_per_item;
			num_items = 0;
			capacity = max_num_keys;
		}

		~CuckooFilter() {
			delete table_;
		}


		// Add an item to the filter.
		Status Add(const ItemType& item);
		Status Add(const uint32_t i, const uint32_t tag);


		// Report if the item is inserted, with false positive rate.
		Status Contain(const ItemType& item) const;
		Status Contain(const uint32_t i, const uint32_t tag, size_t* index) const;

		// Delete an key from the filter
		Status Delete(const ItemType& item);

		// Adapt to False Positive
		void AdaptFalsePositive(const size_t i);

		/* methods for providing stats  */
		// summary infomation
		std::string Info() const;

		// number of current inserted items;
		size_t Size() const { return num_items; }

		// size of the filter in bytes.
		size_t SizeInBytes() const { return table_->SizeInBytes(); }
	};


	template <typename ItemType, size_t bits_per_item,
			  template<size_t> class TableType>
	Status
	CuckooFilter<ItemType, bits_per_item, TableType>::Add(
			const ItemType& item) {
		size_t i;
		uint32_t tag;

		if (victim_.used) {
			return NotEnoughSpace;
		}

		GenerateIndexTagHash(item, &i, &tag);
		return AddImpl(i, tag);
	}

	template <typename ItemType, size_t bits_per_item,
			  template<size_t> class TableType>
	Status
	CuckooFilter<ItemType, bits_per_item, TableType>::Add(const uint32_t i, const uint32_t tag) {
		if (victim_.used) {
			return NotEnoughSpace;
		}
		return AddImpl(IndexHash(i), TagHash(tag));
	}

	template <typename ItemType, size_t bits_per_item,
			  template<size_t> class TableType>
	Status
	CuckooFilter<ItemType, bits_per_item, TableType>::AddImpl(
		const size_t i, const uint32_t tag) {
		size_t curindex = i;
		uint32_t curtag = tag;
		uint32_t oldtag;
		bool needClearNC = false;

		if(table_->IsBucketFalsePositive(i) ||
		   table_->IsBucketFalsePositive(AltIndex(i, tag))) {
			needClearNC = true;
		}

		for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
			bool kickout = count > 0;
			oldtag = 0;
			if (table_->InsertTagToBucket(curindex, curtag, kickout, oldtag)) {
				num_items++;
				if (needClearNC) {
					// Maybe insert the previous false positive item
					return MayClearNC;
				} else {
					return Ok;
				}
			}
			if (kickout) {
				curtag = oldtag;
			}
			curindex = AltIndex(curindex, curtag);
		}

		victim_.index = curindex;
		victim_.tag = curtag;
		victim_.used = true;

		if(needClearNC) {
			// Maybe insert the previous false positive item
			return MayClearNC;
		} else {
			return Ok;
		}
	}

	template <typename ItemType,
			  size_t bits_per_item,
			  template<size_t> class TableType>
	Status
	CuckooFilter<ItemType, bits_per_item, TableType>::Contain(
			const ItemType& key) const {
		bool found = false;
		size_t i1, i2;
		uint32_t tag;

		GenerateIndexTagHash(key, &i1, &tag);

		i2 = AltIndex(i1, tag);

		assert(i1 == AltIndex(i2, tag));

		found = victim_.used && (tag == victim_.tag) && 
			(i1 == victim_.index || i2 == victim_.index);

		if (found || table_->FindTagInBuckets(i1, i2, tag)) {
			return Ok;
		} else {
			return NotFound;
		}
	}

	template <typename ItemType,
			  size_t bits_per_item,
			  template<size_t> class TableType>
	Status
	CuckooFilter<ItemType, bits_per_item, TableType>::Contain(
			const uint32_t i, const uint32_t tag, size_t *index) const {
		bool found = false;
		size_t i2, t_i;
		uint32_t t_tag;
		int rv;

		t_i = IndexHash(i);
		t_tag = TagHash(tag);

		i2 = AltIndex(t_i, t_tag);
		assert(t_i == AltIndex(i2, t_tag));
		found = victim_.used && (t_tag == victim_.tag) && 
			(t_i == victim_.index || i2 == victim_.index);

		if(found) {
			return Ok;
		} else {
			rv = table_->FindTagInBuckets2(t_i, i2, t_tag);
			if(rv / 2 == 0)
				*index = t_i;
			else
				*index = i2;
			if ( rv == -1){ 
				return NotFound;
			} else if (rv % 2 == 1) {
				return Ok;
			} else {
				return NotSure;
			}
		}
	}


	template <typename ItemType,
			  size_t bits_per_item,
			  template<size_t> class TableType>
	Status
	CuckooFilter<ItemType, bits_per_item, TableType>::Delete(
			const ItemType& key) {
		size_t i1, i2;
		uint32_t tag;

		GenerateIndexTagHash(key, &i1, &tag);
		i2 = AltIndex(i1, tag);

		if (table_->DeleteTagFromBucket(i1, tag))  {
			num_items--;
			goto TryEliminateVictim;
		} else if (table_->DeleteTagFromBucket(i2, tag))  {
			num_items--;
			goto TryEliminateVictim;
		} else if (victim_.used && tag == victim_.tag &&
				 (i1 == victim_.index || i2 == victim_.index)) {
			//num_items--;
			victim_.used = false;
			return Ok;
		} else {
			return NotFound;
		}
	TryEliminateVictim:
		if (victim_.used) {
			victim_.used = false;
			size_t i = victim_.index;
			uint32_t tag = victim_.tag;
			AddImpl(i, tag);
		}
		return Ok;
	}

	template <typename ItemType,
			  size_t bits_per_item,
			  template<size_t> class TableType>
	void
	CuckooFilter<ItemType, bits_per_item, TableType>::AdaptFalsePositive(const size_t i) {
		table_->SetBucketFalsePositive(i);
	}

	template <typename ItemType,
			  size_t bits_per_item,
			  template<size_t> class TableType>
	std::string CuckooFilter<ItemType, bits_per_item, TableType>::Info() const {
		std::stringstream ss;
		ss << "CuckooFilter Status:\n"
#ifdef QUICK_N_DIRTY_HASHING
		   << "\t\tQuick hashing used\n"
#else
		   << "\t\tBob hashing used\n"
#endif
		   << "\t\t" << table_->Info() << "\n"
		   << "\t\tKeys stored: " << Size() << "\n"
		   << "\t\tLoad facotr: " << LoadFactor() << "\n"
		   << "\t\tHashtable size: " << (table_->SizeInBytes() >> 10)
		   << " KB\n";
		if (Size() > 0) {
			ss << "\t\tbit/key:   " << BitsPerItem() << "\n";
		} else {
			ss << "\t\tbit/key:   N/A\n";
		}
		return ss.str();
	}
}  // namespace cuckoofilter

#endif // #ifndef _CUCKOO_FILTER_H_
