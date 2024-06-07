#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <ctime>
#include <cassert>

// BlockHeader structure to represent metadata for each block
struct BlockHeader {
    size_t size;
    bool is_free;
    bool is_first;
    bool is_last;
};

// Arena structure to represent a memory arena
struct Arena {
    size_t size;
    void* base;
    Arena* next;
};

// Global variables for the memory allocator
size_t default_arena_size;
Arena* arena_list = nullptr;
std::unordered_map<void*, BlockHeader*> block_map;

// Function to align the size to a 4-byte boundary
size_t align(size_t size) {
    return (size + 3) & ~3;
}

// Function to create a new memory arena
Arena* arena_create(size_t size) {
    size = max(size, default_arena_size);
    size = align(size);

    void* base = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) {
        return nullptr;
    }

    Arena* arena = new Arena{ size, base, arena_list };
    arena_list = arena;

    BlockHeader* initial_block = reinterpret_cast<BlockHeader*>(base);
    initial_block->size = size - sizeof(BlockHeader);
    initial_block->is_free = true;
    initial_block->is_first = true;
    initial_block->is_last = true;

    block_map[base] = initial_block;

    return arena;
}

// Function to split a block if it's larger than the requested size
void block_split(BlockHeader* block, size_t size) {
    size = align(size);
    if (block->size >= size + sizeof(BlockHeader) + 4) {
        BlockHeader* new_block = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<char*>(block) + sizeof(BlockHeader) + size);
        new_block->size = block->size - size - sizeof(BlockHeader);
        new_block->is_free = true;
        new_block->is_first = false;
        new_block->is_last = block->is_last;

        block->size = size;
        block->is_last = false;

        block_map[new_block] = new_block;
    }
}

// Function to coalesce adjacent free blocks
void block_unite() {
    for (auto it = block_map.begin(); it != block_map.end();) {
        BlockHeader* block = it->second;
        if (block->is_free) {
            char* base = reinterpret_cast<char*>(block);
            BlockHeader* next_block = reinterpret_cast<BlockHeader*>(
                    base + sizeof(BlockHeader) + block->size);

            if (block_map.find(next_block) != block_map.end() && next_block->is_free) {
                block->size += sizeof(BlockHeader) + next_block->size;
                block->is_last = next_block->is_last;
                block_map.erase(next_block);
                continue;
            }
        }
        ++it;
    }
}

// Function to allocate a block of memory from an arena
void* block_alloc(Arena* arena, size_t size) {
    size = align(size);

    char* base = static_cast<char*>(arena->base);
    while (reinterpret_cast<size_t>(base) < reinterpret_cast<size_t>(arena->base) + arena->size) {
        BlockHeader* block = reinterpret_cast<BlockHeader*>(base);
        if (block->is_free && block->size >= size) {
            block_split(block, size);
            block->is_free = false;
            return base + sizeof(BlockHeader);
        }
        base += sizeof(BlockHeader) + block->size;
    }
    return nullptr;
}

// Function to allocate memory
void* mem_alloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    size = align(size);

    for (Arena* arena = arena_list; arena; arena = arena->next) {
        if (void* ptr = block_alloc(arena, size)) {
            return ptr;
        }
    }

    Arena* new_arena = arena_create(size + sizeof(BlockHeader));
    if (!new_arena) {
        return nullptr;
    }

    return block_alloc(new_arena, size);
}

// Function to free memory
void mem_free(void* ptr) {
    if (!ptr) {
        return;
    }

    auto it = block_map.find(static_cast<char*>(ptr) - sizeof(BlockHeader));
    if (it == block_map.end()) {
        return;
    }

    BlockHeader* block = it->second;
    block->is_free = true;
    block_unite();
}

