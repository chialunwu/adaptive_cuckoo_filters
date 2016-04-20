/*
Header File for hash_library_functions.cpp
Akshay Khole, August 2012.
CSS 2.0
 */
#ifndef _HASH_FUNCTIONS_H_
#define _HASH_FUNCTIONS_H_

#include "fnv.h"


Fnv32_t fnv_32a_str(char *str, Fnv32_t hval, int object_key_len);

void MurmurHash3_x86_32 ( const void * key, int len, uint32_t seed, void * out );

#endif
