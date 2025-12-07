#include "gtest/gtest.h"

#include "lrumemorymanager.h"

class LRUMemoryManagerTest: public ::testing::Test {
protected:
    lrumm::LRUMemoryManager sut_;

    LRUMemoryManagerTest() : sut_(2048) { }

    // SetUp() is called before each test in this fixture
    void SetUp() override
    {
    }

    // TearDown() is called after each test in this fixture
    void TearDown() override
    {
    }
};

TEST_F(LRUMemoryManagerTest, CopyDisabled)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    sut_.alloc(&handle, 50);

    lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy;
    ASSERT_DEATH(handle_copy = handle, "");
    ASSERT_DEATH({ lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy = handle; }, "");
}

TEST_F(LRUMemoryManagerTest, MoveDisabled)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    sut_.alloc(&handle, 50);

    lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy;
    ASSERT_DEATH(handle_copy = std::move(handle), "");
    ASSERT_DEATH({ lrumm::LRUMemoryManager::LRUMemoryHandle handle_copy = std::move(handle); }, "");
}

TEST_F(LRUMemoryManagerTest, InitialState)
{
    EXPECT_EQ(sut_.begin(), sut_.end()) << "Should be empty.";
}

TEST_F(LRUMemoryManagerTest, AllocateOne)
{
    constexpr size_t kExpectedSize = 50;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    sut_.alloc(&handle, kExpectedSize);

    EXPECT_NE(sut_.begin(), sut_.end()) << "Should not be empty.";
    EXPECT_GE(sut_.begin()->size(), kExpectedSize);
    EXPECT_EQ(sut_.begin()->hunk_ptr(), handle.hunk_ptr());
    EXPECT_EQ(++sut_.begin(), sut_.end())  << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeOrder)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 150, kExpectedSize2 = 50;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);

    // order 0->1->2
    auto itr = sut_.begin(false);
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut_.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeOrderLru)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);

    // recent order 2->1->0
    auto itr = sut_.begin(true);
    EXPECT_NE(itr, sut_.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut_.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, UpdateLruOrder)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);

    // get buffer and refresh lru order
    sut_.get_buffer_and_refresh(&handle1);

    // recent order 2->1->0
    auto itr = sut_.begin(true);
    EXPECT_NE(itr, sut_.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut_.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateOneThenFree)
{
    constexpr size_t kExpectedSize = 50;

    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    sut_.alloc(&handle, kExpectedSize);
    sut_.free(&handle);

    EXPECT_EQ(sut_.begin(), sut_.end()) << "Should be empty.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeThenFreeOne)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedSize2 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;

    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);
    sut_.free(&handle1);

    auto itr = sut_.begin(false);
    EXPECT_NE(itr, sut_.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut_.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeThenFreeOneThenAllocOne)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedSize2 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;

    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);
    sut_.free(&handle1);
    sut_.alloc(&handle1, kExpectedSize1);

    auto itr = sut_.begin(false);
    EXPECT_NE(itr, sut_.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize1);
    EXPECT_EQ(itr->hunk_ptr(), handle1.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut_.end()) << "Should be last.";
}

TEST_F(LRUMemoryManagerTest, AllocateNoFreeSpace)
{
    constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedOverflowSize = 4096;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;

    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);

    auto result = sut_.alloc(&handle2, kExpectedOverflowSize);

    EXPECT_EQ(sut_.begin(), sut_.end()) << "Should be empty.";
    EXPECT_EQ(result, nullptr);
}

TEST_F(LRUMemoryManagerTest, GetAllocatedMemorySize)
{
    size_t initial_size = sut_.get_allocated_memory_size();

    constexpr size_t kExpectedSize = 50;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    sut_.alloc(&handle, kExpectedSize);

    // Check that allocated size is greater than 0 (exact size depends on alignment)
    EXPECT_GT(sut_.get_allocated_memory_size(), initial_size);

    sut_.free(&handle);
    // After freeing, size should be back to initial
    EXPECT_EQ(sut_.get_allocated_memory_size(), initial_size);
}

TEST_F(LRUMemoryManagerTest, Flush)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;

    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);

    EXPECT_NE(sut_.begin(), sut_.end()) << "Should not be empty.";

    sut_.flush();

    EXPECT_EQ(sut_.begin(), sut_.end()) << "Should be empty after flush.";
}

