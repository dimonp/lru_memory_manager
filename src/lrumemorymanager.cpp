#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>

#include <sanitizer/asan_interface.h>

#include "lru_memory_manager/lrumemorymanager.h"

namespace lrumm {

inline
size_t
align_up(size_t size) {
    // Align size to ALLOCATED_BLOCK_ALIGNMENT boundary
    return (size + (LRUMemoryManager::BLOCK_ALIGNMENT - 1)) & ~(LRUMemoryManager::BLOCK_ALIGNMENT - 1);
}

inline int portable_clz(uint32_t x) {
    if (x == 0) { return 32; }
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(x);
#elif defined(_MSC_VER)
    unsigned long leading_zero = 0;
    if (_BitScanReverse(&leading_zero, x)) {
        return 31 - leading_zero;
    }
    return 32;
#else
    int n = 0;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8;  x <<= 8;  }
    if (x <= 0x0FFFFFFF) { n += 4;  x <<= 4;  }
    if (x <= 0x3FFFFFFF) { n += 2;  x <<= 2;  }
    if (x <= 0x7FFFFFFF) { n += 1; }
    return n;
#endif
}

inline
size_t
get_bin_index(size_t size)
{
    if (size <= 64) { return 0; }
    return 31 - portable_clz(static_cast<uint32_t>(size));
}

struct LRUMemoryManager::LRUMemoryHunk {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    LRUMemoryHunk() noexcept
    { 
        free_next = nullptr;
        free_prev = nullptr;
    }

    ptrdiff_t size = 0; // Positive = Free, Negative = Allocated
    LRUMemoryHandle *handle = nullptr;
    LRUMemoryHunk *phys_prev = nullptr, *phys_next = nullptr;

