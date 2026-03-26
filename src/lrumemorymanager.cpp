#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <sanitizer/asan_interface.h>

#include "lru_memory_manager/lrumemorymanager.h"

namespace lrumm {

inline
size_t
align_up(size_t size) {
    // Align size to ALLOCATED_BLOCK_ALIGNMENT boundary
    return (size + (LRUMemoryManager::BLOCK_ALIGNMENT - 1)) & ~(LRUMemoryManager::BLOCK_ALIGNMENT - 1);
}

struct LRUMemoryManager::LRUMemoryHunk {
    struct alignas(LRUMemoryManager::BLOCK_ALIGNMENT) {
        ptrdiff_t size; // Positive = Free, Negative = Allocated
        LRUMemoryHandle *handle;
        LRUMemoryHunk *phys_prev, *phys_next;

        union {
            struct { LRUMemoryHunk *free_next, *free_prev; };
            struct { LRUMemoryHunk *most_recent, *least_recent; };
        };
    };
    uint8_t data_ptr[];
};

LRUMemoryManager::LRUMemoryHandle*
LRUMemoryManager::LRUMemoryHandle::least_recent() const
{
    assert(hunk_ != nullptr);
    return hunk_->least_recent->handle;
}

size_t
LRUMemoryManager::LRUMemoryHandle::size() const
{
    return hunk_ != nullptr ? std::abs(hunk_->size) - sizeof(LRUMemoryHunk) : 0;
}

LRUMemoryManager::LRUMemoryManager(size_t mem_pool_size)
    : mem_total_size_(mem_pool_size)
    , mem_allocated_size_(0)
    , mem_pool_(nullptr)
    , free_anchor_(nullptr)
    , lru_anchor_(nullptr)
{
    Expects(mem_pool_size > 0);

    void* raw = ::operator new(mem_pool_size, std::align_val_t{BLOCK_ALIGNMENT});
    mem_pool_ = static_cast<char*>(raw);
    if (!mem_pool_) {
        LOG_ERROR("Failed to allocate memory pool of size %zu.\n", mem_pool_size);
        std::abort();
    }
    init_pool();
}

LRUMemoryManager::~LRUMemoryManager() noexcept
{
    flush();

    // Unpoison before deallocation to avoid false positives during potential internal checks
    char* ptr = mem_pool_ + sizeof(LRUMemoryHunk) * 2;
    LRUMemoryHunk* first = reinterpret_cast<LRUMemoryHunk*>(ptr);

    ASAN_UNPOISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(first) + sizeof(LRUMemoryHunk),
        first->size - sizeof(LRUMemoryHunk)
    );
    ::operator delete(mem_pool_, std::align_val_t{BLOCK_ALIGNMENT});
}

void LRUMemoryManager::init_pool() {
    char* ptr = mem_pool_;

    // Place sentinels at the start of the arena
    free_anchor_ = reinterpret_cast<LRUMemoryHunk*>(ptr);
    ptr += sizeof(LRUMemoryHunk);
    lru_anchor_  = reinterpret_cast<LRUMemoryHunk*>(ptr);
    ptr += sizeof(LRUMemoryHunk);

    // Init circular sentinels
    free_anchor_->free_next = free_anchor_;
    free_anchor_->free_prev = free_anchor_;
    free_anchor_->size = 0;
    free_anchor_->phys_next = nullptr;
    free_anchor_->phys_prev = nullptr;
    lru_anchor_->handle = nullptr;

    lru_anchor_->most_recent = lru_anchor_;
    lru_anchor_->least_recent = lru_anchor_;
    lru_anchor_->size = 0;
    lru_anchor_->phys_next = nullptr;
    lru_anchor_->phys_prev = nullptr;
    lru_anchor_->handle = nullptr;

    // Initial big free block
    size_t header_offset = ptr - mem_pool_;
    LRUMemoryHunk* first_hunk = reinterpret_cast<LRUMemoryHunk*>(ptr);
    first_hunk->size = static_cast<ptrdiff_t>(mem_total_size_ - header_offset);
    first_hunk->phys_next = nullptr;
    first_hunk->phys_prev = nullptr;


    // Add to free ring
    first_hunk->free_next = free_anchor_->free_next;
    first_hunk->free_prev = free_anchor_;
    free_anchor_->free_next->free_prev = first_hunk;
    free_anchor_->free_next = first_hunk;

    mem_allocated_size_ = sizeof(LRUMemoryHunk) * 2;

    ASAN_POISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(first_hunk) + sizeof(LRUMemoryHunk),
        static_cast<size_t>(first_hunk->size) - sizeof(LRUMemoryHunk)
    );
}

