#ifdef HASHTABLE

#include <stdbool.h>
#include "inode_hash.h"

#include <glib.h>
#include <glib/glib.h>

// (iangneal): glib declares this structure within the ghash.c file, so I can't
// reference the internal members at compile time. These fields are supposed to
// be private, but I rather do this than directly hack the glib source code.
struct _GHashTable {
  gint             size;
  gint             mod;
  guint            mask;
  gint             nnodes;
  gint             noccupied;  /* nnodes + tombstones */

  gpointer        *keys;
  guint           *hashes;
  gpointer        *values;

  GHashFunc        hash_func;
  GEqualFunc       key_equal_func;
  gint             ref_count;
  GDestroyNotify   key_destroy_func;
  GDestroyNotify   value_destroy_func;
};


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
  mlfs_fsblk_t keys_start;
  mlfs_fsblk_t nblocks_keys;

  mlfs_fsblk_t hashes_start;
  mlfs_fsblk_t nblocks_hashes;

  mlfs_fsblk_t values_start;
  mlfs_fsblk_t nblocks_values;
};

// (iangneal): Global hash table for all of NVRAM. Each inode has a point to
// this one hash table just for abstraction of the inode interface.
static GHashTable *ghash = NULL;

void init_hash(struct inode *inode) {
  //TODO: init in NVRAM.
  if (!ghash) {
    ghash = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!ghash) {
      panic("Failed to initialize inode hashtable\n");
    }
  }

  inode->htable = ghash;
}

int insert_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t value) {
  int ret = 0;
  hash_key_t k = MAKEKEY(inode, key);
  gboolean exists = g_hash_table_insert(inode->htable,
                                        GKEY2PTR(k),
                                        GVAL2PTR(value));
  // if not exists, then the value was not already in the table, therefore
  // success.
  return (int)exists;
}


int lookup_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t* value) {
  int ret = 0;
  hash_key_t k = MAKEKEY(inode, key);
  gpointer val = g_hash_table_lookup(inode->htable, GKEY2PTR(k));
  if (val) {
    *value = GPTR2VAL(val);
  }
  return val != NULL;
}

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags) {
	struct mlfs_ext_path *path = NULL;
	struct mlfs_ext_path *_path = NULL;
	struct mlfs_extent newex, *ex;
	int goal, err = 0, depth;
	mlfs_lblk_t allocated = 0;
	mlfs_fsblk_t next, newblock;
	int create;
	uint64_t tsc_start = 0;

	mlfs_assert(handle != NULL);

	create = flags & MLFS_GET_BLOCKS_CREATE_DATA;
  int ret = map->m_len;

  // lookup all blocks.
  uint32_t len = map->m_len;
  bool set = false;

  for (uint32_t i = 0; i < map->m_len; i++) {
    hash_value_t value;
    int pre = lookup_hash(inode, map->m_lblk + i, &value);
    if (!pre) {
      goto create;
    }

    if (SPECIAL(value)) {
      len -= MAX_CONTIGUOUS_BLOCKS - INDEX(value);
      i += MAX_CONTIGUOUS_BLOCKS - INDEX(value) - 1;
      if (!set) {
        map->m_pblk = ADDR(value) + INDEX(value);
        set = true;
      }
    } else {
      --len;
    }

  }
  return ret;

create:
  if (create) {
    mlfs_fsblk_t blockp;
    struct super_block *sb = get_inode_sb(handle->dev, inode);
    int ret;
    int retry_count = 0;
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

      ret = mlfs_new_blocks(get_inode_sb(handle->dev, inode), &blockp,
          nblocks_to_alloc, 0, 0, a_type, goal);

      if (ret > 0) {
        bitmap_bits_set_range(get_inode_sb(handle->dev, inode)->s_blk_bitmap,
            blockp, ret);
        get_inode_sb(handle->dev, inode)->used_blocks += ret;
      } else if (ret == -ENOSPC) {
        panic("Fail to allocate block\n");
        try_migrate_blocks(g_root_dev, g_ssd_dev, 0, 1);
      }

      if (err) fprintf(stderr, "ERR = %d\n", err);

      if (!set) {
        map->m_pblk = blockp;
        set = true;
      }

      mlfs_lblk_t lb = map->m_lblk + (map->m_len - len);
      for (uint32_t i = 0; i < nblocks_to_alloc; ++i) {
        hash_value_t in = MAKEVAL(nblocks_to_alloc > 1, i, blockp);
        int success = insert_hash(inode, lb + i, in);

        if (!success) {
          fprintf(stderr, "%d, %d, %d: %d\n", 1, CONTINUITY_BITS,
              REMAINING_BITS, CHAR_BIT * sizeof(hash_value_t));
          fprintf(stderr, "could not insert: key = %u, val = %0lx\n",
              lb + i, *((mlfs_fsblk_t*)&in));
        }

        //blockp++;
        //lb++;
      }
    }

  }

  return ret;
}
int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end) {
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, inode->htable);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    mlfs_lblk_t lb = GPOINTER_TO_UINT(key);
    mlfs_fsblk_t pb = GPOINTER_TO_UINT(value);
    if (lb >= start && lb <= end) {
      mlfs_free_blocks(handle, inode, NULL, lb, 1, 0);
      g_hash_table_iter_remove(&iter);
    }
  }

  return 0;
}

double check_load_factor(struct inode *inode) {
  double load = 0.0;
  GHashTable *hash = inode->htable;
  double allocated_size = (double)hash->size;
  double current_size = (double)hash->nnodes;
  load = current_size / allocated_size;
  return load;
}

int mlfs_hash_persist(handle_t *handle, struct inode *inode) {
  int ret = 0;
  GHashTable *hash = inode->htable;

  // alloc a big range for keys.

  return ret;
}

#endif
