#ifndef LRU_MEMORY_MANAGER__H
#define LRU_MEMORY_MANAGER__H

#include <cstdint>
#include <cassert>
#include <iterator>
#include <type_traits>
#include <gsl/gsl>

#ifndef LOG_ERROR
#define LOG_ERROR(...) std::fprintf(stderr, __VA_ARGS__)
#endif

#ifndef LOG_INFO
#define LOG_INFO(...) std::fprintf(stdout, __VA_ARGS__)
#endif

namespace lrumm {

/**
 * @brief A memory manager implementing an LRU (Least Recently Used) eviction strategy
 *
 * This memory manager allocates memory from a fixed-size pool and automatically
 * evicts the least recently used allocations when space is needed.
 */
class LRUMemoryManager {
public:
    static constexpr size_t BLOCK_ALIGNMENT = 64;

    struct LRUMemoryHunk;

    /**
     * @brief Handle to a memory allocation
     *
     * This handle is used to track and manage memory allocations.
     * It should not be copied or moved after allocation.
     */
    struct LRUMemoryHandle {
        LRUMemoryHandle() = default;

        // Should not be copying and moving after initialization
        LRUMemoryHandle(const LRUMemoryHandle& other) { Expects(other.hunk_ == nullptr); } // Copyable in initial state only.
        void operator= (const LRUMemoryHandle& other) { Expects(other.hunk_ == nullptr); } // Copyable in initial state only.
        LRUMemoryHandle(LRUMemoryHandle&& other) { Expects(other.hunk_ == nullptr); } // Movable in initial state only.
        void operator= (LRUMemoryHandle&& other) { Expects(other.hunk_ == nullptr); } // Movable in initial state only.

        const LRUMemoryHunk* hunk_ptr() const { return hunk_; }
        size_t size() const;
    private:
        LRUMemoryHunk *hunk_ = nullptr;
        friend LRUMemoryManager;
    };

    template<bool IsConst>
    class LruIterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using pointer           = std::conditional_t<IsConst, const LRUMemoryHandle*, LRUMemoryHandle*>;
        using reference         = std::conditional_t<IsConst, const LRUMemoryHandle&, LRUMemoryHandle&>;

        explicit LruIterator(pointer handle_ptr, std::function<pointer(pointer)> get_next)
            : current_handle_(handle_ptr), get_next_(get_next) {}

        reference operator*() const { return *current_handle_; }
        pointer operator->() const { return current_handle_; }

        LruIterator& operator++()
        {
            current_handle_ = get_next_(current_handle_);
            return *this;
        }

        bool operator==(const LruIterator& other) const { return current_handle_ == other.current_handle_; };
        bool operator!=(const LruIterator& other) const { return current_handle_ != other.current_handle_; };

    private:
        std::function<pointer(pointer)> get_next_;
        pointer current_handle_;
    };

    const LruIterator<true> begin() const;
    const LruIterator<true> end() const;
    LruIterator<false> begin();
    LruIterator<false> end();

    explicit LRUMemoryManager(size_t mem_pool_size = 4 * 1024 * 1024);
    ~LRUMemoryManager() noexcept;

    LRUMemoryManager(const LRUMemoryManager&) = delete;
    LRUMemoryManager& operator=(const LRUMemoryManager&) = delete;

    void* alloc(LRUMemoryHandle *handle, size_t size) noexcept;
    void free(LRUMemoryHandle *handle) noexcept;
    void* get_buffer_and_refresh(LRUMemoryHandle *handle) noexcept;
    void flush();
    void arena_clean();

    void lru_state() const;
    void debug_dump() const;

    size_t get_allocated_memory_size() const;

private:
    struct ListLinks {
        ListLinks *next = nullptr;
        ListLinks *prev = nullptr;
    };

    void init_pool();

    void* real_get_buffer(LRUMemoryHandle *handle) noexcept;
    void* real_alloc(LRUMemoryHandle *handle, size_t size) noexcept;
    void real_free(LRUMemoryHandle *handle) noexcept;

    // Core allocation sub-steps
    inline LRUMemoryHunk* find_free_block(size_t size) const noexcept;
    inline void split_block(LRUMemoryHunk* hunk, size_t size) noexcept;
    inline void activate_lru_hunk(LRUMemoryHunk* hunk) noexcept;
    inline LRUMemoryHunk* try_alloc(size_t size) noexcept;

    // Memory state management (Bitmap & Rings)
    inline void add_to_free_list(LRUMemoryHunk* hunk) noexcept;
    inline void remove_from_free_list(LRUMemoryHunk* hunk) noexcept;

    inline LRUMemoryHunk* get_head_hunk() const noexcept;
    inline static LRUMemoryHunk* to_hunk_from_free(ListLinks* l) noexcept;
    inline static LRUMemoryHunk* to_hunk_from_lru(ListLinks* l) noexcept;

    // Bitmap of non-empty bins for O(1) bin selection
    uint32_t free_bin_mask_;
    ListLinks* free_bins_;
    ListLinks* lru_anchor_;

    alignas(BLOCK_ALIGNMENT) char* mem_arena_;

    size_t mem_total_size_;      ///< Total size of the memory pool
    size_t mem_allocated_size_;  ///< Currently allocated size
};

// Inline implementations
inline
void*
LRUMemoryManager::get_buffer_and_refresh(LRUMemoryHandle *handle) noexcept
{
    Ensures(handle != nullptr);
    return real_get_buffer(handle);
}

inline
void
LRUMemoryManager::free(LRUMemoryHandle *handle) noexcept
{
    if (!handle || !handle->hunk_ptr()) {
        return;
    }
    Ensures(handle->hunk_ != nullptr);
    real_free(handle);
}

inline
void*
LRUMemoryManager::alloc(LRUMemoryHandle *handle, size_t size) noexcept
{
    Ensures(size > 0);
    Ensures(handle != nullptr);
    Ensures(handle->hunk_ == nullptr);
    return real_alloc(handle, size);
}

inline
size_t
LRUMemoryManager::get_allocated_memory_size() const
{
    return mem_allocated_size_;
}

} // namespace lrumm

#endif // LRU_MEMORY_MANAGER__H
