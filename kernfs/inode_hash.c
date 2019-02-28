#ifdef HASHTABLE

#include <stdbool.h>
#include "inode_hash.h"


#define GPOINTER_TO_UINT(x) ((uint64_t)x)

#define HASHPERF


mlfs_fsblk_t single_hash_meta_loc = 0;
mlfs_fsblk_t chunk_hash_meta_loc = 0;
mlfs_fsblk_t id_map_meta_loc = 0;

// (iangneal): Global hash table for all of NVRAM. Each inode has a point to
// this one hash table just for abstraction of the inode interface.
static GHashTable *ghash = NULL;
// (iangneal): Second level hash table.
static GHashTable *gsuper = NULL;

static idx_struct_t hash_idx;

static pthread_mutex_t alloc_tex = PTHREAD_MUTEX_INITIALIZER;
/*
 *
 */
void init_hash(struct super_block *sb) {
#ifndef USE_API
  if (ghash) return;

  assert(sb);


  // Calculate the locations of all the data structure metadata.
  single_hash_meta_loc = sb->ondisk->ndatablocks - 1;
  chunk_hash_meta_loc = single_hash_meta_loc - 1;
  id_map_meta_loc = chunk_hash_meta_loc - 1;

  // single block table
  ghash = g_hash_table_new(g_direct_hash, sb->ondisk->ndatablocks, 1,
      single_hash_meta_loc);
  if (!ghash) {
    panic("Failed to initialize the single-block hash table.\n");
  }

  printf("Finished initializing the single-block hash table.\n");

  // chunk (maps a range of blocks) hash table
  gsuper = g_hash_table_new(g_direct_hash, sb->ondisk->ndatablocks,
      RANGE_SIZE, chunk_hash_meta_loc);
  if (!gsuper) {
    panic("Failed to initialize multi-block hash table\n");
  }

  printf("Finished initializing the multi-block hash table.\n");
  printf("Finished initializing NVM hash table structures.\n");
#else

  printf("Initializing NVM hashtable... (sb = %p)\n", sb);
  paddr_t metadata_block = sb->ondisk->api_metadata_block;
  printf("metadata block %lu\n", metadata_block);
  int ret = hash_fns.im_init(&strata_idx_spec, &hash_idx, &metadata_block);
  if (ret) return;
  printf("Finished initializing NVM hashtable.\n");
#endif
}

#ifndef USE_API
inline int insert_hash(GHashTable *hash, struct inode *inode, hash_key_t key,
    hash_value_t value, hash_value_t size) {
  // if not exists, then the value was not already in the table, therefore
  // success.

  return g_hash_table_insert(hash, key, value, size);
}

/*
 * Returns 0 if not found (value == 0 means no associated value).
 */
int lookup_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t* value,
    hash_value_t *size, hash_value_t *index, bool force) {
  int ret = 0;
  hash_key_t k = MAKEKEY(inode, key);
  hash_key_t r = RANGE_KEY(inode->inum, key);

  *index = 0;
  // Two-level lookup
#if 0
  g_hash_table_lookup(gsuper, r, value, size, force);
  bool present = (*value) && ((key & RANGE_BITS) < *size);
  if (!present) {
    g_hash_table_lookup(ghash, k, value, size, force);
    present = (*value) != 0;
  } else {
    *index = (key & RANGE_BITS);
  }
#else
  g_hash_table_lookup(ghash, k, value, size, force);
  bool present = (*value) != 0;
  if (!present) {
    g_hash_table_lookup(gsuper, r, value, size, force);
    present = (*value) && ((key & RANGE_BITS) < *size);
    *index = (key & RANGE_BITS);
  }
#endif

  return (int) present;
}

/*
 * Returns FALSE if the requested logical block was not present in any of the
 * two hash tables.
 */
