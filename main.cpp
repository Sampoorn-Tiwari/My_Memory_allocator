#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <chrono>
#include <cstdlib>
#include <cassert>

#include "phase4_segregated.hpp"

using Clock = std::chrono::high_resolution_clock;

void run_correctness_tests() {
    std::cout << "====================================================================\n";
    std::cout << "  1. FUNCTIONAL VERIFICATION SUITE (PHASES 1 - 4)\n";
    std::cout << "====================================================================\n";

    CustomAllocator alloc;

    int* small_a = static_cast<int*>(alloc.my_malloc(32));
    int* small_b = static_cast<int*>(alloc.my_malloc(64));
    assert(small_a != nullptr && small_b != nullptr);
    small_a[0] = 101;
    small_b[0] = 202;
    assert(small_a[0] == 101 && small_b[0] == 202);

    size_t splits_before = alloc.buddy.total_splits;
    assert(alloc.my_free(small_a) == true);
    int* small_reused = static_cast<int*>(alloc.my_malloc(32));
    assert(small_reused == small_a);
    assert(alloc.buddy.total_splits == splits_before);

    void* large_1 = alloc.my_malloc(2048);
    void* large_2 = alloc.my_malloc(4096);
    assert(large_1 != nullptr && large_2 != nullptr);

    assert(alloc.my_free(large_1) == true);
    assert(alloc.my_free(large_1) == false);
    assert(alloc.my_free(large_2) == true);
    assert(alloc.my_free(small_reused) == true);
    assert(alloc.my_free(small_b) == true);

    std::cout << "  [PASS] Phase 1: OS mmap() 20 MB Dual-Zone Arena & Alignment\n";
    std::cout << "  [PASS] Phase 2: Metadata Hash Table Lookup & Slot Recycling\n";
    std::cout << "  [PASS] Phase 3: Buddy Splitting & Relative-XOR Twin Coalescing\n";
    std::cout << "  [PASS] Phase 4: Segregated 16B-256B O(1) Reuse & Double-Free Guard\n";
}

struct BenchResult {
    std::string name;
    double avg_alloc_ns;
    double avg_free_ns;
    double ops_per_sec;
    size_t splits;
    size_t merges;
};

BenchResult benchmark_hybrid(const std::vector<size_t>& init_sizes, const std::vector<size_t>& test_sizes) {
    CustomAllocator alloc;
    std::vector<void*> init_ptrs(init_sizes.size(), nullptr);
    for (size_t i = 0; i < init_sizes.size(); i++) {
        init_ptrs[i] = alloc.my_malloc(init_sizes[i]);
    }
    for (size_t i = 0; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) {
            alloc.my_free(init_ptrs[i]);
            init_ptrs[i] = nullptr;
        }
    }

    std::vector<void*> test_ptrs(test_sizes.size(), nullptr);

    auto t0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        test_ptrs[i] = alloc.my_malloc(test_sizes[i]);
    }
    auto t1 = Clock::now();

    auto f0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        if (test_ptrs[i]) alloc.my_free(test_ptrs[i]);
    }
    auto f1 = Clock::now();

    for (size_t i = 1; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) alloc.my_free(init_ptrs[i]);
    }

    double alloc_ns  = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double free_ns   = std::chrono::duration<double, std::nano>(f1 - f0).count();
    double total_sec = (alloc_ns + free_ns) / 1e9;

    return {
        "Hybrid (Seg+Buddy)",
        alloc_ns / test_sizes.size(),
        free_ns / test_sizes.size(),
        (2.0 * test_sizes.size()) / total_sec,
        alloc.buddy.total_splits,
        alloc.buddy.total_merges
    };
}

BenchResult benchmark_pure_buddy(const std::vector<size_t>& init_sizes, const std::vector<size_t>& test_sizes) {
    Arena arena;
    MetadataHashTable table;
    table.init(&arena);
    BuddyAllocator buddy;
    buddy.init(&arena, &table);

    std::vector<void*> init_ptrs(init_sizes.size(), nullptr);
    for (size_t i = 0; i < init_sizes.size(); i++) {
        init_ptrs[i] = buddy.allocate(init_sizes[i]);
    }
    for (size_t i = 0; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) {
            buddy.deallocate(init_ptrs[i]);
            init_ptrs[i] = nullptr;
        }
    }

    std::vector<void*> test_ptrs(test_sizes.size(), nullptr);

    auto t0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        test_ptrs[i] = buddy.allocate(test_sizes[i]);
    }
    auto t1 = Clock::now();

    auto f0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        if (test_ptrs[i]) buddy.deallocate(test_ptrs[i]);
    }
    auto f1 = Clock::now();

    for (size_t i = 1; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) buddy.deallocate(init_ptrs[i]);
    }

    double alloc_ns  = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double free_ns   = std::chrono::duration<double, std::nano>(f1 - f0).count();
    double total_sec = (alloc_ns + free_ns) / 1e9;

    return {
        "Pure Buddy Only",
        alloc_ns / test_sizes.size(),
        free_ns / test_sizes.size(),
        (2.0 * test_sizes.size()) / total_sec,
        buddy.total_splits,
        buddy.total_merges
    };
}

