/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * MT safe
 */
#include <stdlib.h>
#include <assert.h>
#include <string.h>  /* memset */
#include <stdint.h>
#include <pthread.h>

#include "ghash.h"

#define G_DISABLE_ASSERT

#define MAX(x,y) (x > y ? x : y)

/**
 * SECTION:hash_tables
 * @title: Hash Tables
 * @short_description: associations between keys and values so that
 *     given a key the value can be found quickly
 *
 * A #GHashTable provides associations between keys and values which is
 * optimized so that given a key, the associated value can be found
 * very quickly.
 *
 * Note that neither keys nor values are copied when inserted into the
 * #GHashTable, so they must exist for the lifetime of the #GHashTable.
 * This means that the use of static strings is OK, but temporary
 * strings (i.e. those created in buffers and those returned by GTK+
 * widgets) should be copied with g_strdup() before being inserted.
 *
 * If keys or values are dynamically allocated, you must be careful to
 * ensure that they are freed when they are removed from the
 * #GHashTable, and also when they are overwritten by new insertions
 * into the #GHashTable. It is also not advisable to mix static strings
 * and dynamically-allocated strings in a #GHashTable, because it then
 * becomes difficult to determine whether the string should be freed.
 *
 * To create a #GHashTable, use g_hash_table_new().
 *
 * To insert a key and value into a #GHashTable, use
 * g_hash_table_insert().
 *
 * To lookup a value corresponding to a given key, use
 * g_hash_table_lookup() and g_hash_table_lookup_extended().
 *
 * g_hash_table_lookup_extended() can also be used to simply
 * check if a key is present in the hash table.
 *
 * To remove a key and value, use g_hash_table_remove().
 *
 * To call a function for each key and value pair use
 * g_hash_table_foreach() or use a iterator to iterate over the
 * key/value pairs in the hash table, see #GHashTableIter.
 *
 * To destroy a #GHashTable use g_hash_table_destroy().
 *
 * A common use-case for hash tables is to store information about a
 * set of keys, without associating any particular value with each
 * key. GHashTable optimizes one way of doing so: If you store only
 * key-value pairs where key == value, then GHashTable does not
 * allocate memory to store the values, which can be a considerable
 * space saving, if your set is large. The functions
 * g_hash_table_add() and g_hash_table_contains() are designed to be
 * used when using #GHashTable this way.
 *
 * #GHashTable is not designed to be statically initialised with keys and
 * values known at compile time. To build a static hash table, use a tool such
 * as [gperf](https://www.gnu.org/software/gperf/).
 */

/**
 * GHashTable:
 *
 * The #GHashTable struct is an opaque data structure to represent a
 * [Hash Table][glib-Hash-Tables]. It should only be accessed via the
 * following functions.
 */


#define HASH_TABLE_MIN_SHIFT 3  /* 1 << 3 == 8 buckets */

#define TRUE 1
#define FALSE 0

/* Each table size has an associated prime modulo (the first prime
 * lower than the table size) used to find the initial bucket. Probing
 * then works modulo 2^n. The prime modulo is necessary to get a
 * good distribution with poor hash functions.
 */
static const int prime_mod [] = {
  1,          /* For 1 << 0 */
  2,
  3,
  7,
  13,
  31,
  61,
  127,
  251,
  509,
  1021,
  2039,
  4093,
  8191,
  16381,
  32749,
  65521,      /* For 1 << 16 */
  131071,
  262139,
  524287,
  1048573,
  2097143,
  4194301,
  8388593,
  16777213,
  33554393,
  67108859,
  134217689,
  268435399,
  536870909,
  1073741789,
  2147483647  /* For 1 << 31 */
};

static void
g_hash_table_set_shift (GHashTable *hash_table, int shift) {
  int i;
  uint32_t mask = 0;

  hash_table->size = 1 << shift;
  hash_table->mod  = prime_mod [shift];

  for (i = 0; i < shift; i++) {
    mask <<= 1;
    mask |= 1;
  }

  hash_table->mask = mask;
}

static int
g_hash_table_find_closest_shift (int n) {
  int i;

  for (i = 0; n; i++) {
    n >>= 1;
  }

  return i;
}

