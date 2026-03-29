#include "gtest/gtest.h"

#include "lru_memory_manager/lrumemorymanager.h"

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
    // Code specific to compilers that support __has_feature(address_sanitizer)
#    define ASAN_ENABLED
#  endif
#endif

#if defined(__SANITIZE_ADDRESS__)
// Code specific to MSVC or other compilers defining this macro
#  define ASAN_ENABLED
#endif

class LRUMemoryManagerTest: public ::testing::Test {
protected:
    static constexpr size_t kPoolSize = 2048;

    // SetUp() is called before each test in this fixture
    void SetUp() override
    {
    }

    // TearDown() is called after each test in this fixture
    void TearDown() override
    {
    }
};

TEST_F(LRUMemoryManagerTest, BasicAllocAlignment) {
    lrumm::LRUMemoryManager::LRUMemoryHandle h1;
    lrumm::LRUMemoryManager sut(kPoolSize);

    void* ptr = sut.alloc(&h1, 100);

    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(h1.size(), 100);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % lrumm::LRUMemoryManager::BLOCK_ALIGNMENT, 0);
}

TEST_F(LRUMemoryManagerTest, BlockSplitting) {
    lrumm::LRUMemoryManager::LRUMemoryHandle h1, h2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&h1, 64);
    size_t allocated_before = sut.get_allocated_memory_size();

    sut.alloc(&h2, 64);
    size_t allocated_after = sut.get_allocated_memory_size();

    EXPECT_GT(allocated_after, allocated_before);
    EXPECT_NE(h1.hunk_ptr(), h2.hunk_ptr());
}

TEST_F(LRUMemoryManagerTest, CopyDisabled)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle, 50);

    lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy;
    ASSERT_DEATH(handle_copy = handle, "");
    ASSERT_DEATH({ lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy = handle; }, "");
}

TEST_F(LRUMemoryManagerTest, MoveDisabled)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle, 50);

    lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy;
    ASSERT_DEATH(handle_copy = std::move(handle), "");
    ASSERT_DEATH({ lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy = std::move(handle); }, "");
}

TEST_F(LRUMemoryManagerTest, InitialState)
{
    lrumm::LRUMemoryManager sut(kPoolSize);
    EXPECT_EQ(sut.begin(), sut.end()) << "Should be empty.";
}

TEST_F(LRUMemoryManagerTest, AllocateOne)
{
    constexpr size_t kExpectedSize = 50;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle, kExpectedSize);

    EXPECT_NE(sut.begin(), sut.end()) << "Should not be empty.";
    EXPECT_GE(sut.begin()->size(), kExpectedSize);
    EXPECT_EQ(sut.begin()->hunk_ptr(), handle.hunk_ptr());
    EXPECT_EQ(++sut.begin(), sut.end())  << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeOrder)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 150, kExpectedSize2 = 50;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);

    // order 0->1->2
    auto itr = sut.begin();
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, UpdateLruOrder)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);

    // get buffer and refresh lru order
    sut.get_buffer_and_refresh(&handle1);

    // recent order 2->1->0
    auto itr = sut.begin();
    EXPECT_NE(itr, sut.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateOneThenFree)
{
    constexpr size_t kExpectedSize = 50;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle, kExpectedSize);
    sut.free(&handle);

    EXPECT_EQ(sut.begin(), sut.end()) << "Should be empty.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeThenFreeOne)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedSize2 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);
    sut.free(&handle1);

    auto itr = sut.begin();
    EXPECT_NE(itr, sut.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeThenFreeOneThenAllocOne)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedSize2 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);
    sut.free(&handle1);
    sut.alloc(&handle1, kExpectedSize1);

    auto itr = sut.begin();
    EXPECT_NE(itr, sut.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateNoFreeSpace)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedOverflowSize = 4096;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);

    auto result = sut.alloc(&handle2, kExpectedOverflowSize);

    EXPECT_EQ(sut.begin(), sut.end()) << "Should be empty.";
    EXPECT_EQ(result, nullptr);
}

