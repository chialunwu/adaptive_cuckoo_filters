#include "hash_functions.h"
#include <stdio.h>

Fnv32_t
fnv_32a_str(char *str, Fnv32_t hval, int object_key_len)
{
    unsigned char *s = (unsigned char *)str;	/* unsigned string */

    /*
     * FNV-1a hash each octet in the buffer
     */
    while (object_key_len) {

	/* xor the bottom with the current octet */
	hval ^= (Fnv32_t)*s++;

	/* multiply by the 32 bit FNV magic prime mod 2^32 */
#if defined(NO_FNV_GCC_OPTIMIZATION)
	hval *= FNV_32_PRIME;
#else
	hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);
#endif
object_key_len-=1;
    }

    /* return our new hash value */
    return hval;
}

