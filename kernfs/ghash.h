/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

#ifndef __G_HASH_MOD_H__
#define __G_HASH_MOD_H__

#ifdef __cplusplus
extern "C" {
#endif

// MLFS includes
#include "global/global.h"
#include "balloc.h"
#include "fs.h"

// Glib stuff
#include "gtypes.h"

typedef struct {
  mlfs_fsblk_t key;
  mlfs_fsblk_t hash;
  mlfs_fsblk_t value;
  mlfs_fsblk_t _dummy; // used for alignment for NVRAM.
} hash_entry_t;

#define BUF_SIZE (g_block_size_bytes / sizeof(hash_entry_t))
#define BUF_IDX(x) (x % (g_block_size_bytes / sizeof(hash_entry_t)))
#define NV_IDX(x) (x / (g_block_size_bytes / sizeof(hash_entry_t)))

#define HASHCACHE

// This is the hash table meta-data that is persisted to NVRAM, that we may read
// it and know everything we need to know in order to reconstruct it in memory.
struct dhashtable_meta {
  // To check if it's been initialized.
  uint32_t magic1;
  uint32_t magic2;
  // Metadata for the in-memory state.
  gint size;
  gint mod;
  guint mask;
  gint nnodes;
  gint noccupied;
  // Metadata about the on-disk state.
  mlfs_fsblk_t nvram_size;
  //mlfs_fsblk_t keys_start;
  //mlfs_fsblk_t hashes_start;
  //mlfs_fsblk_t values_start;
  mlfs_fsblk_t data_start;
};

typedef struct _GHashTable
{
  int             size;
  int             mod;
  unsigned        mask;
  int             nnodes;
  int             noccupied;  /* nnodes + tombstones */

  //mlfs_fsblk_t     keys;
  //mlfs_fsblk_t     hashes;
  //mlfs_fsblk_t     values;
  mlfs_fsblk_t     data;
  mlfs_fsblk_t     nvram_size;
  pthread_mutex_t *mutexes;

  GHashFunc        hash_func;
  GEqualFunc       key_equal_func;
  int              ref_count;

  GDestroyNotify   key_destroy_func;
  GDestroyNotify   value_destroy_func;

  // concurrency
  pthread_rwlock_t *locks;
  pthread_mutex_t *metalock;

  // caching
#ifdef HASHCACHE
  // array of blocks
  hash_entry_t **cache;
  // dirty block
  int dirty;
#endif
} GHashTable;

typedef int (*GHRFunc) (void* key,
                        void* value,
                        void* user_data);


GHashTable* g_hash_table_new(GHashFunc  hash_func,
                             GEqualFunc key_equal_func,
                             size_t     max_entries);

GHashTable* g_hash_table_new_full(GHashFunc      hash_func,
                                  GEqualFunc     key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func,
                                  size_t         max_entries);

void  g_hash_table_destroy(GHashTable     *hash_table);

int g_hash_table_insert(GHashTable *hash_table,
                        mlfs_fsblk_t       key,
                        mlfs_fsblk_t       value);

int g_hash_table_replace(GHashTable *hash_table,
                         mlfs_fsblk_t key,
                         mlfs_fsblk_t value);

int g_hash_table_add(GHashTable *hash_table,
                     mlfs_fsblk_t key);

int g_hash_table_remove(GHashTable *hash_table,
                        mlfs_fsblk_t key);

void g_hash_table_remove_all(GHashTable *hash_table);

int g_hash_table_steal(GHashTable *hash_table,
                       mlfs_fsblk_t key);

void g_hash_table_steal_all(GHashTable *hash_table);

mlfs_fsblk_t g_hash_table_lookup(GHashTable *hash_table, mlfs_fsblk_t key);

int g_hash_table_contains(GHashTable *hash_table,
                          mlfs_fsblk_t key);

int g_hash_table_lookup_extended(GHashTable *hash_table,
                                 const void *lookup_key,
                                 void       **orig_key,
                                 void       **value);

void g_hash_table_foreach(GHashTable *hash_table,
                          GHFunc      func,
                          void*       user_data);

void* g_hash_table_find(GHashTable *hash_table,
                        GHRFunc     predicate,
                        void*       user_data);


unsigned g_hash_table_size(GHashTable *hash_table);

void** g_hash_table_get_keys_as_array(GHashTable *hash_table,
                                      unsigned *length);


GHashTable* g_hash_table_ref(GHashTable *hash_table);

void g_hash_table_unref(GHashTable *hash_table);

/* Hash Functions
 */

int g_str_equal(const void *v1,
                const void *v2);

unsigned g_str_hash(const void *v);


int g_int_equal(const void *v1,
                const void *v2);

unsigned g_int_hash(const void *v);


int g_int64_equal(const void *v1,
                  const void *v2);

unsigned g_int64_hash(const void *v);

unsigned g_direct_hash (const void *v);

int g_direct_equal(const void *v1,
                   const void *v2);

uint64_t reads;
uint64_t writes;

/*
 * Read a NVRAM block and stash the results into a user-provided buffer.
 * Used to read buckets and potentially iterate over them.
 *
 * buf -- reference to cache page. Don't free!
 * offset -- needs to be block aligned!
 */
static void
nvram_read(GHashTable *ht, mlfs_fsblk_t offset, hash_entry_t **buf) {
  struct buffer_head *bh;
  int err;

  /*
   * Do some caching!
   */
#ifdef HASHCACHE
  // if NULL, then it got invalidated or never loaded.
  if (ht->cache[offset]) {
    //memcpy((uint8_t*)buf, (uint8_t*)ht->cache[offset], g_block_size_bytes);
    *buf = ht->cache[offset];
    return;
  }

  if (!ht->cache[offset]) {
    ht->cache[offset] = (hash_entry_t*)malloc(g_block_size_bytes);
    assert(ht->cache[offset]);
    *buf = ht->cache[offset];
  }
#endif

  bh = bh_get_sync_IO(g_root_dev, ht->data + offset, BH_NO_DATA_ALLOC);
  bh->b_offset = 0;
  bh->b_size = g_block_size_bytes;
  bh->b_data = (uint8_t*)ht->cache[offset];
  bh_submit_read_sync_IO(bh);

  // uint8_t dev, int read (enables read)
  err = mlfs_io_wait(g_root_dev, 1);
  assert(!err);

  bh_release(bh);
  reads++;
}

/*
 * Convenience wrapper for when you need to look up the single value within
 * the block and nothing else. Index is offset from start (bytes).
 */
static void
nvram_read_entry(GHashTable *ht, mlfs_fsblk_t idx, hash_entry_t *ret) {
  mlfs_fsblk_t offset = NV_IDX(idx);
  hash_entry_t *buf;
  nvram_read(ht, offset, &buf);
  *ret = buf[BUF_IDX(idx)];
}
/*
 * Read the hashtable metadata from disk. If the size is zero, then we need to
 * allocate the table. Otherwise, the structures have already been
 * pre-allocated.
 *
 * Returns 1 on success, 0 on failure.
 */
static int
nvram_read_metadata(GHashTable *hash, size_t nvram_size) {
  struct buffer_head *bh;
  struct dhashtable_meta metadata;
  int err;
  int ret;

  // TODO: maybe generalize this.
  // size - 1 for the last block (where we will allocate the hashtable)
  bh = bh_get_sync_IO(g_root_dev, nvram_size - 1, BH_NO_DATA_ALLOC);
  bh->b_size = sizeof(metadata);
  bh->b_data = (uint8_t*)&metadata;
  bh->b_offset = 0;
  bh_submit_read_sync_IO(bh);

  // uint8_t dev, int read (enables read)
  err = mlfs_io_wait(g_root_dev, 1);
  assert(!err);

  // now check the actual metadata

  if (metadata.size > 0) {
    ret = 1;
    assert(hash->nvram_size == metadata.nvram_size);
    // reconsititute the rest of the hashtable from
    hash->size = metadata.size;
    hash->mod = metadata.mod;
    hash->mask = metadata.mask;
    hash->nnodes = metadata.nnodes;
    hash->noccupied = metadata.noccupied;
    hash->data = metadata.data_start;
  } else {
    // we need to do all of the allocation
    ret = 0;
    // first, need to actuall allocate the last block for the hashtable
    // TODO: GOAL is unused -- should fix
    /*
    err = mlfs_new_blocks(super, &block, count, 0, 0, DATA, 0);
    if (err < 0) {
      fprintf(stderr, "Error: could not allocate new blocks: %d\n", err);
    }

    assert(err >= 0);
    assert(err == count);
    */
  }

  bh_release(bh);


  return ret;
}

#ifdef HASHCACHE
static inline void nvram_flush(GHashTable *ht) {
  struct buffer_head *bh;
  int ret;

  if (ht->dirty >= 0) {
    assert(ht->cache[ht->dirty]);
    // TODO: maybe generalize for other devices.
    bh = bh_get_sync_IO(g_root_dev, ht->data + ht->dirty, BH_NO_DATA_ALLOC);
    assert(bh);

    bh->b_data = (uint8_t*)ht->cache[ht->dirty];
    bh->b_size = g_block_size_bytes;
    bh->b_offset = 0;

    ret = mlfs_write(bh);
    assert(!ret);
    bh_release(bh);
    writes++;

    ht->dirty = -1;
  }
}
#endif

static int
nvram_write_metadata(GHashTable *hash, size_t nvram_size) {
  struct buffer_head *bh;
  int ret;
  // Set up the hash table metadata
  struct dhashtable_meta metadata = {
    .nvram_size = hash->nvram_size,
    .size = hash->size,
    .mod = hash->mod,
    .mask = hash->mask,
    .nnodes = hash->nnodes,
    .noccupied = hash->noccupied,
    .data_start = hash->data
  };


  // TODO: maybe generalize for other devices.
  bh = bh_get_sync_IO(g_root_dev, nvram_size - 1, BH_NO_DATA_ALLOC);
  assert(bh);

  bh->b_size = sizeof(metadata);
  bh->b_data = (uint8_t*)&metadata;
  bh->b_offset = 0;

  ret = mlfs_write(bh);

  assert(!ret);
  bh_release(bh);
}

static mlfs_fsblk_t
nvram_alloc_range(size_t count) {
  int err;
  // TODO: maybe generalize this.
  struct super_block *super = sb[g_root_dev];
  mlfs_fsblk_t block;

  err = mlfs_new_blocks(super, &block, count, 0, 0, DATA, 0);
  if (err < 0) {
    fprintf(stderr, "Error: could not allocate new blocks: %d\n", err);
  }

#if 0
  printf("allocing range: %lu blocks (block size: %u, shift = %u)\n", count,
      g_block_size_bytes, g_block_size_shift);
  for (uint64_t i = 0; i < count; ++i) {
    for (uint64_t j = 0; j < g_block_size_bytes / sizeof(mlfs_fsblk_t); ++j) {
      mlfs_fsblk_t byte_index = (i << g_block_size_shift) + j;
      mlfs_fsblk_t entry = nvram_read_entry(block, byte_index);
      printf("-- %d[%d], %0lx\n", i, j, entry);
    }
  }
#endif
  assert(err >= 0);
  assert(err == count);

  return block;
}

/*
 * Update a single slot in NVRAM.
 * Used to insert or update a key -- since we'll never need to modify keys
 * en-masse, this will be fine.
 *
 * start: block address of range.
 * index: byte index into range.
 */
static inline void
nvram_update(GHashTable *ht, mlfs_fsblk_t index, hash_entry_t* val) {
  struct buffer_head *bh;
  int ret;

  mlfs_fsblk_t block_addr = ht->data + NV_IDX(index);
  mlfs_fsblk_t block_offset = BUF_IDX(index) * sizeof(hash_entry_t);

#ifdef HASHCACHE
  if (ht->cache[NV_IDX(index)]) {
    ht->cache[NV_IDX(index)][BUF_IDX(index)] = *val;

    if (ht->dirty >= 0 && ht->dirty != NV_IDX(index)) {
      nvram_flush(ht);
    }

    ht->dirty = NV_IDX(index);

    return;
  }
#endif

  // TODO: maybe generalize for other devices.
  bh = bh_get_sync_IO(g_root_dev, block_addr, BH_NO_DATA_ALLOC);
  assert(bh);

  bh->b_data = (uint8_t*)val;
  bh->b_size = sizeof(hash_entry_t);
  bh->b_offset = block_offset;

  ret = mlfs_write(bh);
  assert(!ret);
  bh_release(bh);
  writes++;
}

#ifdef __cplusplus
}
#endif

#endif /* __G_HASH_MOD_H__ */
