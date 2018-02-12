#include "kernfs_interface.h"
#include "../io/block_io.h"
#include "../io/buffer_head.h"
#include "../extents.h"
#include "../extents_bh.h"
#include "../fs.h"
#include "../balloc.h"
#include "../mlfs/mlfs_user.h"
#include "../global/global.h"
#include "../global/util.h"
#include "../global/defs.h"
#include "../storage/storage.h"
#include "../extents.h"
#include "../extents_bh.h"
#include "../slru.h"
#include "../migrate.h"

#include <random>
#include <iostream>
#include <list>
#include <vector>
#include <cassert>

#include "time_stat.h"

#define INUM 100

using namespace std;

class ExtentTest
{
  private:
  struct inode *inode;

  public:
  void initialize(void);

  void async_io_test(void);

  static void hexdump(void *mem, unsigned int len);

  void run_read_block_test(uint32_t inum, mlfs_lblk_t from,
    mlfs_lblk_t to, uint32_t nr_block);
  void run_multi_block_test(mlfs_lblk_t from, mlfs_lblk_t to,
    uint32_t nr_block);
  void run_ftruncate_test(mlfs_lblk_t from, mlfs_lblk_t to,
    uint32_t nr_block);
};

void ExtentTest::initialize(void)
{
#if 1
	int i;
	const char *perf_profile;

	g_ssd_dev = 2;
	g_hdd_dev = 3;
	g_log_dev = 4;

  cerr << "Start init" << endl;
#ifdef USE_SLAB
	mlfs_slab_init(3UL << 30);
#endif

	device_init();
  cerr << "Devices initialized." << endl;

	init_device_lru_list();

	shared_memory_init();
	cache_init(g_root_dev);

	for (i = 0; i < g_n_devices; i++)
		sb[i] = (super_block*)mlfs_zalloc(sizeof(struct super_block));

	read_superblock(g_root_dev);
	read_root_inode(g_root_dev);
	balloc_init(g_root_dev, sb[g_root_dev]);

#ifdef USE_SSD
	read_superblock(g_ssd_dev);
	balloc_init(g_ssd_dev, sb[g_ssd_dev]);
#endif
#ifdef USE_HDD
	read_superblock(g_hdd_dev);
	balloc_init(g_hdd_dev, sb[g_hdd_dev]);
#endif

	memset(&g_perf_stats, 0, sizeof(kernfs_stats_t));

	inode_version_table =
		(uint16_t *)mlfs_zalloc(sizeof(uint16_t) * NINODES);

	perf_profile = getenv("MLFS_PROFILE");

	if (perf_profile)
		enable_perf_stats = 1;
	else
		enable_perf_stats = 0;

	mlfs_debug("%s\n", "LIBFS is initialized");

#endif

  cerr << "Begin init" << endl;
  handle_t dev_handle = {.dev = g_root_dev};

  inode = ialloc(g_root_dev, T_FILE, INUM);
  mlfs_assert(inode);
  cerr << "inode allocated" << endl;

  struct mlfs_extent_header *ihdr;

  ihdr = ext_inode_hdr(&dev_handle, inode);
  cerr << "header allocated" << endl;

  // First creation of dinode of file
  if (ihdr->eh_magic != MLFS_EXT_MAGIC) {
    mlfs_debug("create new inode %u\n", inode->inum);
    memset(inode->l1.i_data, 0, sizeof(uint32_t) * 15);
    cerr << "i_data set" << endl;
    mlfs_ext_tree_init(&dev_handle, inode);
    cerr << "ext tree initialized" << endl;

    /* For testing purpose, those data is hard-coded. */
    inode->i_writeback = NULL;
    memset(inode->i_uuid, 0xCC, sizeof(inode->i_uuid));
    inode->i_csum = mlfs_crc32c(~0, inode->i_uuid, sizeof(inode->i_uuid));
    inode->i_csum =
      mlfs_crc32c(inode->i_csum, &inode->inum, sizeof(inode->inum));
    inode->i_csum = mlfs_crc32c(inode->i_csum, &inode->i_generation,
      sizeof(inode->i_generation));

    write_ondisk_inode(g_root_dev, inode);
  }

  cout << "Init finished." << endl;
  mlfs_debug("%s\n", "LIBFS is initialized");
}

#define HEXDUMP_COLS 8
void ExtentTest::hexdump(void *mem, unsigned int len)
{
  unsigned int i, j;

  for(i = 0; i < len + ((len % HEXDUMP_COLS) ?
        (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++) {
    /* print offset */
    if(i % HEXDUMP_COLS == 0) {
      printf("0x%06x: ", i);
    }

    /* print hex data */
    if(i < len) {
      printf("%02x ", 0xFF & ((char*)mem)[i]);
    } else {/* end of block, just aligning for ASCII dump */
      printf("  ");
    }

    /* print ASCII dump */
    if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1)) {
      for(j = i - (HEXDUMP_COLS - 1); j <= i; j++) {
        if(j >= len) { /* end of block, not really printing */
          printf(" ");
        } else if(isprint(((char*)mem)[j])) { /* printable char */
          printf("%c",(0xFF & ((char*)mem)[j]));
        } else {/* other char */
          printf(".");
        }
      }
      printf("\n");
    }
  }
}

