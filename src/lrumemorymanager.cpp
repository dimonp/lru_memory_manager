#include <cstdlib>
#include <cstring>
#include <climits>
#include <new>

#include <sanitizer/asan_interface.h>

#include "lru_memory_manager/lrumemorymanager.h"

namespace lrumm {

static constexpr size_t MINIMUM_ALLOCATE_BLOCK = 64;
static constexpr size_t ALLOCATED_BLOCK_ALIGNMENT = 64;
static constexpr size_t NUMBER_OF_BINS = 32;

inline
size_t
align_up(size_t size)
{
    // Align size to ALLOCATED_BLOCK_ALIGNMENT boundary
    return (size + (ALLOCATED_BLOCK_ALIGNMENT - 1)) & ~(ALLOCATED_BLOCK_ALIGNMENT - 1);
}

inline
int
portable_ctz(unsigned int x) {
    if (x == 0) {
        return sizeof(unsigned int) * CHAR_BIT;
    }

#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, x);
    return static_cast<int>(index);
#endif
}

inline
size_t
get_bin_index(size_t size) {
    // 64 -> 0, 128 -> 1, 512 -> 4, 1024 -> 8, 2048б -> 16
    uint32_t idx = static_cast<uint32_t>(size >> 7);
    return (idx > 31) ? 31 : static_cast<int>(idx);}

struct LRUMemoryManager::LRUMemoryHunk
{
    ptrdiff_t size; // Positive = Free, Negative = Allocated
    LRUMemoryHandle *handle;
    LRUMemoryHunk *phys_prev, *phys_next;

    union {
        struct { ListLinks free_links; };
        struct { ListLinks lru_links; };
    };

    uint8_t data_ptr[];
};

inline
LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::to_hunk_from_free(LRUMemoryManager::ListLinks* l) {
    return reinterpret_cast<LRUMemoryManager::LRUMemoryHunk*>(
        reinterpret_cast<uint8_t*>(l) - offsetof(LRUMemoryManager::LRUMemoryHunk, free_links));
}

inline
LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::to_hunk_from_lru(LRUMemoryManager::ListLinks* l) {
    return reinterpret_cast<LRUMemoryManager::LRUMemoryHunk*>(
        reinterpret_cast<uint8_t*>(l) - offsetof(LRUMemoryManager::LRUMemoryHunk, lru_links));
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
    , free_bin_mask_(0)
    , mem_arena_(nullptr)
    , free_bins_(nullptr)
    , lru_anchor_(nullptr)
{
    Ensures(mem_pool_size > MINIMUM_ALLOCATE_BLOCK);

    free_bins_ = new ListLinks[NUMBER_OF_BINS];
    lru_anchor_ = new ListLinks();
    mem_arena_ = std::malloc(mem_total_size_);
    if (!mem_arena_) {
        LOG_ERROR("Failed to allocate memory pool of size %zu.\n", mem_pool_size);
        std::terminate();
    }
    init_pool();

    LRUMemoryHunk* head_hunk = get_head_hunk();
    ASAN_POISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(head_hunk) + sizeof(LRUMemoryHunk),
        static_cast<size_t>(head_hunk->size) - sizeof(LRUMemoryHunk)
    );
}

LRUMemoryManager::~LRUMemoryManager() noexcept
{
    flush();

    // Unpoison before deallocation to avoid false positives during potential internal checks
    LRUMemoryHunk* head_hunk = get_head_hunk();
    ASAN_UNPOISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(head_hunk) + sizeof(LRUMemoryHunk),
        static_cast<size_t>(head_hunk->size) - sizeof(LRUMemoryHunk)
    );

    std::free(mem_arena_);
    delete[] free_bins_;
    delete lru_anchor_;
}

