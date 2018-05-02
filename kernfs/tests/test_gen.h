// vim: set ft=cpp:
#include <algorithm>
#include <list>
#include <memory>
#include <random>

/*
 * We support operations on logical blocks in the following sequences:
 * - SEQUENTIAL: 0 -> NUM_BLOCKS, by intervals of RANGE
 * - REVERSE:    NUM_BLOCKS - RANGE -> 0, by intervals of RANGE (allocation
 *   still done sequentially, so if range size is 100 then you'll allocate
 *   blocks 900->999, 800->899, etc.)
 * - RANDOM:     SHUFFLE(0 -> NUM_BLOCKS, by intervals of RANGE). So the blocks
 *   are random, but aligned to RANGE.
 */
enum SequenceType {
  SEQUENTIAL = 0, REVERSE, RANDOM
};

const char* SequenceTypeNames[] = { "sequential", "reverse", "random" };
const char* SequenceTypeAbbr[] = { "seq", "rev", "rand" };

struct TestInput {
  std::shared_ptr<inode_t> Inode;
  mlfs_lblk_t LogicalBlock;

  TestInput(std::shared_ptr<inode_t> inode, mlfs_lblk_t lblk):
    Inode(inode), LogicalBlock(lblk) {}
};

struct TestOutput {
  mlfs_fsblk_t PhysicalBlockExpected = 0;
  mlfs_fsblk_t PhysicalBlockActual = 0;

  bool IsCorrect() {
    return PhysicalBlockExpected == PhysicalBlockActual;
  }
};

struct TestCase {
  std::shared_ptr<TestInput> In;
  std::shared_ptr<TestOutput> Out;
};

class TestGenerator {
  private:
    std::random_device rd_;
    std::mt19937 mt_;
    handle_t dev_handle_;

    std::shared_ptr<inode_t> CreateInode(uint32_t inum);

    std::vector<mlfs_lblk_t> CreateBlockListSequential(size_t num, size_t skip);

  public:
    TestGenerator(uint8_t dev = g_root_dev): rd_(), mt_(rd_()) {
      dev_handle_ = {.dev = dev};
    }

    ~TestGenerator() {}

    std::list<TestCase> CreateTests(SequenceType s, size_t num_files,
        size_t blocks_per_file, size_t block_range_size);


};
