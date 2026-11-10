#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

// Exact offline oracle for simultaneous *strong* single-vertex deletion.
// Edges must already be complete internal hyperedges of the current candidate.
// This implementation never truncates a hyperedge to a candidate vertex set.
namespace robust_batch {

struct Result {
    int cut_vertex = -1;
    std::uint64_t edge_intervals = 0;
    std::uint64_t segment_assignments = 0;
    std::uint64_t union_attempts = 0;
    std::uint64_t leaf_checks = 0;
};

class RollbackDSU {
    struct Change { int child; int parent; int old_parent_size; };
    std::vector<int> parent_, size_;
    std::vector<Change> history_;
    int components_;
public:
    explicit RollbackDSU(int n) : parent_(n), size_(n, 1), components_(n) {
        for (int i = 0; i < n; ++i) parent_[i] = i;
    }
    int find(int x) const {
        // Deliberately no path compression: every mutation must be reversible.
        while (parent_[x] != x) x = parent_[x];
        return x;
    }
    void unite(int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (size_[a] < size_[b]) std::swap(a, b);
        history_.push_back({b, a, size_[a]});
        parent_[b] = a;
        size_[a] += size_[b];
        --components_;
    }
    std::size_t snapshot() const { return history_.size(); }
    int components() const { return components_; }
    void rollback(std::size_t mark) {
        while (history_.size() > mark) {
            const Change x = history_.back(); history_.pop_back();
            parent_[x.child] = x.child;
            size_[x.parent] = x.old_parent_size;
            ++components_;
        }
    }
};

class Oracle {
    int n_;
    const std::vector<std::vector<int>>& edges_;
    std::vector<std::vector<int>> segment_;
    RollbackDSU dsu_;
    Result result_;

    void add(int node, int left, int right, int ql, int qr, int edge_id) {
        if (ql <= left && right <= qr) {
            segment_[node].push_back(edge_id);
            ++result_.segment_assignments;
            return;
        }
        const int mid = left + (right - left) / 2;
        if (ql <= mid) add(node * 2, left, mid, ql, qr, edge_id);
        if (qr > mid) add(node * 2 + 1, mid + 1, right, ql, qr, edge_id);
    }

    bool visit(int node, int left, int right) {
        const auto mark = dsu_.snapshot();
        for (const int eid : segment_[node]) {
            const auto& e = edges_[eid];
            for (std::size_t k = 1; k < e.size(); ++k) {
                ++result_.union_attempts;
                dsu_.unite(e[0], e[k]);
            }
        }
        bool failed = false;
        if (left == right) {
            ++result_.leaf_checks;
            // Vertex left is isolated: every hyperedge incident to it is off.
            // Therefore two components mean one failed vertex plus one
            // connected surviving population (n >= 3).
            if (dsu_.components() > 2) {
                result_.cut_vertex = left;
                failed = true;
            }
        } else {
            const int mid = left + (right - left) / 2;
            failed = visit(node * 2, left, mid);
            if (!failed) failed = visit(node * 2 + 1, mid + 1, right);
        }
        dsu_.rollback(mark);
        return failed;
    }

public:
    Oracle(int n, const std::vector<std::vector<int>>& edges)
        : n_(n), edges_(edges), segment_(std::max<std::size_t>(1, 4ULL * n)), dsu_(n) {}

    Result run() {
        if (n_ < 3) return result_;
        for (std::size_t eid = 0; eid < edges_.size(); ++eid) {
            auto members = edges_[eid];
            std::sort(members.begin(), members.end());
            members.erase(std::unique(members.begin(), members.end()), members.end());
            for (const int v : members)
                if (v < 0 || v >= n_) throw std::invalid_argument("non-local vertex in batch oracle");
            if (members.size() < 2 || members.size() == static_cast<std::size_t>(n_)) continue;
            int start = 0;
            for (const int v : members) {
                if (start < v) {
                    ++result_.edge_intervals;
                    add(1, 0, n_ - 1, start, v - 1, static_cast<int>(eid));
                }
                start = v + 1;
            }
            if (start < n_) {
                ++result_.edge_intervals;
                add(1, 0, n_ - 1, start, n_ - 1, static_cast<int>(eid));
            }
        }
        visit(1, 0, n_ - 1);
        return result_;
    }
};

inline Result find_cut(int n, const std::vector<std::vector<int>>& edges) {
    if (n < 0) throw std::invalid_argument("negative vertex count");
    return Oracle(n, edges).run();
}

} // namespace robust_batch
