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

#ifdef __cplusplus
extern "C" {
#endif

#define CONTINUITY_BITS 4
#define MAX_CONTIGUOUS_BLOCKS (2 << 4)
#define REMAINING_BITS ((CHAR_BIT * sizeof(mlfs_fsblk_t)) - CONTINUITY_BITS - 1)

typedef struct {
  mlfs_fsblk_t is_special : 1;
  mlfs_fsblk_t index : CONTINUITY_BITS;
  mlfs_fsblk_t addr : REMAINING_BITS;
} hash_value_t;


/*
 * Generic hash table functions.
 */

void init_hash(struct inode *inode);

int insert_hash(struct inode *inode, mlfs_lblk_t key, mlfs_fsblk_t value);

int lookup_hash(struct inode *inode, mlfs_lblk_t key, mlfs_fsblk_t* value);

/*
 * Hash table metrics.
 */

double check_load_factor(struct inode *inode);

/*
 * Emulated mlfs_ext functions.
 */

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags);

int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end);

/*
 * Helper functions.
 */

int mlfs_hash_persist(handle_t *handle, struct inode *inode);

#ifdef __cplusplus
}
#endif

#endif  // __INODE_HASH__
