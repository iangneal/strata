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


#define MAKEKEY(inode, key) { .entry = {.inum = inode->inum, .lblk = key} }

// (iangneal): Global hash table for all of NVRAM. Each inode has a point to
// this one hash table just for abstraction of the inode interface.
static GHashTable *ghash = NULL;


void init_hash(struct inode *inode) {
  //TODO: init in NVRAM.
  printf("SIZE OF HASH_KEY_T: %lu\n", sizeof(hash_key_t));
  printf("SIZE OF HASH_VALUE_T: %lu\n", sizeof(hash_value_t));
  if (!ghash) {
    ghash = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  inode->htable = ghash;

  if (!ghash) {
    panic("Failed to initialize inode hashtable\n");
  }
}

inline
int insert_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t value) {
  calls++;
  hash_key_t k = MAKEKEY(inode, key);
  //printf("insert_hash: %p -> %p\n", GKEY2PTR(k), GVAL2PTR(value));
  gboolean exists = g_hash_table_insert(inode->htable,
                                        GKEY2PTR(k),
                                        GVAL2PTR(value));
  // if not exists, then the value was not already in the table, therefore
  // success.
  return (int)exists;
}

inline
int lookup_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t* value) {
  hash_key_t k = MAKEKEY(inode, key);
  gpointer val = g_hash_table_lookup(inode->htable, GKEY2PTR(k));
  if (val) {
    //value->raw = GPOINTER_TO_UINT(val);
    //*value = GPOINTER_TO_UINT(val);
    *value = GPTR2VAL(val);
  }
  //printf("lookup_hash: %p -> %p (%lx)\n", GKEY2PTR(k), val, *value);

  return val != NULL;
}

int calls = 0;

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags) {
	int err = 0;
	int create = flags & MLFS_GET_BLOCKS_CREATE_DATA;
  int ret = map->m_len;
  uint32_t len = map->m_len;
  bool set = false;

	mlfs_assert(handle);

  // lookup all blocks.
  for (mlfs_lblk_t i = 0; i < map->m_len;) {
    hash_value_t value;
    int pre = lookup_hash(inode, map->m_lblk + i, &value);
    if (!pre) {
      goto create;
    }

    if (IS_SPECIAL(value)) {
      //printf("LOOKUP: %lx (%d, %d, %lu)\n", value, IS_SPECIAL(value),
      //       INDEX(value), ADDR(value));
      len -= MAX_CONTIGUOUS_BLOCKS - INDEX(value);
      i += MAX_CONTIGUOUS_BLOCKS - INDEX(value);
      if (!set) {
        map->m_pblk = ADDR(value) + INDEX(value);
        set = true;
      }
    } else {
      //printf("LOOKUP: not special %lx\n", value);
      --len;
      ++i;
    }

  }
  return ret;

create:
  if (create) {
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
    for (mlfs_lblk_t c = 0; c < len; c += MAX_CONTIGUOUS_BLOCKS) {
      mlfs_fsblk_t blockp;
      mlfs_lblk_t nblocks_to_alloc = min(len - c, MAX_CONTIGUOUS_BLOCKS);

      int r = mlfs_new_blocks(sb, &blockp, nblocks_to_alloc, 0, 0, a_type, 0);

      if (r > 0) {
        bitmap_bits_set_range(sb->s_blk_bitmap, blockp, r);
        sb->used_blocks += r;
      } else if (r == -ENOSPC) {
        panic("Fail to allocate block\n");
        try_migrate_blocks(g_root_dev, g_ssd_dev, 0, 1);
      }

      if (!set) {
        map->m_pblk = blockp;
        set = true;
      }

      mlfs_lblk_t lb = map->m_lblk + (map->m_len - len);
      for (mlfs_lblk_t i = 0; i < nblocks_to_alloc; ++i) {
        hash_value_t in = MAKEVAL(nblocks_to_alloc > 1, i, blockp);
        //printf("INSERT: %lx (%d, %d, %lu)\n", in, IS_SPECIAL(in),
        //       INDEX(in), ADDR(in));

        if (!insert_hash(inode, lb + i, in)) {
          fprintf(stderr, "%d, %d, %lu: %lu\n", 1, CONTINUITY_BITS,
              REMAINING_BITS, CHAR_BIT * sizeof(hash_value_t));
          fprintf(stderr, "could not insert: key = %u, val = %0lx\n",
              lb + i, *((mlfs_fsblk_t*)&in));
        }

      }
    }

  }

  return ret;
}
int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end) {
  GHashTableIter iter;
  hash_key_t k;
  hash_value_t v;
  mlfs_fsblk_t fblk;
  gpointer key, value;

  g_hash_table_iter_init (&iter, inode->htable);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    k.raw = GPOINTER_TO_UINT(key);
    //v.raw = GPOINTER_TO_UINT(value);
    v = GPTR2VAL(value);

    if (k.entry.inum == inode->inum && k.entry.lblk >= start &&
        k.entry.lblk <= end) {
      if (IS_SPECIAL(v)) {
        fblk = ADDR(v) + INDEX(v);
      } else {
        fblk = v;
      }
      //fprintf(stderr, "Truncate: %lu = (%lu + %lu)\n", fblk, ADDR(v), INDEX(v));
      mlfs_free_blocks(handle, inode, NULL, fblk, 1, 0);
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
  return current_size / allocated_size;
}

int mlfs_hash_persist(handle_t *handle, struct inode *inode) {
  int ret = 0;
  struct buffer_head *bh;
  GHashTable *hash = inode->htable;

  // Set up the hash table metadata in
  struct dhashtable_meta metadata = {
    .size = hash->size,
    .mod = hash->mod,
    .mask = hash->mask,
    .nnodes = hash->nnodes,
    .noccupied = hash->noccupied
  };

  assert(inode->l1.addrs);
  bool force = true;
  if (!inode->l1.addrs[0] || force) {
    // allocate a block for metadata
    int err;
    mlfs_lblk_t count = 1;
    fprintf(stderr, "Preparing to allocate new meta blocks...\n");
    inode->l1.addrs[0] = mlfs_new_meta_blocks(handle, inode, 0,
        MLFS_GET_BLOCKS_CREATE, &count, &err);
    if (err < 0) {
      fprintf(stderr, "Error: could not allocate new meta block: %d\n", err);
      return err;
    }
    assert(err == count);
    printf("Allocated meta block #%lu\n", inode->l1.addrs[0]);
  }
  // Write metadata to disk.
  bh = bh_get_sync_IO(handle->dev, inode->l1.addrs[0], BH_NO_DATA_ALLOC);
  //bh = bh_get_sync_IO(handle->dev, 8, BH_NO_DATA_ALLOC);
  assert(bh);

  bh->b_data = (uint8_t*)&metadata;
  bh->b_size = sizeof(metadata);
  bh->b_offset = 0;

  printf("DING: %p, size %u to %08lx\n", bh->b_data, bh->b_size,
      inode->l1.addrs[0]);
  ret = mlfs_write(bh);
  assert(!ret);

  bh_release(bh);

  // TODO: do rest of hash table.

  return ret;
}

int mlfs_hash_construct_from_device(handle_t *handle, struct inode *inode) {
  return 0;
}

#endif