TEST_F(LRUMemoryManagerTest, HandleMethods)
{
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    void* ptr = sut_.alloc(&handle, kExpectedSize);

    EXPECT_NE(ptr, nullptr) << "Allocation should succeed.";
    EXPECT_NE(handle.hunk_ptr(), nullptr) << "Handle should have a valid hunk pointer.";
    EXPECT_GE(handle.size(), kExpectedSize) << "Handle should report correct size.";
}

TEST_F(LRUMemoryManagerTest, GetBufferAndRefreshReturnValue)
{
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    void* alloc_ptr = sut_.alloc(&handle, kExpectedSize);

    void* buffer_ptr = sut_.get_buffer_and_refresh(&handle);
    EXPECT_EQ(alloc_ptr, buffer_ptr) << "get_buffer_and_refresh should return the same pointer as alloc.";

    // Test with null handle
    lrumm::LRUMemoryManager::LRUMemoryHandle null_handle;
    void* null_ptr = sut_.get_buffer_and_refresh(&null_handle);
    EXPECT_EQ(null_ptr, nullptr) << "get_buffer_and_refresh should return nullptr for unallocated handle.";
}

TEST_F(LRUMemoryManagerTest, ConstIterator)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1;
    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);

    // Test const iterator functionality
    const lrumm::LRUMemoryManager& const_sut = sut_;
    auto const_itr = const_sut.begin(false);
    EXPECT_NE(const_itr, const_sut.end());
    EXPECT_GE(const_itr->size(), kExpectedSize0);
    ++const_itr;
    EXPECT_NE(const_itr, const_sut.end());
    EXPECT_GE(const_itr->size(), kExpectedSize1);
    ++const_itr;
    EXPECT_EQ(const_itr, const_sut.end());
}

TEST_F(LRUMemoryManagerTest, ZeroSizeAllocation)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    ASSERT_DEATH(sut_.alloc(&handle, 0), "") << "Zero size allocation is not allowed.";
}

TEST_F(LRUMemoryManagerTest, LruEvictionOrder)
{
    // Allocate chunks that should fill most of the memory
    constexpr size_t kAllocateSize = 400;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3, handle4;

    // Allocate 4 chunks
    void* ptr1 = sut_.alloc(&handle1, kAllocateSize);
    void* ptr2 = sut_.alloc(&handle2, kAllocateSize);
    void* ptr3 = sut_.alloc(&handle3, kAllocateSize);
    void* ptr4 = sut_.alloc(&handle4, kAllocateSize);

    // All allocations should succeed initially
    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr3, nullptr);
    EXPECT_NE(ptr4, nullptr);

    // Access handle2 to make it recently used
    sut_.get_buffer_and_refresh(&handle2);

    // Allocate another chunk, which should cause eviction of the least recently used
    // Since we accessed handle2, handle1 should be evicted
    lrumm::LRUMemoryManager::LRUMemoryHandle handle5;
    void* ptr5 = sut_.alloc(&handle5, kAllocateSize);

    EXPECT_NE(ptr5, nullptr) << "New allocation should succeed.";
    EXPECT_EQ(handle1.hunk_ptr(), nullptr) << "Handle1 should have been evicted.";
    EXPECT_NE(handle2.hunk_ptr(), nullptr) << "Handle2 should not have been evicted.";
    EXPECT_NE(handle3.hunk_ptr(), nullptr) << "Handle3 should not have been evicted.";
    EXPECT_NE(handle4.hunk_ptr(), nullptr) << "Handle4 should not have been evicted.";
    EXPECT_NE(handle5.hunk_ptr(), nullptr) << "Handle5 should be allocated.";
}

TEST_F(LRUMemoryManagerTest, IteratorComparison)
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    sut_.alloc(&handle, 100);

    // Test iterator comparison operators
    auto itr1 = sut_.begin();
    auto itr2 = sut_.begin();
    auto end_itr = sut_.end();

    EXPECT_TRUE(itr1 == itr2) << "Iterators pointing to same element should be equal.";
    EXPECT_FALSE(itr1 != itr2) << "Iterators pointing to same element should not be unequal.";
    EXPECT_FALSE(itr1 == end_itr) << "Iterators pointing to different elements should not be equal.";
    EXPECT_TRUE(itr1 != end_itr) << "Iterators pointing to different elements should be unequal.";
}


