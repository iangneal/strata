#ifndef __HASH_BASE_H__
#define __HASH_BASE_H__

#ifdef __cplusplus
extern "C" {
#endif

// MLFS includes
#include "../global/global.h"
#include "../balloc.h"
#include "../fs.h"

// Local includes

#if !(GLIB_HASH ^ CUCKOO_HASH)
#warning ("Should define either GLIB_HASH to 1 or CUCKOO_HASH to 1. " \
          "Defaulting to GLIB_HASH...")
#define GLIB_HASH 1
#define CUCKOO_HASH 0
#endif

#if GLIB_HASH
typedef struct  _hash_entry {
  mlfs_fsblk_t key;
  mlfs_fsblk_t value;
  mlfs_fsblk_t size;
  mlfs_fsblk_t _unused;
} hash_entry_t;

typedef struct _hash_table {
} hash_table_t;
#elif CUCKOO_HASH
#else
#error "Need to have a type of hash table selected!"
#endif

typedef struct _hash_functions {
  int _placeholder;
} hash_functions_t;

#ifdef __cplusplus
}
#endif

#endif  // __HASH_BASE_H__
