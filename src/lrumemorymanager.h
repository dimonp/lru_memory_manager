#ifndef LRU_MEMORY_MANAGER_H
#define LRU_MEMORY_MANAGER_H

#include <cstddef>
#include <cassert>
#include <cstdio>
#include <iterator>
#include <type_traits>

#define log_error(...) std::fprintf(stderr, __VA_ARGS__)
#define log_info(...) std::fprintf(stdout, __VA_ARGS__)

/**
 * @brief A memory manager implementing an LRU (Least Recently Used) eviction strategy
 *
 * This memory manager allocates memory from a fixed-size pool and automatically
 * evicts the least recently used allocations when space is needed.
 */
class LRUMemoryManager {
public:
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
        LRUMemoryHandle(const LRUMemoryHandle& other) { assert(other.hunk_ptr_ == nullptr && "Copyable in initial state only."); }
        void operator= (const LRUMemoryHandle& other) { assert(other.hunk_ptr_ == nullptr && "Copyable in initial state only."); }
        LRUMemoryHandle(LRUMemoryHandle&& other) { assert(other.hunk_ptr_ == nullptr && "Movable in initial state only."); }
        void operator= (LRUMemoryHandle&& other) { assert(other.hunk_ptr_ == nullptr && "Movable in initial state only."); }

        const LRUMemoryHunk* hunk_ptr() const { return hunk_ptr_; }

        LRUMemoryHandle* next() const;
        LRUMemoryHandle* most_recent() const;

        size_t size() const;
    private:
        LRUMemoryHunk *hunk_ptr_ = nullptr;
        friend LRUMemoryManager;
    };

    template<bool IsConst>
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<IsConst, const LRUMemoryHandle*, LRUMemoryHandle*>;
        using reference = std::conditional_t<IsConst, const LRUMemoryHandle&, LRUMemoryHandle&>;

        explicit Iterator(pointer handle_ptr, bool is_lru_order = true)
            : current_handle_ptr_(handle_ptr), is_lru_order_(is_lru_order) {}

        reference operator*() const { return *current_handle_ptr_; }
        pointer operator->() const { return current_handle_ptr_; }

        Iterator& operator++()
        {
            current_handle_ptr_ = is_lru_order_ ? current_handle_ptr_->most_recent() : current_handle_ptr_->next();
            return *this;
        }

        bool operator==(const Iterator& other) const { return current_handle_ptr_ == other.current_handle_ptr_; };
        bool operator!=(const Iterator& other) const { return current_handle_ptr_ != other.current_handle_ptr_; };
    private:
        pointer current_handle_ptr_;
        bool is_lru_order_;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    explicit LRUMemoryManager(size_t mem_pool_size = 4 * 1024 * 1024);
    ~LRUMemoryManager() noexcept;

    LRUMemoryManager(const LRUMemoryManager&) = delete;
    LRUMemoryManager& operator=(const LRUMemoryManager&) = delete;

    void* alloc(LRUMemoryHandle *handle_ptr, size_t size);
    void free(LRUMemoryHandle *handle_ptr);
    void* get_buffer_and_refresh(LRUMemoryHandle *handle_ptr);
    void flush();

    void report_state() const;
    void debug_dump() const;

    size_t get_allocated_memory_size() const;

    iterator begin(bool lru = true);
    iterator end();
    const_iterator begin(bool lru = true) const;
    const_iterator end() const;

    static LRUMemoryManager& get_instance();

private:
    LRUMemoryHunk* get_head_hunk() const;

    LRUMemoryHunk* try_alloc(size_t size);
    void* real_get_buffer(LRUMemoryHandle *handle_ptr);
    void* real_alloc(LRUMemoryHandle *handle_ptr, size_t size);
    void real_free(LRUMemoryHandle *handle_ptr);

    void unlink_lru(LRUMemoryHunk *hunk_ptr);
    void link_lru(LRUMemoryHunk *hunk_ptr);

    size_t mem_total_size_;      ///< Total size of the memory pool
    size_t mem_allocated_size_;  ///< Currently allocated size
    void* mem_arena_ptr_;         ///< Pointer to the memory pool
};

// Inline implementations
inline
LRUMemoryManager::LRUMemoryHunk*
LRUMemoryManager::get_head_hunk() const
{
    return static_cast<LRUMemoryHunk*>(mem_arena_ptr_);
}

inline
void*
LRUMemoryManager::get_buffer_and_refresh(LRUMemoryHandle *handle_ptr)
{
    return real_get_buffer(handle_ptr);
}

inline
void
LRUMemoryManager::free(LRUMemoryHandle *handle_ptr)
{
    real_free(handle_ptr);
}

inline
void*
LRUMemoryManager::alloc(LRUMemoryHandle *handle_ptr, size_t size)
{
    return real_alloc(handle_ptr, size);
}

inline
size_t
LRUMemoryManager::get_allocated_memory_size() const
{
    return mem_allocated_size_;
}

#endif // LRU_MEMORY_MANAGER_H
