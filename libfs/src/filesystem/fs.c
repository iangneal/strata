#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <json-c/json.h>

#include "mlfs/mlfs_user.h"
#include "global/global.h"
#include "concurrency/synchronization.h"
#include "concurrency/thread.h"
#include "filesystem/fs.h"
#include "io/block_io.h"
#include "filesystem/file.h"
#include "log/log.h"
#include "mlfs/mlfs_interface.h"
#include "ds/bitmap.h"
#include "filesystem/slru.h"
#include "storage/storage.h"

#include "filesystem/cache_stats.h"

#include "lpmem_ghash.h"
#include "inode_hash.h"

#define _min(a, b) ({\
    __typeof__(a) _a = a;\
    __typeof__(b) _b = b;\
    _a < _b ? _a : _b; })

int log_fd = 0;
int shm_fd = 0;

struct disk_superblock *disk_sb;
struct super_block *sb[g_n_devices + 1];
ncx_slab_pool_t *mlfs_slab_pool;
ncx_slab_pool_t *mlfs_slab_pool_shared;
uint8_t *shm_base;
struct list_head *lru_heads;
uint8_t shm_slab_index = 0;

uint8_t g_log_dev = 0;
uint8_t g_ssd_dev = 0;
uint8_t g_hdd_dev = 0;

uint8_t strata_initialized = 0;

// statistics
json_object *libfs_stats_json;
uint8_t enable_perf_stats;
libfs_stat_t g_perf_stats;

struct lru g_fcache_head;

pthread_rwlock_t *icache_rwlock;
pthread_rwlock_t *dcache_rwlock;
pthread_rwlock_t *dlookup_rwlock;
pthread_rwlock_t *invalidate_rwlock;
pthread_rwlock_t *g_fcache_rwlock;

pthread_rwlock_t *shm_slab_rwlock;
pthread_rwlock_t *shm_lru_rwlock;

struct inode *inode_hash[g_n_devices + 1];
struct dirent_block *dirent_hash[g_n_devices + 1];
struct dlookup_data *dlookup_hash[g_n_devices + 1];

int prof_fd;

