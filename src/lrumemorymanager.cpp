#include <cstdlib>
#include <cstdint>
#include <new>
#include <cstring>

#include <sanitizer/asan_interface.h>

#include "lrumemorymanager.h"

namespace lrumm {

static constexpr size_t MEMORY_ALIGNMENT = 16;
static constexpr size_t ALIGNMENT_MASK = MEMORY_ALIGNMENT - 1;

struct LRUMemoryManager::LRUMemoryHunk {
    size_t size = 0;
    LRUMemoryHandle *handler_ptr = nullptr;
    LRUMemoryHunk *prev_ptr = nullptr, *next_ptr = nullptr;
    LRUMemoryHunk *least_recent_ptr = nullptr, *most_recent_ptr = nullptr;
    uint8_t data_ptr[];
};

LRUMemoryManager::LRUMemoryHandle*
LRUMemoryManager::LRUMemoryHandle::next() const
{
    Expects(hunk_ptr_ != nullptr);
    return hunk_ptr_->next_ptr->handler_ptr;
}

LRUMemoryManager::LRUMemoryHandle*
LRUMemoryManager::LRUMemoryHandle::most_recent() const
{
    Expects(hunk_ptr_ != nullptr);
    return hunk_ptr_->most_recent_ptr->handler_ptr;
}

size_t
LRUMemoryManager::LRUMemoryHandle::size() const
{
    Expects(hunk_ptr_ != nullptr);
    return hunk_ptr_->size - sizeof(LRUMemoryHunk);
}

LRUMemoryManager::LRUMemoryManager(size_t mem_pool_size)
    : mem_total_size_(mem_pool_size)
    , mem_allocated_size_(0)
    , mem_arena_ptr_(nullptr)
{
    Expects(mem_pool_size > 0);

    mem_arena_ptr_ = std::malloc(mem_total_size_);
    if (!mem_arena_ptr_) {
        LOG_ERROR("Failed to allocate memory pool of size %zu.\n", mem_pool_size);
        std::abort();
    }

    // Initialize the head hunk (sentinel)
    LRUMemoryHunk* head_hunk_ptr = new (mem_arena_ptr_) LRUMemoryHunk();
    head_hunk_ptr->next_ptr = head_hunk_ptr;
    head_hunk_ptr->prev_ptr = head_hunk_ptr;
    head_hunk_ptr->most_recent_ptr = head_hunk_ptr;
    head_hunk_ptr->least_recent_ptr = head_hunk_ptr;
    head_hunk_ptr->size = sizeof(LRUMemoryHunk);

    mem_allocated_size_ = sizeof(LRUMemoryHunk);

    // Initially, poison the entire buffer as it contains no valid data yet
    void* mem_free_ptr_ = static_cast<uint8_t*>(mem_arena_ptr_) + mem_allocated_size_;
    ASAN_POISON_MEMORY_REGION(mem_free_ptr_, mem_total_size_ - mem_allocated_size_);
}

LRUMemoryManager::~LRUMemoryManager() noexcept
{
    // Unpoison before deallocation to avoid false positives during potential internal checks
    ASAN_UNPOISON_MEMORY_REGION(mem_arena_ptr_, mem_total_size_);
    std::free(mem_arena_ptr_);
}

void
LRUMemoryManager::flush()
{
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();

    // Keep removing the first allocated hunk until only the head remains
    while(head_hunk_ptr->next_ptr != head_hunk_ptr) {
        real_free(head_hunk_ptr->next_ptr->handler_ptr);
    }
}

LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::try_alloc(size_t size)
{
    LRUMemoryHunk *next_hunk_ptr, *current_hunk_ptr;
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();

    // Search free space from the bottom
    current_hunk_ptr = head_hunk_ptr;
    next_hunk_ptr = head_hunk_ptr->next_ptr;

    do {
        // Calculate available space between current and next hunks
        uint8_t* current_end = reinterpret_cast<uint8_t*>(current_hunk_ptr) + current_hunk_ptr->size;
        uint8_t* next_start = reinterpret_cast<uint8_t*>(next_hunk_ptr);

        if (next_start >= current_end + size) {
            // Unpoison the space before allocate it
            ASAN_UNPOISON_MEMORY_REGION(current_end, size);

            // Free space found, allocate new hunk here
            LRUMemoryHunk* new_hunk_ptr = new (current_end) LRUMemoryHunk;
            new_hunk_ptr->size = size;

            // Insert into the allocation linked list
            new_hunk_ptr->next_ptr = next_hunk_ptr;
            new_hunk_ptr->prev_ptr = next_hunk_ptr->prev_ptr;
            next_hunk_ptr->prev_ptr->next_ptr = new_hunk_ptr;
            next_hunk_ptr->prev_ptr = new_hunk_ptr;

            // Add to LRU list
            link_lru(new_hunk_ptr);

            mem_allocated_size_ += size;
            return new_hunk_ptr;
        }

        // Continue looking
        current_hunk_ptr = next_hunk_ptr;
        next_hunk_ptr = next_hunk_ptr->next_ptr;
    } while (next_hunk_ptr != head_hunk_ptr);

    // Try to allocate at the end of the memory pool
    uint8_t* pool_end = static_cast<uint8_t*>(mem_arena_ptr_) + mem_total_size_;
    uint8_t* last_hunk_end = reinterpret_cast<uint8_t*>(head_hunk_ptr->prev_ptr) + head_hunk_ptr->prev_ptr->size;

    if (pool_end >= last_hunk_end + size) {
        // Unpoison the space before allocate it
        ASAN_UNPOISON_MEMORY_REGION(last_hunk_end, size);

        // Space available at the end
        LRUMemoryHunk* new_hunk_ptr = new (last_hunk_end) LRUMemoryHunk;
        new_hunk_ptr->size = size;

        // Insert into the allocation linked list
        new_hunk_ptr->next_ptr = head_hunk_ptr;
        new_hunk_ptr->prev_ptr = head_hunk_ptr->prev_ptr;
        head_hunk_ptr->prev_ptr->next_ptr = new_hunk_ptr;
        head_hunk_ptr->prev_ptr = new_hunk_ptr;

        // Add to LRU list
        link_lru(new_hunk_ptr);

        mem_allocated_size_ += size;
        return new_hunk_ptr;
    }

    return nullptr;  // Couldn't allocate
}

void*
LRUMemoryManager::real_get_buffer(LRUMemoryHandle *handle_ptr)
{
    if (handle_ptr->hunk_ptr_ == nullptr) {
        return nullptr;
    }

    LRUMemoryHunk *hunk_ptr = handle_ptr->hunk_ptr_;

    // Move to top of LRU linked list (most recently used)
    unlink_lru(hunk_ptr);
    link_lru(hunk_ptr);

    return hunk_ptr->data_ptr;
}

void*
LRUMemoryManager::real_alloc(LRUMemoryHandle *handle_ptr, size_t size)
{
    // Align size to MEMORY_ALIGNMENT boundary
    size_t aligned_size = (size + sizeof(LRUMemoryHunk) + ALIGNMENT_MASK) & ~ALIGNMENT_MASK;

    // Try to find and allocate
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();

    while (true) {
        LRUMemoryHunk* hunk_ptr = try_alloc(aligned_size);
        if (hunk_ptr) {
            hunk_ptr->handler_ptr = handle_ptr;
            handle_ptr->hunk_ptr_ = hunk_ptr;
            return hunk_ptr->data_ptr;
        }

        // If no free space found, try to free the least recently used hunk
        if (head_hunk_ptr != head_hunk_ptr->least_recent_ptr) {
            real_free(head_hunk_ptr->least_recent_ptr->handler_ptr);
        } else {
            // No more hunks to free, allocation failed
            return nullptr;
        }
    }

    Ensures(false); // unreachable
}

void
LRUMemoryManager::real_free(LRUMemoryHandle *handle_ptr)
{
    LRUMemoryHunk* hunk_ptr = handle_ptr->hunk_ptr_;

    size_t size = hunk_ptr->size;

    // Remove from allocation linked list
    hunk_ptr->prev_ptr->next_ptr = hunk_ptr->next_ptr;
    hunk_ptr->next_ptr->prev_ptr = hunk_ptr->prev_ptr;
    hunk_ptr->next_ptr = hunk_ptr->prev_ptr = nullptr;

    mem_allocated_size_ -= hunk_ptr->size;
    hunk_ptr->size = 0;

    // Remove from LRU list
    unlink_lru(hunk_ptr);

    // Mark the region as invalid/poisoned, after an element is "freed" in a pool
    ASAN_POISON_MEMORY_REGION(hunk_ptr, size);

    hunk_ptr->~LRUMemoryHunk();
    handle_ptr->hunk_ptr_ = nullptr;
}

void
LRUMemoryManager::unlink_lru(LRUMemoryHunk *hunk_ptr)
{
    Expects(hunk_ptr);
    Expects(hunk_ptr->most_recent_ptr && hunk_ptr->least_recent_ptr); // LRUMemoryManager::unlink_lru: not linked.

    hunk_ptr->most_recent_ptr->least_recent_ptr = hunk_ptr->least_recent_ptr;
    hunk_ptr->least_recent_ptr->most_recent_ptr = hunk_ptr->most_recent_ptr;
    hunk_ptr->least_recent_ptr = hunk_ptr->most_recent_ptr = nullptr;
}

void
LRUMemoryManager::link_lru(LRUMemoryHunk *hunk_ptr)
{
    Expects(hunk_ptr);
    Expects(!hunk_ptr->most_recent_ptr && !hunk_ptr->least_recent_ptr); // LRUMemoryManager::link_lru: already linked.

    // link to the top of the lru list
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();
    head_hunk_ptr->most_recent_ptr->least_recent_ptr = hunk_ptr;
    hunk_ptr->most_recent_ptr = head_hunk_ptr->most_recent_ptr;
    hunk_ptr->least_recent_ptr = head_hunk_ptr;
    head_hunk_ptr->most_recent_ptr = hunk_ptr;
}

void
LRUMemoryManager::report_state() const
{
    LOG_INFO("------------ LRU state ------------\n");

    size_t hunk_idx = 0;
    for (const auto& handler : *this) {
        LOG_INFO("%zu: %p (size: %zu)\n", hunk_idx, handler.hunk_ptr_, handler.hunk_ptr_->size);
        hunk_idx++;
    }

    LOG_INFO("%4.2f Mb left\n", static_cast<float>(mem_total_size_ - mem_allocated_size_) / 1024.0f*1024.0f);
    LOG_INFO("allocated: %zu, total pool size: %zu\n", mem_allocated_size_, mem_total_size_);
}

void
LRUMemoryManager::debug_dump() const
{
    LOG_INFO("------------ Pool dump -----------------\n");

    size_t hunk_idx = 0;
    for (auto itr = begin(false); itr != end(); ++itr) {
        const LRUMemoryHunk* current_hunk_ptr = itr->hunk_ptr_;
        const LRUMemoryHunk* prev_hunk_ptr = current_hunk_ptr->prev_ptr;
        const uint8_t* prev_hunk_raw_ptr = reinterpret_cast<const uint8_t*>(prev_hunk_ptr);

        if (prev_hunk_raw_ptr + prev_hunk_ptr->size < reinterpret_cast<const uint8_t*>(current_hunk_ptr)) {
            // Free space found
            std::ptrdiff_t hunk_diff = reinterpret_cast<const uint8_t*>(current_hunk_ptr) - prev_hunk_raw_ptr - prev_hunk_ptr->size;
            LOG_INFO("%zu: free space: %p (size: %zu)\n", hunk_idx, current_hunk_ptr, hunk_diff);
            hunk_idx++;
        }

        LOG_INFO("%zu: allocated space: %p (size: %zu)\n", hunk_idx, current_hunk_ptr, current_hunk_ptr->size);
        hunk_idx++;
    }

    const LRUMemoryHunk* head_hunk_ptr = get_head_hunk();
    const uint8_t* last_free_ptr = reinterpret_cast<const uint8_t*>(head_hunk_ptr->prev_ptr) + head_hunk_ptr->prev_ptr->size;
    const uint8_t* last_pool_ptr = static_cast<const uint8_t*>(mem_arena_ptr_) + mem_total_size_;

    if (last_pool_ptr > last_free_ptr) {
        std::ptrdiff_t last_diff = last_pool_ptr - last_free_ptr;
        LOG_INFO("leading free space: %p (size: %zu)\n", last_free_ptr, last_diff);
    }

    LOG_INFO("used memory: %zu, total pool size %zu\n", mem_allocated_size_, mem_total_size_);
}

LRUMemoryManager::iterator
LRUMemoryManager::begin(bool is_lru_order)
{
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();
    return LRUMemoryManager::iterator(
        is_lru_order ? head_hunk_ptr->most_recent_ptr->handler_ptr : head_hunk_ptr->next_ptr->handler_ptr, is_lru_order);
}

LRUMemoryManager::iterator
LRUMemoryManager::end()
{
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();
    return LRUMemoryManager::iterator(head_hunk_ptr->handler_ptr);
}

LRUMemoryManager::const_iterator
LRUMemoryManager::begin(bool is_lru_order) const
{
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();
    return LRUMemoryManager::const_iterator(
        is_lru_order ? head_hunk_ptr->most_recent_ptr->handler_ptr : head_hunk_ptr->next_ptr->handler_ptr, is_lru_order);
}

LRUMemoryManager::const_iterator
LRUMemoryManager::end() const
{
    LRUMemoryHunk* head_hunk_ptr = get_head_hunk();
    return LRUMemoryManager::const_iterator(head_hunk_ptr->handler_ptr);
}

LRUMemoryManager&
LRUMemoryManager::get_instance() {
    static LRUMemoryManager lru_memory_cache_;
    return lru_memory_cache_;
}

}