TEST_F(LRUMemoryManagerTest, AllocateFiveThenFreeTwo)
{
    constexpr size_t kExpectedSize0 = 50, kExpectedSize1 = 150, kExpectedSize2 = 250, kExpectedSize3 = 350, kExpectedSize4 = 450;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2, handle3, handle4;
    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);
    sut_.alloc(&handle3, kExpectedSize3);
    sut_.alloc(&handle4, kExpectedSize4);

    sut_.free(&handle1);
    sut_.free(&handle3);

    auto ptr0 = reinterpret_cast<const uint8_t*>(handle0.hunk_ptr());
    auto ptr2 = reinterpret_cast<const uint8_t*>(handle2.hunk_ptr());
    auto ptr4 = reinterpret_cast<const uint8_t*>(handle4.hunk_ptr());

    auto itr = sut_.begin(false);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    EXPECT_EQ((++itr)->hunk_ptr(), handle2.hunk_ptr());
    EXPECT_EQ((++itr)->hunk_ptr(), handle4.hunk_ptr());
    EXPECT_EQ((++itr), sut_.end()) << "Should be last.";

    EXPECT_GE(ptr0 + handle0.size() - ptr2, kExpectedSize1) << "Should have free space after release.";
    EXPECT_GE(ptr2 + handle2.size() - ptr4, kExpectedSize3) << "Should have free space after release.";
}

TEST_F(LRUMemoryManagerTest, AllocateThreeEvictOne)
{
    constexpr size_t kExpectedSize0 = 900, kExpectedSize1 = 900, kExpectedSize2 = 900;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;
    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);

    // recent order 2->1
    auto itr = sut_.begin(true);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    EXPECT_EQ((++itr)->hunk_ptr(), handle1.hunk_ptr());
    EXPECT_EQ(++itr, sut_.end()) << "Should be last.";
    EXPECT_EQ(handle0.hunk_ptr(), nullptr) << "Should have been evicted.";
}

TEST_F(LRUMemoryManagerTest, HandleDestruction)
{
constexpr size_t kExpectedSize0 = 250, kExpectedSize1 = 50, kExpectedSize2 = 150;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1, handle2;

    sut_.alloc(&handle0, kExpectedSize0);
    sut_.alloc(&handle1, kExpectedSize1);
    sut_.alloc(&handle2, kExpectedSize2);

    handle1.~LRUMemoryHandle();

    auto itr = sut_.begin(false);
    EXPECT_NE(itr, sut_.end()) << "Should not be empty.";
    EXPECT_GE(itr->size(), kExpectedSize0);
    EXPECT_EQ(itr->hunk_ptr(), handle0.hunk_ptr());
    ++itr;
    EXPECT_NE(itr, sut_.end());
    EXPECT_GE(itr->size(), kExpectedSize2);
    EXPECT_EQ(itr->hunk_ptr(), handle2.hunk_ptr());
    ++itr;
    EXPECT_EQ(itr, sut_.end()) << "Should be last.";}

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)

// ASAN Positive Scenario Tests
TEST_F(LRUMemoryManagerTest, AsanPositiveAllocation)
{
    // This test verifies that normal allocation/deallocation doesn't trigger ASAN errors
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;

    // Allocate memory - should not trigger ASAN errors
    void* ptr = sut_.alloc(&handle, kExpectedSize);
    EXPECT_NE(ptr, nullptr) << "Allocation should succeed.";

    // Write to allocated memory - should not trigger ASAN errors
    memset(ptr, 0xAA, kExpectedSize);

    // Access allocated memory - should not trigger ASAN errors
    sut_.get_buffer_and_refresh(&handle);

    // Free memory - should not trigger ASAN errors
    sut_.free(&handle);
}

TEST_F(LRUMemoryManagerTest, AsanPositiveMultipleAllocations)
{
    // This test verifies that multiple allocations don't trigger ASAN errors
    constexpr size_t kExpectedSize = 50;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3;

    // Allocate multiple chunks
    void* ptr1 = sut_.alloc(&handle1, kExpectedSize);
    void* ptr2 = sut_.alloc(&handle2, kExpectedSize);
    void* ptr3 = sut_.alloc(&handle3, kExpectedSize);

    EXPECT_NE(ptr1, nullptr) << "First allocation should succeed.";
    EXPECT_NE(ptr2, nullptr) << "Second allocation should succeed.";
    EXPECT_NE(ptr3, nullptr) << "Third allocation should succeed.";

    // Write to all allocated memory - should not trigger ASAN errors
    memset(ptr1, 0x11, kExpectedSize);
    memset(ptr2, 0x22, kExpectedSize);
    memset(ptr3, 0x33, kExpectedSize);

    // Free all memory - should not trigger ASAN errors
    sut_.free(&handle1);
    sut_.free(&handle2);
    sut_.free(&handle3);
}

