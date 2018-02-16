#ifdef HASHTABLE

#include <stdbool.h>
#include "inode_hash.h"


void init_hash(struct inode *inode) {
  //TODO: init in NVRAM.
#if USE_GLIB_HASH
  inode->htable = g_hash_table_new(g_direct_hash, g_direct_equal);
  bool success = inode->htable != NULL;
#elif USE_CUCKOO_HASH
  inode->htable = (struct cuckoo_hash*)mlfs_alloc(sizeof(inode->htable));
  bool success = cuckoo_hash_init(inode->htable, 20);
#else
#error "Insert undefined for inode hashtable type!"
#endif

  if (!success) {
    panic("Failed to initialize cuckoo hashtable\n");
  }
}

int insert_hash(struct inode *inode, mlfs_lblk_t key, mlfs_fsblk_t value) {
  int ret = 0;
#if USE_GLIB_HASH
  gboolean exists = g_hash_table_insert(inode->htable,
                                        GUINT_TO_POINTER(key),
                                        GUINT_TO_POINTER(value));
  ret = exists;
#elif USE_CUCKOO_HASH
  struct cuckoo_hash_item* out = cuckoo_hash_insert(inode->htable,
                                                    key,
                                                    value);
  if (out == CUCKOO_HASH_FAILED) {
    panic("cuckoo_hash_insert: failed to insert.\n");
  }
  // if is NULL, then value was not already in the table, therefore success.
  ret = out == NULL;
#else
#error "Insert undefined for inode hashtable type!"
#endif
  return ret;
}


int lookup_hash(struct inode *inode, mlfs_lblk_t key, mlfs_fsblk_t* value) {
  int ret = 0;
#if USE_GLIB_HASH
  gpointer val = g_hash_table_lookup(inode->htable,
                                     GUINT_TO_POINTER(key));
  if (val) *value = GPOINTER_TO_UINT(val);
  ret = val != NULL && *value > 0;
  //printf("%p, %lu\n", val, *value);
#elif USE_CUCKOO_HASH
  struct cuckoo_hash_item* out = cuckoo_hash_lookup(inode->htable,
                                                    key);
  if (out) *value = out->value;
  ret = out != NULL;
#else
#error "Insert undefined for inode hashtable type!"
#endif
  return ret;
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
  for (uint32_t i = 0; i < map->m_len; i++) {
    int pre = lookup_hash(inode, map->m_lblk + i, &newblock);
    if (!pre) {
      goto create;
    }
    --len;
  }
  return ret;

create:
  if (create) {
    mlfs_fsblk_t blockp;
    struct super_block *sb = get_inode_sb(handle->dev, inode);
    int ret;
    int retry_count = 0;
    enum alloc_type a_type;

    if (flags & MLFS_GET_BLOCKS_CREATE_DATA_LOG)
      a_type = DATA_LOG;
    else if (flags & MLFS_GET_BLOCKS_CREATE_META)
      a_type = TREE;
    else
      a_type = DATA;

    ret = mlfs_new_blocks(get_inode_sb(handle->dev, inode), &blockp,
        len, 0, 0, a_type, goal);

    if (ret > 0) {
      bitmap_bits_set_range(get_inode_sb(handle->dev, inode)->s_blk_bitmap,
          blockp, ret);
      get_inode_sb(handle->dev, inode)->used_blocks += ret;
    } else if (ret == -ENOSPC) {
      panic("Fail to allocate block\n");
      try_migrate_blocks(g_root_dev, g_ssd_dev, 0, 1);
    }

    if (err) fprintf(stderr, "ERR = %d\n", err);

    map->m_pblk = blockp;

    mlfs_lblk_t lb = map->m_lblk + (map->m_len - len);
    for (uint32_t i = 0; i < len; ++i) {
      int success = insert_hash(inode, lb, blockp);

      if (!success) fprintf(stderr, "could not insert\n");

      blockp++;
      lb++;
    }
  }

  return ret;
}
int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end) {
#if USE_GLIB_HASH
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
#elif USE_CUCKOO_HASH
  for (struct cuckoo_hash_item *cuckoo_hash_each(iter, inode->htable)) {
    mlfs_lblk_t lb = iter->key;
    mlfs_fsblk_t pb = iter->value;
    if (lb >= start && lb <= end) {
      mlfs_free_blocks(handle, inode, NULL, lb, 1, 0);
      cuckoo_hash_remove(inode->htable, iter);
    }
  }
#else
#error "No mlfs_hash_truncate for this hash table!"
#endif
  return 0;
}

#endif
