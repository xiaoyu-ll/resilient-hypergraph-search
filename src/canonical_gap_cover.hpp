#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

// Output-sensitive canonical covers of the existing midpoint segment tree.
// Nodes retain the old recursive add() heap IDs; no padding leaves are added.
namespace gap_cover {

using Node = std::size_t;

struct Stats {
    std::uint64_t next_calls = 0;
    std::uint64_t lca_queries = 0;
    std::uint64_t emitted_nodes = 0;
};

class Layout {
    int n_;
    std::vector<int> left_, right_;
    std::vector<Node> tin_, tout_, preorder_, highest_left_;
    std::vector<std::uint64_t> leaf_code_;
    std::vector<int> leaf_depth_;

    static int checked_size(int n) {
        if (n < 0) throw std::invalid_argument("negative gap-cover leaf count");
        return n;
    }

    static Node slots(int n) {
        const auto count = static_cast<Node>(n);
        if (count > std::numeric_limits<Node>::max() / 4)
            throw std::length_error("gap-cover layout size overflow");
        return std::max<Node>(1, count * 4);
    }

    void build(Node node, int l, int r, int depth) {
        left_[node] = l;
        right_[node] = r;
        tin_[node] = preorder_.size();
        preorder_.push_back(node);
        // The first node encountered with left endpoint l is the highest one.
        if (highest_left_[l] == 0) highest_left_[l] = node;
        if (l == r) {
            leaf_code_[l] = static_cast<std::uint64_t>(node);
            leaf_depth_[l] = depth;
        } else {
            const int mid = l + (r - l) / 2;
            build(node * 2, l, mid, depth + 1);
            build(node * 2 + 1, mid + 1, r, depth + 1);
        }
        tout_[node] = preorder_.size();
    }

    void check_leaf(int v) const {
        if (v < 0 || v >= n_)
            throw std::out_of_range("non-local gap-cover leaf");
    }

    int lca_depth_unchecked(int u, int v) const {
        // Equal leaves must be handled before the count-leading-zero primitive.
        if (u == v) return leaf_depth_[u];
        const int h = std::min(leaf_depth_[u], leaf_depth_[v]);
        const std::uint64_t a = leaf_code_[u] >> (leaf_depth_[u] - h);
        const std::uint64_t b = leaf_code_[v] >> (leaf_depth_[v] - h);
        const std::uint64_t difference = a ^ b;
        // Distinct leaves are never ancestors, including on nonuniform trees.
        if (difference == 0)
            throw std::logic_error("non-distinct gap-cover leaf paths");
        static_assert(std::numeric_limits<unsigned long long>::digits == 64,
                      "gap-cover LCA requires 64-bit unsigned long long");
        const int width = 64 - __builtin_clzll(difference);
        return h - width;
    }

    Node next_impl(int p, int q, Stats* stats) const {
        // In particular, [0,-1] and [n,n-1] are safe without any leaf access.
        if (p > q) return 0;
        check_leaf(p);
        check_leaf(q);
        if (stats) ++stats->next_calls;
        Node selected = highest_left_[p];
        if (right_[selected] > q) {
            if (stats) ++stats->lca_queries;
            const int depth = lca_depth_unchecked(p, q);
            const Node common = static_cast<Node>(
                leaf_code_[p] >> (leaf_depth_[p] - depth));
            // If its right endpoint exceeds q, the LCA is internal and its
            // left child is the highest node contained in [p,q].
            selected = right_[common] == q ? common : common * 2;
        }
        if (stats) ++stats->emitted_nodes;
        return selected;
    }

public:
    explicit Layout(int n)
        : n_(checked_size(n)), left_(slots(n_), -1), right_(slots(n_), -1),
          tin_(slots(n_)), tout_(slots(n_)), highest_left_(n_),
          leaf_code_(n_), leaf_depth_(n_) {
        if (n_ == 0) return;
        preorder_.reserve(static_cast<Node>(n_) * 2 - 1);
        build(1, 0, n_ - 1, 0);
    }

    int size() const noexcept { return n_; }
    Node capacity() const noexcept { return right_.size(); }
    Node node_count() const noexcept { return preorder_.size(); }

    // Heap-node accessors require a real node. Preorder intervals are half-open.
    int left(Node node) const { return left_.at(node); }
    int right(Node node) const { return right_.at(node); }
    Node tin(Node node) const { return tin_.at(node); }
    Node tout(Node node) const { return tout_.at(node); }
    Node heap_node(Node preorder) const { return preorder_.at(preorder); }
    Node highest_left(int p) const {
        check_leaf(p);
        return highest_left_[p];
    }
    std::uint64_t leaf_code(int p) const {
        check_leaf(p);
        return leaf_code_[p];
    }
    int leaf_depth(int p) const {
        check_leaf(p);
        return leaf_depth_[p];
    }

    // These helpers are pure: witness callers own their own instrumentation.
    int lca_depth(int u, int v) const {
        check_leaf(u);
        check_leaf(v);
        return lca_depth_unchecked(u, v);
    }
    Node lca(int u, int v) const {
        const int depth = lca_depth(u, v);
        return static_cast<Node>(leaf_code_[u] >> (leaf_depth_[u] - depth));
    }

    // Returns 0 for p>q. Otherwise returns the leftmost maximal canonical node.
    // Repeat with p=right(node)+1, stopping at p>q before the next leaf access.
    Node next(int p, int q) const { return next_impl(p, q, nullptr); }
    Node next(int p, int q, Stats& stats) const { return next_impl(p, q, &stats); }
};

} // namespace gap_cover
