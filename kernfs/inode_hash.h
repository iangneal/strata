#ifndef __INODE_HASH__
#define __INODE_HASH__

#include <malloc.h>
#include <memory.h>
#include <string.h>
#include "fs.h"
#include "extents.h"
#include "global/util.h"
#ifdef KERNFS
#include "balloc.h"
#include "migrate.h"
#endif

/*
 * Generic hash table functions.
 */

void init_hash(struct inode *inode);

int insert_hash(struct inode *inode, mlfs_lblk_t key, mlfs_fsblk_t value);

int lookup_hash(struct inode *inode, mlfs_lblk_t key, mlfs_fsblk_t* value);

/*
 * Emulated mlfs_ext functions.
 */

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags);

int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end);

#endif  // __INODE_HASH__