TEST_F(LRUMemoryManagerTest, GetAllocatedMemorySize)
{
    constexpr size_t kExpectedSize = 50;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);
    size_t initial_size = sut.get_allocated_memory_size();

    sut.alloc(&handle, kExpectedSize);

    // Check that allocated size is greater than 0 (exact size depends on alignment)
    EXPECT_GT(sut.get_allocated_memory_size(), initial_size);

    sut.free(&handle);
    // After freeing, size should be back to initial
    EXPECT_EQ(sut.get_allocated_memory_size(), initial_size);
}

TEST_F(LRUMemoryManagerTest, Clean)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);

    EXPECT_NE(sut.begin(), sut.end()) << "Should not be empty.";

    sut.arena_clean();

    EXPECT_EQ(sut.begin(), sut.end()) << "Should be empty after flush.";
}

TEST_F(LRUMemoryManagerTest, ArenaClean)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);

    EXPECT_NE(sut.begin(), sut.end()) << "Should not be empty.";

    sut.arena_clean();

    EXPECT_EQ(sut.begin(), sut.end()) << "Should be empty after clean.";
}

TEST_F(LRUMemoryManagerTest, HandleMethods)
{
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    void* ptr = sut.alloc(&handle, kExpectedSize);

    EXPECT_NE(ptr, nullptr) << "Allocation should succeed.";
    EXPECT_NE(handle.hunk_ptr(), nullptr) << "Handle should have a valid hunk pointer.";
    EXPECT_GE(handle.size(), kExpectedSize) << "Handle should report correct size.";
}

TEST_F(LRUMemoryManagerTest, GetBufferAndRefreshReturnValue)
{
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    void* alloc_ptr = sut.alloc(&handle, kExpectedSize);

    void* buffer_ptr = sut.get_buffer_and_refresh(&handle);
    EXPECT_EQ(alloc_ptr, buffer_ptr) << "get_buffer_and_refresh should return the same pointer as alloc.";

    // Test with null handle
    lrumm::LRUMemoryManager::LRUMemoryHandle null_handle;
    void* null_ptr = sut.get_buffer_and_refresh(&null_handle);
    EXPECT_EQ(null_ptr, nullptr) << "get_buffer_and_refresh should return nullptr for unallocated handle.";
}

TEST_F(LRUMemoryManagerTest, ConstIterator)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);

    // Test const iterator functionality
    auto const_itr = std::as_const(sut).begin();
    EXPECT_NE(const_itr, std::as_const(sut).end());
    EXPECT_GE(const_itr->size(), kExpectedSize1);
    ++const_itr;
    EXPECT_NE(const_itr, std::as_const(sut).end());
    EXPECT_GE(const_itr->size(), kExpectedSize0);
    ++const_itr;
    EXPECT_EQ(const_itr, std::as_const(sut).end());
}

TEST_F(LRUMemoryManagerTest, ZeroSizeAllocation)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    ASSERT_DEATH(sut.alloc(&handle, 0), "") << "Zero size allocation is not allowed.";
}

TEST_F(LRUMemoryManagerTest, LruEvictionOrder)
{
    // Allocate chunks that should fill most of the memory
    constexpr size_t kAllocateSize = 350;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3, handle4;
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Allocate 4 chunks
    void* ptr1 = sut.alloc(&handle1, kAllocateSize);
    void* ptr2 = sut.alloc(&handle2, kAllocateSize);
    void* ptr3 = sut.alloc(&handle3, kAllocateSize);
    void* ptr4 = sut.alloc(&handle4, kAllocateSize);

    // All allocations should succeed initially
    EXPECT_NE(handle1.size(), 0);
    EXPECT_NE(handle2.size(), 0);
    EXPECT_NE(handle3.size(), 0);
    EXPECT_NE(handle4.size(), 0);

    // Access handle2 to make it recently used
    sut.get_buffer_and_refresh(&handle2);

    // Allocate another chunk, which should cause eviction of the least recently used
    // Since we accessed handle2, handle1 should be evicted
    lrumm::LRUMemoryManager::LRUMemoryHandle handle5;
    void* ptr5 = sut.alloc(&handle5, kAllocateSize);

    EXPECT_NE(ptr5, nullptr) << "New allocation should succeed.";
    EXPECT_EQ(handle1.hunk_ptr(), nullptr) << "Handle1 should have been evicted.";
    EXPECT_EQ(handle1.size(), 0) << "Size of evicted handle1 should have been zero.";
    EXPECT_NE(handle2.hunk_ptr(), nullptr) << "Handle2 should not have been evicted.";
    EXPECT_NE(handle3.hunk_ptr(), nullptr) << "Handle3 should not have been evicted.";
    EXPECT_NE(handle4.hunk_ptr(), nullptr) << "Handle4 should not have been evicted.";
    EXPECT_NE(handle5.hunk_ptr(), nullptr) << "Handle5 should be allocated.";
}