static void
g_hash_table_set_shift_from_size (GHashTable *hash_table, int size) {
  int shift;

  shift = g_hash_table_find_closest_shift(size);
  shift = MAX(shift, HASH_TABLE_MIN_SHIFT);

  g_hash_table_set_shift(hash_table, shift);
}

/*
 * g_hash_table_lookup_node:
 * @hash_table: our #GHashTable
 * @key: the key to lookup against
 * @hash_return: key hash return location
 *
 * Performs a lookup in the hash table, preserving extra information
 * usually needed for insertion.
 *
 * This function first computes the hash value of the key using the
 * user's hash function.
 *
 * If an entry in the table matching @key is found then this function
 * returns the index of that entry in the table, and if not, the
 * index of an unused node (empty or tombstone) where the key can be
 * inserted.
 *
 * The computed hash value is returned in the variable pointed to
 * by @hash_return. This is to save insertions from having to compute
 * the hash record again for the new record.
 *
 * Returns: index of the described node
 */
static inline uint32_t
g_hash_table_lookup_node (GHashTable    *hash_table,
                          mlfs_fsblk_t   key,
                          hash_entry_t  *ent_return,
                          uint32_t      *hash_return) {
  uint32_t node_index;
  uint32_t hash_value;
  uint32_t first_tombstone = 0;
  int have_tombstone = FALSE;
  uint32_t step = 0;
  hash_entry_t *buffer;
  hash_entry_t cur;

  /*
   * If this happens, then the application is probably doing too much work from
   * a destroy notifier. The alternative would be to crash any second (as keys,
   * etc. will be NULL). Applications need to either use g_hash_table_destroy,
   * or ensure the hash table is empty prior to removing the last reference
   * using g_hash_table_unref().
   */
  assert (hash_table->ref_count > 0);

  hash_value = hash_table->hash_func((void*)key);
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }

  *hash_return = hash_value;

  node_index = hash_value % hash_table->mod;
  //pthread_rwlock_rdlock(hash_table->locks + node_index);

  /*
  nvram_read(hash_table, NV_IDX(node_index), &buffer, FALSE);
  cur = buffer[BUF_IDX(node_index)];
  */
  cur = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];

  while (!IS_EMPTY(cur.value)) {
    if (cur.key == key && IS_VALID(cur.value)) {
      *ent_return = cur;
      return node_index;
    } else if (IS_TOMBSTONE(cur.value) && !have_tombstone) {
      first_tombstone = node_index;
      have_tombstone = TRUE;
    }

    step++;
    uint32_t new_idx = (node_index + step) & hash_table->mask;
    //pthread_rwlock_unlock(hash_table->locks + node_index);
    //pthread_rwlock_rdlock(hash_table->locks + new_idx);
    // if the next index would be outside of the block we read from nvram,
    // we need to read another block
    // TODO profile me and see how many times we actually have to do this
    /*
    if (NV_IDX(new_idx) != NV_IDX(node_index)) {
      nvram_read(hash_table, NV_IDX(new_idx), &buffer, FALSE);
    }
    */

    node_index = new_idx;
    //cur = buffer[BUF_IDX(node_index)];
    cur = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];
  }

  if (have_tombstone) {
    return first_tombstone;
  }

  //pthread_rwlock_unlock(hash_table->locks + node_index);
  //*ent_return = buffer[BUF_IDX(node_index)];
  *ent_return = hash_table->cache[NV_IDX(node_index)][BUF_IDX(node_index)];

  return node_index;
}

/*
 * g_hash_table_remove_node:
 * @hash_table: our #GHashTable
 * @node: pointer to node to remove
 * @notify: %TRUE if the destroy notify handlers are to be called
 *
 * Removes a node from the hash table and updates the node count.
 * The node is replaced by a tombstone. No table resize is performed.
 *
 * If @notify is %TRUE then the destroy notify functions are called
 * for the key and value of the hash node.
 */
static void g_hash_table_remove_node (GHashTable  *hash_table,
                                      int          i,
                                      int          notify) {
  hash_entry_t ent;
  UNUSED(notify);

  //pthread_rwlock_wrlock(hash_table->locks + i);
  //pthread_mutex_lock(hash_table->metalock);

  //nvram_read_entry(hash_table, i, &ent);
  ent.value = VTOMB;
  ent.size = 0;

  /* Erect tombstone */
  nvram_update(hash_table, i, &ent);

  hash_table->nnodes--;
  // update metadata on disk
  //nvram_write_metadata(hash_table, hash_table->size);

  //pthread_mutex_unlock(hash_table->metalock);
  //pthread_rwlock_unlock(hash_table->locks + i);

}