void ExtentTest::async_io_test(void)
{
  int ret;
  struct buffer_head *bh;
  uint32_t from = 1000, to = 500000;
  std::random_device rd;
  std::mt19937 mt(rd());
  std::list<uint32_t> io_list;

  std::uniform_int_distribution<> dist(from, to);

  for (int i = 0; i < (to - from) / 2; i++)
  io_list.push_back(dist(mt));

  for (auto it : io_list) {
  bh = fs_get_bh(g_root_dev, it, &ret);

  memset(bh->b_data, 0, g_block_size_bytes);

  for (int i = 0; i < g_block_size_bytes ; i++)
    bh->b_data[i] = '0' + (i % 9);

  set_buffer_dirty(bh);
  fs_brelse(bh);
  }

  sync_all_buffers(g_bdev[g_root_dev]);
  //sync_writeback_buffers(g_bdev[g_root_dev]);

  int count = 0;
  for (auto it : io_list) {
  bh = fs_bread(g_root_dev, it, &ret);
  count++;

  for (int i = 0; i < g_block_size_bytes ; i++)
    if (bh->b_data[i] != '0' + (i % 9)) {
    fprintf(stderr, "count: %d/%d, read data mismatch at %u(0x%x)\n",
      count, (to - from) / 2, i, i);
    ExtentTest::hexdump(bh->b_data, 128);
    exit(-1);
    }
  }

  printf("%s: read matches data\n", __func__);
}

void ExtentTest::run_read_block_test(uint32_t inum, mlfs_lblk_t from,
  mlfs_lblk_t to, uint32_t nr_block)
{
  int ret;
  struct buffer_head bh_got;
  struct mlfs_map_blocks map;
  struct inode *ip;
  struct dinode *dip;

  dip = (struct dinode *)mlfs_alloc(sizeof(*dip));
  ret = read_ondisk_inode(g_root_dev, inum, dip);
  ip = (struct inode *)mlfs_alloc(sizeof(*ip));

  mlfs_assert(dip->itype != 0);

  ip->i_sb = &sb[g_root_dev];
  ip->_dinode = (struct dinode *)ip;
  sync_inode_from_dinode(ip, dip);
  ip->flags |= I_VALID;

  cout << "-------------------------------------------" << endl;
  /* populate all logical blocks */
  mlfs_lblk_t _from = from;
  for (int i = 0; _from <= to, i < nr_block; _from += g_block_size_bytes, i++) {
  map.m_lblk = (_from >> g_block_size_shift);
  map.m_len = 1;
  ret = mlfs_ext_get_blocks(NULL, ip, &map, 0);
  fprintf(stdout, "[dev %d] ret: %d, offset %u(0x%x) - block: %lx\n",
    g_root_dev, ret, _from, _from,  map.m_pblk);
  }


  ret = read_ondisk_inode(g_root_dev, inum, dip);
  ip = (struct inode *)mlfs_alloc(sizeof(*ip));

  mlfs_assert(dip->itype != 0);

  ip->i_sb = &sb[g_ssd_dev];
  ip->_dinode = (struct dinode *)ip;
  sync_inode_from_dinode(ip, dip);
  ip->flags |= I_VALID;

  cout << "-------------------------------------------" << endl;
  /* populate all logical blocks */
  _from = from;
  for (int i = 0; _from <= to, i < nr_block; _from += g_block_size_bytes, i++) {
  map.m_lblk = (_from >> g_block_size_shift);
  map.m_len = 1;
  ret = mlfs_ext_get_blocks(NULL, ip, &map, 0);
  fprintf(stdout, "[dev %d] ret: %d, offset %u(0x%x) - block: %lx\n",
    g_ssd_dev, ret, _from, _from,  map.m_pblk);
  }
}

