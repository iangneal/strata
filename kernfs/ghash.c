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

/**
 * GHashFunc:
 * @key: a key
 *
 * Specifies the type of the hash function which is passed to
 * g_hash_table_new() when a #GHashTable is created.
 *
 * The function is passed a key and should return a #uint32_t hash value.
 * The functions g_direct_hash(), g_int_hash() and g_str_hash() provide
 * hash functions which can be used when the key is a #void*, #int*,
 * and #char* respectively.
 *
 * g_direct_hash() is also the appropriate hash function for keys
 * of the form `GINT_TO_POINTER (n)` (or similar macros).
 *
 * A good hash functions should produce
 * hash values that are evenly distributed over a fairly large range.
 * The modulus is taken with the hash table size (a prime number) to
 * find the 'bucket' to place each key into. The function should also
 * be very fast, since it is called for each key lookup.
 *
 * Note that the hash functions provided by GLib have these qualities,
 * but are not particularly robust against manufactured keys that
 * cause hash collisions. Therefore, you should consider choosing
 * a more secure hash function when using a GHashTable with keys
 * that originate in untrusted data (such as HTTP requests).
 * Using g_str_hash() in that situation might make your application
 * vulerable to
 * [Algorithmic Complexity Attacks](https://lwn.net/Articles/474912/).
 *
 * The key to choosing a good hash is unpredictability.  Even
 * cryptographic hashes are very easy to find collisions for when the
 * remainder is taken modulo a somewhat predictable prime number.  There
 * must be an element of randomness that an attacker is unable to guess.
 *
 * Returns: the hash value corresponding to the key
 */

/**
 * GHFunc:
 * @key: a key
 * @value: the value corresponding to the key
 * @user_data: user data passed to g_hash_table_foreach()
 *
 * Specifies the type of the function passed to g_hash_table_foreach().
 * It is called with each key/value pair, together with the @user_data
 * parameter which is passed to g_hash_table_foreach().
 */

/**
 * GHRFunc:
 * @key: a key
 * @value: the value associated with the key
 * @user_data: user data passed to g_hash_table_remove()
 *
 * Specifies the type of the function passed to
 * g_hash_table_foreach_remove(). It is called with each key/value
 * pair, together with the @user_data parameter passed to
 * g_hash_table_foreach_remove(). It should return %TRUE if the
 * key/value pair should be removed from the #GHashTable.
 *
 * Returns: %TRUE if the key/value pair should be removed from the
 *     #GHashTable
 */

/**
 * GEqualFunc:
 * @a: a value
 * @b: a value to compare with
 *
 * Specifies the type of a function used to test two values for
 * equality. The function should return %TRUE if both values are equal
 * and %FALSE otherwise.
 *
 * Returns: %TRUE if @a = @b; %FALSE otherwise
 */

#define HASH_TABLE_MIN_SHIFT 3  /* 1 << 3 == 8 buckets */

#define UNUSED_HASH_VALUE 0
#define TOMBSTONE_HASH_VALUE 1
#define HASH_IS_UNUSED(h_) ((h_) == UNUSED_HASH_VALUE)
#define HASH_IS_TOMBSTONE(h_) ((h_) == TOMBSTONE_HASH_VALUE)

#define HASH_IS_REAL(h_) ((h_) >= 2)

#define TRUE 1
#define FALSE 0

/* Each table size has an associated prime modulo (the first prime
 * lower than the table size) used to find the initial bucket. Probing
 * then works modulo 2^n. The prime modulo is necessary to get a
 * good distribution with poor hash functions.
 */