/*
 * g_hash_table_maybe_resize:
 * @hash_table: our #GHashTable
 *
 * Resizes the hash table, if needed.
 *
 * Essentially, calls g_hash_table_resize() if the table has strayed
 * too far from its ideal size for its number of nodes.
 *
 * iangneal: Hijacking this function to assure that we haven't over-committed.
 */
static inline void
g_hash_table_maybe_resize (GHashTable *hash_table) {
  int noccupied = hash_table->noccupied;
  int size = hash_table->size;

  assert(noccupied <= size);
#if 0
  if ((size > hash_table->nnodes * 4 && size > 1 << HASH_TABLE_MIN_SHIFT) ||
      (size <= noccupied + (noccupied / 16))) {
    g_hash_table_resize (hash_table);
  }
#endif
}

/**
 * g_hash_table_new:
 * @hash_func: a function to create a hash value from a key
 * @key_equal_func: a function to check two keys for equality
 *
 * Creates a new #GHashTable with a reference count of 1.
 *
 * Hash values returned by @hash_func are used to determine where keys
 * are stored within the #GHashTable data structure. The g_direct_hash(),
 * g_int_hash(), g_int64_hash(), g_double_hash() and g_str_hash()
 * functions are provided for some common types of keys.
 * If @hash_func is %NULL, g_direct_hash() is used.
 *
 * @key_equal_func is used when looking up keys in the #GHashTable.
 * The g_direct_equal(), g_int_equal(), g_int64_equal(), g_double_equal()
 * and g_str_equal() functions are provided for the most common types
 * of keys. If @key_equal_func is %NULL, keys are compared directly in
 * a similar fashion to g_direct_equal(), but without the overhead of
 * a function call. @key_equal_func is called with the key from the hash table
 * as its first parameter, and the user-provided key to check against as
 * its second.
 *
 * Returns: a new #GHashTable
 */
GHashTable *
g_hash_table_new (GHashFunc    hash_func,
                  size_t       max_entries,
                  size_t       range_size,
                  mlfs_fsblk_t metadata_location) {
  GHashTable *hash_table;

  hash_table = malloc(sizeof(*hash_table));

  hash_table->hash_func          = hash_func ? hash_func : g_direct_hash;
  hash_table->ref_count          = 1;
  hash_table->nnodes             = 0;
  hash_table->noccupied          = 0;
  // Number of entries in nvram.
  hash_table->nvram_size         = max_entries / range_size;
  hash_table->range_size         = range_size;

  if (max_entries % range_size) {
    // If there's a partial range, need to not under-allocate.
    hash_table->nvram_size++;
  }

  g_hash_table_set_shift_from_size(hash_table, hash_table->nvram_size);
  // Make sure the mod size doesn't go out of range.
  hash_table->nvram_size = max(hash_table->nvram_size, hash_table->size);
  size_t scaled_size = hash_table->nvram_size;

  size_t nblocks = NV_IDX(scaled_size);
  if (BUF_IDX(scaled_size)) {
    // Partial block.
    nblocks++;
  }

  // initialize read-writer locks
  hash_table->locks = malloc(scaled_size * sizeof(pthread_rwlock_t));
  assert(hash_table->locks);
  for (size_t i = 0; i < scaled_size; ++i) {
    int err = pthread_rwlock_init(hash_table->locks + i, NULL);
    if (err) {
      panic("Could not init rwlock!");
    }
  }

  // init metadata lock
  hash_table->metalock = malloc(sizeof(pthread_mutex_t));
  assert(hash_table->metalock);
  pthread_mutex_init(hash_table->metalock, NULL);

  if (!nvram_read_metadata(hash_table, metadata_location)) {
    printf("Metadata not set up. Allocating hashtable on NVRAM...\n");
    hash_table->data = nvram_alloc_range(nblocks);

    nvram_write_metadata(hash_table, metadata_location);
    printf("max_entries: %lu, sizeof %lu, nblocks: %lu\n", scaled_size,
        sizeof(hash_entry_t), nblocks);
  } else {
    printf("Metadata found!\n");
  }

  // cache
#ifdef HASHCACHE
  hash_table->cache = calloc(nblocks, sizeof(hash_entry_t*));
  assert(hash_table->cache);

  hash_entry_t *unused;

  for (int i = 0; i < nblocks; ++i) {
    hash_table->cache[i] = calloc(g_block_size_bytes / sizeof(hash_entry_t),
        sizeof(hash_entry_t));
    // load from NVRAM (force flag)
    nvram_read(hash_table, i, &unused, 1);
  }

  hash_table->bh_cache = calloc(nblocks, sizeof(*hash_table->bh_cache));
  assert(hash_table->bh_cache);

  hash_table->bh_cache_head = NULL;
#endif


  return hash_table;
}