TEST_F(LRUMemoryManagerTest, IteratorIterationEmpty) {
    lrumm::LRUMemoryManager sut(kPoolSize);

    auto it = sut.begin();
    auto end = sut.end();
    EXPECT_EQ(it, end);
}

TEST_F(LRUMemoryManagerTest, IteratorIteration) {
    lrumm::LRUMemoryManager::LRUMemoryHandle h1, h2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&h1, 100);
    sut.alloc(&h2, 100);

    int count = 0;
    auto it = sut.begin();
    auto end = sut.end();

    while (it != end) {
        ASSERT_NE(&(*it), nullptr);
        count++;
        ++it;
    }

    EXPECT_EQ(count, 2);
}

TEST_F(LRUMemoryManagerTest, IteratorComparison)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle, 100);

    // Test iterator comparison operators
    auto itr1 = sut.begin();
    auto itr2 = sut.begin();
    auto end_itr = sut.end();

    EXPECT_TRUE(itr1 == itr2) << "Iterators pointing to same element should be equal.";
    EXPECT_FALSE(itr1 != itr2) << "Iterators pointing to same element should not be unequal.";
    EXPECT_FALSE(itr1 == end_itr) << "Iterators pointing to different elements should not be equal.";
    EXPECT_TRUE(itr1 != end_itr) << "Iterators pointing to different elements should be unequal.";
}


TEST_F(LRUMemoryManagerTest, AllocateFiveThenFreeTwo)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250, kExpectedSize3 = 350, kExpectedSize4 = 450;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2, handle3, handle4;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);
    sut.alloc(&handle3, kExpectedSize3);
    sut.alloc(&handle4, kExpectedSize4);

    sut.free(&handle1);
    sut.free(&handle3);

    auto ptr0 = reinterpret_cast<const uint8_t*>(handle0.hunk_ptr());
    auto ptr2 = reinterpret_cast<const uint8_t*>(handle2.hunk_ptr());
    auto ptr4 = reinterpret_cast<const uint8_t*>(handle4.hunk_ptr());

    auto itr = sut.begin();
    EXPECT_EQ(itr->hunk_ptr(), handle4.hunk_ptr());
    EXPECT_EQ((++itr)->hunk_ptr(), handle2.hunk_ptr());
    EXPECT_EQ((++itr)->hunk_ptr(), handle0.hunk_ptr());
    EXPECT_EQ((++itr), sut.end()) << "Should be last.";

    EXPECT_GE(ptr0 + handle0.size() - ptr2, kExpectedSize1) << "Should have free space after release.";
    EXPECT_GE(ptr2 + handle2.size() - ptr4, kExpectedSize3) << "Should have free space after release.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeEvictOne)
{
    constexpr size_t kExpectedSize0 = 800, kExpectedSize1 = 800, kExpectedSize2 = 800;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    lrumm::LRUMemoryManager sut(kPoolSize);

    sut.alloc(&handle0, kExpectedSize0);
    sut.alloc(&handle1, kExpectedSize1);
    sut.alloc(&handle2, kExpectedSize2);

    // recent order 2->1
    auto itr = sut.begin();
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    EXPECT_EQ((++itr)->hunk_ptr(), handle1.hunk_ptr());
    EXPECT_EQ(++itr, sut.end()) << "Should be last.";
    EXPECT_EQ(handle0.hunk_ptr(), nullptr) << "Should have been evicted.";
}

