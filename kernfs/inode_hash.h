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

typedef union {
  struct {
    uint32_t inum; // inode number
    mlfs_lblk_t lblk; // logical block within specified inode
  } entry;
  uint64_t raw;
} hash_key_t;

#define GKEY2PTR(hk) GUINT_TO_POINTER(hk.raw)
#define GPTR2KEY(ptr) ((uint64_t)ptr)

#if 0
typedef union {
  struct {
    mlfs_fsblk_t is_special : 1;
    mlfs_fsblk_t index : CONTINUITY_BITS;
    mlfs_fsblk_t addr : REMAINING_BITS;
  } encoding;
  uint64_t raw;
} hash_value_t;
#define GVAL2PTR(hv) GUINT_TO_POINTER(hv.raw)
#else
typedef mlfs_fsblk_t hash_value_t;

#define IS_SPECIAL(hv) !(!(hv & (1lu << 63lu)))
#define INDEX(hv) ((hv >> REMAINING_BITS) & ((1lu << CONTINUITY_BITS) - 1lu))
#define ADDR(hv) (((1lu << REMAINING_BITS) - 1lu) & hv)
#define GVAL2PTR(hv) (GUINT_TO_POINTER(hv))
#define GPTR2VAL(ptr) ((hash_value_t)ptr)

#define MAKE_SPECIAL(sp) ((uint64_t) (sp & 1lu) << 63lu)
#define MAKE_INDEX(idx) (((uint64_t) (idx & ((1 << CONTINUITY_BITS) - 1))) << REMAINING_BITS)
#define MAKEVAL(sp, idx, addr) (MAKE_SPECIAL(sp) | MAKE_INDEX(idx) | addr)
#endif

extern int calls;

/*
 * Generic hash table functions.
 */

void init_hash(struct inode *inode);

int insert_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t value);

int lookup_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t* value);

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
