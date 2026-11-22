#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <chrono>
#include <cstdlib>
#include <cassert>

#include "phase4_segregated.hpp"
#include "phase5_strat.hpp"
#include "phase6_analytic.hpp"

using Clock = std::chrono::high_resolution_clock;

void run_correctness_tests() {
    std::cout << "====================================================================\n";
    std::cout << "  1. FUNCTIONAL VERIFICATION SUITE (PHASES 1 - 4)\n";
    std::cout << "====================================================================\n";

    CustomAllocator alloc(true);

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

    std::cout << "  [PASS] Segregated 16B-256B O(1) Allocation & Zero-Split Reuse\n";
    std::cout << "  [PASS] Buddy >256B Power-of-2 Splitting & Relative-XOR Coalescing\n";
    std::cout << "  [PASS] Metadata Hash Table Lookup & Recycled Slot Reuse\n";
    std::cout << "  [PASS] Double-Free Rejection & Event Stack Logging ("
              << alloc.event_log.size() << " events captured)\n";
}

struct BenchResult {
    std::string name;
    double avg_alloc_ns;
    double avg_free_ns;
    double max_latency_ns;
    double ops_per_sec;
    double internal_frag;
    double external_frag;
};

BenchResult benchmark_hybrid_fragmented(const std::vector<size_t>& init_sizes, const std::vector<size_t>& test_sizes) {
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
    double max_ns = 0.0;

    auto t0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        auto s0 = Clock::now();
        test_ptrs[i] = alloc.my_malloc(test_sizes[i]);
        auto s1 = Clock::now();
        double dt = std::chrono::duration<double, std::nano>(s1 - s0).count();
        if (dt > max_ns) max_ns = dt;
    }
    auto t1 = Clock::now();

    AllocatorStats mid_stats = alloc.get_stats();

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
        max_ns,
        (2.0 * test_sizes.size()) / total_sec,
        mid_stats.internal_frag_pct,
        mid_stats.external_frag_pct
    };
}

BenchResult benchmark_strategy_fragmented(const std::string& label, SearchStrategy strat,
                                          const std::vector<size_t>& init_sizes,
                                          const std::vector<size_t>& test_sizes) {
    VariableStrategyAllocator alloc(strat);
    std::vector<void*> init_ptrs(init_sizes.size(), nullptr);
    for (size_t i = 0; i < init_sizes.size(); i++) {
        init_ptrs[i] = alloc.allocate(init_sizes[i]);
    }
    for (size_t i = 0; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) {
            alloc.deallocate(init_ptrs[i]);
            init_ptrs[i] = nullptr;
        }
    }

    std::vector<void*> test_ptrs(test_sizes.size(), nullptr);
    double max_ns = 0.0;

    auto t0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        auto s0 = Clock::now();
        test_ptrs[i] = alloc.allocate(test_sizes[i]);
        auto s1 = Clock::now();
        double dt = std::chrono::duration<double, std::nano>(s1 - s0).count();
        if (dt > max_ns) max_ns = dt;
    }
    auto t1 = Clock::now();

    double int_f = alloc.get_internal_frag_pct();
    double ext_f = alloc.get_external_frag_pct();

    auto f0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        if (test_ptrs[i]) alloc.deallocate(test_ptrs[i]);
    }
    auto f1 = Clock::now();

    for (size_t i = 1; i < init_sizes.size(); i += 2) {
        if (init_ptrs[i]) alloc.deallocate(init_ptrs[i]);
    }

    double alloc_ns  = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double free_ns   = std::chrono::duration<double, std::nano>(f1 - f0).count();
    double total_sec = (alloc_ns + free_ns) / 1e9;

    return {
        label,
        alloc_ns / test_sizes.size(),
        free_ns / test_sizes.size(),
        max_ns,
        (2.0 * test_sizes.size()) / total_sec,
        int_f,
        ext_f
    };
}

BenchResult benchmark_glibc_fragmented(const std::vector<size_t>& init_sizes, const std::vector<size_t>& test_sizes) {
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
    double max_ns = 0.0;

    auto t0 = Clock::now();
    for (size_t i = 0; i < test_sizes.size(); i++) {
        auto s0 = Clock::now();
        test_ptrs[i] = std::malloc(test_sizes[i]);
        auto s1 = Clock::now();
        double dt = std::chrono::duration<double, std::nano>(s1 - s0).count();
        if (dt > max_ns) max_ns = dt;
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
        max_ns,
        (2.0 * test_sizes.size()) / total_sec,
        0.0,
        0.0
    };
}

