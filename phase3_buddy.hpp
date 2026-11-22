#pragma once
#include "phase1_arena.hpp"
#include "phase2_metabase.hpp"

struct FreeBlock{

    FreeBlock* next;
    FreeBlock* prev;

};


class BuddyAllocator {

    public:

    static const int MAX_LEVELS = 21;

    Arena* arena;
    MetadataHashTable* hash_table;
    FreeBlock* free_lists[MAX_LEVELS];

    size_t total_splits;
    size_t total_merges;

    void init(Arena* arena_ptr , MetadataHashTable* table_ptr){

        arena = arena_ptr;
        hash_table = table_ptr;
        total_merges = 0;
        total_splits = 0;

        for(int i = 0 ; i < MAX_LEVELS ; i++) free_lists[i] = nullptr;

        void* initial_block = arena->arena_base;
        push_free_block(initial_block , Arena::PAYLOAD_SIZE);

        hash_table->put(initial_block , Arena::PAYLOAD_SIZE , true , 2);

    }

    void push_free_block(void* ptr , size_t size){
        int index = get_size_index(size);
        FreeBlock* block = static_cast<FreeBlock*>(ptr);

        block->prev = nullptr;
        block->next = free_lists[index];

        if(free_lists[index] != nullptr){
            free_lists[index]->prev = block;
        }
        free_lists[index] = block;
    }

    void remove_free_block(void* ptr , size_t size){
        int index = get_size_index(size);
        FreeBlock* block = static_cast<FreeBlock*>(ptr);

        if(block->prev != nullptr){
            block->prev->next = block->next;
        }
        else{
        free_lists[index] = block->next;
        }

        if(block->next != nullptr){
            block->next->prev = block->prev;
        }

        block->next = nullptr;
        block->prev = nullptr;
    }

    void* allocate(size_t requested_size) {
        if (requested_size == 0 || requested_size > Arena::PAYLOAD_SIZE) {
            return nullptr;
        }

        // Step 1: Round request up to a power of 2 (minimum 16 bytes)
        size_t target_size = next_power_of_two(requested_size);
        int target_index   = get_size_index(target_size);

        // Step 2: Find the smallest non-empty shelf >= target_index
        int found_index = -1;
        for (int i = target_index; i < MAX_LEVELS; i++) {
            if (free_lists[i] != nullptr) {
                found_index = i;
                break;
            }
        }

        // No block big enough available in the Warehouse!
        if (found_index == -1) {
            return nullptr;
        }

        // Step 3: Take the free block off the shelf we found
        FreeBlock* block_to_use = free_lists[found_index];
        size_t current_size     = (size_t(16) << found_index); // Size at found_index
        remove_free_block(block_to_use, current_size);

        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block_to_use);

        // Step 4: If the block is bigger than what the user asked for, SPLIT IT!
        while (current_size > target_size) {
            total_splits++;
            current_size = current_size / 2; // Snap in half!

            // The Right Twin sits at (block_ptr + current_size)
            uint8_t* right_twin = block_ptr + current_size;

            // Put the Right Twin onto its shelf and mark it FREE in Hash Table
            push_free_block(right_twin, current_size);
            hash_table->put(right_twin, current_size, true, 2);
        }

        // Step 5: Mark our final Left Block as ALLOCATED (is_free = false) in Hash Table!
        hash_table->put(block_ptr, target_size, false, 2);
        return block_ptr;
    }

    // ========================================================================
    // CORE OPERATION 2: DEALLOCATE A BLOCK (Free & Merge Twins)
    // ========================================================================
    bool deallocate(void* ptr) {
        if (ptr == nullptr || !arena->is_valid_playload_ptr(ptr)) {
            return false;
        }

        // Step 1: Look up this block's ID card in our Phase 2 Hash Table!
        HashNode* info = hash_table->get(ptr);
        if (info == nullptr || info->is_free == true) {
            return false; // Invalid pointer or Double-Free!
        }

        size_t current_size  = info->block_size;
        uint8_t* current_ptr = static_cast<uint8_t*>(ptr);

        // Step 2: Keep merging with our Twin as long as our Twin is also FREE!
        while (current_size < Arena::PAYLOAD_SIZE) {
            // Find our twin's address using Phase 1's XOR formula
            uint8_t* buddy_ptr = arena->get_buddy(current_ptr, current_size);

            // Look up our twin in the Phase 2 Hash Table
            HashNode* buddy_info = hash_table->get(buddy_ptr);

            // Can we merge? ONLY if the twin exists, is FREE, and has the SAME size!
            if (buddy_info == nullptr || !buddy_info->is_free || buddy_info->block_size != current_size) {
                break; // Twin is busy (or split into smaller pieces) -> stop merging!
            }

            // Twin is FREE! Let's merge them together:
            total_merges++;

            // A. Take the twin off its current free shelf
            remove_free_block(buddy_ptr, current_size);

            // B. Remove both old half-block cards from the Hash Table
            hash_table->remove(current_ptr);
            hash_table->remove(buddy_ptr);

            // C. The merged parent starts at whichever twin has the smaller address
            if (buddy_ptr < current_ptr) {
                current_ptr = buddy_ptr;
            }

            // D. Double the block size!
            current_size = current_size * 2;
        }

        // Step 3: Put the final merged block onto its shelf and record as FREE!
        push_free_block(current_ptr, current_size);
        hash_table->put(current_ptr, current_size, true, 2);
        return true;
    }
};