#pragma once
#include "phase1_arena.hpp"
#include <algorithm>

struct VarBlock {
    uint8_t*  ptr;
    size_t    size;
    size_t    requested_size;
    bool      is_free;
    VarBlock* phys_prev;
    VarBlock* phys_next;
    VarBlock* free_prev;
    VarBlock* free_next;
};

struct AVLNode {
    size_t    block_size;
    VarBlock* block;
    int       height;
    AVLNode*  left;
    AVLNode*  right;
};

class FreeBlockAVLTree {
public:
    Arena*   arena;
    AVLNode* root;
    AVLNode* recycled;

    void init(Arena* arena_ptr) {
        arena    = arena_ptr;
        root     = nullptr;
        recycled = nullptr;
    }

    int get_height(AVLNode* n) {
        return n ? n->height : 0;
    }

    int get_balance(AVLNode* n) {
        return n ? (get_height(n->left) - get_height(n->right)) : 0;
    }

    void update_height(AVLNode* n) {
        int lh = get_height(n->left);
        int rh = get_height(n->right);
        n->height = 1 + (lh > rh ? lh : rh);
    }

    AVLNode* create_node(VarBlock* b) {
        AVLNode* n = nullptr;
        if (recycled != nullptr) {
            n        = recycled;
            recycled = recycled->left;
        } else {
            n = static_cast<AVLNode*>(arena->allocate_metadata(sizeof(AVLNode)));
        }
        n->block_size = b->size;
        n->block      = b;
        n->height     = 1;
        n->left       = nullptr;
        n->right      = nullptr;
        return n;
    }

    void recycle_node(AVLNode* n) {
        n->left  = recycled;
        n->right = nullptr;
        recycled = n;
    }

    bool less_key(size_t s1, VarBlock* b1, size_t s2, VarBlock* b2) {
        if (s1 != s2) return s1 < s2;
        return b1->ptr < b2->ptr;
    }

    AVLNode* rotate_right(AVLNode* y) {
        AVLNode* x  = y->left;
        AVLNode* t2 = x->right;
        x->right = y;
        y->left  = t2;
        update_height(y);
        update_height(x);
        return x;
    }

    AVLNode* rotate_left(AVLNode* x) {
        AVLNode* y  = x->right;
        AVLNode* t2 = y->left;
        y->left  = x;
        x->right = t2;
        update_height(x);
        update_height(y);
        return y;
    }

    AVLNode* balance_node(AVLNode* node) {
        update_height(node);
        int bf = get_balance(node);

        if (bf > 1) {
            if (get_balance(node->left) < 0) {
                node->left = rotate_left(node->left);
            }
            return rotate_right(node);
        }
        if (bf < -1) {
            if (get_balance(node->right) > 0) {
                node->right = rotate_right(node->right);
            }
            return rotate_left(node);
        }
        return node;
    }

    AVLNode* insert_rec(AVLNode* node, VarBlock* b) {
        if (node == nullptr) return create_node(b);
        if (less_key(b->size, b, node->block_size, node->block)) {
            node->left = insert_rec(node->left, b);
        } else {
            node->right = insert_rec(node->right, b);
        }
        return balance_node(node);
    }

    AVLNode* min_node(AVLNode* node) {
        while (node->left != nullptr) node = node->left;
        return node;
    }

    AVLNode* remove_rec(AVLNode* node, size_t sz, VarBlock* b) {
        if (node == nullptr) return nullptr;

        if (less_key(sz, b, node->block_size, node->block)) {
            node->left = remove_rec(node->left, sz, b);
        } else if (less_key(node->block_size, node->block, sz, b)) {
            node->right = remove_rec(node->right, sz, b);
        } else {
            if (node->left == nullptr || node->right == nullptr) {
                AVLNode* child = node->left ? node->left : node->right;
                recycle_node(node);
                return child;
            }
            AVLNode* succ    = min_node(node->right);
            node->block_size = succ->block_size;
            node->block      = succ->block;
            node->right      = remove_rec(node->right, succ->block_size, succ->block);
        }
        return balance_node(node);
    }

    void insert(VarBlock* b) {
        root = insert_rec(root, b);
    }

    void remove(VarBlock* b) {
        root = remove_rec(root, b->size, b);
    }

    VarBlock* find_best_fit(size_t required_size) {
        AVLNode* curr  = root;
        VarBlock* best = nullptr;
        while (curr != nullptr) {
            if (curr->block_size >= required_size) {
                best = curr->block;
                curr = curr->left;
            } else {
                curr = curr->right;
            }
        }
        return best;
    }
};

class MaxHeapFreeBlocks {
public:
    static const size_t MAX_HEAP_CAP = 8192;
    VarBlock** heap_arr;
    size_t     count;

    void init(Arena* arena) {
        heap_arr = static_cast<VarBlock**>(arena->allocate_metadata(sizeof(VarBlock*) * MAX_HEAP_CAP));
        count    = 0;
    }

