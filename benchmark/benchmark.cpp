#include <iostream>
#include <vector>
#include <random>
#include <benchmark/benchmark.h>

#include "lru_memory_manager/lrumemorymanager.h"

// Benchmark for allocating memory with random sizes
static void BM_LRUAllocAllocationRandomSize(benchmark::State& state) 
{
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles;
    handles.resize(100000000);
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    size_t handle_idx = 0;
    for ([[maybe_unused]] auto _ : state) {
        if (handle_idx == handles.size()) {
            state.SkipWithError("Reached max iterations limit");
            break;
        }

        size_t alloc_size = std::rand() % (4 * 1024) + 1;
        auto* handle = &handles[handle_idx++];
        manager.alloc(handle, alloc_size);
    }
}

// Benchmark for allocating memory and freeing it
static void BM_LRUAllocAllocationFree(benchmark::State& state) 
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    size_t alloc_size = state.range(0); // Get size from benchmark argument
    for ([[maybe_unused]] auto _ : state) {
        manager.alloc(&handle, alloc_size);
        manager.free(&handle); // Deallocate memory
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * alloc_size);
    state.SetLabel("alloc_free");

    state.SetComplexityN(state.range(0));
}

// Benchmark for allocating memory with fixed size
static void BM_LRUAllocAllocation(benchmark::State& state) 
{
    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles;
    handles.resize(100000000);
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    size_t alloc_size = state.range(0);
    size_t handle_idx = 0;
    for ([[maybe_unused]] auto _ : state) {
        if (handle_idx == handles.size()) {
            state.SkipWithError("Reached max iterations limit");
            break;
        }

        auto* handle = &handles[handle_idx++];
        manager.alloc(handle, alloc_size);
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * alloc_size);
    state.SetLabel("alloc");

    state.SetComplexityN(state.range(0));
}

// Benchmark for allocating memory with random sizes
static void BM_LRUAllocAllocationFreeRandomSize(benchmark::State& state) 
{
    lrumm::LRUMemoryManager::LRUMemoryHandle handle;
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    for ([[maybe_unused]] auto _ : state) {
        size_t alloc_size = std::rand() % (4 * 1024) + 1;
        manager.alloc(&handle, alloc_size);
        manager.free(&handle); // Deallocate memory
    }
}

// Benchmark for get_buffer_and_refresh (accessing and refreshing LRU items)
static void BM_LRUGetBufferAndRefresh(benchmark::State& state) 
{
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    // Pre-allocate handles
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
    }

    size_t index = 0;
    for ([[maybe_unused]] auto _ : state) {
        // Access and refresh a buffer (moves it to front of LRU list)
        manager.get_buffer_and_refresh(&handles[index]);
        index = (index + 1) % num_handles;
    }

    // Clean up
    for (size_t i = 0; i < num_handles; ++i) {
        manager.free(&handles[i]);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("get_buffer_and_refresh");

    state.SetComplexityN(state.range(1));
}

// Benchmark for freeing memory
static void BM_LRUFree(benchmark::State& state) 
{
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    // Pre-allocate handles
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
    }

    size_t index = 0;
    for ([[maybe_unused]] auto _ : state) {
        // Free a buffer
        manager.free(&handles[index]);
        benchmark::DoNotOptimize(handles[index]);

        // Reallocate to maintain steady state
        state.PauseTiming(); // Stop the timer before preparation
        pointers[index] = manager.alloc(&handles[index], alloc_size);
        state.ResumeTiming(); // Start the timer for the code under test

        index = (index + 1) % num_handles;
    }

    // Clean up
    for (size_t i = 0; i < num_handles; ++i) {
        manager.free(&handles[i]);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("free");

    state.SetComplexityN(state.range(1));
}

