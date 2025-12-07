#include <benchmark/benchmark.h>

#include "lrumemorymanager.h"
#include <vector>
#include <random>

// Benchmark for allocating memory using 'alloc'
static void BM_LRUAllocAllocation(benchmark::State& state) {
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;

    size_t size = state.range(0); // Get size from benchmark argument
    for ([[maybe_unused]] auto _ : state) {
        void* data = manager.alloc(&handle, size);
        benchmark::DoNotOptimize(data); // Prevent compiler from optimizing out allocation
        manager.free(&handle); // Deallocate memory
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * size);
    state.SetLabel("alloc");
}

// Benchmark for get_buffer_and_refresh (accessing and refreshing LRU items)
static void BM_LRUGetBufferAndRefresh(benchmark::State& state) {
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    // Pre-allocate handles
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
        benchmark::DoNotOptimize(pointers[i]);
    }

    size_t index = 0;
    for ([[maybe_unused]] auto _ : state) {
        // Access and refresh a buffer (moves it to front of LRU list)
        void* data = manager.get_buffer_and_refresh(&handles[index]);
        benchmark::DoNotOptimize(data);

        index = (index + 1) % num_handles;
    }

    // Clean up
    for (size_t i = 0; i < num_handles; ++i) {
        manager.free(&handles[i]);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("get_buffer_and_refresh");
}

// Benchmark for freeing memory
static void BM_LRUFree(benchmark::State& state) {
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    // Pre-allocate handles
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
        benchmark::DoNotOptimize(pointers[i]);
    }

    size_t index = 0;
    for ([[maybe_unused]] auto _ : state) {
        // Free a buffer
        manager.free(&handles[index]);
        benchmark::DoNotOptimize(handles[index]);

        // Reallocate to maintain steady state
        pointers[index] = manager.alloc(&handles[index], alloc_size);
        benchmark::DoNotOptimize(pointers[index]);

        index = (index + 1) % num_handles;
    }

    // Clean up
    for (size_t i = 0; i < num_handles; ++i) {
        manager.free(&handles[i]);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("free");
}

// Benchmark for mixed allocation/deallocation workload
static void BM_LRUMixedWorkload(benchmark::State& state) {
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    // Pre-allocate handles
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
        benchmark::DoNotOptimize(pointers[i]);
    }

    // Random number generator for mixed workload
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, num_handles - 1);

    size_t alloc_count = 0;
    size_t free_count = 0;

    for ([[maybe_unused]] auto _ : state) {
        // Randomly choose to allocate or free
        int choice = dis(gen) % 2;
        size_t index = dis(gen);

        if (choice == 0 && pointers[index] != nullptr) {
            // Free an existing allocation
            manager.free(&handles[index]);
            pointers[index] = nullptr;
            benchmark::DoNotOptimize(handles[index]);
            free_count++;
        } else if (pointers[index] == nullptr) {
            // Allocate a new buffer
            pointers[index] = manager.alloc(&handles[index], alloc_size);
            benchmark::DoNotOptimize(pointers[index]);
            alloc_count++;
        } else {
            // Access and refresh an existing buffer
            void* data = manager.get_buffer_and_refresh(&handles[index]);
            benchmark::DoNotOptimize(data);
        }
    }

    // Clean up
    for (size_t i = 0; i < num_handles; ++i) {
        if (pointers[i] != nullptr) {
            manager.free(&handles[i]);
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("mixed_workload");
}

// Benchmark for LRU eviction performance
static void BM_LRUEviction(benchmark::State& state) {
    size_t pool_size = state.range(0);
    size_t alloc_size = state.range(1);
    size_t num_allocations = state.range(2);

    lrumm::LRUMemoryManager manager(pool_size);

    // Pre-allocate handles
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_allocations);
    std::vector<void*> pointers(num_allocations);

    size_t evicted_count = 0;

    for ([[maybe_unused]] auto _ : state) {

        // Allocate all memory
        for (size_t i = 0; i < num_allocations; ++i) {
            pointers[i] = manager.alloc(&handles[i], alloc_size);
            benchmark::DoNotOptimize(pointers[i]);
        }

        void* pointer = manager.alloc(&handle, pool_size);
        if (pointer == nullptr) {
            // Whole pool allocation failed, which means evictions occurred
            evicted_count++;
        }
        benchmark::DoNotOptimize(pointer);
    }

    state.SetItemsProcessed(state.iterations() * num_allocations);
    state.SetLabel("eviction");
    state.counters["Evictions"] = benchmark::Counter(evicted_count, benchmark::Counter::kAvgThreads);
}

// Benchmark for iterator performance
static void BM_LRUIterator(benchmark::State& state) {
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    // Pre-allocate handles
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
        benchmark::DoNotOptimize(pointers[i]);
    }

    size_t count = 0;
    for ([[maybe_unused]] auto _ : state) {
        // Iterate through all allocations in LRU order
        for (auto& handle : manager) {
            benchmark::DoNotOptimize(handle);
            count++;
        }
    }

    // Clean up
    for (size_t i = 0; i < num_handles; ++i) {
        manager.free(&handles[i]);
    }

    state.SetItemsProcessed(state.iterations() * num_handles);
    state.SetLabel("iterator");
}

BENCHMARK(BM_LRUAllocAllocation)->Range(8, 8 << 20);
BENCHMARK(BM_LRUGetBufferAndRefresh)->Ranges({{1, 1 << 10}, {64, 1 << 10}});
BENCHMARK(BM_LRUFree)->Ranges({{1, 1 << 10}, {64, 1 << 10}});
BENCHMARK(BM_LRUMixedWorkload)->Ranges({{1, 1 << 10}, {64, 1 << 10}});
BENCHMARK(BM_LRUEviction)->Ranges({{1024, 16 * 1024}, {32, 128}, {10, 100}});
BENCHMARK(BM_LRUIterator)->Ranges({{1, 1 << 10}, {64, 1 << 10}});

BENCHMARK_MAIN();