// Function to reallocate memory
void* mem_realloc(void* ptr, size_t size) {
    if (!ptr) {
        return mem_alloc(size);
    }

    size = align(size);

    auto it = block_map.find(static_cast<char*>(ptr) - sizeof(BlockHeader));
    if (it == block_map.end()) {
        return nullptr;
    }

    BlockHeader* block = it->second;
    if (block->size >= size) {
        block_split(block, size);
        return ptr;
    }

    void* new_ptr = mem_alloc(size);
    if (!new_ptr) {
        return nullptr;
    }

    memcpy(new_ptr, ptr, block->size);
    mem_free(ptr);
    return new_ptr;
}

// Function to display the memory layout
void mem_show() {
    for (Arena* arena = arena_list; arena; arena = arena->next) {
        std::cout << "Arena (" << arena->size << "b)" << std::endl;
        char* base = static_cast<char*>(arena->base);
        while (reinterpret_cast<size_t>(base) < reinterpret_cast<size_t>(arena->base) + arena->size) {
            BlockHeader* block = reinterpret_cast<BlockHeader*>(base);
            std::cout << (block->is_free ? "*" : " ") << " Block at " << reinterpret_cast<void*>(base)
                      << " -> Size: " << block->size
                      << ", Busy: " << (block->is_free ? "No" : "Yes")
                      << ", First: " << (block->is_first ? "Yes" : "No")
                      << ", Last: " << (block->is_last ? "Yes" : "No") << std::endl;
            base += sizeof(BlockHeader) + block->size;
        }
    }
    std::cout << "----------" << std::endl;
}

// Function to calculate checksum
uint32_t checksum(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; ++i) {
        checksum += bytes[i];
    }
    return checksum;
}

// Function to fill data with random values
void random_input(void* data, size_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = rand() % 256;
    }
}

// Function to run automatic tests
void tester(size_t iterations, size_t max_block_size) {
    default_arena_size = 4096;
    std::vector<BlockHeader*> arenas;
    std::vector<void*> allocations;
    srand(static_cast<unsigned>(time(0)));

    for (size_t i = 0; i < iterations; ++i) {
        std::cout << i + 1 << "/" << iterations << ": \n";
        int operation = rand() % 3;
        switch (operation) {
            case 0: { // mem_alloc
                size_t size = rand() % max_block_size + 1;
                std::cout << "mem_alloc(size=" << size << ")" << std::endl;
                void* ptr = mem_alloc(size);
                if (ptr) {
                    random_input(ptr, size);
                    allocations.push_back(ptr);
                }
                break;
            }
            case 1: { // mem_free
                if (!allocations.empty()) {
                    size_t index = rand() % allocations.size();
                    void* ptr = allocations[index];
                    std::cout << "mem_free(ptr=" << ptr << ")" << std::endl;
                    mem_free(ptr);
                    allocations.erase(allocations.begin() + index);
                }
                break;
            }
            case 2: { // mem_realloc
                if (!allocations.empty()) {
                    size_t index = rand() % allocations.size();
                    void* ptr = allocations[index];
                    size_t new_size = rand() % max_block_size + 1;
                    std::cout << "mem_realloc(ptr=" << ptr << ", new_size=" << new_size << ")" << std::endl;
                    void* new_ptr = mem_realloc(ptr, new_size);
                    if (new_ptr) {
                        random_input(new_ptr, new_size);
                        allocations[index] = new_ptr;
                    }
                }
                break;
            }
        }
        mem_show();
    }

    for (void* ptr : allocations) {
        mem_free(ptr);
    }

    std::cout << "Automatic test completed" << std::endl;
}

int main() {
    default_arena_size = 4096;

    /*
    tester((size_t) 10, (size_t) 1024);
    */

    void* p0 = mem_alloc(2000);
    mem_show();
    void* p1 = mem_alloc(8501);
    mem_show();
    void* p2 = mem_alloc(99999999999999999);
    mem_show();
    void* p3 = mem_alloc(200);
    void* p4 = mem_alloc(200);
    void* p5 = mem_alloc(200);
    mem_show();
    mem_realloc(p3, 300);
    mem_free(p4);
    mem_free(p5);
    mem_show();

    return 0;
}