void
LRUMemoryManager::flush()
{
    // Keep removing the first allocated hunk until only the head remains
    while(lru_anchor_->least_recent != lru_anchor_) {
        real_free(lru_anchor_->least_recent->handle);
    }
}

void
LRUMemoryManager::arena_clean()
{
    init_pool();
}

LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::get_head_hunk() const noexcept
{
    uint8_t* head_hunk = reinterpret_cast<uint8_t*>(lru_anchor_) + sizeof(LRUMemoryHunk);
    return reinterpret_cast<LRUMemoryHunk*>(head_hunk);
}

LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::try_alloc(size_t size) noexcept
{
    LRUMemoryHunk* found = nullptr;
    const ptrdiff_t target_size = (ptrdiff_t)size;

    for (LRUMemoryHunk* current = free_anchor_->free_next;
         current != free_anchor_;
         current = current->free_next) {

        if (current->size >= target_size) {
            found = current;
            break; // Free space found
        }
    }

    if (!found) { return nullptr; }

    ASAN_UNPOISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(found) + sizeof(LRUMemoryHunk),
        std::abs(found->size) - sizeof(LRUMemoryHunk)
    );

    // Splitting
    if (found->size >= (ptrdiff_t)(size + MINIMUM_ALLOCATE_BLOCK)) {
        LRUMemoryHunk* remain = reinterpret_cast<LRUMemoryHunk*>(reinterpret_cast<uint8_t*>(found) + size);

        remain->size = found->size - size;
        found->size = (ptrdiff_t)size;

        remain->phys_next = found->phys_next;
        remain->phys_prev = found;
        if (found->phys_next) {
            found->phys_next->phys_prev = remain;
        }
        found->phys_next = remain;

        remain->free_next = found->free_next;
        remain->free_prev = found->free_prev;
        remain->free_next->free_prev = remain;
        remain->free_prev->free_next = remain;

        // Remove found from free list
        found->free_next = nullptr;
        found->free_prev = nullptr;

        // poison remain free
        ASAN_POISON_MEMORY_REGION(
            reinterpret_cast<uint8_t*>(remain) + sizeof(LRUMemoryHunk),
            static_cast<size_t>(remain->size) - sizeof(LRUMemoryHunk)
        );
    } else {
        found->free_prev->free_next = found->free_next;
        found->free_next->free_prev = found->free_prev;
        found->free_next = nullptr;
        found->free_prev = nullptr;
    }

    // Insert into LRU ring (Most Recent position)
    found->least_recent = lru_anchor_->least_recent;
    found->most_recent = lru_anchor_;
    lru_anchor_->least_recent->most_recent = found;
    lru_anchor_->least_recent = found;

    found->size = -std::abs(found->size);
    mem_allocated_size_ += std::abs(found->size);

    return found;
}

void*
LRUMemoryManager::real_get_buffer(LRUMemoryHandle *handle) noexcept
{
    if (handle->hunk_ == nullptr) {
        return nullptr;
    }

    LRUMemoryHunk *hunk = handle->hunk_;

    // most recent already ?
    if (lru_anchor_->least_recent == hunk) {
        return hunk->data_ptr;
    }

    // remove from current LRU position
    hunk->least_recent->most_recent = hunk->most_recent;
    hunk->most_recent->least_recent = hunk->least_recent;

    // Move to top LRU position
    hunk->least_recent = lru_anchor_->least_recent;
    hunk->most_recent = lru_anchor_;

    // update neighbors
    lru_anchor_->least_recent->most_recent = hunk;
    lru_anchor_->least_recent = hunk;

    return hunk->data_ptr;
}

