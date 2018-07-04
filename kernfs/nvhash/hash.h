#ifndef __NVHASH_HASH_H__
#define __NVHASH_HASH_H__

#ifdef __cplusplus
extern "C" {
#endif

// System
#include <errno.h>

// Local
#include "ghash.h"

/*
 * hash_lock_handle_t
 */
typedef struct _hash_lock_handle {
  size_t             hlh_num_locks;
  size_t             hlh_entries_per_lock;
  pthread_rwlock_t **hlh_entry_locks;
  pthread_mutex_t   *hlh_metadata_lock;
} hash_lock_handle_t;

int hlh_initialize(hash_lock_handle_t **hlh,
                   size_t num_locks,
                   size_t entries_per_lock);

void hlh_destroy(hash_lock_handle_t *hlh);

int hlh_lock_entry(hash_lock_handle_t *hlh,
                   size_t entry_index,
                   bool is_write);

int hlh_unlock_entry(hash_lock_handle_t *hlh,
                     size_t entry_index);

/*
 * hash_handle_t
 */

typedef struct _hash_handle {
  hash_table_t hh_main_table;
  hash_table_t hh_gc_table;
  hash_lock_handle_t hh_lock_handle;
  pthread_t gc_thread;
  sig_atomic_t gc_in_progress;
} hash_handle_t;

#ifdef __cplusplus
}
#endif

#endif  // __NVHASH_HASH_H__