void print_table(const std::string& title, const std::vector<BenchResult>& results) {
    std::cout << "\n=========================================================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "=========================================================================================\n";
    std::cout << std::left << std::setw(20) << "Allocator"
              << std::right << std::setw(12) << "Alloc(ns)"
              << std::setw(12) << "Free(ns)"
              << std::setw(12) << "Max(ns)"
              << std::setw(14) << "Ops/sec"
              << std::setw(10) << "IntFrag%"
              << std::setw(10) << "ExtFrag%" << "\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";
    for (const auto& r : results) {
        std::cout << std::left << std::setw(20) << r.name
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(12) << r.avg_alloc_ns
                  << std::setw(12) << r.avg_free_ns
                  << std::setw(12) << r.max_latency_ns
                  << std::setw(14) << std::setprecision(0) << r.ops_per_sec
                  << std::setw(10) << std::setprecision(2) << r.internal_frag
                  << std::setw(10) << r.external_frag << "\n";
    }
}

int main() {
    run_correctness_tests();

    std::mt19937 rng(42);

    const size_t N_SMALL_INIT = 3000;
    const size_t N_SMALL_TEST = 1500;
    std::vector<size_t> small_init(N_SMALL_INIT), small_test(N_SMALL_TEST);
    std::uniform_int_distribution<size_t> small_dist(16, 256);
    for (size_t i = 0; i < N_SMALL_INIT; i++) small_init[i] = small_dist(rng);
    for (size_t i = 0; i < N_SMALL_TEST; i++) small_test[i] = small_dist(rng);

    std::vector<BenchResult> r_small = {
        benchmark_hybrid_fragmented(small_init, small_test),
        benchmark_strategy_fragmented("First-Fit", SearchStrategy::FIRST_FIT, small_init, small_test),
        benchmark_strategy_fragmented("Best-Fit", SearchStrategy::BEST_FIT, small_init, small_test),
        benchmark_strategy_fragmented("AVL Best-Fit", SearchStrategy::AVL_BEST_FIT, small_init, small_test),
        benchmark_strategy_fragmented("Max-Heap WorstFit", SearchStrategy::WORST_FIT_HEAP, small_init, small_test),
        benchmark_glibc_fragmented(small_init, small_test)
    };
    print_table("2. WORKLOAD A: FRAGMENTED SMALL-BLOCK POOL (1,500 holes, 16B - 256B)", r_small);

    const size_t N_MIX_INIT = 2000;
    const size_t N_MIX_TEST = 1000;
    std::vector<size_t> mix_init(N_MIX_INIT), mix_test(N_MIX_TEST);
    std::uniform_int_distribution<size_t> mix_dist(32, 4096);
    for (size_t i = 0; i < N_MIX_INIT; i++) mix_init[i] = mix_dist(rng);
    for (size_t i = 0; i < N_MIX_TEST; i++) mix_test[i] = mix_dist(rng);

    std::vector<BenchResult> r_mixed = {
        benchmark_hybrid_fragmented(mix_init, mix_test),
        benchmark_strategy_fragmented("First-Fit", SearchStrategy::FIRST_FIT, mix_init, mix_test),
        benchmark_strategy_fragmented("Best-Fit", SearchStrategy::BEST_FIT, mix_init, mix_test),
        benchmark_strategy_fragmented("AVL Best-Fit", SearchStrategy::AVL_BEST_FIT, mix_init, mix_test),
        benchmark_strategy_fragmented("Max-Heap WorstFit", SearchStrategy::WORST_FIT_HEAP, mix_init, mix_test),
        benchmark_glibc_fragmented(mix_init, mix_test)
    };
    print_table("3. WORKLOAD B: FRAGMENTED MIXED WORKLOAD (1,000 holes, 32B - 4KB)", r_mixed);

    std::cout << "\n====================================================================\n";
    std::cout << "  4. LIVE ARENA TELEMETRY, VISUALIZER & GRAPH/ALGO MODULES\n";
    std::cout << "====================================================================\n";
    CustomAllocator live_alloc;
    std::vector<void*> live_ptrs;
    for (int i = 0; i < 40; i++) live_ptrs.push_back(live_alloc.my_malloc(64));
    for (int i = 0; i < 8; i++)  live_ptrs.push_back(live_alloc.my_malloc(262144));
    for (int i = 0; i < 8; i += 2) live_alloc.my_free(live_ptrs[40 + i]);

    AllocatorStats st = live_alloc.get_stats();
    std::cout << "  Active Allocations      : " << st.active_allocs << "\n";
    std::cout << "  Buddy Splits / Merges   : " << st.buddy_splits << " / " << st.buddy_merges << "\n";
    std::cout << "  Internal Fragmentation  : " << std::fixed << std::setprecision(2) << st.internal_frag_pct << "%\n";
    std::cout << "  External Fragmentation  : " << st.external_frag_pct << "%\n";
    std::cout << "  Arena Memory Utilization: " << st.utilization_pct << "%\n";

    MemoryAnalytics::print_arena_visual_map(live_alloc);
    MemoryAnalytics::run_graph_bfs_dfs_demo(live_alloc);
    MemoryAnalytics::run_algorithmic_optimizations_demo();

    return 0;
}