void*
LRUMemoryManager::real_alloc(LRUMemoryHandle *handle, size_t size) noexcept
{
    size_t aligned_size = align_up(size + sizeof(LRUMemoryHunk));

    // Try to find and allocate
    while (true) {
        LRUMemoryHunk* hunk = try_alloc(aligned_size);
        size_t vv = sizeof(LRUMemoryHunk);
        ptrdiff_t pp = reinterpret_cast<char*>(hunk->data_ptr) - reinterpret_cast<char*>(hunk);

        if (hunk) {
            hunk->handle = handle;
            handle->hunk_ = hunk;
            return hunk->data_ptr;
        }

        // If no free space found, try to free the least recently used hunk
        if (lru_anchor_ != lru_anchor_->most_recent) {
            real_free(lru_anchor_->most_recent->handle);
        } else {
            // No more hunks to free, allocation failed
            break;
        }
    }
    return nullptr;
}

void
LRUMemoryManager::real_free(LRUMemoryHandle *handle) noexcept
{
    LRUMemoryHunk* hunk = handle->hunk_;

    // Remove from LRU
    hunk->least_recent->most_recent = hunk->most_recent;
    hunk->most_recent->least_recent = hunk->least_recent;

    hunk->size = std::abs(hunk->size);
    mem_allocated_size_ -= hunk->size;
    handle->hunk_ = nullptr;

    // Coalesce Right
    LRUMemoryHunk* r_neighbor = hunk->phys_next;
    if (r_neighbor && r_neighbor->size > 0) {
        // remove from free ring
        r_neighbor->free_prev->free_next = r_neighbor->free_next;
        r_neighbor->free_next->free_prev = r_neighbor->free_prev;
        hunk->size += r_neighbor->size;
        hunk->phys_next = r_neighbor->phys_next;
        if (r_neighbor->phys_next) {
            r_neighbor->phys_next->phys_prev = hunk;
        }
    }

    // Coalesce Left
    LRUMemoryHunk* l_neighbor = hunk->phys_prev;
    if (l_neighbor && l_neighbor->size > 0) {
        // remove from free ring
        l_neighbor->free_prev->free_next = l_neighbor->free_next;
        l_neighbor->free_next->free_prev = l_neighbor->free_prev;
        l_neighbor->size += hunk->size;
        l_neighbor->phys_next = hunk->phys_next;
        if (hunk->phys_next) {
            hunk->phys_next->phys_prev = l_neighbor;
        }
        hunk = l_neighbor;
    }

    // Return to free ring
    hunk->free_next = free_anchor_->free_next;
    hunk->free_prev = free_anchor_;
    free_anchor_->free_next->free_prev = hunk;
    free_anchor_->free_next = hunk;
    hunk->handle = nullptr;

    ASAN_POISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(hunk) + sizeof(LRUMemoryHunk),
        hunk->size - sizeof(LRUMemoryHunk)
    );
}

void
LRUMemoryManager::lru_state() const
{
    LOG_INFO("------------ LRU state ------------\n");
    size_t hunk_idx = 0;
    for (const auto& handler : *this) {
        LOG_INFO("%zu: %p (size: %zu)\n", hunk_idx, handler.hunk_, handler.size());
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
    for (auto itr = get_head_hunk(); itr != nullptr; itr = itr->phys_next) {
        if (itr->size > 0) {
            // Free space found
            LOG_INFO("%zu: free space: %p (size: %zu)\n", hunk_idx, itr, itr->size);
        } else {
            LOG_INFO("%zu: allocated space: %p (size: %zu)\n", hunk_idx, itr, -itr->size);
        }
        hunk_idx++;
    }
    LOG_INFO("used memory: %zu, total pool size %zu\n", mem_allocated_size_, mem_total_size_);
}

LRUMemoryManager::LruIterator<false>
LRUMemoryManager::begin()
{
    LRUMemoryHunk* head = lru_anchor_->least_recent;
    return LRUMemoryManager::LruIterator<false>(head ? head->handle : nullptr);
}

LRUMemoryManager::LruIterator<false>
LRUMemoryManager::end()
{
    return LRUMemoryManager::LruIterator<false>(lru_anchor_->handle);
}

const LRUMemoryManager::LruIterator<true>
LRUMemoryManager::begin() const
{
    LRUMemoryHunk* head = lru_anchor_->least_recent;
    return LRUMemoryManager::LruIterator<true>(head ? head->handle : nullptr);
}

const LRUMemoryManager::LruIterator<true>
LRUMemoryManager::end() const
{
    return LRUMemoryManager::LruIterator<true>(lru_anchor_->handle);
}

} // namespace lrumm