void ExtentTest::run_multi_block_test(mlfs_lblk_t from,
  mlfs_lblk_t to, uint32_t nr_block)
{
  int err;
  struct buffer_head bh_got;
  struct mlfs_map_blocks map;
  struct time_stats ts;
  struct time_stats lookup;
  struct time_stats hs;
  struct time_stats ls;
  std::random_device rd;
  std::mt19937 mt(rd());
  std::vector<uint64_t> merge_list;
  mlfs_lblk_t start = from;

  handle_t handle = {.dev = g_root_dev};
  std::uniform_int_distribution<> dist(from, to);

  int ntests = (to - from) / (nr_block * g_block_size_bytes);

  // Create merge_list (list of random logical blocks)
  for (int i = 0; i < ntests; i++) {
    merge_list.push_back(dist(mt) >> g_block_size_shift);
  }

  time_stats_init(&ts, 1);
  time_stats_init(&lookup, 1);
  time_stats_init(&hs, ntests);
  time_stats_init(&ls, ntests);
#if 1
  time_stats_start(&ts);
  /* populate all logical blocks */
  for (; from <= to; from += (nr_block * g_block_size_bytes)) {
    map.m_lblk = (from >> g_block_size_shift);
    map.m_len = nr_block;
    time_stats_start(&hs);
    err = mlfs_ext_get_blocks(&handle, inode, &map,
      MLFS_GET_BLOCKS_CREATE);
    time_stats_stop(&hs);
    if (err < 0) {
      fprintf(stderr, "err: %s, offset %x, block: %lx\n",
        strerror(-err), from, map.m_pblk);
    }

    if (map.m_len != nr_block) {
      cout << "request nr_block " << nr_block <<
      " received nr_block " << map.m_len << endl;
      //exit(-1);
    }

    //fprintf(stdout, "INSERT [%d/%d] offset %u, block: %lx len %u\n",
    //    from, to, from, map.m_pblk, map.m_len);
  }
  time_stats_stop(&ts);
#endif


#if 1
  time_stats_start(&lookup);
  /* random lookup */
  for (uint64_t lblock : merge_list) {
    map.m_lblk = lblock;
    map.m_len = 1;
    map.m_pblk = 0;
    time_stats_start(&ls);
    err = mlfs_ext_get_blocks(&handle, inode, &map, MLFS_GET_BLOCKS_CREATE);
    time_stats_stop(&ls);

    if (err < 0) {
      fprintf(stderr, "err: %s, offset %x, block: %lx\n",
        strerror(-err), from, map.m_pblk);
    }

    //fprintf(stdout, "LOOKUP [%d/%d] offset %u, block: %x -> %lx len %u\n",
    //    i, (to - start), start, map.m_lblk, map.m_pblk, map.m_len);
  }
  time_stats_stop(&lookup);
#endif

  printf("** Total used block %d\n",
    bitmap_weight((uint64_t *)sb[g_root_dev]->s_blk_bitmap->bitmap,
    sb[g_root_dev]->ondisk->ndatablocks));

  cout << "truncate all allocated blocks" << endl;

  mlfs_ext_truncate(&handle, inode, 0, to >> g_block_size_shift);

  printf("** Total used block %d\n",
    bitmap_weight((uint64_t *)sb[g_root_dev]->s_blk_bitmap->bitmap,
    sb[g_root_dev]->ondisk->ndatablocks));

  cout << "INSERT TIME (total)" << endl;
  time_stats_print(&ts, NULL);
  cout << "INSERT TIME (per insert [" << hs.n <<"] )" << endl;
  time_stats_print(&hs, NULL);
  cout << "LOOKUP TIME (total)" << endl;
  time_stats_print(&lookup, NULL);
  cout << "LOOKUP TIME (per lookup [" << ls.n <<"] )" << endl;
  time_stats_print(&ls, NULL);
}

void ExtentTest::run_ftruncate_test(mlfs_lblk_t from,
  mlfs_lblk_t to, uint32_t nr_block)
{
  int ret;
  struct buffer_head bh_got;
  struct mlfs_map_blocks map;
  std::random_device rd;
  std::mt19937 mt(rd());
  std::list<mlfs_lblk_t> delete_list;
  std::uniform_int_distribution<> dist(from, to);
  handle_t handle = {.dev = g_root_dev};

  for (int i = 0; i < nr_block; i++)
  delete_list.push_back(dist(mt));

  for (auto it: delete_list) {
  ret = mlfs_ext_truncate(&handle, inode, (it >> g_block_size_shift),
    (it >> g_block_size_shift) + 1);

  fprintf(stdout, "truncate %u, ret %d\n", it, ret);

  /* Try to search truncated block.
   * mlfs_ext_get_block() must return 0
   */
  map.m_lblk = (it >> g_block_size_shift);
  map.m_len = 1;

  ret = mlfs_ext_get_blocks(NULL, inode, &map, 0);
  fprintf(stdout, "ret %d, block %lx, len %u\n",
    ret, map.m_pblk, map.m_len);
  }
}

int main(int argc, char **argv)
{
  ExtentTest cExtTest;
  cExtTest.initialize();

  //cExtTest.async_io_test();
  //cExtTest.run_read_block_test(3, 0, 20 * g_block_size_bytes, 10);
  //cExtTest.run_multi_block_test(0, 100000 * g_block_size_bytes, 4);
  cExtTest.run_multi_block_test(0, 100000 * g_block_size_bytes, 1);
  //cExtTest.run_ftruncate_test(1 * g_block_size_bytes, 10 * g_block_size_bytes, 5);

  return 0;
}
