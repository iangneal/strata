#ifndef __HASH_LOG_H__
#define __HASH_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "hash_base.h"

typedef struct _hash_log_entry {
  uint64_t hl_hash_table_id;
  uint64_t hl_hash_table_index;
  hash_entry_t hl_prev_entry;
  hash_entry_t hl_new_entry;
} hash_log_entry_t;

/*
 * CLEAN: No operations are pending.
 * DIRTY: Operation is being logged.
 * IN_PROGRESS: Operation has been logged but not persisted.
 * UNINITIALIZED: Have yet to allocate space for the hash log.
 */
typedef enum _hash_log_status {
  CLEAN, DIRTY, IN_PROGRESS, UNINITIALIZED
} hash_log_status_t;

typedef struct _hash_log_header {
  hash_log_status_t hl_status;
  size_t hl_max_entries;
  mlfs_fsblk_t hl_nvram_start_block;
} hash_log_header_t;

#ifdef __cplusplus
}
#endif

#endif  // __HASH_LOG_H__