// Benchmark for mixed allocation/deallocation workload
static void BM_LRUMixedWorkload(benchmark::State& state) 
{
    size_t num_handles = state.range(0);
    size_t alloc_size = state.range(1);

    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles(num_handles);
    lrumm::LRUMemoryManager manager(16 * 1024 * 1024);

    // Pre-allocate handles
    std::vector<void*> pointers(num_handles);

    // Allocate all memory first
    for (size_t i = 0; i < num_handles; ++i) {
        pointers[i] = manager.alloc(&handles[i], alloc_size);
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
            free_count++;
        } else if (pointers[index] == nullptr) {
            // Allocate a new buffer
            pointers[index] = manager.alloc(&handles[index], alloc_size);
            alloc_count++;
        } else {
            // Access and refresh an existing buffer
            manager.get_buffer_and_refresh(&handles[index]);
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

    state.SetComplexityN(state.range(1));
}

// Benchmark for LRU eviction performance
static void BM_LRUEviction(benchmark::State& state) 
{
    size_t pool_size = state.range(0);
    size_t alloc_size = state.range(1);
    size_t num_allocations = state.range(2);

    lrumm::LRUMemoryManager::LRUMemoryHandle handle0, handle1;
    lrumm::LRUMemoryManager manager(pool_size);

    // Pre-allocate handles
    std::vector<void*> pointers(num_allocations);

    size_t evicted_count = 0;
    for ([[maybe_unused]] auto _ : state) {

        state.PauseTiming(); // Stop the timer before preparation
        manager.alloc(&handle0, alloc_size);
        state.ResumeTiming(); // Start the timer for the code under test

        void* pointer = manager.alloc(&handle1, pool_size);
        if (pointer == nullptr) {
            // Whole pool allocation failed, which means evictions occurred
            evicted_count++;
        }
        benchmark::DoNotOptimize(pointer);
    }

    state.SetItemsProcessed(state.iterations() * num_allocations);
    state.SetLabel("eviction");
    state.counters["Evictions"] = benchmark::Counter(evicted_count, benchmark::Counter::kAvgThreads);

    state.SetComplexityN(state.range(1));
}

static void BM_LRURandomAllocFree(benchmark::State& state) 
{
    static constexpr size_t MIN_ALLOC = 128;
    static constexpr size_t MAX_ALLOC = 4096;

    std::vector<lrumm::LRUMemoryManager::LRUMemoryHandle> handles;
    handles.resize(10000);

    size_t arena_size = state.range(0) * 1024 * 1024;
    lrumm::LRUMemoryManager manager(arena_size);

    std::default_random_engine gen(42);
    std::uniform_int_distribution<size_t> size_dist(MIN_ALLOC, MAX_ALLOC);
    std::uniform_int_distribution<int> op_dist(0, 100);

    for ([[maybe_unused]] auto _ : state) {
        size_t idx = gen() % handles.size();
        auto& handle = handles[idx];

        if (op_dist(gen) < 80 && handle.hunk_ptr() == nullptr) {
            size_t size = size_dist(gen);
            manager.alloc(&handle, size);
        } else {
            manager.free(&handle);
        }

        benchmark::DoNotOptimize(manager);
    }
}

BENCHMARK(BM_LRUAllocAllocationRandomSize);
BENCHMARK(BM_LRUAllocAllocationFreeRandomSize);
BENCHMARK(BM_LRUAllocAllocation)->Range(8, 8 << 20)->Complexity();
BENCHMARK(BM_LRUAllocAllocationFree)->Range(8, 8 << 20)->Complexity();
BENCHMARK(BM_LRUGetBufferAndRefresh)->Ranges({{1, 1 << 10}, {64, 1 << 10}})->Complexity();
BENCHMARK(BM_LRUFree)->Ranges({{1, 1 << 10}, {64, 1 << 10}})->Complexity();
BENCHMARK(BM_LRUMixedWorkload)->Ranges({{1, 1 << 10}, {64, 1 << 10}})->Complexity();
BENCHMARK(BM_LRUEviction)->Ranges({{4096, 16 * 4096}, {128, 2048}})->Complexity();
BENCHMARK(BM_LRURandomAllocFree)->Ranges({{4, 256}});

BENCHMARK_MAIN();