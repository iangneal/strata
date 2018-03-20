#ifdef HASHTABLE

#include <stdbool.h>
#include "inode_hash.h"

#define CUSTOM

#ifndef CUSTOM
#warning "Non-custom!"
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
#else
#include "ghash.h"

#define GPOINTER_TO_UINT(x) ((uint64_t)x)
#endif



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
  size_t nblocks;
  mlfs_fsblk_t keys_start;
  mlfs_fsblk_t hashes_start;
  mlfs_fsblk_t values_start;
};

// (iangneal): Global hash table for all of NVRAM. Each inode has a point to
// this one hash table just for abstraction of the inode interface.
static GHashTable *ghash = NULL;
static struct dhashtable_meta *ghash_meta;

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
	int err = 0;
	mlfs_lblk_t allocated = 0;
	int create;

	mlfs_assert(handle);

	create = flags & MLFS_GET_BLOCKS_CREATE_DATA;
  int ret = map->m_len;

  // lookup all blocks.
  uint32_t len = map->m_len;
  bool set = false;

  for (mlfs_lblk_t i = 0; i < map->m_len;) {
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
        int success = insert_hash(inode, lb + i, in);

        if (!success) {
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
  gpointer key, value;
  hash_key_t k;
  hash_value_t v;

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
  struct buffer_head *bh;
  GHashTable *hash = inode->htable;
  struct super_block *sb = get_inode_sb(handle->dev, inode);

  // Do the three big chunks:
  size_t num_to_alloc = (sb->num_blocks * sizeof(gpointer)) >>
    g_block_size_shift;
  mlfs_fsblk_t start;

  if (!inode->l1.addrs[0]) {
    // allocate blocks for hashtable
    int err;
    mlfs_lblk_t count = 3 * num_to_alloc;
    fprintf(stderr, "Preparing to allocate new meta blocks...\n");
    start = mlfs_new_meta_blocks(handle, inode, 0,
        MLFS_GET_BLOCKS_CREATE, &count, &err);
    if (err < 0) {
      fprintf(stderr, "Error: could not allocate new meta block: %d\n", err);
      return err;
    }
    assert(err == count);
    // Write keys
    bh = bh_get_sync_IO(handle->dev, start, BH_NO_DATA_ALLOC);
    assert(bh);

    bh->b_data = (uint8_t*)hash->keys;
    bh->b_size = hash->size;
    bh->b_offset = 0;

    ret = mlfs_write(bh);
    assert(!ret);
    bh_release(bh);
    // Write hashes
    bh = bh_get_sync_IO(handle->dev, start + num_to_alloc, BH_NO_DATA_ALLOC);
    assert(bh);

    bh->b_data = (uint8_t*)hash->hashes;
    bh->b_size = hash->size;
    bh->b_offset = 0;

    ret = mlfs_write(bh);
    assert(!ret);
    bh_release(bh);
    // Write values
    bh = bh_get_sync_IO(handle->dev, start + (2 * num_to_alloc), BH_NO_DATA_ALLOC);
    assert(bh);

    bh->b_data = (uint8_t*)hash->values;
    bh->b_size = hash->size;
    bh->b_offset = 0;

    ret = mlfs_write(bh);
    assert(!ret);
    bh_release(bh);

    // Set up the hash table metadata in NVRAM
    struct dhashtable_meta metadata = {
      .size = hash->size,
      .mod = hash->mod,
      .mask = hash->mask,
      .nnodes = hash->nnodes,
      .noccupied = hash->noccupied,
      .nblocks = num_to_alloc,
      .keys_start = start,
      .hashes_start = start + num_to_alloc,
      .values_start = start + (2 * num_to_alloc)
    };
    *ghash_meta = metadata;

    assert(inode->l1.addrs);
    // allocate a block for metadata
    count = 1;
    fprintf(stderr, "Preparing to allocate new meta blocks...\n");
    inode->l1.addrs[0] = mlfs_new_meta_blocks(handle, inode, 0,
        MLFS_GET_BLOCKS_CREATE, &count, &err);
    if (err < 0) {
      fprintf(stderr, "Error: could not allocate new meta block: %d\n", err);
      return err;
    }
    assert(err == count);
    printf("Allocated meta block #%lu\n", inode->l1.addrs[0]);
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
  } else {



  }



  return ret;
}

#endif
