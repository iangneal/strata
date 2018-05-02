#include "test_gen.h"


std::shared_ptr<inode_t> TestGenerator::CreateInode(uint32_t inum) {
  struct mlfs_extent_header *ihdr;
  inode_t *inode = ialloc(dev_handle_.dev, T_FILE, inum);
  assert(inode);

  ihdr = ext_inode_hdr(&dev_handle_, inode);
  assert(ihdr);

  // First creation of dinode of file
  if (ihdr->eh_magic != MLFS_EXT_MAGIC) {
    memset(inode->l1.i_data, 0, sizeof(uint32_t) * 15);
    mlfs_ext_tree_init(&dev_handle_, inode);

    /* For testing purpose, those data is hard-coded. */
    inode->i_writeback = NULL;
    memset(inode->i_uuid, 0xCC, sizeof(inode->i_uuid));
    inode->i_csum = mlfs_crc32c(~0, inode->i_uuid, sizeof(inode->i_uuid));
    inode->i_csum =
      mlfs_crc32c(inode->i_csum, &inode->inum, sizeof(inode->inum));
    inode->i_csum = mlfs_crc32c(inode->i_csum, &inode->i_generation,
      sizeof(inode->i_generation));

    write_ondisk_inode(dev_handle_.dev, inode);
  }
}

std::vector<mlfs_lblk_t> TestGenerator::CreateBlockListSequential(
    size_t num, size_t skip) {
  assert(num);
  assert(skip);

  std::vector<mlfs_lblk_t> blocks;

  for (mlfs_fsblk_t b = 0; b < num; b += skip) {
    blocks.push_back(b);
  }

  return blocks;
}

std::list<TestCase> TestGenerator::CreateTests(
    SequenceType s,
    size_t num_files,
    size_t blocks_per_file,
    size_t block_range_size) {

  assert(blocks_per_file % block_range_size == 0);
  assert(s == SEQUENTIAL || s == REVERSE || s == RANDOM);

  size_t ntests = num_files * (blocks_per_file / block_range_size);

  std::vector<mlfs_lblk_t> blocks = CreateBlockListSequential(blocks_per_file,
      block_range_size);

  std::list<TestCase> cases;

  if (s == REVERSE) {
    std::reverse(blocks.begin(), blocks.end());
  }

  for (uint32_t inum = 0; inum < num_files; ++inum) {
    if (s == RANDOM) {
      std::random_shuffle(blocks.begin(), blocks.end());
    }

    std::shared_ptr<inode_t> inode = CreateInode(inum);

    for (const mlfs_lblk_t& lblk : blocks) {
      std::shared_ptr<TestInput> in(new TestInput(inode, lblk));
      std::shared_ptr<TestOutput> out(new TestOutput());

      cases.emplace_back(in, out);
    }
  }

  assert(cases.size() == ntests);

  return cases;
}