int erase_hash(struct inode *inode, mlfs_lblk_t key) {
  int ret = 0;
  hash_key_t k = MAKEKEY(inode, key);
  hash_key_t r = RANGE_KEY(inode->inum, key);

  if (!g_hash_table_remove(gsuper, r)) {
    return g_hash_table_remove(ghash, k);
  }

  return true;
}
#endif

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags, bool force) {
	int err = 0;
	mlfs_lblk_t allocated = 0;
	int create;

	mlfs_assert(handle);

	create = flags & MLFS_GET_BLOCKS_CREATE_DATA;
  int ret = 0;
  map->m_pblk = 0;

  // lookup all blocks.
  uint32_t len = map->m_len;
  bool set = false;

  for (mlfs_lblk_t i = 0; i < max(map->m_len, 1); ) {
#ifndef USE_API
    hash_value_t index = 0;
    hash_value_t value = 0;
    hash_value_t size  = 0;
    int pre = lookup_hash(inode, map->m_lblk + i, &value, &size, &index, force);
#else
    paddr_t value = 0;
    hash_value_t index = 0; // to make life easier
    ssize_t size = FN(&hash_idx, im_lookup,
                      &hash_idx, inode->inum, map->m_lblk + i, &value);
    bool pre = size > 0;
#endif
    //printf("LDOWN value = %llu size = %llu index = %llu\n", value, size, index);
    if (!pre) {
      goto create;
    }

    if (!set) {
      //printf("Setting to %lu + %lu\n", value, index);
      map->m_pblk = value + index;
      set = true;
    } else if (value + index != map->m_pblk + i) {
      // only return contiguous ranges
      return i;
    }

    len -= size - index;
    i += size - index;
    ret = i;
  }

  ret = min(ret, map->m_len);

  return ret;

create:
  if (create) {

#ifndef USE_API
    mlfs_fsblk_t blockp;
    struct super_block *sb = get_inode_sb(handle->dev, inode);
    enum alloc_type a_type;

    if (flags & MLFS_GET_BLOCKS_CREATE_DATA_LOG) {
      a_type = DATA_LOG;
    } else if (flags & MLFS_GET_BLOCKS_CREATE_META) {
      a_type = TREE;
    } else {
      a_type = DATA;
    }

    pthread_mutex_lock(&alloc_tex);
    mlfs_lblk_t lb = map->m_lblk + (map->m_len - len);
    int r = mlfs_new_blocks(sb, &blockp, len, 0, 0, a_type, 0);
    if (r > 0) {
      bitmap_bits_set_range(sb->s_blk_bitmap, blockp, r);
      sb->used_blocks += r;
    } else if (r == -ENOSPC) {
      panic("Failed to allocate block -- no space!\n");
    } else if (r == -EINVAL) {
      panic("Failed to allocate block -- invalid arguments!\n");
    } else {
      panic("Failed to allocate block -- unknown error!\n");
    }

    ret = r;
    pthread_mutex_unlock(&alloc_tex);

    //printf("Starting insert: %u, %lu, %lu\n", map->m_lblk, map->m_len, len);
    for (int c = 0; c < ret; ) {
      int offset = ((lb + c) & RANGE_BITS);
      int aligned = offset == 0;
      if (unlikely(ret >= (RANGE_SIZE / 2) && aligned)) {
        /*
         * It's possible part of the range has already been allocated.
         * Say if someone requests (RANGE_SIZE + 1) blocks, but the blocks from
         * (RANGE_SIZE, RANGE_SIZE + RANGE_SIZE) have already been allocated,
         * we need to skip the last block.
         */
        hash_value_t index;
        hash_value_t value;
        hash_value_t size;
        // Doesn't make sense to do this on the first pass though, we just
        // looked it up.
        if (c > 0) {
          int pre = lookup_hash(inode, lb + c, &value, &size, &index, force);
          if (pre) {
            c += size;
            if (!set) {
              map->m_pblk = value + index;
              set = true;
            }
            continue;
          }
        }

        uint32_t nblocks = min(ret - c, RANGE_SIZE);
        //printf("Insert to big.\n");
        hash_key_t k = RANGE_KEY(inode->inum, lb + c);
        int already_exists = !insert_hash(gsuper, inode, k, blockp, nblocks);
        if (already_exists) {
          //panic("Could not insert huge range!\n");
          printf("Weird, already exists?\n");
        }

        if (!set) {
          map->m_pblk = blockp;
          set = true;
        }

        c += nblocks;
        blockp += nblocks;

      } else {
        uint32_t nblocks_to_alloc = min(ret - c, RANGE_SIZE - offset);

        if (!set) {
          map->m_pblk = blockp;
          set = true;
        }

        //printf("Insert to small: %u.\n", nblocks_to_alloc);
        for (uint32_t i = 0; i < nblocks_to_alloc; ++i) {
          hash_key_t k = MAKEKEY(inode, lb + i + c);
          int already_exists = !insert_hash(ghash, inode, k, blockp + i,
              nblocks_to_alloc - i);

          if (already_exists) {
            fprintf(stderr, "could not insert: key = %u, val = %0lx, already exists (small)\n",
                lb + i + c, blockp);
            //panic("Could not insert into small table!");
          }

        }

        c += nblocks_to_alloc;
        blockp += nblocks_to_alloc;
      }
    }
#else
      inum_t inum   = inode->inum;
      laddr_t lblk  = map->m_lblk + ret;
      size_t nalloc = map->m_len - ret;
      paddr_t pblk  = 0;

      ssize_t nret = FN(&hash_idx, im_create,
                        &hash_idx, inum, lblk, nalloc, &pblk);

      if (nret < 0) {
          printf("im_create returned %ld, or %s\n", nret, strerror(-nret));
          panic("wat");
      }
      ret += nret;
      map->m_pblk = pblk;
#endif

    mlfs_hash_persist();
  }

  ret = min(ret, map->m_len);

  return ret;
}

