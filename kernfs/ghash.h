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

// For the big hash table, mapping (inode, lblk) -> single block
typedef struct  _hash_entry {
  mlfs_fsblk_t key;
  mlfs_fsblk_t value;
  mlfs_fsblk_t size;
} hash_entry_t;

#define KB(x)   ((size_t) (x) << 10)
#define MB(x)   ((size_t) (x) << 20)

//#define RANGE_SIZE (1 << 5) // 32
#define RANGE_SIZE (1 << 9) // 512 -- 2MB
#define RANGE_BITS (RANGE_SIZE - 1)
#define RANGE_MASK (~RANGE_BITS)
#define RANGE_KEY(i, l) ( (((uint64_t)(i)) << 32) | ((l) & RANGE_MASK))

#define UNUSED_HASH_VALUE 0
#define TOMBSTONE_HASH_VALUE 1
#define HASH_IS_UNUSED(h_) ((h_) == UNUSED_HASH_VALUE)
#define HASH_IS_TOMBSTONE(h_) ((h_) == TOMBSTONE_HASH_VALUE)
#define HASH_IS_REAL(h_) ((h_) >= 2)

#define VTOMB (~0UL)
#define VEMPTY (0UL)
#define IS_TOMBSTONE(v) ((v) == VTOMB)
#define IS_EMPTY(v) ((v) == VEMPTY)
#define IS_VALID(v) (!(IS_TOMBSTONE(v) || IS_EMPTY(v)))

/*
#define HASH_IS_UNUSED(value) ((value) == UNUSED_HASH_VALUE)
#define HASH_IS_TOMBSTONE(value) ((value) == TOMBSTONE_HASH_VALUE)
#define HASH_IS_REAL(value) (!(HASH_IS_UNUSED(value) || HASH_IS_TOMBESTON(value)))
*/

#define BUF_SIZE (g_block_size_bytes / sizeof(hash_entry_t))
#define BUF_IDX(x) (x % (g_block_size_bytes / sizeof(hash_entry_t)))
#define NV_IDX(x) (x / (g_block_size_bytes / sizeof(hash_entry_t)))



#define HASHCACHE

// This is the hash table meta-data that is persisted to NVRAM, that we may read
// it and know everything we need to know in order to reconstruct it in memory.
struct dhashtable_meta {
  // Metadata for the in-memory state.
  gint size;
  gint mod;
  guint mask;
  gint nnodes;
  gint noccupied;
  // Metadata about the on-disk state.
  mlfs_fsblk_t nvram_size;
  mlfs_fsblk_t range_size;
  mlfs_fsblk_t data_start;
};

typedef struct _bh_cache_node {
  uint64_t cache_index;
  struct _bh_cache_node *next;
} bh_cache_node;


typedef struct _GHashTable {
  int             size;
  int             mod;
  unsigned        mask;
  int             nnodes;
  int             noccupied;  /* nnodes + tombstones */

  mlfs_fsblk_t     data;
  mlfs_fsblk_t     nvram_size;
  size_t           range_size;
  pthread_mutex_t *mutexes;

  GHashFunc        hash_func;
  GEqualFunc       key_equal_func;
  int              ref_count;

  // concurrency
  pthread_rwlock_t *locks;
  pthread_mutex_t *metalock;

  // caching
#ifdef HASHCACHE
  // array of blocks
  hash_entry_t **cache;
  // cache of buffer heads to reduce search time
  // struct buffer_head == bh_t
  bh_t **bh_cache;
  // list of cache locations to override on flush
  bh_cache_node *bh_cache_head;
#endif
} GHashTable;



typedef int (*GHRFunc) (void* key,
                        void* value,
                        void* user_data);


GHashTable* g_hash_table_new(GHashFunc    hash_func,
                             size_t       max_entries,
                             size_t       range_size,
                             mlfs_fsblk_t metadata_location);

void  g_hash_table_destroy(GHashTable     *hash_table);

int g_hash_table_insert(GHashTable *hash_table,
                        mlfs_fsblk_t       key,
                        mlfs_fsblk_t       value,
                        mlfs_fsblk_t       range);

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

void g_hash_table_lookup(GHashTable *hash_table, mlfs_fsblk_t key,
    mlfs_fsblk_t *val, mlfs_fsblk_t *size);

int g_hash_table_contains(GHashTable *hash_table,
                          mlfs_fsblk_t key);

void g_hash_table_foreach(GHashTable *hash_table,
                          GHFunc      func,
                          void*       user_data);

void* g_hash_table_find(GHashTable *hash_table,
                        GHRFunc     predicate,
                        void*       user_data);


unsigned g_hash_table_size(GHashTable *hash_table);

void** g_hash_table_get_keys_as_array(GHashTable *hash_table,
                                      unsigned *length);


/* Hash Functions
 */

unsigned g_direct_hash (const void *v);

uint64_t reads;
uint64_t writes;
uint64_t blocks;

/*
 * Read a NVRAM block and give the users a reference to our cache (saves a
 * memcpy).
 * Used to read buckets and potentially iterate over them.
 *
 * buf -- reference to cache page. Don't free!
 * offset -- needs to be block aligned!
 * force -- refresh the cache from NVRAM.
 */