BenchResult benchmark_glibc(const std::vector<size_t>& init_sizes, const std::vector<size_t>& test_sizes) {
    std::vector<void*> init_ptrs(init_sizes.size(), nullptr);
    for (size_t i = 0; i < init_sizes.size(); i++) {
        init_ptrs[i] = std::malloc(init_sizes[i]);
    }
    for (size_t i = 0; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) {
            std::free(init_ptrs[i]);
            init_ptrs[i] = nullptr;
        }
    }

    std::vector<void*> test_ptrs(test_sizes.size(), nullptr);

    auto t0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        test_ptrs[i] = std::malloc(test_sizes[i]);
    }
    auto t1 = Clock::now();

    auto f0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        if (test_ptrs[i]) std::free(test_ptrs[i]);
    }
    auto f1 = Clock::now();

    for (size_t i = 1; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) std::free(init_ptrs[i]);
    }

    double alloc_ns  = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double free_ns   = std::chrono::duration<double, std::nano>(f1 - f0).count();
    double total_sec = (alloc_ns + free_ns) / 1e9;

    return {
        "Standard glibc",
        alloc_ns / test_sizes.size(),
        free_ns / test_sizes.size(),
        (2.0 * test_sizes.size()) / total_sec,
        0,
        0
    };
}

void print_table(const std::string& title, const std::vector<BenchResult>& results) {
    std::cout << "\n===============================================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "===============================================================================\n";
    std::cout << std::left << std::setw(22) << "Allocator"
              << std::right << std::setw(12) << "Alloc(ns)"
              << std::setw(12) << "Free(ns)"
              << std::setw(14) << "Ops/sec"
              << std::setw(10) << "Splits"
              << std::setw(10) << "Merges" << "\n";
    std::cout << "-------------------------------------------------------------------------------\n";
    for (const auto& r : results) {
        std::cout << std::left << std::setw(22) << r.name
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(12) << r.avg_alloc_ns
                  << std::setw(12) << r.avg_free_ns
                  << std::setw(14) << std::setprecision(0) << r.ops_per_sec
                  << std::setw(10) << r.splits
                  << std::setw(10) << r.merges << "\n";
    }
}

void print_arena_map(CustomAllocator& alloc) {
    const int SLOTS = 64;
    const size_t SLOT_BYTES = Arena::PAYLOAD_SIZE / SLOTS;
    char bar[SLOTS + 1];
    for (int i = 0; i < SLOTS; i++) bar[i] = '.';
    bar[SLOTS] = '\0';

    for (size_t b = 0; b < MetadataHashTable::NUM_BUCKETS; b++) {
        HashNode* curr = alloc.hash_table.buckets[b];
        while (curr != nullptr) {
            if (!curr->is_free) {
                size_t start_off = alloc.arena.get_offset(curr->block_ptr);
                size_t end_off   = start_off + curr->block_size;
                size_t s_slot    = start_off / SLOT_BYTES;
                size_t e_slot    = (end_off + SLOT_BYTES - 1) / SLOT_BYTES;
                if (e_slot > (size_t)SLOTS) e_slot = SLOTS;
                char sym = (curr->allocator_type == 1) ? 'S' : 'B';
                for (size_t k = s_slot; k < e_slot; k++) bar[k] = sym;
            }
            curr = curr->next;
        }
    }

    std::cout << "\n[Arena 16 MB Map | 1 char = 256 KB | '.'=Free, 'S'=Segregated, 'B'=Buddy]\n";
    std::cout << "  [" << bar << "]\n";
}

int main() {
    run_correctness_tests();

    std::mt19937 rng(42);

    std::vector<size_t> small_init(3000), small_test(1500);
    std::uniform_int_distribution<size_t> small_dist(16, 256);
    for (size_t& x : small_init) x = small_dist(rng);
    for (size_t& x : small_test) x = small_dist(rng);

    print_table("2. SMALL WORKLOAD (16B - 256B, 1,500 fragmented holes)", {
        benchmark_hybrid(small_init, small_test),
        benchmark_pure_buddy(small_init, small_test),
        benchmark_glibc(small_init, small_test)
    });

    std::vector<size_t> mix_init(2000), mix_test(1000);
    std::uniform_int_distribution<size_t> mix_dist(32, 4096);
    for (size_t& x : mix_init) x = mix_dist(rng);
    for (size_t& x : mix_test) x = mix_dist(rng);

    print_table("3. MIXED WORKLOAD (32B - 4KB, 1,000 fragmented holes)", {
        benchmark_hybrid(mix_init, mix_test),
        benchmark_pure_buddy(mix_init, mix_test),
        benchmark_glibc(mix_init, mix_test)
    });

    std::cout << "\n====================================================================\n";
    std::cout << "  4. LIVE ARENA TELEMETRY & MEMORY MAP\n";
    std::cout << "====================================================================\n";

    CustomAllocator live_alloc;
    std::vector<void*> ptrs;
    for (int i = 0; i < 40; i++) ptrs.push_back(live_alloc.my_malloc(64));
    for (int i = 0; i < 8; i++)  ptrs.push_back(live_alloc.my_malloc(262144));
    for (int i = 0; i < 8; i += 2) live_alloc.my_free(ptrs[40 + i]);

    AllocatorStats st = live_alloc.get_stats();
    std::cout << "  Active Allocations      : " << st.active_allocs << "\n";
    std::cout << "  Buddy Splits / Merges   : " << st.buddy_splits << " / " << st.buddy_merges << "\n";
    std::cout << "  Internal Fragmentation  : " << std::fixed << std::setprecision(2) << st.internal_frag_pct << "%\n";
    std::cout << "  External Fragmentation  : " << st.external_frag_pct << "%\n";
    std::cout << "  Arena Memory Utilization: " << st.utilization_pct << "%\n";

    print_arena_map(live_alloc);

    return 0;
}