int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end) {
  hash_value_t size, value, index;

#ifndef USE_API
  // TODO: probably inefficient
  for (mlfs_lblk_t i = start; i <= end;) {
    if (lookup_hash(inode, i, &value, &size, &index, false)) {
      mlfs_fsblk_t pblock = value + index;
      mlfs_free_blocks(handle, inode, NULL, pblock, size, 0);
      erase_hash(inode, i);
      i += size;
    }
  }
#else
  size_t  nremove  = (end - start) + 1;
  ssize_t nremoved = FN(&hash_idx, im_remove,
                        &hash_idx, inode->inum, start, nremove);
  if_then_panic(nremove != nremoved, "Could not remove all blocks!");
#endif

  //return mlfs_hash_persist();
  return 0;
}

double check_load_factor(struct inode *inode) {
  double load = 0.0;
  double allocated_size = (double)ghash->size;
  double current_size = (double)ghash->noccupied;
  load = current_size / allocated_size;
  return load;
}

int mlfs_hash_persist() {
#if 1
  return 0;
#else
  pthread_mutex_lock(ghash->metalock);
  pthread_mutex_lock(gsuper->metalock);

  sync_all_buffers(g_bdev[g_root_dev]);
  nvram_flush(ghash);
  nvram_flush(gsuper);

  pthread_mutex_unlock(gsuper->metalock);
  pthread_mutex_unlock(ghash->metalock);

  return 0;
#endif
}

// TODO: probably could keep track of this with a bitmap or something, but this
// is very easy to implement
int mlfs_hash_cache_invalidate() {
#if !defined(USE_API) && defined(HASHCACHE)
  bitmap_set(ghash->cache_bitmap, 0, ghash->cache_bitmap_size);
  bitmap_set(gsuper->cache_bitmap, 0, gsuper->cache_bitmap_size);
#elif defined(USE_API) && defined(HASHCACHE)
  printf("TODO implement invalidate for API!\n");
#endif
}

#endif