/*
 * g_hash_table_insert_node:
 * @hash_table: our #GHashTable
 * @node_index: pointer to node to insert/replace
 * @key_hash: key hash
 * @key: (nullable): key to replace with, or %NULL
 * @value: value to replace with
 * @keep_new_key: whether to replace the key in the node with @key
 * @reusing_key: whether @key was taken out of the existing node
 *
 * Inserts a value at @node_index in the hash table and updates it.
 *
 * If @key has been taken out of the existing node (ie it is not
 * passed in via a g_hash_table_insert/replace) call, then @reusing_key
 * should be %TRUE.
 *
 * Returns: %TRUE if the key did not exist yet
 */
static int
g_hash_table_insert_node (GHashTable    *hash_table,
                          uint32_t       node_index,
                          uint32_t       key_hash,
                          mlfs_fsblk_t   new_key,
                          mlfs_fsblk_t   new_value,
                          mlfs_fsblk_t   new_range)
{
  int already_exists;
  hash_entry_t ent;

  //pthread_rwlock_wrlock(hash_table->locks + node_index);
  //pthread_mutex_lock(hash_table->metalock);

  nvram_read_entry(hash_table, node_index, &ent);
  already_exists = IS_VALID(ent.value);

  ent.key = new_key;
  ent.value = new_value;
  ent.size = new_range;
  nvram_update(hash_table, node_index, &ent);


  /* Now, the bookkeeping... */
  if (!already_exists) {
    hash_table->nnodes++;

    /*
    if (HASH_IS_UNUSED (old_hash)) {
      // We replaced an empty node, and not a tombstone
      hash_table->noccupied++;
    }
    */
  }

  //nvram_write_metadata(hash_table, hash_table->size);

  //pthread_mutex_lock(hash_table->metalock);
  //pthread_rwlock_unlock(hash_table->locks + node_index);
  return !already_exists;
}

/**
 * g_hash_table_destroy:
 * @hash_table: a #GHashTable
 *
 * Destroys all keys and values in the #GHashTable and decrements its
 * reference count by 1. If keys and/or values are dynamically allocated,
 * you should either free them first or create the #GHashTable with destroy
 * destruction phase.
 */
void
g_hash_table_destroy (GHashTable *hash_table)
{
  assert (hash_table != NULL);

  for (size_t i = 0; i < hash_table->nvram_size; ++i) {
    int err = pthread_rwlock_destroy(hash_table->locks + i);
    if (err) panic("Could not destroy rwlock!");
  }
}

/**
 * g_hash_table_lookup:
 * @hash_table: a #GHashTable
 * @key: the key to look up
 *
 * Looks up a key in a #GHashTable. Note that this function cannot
 * distinguish between a key that is not present and one which is present
 * and has the value %NULL. If you need this distinction, use
 * g_hash_table_lookup_extended().
 *
 * Returns: (nullable): the associated value, or %NULL if the key is not found
 */
void g_hash_table_lookup(GHashTable *hash_table, mlfs_fsblk_t key,
    mlfs_fsblk_t *val, mlfs_fsblk_t *size) {
  uint32_t node_index;
  uint32_t hash_return;
  hash_entry_t ent;

  assert(hash_table != NULL);

  node_index = g_hash_table_lookup_node(hash_table, key, &ent, &hash_return);

  //pthread_rwlock_rdlock(hash_table->locks + node_index);

  *val = !IS_TOMBSTONE(ent.value) ? ent.value : 0;
  *size = ent.size;

  //pthread_rwlock_unlock(hash_table->locks + node_index);
}