static const int prime_mod [] =
{
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
g_hash_table_find_closest_shift (int n)
{
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
  uint32_t node_hash;
  uint32_t hash_value;
  uint32_t first_tombstone = 0;
  int have_tombstone = FALSE;
  uint32_t step = 0;
  hash_entry_t buffer[BUF_SIZE];
  hash_entry_t cur;

  /* If this happens, then the application is probably doing too much work
   * from a destroy notifier. The alternative would be to crash any second
   * (as keys, etc. will be NULL).
   * Applications need to either use g_hash_table_destroy, or ensure the hash
   * table is empty prior to removing the last reference using g_hash_table_unref(). */
  assert (hash_table->ref_count > 0);

  hash_value = hash_table->hash_func((void*)key);
  if (unlikely (!HASH_IS_REAL (hash_value))) {
    hash_value = 2;
  }

  *hash_return = hash_value;

  node_index = hash_value % hash_table->mod;

  //pthread_rwlock_rdlock(hash_table->locks + node_index);

  nvram_read(hash_table->data + NV_IDX(node_index), buffer);
  cur = buffer[BUF_IDX(node_index)];
  node_hash = cur.hash;

  while (!HASH_IS_UNUSED (node_hash)) {
    /* We first check if our full hash values
     * are equal so we can avoid calling the full-blown
     * key equality function in most cases.
     */
    if (node_hash == hash_value) {
      void* node_key = (void*) cur.key;

      /*
      if (hash_table->key_equal_func) {
        if (hash_table->key_equal_func (node_key, (gconstpointer) key)) {
          return node_index;
        }
      } else if (node_key == (void*)key) {
        return node_index;
      }
      */
      if (node_key == (void*)key) {
        *ent_return = cur;
        return node_index;
      }
    } else if (HASH_IS_TOMBSTONE(node_hash) && !have_tombstone) {
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
    if ( NV_IDX(new_idx) != NV_IDX(node_index)) {
      nvram_read(hash_table->data + NV_IDX(new_idx), buffer);
    }

    node_index = new_idx;
    cur = buffer[BUF_IDX(node_index)];
    node_hash = cur.hash;
  }

  if (have_tombstone) {
    return first_tombstone;
  }

  //pthread_rwlock_unlock(hash_table->locks + node_index);
  *ent_return = buffer[BUF_IDX(node_index)];

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

  nvram_read_entry(hash_table->data, i, &ent);
  ent.hash = TOMBSTONE_HASH_VALUE;

  /* Erect tombstone */
  nvram_update(hash_table->data, i, &ent);

  hash_table->nnodes--;
  // update metadata on disk
  //nvram_write_metadata(hash_table, hash_table->size);

  //pthread_mutex_unlock(hash_table->metalock);
  //pthread_rwlock_unlock(hash_table->locks + i);

}

/*
 * g_hash_table_remove_all_nodes:
 * @hash_table: our #GHashTable
 * @notify: %TRUE if the destroy notify handlers are to be called
 *
 * Removes all nodes from the table.  Since this may be a precursor to
 * freeing the table entirely, no resize is performed.
 *
 * If @notify is %TRUE then the destroy notify functions are called
 * for the key and value of the hash node.
 */
static void
g_hash_table_remove_all_nodes (GHashTable *hash_table,
                               int         notify,
                               int         destruction)
{
  int i;
  void* key;
  void* value;
  int old_size;
  void* *old_keys;
  void* *old_values;
  uint32_t    *old_hashes;

  assert(0);
  /* If the hash table is already empty, there is nothing to be done. */
  if (hash_table->nnodes == 0)
    return;

  hash_table->nnodes = 0;
  hash_table->noccupied = 0;

  //nvram_write_metadata(hash_table, hash_table->size);

#if 0
  if (!notify ||
      (hash_table->key_destroy_func == NULL &&
       hash_table->value_destroy_func == NULL)) {
    if (!destruction) {
      memset (hash_table->hashes, 0, hash_table->size * sizeof (uint32_t));
      memset (hash_table->keys, 0, hash_table->size * sizeof (void*));
      memset (hash_table->values, 0, hash_table->size * sizeof (void*));
    }

    return;
  }

  /* Keep the old storage space around to iterate over it. */
  old_size   = hash_table->size;
  old_keys   = hash_table->keys;
  old_values = hash_table->values;
  old_hashes = hash_table->hashes;

  /* Now create a new storage space; If the table is destroyed we can use the
   * shortcut of not creating a new storage. This saves the allocation at the
   * cost of not allowing any recursive access.
   * However, the application doesn't own any reference anymore, so access
   * is not allowed. If accesses are done, then either an assert or crash
   * *will* happen. */
  g_hash_table_set_shift (hash_table, HASH_TABLE_MIN_SHIFT);
  if (!destruction) {
    hash_table->keys   = calloc(hash_table->size, sizeof(void*));
    hash_table->values = hash_table->keys;
    hash_table->hashes = calloc(hash_table->size, sizeof(uint32_t));
  } else {
    hash_table->keys   = NULL;
    hash_table->values = NULL;
    hash_table->hashes = NULL;
  }

  for (i = 0; i < old_size; i++) {
    if (HASH_IS_REAL (old_hashes[i])) {
      key = old_keys[i];
      value = old_values[i];

      old_hashes[i] = UNUSED_HASH_VALUE;
      old_keys[i] = NULL;
      old_values[i] = NULL;

      if (hash_table->key_destroy_func != NULL) {
        hash_table->key_destroy_func(key);
      }

      if (hash_table->value_destroy_func != NULL) {
        hash_table->value_destroy_func(value);
      }
    }
  }

  /* Destroy old storage space. */
  if (old_keys != old_values) {
    free(old_values);
  }

  free(old_keys);
  free(old_hashes);
#endif
}

/*
 * g_hash_table_resize:
 * @hash_table: our #GHashTable
 *
 * Resizes the hash table to the optimal size based on the number of
 * nodes currently held. If you call this function then a resize will
 * occur, even if one does not need to occur.
 * Use g_hash_table_maybe_resize() instead.
 *
 * This function may "resize" the hash table to its current size, with
 * the side effect of cleaning up tombstones and otherwise optimizing
 * the probe sequences.
 */
static void
g_hash_table_resize (GHashTable *hash_table) {
  void* *new_keys;
  void* *new_values;
  uint32_t *new_hashes;
  int old_size;
  int i;

  // (iangneal): Hash table is to be PRE-ALLOCATED.
  assert(0);
#if 0
  old_size = hash_table->size;
  g_hash_table_set_shift_from_size (hash_table, hash_table->nnodes * 2);

  //new_keys = g_new0 (void*, hash_table->size);
  new_keys = calloc(hash_table->size, sizeof(void*));
  if (hash_table->keys == hash_table->values) {
    new_values = new_keys;
  } else {
    //new_values = g_new0 (void*, hash_table->size);
    new_values = calloc(hash_table->size, sizeof(void*));
  }

  //new_hashes = g_new0 (uint32_t, hash_table->size);
  new_hashes = calloc(hash_table->size, sizeof(uint32_t));

  for (i = 0; i < old_size; i++) {
    uint32_t node_hash = hash_table->hashes[i];
    uint32_t hash_val;
    uint32_t step = 0;

    if (!HASH_IS_REAL (node_hash))
      continue;

    hash_val = node_hash % hash_table->mod;

    while (!HASH_IS_UNUSED (new_hashes[hash_val])) {
      step++;
      hash_val += step;
      hash_val &= hash_table->mask;
    }

    new_hashes[hash_val] = hash_table->hashes[i];
    new_keys[hash_val] = hash_table->keys[i];
    new_values[hash_val] = hash_table->values[i];
  }

  if (hash_table->keys != hash_table->values) {
    free (hash_table->values);
  }

  free (hash_table->keys);
  free (hash_table->hashes);

  hash_table->keys = new_keys;
  hash_table->values = new_values;
  hash_table->hashes = new_hashes;

  hash_table->noccupied = hash_table->nnodes;
#endif
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
g_hash_table_new (GHashFunc  hash_func,
                  GEqualFunc key_equal_func,
                  size_t     max_entries) {
  return g_hash_table_new_full (hash_func, key_equal_func, NULL, NULL, max_entries);
}


/**
 * g_hash_table_new_full:
 * @hash_func: a function to create a hash value from a key
 * @key_equal_func: a function to check two keys for equality
 * @key_destroy_func: (nullable): a function to free the memory allocated for the key
 *     used when removing the entry from the #GHashTable, or %NULL
 *     if you don't want to supply such a function.
 * @value_destroy_func: (nullable): a function to free the memory allocated for the
 *     value used when removing the entry from the #GHashTable, or %NULL
 *     if you don't want to supply such a function.
 *
 * Creates a new #GHashTable like g_hash_table_new() with a reference
 * count of 1 and allows to specify functions to free the memory
 * allocated for the key and value that get called when removing the
 * entry from the #GHashTable.
 *
 * Since version 2.42 it is permissible for destroy notify functions to
 * recursively remove further items from the hash table. This is only
 * permissible if the application still holds a reference to the hash table.
 * This means that you may need to ensure that the hash table is empty by
 * calling g_hash_table_remove_all() before releasing the last reference using
 * g_hash_table_unref().
 *
 * Returns: a new #GHashTable
 */
GHashTable *
g_hash_table_new_full (GHashFunc      hash_func,
                       GEqualFunc     key_equal_func,
                       GDestroyNotify key_destroy_func,
                       GDestroyNotify value_destroy_func,
                       size_t         max_entries)
{
  GHashTable *hash_table;

  hash_table = malloc(sizeof(*hash_table));

  hash_table->hash_func          = hash_func ? hash_func : g_direct_hash;
  hash_table->key_equal_func     = key_equal_func;
  hash_table->key_destroy_func   = key_destroy_func;
  hash_table->value_destroy_func = value_destroy_func;
  hash_table->ref_count          = 1;
  hash_table->nnodes             = 0;
  hash_table->noccupied          = 0;
  hash_table->nvram_size         = max_entries;

  // initialize read-writer locks
  hash_table->locks = malloc(max_entries * sizeof(pthread_rwlock_t));
  assert(hash_table->locks);
  for (size_t i = 0; i < max_entries; ++i) {
    int err = pthread_rwlock_init(hash_table->locks + i, NULL);
    if (err) panic("Could not init rwlock!");
  }
  // init metadata lock
  hash_table->metalock = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(hash_table->metalock, NULL);

  if (!nvram_read_metadata(hash_table, max_entries)) {
    printf("Metadata not set up. Allocating hashtable on NVRAM...\n");
    g_hash_table_set_shift_from_size(hash_table, max_entries);
    // (iangneal): Allocate 3 strips in NVRAM for these three arrays.
    size_t nblocks = NV_IDX(max_entries);
    /*
    hash_table->keys   = nvram_alloc_range(nblocks);
    hash_table->values = nvram_alloc_range(nblocks);
    hash_table->hashes = nvram_alloc_range(nblocks);
    */
    hash_table->data   = nvram_alloc_range(nblocks);

    nvram_write_metadata(hash_table, max_entries);
    printf("max_entries: %lu, sizeof %lu, nblocks: %lu\n", max_entries,
        sizeof(hash_entry_t), nblocks);
  } else {
    printf("Metadata found!\n");
  }


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
                          int            keep_new_key,
                          int            reusing_key)
{
  int already_exists;
  hash_entry_t ent;
  mlfs_fsblk_t old_hash;
  mlfs_fsblk_t key_to_free = 0;
  mlfs_fsblk_t value_to_free = 0;

  //pthread_rwlock_wrlock(hash_table->locks + node_index);
  //pthread_mutex_lock(hash_table->metalock);

  nvram_read_entry(hash_table->data, node_index, &ent);
  already_exists = HASH_IS_REAL(ent.hash);
  old_hash = ent.hash;

  /* Proceed in three steps.  First, deal with the key because it is the
   * most complicated.  Then consider if we need to split the table in
   * two (because writing the value will result in the set invariant
   * becoming broken).  Then deal with the value.
   *
   * There are three cases for the key:
   *
   *  - entry already exists in table, reusing key:
   *    free the just-passed-in new_key and use the existing value
   *
   *  - entry already exists in table, not reusing key:
   *    free the entry in the table, use the new key
   *
   *  - entry not already in table:
   *    use the new key, free nothing
   *
   * We update the hash at the same time...
   */
  if (already_exists) {
    /* Note: we must record the old value before writing the new key
     * because we might change the value in the event that the two
     * arrays are shared.
     */
    //value_to_free = nvram_read_entry(hash_table->values, node_index);
    value_to_free = ent.value;

    if (keep_new_key) {
      //key_to_free = nvram_read_entry(hash_table->keys, node_index);
      key_to_free = ent.key;
      ent.hash = key_hash;
      ent.key = new_key;
      ent.value = new_value;
      nvram_update(hash_table->data, node_index, &ent);
    } else {
      key_to_free = new_key;
    }
  } else {
    //nvram_update(hash_table->hashes, node_index, key_hash);
    //nvram_update(hash_table->keys, node_index, new_key);
    ent.hash = key_hash;
    ent.key = new_key;
    ent.value = new_value;
    nvram_update(hash_table->data, node_index, &ent);
  }

  /* Step two: check if the value that we are about to write to the
   * table is the same as the key in the same position.  If it's not,
   * split the table.
   */
#if 0
  if (unlikely (hash_table->keys == hash_table->values &&
                hash_table->keys[node_index] != new_value)) {
    //hash_table->values = g_memdup (hash_table->keys, sizeof (void*) * hash_table->size);
    hash_table->values = malloc(sizeof(void*) * hash_table->size);
    memcpy(hash_table->values, hash_table->keys, sizeof(void*) * hash_table->size);
  }
#endif

  /* Step 3: Actually do the write */
  //nvram_update(hash_table->values, node_index, new_value);

  /* Now, the bookkeeping... */
  if (!already_exists) {
    hash_table->nnodes++;

    if (HASH_IS_UNUSED (old_hash)) {
      /* We replaced an empty node, and not a tombstone */
      hash_table->noccupied++;
    }
  }

#if 0
  if (already_exists) {
    if (hash_table->key_destroy_func && !reusing_key)
      (* hash_table->key_destroy_func) (key_to_free);
    if (hash_table->value_destroy_func)
      (* hash_table->value_destroy_func) (value_to_free);
  }
#endif

  //nvram_write_metadata(hash_table, hash_table->size);

  //pthread_mutex_lock(hash_table->metalock);
  //pthread_rwlock_unlock(hash_table->locks + node_index);
  return !already_exists;
}

/**
 * g_hash_table_ref:
 * @hash_table: a valid #GHashTable
 *
 * Atomically increments the reference count of @hash_table by one.
 * This function is MT-safe and may be called from any thread.
 *
 * Returns: the passed in #GHashTable
 *
 * Since: 2.10
 */
GHashTable *
g_hash_table_ref (GHashTable *hash_table)
{
  assert(hash_table != NULL);

  __atomic_add_fetch(&hash_table->ref_count, 1, __ATOMIC_SEQ_CST);
  //g_atomic_int_inc (&hash_table->ref_count);

  return hash_table;
}

/**
 * g_hash_table_unref:
 * @hash_table: a valid #GHashTable
 *
 * Atomically decrements the reference count of @hash_table by one.
 * If the reference count drops to 0, all keys and values will be
 * destroyed, and all memory allocated by the hash table is released.
 * This function is MT-safe and may be called from any thread.
 *
 * Since: 2.10
 */
void
g_hash_table_unref (GHashTable *hash_table)
{
  assert (hash_table != NULL);

  if (__atomic_sub_fetch(&hash_table->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
    // Don't call this function!
    assert(0);
  }
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

  g_hash_table_remove_all (hash_table);
  g_hash_table_unref (hash_table);
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
mlfs_fsblk_t g_hash_table_lookup(GHashTable *hash_table, mlfs_fsblk_t key) {
  uint32_t node_index;
  uint32_t hash_return;
  hash_entry_t ent;

  assert(hash_table != NULL);

  node_index = g_hash_table_lookup_node(hash_table, key, &ent, &hash_return);

  //pthread_rwlock_rdlock(hash_table->locks + node_index);

  mlfs_fsblk_t val = HASH_IS_REAL(ent.hash) ? ent.value : 0;

  //pthread_rwlock_unlock(hash_table->locks + node_index);

  return val;
}

/**
 * g_hash_table_lookup_extended:
 * @hash_table: a #GHashTable
 * @lookup_key: the key to look up
 * @orig_key: (out) (optional): return location for the original key
 * @value: (out) (optional) (nullable): return location for the value associated
 * with the key
 *
 * Looks up a key in the #GHashTable, returning the original key and the
 * associated value and a #int which is %TRUE if the key was found. This
 * is useful if you need to free the memory allocated for the original key,
 * for example before calling g_hash_table_remove().
 *
 * You can actually pass %NULL for @lookup_key to test
 * whether the %NULL key exists, provided the hash and equal functions
 * of @hash_table are %NULL-safe.
 *
 * Returns: %TRUE if the key was found in the #GHashTable
 */
int
g_hash_table_lookup_extended (GHashTable    *hash_table,
                              const void*  lookup_key,
                              void*      *orig_key,
                              void*      *value)
{
  uint32_t node_index;
  uint32_t node_hash;

  assert(hash_table != NULL);
  assert(0); // don't call me
#if 0
  node_index = g_hash_table_lookup_node (hash_table, lookup_key, &node_hash);

  if (!HASH_IS_REAL (hash_table->hashes[node_index]))
    return FALSE;

  if (orig_key)
    *orig_key = hash_table->keys[node_index];

  if (value)
    *value = hash_table->values[node_index];

#endif
  return TRUE;
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
g_hash_table_insert_internal (GHashTable *hash_table,
                              mlfs_fsblk_t    key,
                              mlfs_fsblk_t    value,
                              int    keep_new_key)
{
  hash_entry_t ent;
  uint32_t node_index;
  uint32_t hash;

  assert(hash_table != NULL);
  node_index = g_hash_table_lookup_node (hash_table, key, &ent, &hash);

  return g_hash_table_insert_node (hash_table, node_index, hash, key,
      value, keep_new_key, FALSE);
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
g_hash_table_insert (GHashTable *hash_table,
                     mlfs_fsblk_t    key,
                     mlfs_fsblk_t    value)
{
  return g_hash_table_insert_internal (hash_table, key, value, FALSE);
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

  return HASH_IS_REAL (ent.hash);
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

  if (!HASH_IS_REAL (ent.hash))
    return FALSE;

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
 * g_hash_table_steal:
 * @hash_table: a #GHashTable
 * @key: the key to remove
 *
 * Removes a key and its associated value from a #GHashTable without
 * calling the key and value destroy functions.
 *
 * Returns: %TRUE if the key was found and removed from the #GHashTable
 */
int
g_hash_table_steal (GHashTable    *hash_table,
                    mlfs_fsblk_t  key)
{
  return g_hash_table_remove_internal (hash_table, key, FALSE);
}

/**
 * g_hash_table_remove_all:
 * @hash_table: a #GHashTable
 *
 * Removes all keys and their associated values from a #GHashTable.
 *
 * If the #GHashTable was created using g_hash_table_new_full(),
 * the keys and values are freed using the supplied destroy functions,
 * otherwise you have to make sure that any dynamically allocated
 * values are freed yourself.
 *
 * Since: 2.12
 */
void
g_hash_table_remove_all (GHashTable *hash_table)
{
  assert (hash_table != NULL);

  g_hash_table_remove_all_nodes (hash_table, TRUE, FALSE);
}

/**
 * g_hash_table_steal_all:
 * @hash_table: a #GHashTable
 *
 * Removes all keys and their associated values from a #GHashTable
 * without calling the key and value destroy functions.
 *
 * Since: 2.12
 */
void
g_hash_table_steal_all (GHashTable *hash_table)
{
  assert (hash_table != NULL);

  g_hash_table_remove_all_nodes (hash_table, FALSE, FALSE);
}

/*
 * g_hash_table_foreach_remove_or_steal:
 * @hash_table: a #GHashTable
 * @func: the user's callback function
 * @user_data: data for @func
 * @notify: %TRUE if the destroy notify handlers are to be called
 *
 * Implements the common logic for g_hash_table_foreach_remove()
 * and g_hash_table_foreach_steal().
 *
 * Iterates over every node in the table, calling @func with the key
 * and value of the node (and @user_data). If @func returns %TRUE the
 * node is removed from the table.
 *
 * If @notify is true then the destroy notify handlers will be called
 * for each removed node.
 */
static uint32_t
g_hash_table_foreach_remove_or_steal (GHashTable *hash_table,
                                      GHRFunc     func,
                                      void*    user_data,
                                      int    notify)
{
  uint32_t deleted = 0;
  int i;

  assert(0); // don't call me

#if 0

  for (i = 0; i < hash_table->size; i++) {
    uint32_t node_hash = hash_table->hashes[i];
    void* node_key = hash_table->keys[i];
    void* node_value = hash_table->values[i];

    if (HASH_IS_REAL (node_hash) &&
        (* func) (node_key, node_value, user_data)) {
      g_hash_table_remove_node (hash_table, i, notify);
      deleted++;
    }

  }
#endif

  return deleted;
}

/**
 * g_hash_table_foreach_remove:
 * @hash_table: a #GHashTable
 * @func: the function to call for each key/value pair
 * @user_data: user data to pass to the function
 *
 * Calls the given function for each key/value pair in the
 * #GHashTable. If the function returns %TRUE, then the key/value
 * pair is removed from the #GHashTable. If you supplied key or
 * value destroy functions when creating the #GHashTable, they are
 * used to free the memory allocated for the removed keys and values.
 *
 * See #GHashTableIter for an alternative way to loop over the
 * key/value pairs in the hash table.
 *
 * Returns: the number of key/value pairs removed
 */
uint32_t
g_hash_table_foreach_remove (GHashTable *hash_table,
                             GHRFunc     func,
                             void*    user_data)
{
  assert (hash_table != NULL);
  assert (func != NULL);

  return g_hash_table_foreach_remove_or_steal (hash_table, func, user_data, TRUE);
}

/**
 * g_hash_table_foreach_steal:
 * @hash_table: a #GHashTable
 * @func: the function to call for each key/value pair
 * @user_data: user data to pass to the function
 *
 * Calls the given function for each key/value pair in the
 * #GHashTable. If the function returns %TRUE, then the key/value
 * pair is removed from the #GHashTable, but no key or value
 * destroy functions are called.
 *
 * See #GHashTableIter for an alternative way to loop over the
 * key/value pairs in the hash table.
 *
 * Returns: the number of key/value pairs removed.
 */
uint32_t
g_hash_table_foreach_steal (GHashTable *hash_table,
                            GHRFunc     func,
                            void*    user_data) {
  assert (hash_table != NULL);
  assert (func != NULL);

  return g_hash_table_foreach_remove_or_steal (hash_table, func, user_data, FALSE);
}

/**
 * g_hash_table_foreach:
 * @hash_table: a #GHashTable
 * @func: the function to call for each key/value pair
 * @user_data: user data to pass to the function
 *
 * Calls the given function for each of the key/value pairs in the
 * #GHashTable.  The function is passed the key and value of each
 * pair, and the given @user_data parameter.  The hash table may not
 * be modified while iterating over it (you can't add/remove
 * items). To remove all items matching a predicate, use
 * g_hash_table_foreach_remove().
 *
 * See g_hash_table_find() for performance caveats for linear
 * order searches in contrast to g_hash_table_lookup().
 */
void g_hash_table_foreach (GHashTable *hash_table,
                          GHFunc      func,
                          void*    user_data) {
  int i;

  assert (hash_table != NULL);
  assert (func != NULL);

  assert(0);

#if 0
  for (i = 0; i < hash_table->size; i++) {
    uint32_t node_hash = hash_table->hashes[i];
    void* node_key = hash_table->keys[i];
    void* node_value = hash_table->values[i];

    if (HASH_IS_REAL (node_hash)) {
      (* func) (node_key, node_value, user_data);
    }

  }
#endif
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
 * g_str_equal:
 * @v1: (not nullable): a key
 * @v2: (not nullable): a key to compare with @v1
 *
 * Compares two strings for byte-by-byte equality and returns %TRUE
 * if they are equal. It can be passed to g_hash_table_new() as the
 * @key_equal_func parameter, when using non-%NULL strings as keys in a
 * #GHashTable.
 *
 * Note that this function is primarily meant as a hash table comparison
 * function. For a general-purpose, %NULL-safe string comparison function,
 * see g_strcmp0().
 *
 * Returns: %TRUE if the two keys match
 */
int g_str_equal (const void* v1, const void* v2) {
  const char *string1 = v1;
  const char *string2 = v2;

  return strcmp(string1, string2) == 0;
}

/**
 * g_str_hash:
 * @v: (not nullable): a string key
 *
 * Converts a string to a hash value.
 *
 * This function implements the widely used "djb" hash apparently
 * posted by Daniel Bernstein to comp.lang.c some time ago.  The 32
 * bit uint32_t hash value starts at 5381 and for each byte 'c' in
 * the string, is updated: `hash = hash * 33 + c`. This function
 * uses the signed value of each byte.
 *
 * It can be passed to g_hash_table_new() as the @hash_func parameter,
 * when using non-%NULL strings as keys in a #GHashTable.
 *
 * Note that this function may not be a perfect fit for all use cases.
 * For example, it produces some hash collisions with strings as short
 * as 2.
 *
 * Returns: a hash value corresponding to the key
 */
uint32_t g_str_hash (const void* v) {
  const signed char *p;
  uint32_t h = 5381;

  for (p = v; *p != '\0'; p++) {
    h = (h << 5) + h + *p;
  }

  return h;
}

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

/**
 * g_direct_equal:
 * @v1: (nullable): a key
 * @v2: (nullable): a key to compare with @v1
 *
 * Compares two #void* arguments and returns %TRUE if they are equal.
 * It can be passed to g_hash_table_new() as the @key_equal_func
 * parameter, when using opaque pointers compared by pointer value as
 * keys in a #GHashTable.
 *
 * This equality function is also appropriate for keys that are integers
 * stored in pointers, such as `GINT_TO_POINTER (n)`.
 *
 * Returns: %TRUE if the two keys match.
 */
int g_direct_equal (const void* v1, const void* v2) {
  return v1 == v2;
}

/**
 * g_int_equal:
 * @v1: (not nullable): a pointer to a #int key
 * @v2: (not nullable): a pointer to a #int key to compare with @v1
 *
 * Compares the two #int values being pointed to and returns
 * %TRUE if they are equal.
 * It can be passed to g_hash_table_new() as the @key_equal_func
 * parameter, when using non-%NULL pointers to integers as keys in a
 * #GHashTable.
 *
 * Note that this function acts on pointers to #int, not on #int
 * directly: if your hash table's keys are of the form
 * `GINT_TO_POINTER (n)`, use g_direct_equal() instead.
 *
 * Returns: %TRUE if the two keys match.
 */
int g_int_equal (const void* v1, const void* v2) {
  return *((const int*) v1) == *((const int*) v2);
}

/**
 * g_int_hash:
 * @v: (not nullable): a pointer to a #int key
 *
 * Converts a pointer to a #int to a hash value.
 * It can be passed to g_hash_table_new() as the @hash_func parameter,
 * when using non-%NULL pointers to integer values as keys in a #GHashTable.
 *
 * Note that this function acts on pointers to #int, not on #int
 * directly: if your hash table's keys are of the form
 * `GINT_TO_POINTER (n)`, use g_direct_hash() instead.
 *
 * Returns: a hash value corresponding to the key.
 */
uint32_t g_int_hash (const void* v) {
  return *(const int*) v;
}

/**
 * g_int64_equal:
 * @v1: (not nullable): a pointer to a #int64 key
 * @v2: (not nullable): a pointer to a #int64 key to compare with @v1
 *
 * Compares the two #int64 values being pointed to and returns
 * %TRUE if they are equal.
 * It can be passed to g_hash_table_new() as the @key_equal_func
 * parameter, when using non-%NULL pointers to 64-bit integers as keys in a
 * #GHashTable.
 *
 * Returns: %TRUE if the two keys match.
 *
 * Since: 2.22
 */
int g_int64_equal (const void* v1, const void* v2) {
  return *((const int64_t*) v1) == *((const int64_t*) v2);
}

/**
 * g_int64_hash:
 * @v: (not nullable): a pointer to a #int64 key
 *
 * Converts a pointer to a #int64 to a hash value.
 *
 * It can be passed to g_hash_table_new() as the @hash_func parameter,
 * when using non-%NULL pointers to 64-bit integer values as keys in a
 * #GHashTable.
 *
 * Returns: a hash value corresponding to the key.
 *
 * Since: 2.22
 */
uint32_t g_int64_hash (const void* v) {
  return (uint32_t) *(const int64_t*) v;
}
