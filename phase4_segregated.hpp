#pragma once
#include "phase1_arena.hpp"
#include "phase2_metabase.hpp"
#include "phase3_buddy.hpp"

struct SmallFreeNode {
    SmallFreeNode* next;
};

class SegregatedAllocator {
public:
    static const int    NUM_CLASSES = 5;
    static const size_t MAX_SMALL   = 256;
    static const size_t SLAB_SIZE   = 4096;

    Arena*             arena;
    MetadataHashTable* hash_table;
    BuddyAllocator*    buddy;

    SmallFreeNode* free_lists[NUM_CLASSES];
    size_t         free_counts[NUM_CLASSES];
    size_t         slabs_acquired[NUM_CLASSES];

    void init(Arena* arena_ptr, MetadataHashTable* table_ptr, BuddyAllocator* buddy_ptr) {
        arena      = arena_ptr;
        hash_table = table_ptr;
        buddy      = buddy_ptr;

        for (int i = 0; i < NUM_CLASSES; i++) {
            free_lists[i]     = nullptr;
            free_counts[i]    = 0;
            slabs_acquired[i] = 0;
        }
    }

    bool refill_class(int class_idx) {
        void* slab = buddy->allocate(SLAB_SIZE);
        if (slab == nullptr) {
            return false;
        }

        hash_table->remove(slab);

        size_t block_size = (size_t(16) << class_idx);
        size_t num_blocks = SLAB_SIZE / block_size;
        uint8_t* base     = static_cast<uint8_t*>(slab);

        for (size_t i = 0; i < num_blocks; i++) {
            uint8_t* chunk      = base + (i * block_size);
            SmallFreeNode* node = reinterpret_cast<SmallFreeNode*>(chunk);
            node->next          = free_lists[class_idx];
            free_lists[class_idx] = node;
            free_counts[class_idx]++;
        }

        slabs_acquired[class_idx]++;
        return true;
    }

    void* allocate(size_t requested_size) {
        if (requested_size == 0 || requested_size > MAX_SMALL) {
            return nullptr;
        }

        size_t block_size = next_power_of_two(requested_size);
        int class_idx     = get_size_index(block_size);

        if (free_lists[class_idx] == nullptr) {
            if (!refill_class(class_idx)) {
                return nullptr;
            }
        }

        SmallFreeNode* node   = free_lists[class_idx];
        free_lists[class_idx] = node->next;
        free_counts[class_idx]--;

        hash_table->put(node, block_size, false, 1);
        return node;
    }

    bool deallocate(void* ptr, size_t block_size) {
        int class_idx = get_size_index(block_size);
        if (class_idx < 0 || class_idx >= NUM_CLASSES) {
            return false;
        }

        SmallFreeNode* node   = static_cast<SmallFreeNode*>(ptr);
        node->next            = free_lists[class_idx];
        free_lists[class_idx] = node;
        free_counts[class_idx]++;

        hash_table->remove(ptr);
        return true;
    }

    size_t get_total_free_bytes() {
        size_t total = 0;
        for (int i = 0; i < NUM_CLASSES; i++) {
            total += free_counts[i] * (size_t(16) << i);
        }
        return total;
    }
};

struct AllocatorStats {
    size_t total_allocs;
    size_t total_frees;
    size_t active_allocs;
    size_t current_allocated_bytes;
    size_t total_free_bytes;
    size_t largest_free_block;
    size_t buddy_splits;
    size_t buddy_merges;
    double internal_frag_pct;
    double external_frag_pct;
    double utilization_pct;
};

class CustomAllocator {
public:
    Arena               arena;
    MetadataHashTable   hash_table;
    BuddyAllocator      buddy;
    SegregatedAllocator segregated;

    size_t total_allocs;
    size_t total_frees;
    size_t active_allocs;
    size_t total_requested_bytes;
    size_t total_allocated_bytes;
    size_t current_allocated_bytes;

    CustomAllocator() {
        hash_table.init(&arena);
        buddy.init(&arena, &hash_table);
        segregated.init(&arena, &hash_table, &buddy);

        total_allocs            = 0;
        total_frees             = 0;
        active_allocs           = 0;
        total_requested_bytes   = 0;
        total_allocated_bytes   = 0;
        current_allocated_bytes = 0;
    }

    void* my_malloc(size_t size) {
        if (size == 0 || size > Arena::PAYLOAD_SIZE) {
            return nullptr;
        }

        void* ptr = nullptr;
        size_t actual_size = next_power_of_two(size);

        if (size <= SegregatedAllocator::MAX_SMALL) {
            ptr = segregated.allocate(size);
        } else {
            ptr = buddy.allocate(size);
        }

        if (ptr != nullptr) {
            total_allocs++;
            active_allocs++;
            total_requested_bytes   += size;
            total_allocated_bytes   += actual_size;
            current_allocated_bytes += actual_size;
        }
        return ptr;
    }

    bool my_free(void* ptr) {
        if (ptr == nullptr || !arena.is_valid_playload_ptr(ptr)) {
            return false;
        }

        HashNode* info = hash_table.get(ptr);
        if (info == nullptr || info->is_free == true) {
            return false;
        }

        size_t block_size = info->block_size;
        int    type       = info->allocator_type;

        bool ok = false;
        if (type == 1) {
            ok = segregated.deallocate(ptr, block_size);
        } else if (type == 2) {
            ok = buddy.deallocate(ptr);
        }

        if (ok) {
            total_frees++;
            if (active_allocs > 0) active_allocs--;
            if (current_allocated_bytes >= block_size) current_allocated_bytes -= block_size;
        }
        return ok;
    }

    size_t get_buddy_free_bytes() {
        size_t total = 0;
        for (int i = 0; i < BuddyAllocator::MAX_LEVELS; i++) {
            FreeBlock* curr = buddy.free_lists[i];
            size_t sz = (size_t(16) << i);
            while (curr != nullptr) {
                total += sz;
                curr = curr->next;
            }
        }
        return total;
    }

    size_t get_buddy_largest_free() {
        for (int i = BuddyAllocator::MAX_LEVELS - 1; i >= 0; i--) {
            if (buddy.free_lists[i] != nullptr) {
                return (size_t(16) << i);
            }
        }
        return 0;
    }

    AllocatorStats get_stats() {
        AllocatorStats s{};
        s.total_allocs            = total_allocs;
        s.total_frees             = total_frees;
        s.active_allocs           = active_allocs;
        s.current_allocated_bytes = current_allocated_bytes;
        s.total_free_bytes        = get_buddy_free_bytes() + segregated.get_total_free_bytes();
        s.largest_free_block      = get_buddy_largest_free();
        s.buddy_splits            = buddy.total_splits;
        s.buddy_merges            = buddy.total_merges;

        if (total_allocated_bytes > 0) {
            s.internal_frag_pct = 100.0 * (double)(total_allocated_bytes - total_requested_bytes)
                                  / (double)total_allocated_bytes;
        } else {
            s.internal_frag_pct = 0.0;
        }

        if (s.total_free_bytes > 0 && s.largest_free_block <= s.total_free_bytes) {
            s.external_frag_pct = 100.0 * (1.0 - (double)s.largest_free_block / (double)s.total_free_bytes);
        } else {
            s.external_frag_pct = 0.0;
        }

        s.utilization_pct = 100.0 * (double)s.current_allocated_bytes / (double)Arena::PAYLOAD_SIZE;
        return s;
    }
};