#include "hash.h"


/*
 * hash_lock_handle_t
 */


int hlh_initialize(hash_lock_handle_t **hlh,
                   size_t num_locks,
                   size_t entries_per_lock) {

  hash_lock_handle_t *h;

  *hlh = NULL;

  if (num_locks == 0 || entries_per_lock == 0) {
    return -EINVAL;
  }

  h = malloc(sizeof(*h));
  if (h == NULL) {
    return -ENOMEM;
  }

  h->hlh_num_locks = num_locks;
  h->hlh_entries_per_lock = entries_per_lock;

  h->hlh_entry_locks = malloc(num_locks * sizeof(*(h->hlh_entry_locks)));
  if (h->hlh_entry_locks == NULL) {
    return -ENOMEM;
  }

  for (size_t i = 0; i < num_locks; ++i) {
    h->hlh_entry_locks[i] = malloc(sizeof(*(h->hlh_entry_locks[i])));
    if (h->hlh_entry_locks[i] == NULL) {
      return -ENOMEM;
    }

    int res = pthread_rwlock_init(h->hlh_entry_locks[i], NULL);
    if (res != 0) {
      return -res;
    }
  }

  h->hlh_metadata_lock = malloc(sizeof(*(h->hlh_metadata_lock)));
  if (h->hlh_metadata_lock == NULL) {
    return -ENOMEM;
  }

  int res = pthread_mutex_init(h->hlh_metadata_lock, NULL);
  if (res != 0) {
    return -ENOMEM;
  }

  return 0;
}

void hlh_destroy(hash_lock_handle_t *hlh) {
  assert(hlh);

  free(hlh->hlh_metadata_lock);

  for (size_t i = 0; i < hlh->hlh_num_locks; ++i) {
    free(hlh->hlh_entry_locks[i]);
  }

  free(hlh->hlh_entry_locks);
  free(hlh);
}

int hlh_lock_entry(hash_lock_handle_t *hlh,
                   size_t entry_index,
                   bool is_write)
{
  size_t lock_index = entry_index / hlh->hlh_entries_per_lock;

  if (lock_index >= hlh->hlh_num_locks) {
    return -EINVAL;
  }

  if (is_write) {
    return pthread_rwlock_wrlock(hlh->hlh_entry_locks[lock_index]);
  }

  return pthread_rwlock_rdlock(hlh->hlh_entry_locks[lock_index]);
}

int hlh_unlock_entry(hash_lock_handle_t *hlh,
                   size_t entry_index)
{
  size_t lock_index = entry_index / hlh->hlh_entries_per_lock;

  if (lock_index >= hlh->hlh_num_locks) {
    return -EINVAL;
  }

  return pthread_rwlock_unlock(hlh->hlh_entry_locks[lock_index]);
}

/*
 * hash_handle_t
 */