void reset_libfs_stats(void)
{
#ifdef STORAGE_PERF
    reset_stats_dist(&storage_rtsc);
    reset_stats_dist(&storage_rnr);
    reset_stats_dist(&storage_wtsc);
    reset_stats_dist(&storage_wnr);
#endif
    memset(&g_perf_stats, 0, sizeof(libfs_stat_t));
    memset(&(g_perf_stats.cache_stats), 0, sizeof(cache_stats_t));
    reset_stats_dist(&(g_perf_stats.read_per_index));
    reset_stats_dist(&(g_perf_stats.read_data_bytes));
    reset_stats_dist(&(g_perf_stats.hash_lookup_count));
    cache_stats_init();
    libfs_stats_json = json_object_new_array();

    reset_cache_stats();
    flush_llc();
}
void show_libfs_stats(const char *title)
{
    get_cache_stats(&(g_perf_stats.cache_stats));

  json_object *root = json_object_new_object();
  json_object_object_add(root, "title", json_object_new_string(title));
  json_object *wait_digest = json_object_new_object(); {
    js_add_int64(wait_digest, "tsc", g_perf_stats.digest_wait_tsc);
    js_add_int64(wait_digest, "nr" , g_perf_stats.digest_wait_nr);
    json_object_object_add(root, "wait_digest", wait_digest);
  }
  json_object *l0 = json_object_new_object(); {
    js_add_int64(l0, "tsc", g_perf_stats.l0_search_tsc);
    js_add_int64(l0, "nr" , g_perf_stats.l0_search_nr);
    json_object_object_add(root, "l0", l0);
  }
  json_object *lsm = json_object_new_object(); {
    js_add_int64(lsm, "tsc", g_perf_stats.tree_search_tsc);
    js_add_int64(lsm, "nr" , g_perf_stats.tree_search_nr);
    json_object_object_add(root, "lsm", lsm);
  }
  json_object *log = json_object_new_object(); {
    json_object *commit = json_object_new_object(); {
      js_add_int64(commit, "tsc", g_perf_stats.log_commit_tsc);
      js_add_int64(commit, "nr" , g_perf_stats.log_commit_nr);
      json_object_object_add(log, "commit", commit);
    }
    json_object *wr = json_object_new_object(); {
      js_add_int64(wr, "tsc", g_perf_stats.log_write_tsc);
      js_add_int64(wr, "nr" , g_perf_stats.log_write_nr);
      json_object_object_add(log, "write", wr);
    }
    json_object *alloc = json_object_new_object(); {
      js_add_int64(alloc, "tsc", g_perf_stats.log_alloc_tsc);
      js_add_int64(alloc, "nr" , g_perf_stats.log_alloc_nr);
      json_object_object_add(log, "alloc", alloc);
    }
    json_object *hdrwr = json_object_new_object(); {
      js_add_int64(hdrwr, "tsc", g_perf_stats.loghdr_write_tsc);
      js_add_int64(hdrwr, "nr" , g_perf_stats.loghdr_write_nr);
      json_object_object_add(log, "hdr_write", hdrwr);
    }
    json_object *hashup = json_object_new_object(); {
      js_add_int64(hashup, "tsc", g_perf_stats.log_hash_update_tsc);
      js_add_int64(hashup, "nr",  g_perf_stats.log_hash_update_nr);
      json_object_object_add(log, "hash_update", hashup);
    }
    json_object_object_add(root, "log", log);
  }
  json_object *read_data = json_object_new_object(); {
    js_add_int64(read_data, "tsc", g_perf_stats.read_data_tsc);
    js_add_int64(read_data, "nr" , g_perf_stats.read_data_bytes.cnt);
    js_add_int64(read_data, "bytes", g_perf_stats.read_data_bytes.total);
    json_object_object_add(root, "read_data", read_data);
  }
  json_object *path_storage = json_object_new_object(); {
    js_add_int64(path_storage, "tsc", g_perf_stats.path_storage_tsc);
    js_add_int64(path_storage, "nr" , g_perf_stats.path_storage_nr);
    json_object_object_add(root, "path_storage", path_storage);
  }
  json_object *storage = json_object_new_object(); {
    js_add_int64(storage, "rtsc", storage_rtsc.total);
    js_add_int64(storage, "rnr" , storage_rnr.total);
    js_add_int64(storage, "wtsc", storage_wtsc.total);
    js_add_int64(storage, "wnr" , storage_wnr.total);
    json_object_object_add(root, "storage", storage);
  }
  json_object *read_end_to_end = json_object_new_object(); {
    js_add_int64(read_end_to_end, "tsc", g_perf_stats.end_to_end_read_tsc);
    js_add_int64(read_end_to_end, "nr" , g_perf_stats.end_to_end_read_nr);
    js_add_int64(read_end_to_end, "n_aligned" , g_perf_stats.aligned_read_nr);
    js_add_int64(read_end_to_end, "n_unaligned" , g_perf_stats.unaligned_read_nr);
    json_object_object_add(root, "read_end_to_end", read_end_to_end);
  }
  json_object *bh_meta = json_object_new_object(); {
    js_add_int64(bh_meta, "tsc", g_perf_stats.bh_meta_tsc);
    js_add_int64(bh_meta, "nr" , g_perf_stats.bh_meta_nr);
    json_object_object_add(root, "bh_meta", bh_meta);
  }
  json_object *ua_fcache = json_object_new_object(); {
    js_add_int64(ua_fcache, "tsc", g_perf_stats.ua_fcache_tsc);
    js_add_int64(ua_fcache, "nr" , g_perf_stats.ua_fcache_nr);
    json_object_object_add(root, "ua_fcache", ua_fcache);
  }
  json_object *fragmentation = json_object_new_object(); {
      js_add_int64(fragmentation, "nfiles", g_perf_stats.n_files);
      js_add_int64(fragmentation, "nblocks", g_perf_stats.n_blocks);
      js_add_int64(fragmentation, "nfragments", g_perf_stats.n_fragments);
      js_add_double(fragmentation, "layout_derived", g_perf_stats.layout_score_derived);
      json_object_object_add(root, "fragmentation", fragmentation);
  }
  json_object *hashfs_lookups = json_object_new_object(); {
      js_add_int64(hashfs_lookups, "nops", g_perf_stats.hash_lookup_count.cnt);
      js_add_int64(hashfs_lookups, "nentries", g_perf_stats.hash_lookup_count.total);
      json_object_object_add(root, "hashfs_lookups", hashfs_lookups);
  }

  add_cache_stats_to_json(root, "idx_cache", &(g_perf_stats.cache_stats)); 

    if (USE_IDXAPI()) {
        json_object *indexing = json_object_new_object();
        add_idx_stats_to_json(enable_perf_stats, indexing);
        json_object_object_add(root, "idx_stats", indexing);
    }

  json_object_array_add(libfs_stats_json, root);
  const char *js_str = json_object_get_string(libfs_stats_json);

  if (enable_perf_stats) {
    assert(!ftruncate(prof_fd, 0));
    (void)lseek(prof_fd, 0, SEEK_SET);    
    assert(strlen(js_str) == write(prof_fd, js_str, strlen(js_str)));
    //write(prof_fd, "\n", 2);
  }
  // json_object_put(root);
  printf("\n");
  printf("-------%s------------- %s libfs statistics\n", getenv("MLFS_IDX_STRUCT"), title);
  printf("wait on digest  (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.digest_wait_tsc,g_perf_stats.digest_wait_nr));
  printf("inode allocation (tsc/op) : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.ialloc_tsc,g_perf_stats.ialloc_nr));
  printf("bcache search (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.bcache_search_tsc,g_perf_stats.bcache_search_nr));
  printf("search l0 tree  (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.l0_search_tsc,g_perf_stats.l0_search_nr));
  printf("  fcache lock (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.fcache_lock_tsc,g_perf_stats.fcache_lock_nr));
  printf("  fcache init (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.fcache_init_tsc,g_perf_stats.fcache_init_nr));
  printf("  fcache get  (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.fcache_get_tsc,g_perf_stats.fcache_get_nr));
  printf("  fcache val  (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.fcache_val_tsc,g_perf_stats.fcache_val_nr));
  printf("  fcache ALL  (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.fcache_all_tsc,g_perf_stats.fcache_all_nr));
  printf("search lsm tree (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.tree_search_tsc,g_perf_stats.tree_search_nr));
    print_cache_stats(&(g_perf_stats.cache_stats));
  printf("log commit (tsc/op)       : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_commit_tsc,g_perf_stats.log_commit_nr));
  printf("  log writes (tsc/op)     : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_write_tsc,g_perf_stats.log_write_nr));
  printf("    log inode (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_write_inode_tsc,g_perf_stats.log_write_inode_nr));
  printf("    log data (tsc/op)     : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_write_data_tsc,g_perf_stats.log_write_data_nr));
  printf("      unaligned (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_write_data_unaligned_tsc,g_perf_stats.log_write_data_unaligned_nr));
  printf("      aligned (tsc/op)    : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_write_data_aligned_tsc,g_perf_stats.log_write_data_aligned_nr));
  printf("        get bh (tsc/op)   : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_aligned_bh_tsc,g_perf_stats.log_aligned_bh_nr));
  printf("        io time (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_aligned_wronly_tsc,g_perf_stats.log_aligned_wronly_nr));
  printf("        loghash (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_hash_update_tsc,g_perf_stats.log_hash_update_nr));
  printf("          fc add (tsc/op) : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_hash_fc_add_tsc,g_perf_stats.log_hash_fc_add_nr));
  printf("            zalloc (tsc/op) : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.fc_add_zalloc_tsc,g_perf_stats.fc_add_zalloc_nr));
  printf("  log alloc (tsc/op)      : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.log_alloc_tsc,g_perf_stats.log_alloc_nr));
  printf("  loghdr writes (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.loghdr_write_tsc,g_perf_stats.loghdr_write_nr));
  printf("read data blocks (tsc/op) : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.read_data_tsc,g_perf_stats.read_data_bytes.cnt));
  print_stats_dist(&(g_perf_stats.read_data_bytes), "read data");
  printf("read data (bytes/tsc)     : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.read_data_bytes.total,g_perf_stats.read_data_tsc));
  printf("directory search (tsc/op) : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.dir_search_tsc,g_perf_stats.dir_search_nr_hit));
  printf("  bmap ext tree (tsc/op)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.dir_search_ext_tsc,g_perf_stats.dir_search_ext_nr));
  printf("path storage (tsc/op)     : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.path_storage_tsc,g_perf_stats.read_per_index.total));
  printf("path storage (tsc/index)  : %lu / %lu(%.2f)\n", tri_ratio(g_perf_stats.path_storage_tsc,g_perf_stats.read_per_index.cnt));
  //printf("temp_debug (tsc)       : %lu\n", tri_ratio(g_perf_stats.tmp_tsc,g_perf_stats.tmp_nr));
  print_stats_dist(&g_perf_stats.hash_lookup_count, "hash_lookup_count");
#ifdef STORAGE_PERF
  printf("--------------------------------------\n");
  printf("search lsm tree : l0 : read_data = 1 : %f : %f\n",
          (double)g_perf_stats.l0_search_tsc/g_perf_stats.tree_search_tsc,
          (double)g_perf_stats.read_data_tsc/g_perf_stats.tree_search_tsc);
  printf("percentage, lsm/(lsm+l0+read_data) : %f\n",
            (double)g_perf_stats.tree_search_tsc/
            (g_perf_stats.tree_search_tsc + g_perf_stats.l0_search_tsc + g_perf_stats.read_data_tsc));
  printf("directory search bmap ext/all : %f\n",
          (double)g_perf_stats.dir_search_ext_tsc/g_perf_stats.dir_search_tsc);
  printf("bmap storage/all : %f\n",
          (double)g_perf_stats.path_storage_tsc/
          (g_perf_stats.tree_search_tsc + g_perf_stats.dir_search_tsc));
  print_stats_dist(&(g_perf_stats.read_per_index), "read per index");
  printf("storage(read nr/ts)     : %lu / %lu(%.2f)\n", tri_ratio(storage_rnr.total, storage_rtsc.total));
  print_stats_dist(&storage_rtsc, "storage read tsc");
  print_stats_dist(&storage_rnr, "storage read nr");
  printf("storage(write nr/ts)    : %lu / %lu(%.2f)\n", tri_ratio(storage_wnr.total, storage_wtsc.total));
  print_stats_dist(&storage_wtsc, "storage write tsc");
  print_stats_dist(&storage_wnr, "storage write nr");
#endif
#if 0
  printf("wait on digest (nr)   : %lu \n", g_perf_stats.digest_wait_nr);
  printf("search lsm tree (nr)  : %lu \n", g_perf_stats.tree_search_nr);
  printf("log writes (nr)       : %lu \n", g_perf_stats.log_write_nr);
  printf("read data blocks (nr) : %lu \n", g_perf_stats.read_data_nr);
  printf("directory search hit  (nr) : %lu \n", g_perf_stats.dir_search_nr_hit);
  printf("directory search miss (nr) : %lu \n", g_perf_stats.dir_search_nr_miss);
#ifndef KERNFS
#include "filesystem/cache_stats.h"
#else
#include "cache_stats.h"
#endif
  printf("directory search notfound (nr) : %lu \n", g_perf_stats.dir_search_nr_notfound);
#endif
      printf("--------------------------------------\n");
    print_global_idx_stats(enable_perf_stats);
	printf("--------------------------------------\n");

    char *frag_calc = getenv("MLFS_FRAG_CALC");
    if (frag_calc && !strcmp("1", frag_calc)) {
        calculate_fragmentation();
        print_fragmentation();
    }
}

void shutdown_fs(void)
{
  int ret;
  // int _enable_perf_stats = enable_perf_stats;
  if(IDXAPI_IS_HASHFS()) {
		pmem_nvm_hash_table_close();
	}
  if (!strata_initialized) {
    return;
  }

  fflush(stdout);
  fflush(stderr);

  if (enable_perf_stats) {
      show_libfs_stats("shutdown fs");
      close(prof_fd);
  }

  enable_perf_stats = 0;

  shutdown_log();

  // enable_perf_stats = _enable_perf_stats;


  /*
  ret = munmap(mlfs_slab_pool_shared, SHM_SIZE);
  if (ret == -1)
    panic("cannot unmap shared memory\n");

  ret = close(shm_fd);
  if (ret == -1)
    panic("cannot close shared memory\n");
  */

  return;
}

#ifdef USE_SLAB
void mlfs_slab_init(uint64_t pool_size)
{
  uint8_t *pool_space;

  // MAP_SHARED is used to share memory in case of fork.
  pool_space = (uint8_t *)mmap(0, pool_size, PROT_READ|PROT_WRITE,
      MAP_SHARED|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);

  mlfs_assert(pool_space);

  if (madvise(pool_space, pool_size, MADV_HUGEPAGE) < 0)
    panic("cannot do madvise for huge page\n");

  mlfs_slab_pool = (ncx_slab_pool_t *)pool_space;
  mlfs_slab_pool->addr = pool_space;
  mlfs_slab_pool->min_shift = 3;
  mlfs_slab_pool->end = pool_space + pool_size;

  ncx_slab_init(mlfs_slab_pool);
}
#endif

void debug_init(void)
{
#ifdef MLFS_LOG
  log_fd = open(LOG_PATH, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
#endif
}

void shared_slab_init(uint8_t _shm_slab_index)
{
  /* TODO: make the following statment work */
  /* shared memory is used for 2 slab regions.
   * At the beginning, The first region is used for allocating lru list.
   * After libfs makes a digest request or lru update request, libfs must free
   * a current lru list and start build new one. Instead of iterating lru list,
   * Libfs reintialize slab to the second region and initialize head of lru.
   * This is because kernel FS might be still absorbing the LRU list
   * in the first region.(kernel FS sends ack of digest and starts absoring
   * the LRU list to reduce digest wait time.)
   * Likewise, the second region is toggle to the first region
   * when it needs to build a new list.
   */
  mlfs_slab_pool_shared = (ncx_slab_pool_t *)(shm_base + 4096);

  mlfs_slab_pool_shared->addr = (shm_base + 4096) + _shm_slab_index * (SHM_SIZE / 2);
  mlfs_slab_pool_shared->min_shift = 3;
  mlfs_slab_pool_shared->end = mlfs_slab_pool_shared->addr + (SHM_SIZE / 2);

  ncx_slab_init(mlfs_slab_pool_shared);
}

static void shared_memory_init(void)
{
  shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
  if (shm_fd == -1) {
    panic("(%d) (%s) cannot open shared memory\n", errno, strerror(errno));
  }

  // the first 4096 is reserved for lru_head array.
  //shm_base = (uint8_t *)mmap(SHM_START_ADDR,
  shm_base = (uint8_t *)mmap(NULL,
      SHM_SIZE + 4096,
      PROT_READ | PROT_WRITE,
      //MAP_SHARED | MAP_FIXED,
      MAP_SHARED,
      shm_fd, 0);
  if (shm_base == MAP_FAILED) {
    panic("(%d) (%s) cannot map shared memory\n", errno, strerror(errno));
  }

  shm_slab_index = 0;
  shared_slab_init(shm_slab_index);

  bandwidth_consumption = (uint64_t *)shm_base;

  lru_heads = (struct list_head *)shm_base + 128;

  INIT_LIST_HEAD(&lru_heads[g_log_dev]);
}

static void cache_init(void)
{
  int i;

  for (i = 1; i < g_n_devices + 1; i++) {
    inode_hash[i] = NULL;
    dirent_hash[i] = NULL;
    dlookup_hash[i] = NULL;
  }

  lru_hash = NULL;

  INIT_LIST_HEAD(&g_fcache_head.lru_head);
  g_fcache_head.n = 0;
}

static void locks_init(void)
{
  pthread_rwlockattr_t rwlattr;

  pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

  icache_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
  dcache_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
  dlookup_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
  invalidate_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
  g_fcache_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));

  shm_slab_rwlock = (pthread_rwlock_t *)mlfs_alloc(sizeof(pthread_rwlock_t));
  shm_lru_rwlock = (pthread_rwlock_t *)mlfs_alloc(sizeof(pthread_rwlock_t));

  pthread_rwlock_init(icache_rwlock, &rwlattr);
  pthread_rwlock_init(dcache_rwlock, &rwlattr);
  pthread_rwlock_init(dlookup_rwlock, &rwlattr);
  pthread_rwlock_init(invalidate_rwlock, &rwlattr);
  pthread_rwlock_init(g_fcache_rwlock, &rwlattr);

  pthread_rwlock_init(shm_slab_rwlock, &rwlattr);
  pthread_rwlock_init(shm_lru_rwlock, &rwlattr);
}

void init_fs(void)
{
#ifdef USE_SLAB
  unsigned long memsize_gb = 4;
#endif

  if (!strata_initialized) {
    const char *perf_profile;
    const char *device_id;
    uint8_t dev_id;
    int i;

    device_id = getenv("DEV_ID");
    g_idx_choice = get_indexing_choice();
    g_idx_cached = get_indexing_is_cached();
    g_idx_has_parallel_lookup = get_idx_has_parallel_lookup();

    // TODO: range check.
    if (device_id)
      dev_id = atoi(device_id);
    else
      dev_id = 4;

#ifdef USE_SLAB
    mlfs_slab_init(memsize_gb << 30);
#endif
    g_ssd_dev = 2;
    g_hdd_dev = 3;
    // iangneal: fix me
    g_log_dev = dev_id;
    //g_log_dev = g_ssd_dev;

    // This is allocated from slab, which is shared
    // between parent and child processes.
    disk_sb = (struct disk_superblock *)mlfs_zalloc(
        sizeof(struct disk_superblock) * (g_n_devices + 1));

    for (i = 0; i < g_n_devices + 1; i++)
      sb[i] = (struct super_block *)mlfs_zalloc(sizeof(struct super_block));

    device_init();

    debug_init();

    cache_init();

    //shared_memory_init();

    locks_init();

    read_superblock(g_root_dev);
#ifdef USE_SSD
    read_superblock(g_ssd_dev);
#endif
#ifdef USE_HDD
    read_superblock(g_hdd_dev);
#endif
    read_superblock(g_log_dev);

    mlfs_file_init();

    init_log(g_log_dev);

    // read root inode in NVM
    read_root_inode(g_root_dev);
    if(IDXAPI_IS_HASHFS()) {
      struct super_block *sblk = sb[g_root_dev];
      pmem_nvm_hash_table_new(sblk->ondisk, NULL);
    }

    mlfs_info("LibFS is initialized with id %d\n", g_log_dev);

    strata_initialized = 1;

    perf_profile = getenv("MLFS_PROFILE");

    if (perf_profile) {
      enable_perf_stats = 1;
      mlfs_info("%s", " enable profile\n");
      char prof_fn[256];
      sprintf(prof_fn, "/tmp/libfs_prof.%d", getpid());
      assert((prof_fd = open(prof_fn, O_CREAT | O_TRUNC | O_WRONLY, 0666)) != -1);
    } else {
      enable_perf_stats = 0;
      mlfs_info("%s", " disable profile\n");
    }

    if (IDXAPI_IS_GLOBAL()) {
        init_hash(sb[g_root_dev], enable_perf_stats);
    }

    reset_libfs_stats();
  }

}

///////////////////////////////////////////////////////////////////////
// Physical block management

void read_superblock(uint8_t dev)
{
  uint32_t inum;
  int ret;
  struct buffer_head *bh;
  struct dinode dip;

  // 1 is superblock address
  bh = bh_get_sync_IO(dev, 1, BH_NO_DATA_ALLOC);

  bh->b_size = g_block_size_bytes;
  bh->b_data = mlfs_zalloc(g_block_size_bytes);

  bh_submit_read_sync_IO(bh);
  mlfs_io_wait(dev, 1);

  if (!bh)
    panic("cannot read superblock\n");

  memmove(&disk_sb[dev], bh->b_data, sizeof(struct disk_superblock));

  mlfs_debug("[dev %d] superblock: size %u nblocks %u ninodes %u "
      "inodestart %lx bmap start %lx datablock_start %lx\n",
      dev,
      disk_sb[dev].size,
      disk_sb[dev].ndatablocks,
      disk_sb[dev].ninodes,
      disk_sb[dev].inode_start,
      disk_sb[dev].bmap_start,
      disk_sb[dev].datablock_start);

  sb[dev]->ondisk = &disk_sb[dev];

  sb[dev]->s_inode_bitmap = (unsigned long *)
    mlfs_zalloc(BITS_TO_LONGS(disk_sb[dev].ninodes));

  if (dev == g_root_dev) {
    // setup inode allocation bitmap.
    for (inum = 1; inum < disk_sb[dev].ninodes; inum++) {
      read_ondisk_inode(dev, inum, &dip);

      if (dip.itype != 0)
        bitmap_set(sb[dev]->s_inode_bitmap, inum, 1);
    }
  }

  mlfs_free(bh->b_data);
  bh_release(bh);
}

void read_root_inode(uint8_t dev_id)
{
  struct dinode _dinode;
  struct inode *ip;

  read_ondisk_inode(dev_id, ROOTINO, &_dinode);
  _dinode.dev = dev_id;
  mlfs_debug("root inode block %lx size %lu\n",
      IBLOCK(ROOTINO, disk_sb[dev_id]), dip->size);

  mlfs_assert(_dinode.itype == T_DIR);

  ip = ialloc(dev_id, ROOTINO, &_dinode);
}

int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip)
{
  int ret;
  struct buffer_head *bh;
  uint8_t _dev = dev;
  addr_t inode_block;

  mlfs_assert(dev == g_root_dev);

  inode_block = get_inode_block(dev, inum);
  bh = bh_get_sync_IO(dev, inode_block, BH_NO_DATA_ALLOC);

  if (dev == g_root_dev) {
    bh->b_size = sizeof(struct dinode);
    bh->b_data = (uint8_t *)dip;
    bh->b_offset = sizeof(struct dinode) * (inum % IPB);
    bh_submit_read_sync_IO(bh);
    mlfs_io_wait(dev, 1);
  } else {
    panic("This code path is deprecated\n");

    bh->b_size = g_block_size_bytes;
    bh->b_data = mlfs_zalloc(g_block_size_bytes);

    bh_submit_read_sync_IO(bh);
    mlfs_io_wait(dev, 1);

    memmove(dip, (struct dinode*)bh->b_data + (inum % IPB), sizeof(struct dinode));

    mlfs_free(bh->b_data);
  }

  bh_release(bh);

  return 0;
}

int sync_inode_ext_tree(uint8_t dev, struct inode *inode)
{
  //if (inode->flags & I_RESYNC) {
    struct buffer_head *bh;
    struct dinode dinode;
    int ret = 0;

    mlfs_assert(dev == g_root_dev);

    read_ondisk_inode(dev, inode->inum, &dinode);

    iwrlock(inode);

    if (IDXAPI_IS_PER_FILE() && inode->ext_idx) {
        FN(inode->ext_idx, im_clear_metadata, inode->ext_idx);
        
        if (g_idx_cached) {
            ret = FN(inode->ext_idx, im_invalidate, inode->ext_idx);
        }
    } else {
        memmove(inode->l1.addrs, dinode.l1_addrs, sizeof(addr_t) * (NDIRECT + 1));
    }
        
#ifdef USE_SSD
        memmove(inode->l2.addrs, dinode.l2_addrs, sizeof(addr_t) * (NDIRECT + 1));
#endif
#ifdef USE_HDD
        memmove(inode->l3.addrs, dinode.l3_addrs, sizeof(addr_t) * (NDIRECT + 1));
#endif

    iunlock(inode);

    /*
    if (inode->itype == T_DIR)
      mlfs_info("resync inode (DIR) %u is done\n", inode->inum);
    else
      mlfs_info("resync inode %u is done\n", inode->inum);
    */
  //}

  inode->flags &= ~I_RESYNC;
  return ret;
}

// Allocate "in-memory" inode.
// on-disk inode is created by icreate
struct inode* ialloc(uint8_t dev, uint32_t inum, struct dinode *dip)
{
  int ret;
  struct inode *ip;
  pthread_rwlockattr_t rwlattr;

  mlfs_assert(dev == g_root_dev);

  ip = icache_find(dev, inum);
  if (!ip)
    ip = icache_alloc_add(dev, inum);

  ip->_dinode = (struct dinode *)ip;

  if (ip->flags & I_DELETING) {
    // There is the case where unlink in the update log is not yet digested.
    // Then, ondisk inode does contain a stale information.
    // So, skip syncing with ondisk inode.
    memset(ip->_dinode, 0, sizeof(struct dinode));
    ip->dev = dev;
    ip->inum = inum;
    mlfs_debug("reuse inode - inum %u\n", inum);
  } else {
    sync_inode_from_dinode(ip, dip);
    mlfs_debug("get inode - inum %u\n", inum);
  }

  ip->flags = 0;
  ip->flags |= I_VALID;
  ip->i_ref = 1;
  ip->n_de_cache_entry = 0;
  ip->i_dirty_dblock = RB_ROOT;
  ip->i_sb = sb;

  pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

  pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
  ip->fcache = NULL;
  ip->n_fcache_entries = 0;

#ifdef KLIB_HASH
  mlfs_debug("allocate hash %u\n", ip->inum);
  ip->fcache_hash = kh_init(fcache);
#endif

  ip->de_cache = NULL;
  pthread_spin_init(&ip->de_cache_spinlock, PTHREAD_PROCESS_SHARED);

  INIT_LIST_HEAD(&ip->i_slru_head);

  pthread_rwlock_init(&ip->i_rwlock, NULL);

  bitmap_set(sb[dev]->s_inode_bitmap, inum, 1);

  return ip;
}

// Allocate a new inode with the given type on device dev.
// A free inode has a type of zero.
struct inode* icreate(uint8_t dev, uint8_t type)
{
  uint32_t inum;
  int ret;
  struct dinode dip;
  struct inode *ip;
  pthread_rwlockattr_t rwlattr;

  // FIXME: hard coded. used for testing multiple applications.
  if (g_log_dev == 4)
    inum = find_next_zero_bit(sb[dev]->s_inode_bitmap,
        sb[dev]->ondisk->ninodes, 1);
  else
    inum = find_next_zero_bit(sb[dev]->s_inode_bitmap,
        sb[dev]->ondisk->ninodes, NINODES/2);

  read_ondisk_inode(dev, inum, &dip);

  // Clean (in-memory in block cache) ondisk inode.
  // At this point, storage and in-memory state diverges.
  // Libfs does not write dip directly, and kernFS will
  // update the dip on storage when digesting the inode.
  setup_ondisk_inode(&dip, dev, type);

  ip = ialloc(dev, inum, &dip);

  return ip;
}

/* Inode (in-memory) cannot be freed at this point.
 * If the inode is freed, libfs will read on-disk inode from
 * read-only area. This cause a problem since the on-disk deletion
 * is not applied yet in kernfs (before digest).
 * idealloc() marks the inode as deleted (I_DELETING). The inode is
 * removed from icache when digesting the inode.
 * from icache. libfs will free the in-memory inode after digesting
 * a log of deleting the inode.
 */
int idealloc(struct inode *inode)
{
  struct inode *_inode;
  lru_node_t *l, *tmp;

  mlfs_assert(inode);
  mlfs_assert(inode->i_ref < 2);

  if (inode->i_ref == 1 &&
      (inode->flags & I_VALID) &&
      inode->nlink == 0) {
    if (inode->flags & I_BUSY)
      panic("Inode must not be busy!");

    /*
       if (inode->size > 0)
       itrunc(inode, 0);
    */

    inode->flags &= ~I_BUSY;
  }

  iwrlock(inode);
  inode->size = 0;
  /* After persisting the inode, libfs moves it to
   * deleted inode hash table in persist_log_inode() */
  inode->flags |= I_DELETING;
  inode->itype = 0;

  /* delete inode data (log) pointers */
  //printf("fcache del?\n");
  //FIXME why don't delete all fcache here?
  //fcache_del_all(inode);
  //inode->fcache_hash = kh_init(fcache);
  //printf("fcache del!\n");

  pthread_spin_destroy(&inode->de_cache_spinlock);
  pthread_rwlock_destroy(&inode->i_rwlock);
  pthread_rwlock_destroy(&inode->fcache_rwlock);

  iunlock(inode);

  mlfs_debug("dealloc inum %u\n", inode->inum);

#if 0 // TODO: do this in parallel by assigning a background thread.
  list_for_each_entry_safe(l, tmp, &inode->i_slru_head, list) {
    HASH_DEL(lru_hash_head, l);
    list_del(&l->list);
    mlfs_free_shared(l);
  }
#endif

  return 0;
}

// Copy a modified in-memory inode to log.
void iupdate(struct inode *ip)
{
  mlfs_assert(!(ip->flags & I_DELETING));

  if (!(ip->dinode_flags & DI_VALID))
    panic("embedded _dinode is invalid\n");

  if (ip->_dinode != (struct dinode *)ip)
    panic("_dinode pointer is incorrect\n");

  mlfs_get_time(&ip->mtime);
  ip->atime = ip->mtime;

  add_to_loghdr(L_TYPE_INODE_UPDATE, ip, 0,
      sizeof(struct dinode), NULL, 0);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
struct inode* iget(uint8_t dev, uint32_t inum)
{
  struct inode *ip;

  ip = icache_find(dev, inum);

  if (ip) {
    if ((ip->flags & I_VALID) && (ip->flags & I_DELETING))
      return NULL;

    iwrlock(ip);
    ip->i_ref++;
    iunlock(ip);
  } else {
    struct dinode dip;
    // allocate new in-memory inode
    mlfs_debug("allocate new inode by iget %u\n", inum);
    read_ondisk_inode(dev, inum, &dip);

    ip = ialloc(dev, inum, &dip);
  }

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode* idup(struct inode *ip)
{
  panic("does not support idup yet\n");

  return ip;
}

/* iput does not deallocate inode. it just drops reference count.
 * An inode is explicitly deallocated by ideallc()
 */
void iput(struct inode *ip)
{
  iwrlock(ip);

  mlfs_muffled("iput num %u ref %u nlink %u\n",
      ip->inum, ip->ref, ip->nlink);

  ip->i_ref--;

  iunlock(ip);
}

// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

/* Get block addresses from extent trees.
 * return = 0, if all requested offsets are found.
 * return = -EAGAIN, if not all blocks are found.
 *
 */
int bmap(struct inode *ip, struct bmap_request *bmap_req)
{
  int ret = 0;
  handle_t handle;
  offset_t offset = bmap_req->start_offset;

  if (ip->itype == T_DIR) {
    bmap_req->block_no = ip->l1.addrs[(offset >> g_block_size_shift)];
    bmap_req->blk_count_found = 1;
    bmap_req->dev = ip->dev;

    return 0;
  }
  /*
  if (ip->itype == T_DIR) {
    handle.dev = g_root_dev;
    struct mlfs_map_blocks map;

    map.m_lblk = (offset >> g_block_size_shift);
    map.m_len = bmap_req->blk_count;

    ret = mlfs_ext_get_blocks(&handle, ip, &map, 0);

    if (ret == bmap_req->blk_count)
      bmap_req->blk_count_found = ret;
    else
      bmap_req->blk_count_found = 0;

    return 0;
  }
  */
  else if (ip->itype == T_FILE) {
    struct mlfs_map_blocks map;

    map.m_lblk = (offset >> g_block_size_shift);
    map.m_len = bmap_req->blk_count;
    map.m_flags = 0;

    // get block address from extent tree.
    mlfs_assert(ip->dev == g_root_dev);

    // L1 search
    handle.dev = g_root_dev;

    ret = mlfs_ext_get_blocks(&handle, ip, &map, 0);

    // all blocks are found in the L1 tree
    if (ret != 0) {
      bmap_req->blk_count_found = ret;
      bmap_req->dev = g_root_dev;
      bmap_req->block_no = map.m_pblk;
      mlfs_debug("physical block: %llu -> %llu\n", map.m_lblk, map.m_pblk);

      if (ret == bmap_req->blk_count) {
        mlfs_debug("[dev %d] Get all offset %lx: blockno %lx from NVM\n",
            g_root_dev, offset, map.m_pblk);
        return 0;
      } else {
        mlfs_debug("[dev %d] Get partial offset %lx: blockno %lx from NVM\n",
            g_root_dev, offset, map.m_pblk);
        return -EAGAIN;
      }
    }

    // L2 search
#ifdef USE_SSD
    if (ret == 0) {
      struct inode *l2_ip;
      struct dinode l2_dip;

      l2_ip = ip;

      mlfs_assert(l2_ip);

      if (!(l2_ip->dinode_flags & DI_VALID)) {
        read_ondisk_inode(g_ssd_dev, ip->inum, &l2_dip);

        iwrlock(l2_ip);

        l2_ip->_dinode = (struct dinode *)l2_ip;
        sync_inode_from_dinode(l2_ip, &l2_dip);
        l2_ip->flags |= I_VALID;

        iunlock(l2_ip);
      }

      map.m_lblk = (offset >> g_block_size_shift);
      map.m_len = bmap_req->blk_count;
      map.m_flags = 0;

      handle.dev = g_ssd_dev;
      ret = mlfs_ext_get_blocks(&handle, l2_ip, &map, 0);

      mlfs_debug("search l2 tree: ret %d\n", ret);

#ifndef USE_HDD
      /* No blocks are found in all trees */
      if (ret == 0)
        return -EIO;
#else
      /* To L3 tree search */
      if (ret == 0)
        goto L3_search;
#endif
      bmap_req->blk_count_found = ret;
      bmap_req->dev = g_ssd_dev;
      bmap_req->block_no = map.m_pblk;

      mlfs_debug("[dev %d] Get offset %lu: blockno %lu from SSD\n",
          g_ssd_dev, offset, map.m_pblk);

      if (ret != bmap_req->blk_count)
        return -EAGAIN;
      else
        return 0;
    }
#endif
    // L3 search
#ifdef USE_HDD
L3_search:
    if (ret == 0) {
      struct inode *l3_ip;
      struct dinode l3_dip;

      l3_ip = ip;

      if (!(l3_ip->dinode_flags & DI_VALID)) {
        read_ondisk_inode(g_hdd_dev, ip->inum, &l3_dip);

        iwrlock(l3_ip);

        l3_ip->_dinode = (struct dinode *)l3_ip;
        sync_inode_from_dinode(l3_ip, &l3_dip);
        l3_ip->flags |= I_VALID;

        iunlock(l3_ip);
      }

      map.m_lblk = (offset >> g_block_size_shift);
      map.m_len = bmap_req->blk_count;
      map.m_flags = 0;

      handle.dev = g_hdd_dev;
      ret = mlfs_ext_get_blocks(&handle, l3_ip, &map, 0);

      mlfs_debug("search l3 tree: ret %d\n", ret);

      iwrlock(l3_ip);
      // iput() without deleting
      ip->i_ref--;
      iunlock(l3_ip);

      /* No blocks are found in all trees */
      if (ret == 0)
        return -EIO;

      bmap_req->blk_count_found = ret;
      bmap_req->dev = l3_ip->dev;
      bmap_req->block_no = map.m_pblk;

      mlfs_debug("[dev %d] Get offset %lx: blockno %lx from SSD\n",
          g_ssd_dev, offset, map.m_pblk);

      if (ret != bmap_req->blk_count)
        return -EAGAIN;
      else
        return 0;
    }
#endif
  }

  return -EIO;
}

/* Get block addresses from extent trees.
 * return = 0, if all requested offsets are found.
 * return = -EAGAIN, if not all blocks are found.
 *
 */
int bmap_api_parallel(struct inode *ip, struct bmap_request_arr *bmap_req_arr)
{
  int ret = 0;
  handle_t handle;
  offset_t offset = bmap_req_arr->start_offset;
  if (ip->itype == T_DIR) {
    bmap_req_arr->block_no[0] = ip->l1.addrs[(offset >> g_block_size_shift)];
    bmap_req_arr->blk_count_found = 1;
    bmap_req_arr->dev = ip->dev;

    return 0;
  } else if (ip->itype == T_FILE) {
    struct mlfs_map_blocks_arr map_arr;

    map_arr.m_lblk = (offset >> g_block_size_shift);
    map_arr.m_len = min(bmap_req_arr->blk_count, 8);
    map_arr.m_flags = 0;

    // get block address from extent tree.
    mlfs_assert(ip->dev == g_root_dev);

    // L1 search
    handle.dev = g_root_dev;
    ret = mlfs_api_get_blocks(&handle, ip, &map_arr, 0);

    // all blocks are found in the L1 tree
    if (ret != 0) {
      bmap_req_arr->blk_count_found = ret;
      bmap_req_arr->dev = g_root_dev;
      memcpy(bmap_req_arr->block_no, &(map_arr.m_pblk), bmap_req_arr->blk_count_found * sizeof(addr_t));

      if (ret == bmap_req_arr->blk_count) {
        return 0;
      } else {
        return -EAGAIN;
      }
    }
  } else {
      panic("bad path");
  }

  return -EIO;
}

/* Get block addresses from extent trees.
 * return = 0, if all requested offsets are found.
 * return = -EAGAIN, if not all blocks are found.
 *
 */
int bmap_hashfs(struct inode *ip, struct bmap_request_arr *bmap_req_arr)
{
  int ret = 0;
  handle_t handle;
  offset_t offset = bmap_req_arr->start_offset;
  if (ip->itype == T_DIR) {
    bmap_req_arr->block_no[0] = ip->l1.addrs[(offset >> g_block_size_shift)];
    bmap_req_arr->blk_count_found = 1;
    bmap_req_arr->dev = ip->dev;

    return 0;
  }
  /*
  if (ip->itype == T_DIR) {
    handle.dev = g_root_dev;
    struct mlfs_map_blocks map;

    map.m_lblk = (offset >> g_block_size_shift);
    map.m_len = bmap_req->blk_count;

    ret = mlfs_ext_get_blocks(&handle, ip, &map, 0);

    if (ret == bmap_req->blk_count)
      bmap_req->blk_count_found = ret;
    else
      bmap_req->blk_count_found = 0;

    return 0;
  }
  */
  else if (ip->itype == T_FILE) {
    struct mlfs_map_blocks_arr map_arr;

    map_arr.m_lblk = (offset >> g_block_size_shift);
    map_arr.m_len = bmap_req_arr->blk_count;
    map_arr.m_flags = 0;

    // get block address from extent tree.
    mlfs_assert(ip->dev == g_root_dev);

    // L1 search
    handle.dev = g_root_dev;
    ret = mlfs_hashfs_get_blocks(&handle, ip, &map_arr, 0);

    // all blocks are found in the L1 tree
    if (ret != 0) {
      bmap_req_arr->blk_count_found = ret;
      bmap_req_arr->dev = g_root_dev;
      memcpy(bmap_req_arr->block_no, &(map_arr.m_pblk), bmap_req_arr->blk_count_found * sizeof(addr_t));
      
      mlfs_debug("physical block: %llu -> %llu\n", map_arr.m_lblk, map_arr.m_pblk[0]);
      //printf("physical block: %llu -> %llu--%llu (%d)\n", map_arr.m_lblk, map_arr.m_pblk[0], map_arr.m_pblk[0] + ret, ret);

      if (ret == bmap_req_arr->blk_count) {
        mlfs_debug("[dev %d] Get all offset %lx: blockno %lx from NVM\n",
            g_root_dev, offset, map.m_pblk[0]);
        return 0;
      } else {
        mlfs_debug("[dev %d] Get partial offset %lx: blockno %lx from NVM\n",
            g_root_dev, offset, map.m_pblk[0]);
        return -EAGAIN;
      }
    }

    // L2 search
#ifdef USE_SSD
    if (ret == 0) {
      struct inode *l2_ip;
      struct dinode l2_dip;

      l2_ip = ip;

      mlfs_assert(l2_ip);

      if (!(l2_ip->dinode_flags & DI_VALID)) {
        read_ondisk_inode(g_ssd_dev, ip->inum, &l2_dip);

        iwrlock(l2_ip);

        l2_ip->_dinode = (struct dinode *)l2_ip;
        sync_inode_from_dinode(l2_ip, &l2_dip);
        l2_ip->flags |= I_VALID;

        iunlock(l2_ip);
      }

      map.m_lblk = (offset >> g_block_size_shift);
      map.m_len = bmap_req->blk_count;
      map.m_flags = 0;

      handle.dev = g_ssd_dev;
      ret = mlfs_ext_get_blocks(&handle, l2_ip, &map, 0);

      mlfs_debug("search l2 tree: ret %d\n", ret);

#ifndef USE_HDD
      /* No blocks are found in all trees */
      if (ret == 0)
        return -EIO;
#else
      /* To L3 tree search */
      if (ret == 0)
        goto L3_search;
#endif
      bmap_req->blk_count_found = ret;
      bmap_req->dev = g_ssd_dev;
      bmap_req->block_no = map.m_pblk;

      mlfs_debug("[dev %d] Get offset %lu: blockno %lu from SSD\n",
          g_ssd_dev, offset, map.m_pblk);

      if (ret != bmap_req->blk_count)
        return -EAGAIN;
      else
        return 0;
    }
#endif
    // L3 search
#ifdef USE_HDD
L3_search:
    if (ret == 0) {
      struct inode *l3_ip;
      struct dinode l3_dip;

      l3_ip = ip;

      if (!(l3_ip->dinode_flags & DI_VALID)) {
        read_ondisk_inode(g_hdd_dev, ip->inum, &l3_dip);

        iwrlock(l3_ip);

        l3_ip->_dinode = (struct dinode *)l3_ip;
        sync_inode_from_dinode(l3_ip, &l3_dip);
        l3_ip->flags |= I_VALID;

        iunlock(l3_ip);
      }

      map.m_lblk = (offset >> g_block_size_shift);
      map.m_len = bmap_req->blk_count;
      map.m_flags = 0;

      handle.dev = g_hdd_dev;
      ret = mlfs_ext_get_blocks(&handle, l3_ip, &map, 0);

      mlfs_debug("search l3 tree: ret %d\n", ret);

      iwrlock(l3_ip);
      // iput() without deleting
      ip->i_ref--;
      iunlock(l3_ip);

      /* No blocks are found in all trees */
      if (ret == 0)
        return -EIO;

      bmap_req->blk_count_found = ret;
      bmap_req->dev = l3_ip->dev;
      bmap_req->block_no = map.m_pblk;

      mlfs_debug("[dev %d] Get offset %lx: blockno %lx from SSD\n",
          g_ssd_dev, offset, map.m_pblk);

      if (ret != bmap_req->blk_count)
        return -EAGAIN;
      else
        return 0;
    }
#endif
  }

  return -EIO;
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
int itrunc(struct inode *ip, offset_t length)
{
  int ret = 0;
  struct buffer_head *bp;
  offset_t size;
  struct fcache_block *fc_block;
  offset_t key;

  mlfs_assert(ip);
  mlfs_assert(ip->itype == T_FILE);

  if (length == 0) {
    fcache_del_all(ip);
  } else if (length < ip->size) {
    /* invalidate all data pointers for log block.
     * If libfs only takes care of zero trucate case,
     * dropping entire hash table is OK.
     * It considers non-zero truncate */
    for (size = ((ip->size >> g_block_size_shift) << g_block_size_shift); 
         size > 0; 
         size -= g_block_size_bytes) {
      key = (size >> g_block_size_shift);

      fc_block = fcache_find(ip, key);
      if (fc_block) {
        if (fc_block->is_data_cached)
          list_del(&fc_block->l);
        if (fcache_del(ip, key)) {
            mlfs_free(fc_block);
        };
      }
    }
  }

  iwrlock(ip);

  ip->size = length;

  iunlock(ip);

  mlfs_get_time(&ip->mtime);

  iupdate(ip);

  return ret;
}

void stati(struct inode *ip, struct stat *st)
{
  mlfs_assert(ip);
  irdlock(ip);

  st->st_dev = ip->dev;
  st->st_ino = ip->inum;
  switch (ip->itype) {
      case T_DIR:
          st->st_mode = S_IFDIR;
          break;
      case T_FILE:
          st->st_mode = S_IFREG;
          break;
      default:
          panic("unknown file type");
  }
  st->st_mode |= S_IRWXU | S_IRGRP | S_IXGRP;
  st->st_nlink = ip->nlink;
  st->st_uid = 1000;
  st->st_gid = 1000;
  st->st_rdev = 0;
  st->st_size = ip->size;
  st->st_blksize = g_block_size_bytes;
  // This could be incorrect if there is file holes.
  st->st_blocks = ip->size / 512;

  st->st_mtime = (time_t)ip->mtime.tv_sec;
  st->st_ctime = (time_t)ip->ctime.tv_sec;
  st->st_atime = (time_t)ip->atime.tv_sec;
  iunlock(ip);
}

// TODO: Now, eviction is simply discarding. Extend this function
// to evict data to the update log.
static void evict_read_cache(struct inode *inode, uint32_t n_entries_to_evict)
{
  uint32_t i = 0;
  struct fcache_block *_fcache_block, *tmp;

  list_for_each_entry_safe_reverse(_fcache_block, tmp,
      &g_fcache_head.lru_head, l) {
    if (i > n_entries_to_evict)
      break;

    if (_fcache_block->is_data_cached) {
      mlfs_free(_fcache_block->data);

      //if (!_fcache_block->log_addr) {
        list_del(&_fcache_block->l);
        if(fcache_del(inode, _fcache_block->key)) {
            mlfs_free(_fcache_block);
        }
      //}

      g_fcache_head.n--;
      i++;
    }
  }
}

// Note that read cache does not copying data (parameter) to _fcache_block->data.
// Instead, _fcache_block->data points memory in data.
static struct fcache_block *add_to_read_cache(struct inode *inode,
    offset_t off, uint8_t *data)
{
  struct fcache_block *_fcache_block;

  _fcache_block = fcache_find(inode, (off >> g_block_size_shift));

  if (!_fcache_block) {
    _fcache_block = fcache_alloc_add(inode, (off >> g_block_size_shift), 0, 0);
    g_fcache_head.n++;
  } else {
    mlfs_assert(_fcache_block->is_data_cached == 0);
  }

  _fcache_block->is_data_cached = 1;
  _fcache_block->data = data;

  list_move(&_fcache_block->l, &g_fcache_head.lru_head);

  if (g_fcache_head.n > g_max_read_cache_blocks) {
    evict_read_cache(inode, g_fcache_head.n - g_max_read_cache_blocks);
  }

  return _fcache_block;
}

ssize_t do_unaligned_read(struct inode *ip, uint8_t *dst, offset_t off, size_t io_size) // always one block
{
  ssize_t io_done = 0;
  int ret;
  offset_t key, off_aligned;
  struct fcache_block *_fcache_block;
  uint64_t start_tsc, all_tsc;
  struct buffer_head *bh, *_bh;
  struct list_head io_list_log;
  bmap_req_t bmap_req;
  bmap_req_arr_t bmap_req_arr;

  if (enable_perf_stats) {
    all_tsc = asm_rdtscp();
    g_perf_stats.unaligned_read_nr++;
  }

  INIT_LIST_HEAD(&io_list_log);

  mlfs_assert(io_size < g_block_size_bytes);

  key = (off >> g_block_size_shift);

  off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);

  if (enable_perf_stats)
    start_tsc = asm_rdtscp();

  _fcache_block = fcache_find(ip, key);

  if (_fcache_block) {
    ret = check_read_log_invalidation(_fcache_block);
    if (ret) {
      if (fcache_del(ip, key)) {
          mlfs_free(_fcache_block);
      }
      _fcache_block = NULL;
    }
  }

  if (enable_perf_stats) {
    g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
    g_perf_stats.l0_search_nr++;
  }

  if (_fcache_block) {
    // read cache hit
      if (enable_perf_stats)
        start_tsc = asm_rdtscp();
    if (_fcache_block->is_data_cached) {
      memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
      list_move(&_fcache_block->l, &g_fcache_head.lru_head);

      if (enable_perf_stats) {
        g_perf_stats.end_to_end_read_tsc += asm_rdtscp() - all_tsc;
        g_perf_stats.end_to_end_read_nr += 1;
      }
      return io_size;
    }
    // the update log search
    else if (_fcache_block->log_addr) {
      addr_t block_no = _fcache_block->log_addr;
      uint16_t fc_off = _fcache_block->start_offset;
      mlfs_debug("GET from cache: blockno %lu (%lu, %lu, (%d,%d)) offset %lu(0x%lx) dst %lx size %lu\n",
              block_no, g_fs_log->start_blk, g_fs_log->next_avail, _fcache_block->log_version, g_fs_log->avail_version, off, off, dst, io_size);

      if (off - off_aligned < fc_off) { // incomplete fcache
          /*off_aligned   off   fc_off                 block_end
           *      |--------|-----|----------|--------------|
           *                                |
           *                            (off+io_size)
           */
          mlfs_debug("patching log %lu with start_offset %u\n", block_no, fc_off);
          uint8_t buffer[g_block_size_bytes];
          bmap_req.start_offset = off_aligned;
          bmap_req.blk_count = 1;
          bmap_req_arr.start_offset = off_aligned;
          bmap_req_arr.blk_count = 1;

          if (enable_perf_stats) start_tsc = asm_rdtscp();

          if(IDXAPI_IS_HASHFS()) {
            ret = bmap_hashfs(ip, &bmap_req_arr);
          } else {
            ret = bmap(ip, &bmap_req);
          }

          if (enable_perf_stats) {
              g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
              g_perf_stats.tree_search_nr++;
          }

          mlfs_assert(ret != -EIO);
          if(IDXAPI_IS_HASHFS()) {
            bh = bh_get_sync_IO(bmap_req_arr.dev, bmap_req_arr.block_no[0], BH_NO_DATA_ALLOC);
          } else {
            bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
          }
          
          bh->b_offset = 0;
          bh->b_data = buffer;
          bh->b_size = fc_off;
          bh_submit_read_sync_IO(bh);
          bh_release(bh);
          // patch existing log with missing data from the beginning of this block
          bh = bh_get_sync_IO(g_log_dev, block_no, BH_NO_DATA_ALLOC);
          bh->b_offset = 0;
          bh->b_data = buffer;
          bh->b_size = fc_off;
          mlfs_write(bh);
          bh_release(bh);
          _fcache_block->start_offset = 0;
      }
      // continue read either patched or already complete log
      bh = bh_get_sync_IO(g_fs_log->dev, block_no, BH_NO_DATA_ALLOC);
      mlfs_debug("physical block (log): %llu, %llu bytes\n", block_no, io_size);
      bh->b_offset = off - off_aligned;
      bh->b_data = dst;
      bh->b_size = io_size;

      if (enable_perf_stats) {
        g_perf_stats.ua_fcache_tsc += asm_rdtscp() - start_tsc;
        g_perf_stats.ua_fcache_nr++;
        start_tsc = asm_rdtscp();
      }

      bh_submit_read_sync_IO(bh);

      if (enable_perf_stats) {
        //printf("[%d] blkno = %llu, bh_size = %llu, io_size = %llu\n", __LINE__, bh->b_blocknr, bh->b_size, io_size);
        g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
        update_stats_dist(&(g_perf_stats.read_data_bytes), bh->b_size);
      }

      bh_release(bh);
      if (enable_perf_stats) {
        g_perf_stats.end_to_end_read_tsc += asm_rdtscp() - all_tsc;
        g_perf_stats.end_to_end_read_nr += 1;
      }
      return io_size;
    }
  }

  // global shared area search
  bmap_req.start_offset = off_aligned;
  bmap_req.blk_count_found = 0;
  bmap_req.blk_count = 1;
  bmap_req_arr.start_offset = off_aligned;
  bmap_req_arr.blk_count_found = 0;
  bmap_req_arr.blk_count = 1;

  if (enable_perf_stats)
    start_tsc = asm_rdtscp();

  // Get block address from shared area.
  if(IDXAPI_IS_HASHFS()) {
    ret = bmap_hashfs(ip, &bmap_req_arr);
  } else {
    ret = bmap(ip, &bmap_req);
  }
  

  if (enable_perf_stats) {
    g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
    g_perf_stats.tree_search_nr++;
  }

    if (enable_perf_stats) {
      start_tsc = asm_rdtscp();
    }
  mlfs_assert(ret != -EIO);
  if(IDXAPI_IS_HASHFS()) {
    bh = bh_get_sync_IO(bmap_req_arr.dev, bmap_req_arr.block_no[0], BH_NO_DATA_ALLOC);
  }
  else {
    bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
  }
    if (enable_perf_stats) {
      g_perf_stats.bh_meta_tsc = asm_rdtscp() - start_tsc;
      g_perf_stats.bh_meta_nr += 1;
    }

  // NVM case: no read caching.
  if ((IDXAPI_IS_HASHFS() && bmap_req_arr.dev == g_root_dev)
       || bmap_req.dev == g_root_dev) {
    if (enable_perf_stats) {
      start_tsc = asm_rdtscp();
    }

    bh->b_offset = off - off_aligned;
    bh->b_data = dst;
    bh->b_size = io_size;
    mlfs_debug("shared io size: %llu\n", io_size);

    bh_submit_read_sync_IO(bh);
    if (enable_perf_stats) {
        g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
        update_stats_dist(&(g_perf_stats.read_data_bytes), bh->b_size);
    }

    if (enable_perf_stats) {
      start_tsc = asm_rdtscp();
    }

    bh_release(bh);

    if (enable_perf_stats) {
      g_perf_stats.bh_meta_tsc = asm_rdtscp() - start_tsc;
      g_perf_stats.bh_meta_nr += 1;
    }

  }
  // SSD and HDD cache: do read caching.
  else {
    mlfs_assert(_fcache_block == NULL);

#if 0
    // TODO: Move block-level readahead to read cache
    if (bh->b_dev == g_ssd_dev)
      mlfs_readahead(g_ssd_dev, bh->b_blocknr, (128 << 10));
#endif

    bh->b_data = mlfs_alloc(bmap_req.blk_count_found << g_block_size_shift);
    bh->b_size = g_block_size_bytes;
    bh->b_offset = 0;

    _fcache_block = add_to_read_cache(ip, off_aligned, bh->b_data);

    if (enable_perf_stats)
      start_tsc = asm_rdtscp();

    bh_submit_read_sync_IO(bh);

    mlfs_io_wait(g_ssd_dev, 1);

    if (enable_perf_stats) {
        g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
        update_stats_dist(&(g_perf_stats.read_data_bytes), bh->b_size);
    }

    bh_release(bh);

    // copying cached data to user buffer
    memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
  }
  if (enable_perf_stats) {
    g_perf_stats.end_to_end_read_tsc += asm_rdtscp() - all_tsc;
    g_perf_stats.end_to_end_read_nr += 1;
  }
  return io_size;
}

ssize_t do_aligned_read(struct inode *ip, uint8_t *dst, offset_t off, size_t io_size)
{
  int io_to_be_done = 0, ret, i;
  offset_t key, _off, pos;
  struct fcache_block *_fcache_block;
  uint64_t start_tsc, all_tsc;
  struct buffer_head *bh, *_bh;
  struct list_head io_list, io_list_log;
  uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
  struct cache_copy_list copy_list[bitmap_size];
  bmap_req_t bmap_req;
  bmap_req_arr_t bmap_req_arr;

  if (enable_perf_stats) {
      all_tsc = asm_rdtscp();
      g_perf_stats.aligned_read_nr++;
  }

  DECLARE_BITMAP(io_bitmap, bitmap_size);

  bitmap_set(io_bitmap, 0, bitmap_size);

  memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size);

  INIT_LIST_HEAD(&io_list);
  INIT_LIST_HEAD(&io_list_log);

  mlfs_assert(io_size % g_block_size_bytes == 0);

  for (pos = 0, _off = off; pos < io_size;
      pos += g_block_size_bytes, _off += g_block_size_bytes) {
    key = (_off >> g_block_size_shift);

    if (enable_perf_stats)
      start_tsc = asm_rdtscp();

    _fcache_block = fcache_find(ip, key);

    if (enable_perf_stats) {
      g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
      g_perf_stats.l0_search_nr++;
    }

    if (_fcache_block) {
      ret = check_read_log_invalidation(_fcache_block);
      if (ret) {
        if (fcache_del(ip, key)) {
            //mlfs_free(_fcache_block);
        }
        _fcache_block = NULL;
      }
    }

    if (_fcache_block) {
      // read cache hit
      if (_fcache_block->is_data_cached) {
        copy_list[pos >> g_block_size_shift].dst_buffer = dst + pos;
        copy_list[pos >> g_block_size_shift].cached_data = _fcache_block->data;
        copy_list[pos >> g_block_size_shift].size = g_block_size_bytes;

        // move the fcache entry to head of LRU
        list_move(&_fcache_block->l, &g_fcache_head.lru_head);

        bitmap_clear(io_bitmap, (pos >> g_block_size_shift), 1);
        io_to_be_done++;

        mlfs_debug("read cache hit: offset %lu(0x%lx) size %u\n",
              off, off, io_size);
      }
      // the update log search
      else if (_fcache_block->log_addr) {
        addr_t block_no = _fcache_block->log_addr;
        uint16_t fc_off = _fcache_block->start_offset;

        mlfs_debug("GET from cache: blockno %lu (%lu, %lu, (%d,%d)) offset %lu(0x%lx) dst %lx size %lu\n",
              block_no, g_fs_log->start_blk, g_fs_log->next_avail, _fcache_block->log_version, g_fs_log->avail_version, _off, _off, dst, io_size);

        bh = bh_get_sync_IO(g_fs_log->dev, block_no, BH_NO_DATA_ALLOC);

        bh->b_offset = fc_off;
        bh->b_data = dst + pos + fc_off;
        bh->b_size = g_block_size_bytes;

        list_add_tail(&bh->b_io_list, &io_list_log);
        bitmap_clear(io_bitmap, (pos >> g_block_size_shift), 1);
        io_to_be_done++;
        if (fc_off > 0) { // incomplete fcache
            bmap_req.start_offset = _off;
            bmap_req.blk_count = 1;
            bmap_req_arr.start_offset = _off;
            bmap_req_arr.blk_count = 1;
            if (enable_perf_stats) {
                start_tsc = asm_rdtscp();
            }

            if(IDXAPI_IS_HASHFS()) {
              ret = bmap_hashfs(ip, &bmap_req_arr);
            }
            else {
              ret = bmap(ip, &bmap_req);
            }
            
            if(enable_perf_stats) {
                g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
                g_perf_stats.tree_search_nr++;
            }
            mlfs_assert(ret != -EIO);
            if(IDXAPI_IS_HASHFS()) {
              bh = bh_get_sync_IO(bmap_req.dev, bmap_req_arr.block_no[0], BH_NO_DATA_ALLOC);
            }
            else {
              bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
            }
            
            bh->b_offset = 0;
            bh->b_data = dst + pos;
            bh->b_size = fc_off;
            bh_submit_read_sync_IO(bh);
            bh_release(bh);
            // patch existing log with missing data from the beginning of this block
            bh = bh_get_sync_IO(g_log_dev, block_no, BH_NO_DATA_ALLOC);
            bh->b_offset = 0;
            bh->b_data = dst + pos;
            bh->b_size = fc_off;
            mlfs_write(bh);
            bh_release(bh);
            _fcache_block->start_offset = 0;
            mlfs_debug("patch log %lu with start_offset %u\n", block_no, fc_off);
        }
      }
    }
  }

  // All data come from the update log.
  if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
    if (enable_perf_stats)
      start_tsc = asm_rdtscp();

    list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
      if (enable_perf_stats) {
        update_stats_dist(&(g_perf_stats.read_data_bytes), bh->b_size);
      }
      bh_submit_read_sync_IO(bh);
      bh_release(bh);
    }

    if (enable_perf_stats) {
      g_perf_stats.read_data_tsc += asm_rdtscp() - start_tsc;
        g_perf_stats.end_to_end_read_tsc += asm_rdtscp() - all_tsc;
        g_perf_stats.end_to_end_read_nr += 1;
    }
    return io_size;
  }

  pblk_lookup_t to_lookup;
  to_lookup.dyn = 0; to_lookup.size = 0;

  if(io_size / g_block_size_bytes > MAX_NUM_BLOCKS_LOOKUP) {
		to_lookup.dyn = 1;
		to_lookup.m_pblk_dyn = (addr_t*)malloc((io_size >> g_block_size_shift) * sizeof(addr_t));
		to_lookup.m_lens_dyn = (uint32_t*)malloc((io_size >> g_block_size_shift) * sizeof(uint32_t));
    to_lookup.m_offsets_dyn = (offset_t*)malloc((io_size >> g_block_size_shift) * sizeof(offset_t));
	}



do_global_search:
  _off = off + (find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift);
  pos = find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift;
  bitmap_pos = find_first_bit(io_bitmap, bitmap_size);

  // global shared area search
  bmap_req.start_offset = _off;
  bmap_req.blk_count =
    find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos;
  bmap_req.dev = 0;
  bmap_req.block_no = 0;
  bmap_req.blk_count_found = 0;

  bmap_req_arr.start_offset = _off;
  bmap_req_arr.blk_count =
    min(MAX_GET_BLOCKS_RETURN, find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos);
  bmap_req_arr.dev = 0;
  // bmap_req_arr.block_no = 0;
  bmap_req_arr.blk_count_found = 0;

  if (enable_perf_stats) {
    start_tsc = asm_rdtscp();
  }

  bool use_req_arr = false;
  // Get block address from shared area.
  if (IDXAPI_IS_HASHFS()) {
    use_req_arr = true;
    ret = bmap_hashfs(ip, &bmap_req_arr);
  } else if (g_idx_has_parallel_lookup && bmap_req_arr.blk_count >= 8) {
    use_req_arr = true;
    ret = bmap_api_parallel(ip, &bmap_req_arr);
  } else {
    ret = bmap(ip, &bmap_req);
  }

  if (enable_perf_stats) {
    g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
    g_perf_stats.tree_search_nr++;
  }

  if (use_req_arr) bmap_req.dev = bmap_req_arr.dev;

  mlfs_assert(ret != -EIO);

  // NVM case: no read caching.
  int which_dev = use_req_arr ? bmap_req_arr.dev : bmap_req.dev;
  if (which_dev == g_root_dev) {
    if(use_req_arr) {
      for(size_t j = 0; j < bmap_req_arr.blk_count_found; ++j) {
        if(to_lookup.dyn) {
          to_lookup.m_pblk_dyn[to_lookup.size] = bmap_req_arr.block_no[j];
          to_lookup.m_lens_dyn[to_lookup.size] = 1;
          to_lookup.m_offsets_dyn[to_lookup.size] = pos + (j * g_block_size_bytes);
        } else {
          to_lookup.m_pblk[to_lookup.size] = bmap_req_arr.block_no[j];
          to_lookup.m_lens[to_lookup.size] = 1;
          to_lookup.m_offsets[to_lookup.size] = pos + (j * g_block_size_bytes);
        }
        ++to_lookup.size;

        // bh = bh_get_sync_IO(bmap_req_arr.dev, bmap_req_arr.block_no[j], BH_NO_DATA_ALLOC);
        // bh->b_offset = 0;
        // bh->b_data = dst + pos + (j * g_block_size_bytes);
        // bh->b_size = min(g_block_size_bytes, io_size);
        // list_add_tail(&bh->b_io_list, &io_list);
      }
    } else {
#if 0
      if(to_lookup.dyn) {
        to_lookup.m_pblk_dyn[to_lookup.size] = bmap_req.block_no;
        to_lookup.m_lens_dyn[to_lookup.size] = bmap_req.blk_count_found;
        to_lookup.m_offsets_dyn[to_lookup.size] = pos;
      } else {
        to_lookup.m_pblk[to_lookup.size] = bmap_req.block_no;
        to_lookup.m_lens[to_lookup.size] = bmap_req.blk_count_found;
        to_lookup.m_offsets[to_lookup.size] = pos;
      }
      ++to_lookup.size;
#else
      // iangneal: trying to see if this condensation is the source of some extra
      // savings that hashfs isn't getting
      for(size_t j = 0; j < bmap_req.blk_count_found; ++j) {
        if(to_lookup.dyn) {
          to_lookup.m_pblk_dyn[to_lookup.size] = bmap_req.block_no + j;
          to_lookup.m_lens_dyn[to_lookup.size] = 1;
          to_lookup.m_offsets_dyn[to_lookup.size] = pos + (j * g_block_size_bytes);
        } else {
          to_lookup.m_pblk[to_lookup.size] = bmap_req.block_no + j;
          to_lookup.m_lens[to_lookup.size] = 1;
          to_lookup.m_offsets[to_lookup.size] = pos + (j * g_block_size_bytes);
        }
        ++to_lookup.size;
      }
#endif

      // bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
      // bh->b_offset = 0;
      // bh->b_data = dst + pos;
      // bh->b_size = min((bmap_req.blk_count_found << g_block_size_shift), io_size);
      // list_add_tail(&bh->b_io_list, &io_list);
    }
    
  }
  // SSD and HDD cache: do read caching.
  else {
    offset_t cur, l;

#if 0
    // TODO: block-level read_ahead to read cache.
    if (bh->b_dev == g_ssd_dev)
      mlfs_readahead(g_ssd_dev, bh->b_blocknr, (256 << 10));
#endif

     /* The read cache is managed by 4 KB block.
     * For large IO size (e.g., 256 KB), we have two design options
     * 1. To make a large IO request to SSD. But, in this case, libfs
     *    must copy IO data to read cache for each 4 KB block.
     * 2. To make 4 KB requests for the large IO. This case does not
     *    need memory copy; SPDK could make read request with the
     *    read cache block.
     * Currently, I implement it with option 2
     */

    // register IO memory to read cache for each 4 KB blocks.
    // When bh finishes IO, the IO data will be in the read cache.
    
    if (enable_perf_stats) {
      start_tsc = asm_rdtscp();
    }

    for (cur = _off, l = 0; l < bmap_req.blk_count_found;
        cur += g_block_size_bytes, l++) {
      bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no + l, BH_NO_DATA_ALLOC);
      bh->b_data = mlfs_alloc(g_block_size_bytes);
      bh->b_size = g_block_size_bytes;
      bh->b_offset = 0;

      _fcache_block = add_to_read_cache(ip, cur, bh->b_data);

      copy_list[l].dst_buffer = dst + pos;
      copy_list[l].cached_data = _fcache_block->data;
      copy_list[l].size = g_block_size_bytes;
    }

    list_add_tail(&bh->b_io_list, &io_list);

    if (enable_perf_stats) {
      g_perf_stats.bh_meta_tsc = asm_rdtscp() - start_tsc;
      g_perf_stats.bh_meta_nr += 1;
    }
  }

  /* EAGAIN happens in two cases:
   * 1. A size of extent is smaller than bmap_req.blk_count. In this
   * case, subsequent bmap call starts finding blocks in next extent.
   * 2. A requested offset is not in the L1 tree. In this case,
   * subsequent bmap call starts finding blocks in other lsm tree.
   */
  if (ret == -EAGAIN) {
    if(use_req_arr) {
      bitmap_clear(io_bitmap, bitmap_pos, bmap_req_arr.blk_count_found);
      io_to_be_done += bmap_req_arr.blk_count_found;
    } else {
      bitmap_clear(io_bitmap, bitmap_pos, bmap_req.blk_count_found);
      io_to_be_done += bmap_req.blk_count_found;
    }
    

    goto do_global_search;
  } else {
    if(use_req_arr) {
      bitmap_clear(io_bitmap, bitmap_pos, bmap_req_arr.blk_count_found);
      io_to_be_done += bmap_req_arr.blk_count_found;
    } else {
      bitmap_clear(io_bitmap, bitmap_pos, bmap_req.blk_count_found);
      io_to_be_done += bmap_req.blk_count_found;
    }

    //mlfs_assert(bitmap_weight(io_bitmap, bitmap_size) == 0);
    if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
      goto do_global_search;
    }
  }
  mlfs_assert(io_to_be_done == (io_size >> g_block_size_shift));

  if (enable_perf_stats) {
    start_tsc = asm_rdtscp();
  }

  for (uint32_t i = 0; i < to_lookup.size; ++i) {
		addr_t curr_pblk = to_lookup.dyn ? to_lookup.m_pblk_dyn[i] : to_lookup.m_pblk[i];
		bh = bh_get_sync_IO(bmap_req.dev, curr_pblk, BH_NO_DATA_ALLOC);
		uint32_t curr_len = to_lookup.dyn ? to_lookup.m_lens_dyn[i] : to_lookup.m_lens[i];
        offset_t curr_off = to_lookup.dyn ? to_lookup.m_offsets_dyn[i] : to_lookup.m_offsets[i];
		bh->b_data = dst + curr_off;
		bh->b_size = min(curr_len << g_block_size_shift, io_size);
		bh->b_offset = 0;
		list_add_tail(&bh->b_io_list, &io_list);
	}

  if (enable_perf_stats) {
    g_perf_stats.bh_meta_tsc = asm_rdtscp() - start_tsc;
    g_perf_stats.bh_meta_nr += 1;
  }

do_io_aligned:
  //mlfs_assert(bitmap_weight(io_bitmap, bitmap_size) == 0);

  if (enable_perf_stats) {
    start_tsc = asm_rdtscp();
  }

  // Read data from L1 ~ trees
  //int ii = 0;
  list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {
    if (enable_perf_stats) {
        update_stats_dist(&(g_perf_stats.read_data_bytes), bh->b_size);
    }
    bh_submit_read_sync_IO(bh);
    bh_release(bh);
  }
  //if (enable_perf_stats) printf("\n");

  mlfs_io_wait(g_ssd_dev, 1);
  // At this point, read cache entries are filled with data.

  // copying read cache data to user buffer.
  for (i = 0 ; i < bitmap_size; i++) {
    if (copy_list[i].dst_buffer != NULL) {
      memmove(copy_list[i].dst_buffer, copy_list[i].cached_data,
          copy_list[i].size);

      if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
        panic("read request overruns the user buffer\n");
    }
  }

  // Patch data from log (L0) if up-to-date blocks are in the update log.
  // This is required when partial updates are in the update log.
  list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
    if (enable_perf_stats) {
      update_stats_dist(&(g_perf_stats.read_data_bytes), bh->b_size);
    }
    bh_submit_read_sync_IO(bh);
    bh_release(bh);
  }

  if (enable_perf_stats) {
    g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
  }

  if (enable_perf_stats) {
    g_perf_stats.end_to_end_read_tsc += asm_rdtscp() - all_tsc;
    g_perf_stats.end_to_end_read_nr += 1;
  }
  return io_size;
}

ssize_t readi(struct inode *ip, uint8_t *dst, offset_t off, size_t io_size)
{
  ssize_t ret = 0;
  uint8_t *_dst;
  offset_t _off, offset_end, offset_aligned, offset_small = 0;
  offset_t size_aligned = 0, size_prepended = 0, size_appended = 0, size_small = 0;
  ssize_t io_done;

  mlfs_assert(off < ip->size);

  if (off + io_size > ip->size)
    io_size = ip->size - off;

  _dst = dst;
  _off = off;

  offset_end = off + io_size;
  offset_aligned = ALIGN(off, g_block_size_bytes);

  // aligned read.
  if ((offset_aligned == off) &&
    (offset_end == ALIGN(offset_end, g_block_size_bytes))) {
    size_aligned = io_size;
  }
  // unaligned read.
  else {
    if ((offset_aligned == off && io_size < g_block_size_bytes) ||
        (offset_end < offset_aligned)) {
      offset_small = off - ALIGN_FLOOR(off, g_block_size_bytes);
      size_small = io_size;
    } else {
      if (off < offset_aligned) {
        size_prepended = offset_aligned - off;
      } else
        size_prepended = 0;

      size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
      if (size_appended > 0) {
        size_appended = g_block_size_bytes - size_appended;
      }

      size_aligned = io_size - size_prepended - size_appended;
    }
  }

  if (size_small) {
    io_done = do_unaligned_read(ip, _dst, _off, size_small);

    mlfs_assert(size_small == io_done);

    _dst += io_done;
    _off += io_done;
    ret += io_done;
  }

  if (size_prepended) {
    io_done = do_unaligned_read(ip, _dst, _off, size_prepended);

    mlfs_assert(size_prepended == io_done);

    _dst += io_done;
    _off += io_done;
    ret += io_done;
  }

  if (size_aligned) {
    io_done = do_aligned_read(ip, _dst, _off, size_aligned);

    mlfs_assert(size_aligned == io_done);

    _dst += io_done;
    _off += io_done;
    ret += io_done;
  }

  if (size_appended) {
    io_done = do_unaligned_read(ip, _dst, _off, size_appended);

    mlfs_assert(size_appended == io_done);

    _dst += io_done;
    _off += io_done;
    ret += io_done;
  }

  return ret;
}

// Write data to log
// add_to_log should handle the logging for both directory and file.
// 1. allocate blocks for log
// 2. add to log_header
size_t add_to_log(struct inode *ip, uint8_t *data, offset_t off, size_t size)
{
  offset_t total;
  uint32_t nr_iovec = 0;
  addr_t block_no;
  struct logheader_meta *loghdr_meta;

  mlfs_assert(ip != NULL);

  loghdr_meta = get_loghdr_meta();
  mlfs_assert(loghdr_meta);

  mlfs_assert(off + size > off);

  /*
  if (ip->itype == T_DIR) {
    add_to_loghdr(L_TYPE_DIR, ip, off, size);
    //dbg_check_dir(io_buf->data);
  }
  */

  if (ip->itype == T_FILE) {
    nr_iovec = loghdr_meta->nr_iovec;
    loghdr_meta->io_vec[nr_iovec].base = data;
    loghdr_meta->io_vec[nr_iovec].size = size;
    loghdr_meta->nr_iovec++;

    mlfs_assert(loghdr_meta->nr_iovec <= 9);
    add_to_loghdr(L_TYPE_FILE, ip, off, size, NULL, 0);
  } else
    panic("unknown inode type\n");

  if ((off + size) > ip->size)
    ip->size = off + size;

  return size;
}