void LRUMemoryManager::init_pool()
{
    uint8_t* ptr = static_cast<uint8_t*>(mem_arena_);

    // Initialize free bins as circular rings
    for (size_t i = 0; i < NUMBER_OF_BINS; ++i) {
        free_bins_[i].next = &free_bins_[i];
        free_bins_[i].prev = &free_bins_[i];
    }

    // Init LRU anchor
    lru_anchor_->next = lru_anchor_;
    lru_anchor_->prev = lru_anchor_;

    // Create the initial large free block
    LRUMemoryHunk* first_hunk = new (mem_arena_) LRUMemoryHunk {};
    first_hunk->size = static_cast<ptrdiff_t>(mem_total_size_);
    // Physical boundaries
    first_hunk->phys_next = nullptr;
    first_hunk->phys_prev = nullptr;

    // Add to the appropriate bin
    int bin_idx = get_bin_index(static_cast<size_t>(first_hunk->size));
    ListLinks* bin_anchor = &free_bins_[bin_idx];
    ListLinks* first_links = &first_hunk->free_links;

    // Standard circular link insertion (Links pointing to Links)
    first_links->next = bin_anchor;
    first_links->prev = bin_anchor->prev;
    bin_anchor->prev->next = first_links;
    bin_anchor->prev = first_links;

    free_bin_mask_ = 0;
    free_bin_mask_ |= (1U << bin_idx);

    mem_allocated_size_ = sizeof(LRUMemoryHunk) * (NUMBER_OF_BINS + 1);
}

void
LRUMemoryManager::flush()
{
    // Keep removing the first allocated hunk until only the head remains
    while(lru_anchor_->prev != lru_anchor_) {
        auto* hunk = to_hunk_from_lru(lru_anchor_->prev);
        real_free(hunk->handle);
    }
}

void
LRUMemoryManager::arena_clean()
{
    init_pool();
}

inline
LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::get_head_hunk() const
{
    return reinterpret_cast<LRUMemoryHunk*>(mem_arena_);
}

/**
 * Searches for a block using bitwise operations on the bin mask.
 * This is the "Hot Path" of the allocator.
 */
inline
LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::find_free_block(size_t size)
{
    // Mask out all bins smaller than requested
    uint32_t mask = free_bin_mask_ & (~0U << get_bin_index(size));
    if (!mask) { return nullptr; }

    // Jump directly to the first non-empty bin index
    int bin_idx = portable_ctz(mask);
    ListLinks* anchor = &free_bins_[bin_idx];

    // In 99% of cases we take the FIRST block from the first bin we find
    LRUMemoryHunk* hunk = to_hunk_from_free(anchor->next);
    if (hunk->size >= (ptrdiff_t)size) { return hunk; }

    // Linear scan inside the specific bin (usually very few items)
    for (ListLinks* current = hunk->free_links.next; current != anchor; current = current->next) {
        LRUMemoryHunk* hunk = to_hunk_from_free(current);
        if (hunk->size >= (ptrdiff_t)size) { return hunk; }
    }
    return nullptr;
}

/**
 * Splits a free block into two if the remainder is at least 128 bytes.
 */
inline
void
LRUMemoryManager::split_block(LRUMemoryHunk* hunk, size_t size)
{
    ASAN_UNPOISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(hunk) + sizeof(LRUMemoryHunk),
        std::abs(hunk->size) - sizeof(LRUMemoryHunk)
    );

    if (hunk->size >= (ptrdiff_t)(size + 128)) {
        LRUMemoryHunk* remain = reinterpret_cast<LRUMemoryHunk*>(
            reinterpret_cast<uint8_t*>(hunk) + size);

        remain->size = hunk->size - size;
        hunk->size = (ptrdiff_t)size;

        // Maintain physical memory order for future coalescing
        remain->phys_next = hunk->phys_next;
        remain->phys_prev = hunk;
        if (hunk->phys_next) { hunk->phys_next->phys_prev = remain; }
        hunk->phys_next = remain;

        // Re-insert the leftover part into the free list/bins
        add_to_free_list(remain);

        ASAN_POISON_MEMORY_REGION(
            reinterpret_cast<uint8_t*>(remain) + sizeof(LRUMemoryHunk),
            static_cast<size_t>(remain->size) - sizeof(LRUMemoryHunk)
        );
    }
}

