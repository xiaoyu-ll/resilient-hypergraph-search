#ifndef ROBUST_RANK_TWO_CERTIFICATE_HPP
#define ROBUST_RANK_TWO_CERTIFICATE_HPP

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

// A sufficient certificate only. If the ordinary graph consisting of the
// candidate's original rank-two hyperedges is biconnected, deleting any one
// member leaves this subgraph connected, hence also the whole-edge hypergraph.
// Failure of this test says nothing about the hypergraph: use the exact oracle.
// Iterative DFS avoids stack-depth failures on large candidates.
namespace rank_two_certificate {

struct Stats {
    std::uint64_t calls=0, successes=0, edge_scans=0, rank_two_edges=0;
    std::uint64_t dfs_arcs=0, rejected_small=0, rejected_degree=0;
    std::uint64_t rejected_articulation=0, rejected_disconnected=0;
};

inline bool certify(int n, const std::vector<std::vector<int>>& edges,
                    Stats& stats) {
    ++stats.calls;
    if(n<3) { ++stats.rejected_small; return false; }
    std::vector<std::vector<int>> adjacency(n);
    for(const auto& edge:edges) {
        ++stats.edge_scans;
        if(edge.size()!=2) continue;
        const int u=edge[0], v=edge[1];
        if(u<0 || v<0 || u>=n || v>=n || u==v)
            throw std::invalid_argument("invalid rank-two certificate edge");
        adjacency[u].push_back(v);
        adjacency[v].push_back(u);
        ++stats.rank_two_edges;
    }
    for(const auto& row:adjacency) {
        if(row.size()<2) { ++stats.rejected_degree; return false; }
    }
    std::vector<int> discovery(n,-1), low(n,-1), parent(n,-1);
    std::vector<int> child_count(n,0), stack;
    std::vector<std::size_t> next(n,0);
    stack.reserve(n);
    int clock=0;
    discovery[0]=low[0]=clock++;
    stack.push_back(0);
    while(!stack.empty()) {
        const int v=stack.back();
        if(next[v]<adjacency[v].size()) {
            const int w=adjacency[v][next[v]++];
            ++stats.dfs_arcs;
            if(discovery[w]<0) {
                parent[w]=v;
                ++child_count[v];
                discovery[w]=low[w]=clock++;
                stack.push_back(w);
            } else if(w!=parent[v]) {
                low[v]=std::min(low[v],discovery[w]);
            }
        } else {
            stack.pop_back();
            const int p=parent[v];
            if(p<0) {
                if(child_count[v]>1) {
                    ++stats.rejected_articulation;
                    return false;
                }
            } else {
                low[p]=std::min(low[p],low[v]);
                if(parent[p]>=0 && low[v]>=discovery[p]) {
                    ++stats.rejected_articulation;
                    return false;
                }
            }
        }
    }
    if(clock!=n) { ++stats.rejected_disconnected; return false; }
    ++stats.successes;
    return true;
}

} // namespace rank_two_certificate
#endif