/*
 * g_hash_table_insert_internal:
 * @hash_table: our #GHashTable
 * @key: the key to insert
 * @value: the value to insert
 * @keep_new_key: if %TRUE and this key already exists in the table
 *   then call the destroy notify function on the old key.  If %FALSE
 *   then call the destroy notify function on the new key.
 *
 * Implements the common logic for the g_hash_table_insert() and
 * g_hash_table_replace() functions.
 *
 * Do a lookup of @key. If it is found, replace it with the new
 * @value (and perhaps the new @key). If it is not found, create
 * a new node.
 *
 * Returns: %TRUE if the key did not exist yet
 */
static int
g_hash_table_insert_internal (GHashTable     *hash_table,
                              mlfs_fsblk_t    key,
                              mlfs_fsblk_t    value,
                              mlfs_fsblk_t    size)
{
  hash_entry_t ent;
  uint32_t node_index;
  uint32_t hash;

  assert(hash_table != NULL);
  node_index = g_hash_table_lookup_node (hash_table, key, &ent, &hash);

  return g_hash_table_insert_node (hash_table, node_index, hash, key,
      value, size);
}

/**
 * g_hash_table_insert:
 * @hash_table: a #GHashTable
 * @key: a key to insert
 * @value: the value to associate with the key
 *
 * Inserts a new key and value into a #GHashTable.
 *
 * If the key already exists in the #GHashTable its current
 * value is replaced with the new value. If you supplied a
 * @value_destroy_func when creating the #GHashTable, the old
 * value is freed using that function. If you supplied a
 * @key_destroy_func when creating the #GHashTable, the passed
 * key is freed using that function.
 *
 * Returns: %TRUE if the key did not exist yet
 */
int
g_hash_table_insert (GHashTable     *hash_table,
                     mlfs_fsblk_t    key,
                     mlfs_fsblk_t    value,
                     mlfs_fsblk_t    size)
{
  return g_hash_table_insert_internal (hash_table, key, value, size);
}

/**
 * g_hash_table_replace:
 * @hash_table: a #GHashTable
 * @key: a key to insert
 * @value: the value to associate with the key
 *
 * Inserts a new key and value into a #GHashTable similar to
 * g_hash_table_insert(). The difference is that if the key
 * already exists in the #GHashTable, it gets replaced by the
 * new key. If you supplied a @value_destroy_func when creating
 * the #GHashTable, the old value is freed using that function.
 * If you supplied a @key_destroy_func when creating the
 * #GHashTable, the old key is freed using that function.
 *
 * Returns: %TRUE if the key did not exist yet
 */
int
g_hash_table_replace (GHashTable *hash_table,
                      mlfs_fsblk_t key,
                      mlfs_fsblk_t value)
{
  return g_hash_table_insert_internal (hash_table, key, value, TRUE);
}

/**
 * g_hash_table_add:
 * @hash_table: a #GHashTable
 * @key: a key to insert
 *
 * This is a convenience function for using a #GHashTable as a set.  It
 * is equivalent to calling g_hash_table_replace() with @key as both the
 * key and the value.
 *
 * When a hash table only ever contains keys that have themselves as the
 * corresponding value it is able to be stored more efficiently.  See
 * the discussion in the section description.
 *
 * Returns: %TRUE if the key did not exist yet
 *
 * Since: 2.32
 */
int
g_hash_table_add (GHashTable *hash_table,
                  mlfs_fsblk_t    key)
{
  return g_hash_table_insert_internal (hash_table, key, key, TRUE);
}

/**
 * g_hash_table_contains:
 * @hash_table: a #GHashTable
 * @key: a key to check
 *
 * Checks if @key is in @hash_table.
 *
 * Returns: %TRUE if @key is in @hash_table, %FALSE otherwise.
 *
 * Since: 2.32
 **/
int
g_hash_table_contains (GHashTable    *hash_table,
                       mlfs_fsblk_t  key)
{
  uint32_t node_index;
  uint32_t hash;
  hash_entry_t ent;

  assert(hash_table != NULL);

  node_index = g_hash_table_lookup_node (hash_table, key, &ent, &hash);

  return IS_VALID(ent.value);
}