/**
 * Moves a block to the "Most Recent" position in the LRU ring.
 */
inline
void
LRUMemoryManager::activate_lru_hunk(LRUMemoryHunk* hunk)
{
    hunk->size = -std::abs(hunk->size); // Set size to negative (allocated marker)

    // Prepare the links
    ListLinks* target = &hunk->lru_links;
    ListLinks* anchor = lru_anchor_;

    // Insert after lru_anchor_ (Most Recent position)
    target->next = anchor->next;
    target->prev = anchor;
    anchor->next->prev = target;
    anchor->next = target;
}

LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::try_alloc(size_t size)
{
    LRUMemoryHunk* hunk = find_free_block(size);
    if (!hunk) {
        return nullptr;
    }

    remove_from_free_list(hunk);
    split_block(hunk, size);
    activate_lru_hunk(hunk);
    mem_allocated_size_ += std::abs(hunk->size);

    return hunk;
}

void*
LRUMemoryManager::real_get_buffer(LRUMemoryHandle *handle)
{
    if (handle->hunk_ == nullptr) {
        return nullptr;
    }

    LRUMemoryHunk *hunk = handle->hunk_;

    // most recent already ?
    if (lru_anchor_->next == &hunk->lru_links) {
        return hunk->data_ptr;
    }

    // remove from current LRU position
    hunk->lru_links.prev->next = hunk->lru_links.next;
    hunk->lru_links.next->prev = hunk->lru_links.prev;

    // Move to top LRU position
    hunk->lru_links.next = lru_anchor_->next;
    hunk->lru_links.prev = lru_anchor_;

    // update neighbors
    lru_anchor_->next->prev = &hunk->lru_links;
    lru_anchor_->next = &hunk->lru_links;

    return hunk->data_ptr;
}

void*
LRUMemoryManager::real_alloc(LRUMemoryHandle *handle, size_t size)
{
    size_t aligned_size = align_up(size + sizeof(LRUMemoryHunk));

    // Try to find and allocate
    while (true) {
        LRUMemoryHunk* hunk = try_alloc(aligned_size);
        if (hunk) {
            hunk->handle = handle;
            handle->hunk_ = hunk;
            return hunk->data_ptr;
        }

        // If no free space found, try to free the least recently used hunk
        if (lru_anchor_->prev == lru_anchor_) {
            // No more hunks to free, allocation failed
            break;
        }

        hunk = to_hunk_from_lru(lru_anchor_->prev);
        real_free(hunk->handle);
    }
    return nullptr;
}

inline
void
LRUMemoryManager::add_to_free_list(LRUMemoryHunk* hunk)
{
    // Put back to the appropriate free bin
    int bin = get_bin_index(static_cast<size_t>(hunk->size));
    ListLinks* anchor = &free_bins_[bin];
    ListLinks* target = &hunk->free_links;

    target->next = anchor->next;
    target->prev = anchor;
    anchor->next->prev = target;
    anchor->next = target;

    free_bin_mask_ |= (1U << bin);
}

inline
void
LRUMemoryManager::remove_from_free_list(LRUMemoryHunk* hunk) {
    size_t bin_idx = get_bin_index(std::abs(hunk->size));
    ListLinks* target = &hunk->free_links;

    // remove from free ring
    target->prev->next = target->next;
    target->next->prev = target->prev;

    // Если после удаления корзина стала пустой (кольцо замкнулось на якоре)
    if (free_bins_[bin_idx].next == &free_bins_[bin_idx]) {
        free_bin_mask_ &= ~(1U << bin_idx);
    }
}