static void
nvram_read(GHashTable *ht, mlfs_fsblk_t offset, hash_entry_t **buf, bool force) {
  struct buffer_head *bh;
  int err;

  /*
   * Do some caching!
   */
#ifdef HASHCACHE
  // if NULL, then it got invalidated or never loaded.
  if (!force) {
    if (ht->cache[offset]) {
      //memcpy((uint8_t*)buf, (uint8_t*)ht->cache[offset], g_block_size_bytes);
      *buf = ht->cache[offset];
      return;
    } else {
      ht->cache[offset] = (hash_entry_t*)malloc(g_block_size_bytes);
      assert(ht->cache[offset]);
      *buf = ht->cache[offset];
    }
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
  nvram_read(ht, offset, &buf, 0);
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
nvram_read_metadata(GHashTable *hash, mlfs_fsblk_t location) {
  struct buffer_head *bh;
  struct dhashtable_meta metadata;
  int err, ret = 0;

  // TODO: maybe generalize this.
  // size - 1 for the last block (where we will allocate the hashtable)
  bh = bh_get_sync_IO(g_root_dev, location, BH_NO_DATA_ALLOC);
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
    assert(hash->range_size == metadata.range_size);
    // reconsititute the rest of the hashtable from
    hash->size = metadata.size;
    hash->range_size = metadata.range_size;
    hash->mod = metadata.mod;
    hash->mask = metadata.mask;
    hash->nnodes = metadata.nnodes;
    hash->noccupied = metadata.noccupied;
    hash->data = metadata.data_start;
  }

  bh_release(bh);

  return ret;
}

#ifdef HASHCACHE
/**
 * Assumes sync_all_buffers has been called!
 */
static inline void nvram_flush(GHashTable *ht) {
  if (ht->bh_cache_head) writes++;

  while(ht->bh_cache_head) {
    /*
     * The bitmap is zeroed as cachelines are written back (in mlfs_write),
     * so by this point the bitmap is completely zeroed out.
     */
    bh_cache_node *tmp = ht->bh_cache_head;
    bh_t *buf = ht->bh_cache[tmp->cache_index];
    buf->b_use_bitmap = 0;
    ht->bh_cache[tmp->cache_index] = NULL;
    ht->bh_cache_head = tmp->next;
    free(tmp);
    blocks++;
  }
}
#endif

static int
nvram_write_metadata(GHashTable *hash, mlfs_fsblk_t location) {
  struct buffer_head *bh;
  struct super_block *super = sb[g_root_dev];
  int ret;
  // Set up the hash table metadata
  struct dhashtable_meta metadata;
  metadata.nvram_size = hash->nvram_size;
  metadata.size = hash->size;
  metadata.range_size = hash->range_size;
  metadata.mod = hash->mod;
  metadata.mask = hash->mask;
  metadata.nnodes = hash->nnodes;
  metadata.noccupied = hash->noccupied;
  metadata.data_start = hash->data;


  // TODO: maybe generalize for other devices.
  bh = bh_get_sync_IO(g_root_dev, location, BH_NO_DATA_ALLOC);
  assert(bh);

  bh->b_size = sizeof(metadata);
  bh->b_data = (uint8_t*)&metadata;
  bh->b_offset = 0;

  ret = mlfs_write(bh);

  assert(!ret);
  bh_release(bh);

  // Actually mark block as allocated.
  bitmap_bits_set_range(super->s_blk_bitmap, location, 1);
  super->used_blocks += 1;
}

static mlfs_fsblk_t
nvram_alloc_range(size_t count) {
  int err;
  // TODO: maybe generalize this for other devices.
  struct super_block *super = sb[g_root_dev];
  mlfs_fsblk_t block;

  err = mlfs_new_blocks(super, &block, count, 0, 0, DATA, 0);
  if (err < 0) {
    fprintf(stderr, "Error: could not allocate new blocks: %d\n", err);
  }

  // Mark superblock bits
  bitmap_bits_set_range(super->s_blk_bitmap, block, count);
  super->used_blocks += count;

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

  ht->cache[NV_IDX(index)][BUF_IDX(index)] = *val;

  // check if we've seen this buffer head before. if not, we need to fetch
  // it and point to our cache page.
  if (!ht->bh_cache[NV_IDX(index)]) {
    // Set up the buffer head -- once we point it to a cache page, we don't
    // need to do this again, just manipulate the page.
    bh = sb_getblk(g_root_dev, block_addr);

    bh->b_data = (uint8_t*)ht->cache[NV_IDX(index)];
    bh->b_size = 0;
    bh->b_offset = 0;

    mlfs_assert(bh->b_dirty_bitmap);

    set_buffer_dirty(bh);
    brelse(bh);

    // Now we need to set up book-keeping.
    ht->bh_cache[NV_IDX(index)] = bh;

    bh_cache_node *tmp = (bh_cache_node*)malloc(sizeof(*tmp));

    if (unlikely(!tmp)) panic("ENOMEM");

    tmp->cache_index = NV_IDX(index);
    tmp->next = ht->bh_cache_head;
    ht->bh_cache_head = tmp;
  } else {
    bh = ht->bh_cache[NV_IDX(index)];
  }

  // Set bitmap cachelines.
  bh->b_use_bitmap = 1;

  uint64_t size = sizeof(hash_entry_t);
  uint64_t bit_start = (BUF_IDX(index) * size);

  /*
   * It's possible that these entries overflow into two "cache lines", since
   * they aren't aligned to units of 64 bytes.
   */
  size_t bit1 = bit_start / bh->b_cacheline_size;
  size_t bit2 = (bit_start + size) / bh->b_cacheline_size;
  bit2 = bit1 == bit2 ? 0 : bit2;

  /*
   * Tracking to make sure we write back the same number of dirty regions.
   * Test and set so we don't double count.
   */
  bh->b_size += !__test_and_set_bit(bit1, bh->b_dirty_bitmap);
  if (bit2) {
    bh->b_size += !__test_and_set_bit(bit2, bh->b_dirty_bitmap);
  }
}

#ifdef __cplusplus
}
#endif

#endif /* __G_HASH_MOD_H__ */
