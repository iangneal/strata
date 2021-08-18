#ifndef __INODE_HASH__
#define __INODE_HASH__ 1

#include <malloc.h>
#include <memory.h>
#include <string.h>
#include "fs.h"
#include "extents.h"
#include "global/util.h"
#include "ghash.h"
#ifdef KERNFS
#include "balloc.h"
#include "migrate.h"
#endif

#include "indexing_api_interface.h"
extern idx_fns_t hash_fns;
extern idx_fns_t radixtree_fns;

extern idx_struct_t hash_idx;

#ifdef __cplusplus
extern "C" {
#endif

#define CONTINUITY_BITS 4
#define MAX_CONTIGUOUS_BLOCKS (2 << 4)
#define REMAINING_BITS ((CHAR_BIT * sizeof(mlfs_fsblk_t)) - CONTINUITY_BITS - 1)

#define TO_64(hk) (*((uint64_t*)&hk))

typedef mlfs_fsblk_t hash_value_t;

#define SPECIAL(hv) !(!(hv & (1UL << 63UL)))
#define INDEX(hv) ((hv >> REMAINING_BITS) & ((1lu << CONTINUITY_BITS) - 1lu))
#define ADDR(hv) (((1lu << REMAINING_BITS) - 1lu) & hv)

#define SBIT(cond) (cond ? (1UL << 63UL) : 0UL)
#define IBITS(i) ((uint64_t)i << REMAINING_BITS)

#define MAKEVAL(cond, idx, addr) (SBIT(cond) | IBITS(idx) | addr)

#define GVAL2PTR(hv) ((void*)(hv))
#define GPTR2VAL(pt) ((uint64_t)pt)


#define MASK_32 ((1lu << 32) - 1)
#ifndef MAKEKEY
#define MAKEKEY(inode, key) (((uint64_t)inode->inum << 32) | key)
#endif
#define GET_INUM(hk) (hk >> 32)
#define GET_LBLK(hk) (hk & MASK_32)
#define GKEY2PTR(hk) ((void*)(hk))
#define GPTR2KEY(pt) ((uint64_t)pt)
typedef uint64_t hash_key_t;

/*
 * This is how the global hash table structure is designed and used:
 * TODO
 */

/*
 * Globals for where the hash tables and other bookkeeping structures are kept
 * on disk. They are calculated as an offset based on the size of the NVM
 * device.
 */
extern mlfs_fsblk_t single_hash_meta_loc;
extern mlfs_fsblk_t chunk_hash_meta_loc;
extern mlfs_fsblk_t id_map_meta_loc;

/*
 * Generic hash table functions.
 */

#define USE_API
//#undef USE_API

void init_hash(struct super_block *sb, bool enable_perf_stats);

#ifndef USE_API
int insert_hash(GHashTable *hash, struct inode *inode, hash_key_t key,
    hash_value_t value, hash_value_t size);

int lookup_hash(struct inode *inode, mlfs_lblk_t key, hash_value_t* value,
    hash_value_t *size, hash_value_t *index, bool force);
#endif

/*
 * Emulated mlfs_ext functions.
 */

int mlfs_hash_get_blocks(handle_t *handle, struct inode *inode,
			struct mlfs_map_blocks *map, int flags, bool force);

int mlfs_hash_truncate(handle_t *handle, struct inode *inode,
		mlfs_lblk_t start, mlfs_lblk_t end);

/*
 * Helper functions.
 */

int mlfs_hash_persist();
int mlfs_hash_cache_invalidate();

#ifdef __cplusplus
}
#endif

#endif  // __INODE_HASH__
