#pragma once
#include <iostream>
#include "phase1_arena.hpp"
struct HashNode{

    void* block_ptr;
    size_t block_size;
    bool is_free;
    int allocator_type;
    HashNode* next;

};

class MetadataHashTable{
    public:
    static const size_t NUM_BUCKETS = 1024;

    Arena* arena;
    HashNode* buckets[NUM_BUCKETS];
    HashNode* recycled_nodes;

    void init(Arena* arena_ptr){
        arena = arena_ptr;
        recycled_nodes = nullptr;

        for(size_t i = 0 ; i < NUM_BUCKETS ; i++){
            buckets[i] = nullptr;
        }
    }

    size_t get_bucket_index(void* ptr){
        size_t offest = arena->get_offset(ptr);
        return (offest / 16) % NUM_BUCKETS;
    }

    HashNode* create_node(){
        if(recycled_nodes != nullptr){
            HashNode* node = recycled_nodes;
            recycled_nodes = recycled_nodes->next;
            return node;
        }
        return static_cast<HashNode*>(arena->allocate_metadata(sizeof(HashNode)));

    }

    void put(void* ptr , size_t size , bool is_free , int alloc_type){
        size_t index = get_bucket_index(ptr);
        HashNode* current = buckets[index];

        while(current != nullptr){
            if(current->block_ptr == ptr){
                current->block_size = size;
                current->is_free = is_free;
                current->allocator_type = alloc_type;
                return;
            }
            current = current->next;
        }

        HashNode* new_node = create_node();
        new_node->block_ptr = ptr;
        new_node->block_size = size;
        new_node->is_free = is_free;
        new_node->allocator_type = alloc_type;
        new_node->next = buckets[index];
        buckets[index] = new_node;

    }

    HashNode* get(void* ptr){
        size_t index = get_bucket_index(ptr);
        HashNode* current = buckets[index];

        while(current != nullptr){
            if(current->block_ptr == ptr){
                return current;
            }
            current = current->next;
        }
        return nullptr;
    }

    bool remove(void* ptr){
        size_t index = get_bucket_index(ptr);
        HashNode* current = buckets[index];
        HashNode* prev = nullptr;

        while(current != nullptr){
            if(current->block_ptr == ptr){
                if(prev == nullptr){
                    buckets[index] = current->next;
                }
                else{
                    prev->next = current->next;
                }

                current->next = recycled_nodes;
                recycled_nodes = current;
                return true;
            }
            prev = current;
            current = current->next;
        }
        return false;
    }
};