    void push(VarBlock* b) {
        if (count >= MAX_HEAP_CAP) return;
        size_t i = count++;
        heap_arr[i] = b;
        while (i > 0) {
            size_t p = (i - 1) / 2;
            if (heap_arr[p]->size < heap_arr[i]->size) {
                std::swap(heap_arr[p], heap_arr[i]);
                i = p;
            } else {
                break;
            }
        }
    }

    VarBlock* pop_largest_valid(size_t required_size) {
        while (count > 0) {
            VarBlock* top = heap_arr[0];
            heap_arr[0]   = heap_arr[--count];

            size_t i = 0;
            while (true) {
                size_t l = 2 * i + 1;
                size_t r = 2 * i + 2;
                size_t largest = i;
                if (l < count && heap_arr[l]->size > heap_arr[largest]->size) largest = l;
                if (r < count && heap_arr[r]->size > heap_arr[largest]->size) largest = r;
                if (largest != i) {
                    std::swap(heap_arr[i], heap_arr[largest]);
                    i = largest;
                } else {
                    break;
                }
            }

            if (top->is_free && top->size >= required_size) {
                return top;
            }
            if (top->is_free && top->size < required_size) {
                push(top);
                return nullptr;
            }
        }
        return nullptr;
    }
};

enum class SearchStrategy {
    FIRST_FIT,
    BEST_FIT,
    AVL_BEST_FIT,
    WORST_FIT_HEAP
};

struct BlockHashEntry {
    void*           ptr;
    VarBlock*       block;
    BlockHashEntry* next;
};

class VariableStrategyAllocator {
public:
    static const size_t MAP_BUCKETS = 1024;

    Arena             arena;
    SearchStrategy    strategy;
    VarBlock*         phys_head;
    VarBlock*         free_head;
    VarBlock*         recycled_blocks;
    FreeBlockAVLTree  avl_tree;
    MaxHeapFreeBlocks max_heap;

    BlockHashEntry*   block_map[MAP_BUCKETS];
    BlockHashEntry*   recycled_map;

    size_t total_allocs;
    size_t total_frees;
    size_t total_requested_bytes;
    size_t total_allocated_bytes;
    size_t total_splits;
    size_t total_merges;

    VariableStrategyAllocator(SearchStrategy strat) : strategy(strat) {
        phys_head             = nullptr;
        free_head             = nullptr;
        recycled_blocks       = nullptr;
        recycled_map          = nullptr;
        total_allocs          = 0;
        total_frees           = 0;
        total_requested_bytes = 0;
        total_allocated_bytes = 0;
        total_splits          = 0;
        total_merges          = 0;

        for (size_t i = 0; i < MAP_BUCKETS; i++) {
            block_map[i] = nullptr;
        }

        avl_tree.init(&arena);
        max_heap.init(&arena);

        VarBlock* initial       = alloc_var_block();
        initial->ptr            = arena.arena_base;
        initial->size           = Arena::PAYLOAD_SIZE;
        initial->requested_size = 0;
        initial->is_free        = true;
        initial->phys_prev      = nullptr;
        initial->phys_next      = nullptr;
        initial->free_prev      = nullptr;
        initial->free_next      = nullptr;

        phys_head = initial;
        add_to_free_index(initial);
    }

    VarBlock* alloc_var_block() {
        if (recycled_blocks != nullptr) {
            VarBlock* b     = recycled_blocks;
            recycled_blocks = recycled_blocks->free_next;
            return b;
        }
        return static_cast<VarBlock*>(arena.allocate_metadata(sizeof(VarBlock)));
    }

    void recycle_var_block(VarBlock* b) {
        b->free_next    = recycled_blocks;
        recycled_blocks = b;
    }

    size_t map_bucket(void* ptr) {
        return (arena.get_offset(ptr) / 16) % MAP_BUCKETS;
    }

    void map_put(void* ptr, VarBlock* b) {
        size_t idx = map_bucket(ptr);
        BlockHashEntry* e = nullptr;
        if (recycled_map != nullptr) {
            e            = recycled_map;
            recycled_map = recycled_map->next;
        } else {
            e = static_cast<BlockHashEntry*>(arena.allocate_metadata(sizeof(BlockHashEntry)));
        }
        e->ptr         = ptr;
        e->block       = b;
        e->next        = block_map[idx];
        block_map[idx] = e;
    }

    VarBlock* map_remove(void* ptr) {
        size_t idx = map_bucket(ptr);
        BlockHashEntry* curr = block_map[idx];
        BlockHashEntry* prev = nullptr;

        while (curr != nullptr) {
            if (curr->ptr == ptr) {
                if (prev == nullptr) block_map[idx] = curr->next;
                else prev->next = curr->next;

                VarBlock* b  = curr->block;
                curr->next   = recycled_map;
                recycled_map = curr;
                return b;
            }
            prev = curr;
            curr = curr->next;
        }
        return nullptr;
    }

