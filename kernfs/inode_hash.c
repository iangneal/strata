#ifdef HASHTABLE

#include <stdbool.h>
#include "inode_hash.h"

#include "ghash.h"

#define GPOINTER_TO_UINT(x) ((uint64_t)x)

// (iangneal): Global hash table for all of NVRAM. Each inode has a point to
// this one hash table just for abstraction of the inode interface.
static GHashTable *ghash = NULL;

void init_hash(struct inode *inode) {
  //TODO: init in NVRAM.
  if (!ghash) {
    printf("INIT HASH!!!\n");
    struct super_block *sb = get_inode_sb(inode->dev, inode);
    ghash = g_hash_table_new(g_direct_hash, g_direct_equal, sb->num_blocks);
    if (!ghash) {
      panic("Failed to initialize inode hashtable\n");
    }
  }
}

int insert_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t value) {
  int ret = 0;
  hash_key_t k = MAKEKEY(inode, key);
  //gboolean exists = g_hash_table_insert(ghash, GKEY2PTR(k), GVAL2PTR(value));
  gboolean exists = g_hash_table_insert(ghash, k, value);
  // if not exists, then the value was not already in the table, therefore
  // success.
  return (int)exists;
}

/*
 * Returns 0 if not found (value == 0 means no associated value).
 */
int lookup_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t* value) {
  int ret = 0;
  hash_key_t k = MAKEKEY(inode, key);
  *value = g_hash_table_lookup(ghash, k);
  return *value != 0;
}

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags) {
	int err = 0;
	mlfs_lblk_t allocated = 0;
	int create;

	mlfs_assert(handle);

	create = flags & MLFS_GET_BLOCKS_CREATE_DATA;
  int ret = map->m_len;

  // lookup all blocks.
  uint32_t len = map->m_len;
  bool set = false;

  for (mlfs_lblk_t i = 0; i < map->m_len; ) {
    hash_value_t value;
    int pre = lookup_hash(inode, map->m_lblk + i, &value);
    if (!pre) {
      goto create;
    }

    if (SPECIAL(value)) {
      len -= MAX_CONTIGUOUS_BLOCKS - INDEX(value);
      i += MAX_CONTIGUOUS_BLOCKS - INDEX(value);
      if (!set) {
        map->m_pblk = ADDR(value) + INDEX(value);
        set = true;
      }
    } else {
      --len;
      ++i;
      if (!set) {
        map->m_pblk = value;
        set = true;
      }
    }

  }
  return ret;

create:
  if (create) {
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

    // break everything up into size of continuity blocks.
    for (int c = 0; c < len; c += MAX_CONTIGUOUS_BLOCKS) {
      uint32_t nblocks_to_alloc = min(len - c, MAX_CONTIGUOUS_BLOCKS);

      int r = mlfs_new_blocks(sb, &blockp, nblocks_to_alloc, 0, 0, a_type, 0);

      if (r > 0) {
        bitmap_bits_set_range(sb->s_blk_bitmap, blockp, r);
        sb->used_blocks += r;
      } else if (ret == -ENOSPC) {
        panic("Fail to allocate block\n");
        try_migrate_blocks(g_root_dev, g_ssd_dev, 0, 1);
      }

      if (!set) {
        map->m_pblk = blockp;
        set = true;
      }

      mlfs_lblk_t lb = map->m_lblk + (map->m_len - len);
      for (uint32_t i = 0; i < nblocks_to_alloc; ++i) {
        hash_value_t in = MAKEVAL(nblocks_to_alloc > 1, i, blockp);

        // check if exists first
        // should already be checked for!
        /*
        mlfs_fsblk_t cur;
        if (lookup_hash(inode, lb + i, &cur)) {
          if (cur != in) {
            fprintf(stderr, "insert key %u with val %0lx collides with %0lx\n",
                lb + i, in, cur);
            assert(cur == in);
          }
          continue;
        }
        */

        int success = insert_hash(inode, lb + i, in);

        if (!success) {
          //fprintf(stderr, "%d, %d, %lu: %lu\n", 1, CONTINUITY_BITS,
          //    REMAINING_BITS, CHAR_BIT * sizeof(hash_value_t));
          fprintf(stderr, "could not insert: key = %u, val = %0lx\n",
              lb + i, in);
        }
      }
    }

  }

  return ret;
}
int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end) {
  gpointer key, value;
  hash_key_t k;
  hash_value_t v;

#if 0
  g_hash_table_iter_init (&iter, inode->htable);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    k = GPTR2KEY(key);
    v = GPTR2VAL(value);
    int size = 1;
    /*
    if (SPECIAL(v) && INDEX(v) > 0) {
      printf("SKIP %d, %d, %lu\n", SPECIAL(v), INDEX(v), ADDR(v));
      g_hash_table_iter_remove(&iter);
      continue;
    } else if (SPECIAL(v)) {
      size = MAX_CONTIGUOUS_BLOCKS;
    }
    */

    if (GET_INUM(k) == inode->inum && GET_LBLK(k) >= start
        && GET_LBLK(k) <= end) {
      //printf("FREE %d, %d, %lu\n", SPECIAL(v), INDEX(v), ADDR(v));
      mlfs_free_blocks(handle, inode, NULL, GET_LBLK(k), size, 0);
      g_hash_table_iter_remove(&iter);
    }
  }
#endif

  return 0;
}

double check_load_factor(struct inode *inode) {
  double load = 0.0;
  double allocated_size = (double)ghash->size;
  double current_size = (double)ghash->noccupied;
  load = current_size / allocated_size;
  return load;
}

#endif
