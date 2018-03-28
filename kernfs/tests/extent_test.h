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
#ifdef HASHTABLE
#include "../inode_hash.h"
extern uint64_t reads;
extern uint64_t writes;
#endif

#include <sys/resource.h>
#include <algorithm>
#include <cassert>
#include <random>
#include <iostream>
#include <list>
#include <map>
#include <tuple>
#include <vector>
#include <unordered_set>
#include <malloc.h>

#include "time_stat.h"

using namespace std;

enum SequenceType {
  SEQUENTIAL = 0, REVERSE, RANDOM
};

const char* SequenceTypeNames[] = { "sequential", "reverse", "random" };
const char* SequenceTypeAbbr[] = { "seq", "rev", "rand" };

class ExtentTest
{
  private:
  struct inode *inode;

  public:
  void initialize(void);

  void async_io_test(void);

  static void hexdump(void *mem, unsigned int len);

  list<mlfs_lblk_t> genLogicalBlockSequence(SequenceType s, mlfs_lblk_t from,
      mlfs_lblk_t ti, uint32_t nr_block);

  void run_read_block_test(uint32_t inum, mlfs_lblk_t from,
    mlfs_lblk_t to, uint32_t nr_block);

  void run_multi_block_test(mlfs_lblk_t from, mlfs_lblk_t to,
    uint32_t nr_block);

  tuple<double, double> run_multi_block_test(list<mlfs_lblk_t> insert_order,
    list<mlfs_lblk_t> lookup_order, int nr_block = 1);

  void run_ftruncate_test(mlfs_lblk_t from, mlfs_lblk_t to,
    uint32_t nr_block);
};