void
LRUMemoryManager::real_free(LRUMemoryHandle *handle)
{
    LRUMemoryHunk* hunk = handle->hunk_;

    // Remove from LRU
    hunk->lru_links.prev->next = hunk->lru_links.next;
    hunk->lru_links.next->prev = hunk->lru_links.prev;

    hunk->size = std::abs(hunk->size);
    mem_allocated_size_ -= hunk->size;

    // Coalesce Right
    LRUMemoryHunk* r_neighbor = hunk->phys_next;
    if (r_neighbor && r_neighbor->size > 0) {
        remove_from_free_list(r_neighbor);

        hunk->size += r_neighbor->size;
        hunk->phys_next = r_neighbor->phys_next;
        if (r_neighbor->phys_next) {
            r_neighbor->phys_next->phys_prev = hunk;
        }
    }

    // Coalesce Left
    LRUMemoryHunk* l_neighbor = hunk->phys_prev;
    if (l_neighbor && l_neighbor->size > 0) {
        remove_from_free_list(l_neighbor);

        l_neighbor->size += hunk->size;
        l_neighbor->phys_next = hunk->phys_next;
        if (hunk->phys_next) {
            hunk->phys_next->phys_prev = l_neighbor;
        }
        hunk = l_neighbor;
    }

    hunk->handle = nullptr;

    // Put back to the appropriate free bin
    add_to_free_list(hunk);

    ASAN_POISON_MEMORY_REGION(
        reinterpret_cast<uint8_t*>(hunk) + sizeof(LRUMemoryHunk),
        hunk->size - sizeof(LRUMemoryHunk)
    );

    // invalidate handle
    handle->hunk_ = nullptr;
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
            LOG_INFO("%zu: free space: %p, size: %zu, handle: %p, free_next: %p, free_prev: %p\n",
                hunk_idx, itr, itr->size, itr->handle, itr->free_links.next, itr->free_links.prev);
        } else {
            LOG_INFO("%zu: allocated space: %p, size: %zu, handle: %p, lru_next: %p, lru_prev: %p\n",
                hunk_idx, itr, -itr->size, itr->handle, itr->lru_links.next, itr->lru_links.prev);
        }
        hunk_idx++;
    }
    LOG_INFO("used memory: %zu, total pool size %zu\n", mem_allocated_size_, mem_total_size_);
}

LRUMemoryManager::LruIterator<false>
LRUMemoryManager::begin()
{
    LRUMemoryHunk* head = (lru_anchor_->next != lru_anchor_) ? to_hunk_from_lru(lru_anchor_->next) : nullptr;
    return LRUMemoryManager::LruIterator<false>(
        head ? head->handle : nullptr,
        [this](LRUMemoryHandle* handle) {
            const auto* hunk = handle->hunk_ptr();
            if (hunk->lru_links.next != lru_anchor_) {
                return to_hunk_from_lru(hunk->lru_links.next)->handle;
            }
            return static_cast<LRUMemoryHandle*>(nullptr);
        });
}

LRUMemoryManager::LruIterator<false>
LRUMemoryManager::end()
{
    return LRUMemoryManager::LruIterator<false>(nullptr, nullptr);
}

const LRUMemoryManager::LruIterator<true>
LRUMemoryManager::begin() const
{
    LRUMemoryHunk* head = (lru_anchor_->next != lru_anchor_) ? to_hunk_from_lru(lru_anchor_->next) : nullptr;
    return LRUMemoryManager::LruIterator<true>(
        head ? head->handle : nullptr,
        [this](const LRUMemoryHandle* handle) {
            const auto* hunk = handle->hunk_ptr();
            if (hunk->lru_links.next != lru_anchor_) {
                return to_hunk_from_lru(hunk->lru_links.next)->handle;
            }
            return static_cast<LRUMemoryHandle*>(nullptr);
        });
}

const LRUMemoryManager::LruIterator<true>
LRUMemoryManager::end() const
{
    return LRUMemoryManager::LruIterator<true>(nullptr, nullptr);
}

} // namespace lrumm