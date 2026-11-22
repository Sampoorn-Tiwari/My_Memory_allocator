#pragma once
#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <sys/mman.h> //Linux system calls : mmap() and munmap()


inline size_t align_to_16(size_t size){
    if(size == 0) return 16;
    size_t remainder = size % 16;
    if(remainder == 0) return size;
    return size + (16-remainder);
}

inline size_t next_power_of_two(size_t size){
    size_t power = 16;
    while(power < size){
        power *= 2;
    }
    return power;
}

inline int get_size_index(size_t size){
    int index = 0;
    size_t current_size = 16;
    while(current_size < size){
        current_size *= 2;
        index++;
    }
    return index;
}

class Arena{
    public:

    static const size_t METADATA_SIZE = 4 * 1024 * 1024;
    static const size_t PAYLOAD_SIZE = 16 * 1024 * 1024;
    static const size_t TOTAL_SIZE = METADATA_SIZE + PAYLOAD_SIZE;

    uint8_t* raw_memory;
    uint8_t* metadata_base;
    uint8_t* arena_base;
    size_t metadata_used;

    Arena(){

        void* ptr = mmap(nullptr , TOTAL_SIZE , PROT_READ | PROT_WRITE , MAP_PRIVATE | MAP_ANONYMOUS , -1 , 0);

        if(ptr == MAP_FAILED){
            std::cerr << "Fatal Error: OS mmap() failed!\n";
            std::exit(1);
        }

        raw_memory = static_cast<uint8_t*>(ptr);
        metadata_base = raw_memory;
        arena_base = raw_memory + METADATA_SIZE;
        metadata_used = 0;
    }

    ~Arena(){

        if(raw_memory != nullptr){
            munmap(raw_memory , TOTAL_SIZE);
        }
    }

    void* allocate_metadata(size_t struct_size){
        struct_size = align_to_16(struct_size);

        if(metadata_used + struct_size > METADATA_SIZE){
            std::cerr << "Error: 4 MB Metadata Zone is full!\n";
            return nullptr;
        }

        uint8_t* ptr = metadata_base + metadata_used;
        metadata_used += struct_size;
        return ptr;
    }

    bool is_valid_playload_ptr(void* ptr){
        uint8_t* byte_ptr = static_cast<uint8_t*>(ptr);
        return (byte_ptr >= arena_base) && (byte_ptr <= (arena_base + PAYLOAD_SIZE));
    }

    size_t get_offset(void* block_ptr){
        uint8_t* byte_ptr = static_cast<uint8_t*>(block_ptr);
        return byte_ptr - arena_base;
    }

    uint8_t* get_ptr(size_t offset){
        return arena_base + offset;
    }

    uint8_t* get_buddy(void* block_ptr , size_t block_size){
        size_t offset = get_offset(block_ptr);
        size_t buddy_offset = offset ^ block_size;
        return get_ptr(buddy_offset);
    }
};