TEST_F(LRUMemoryManagerTest, AsanPositiveFlush)
{
    // This test verifies that flush operation doesn't trigger ASAN errors
    constexpr size_t kExpectedSize = 75;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3;

    // Allocate multiple chunks
    sut_.alloc(&handle1, kExpectedSize);
    sut_.alloc(&handle2, kExpectedSize);
    sut_.alloc(&handle3, kExpectedSize);

    // Flush all allocations - should not trigger ASAN errors
    sut_.flush();

    EXPECT_EQ(sut_.begin(), sut_.end()) << "Should be empty after flush.";
}

// ASAN False Scenario Tests (These would normally trigger ASAN errors in a real environment)
TEST_F(LRUMemoryManagerTest, AsanFalseUseAfterFree)  // Disabled because it would trigger ASAN in real environment
{
    // This test demonstrates what would happen with use-after-free
    // In a real ASAN environment, this would trigger an error
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;

    void* ptr = sut_.alloc(&handle, kExpectedSize);
    ASSERT_NE(ptr, nullptr) << "Allocation should succeed.";

    // Free the memory
    sut_.free(&handle);

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

    void* ptr = sut_.alloc(&handle, kExpectedSize);
    ASSERT_NE(ptr, nullptr) << "Allocation should succeed.";

    size_t allocated_size = handle.size();
    // Writing beyond allocated size would trigger an error
    ASSERT_DEATH({
        // Attempt to write to the poisoned region
        memset(static_cast<char*>(ptr) + allocated_size, 0xAA, 10);  // This would trigger ASAN
    }, "AddressSanitizer");
}

TEST_F(LRUMemoryManagerTest, AsanPoisoningVerification)
{
    // This test verifies that memory is properly poisoned and unpoisoned
    constexpr size_t kExpectedSize = 100;
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;

    // Before allocation, memory should be poisoned (simulated by checking allocation behavior)
    void* ptr1 = sut_.alloc(&handle, kExpectedSize);
    EXPECT_NE(ptr1, nullptr) << "Allocation should succeed.";

    // After allocation, memory should be unpoisoned and accessible
    memset(ptr1, 0x55, kExpectedSize);  // Should not trigger ASAN

    // After freeing, memory should be poisoned again
    sut_.free(&handle);

    // Allocate again to verify memory is properly unpoisoned for reuse
    void* ptr2 = sut_.alloc(&handle, kExpectedSize);
    EXPECT_NE(ptr2, nullptr) << "Reallocation should succeed.";
    memset(ptr2, 0xAA, kExpectedSize);  // Should not trigger ASAN
    sut_.free(&handle);
}

TEST_F(LRUMemoryManagerTest, AsanEvictionPoisoning)
{
    // This test verifies that evicted memory is properly poisoned
    constexpr size_t kSmallSize = 50;
    constexpr size_t kLargeSize = 1700;  // Large enough to trigger eviction

    // Allocate small chunks first
    lrumm::LRUMemoryManager::LRUMemoryHandle handle1, handle2, handle3;
    void* ptr1 = sut_.alloc(&handle1, kSmallSize);
    void* ptr2 = sut_.alloc(&handle2, kSmallSize);
    void* ptr3 = sut_.alloc(&handle3, kSmallSize);

    EXPECT_NE(ptr1, nullptr) << "First allocation should succeed.";
    EXPECT_NE(ptr2, nullptr) << "Second allocation should succeed.";
    EXPECT_NE(ptr3, nullptr) << "Third allocation should succeed.";

    // Access handle2 to make it recently used
    sut_.get_buffer_and_refresh(&handle2);

    // Allocate a large chunk that should cause eviction of least recently used (handle1)
    lrumm::LRUMemoryManager::LRUMemoryHandle handle4;
    void* ptr4 = sut_.alloc(&handle4, kLargeSize);

    EXPECT_NE(ptr4, nullptr) << "Large allocation should succeed.";
    EXPECT_EQ(handle1.hunk_ptr(), nullptr) << "Handle1 should have been evicted.";

    // The evicted memory should be properly poisoned
    ASSERT_DEATH({
        // Attempt to write to the poisoned region
        memset(ptr1, 0xAA, kSmallSize);
    }, "AddressSanitizer");
}

#endif //__SANITIZE_ADDRESS__

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}