/*
 * g_hash_table_remove_internal:
 * @hash_table: our #GHashTable
 * @key: the key to remove
 * @notify: %TRUE if the destroy notify handlers are to be called
 * Returns: %TRUE if a node was found and removed, else %FALSE
 *
 * Implements the common logic for the g_hash_table_remove() and
 * g_hash_table_steal() functions.
 *
 * Do a lookup of @key and remove it if it is found, calling the
 * destroy notify handlers only if @notify is %TRUE.
 */
static int
g_hash_table_remove_internal (GHashTable    *hash_table,
                              mlfs_fsblk_t   key,
                              int            notify)
{
  hash_entry_t ent;
  uint32_t node_index;
  uint32_t hash;

  assert(hash_table != NULL);

  node_index = g_hash_table_lookup_node (hash_table, key, &ent, &hash);

  if (!IS_VALID(ent.value)) return FALSE;

  g_hash_table_remove_node (hash_table, node_index, notify);

  return TRUE;
}

/**
 * g_hash_table_remove:
 * @hash_table: a #GHashTable
 * @key: the key to remove
 *
 * Removes a key and its associated value from a #GHashTable.
 *
 * If the #GHashTable was created using g_hash_table_new_full(), the
 * key and value are freed using the supplied destroy functions, otherwise
 * you have to make sure that any dynamically allocated values are freed
 * yourself.
 *
 * Returns: %TRUE if the key was found and removed from the #GHashTable
 */
int
g_hash_table_remove (GHashTable    *hash_table,
                     mlfs_fsblk_t  key)
{
  return g_hash_table_remove_internal (hash_table, key, TRUE);
}



/**
 * g_hash_table_find:
 * @hash_table: a #GHashTable
 * @predicate: function to test the key/value pairs for a certain property
 * @user_data: user data to pass to the function
 *
 * Calls the given function for key/value pairs in the #GHashTable
 * until @predicate returns %TRUE. The function is passed the key
 * and value of each pair, and the given @user_data parameter. The
 * hash table may not be modified while iterating over it (you can't
 * add/remove items).
 *
 * Note, that hash tables are really only optimized for forward
 * lookups, i.e. g_hash_table_lookup(). So code that frequently issues
 * g_hash_table_find() or g_hash_table_foreach() (e.g. in the order of
 * once per every entry in a hash table) should probably be reworked
 * to use additional or different data structures for reverse lookups
 * (keep in mind that an O(n) find/foreach operation issued for all n
 * values in a hash table ends up needing O(n*n) operations).
 *
 * Returns: (nullable): The value of the first key/value pair is returned,
 *     for which @predicate evaluates to %TRUE. If no pair with the
 *     requested property is found, %NULL is returned.
 *
 * Since: 2.4
 */
void*
g_hash_table_find (GHashTable *hash_table,
                   GHRFunc     predicate,
                   void*    user_data)
{
  int i;
  int match;

  assert(hash_table != NULL);
  assert(predicate != NULL);

  assert(0);

  match = FALSE;
#if 0
  for (i = 0; i < hash_table->size; i++) {
    uint32_t node_hash = hash_table->hashes[i];
    void* node_key = hash_table->keys[i];
    void* node_value = hash_table->values[i];

    if (HASH_IS_REAL (node_hash)) {
      match = predicate (node_key, node_value, user_data);
    }

    if (match) return node_value;
  }
#endif

  return NULL;
}

/**
 * g_hash_table_size:
 * @hash_table: a #GHashTable
 *
 * Returns the number of elements contained in the #GHashTable.
 *
 * Returns: the number of key/value pairs in the #GHashTable.
 */
uint32_t g_hash_table_size (GHashTable *hash_table) {
  assert (hash_table != NULL);

  return hash_table->nnodes;
}

/* Hash functions.
 */

/**
 * g_direct_hash:
 * @v: (nullable): a #void* key
 *
 * Converts a void* to a hash value.
 * It can be passed to g_hash_table_new() as the @hash_func parameter,
 * when using opaque pointers compared by pointer value as keys in a
 * #GHashTable.
 *
 * This hash function is also appropriate for keys that are integers
 * stored in pointers, such as `GINT_TO_POINTER (n)`.
 *
 * Returns: a hash value corresponding to the key.
 */
uint32_t g_direct_hash (const void* v) {
  return (uint32_t)((uint64_t)(v) & 0xFFFFFFFF);
}