    union {
        struct { LRUMemoryHunk *free_next, *free_prev; };
        struct { LRUMemoryHunk *most_recent, *least_recent; };
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
    assert(hunk_ != nullptr);
    return std::abs(hunk_->size) - sizeof(LRUMemoryHunk);
}

LRUMemoryManager::LRUMemoryManager(size_t mem_pool_size)
    : mem_total_size_(mem_pool_size)
    , mem_allocated_size_(0)
    , mem_pool_(nullptr)
    , free_anchor_(nullptr)
    , lru_anchor_(nullptr)
{
    assert(mem_pool_size > 0);

    mem_pool_ = new char[mem_total_size_];
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
    delete [] mem_pool_;
}

void LRUMemoryManager::init_pool() {
    char* ptr = mem_pool_;

    // Place sentinels at the start of the arena
    free_anchor_ = new (ptr) LRUMemoryHunk {};
    ptr += sizeof(LRUMemoryHunk);
    lru_anchor_  = new (ptr) LRUMemoryHunk {};
    ptr += sizeof(LRUMemoryHunk);

    // Init circular sentinels
    free_anchor_->free_next = free_anchor_;
    free_anchor_->free_prev = free_anchor_;
    free_anchor_->size = 0;
    free_anchor_->phys_next = nullptr;
    free_anchor_->phys_prev = nullptr;

    lru_anchor_->most_recent = lru_anchor_;
    lru_anchor_->least_recent = lru_anchor_;
    lru_anchor_->size = 0;
    lru_anchor_->phys_next = nullptr;
    lru_anchor_->phys_prev = nullptr;

    // Initial big free block
    size_t header_offset = ptr - mem_pool_;
    LRUMemoryHunk* first_hunk = new (ptr) LRUMemoryHunk {};
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
    uint8_t* head_hunk_ptr = reinterpret_cast<uint8_t*>(lru_anchor_) + sizeof(LRUMemoryHunk);
    return reinterpret_cast<LRUMemoryHunk*>(head_hunk_ptr);
}

LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::try_alloc(size_t size) noexcept
{
    for (LRUMemoryHunk* current = free_anchor_->free_next; 
        current != free_anchor_; 
        current = current->free_next) {
            
        if (current->size >= (ptrdiff_t)size) {

            ASAN_UNPOISON_MEMORY_REGION(
                reinterpret_cast<uint8_t*>(current) + sizeof(LRUMemoryHunk),
                std::abs(current->size) - sizeof(LRUMemoryHunk)
            );

            // Splitting
            if (current->size >= (ptrdiff_t)(size + MINIMUM_ALLOCATE_BLOCK)) {
                LRUMemoryHunk* remain = new (reinterpret_cast<uint8_t*>(current) + size) LRUMemoryHunk {};

                remain->size = current->size - size;
                current->size = (ptrdiff_t)size;

                remain->phys_next = current->phys_next;
                remain->phys_prev = current;
                if (current->phys_next) {
                    current->phys_next->phys_prev = remain;
                }
                current->phys_next = remain;

                remain->free_next = current->free_next;
                remain->free_prev = current->free_prev;
                remain->free_next->free_prev = remain;
                remain->free_prev->free_next = remain;

                // poison remain free
                ASAN_POISON_MEMORY_REGION(
                    reinterpret_cast<uint8_t*>(remain) + sizeof(LRUMemoryHunk),
                    static_cast<size_t>(remain->size) - sizeof(LRUMemoryHunk)
                );
            } else {
                current->free_prev->free_next = current->free_next;
                current->free_next->free_prev = current->free_prev;
            }

            // Insert into LRU ring (Most Recent position)
            current->least_recent = lru_anchor_->least_recent;
            current->most_recent = lru_anchor_;
            lru_anchor_->least_recent->most_recent = current;
            lru_anchor_->least_recent = current;

            current->size = -std::abs(current->size);
            mem_allocated_size_ += std::abs(current->size);

            return current;
        }
    }
    return nullptr;
}

void*
LRUMemoryManager::real_get_buffer(LRUMemoryHandle *handle_ptr) noexcept
{
    if (handle_ptr->hunk_ == nullptr) {
        return nullptr;
    }

    LRUMemoryHunk *hunk_ptr = handle_ptr->hunk_;

    // most recent already ?
    if (lru_anchor_->least_recent == hunk_ptr) {
        return hunk_ptr->data_ptr;
    }

    // remove from current LRU position
    hunk_ptr->least_recent->most_recent = hunk_ptr->most_recent;
    hunk_ptr->most_recent->least_recent = hunk_ptr->least_recent;

    // Move to top LRU position
    hunk_ptr->least_recent = lru_anchor_->least_recent;
    hunk_ptr->most_recent = lru_anchor_;

    // update neighbors
    lru_anchor_->least_recent->most_recent = hunk_ptr;
    lru_anchor_->least_recent = hunk_ptr;

    return hunk_ptr->data_ptr;
}

void*
LRUMemoryManager::real_alloc(LRUMemoryHandle *handle_ptr, size_t size) noexcept
{
    size_t aligned_size = align_up(size + sizeof(LRUMemoryHunk));

    // Try to find and allocate
    while (true) {
        LRUMemoryHunk* hunk_ptr = try_alloc(aligned_size);
        if (hunk_ptr) {
            hunk_ptr->handle = handle_ptr;
            handle_ptr->hunk_ = hunk_ptr;
            return hunk_ptr->data_ptr;
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
LRUMemoryManager::real_free(LRUMemoryHandle *handle_ptr) noexcept
{
    LRUMemoryHunk* hunk_ptr = handle_ptr->hunk_;

    // Remove from LRU
    hunk_ptr->least_recent->most_recent = hunk_ptr->most_recent;
    hunk_ptr->most_recent->least_recent = hunk_ptr->least_recent;

    hunk_ptr->size = std::abs(hunk_ptr->size);
    mem_allocated_size_ -= hunk_ptr->size;

    // Coalesce Right
    LRUMemoryHunk* r_neighbor = hunk_ptr->phys_next;
    if (r_neighbor && r_neighbor->size > 0) {
        // remove from free ring
        r_neighbor->free_prev->free_next = r_neighbor->free_next;
        r_neighbor->free_next->free_prev = r_neighbor->free_prev;
        hunk_ptr->size += r_neighbor->size;
        hunk_ptr->phys_next = r_neighbor->phys_next;
        if (r_neighbor->phys_next) {
            r_neighbor->phys_next->phys_prev = hunk_ptr;
        }
    }

    // Coalesce Left
    LRUMemoryHunk* l_neighbor = hunk_ptr->phys_prev;
    if (l_neighbor && l_neighbor->size > 0) {
        // remove from free ring
        l_neighbor->free_prev->free_next = l_neighbor->free_next;
        l_neighbor->free_next->free_prev = l_neighbor->free_prev;
        l_neighbor->size += hunk_ptr->size;
        l_neighbor->phys_next = hunk_ptr->phys_next;
        if (hunk_ptr->phys_next) {
            hunk_ptr->phys_next->phys_prev = l_neighbor;
        }
        hunk_ptr = l_neighbor;
    }

    // Return to free ring
    hunk_ptr->free_next = free_anchor_->free_next;
    hunk_ptr->free_prev = free_anchor_;
    free_anchor_->free_next->free_prev = hunk_ptr;
    free_anchor_->free_next = hunk_ptr;
    hunk_ptr->handle = nullptr;

    ASAN_POISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(hunk_ptr) + sizeof(LRUMemoryHunk),
        hunk_ptr->size - sizeof(LRUMemoryHunk)
    );

    // invalidate handle
    handle_ptr->hunk_ = nullptr;
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