    void add_to_free_index(VarBlock* b) {
        b->is_free   = true;
        b->free_prev = nullptr;
        b->free_next = free_head;
        if (free_head != nullptr) free_head->free_prev = b;
        free_head = b;

        if (strategy == SearchStrategy::AVL_BEST_FIT) {
            avl_tree.insert(b);
        } else if (strategy == SearchStrategy::WORST_FIT_HEAP) {
            max_heap.push(b);
        }
    }

    void remove_from_free_index(VarBlock* b) {
        if (b->free_prev != nullptr) b->free_prev->free_next = b->free_next;
        else free_head = b->free_next;

        if (b->free_next != nullptr) b->free_next->free_prev = b->free_prev;
        b->free_prev = nullptr;
        b->free_next = nullptr;

        if (strategy == SearchStrategy::AVL_BEST_FIT) {
            avl_tree.remove(b);
        }
        b->is_free = false;
    }

    void* allocate(size_t requested_size) {
        if (requested_size == 0 || requested_size > Arena::PAYLOAD_SIZE) return nullptr;
        size_t aligned = align_to_16(requested_size);

        VarBlock* chosen = nullptr;

        if (strategy == SearchStrategy::FIRST_FIT) {
            VarBlock* curr = free_head;
            while (curr != nullptr) {
                if (curr->size >= aligned) {
                    chosen = curr;
                    break;
                }
                curr = curr->free_next;
            }
        } else if (strategy == SearchStrategy::BEST_FIT) {
            VarBlock* curr = free_head;
            while (curr != nullptr) {
                if (curr->size >= aligned) {
                    if (chosen == nullptr || curr->size < chosen->size) {
                        chosen = curr;
                        if (chosen->size == aligned) break;
                    }
                }
                curr = curr->free_next;
            }
        } else if (strategy == SearchStrategy::AVL_BEST_FIT) {
            chosen = avl_tree.find_best_fit(aligned);
        } else if (strategy == SearchStrategy::WORST_FIT_HEAP) {
            chosen = max_heap.pop_largest_valid(aligned);
        }

        if (chosen == nullptr) return nullptr;

        remove_from_free_index(chosen);

        if (chosen->size >= aligned + 16) {
            total_splits++;
            VarBlock* rem       = alloc_var_block();
            rem->ptr            = chosen->ptr + aligned;
            rem->size           = chosen->size - aligned;
            rem->requested_size = 0;
            rem->is_free        = true;

            rem->phys_prev = chosen;
            rem->phys_next = chosen->phys_next;
            if (chosen->phys_next != nullptr) {
                chosen->phys_next->phys_prev = rem;
            }
            chosen->phys_next = rem;
            chosen->size      = aligned;

            add_to_free_index(rem);
        }

        chosen->requested_size = requested_size;
        map_put(chosen->ptr, chosen);

        total_allocs++;
        total_requested_bytes += requested_size;
        total_allocated_bytes += chosen->size;
        return chosen->ptr;
    }

    bool deallocate(void* ptr) {
        if (ptr == nullptr || !arena.is_valid_playload_ptr(ptr)) return false;

        VarBlock* b = map_remove(ptr);
        if (b == nullptr || b->is_free) return false;

        total_frees++;
        b->requested_size = 0;

        if (b->phys_next != nullptr && b->phys_next->is_free) {
            total_merges++;
            VarBlock* nxt = b->phys_next;
            remove_from_free_index(nxt);
            b->size     += nxt->size;
            b->phys_next = nxt->phys_next;
            if (nxt->phys_next != nullptr) nxt->phys_next->phys_prev = b;
            recycle_var_block(nxt);
        }

        if (b->phys_prev != nullptr && b->phys_prev->is_free) {
            total_merges++;
            VarBlock* prv = b->phys_prev;
            remove_from_free_index(prv);
            prv->size     += b->size;
            prv->phys_next = b->phys_next;
            if (b->phys_next != nullptr) b->phys_next->phys_prev = prv;
            recycle_var_block(b);
            b = prv;
        }

        add_to_free_index(b);
        return true;
    }

    double get_external_frag_pct() {
        size_t total_free = 0;
        size_t max_free   = 0;
        VarBlock* curr = free_head;
        while (curr != nullptr) {
            total_free += curr->size;
            if (curr->size > max_free) max_free = curr->size;
            curr = curr->free_next;
        }
        if (total_free == 0) return 0.0;
        return 100.0 * (1.0 - (double)max_free / (double)total_free);
    }

    double get_internal_frag_pct() {
        if (total_allocated_bytes == 0) return 0.0;
        return 100.0 * (double)(total_allocated_bytes - total_requested_bytes)
               / (double)total_allocated_bytes;
    }
};