#pragma once
#include "phase4_segregated.hpp"
#include "phase5_strat.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <queue>
#include <stack>
#include <algorithm>

class MemoryAnalytics {
public:
    static void print_arena_visual_map(CustomAllocator& alloc) {
        const int SLOTS = 64;
        const size_t SLOT_BYTES = Arena::PAYLOAD_SIZE / SLOTS;
        char map_bar[SLOTS + 1];
        for (int i = 0; i < SLOTS; i++) map_bar[i] = '.';
        map_bar[SLOTS] = '\0';

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
                    for (size_t k = s_slot; k < e_slot; k++) {
                        map_bar[k] = sym;
                    }
                }
                curr = curr->next;
            }
        }

        std::cout << "\n[Arena 16 MB Visual Map | 1 char = 256 KB | '.'=Free, 'S'=Segregated, 'B'=Buddy]\n";
        std::cout << "  [" << map_bar << "]\n";
    }

    static void run_graph_bfs_dfs_demo(CustomAllocator& alloc) {
        struct BlockNode {
            size_t offset;
            size_t size;
            bool   is_free;
        };

        std::vector<BlockNode> nodes;
        for (size_t b = 0; b < MetadataHashTable::NUM_BUCKETS; b++) {
            HashNode* curr = alloc.hash_table.buckets[b];
            while (curr != nullptr) {
                if (curr->allocator_type == 2) {
                    nodes.push_back({alloc.arena.get_offset(curr->block_ptr), curr->block_size, curr->is_free});
                }
                curr = curr->next;
            }
        }

        std::sort(nodes.begin(), nodes.end(), [](const BlockNode& a, const BlockNode& b) {
            return a.offset < b.offset;
        });

        int n = (int)nodes.size();
        if (n == 0) return;

        std::vector<std::vector<int>> adj(n);
        for (int i = 0; i < n; i++) {
            if (i + 1 < n && nodes[i].offset + nodes[i].size == nodes[i + 1].offset) {
                adj[i].push_back(i + 1);
                adj[i + 1].push_back(i);
            }
            size_t buddy_off = nodes[i].offset ^ nodes[i].size;
            for (int j = i + 1; j < n; j++) {
                if (nodes[j].offset == buddy_off && nodes[j].size == nodes[i].size) {
                    adj[i].push_back(j);
                    adj[j].push_back(i);
                }
            }
        }

        std::vector<bool> vis_bfs(n, false);
        std::queue<int> q;
        q.push(0);
        vis_bfs[0] = true;
        int bfs_count     = 0;
        int free_clusters = 0;

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            bfs_count++;
            if (nodes[u].is_free) free_clusters++;
            for (int v : adj[u]) {
                if (!vis_bfs[v]) {
                    vis_bfs[v] = true;
                    q.push(v);
                }
            }
        }

        std::vector<bool> vis_dfs(n, false);
        std::stack<int> st;
        st.push(0);
        int dfs_count = 0;
        while (!st.empty()) {
            int u = st.top();
            st.pop();
            if (vis_dfs[u]) continue;
            vis_dfs[u] = true;
            dfs_count++;
            for (int v : adj[u]) {
                if (!vis_dfs[v]) st.push(v);
            }
        }

        std::cout << "\n[Module 6: Memory Block Graph Analysis (Adjacency & Buddy Edges)]\n";
        std::cout << "  Tracked Buddy Graph Vertices : " << n << "\n";
        std::cout << "  BFS Reachable Blocks         : " << bfs_count << " (Free nodes in component: " << free_clusters << ")\n";
        std::cout << "  DFS Traversed Blocks         : " << dfs_count << "\n";
    }

    static void run_algorithmic_optimizations_demo() {
        std::cout << "\n[Module 8: Algorithmic Optimization Demonstrations]\n";

        std::vector<int> workload_sizes = {12, 20, 28, 44, 60, 80, 110, 180, 240};
        std::sort(workload_sizes.begin(), workload_sizes.end());
        int m = (int)workload_sizes.size();
        int K = 4;
        const int INF = 1000000000;
        std::vector<std::vector<int>> dp(K + 1, std::vector<int>(m + 1, INF));
        dp[0][0] = 0;

        for (int k = 1; k <= K; k++) {
            for (int i = 1; i <= m; i++) {
                for (int j = 0; j < i; j++) {
                    if (dp[k - 1][j] == INF) continue;
                    int class_sz = (int)align_to_16((size_t)workload_sizes[i - 1]);
                    int waste = 0;
                    for (int t = j; t < i; t++) {
                        waste += (class_sz - workload_sizes[t]);
                    }
                    dp[k][i] = std::min(dp[k][i], dp[k - 1][j] + waste);
                }
            }
        }
        std::cout << "  1. Dynamic Programming (Optimal 4 Size-Classes Min Waste) : "
                  << dp[K][m] << " bytes wasted across " << m << " request profiles\n";

        int V = 5;
        std::vector<std::vector<std::pair<int, int>>> cost_graph(V);
        cost_graph[0] = {{1, 10}, {2, 35}};
        cost_graph[1] = {{2, 15}, {3, 40}};
        cost_graph[2] = {{3, 12}, {4, 25}};
        cost_graph[3] = {{4, 8}};

        std::vector<int> dist(V, INF);
        std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>> pq;
        dist[0] = 0;
        pq.push({0, 0});
        while (!pq.empty()) {
            std::pair<int, int> top = pq.top();
            pq.pop();
            int d = top.first;
            int u = top.second;
            if (d > dist[u]) continue;
            for (size_t i = 0; i < cost_graph[u].size(); i++) {
                int v = cost_graph[u][i].first;
                int w = cost_graph[u][i].second;
                if (dist[u] + w < dist[v]) {
                    dist[v] = dist[u] + w;
                    pq.push({dist[v], v});
                }
            }
        }
        std::cout << "  2. Dijkstra Shortest Allocation Cost (Tier 0 -> Tier 4)   : "
                  << dist[4] << " cycles\n";

        int num_threads = 5;
        std::vector<std::vector<int>> conflict(num_threads);
        conflict[0] = {1, 2};
        conflict[1] = {0, 2, 3};
        conflict[2] = {0, 1, 4};
        conflict[3] = {1, 4};
        conflict[4] = {2, 3};

        std::vector<int> color(num_threads, -1);
        int max_color = 0;
        for (int u = 0; u < num_threads; u++) {
            std::vector<bool> used(num_threads, false);
            for (int v : conflict[u]) {
                if (color[v] != -1) used[color[v]] = true;
            }
            int c = 0;
            while (used[c]) c++;
            color[u] = c;
            if (c + 1 > max_color) max_color = c + 1;
        }
        std::cout << "  3. Graph Coloring (Lock-Free Arena Assignment for 5 Tasks): "
                  << max_color << " independent arenas required\n";

        std::vector<int> items = {48, 32, 64, 16, 48};
        int bin_cap   = 112;
        int best_bins = (int)items.size();
        std::vector<int> bins;
        auto backtrack = [&](auto&& self, int idx) -> void {
            if ((int)bins.size() >= best_bins) return;
            if (idx == (int)items.size()) {
                best_bins = (int)bins.size();
                return;
            }
            for (size_t i = 0; i < bins.size(); i++) {
                if (bins[i] + items[idx] <= bin_cap) {
                    bins[i] += items[idx];
                    self(self, idx + 1);
                    bins[i] -= items[idx];
                }
            }
            bins.push_back(items[idx]);
            self(self, idx + 1);
            bins.pop_back();
        };
        backtrack(backtrack, 0);
        std::cout << "  4. Backtracking Optimal Slab Packing (5 blocks, 112B cap) : "
                  << best_bins << " slabs minimum\n";
    }
};