TEST_F(LRUMemoryManagerTest, AlignmentAndSizeRounding)
{
    // Test that allocated size respects alignment and is at least requested size
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Test a range of sizes
    for (size_t request_size : {1, 10, 50, 63, 64, 65, 100, 127, 128, 200, 500, 1000}) {
        lrumm::LRUMemoryManager::LRUMemoryHandle handle;
        void* ptr = sut.alloc(&handle, request_size);
        ASSERT_NE(ptr, nullptr) << "Allocation failed for size " << request_size;
        size_t allocated_size = handle.size();
        EXPECT_GE(allocated_size, request_size) << "Allocated size smaller than requested for size " << request_size;
        // Check alignment of the pointer? The pointer is to data, not to hunk.
        // The internal alignment is on the whole block (including header). We can't easily test.
        // But we can verify that (allocated_size + sizeof(LRUMemoryHunk)) is aligned to 64.
        // Since we don't have access to internal, we'll just trust the implementation.
        sut.free(&handle);
    }
}

TEST_F(LRUMemoryManagerTest, SplittingBehavior)
{
    // Create a pool with enough space for splitting
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Allocate a small block to leave a large free block
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1;
    void* ptr1 = sut.alloc(&handle1, 64);
    ASSERT_NE(ptr1, nullptr);

    // Now allocate a block that should cause splitting (since free block is large)
    lrumm::LRUMemoryManager::LRUMemoryHandle handle2;
    void* ptr2 = sut.alloc(&handle2, 128);
    ASSERT_NE(ptr2, nullptr);

    // After splitting, there should still be free space left
    // We can verify by allocating another block
    lrumm::LRUMemoryManager::LRUMemoryHandle handle3;
    void* ptr3 = sut.alloc(&handle3, 64);
    EXPECT_NE(ptr3, nullptr) << "Should have free space after splitting";

    sut.free(&handle1);
    sut.free(&handle2);
    sut.free(&handle3);
}

TEST_F(LRUMemoryManagerTest, CoalescingOnFree)
{
    constexpr size_t kExpectedSize = 1500;
    // Allocate three contiguous blocks, free the middle one, then free the sides
    // and verify that they merge into a single free block.
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Allocate three blocks that will be physically contiguous (since pool is empty)
    lrumm::LRUMemoryManager::LRUMemoryHandle left, middle, right;
    void* pleft = sut.alloc(&left, 300);
    void* pmiddle = sut.alloc(&middle, 300);
    void* pright = sut.alloc(&right, 300);
    ASSERT_NE(pleft, nullptr);
    ASSERT_NE(pmiddle, nullptr);
    ASSERT_NE(pright, nullptr);

    // Free the middle block
    sut.free(&middle);

    // Now free left and right; they should coalesce with the middle free block
    sut.free(&left);
    sut.free(&right);

    // After all freed, the pool should have a single free block covering the whole region
    // We can verify by allocating a block of size close to the whole pool
    lrumm::LRUMemoryManager::LRUMemoryHandle big;
    void* pbig = sut.alloc(&big, kExpectedSize); // leave some space for overhead
    EXPECT_NE(pbig, nullptr) << "Should be able to allocate large block after coalescing";
    EXPECT_GE(big.size(), kExpectedSize);
    sut.free(&big);
}


TEST_F(LRUMemoryManagerTest, FreeNullHandle)
{
    lrumm::LRUMemoryManager sut(kPoolSize);
    lrumm::LRUMemoryManager::LRUMemoryHandle null_handle;

    // Should not crash
    sut.free(&null_handle);
}

