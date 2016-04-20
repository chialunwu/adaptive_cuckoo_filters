/*
Header File for hash_library_functions.cpp
Akshay Khole, August 2012.
CSS 2.0
 */
#ifndef _HASH_FUNCTIONS_H_
#define _HASH_FUNCTIONS_H_

#include "fnv.h"
#include "MurmurHash3.h"


Fnv32_t fnv_32a_str(char *str, Fnv32_t hval, int object_key_len);

#endif
