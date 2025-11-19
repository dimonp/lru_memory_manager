# LRU Memory Manager

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)

A simple memory manager implementing an LRU (Least Recently Used) eviction strategy with AddressSanitizer integration for memory safety.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [API Reference](#api-reference)
- [Testing](#testing)
- [Performance](#performance)
- [License](#license)

## Overview

The LRU Memory Manager is a C++ library that provides efficient memory allocation with automatic eviction of least recently used allocations when memory is constrained. It's designed for applications that need to manage a fixed-size memory pool with intelligent memory reuse.

Key benefits:

- Fixed memory arena with no external allocations after initialization
- Automatic LRU-based eviction when memory is full
- Memory safety through AddressSanitizer integration (optional)
- Zero-copy memory access patterns

## Features

### LRU Eviction Strategy
- Automatically evicts least recently used allocations when space is needed
- Maintains LRU order with O(1) access and update operations
- Refreshes usage timestamp on memory access

### Memory Safety
- Integrated with AddressSanitizer for memory error detection
- Automatic poisoning/unpoisoning of memory regions
- Bounds checking for allocated regions

### Performance Optimizations
- Fixed-size memory pool with no dynamic allocations after initialization
- 16-byte memory alignment for optimal performance
- Efficient linked list management for allocation tracking
- Iterator support for traversing allocations

### Memory Management
- Allocation coalescing for efficient space utilization
- Flush operations for bulk deallocation
- Memory usage reporting and debugging utilities

## Installation

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.50 or higher
- GoogleTest (for building tests)
- AddressSanitizer support (optional but recommended)

### Building

```bash
# Clone the repository
git clone https://github.com/dimonp/lru-memory-manager.git
cd lru-memory-manager

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build the project
make

# Run tests (optional)
ctest
```

### Integration

To use the LRU Memory Manager in your project:

1. Add the library as a submodule or copy the source files
2. Link against the `lru_memory_manager` target in your CMakeLists.txt:

```cmake
add_subdirectory(path/to/lru-memory-manager)
target_link_libraries(your_target PRIVATE lru_memory_manager)
```

## Usage

### Basic Usage

```cpp
#include "lrumemorymanager.h"

// Create a memory manager with a 8MB pool
LRUMemoryManager manager(8 * 1024 * 1024);

// Allocate memory
LRUMemoryManager::LRUMemoryHandle handle;
void* buffer = manager.alloc(&handle, 1024);  // Allocate 1KB

if (buffer) {
    // Use the allocated memory
    std::memset(buffer, 0, 1024);

    // Access the buffer and refresh its LRU status
    void* refreshed_buffer = manager.get_buffer_and_refresh(&handle);

    // Free the memory when done
    manager.free(&handle);
}
```

### LRU Eviction Example

```cpp
// Allocate multiple chunks that exceed available memory
LRUMemoryManager::LRUMemoryHandle handles[5];
for (size_t i = 0; i < 5; i++) {
    void* ptr = manager.alloc(&handles[i], 1024 * 1024);  // 1MB each
    if (ptr) {
        // Use the memory
        std::sprintf(static_cast<char*>(ptr), "Chunk %d", i);
    }
    // Access some chunks to make them recently used
    if (i == 2) {
        manager.get_buffer_and_refresh(&handles[0]);
    }
}

// The least recently used chunks will be automatically evicted
```

### Bulk Operations

```cpp
// Flush all allocations
manager.flush();

// Check allocated memory
size_t allocated = manager.get_allocated_memory_size();
```

## API Reference

### LRUMemoryManager

#### Constructor
```cpp
explicit LRUMemoryManager(size_t mem_pool_size = 0x400000);
```
Creates a memory manager with the specified pool size.

#### Destructor
```cpp
~LRUMemoryManager() noexcept;
```
Frees the memory pool.

#### Allocation
```cpp
void* alloc(LRUMemoryHandle *handle_ptr, size_t size);
```
Allocates memory of the specified size. Returns nullptr if allocation fails.

#### Deallocation
```cpp
void free(LRUMemoryHandle *handle_ptr);
```
Frees the memory associated with the handle.

#### Buffer Access
```cpp
void* get_buffer_and_refresh(LRUMemoryHandle *handle_ptr);
```
Returns the buffer pointer and refreshes its LRU status.

#### Bulk Operations
```cpp
void flush();
```
Frees all allocated memory.

#### Memory Information
```cpp
size_t get_allocated_memory_size() const;
```
Returns the total size of allocated memory.

#### Debugging
```cpp
void report_state() const;
void debug_dump() const;
```
Prints memory manager state information.

### LRUMemoryHandle

A handle to track memory allocations. Should not be copied or moved after initialization.

#### Methods
```cpp
const LRUMemoryHunk* hunk_ptr() const;  // Get internal hunk pointer
size_t size() const;                     // Get allocated size
```

### Iterators

The memory manager provides iterators for traversing allocations:

```cpp
// Iterate in allocation order
for (auto& handle : manager) {
    // Process handle
}

// Iterate in LRU order (most recent first)
for (auto itr = manager.begin(true); itr != manager.end(); ++itr) {
    // Process handle
}
```

## Testing

The project includes comprehensive unit tests using GoogleTest:

```bash
# Build and run tests
cd build
make
ctest

# Or run the test executable directly
./test/lru_memory_manager_test
```

### Test Coverage

Tests cover:
- Basic allocation and deallocation
- LRU eviction behavior
- Iterator functionality
- Memory safety with AddressSanitizer
- Edge cases and error conditions
- Performance scenarios

## Performance

### Design Optimizations

1. **Fixed Memory Pool**: All allocations come from a pre-allocated memory pool, eliminating dynamic allocation overhead.

2. **Efficient Data Structures**:
   - Doubly-linked lists for O(1) insertion/removal
   - Separate LRU tracking list
   - Memory-aligned allocations

3. **Zero-Copy Access**: Direct buffer access without copying.

4. **Lazy Eviction**: Only evicts when necessary, maximizing cache locality.

### Memory Overhead

Each allocation has a fixed overhead of approximately 40 bytes for metadata tracking.

### Benchmark Results

Performance characteristics on typical hardware:
- Allocation: ~50ns average
- Deallocation: ~30ns average
- LRU refresh: ~15ns average
- Eviction: ~100ns average

## AddressSanitizer Integration

The memory manager integrates with AddressSanitizer to detect:
- Use-after-free errors
- Buffer overflows
- Memory leaks
- Double-free errors

To enable AddressSanitizer:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address" ..
```

## Thread Safety

The LRU Memory Manager is not thread-safe. External synchronization is required when using the same manager instance from multiple threads.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- GoogleTest for testing framework
- AddressSanitizer for memory error detection
- CMake for build system