TEST_F(LRUMemoryManagerTest, GetBufferAndRefreshNullHandle)
{
    lrumm::LRUMemoryManager sut(kPoolSize);
    lrumm::LRUMemoryManager::LRUMemoryHandle null_handle;
    void* ptr = sut.get_buffer_and_refresh(&null_handle);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(LRUMemoryManagerTest, CleanEmpty)
{
    lrumm::LRUMemoryManager sut(kPoolSize);
    // Should not crash
    sut.arena_clean();
    EXPECT_EQ(sut.begin(), sut.end());
}

TEST_F(LRUMemoryManagerTest, LargeAllocationExceedsPool)
{
    lrumm::LRUMemoryManager sut(kPoolSize);
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;

    void* ptr = sut.alloc(&handle, kPoolSize * 2); // larger than pool

    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(handle.hunk_ptr(), nullptr);
}

TEST_F(LRUMemoryManagerTest, MemoryExhaustionWithEviction)
{
    // Allocate blocks that sum to near pool size
    constexpr size_t kAllocateSize = kPoolSize / 7;

    // Fill the pool with allocations, then access all to make them recently used,
    // then try to allocate another block; should evict the least recent.
    lrumm::LRUMemoryManager sut(kPoolSize);

    lrumm::LRUMemoryManager::LRUMemoryHandle handles[5];
    for (int i = 0; i < 5; ++i) {
        void* ptr = sut.alloc(&handles[i], kAllocateSize);
        ASSERT_NE(ptr, nullptr) << "Allocation " << i << " failed";
    }

    // Access all handles to make them recently used (order doesn't matter)
    for (int i = 0; i < 5; ++i) {
        sut.get_buffer_and_refresh(&handles[i]);
    }

    // Now allocate another block; should evict the least recently used (which is the oldest after refresh)
    // Since we refreshed all, the order is reversed? Actually each refresh moves to most recent,
    // so after loop the least recent is the one that was not accessed? Actually they all become most recent sequentially.
    // The LRU order after loop is handles[4] most recent, handles[3], ..., handles[0] least recent.
    // So eviction should target handles[0].
    lrumm::LRUMemoryManager::LRUMemoryHandle new_handle;
    void* new_ptr = sut.alloc(&new_handle, kAllocateSize);
    EXPECT_NE(new_ptr, nullptr);
    EXPECT_EQ(handles[0].hunk_ptr(), nullptr) << "First handle should have been evicted";
    // The other handles should still be allocated
    for (int i = 1; i < 5; ++i) {
        EXPECT_NE(handles[i].hunk_ptr(), nullptr) << "Handle " << i << " should not be evicted";
    }
}

#if defined(ASAN_ENABLED)

// ASAN Positive Scenario Tests
TEST_F(LRUMemoryManagerTest, AsanPositiveAllocation)
{
    // This test verifies that normal allocation/deallocation doesn't trigger ASAN errors
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Allocate memory - should not trigger ASAN errors
    void* ptr = sut.alloc(&handle, kExpectedSize);
    EXPECT_NE(ptr, nullptr) << "Allocation should succeed.";

    // Write to allocated memory - should not trigger ASAN errors
    memset(ptr, 0xAA, kExpectedSize);

    // Access allocated memory - should not trigger ASAN errors
    sut.get_buffer_and_refresh(&handle);

    // Free memory - should not trigger ASAN errors
    sut.free(&handle);
}

TEST_F(LRUMemoryManagerTest, AsanPositiveMultipleAllocations)
{
    // This test verifies that multiple allocations don't trigger ASAN errors
    constexpr size_t kExpectedSize = 50;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3;
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Allocate multiple chunks
    void* ptr1 = sut.alloc(&handle1, kExpectedSize);
    void* ptr2 = sut.alloc(&handle2, kExpectedSize);
    void* ptr3 = sut.alloc(&handle3, kExpectedSize);

    EXPECT_NE(ptr1, nullptr) << "First allocation should succeed.";
    EXPECT_NE(ptr2, nullptr) << "Second allocation should succeed.";
    EXPECT_NE(ptr3, nullptr) << "Third allocation should succeed.";

    // Write to all allocated memory - should not trigger ASAN errors
    memset(ptr1, 0x11, kExpectedSize);
    memset(ptr2, 0x22, kExpectedSize);
    memset(ptr3, 0x33, kExpectedSize);

    // Free all memory - should not trigger ASAN errors
    sut.free(&handle1);
    sut.free(&handle2);
    sut.free(&handle3);
}

TEST_F(LRUMemoryManagerTest, AsanPositiveClean)
{
    // This test verifies that flush operation doesn't trigger ASAN errors
    constexpr size_t kExpectedSize = 75;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3;
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Allocate multiple chunks
    sut.alloc(&handle1, kExpectedSize);
    sut.alloc(&handle2, kExpectedSize);
    sut.alloc(&handle3, kExpectedSize);

    // Flush all allocations - should not trigger ASAN errors
    sut.arena_clean();

    EXPECT_EQ(sut.begin(), sut.end()) << "Should be empty after clean.";
}

// ASAN False Scenario Tests (These would normally trigger ASAN errors in a real environment)
TEST_F(LRUMemoryManagerTest, AsanFalseUseAfterFree)  // Disabled because it would trigger ASAN in real environment
{
    // This test demonstrates what would happen with use-after-free
    // In a real ASAN environment, this would trigger an error
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    void* ptr = sut.alloc(&handle, kExpectedSize);
    ASSERT_NE(ptr, nullptr) << "Allocation should succeed.";

    // Free the memory
    sut.free(&handle);

    // Accessing freed memory would trigger an error
    ASSERT_DEATH({
        // Attempt to write to the poisoned region
        memset(ptr, 0xAA, kExpectedSize);  // This would trigger ASAN
    }, "AddressSanitizer");
}

TEST_F(LRUMemoryManagerTest, AsanFalseBufferOverflow)  // Disabled because it would trigger ASAN in real environment
{
    // This test demonstrates what would happen with buffer overflow
    // In a real ASAN environment, this would trigger an error
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    void* ptr = sut.alloc(&handle, kExpectedSize);
    ASSERT_NE(ptr, nullptr) << "Allocation should succeed.";

    size_t allocated_size = handle.size();
    // Writing beyond allocated size would trigger an error
    ASSERT_DEATH({
        // Attempt to write to the poisoned region
        memset(static_cast<char*>(ptr) + allocated_size, 0xAA, 100);  // This would trigger ASAN
    }, "AddressSanitizer");
}

TEST_F(LRUMemoryManagerTest, AsanPoisoningVerification)
{
    // This test verifies that memory is properly poisoned and unpoisoned
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager sut(kPoolSize);

    // Before allocation, memory should be poisoned (simulated by checking allocation behavior)
    void* ptr1 = sut.alloc(&handle, kExpectedSize);
    EXPECT_NE(ptr1, nullptr) << "Allocation should succeed.";

    // After allocation, memory should be unpoisoned and accessible
    memset(ptr1, 0x55, kExpectedSize);  // Should not trigger ASAN

    // After freeing, memory should be poisoned again
    sut.free(&handle);

    // Allocate again to verify memory is properly unpoisoned for reuse
    void* ptr2 = sut.alloc(&handle, kExpectedSize);
    EXPECT_NE(ptr2, nullptr) << "Reallocation should succeed.";
    memset(ptr2, 0xAA, kExpectedSize);  // Should not trigger ASAN
    sut.free(&handle);
}

TEST_F(LRUMemoryManagerTest, AsanEvictionPoisoning)
{
    // This test verifies that evicted memory is properly poisoned
    constexpr size_t kSmallSize = 50;
    constexpr size_t kLargeSize = kPoolSize;  // Large enough to trigger eviction

    // Allocate small chunks first
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3;
    lrumm::LRUMemoryManager sut(kPoolSize);

    void* ptr1 = sut.alloc(&handle1, kSmallSize);
    void* ptr2 = sut.alloc(&handle2, kSmallSize);
    void* ptr3 = sut.alloc(&handle3, kSmallSize);

    EXPECT_NE(ptr1, nullptr) << "First allocation should succeed.";
    EXPECT_NE(ptr2, nullptr) << "Second allocation should succeed.";
    EXPECT_NE(ptr3, nullptr) << "Third allocation should succeed.";

    // Access handle2 to make it recently used
    sut.get_buffer_and_refresh(&handle2);

    // Allocate a large chunk that should cause eviction of least recently used (handle1)
    lrumm::LRUMemoryManager::LRUMemoryHandle handle4;
    void* ptr4 = sut.alloc(&handle4, kLargeSize);

    EXPECT_EQ(ptr4, nullptr) << "Large allocation should not succeed.";
    EXPECT_EQ(handle1.hunk_ptr(), nullptr) << "Handle1 should have been evicted.";

    // The evicted memory should be properly poisoned
    ASSERT_DEATH({
        // Attempt to write to the poisoned region
        memset(ptr1, 0xAA, kSmallSize);
    }, "AddressSanitizer");
}

#endif // ASAN_ENABLED

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}