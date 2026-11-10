#pragma once

#include "batch_oracle.hpp"
#include <array>
#include <limits>

// A sufficient certificate for an entire interval of single-member failures.
// Every edge is still activated exactly on the complement of its full member set.
namespace robust_interval {

enum class Mode { Full, Interval };
inline Mode mode = Mode::Interval;

struct Result : robust_batch::Result {
    std::uint64_t visited_nodes = 0, certificate_checks = 0;
    std::uint64_t outside_connected = 0, interval_certificates = 0;
    std::uint64_t certified_failure_vertices = 0;
    std::uint64_t witness_incidence_scans = 0, witness_lca_queries = 0;
    std::vector<std::array<int,2>> certified_ranges;
};

class Oracle {
    int n_;
    const std::vector<std::vector<int>>& edges_;
    std::vector<std::vector<int>> segment_;
    robust_batch::RollbackDSU dsu_;
    std::vector<std::uint64_t> leaf_code_;
    std::vector<int> leaf_depth_, witness_depth_, maximum_witness_depth_;
    bool trace_;
    Result result_;

    void layout(std::size_t node, int left, int right, int depth) {
        if (left == right) {
            leaf_code_[left] = node; leaf_depth_[left] = depth;
            return;
        }
        const int mid = left + (right-left)/2;
        layout(node*2,left,mid,depth+1);
        layout(node*2+1,mid+1,right,depth+1);
    }

    int lca_depth(int u, int v) {
        ++result_.witness_lca_queries;
        if (u == v) return leaf_depth_[u];
        const int h = std::min(leaf_depth_[u],leaf_depth_[v]);
        const std::uint64_t a = leaf_code_[u] >> (leaf_depth_[u]-h);
        const std::uint64_t b = leaf_code_[v] >> (leaf_depth_[v]-h);
        const std::uint64_t difference = a ^ b;
        // Distinct leaves cannot be ancestors of each other. Guard zero anyway.
        if (!difference) throw std::logic_error("non-distinct segment-tree leaf paths");
        const int width = 64 - __builtin_clzll(difference);
        return h-width;
    }

    int aggregate(std::size_t node,int left,int right) {
        if(left==right)return maximum_witness_depth_[node]=witness_depth_[left];
        const int mid=left+(right-left)/2;
        return maximum_witness_depth_[node]=std::max(
            aggregate(node*2,left,mid),aggregate(node*2+1,mid+1,right));
    }

    void add(std::size_t node,int left,int right,int ql,int qr,int eid) {
        if(ql<=left && right<=qr) {
            segment_[node].push_back(eid);++result_.segment_assignments;return;
        }
        const int mid=left+(right-left)/2;
        if(ql<=mid)add(node*2,left,mid,ql,qr,eid);
        if(qr>mid)add(node*2+1,mid+1,right,ql,qr,eid);
    }

    bool visit(std::size_t node,int left,int right,int depth) {
        ++result_.visited_nodes;
        const auto mark=dsu_.snapshot();
        for(const int eid:segment_[node]) {
            const auto& e=edges_[eid];
            for(std::size_t j=1;j<e.size();++j) {
                ++result_.union_attempts;dsu_.unite(e[0],e[j]);
            }
        }
        bool failed=false;
        if(left==right) {
            ++result_.leaf_checks;
            if(dsu_.components()>2) {result_.cut_vertex=left;failed=true;}
        } else {
            ++result_.certificate_checks;
            const int count=right-left+1;
            // All vertices of this interval are isolated in this DSU.
            // Exactly count+1 components means one nonempty outside component.
            const bool outside=count<n_ && dsu_.components()==count+1;
            if(outside)++result_.outside_connected;
            if(outside && maximum_witness_depth_[node]<=depth) {
                ++result_.interval_certificates;
                result_.certified_failure_vertices+=count;
                if(trace_)result_.certified_ranges.push_back({left,right});
            } else {
                const int mid=left+(right-left)/2;
                failed=visit(node*2,left,mid,depth+1);
                if(!failed)failed=visit(node*2+1,mid+1,right,depth+1);
            }
        }
        dsu_.rollback(mark);
        return failed;
    }

public:
    Oracle(int n,const std::vector<std::vector<int>>& edges,bool trace)
        :n_(n),edges_(edges),segment_(std::max<std::size_t>(1,4ULL*n)),dsu_(n),
         leaf_code_(n),leaf_depth_(n),
         witness_depth_(n,std::numeric_limits<int>::max()),
         maximum_witness_depth_(std::max<std::size_t>(1,4ULL*n)),trace_(trace) {}

    Result run() {
        if(n_<3)return result_;
        layout(1,0,n_-1,0);
        for(std::size_t eid=0;eid<edges_.size();++eid) {
            auto members=edges_[eid];
            std::sort(members.begin(),members.end());
            members.erase(std::unique(members.begin(),members.end()),members.end());
            for(int v:members)if(v<0 || v>=n_)
                throw std::invalid_argument("non-local vertex in interval oracle");
            if(members.size()<2 || members.size()==static_cast<std::size_t>(n_))continue;
            int previous_lca=-1;
            for(std::size_t j=0;j<members.size();++j) {
                ++result_.witness_incidence_scans;
                const int next_lca=j+1<members.size()?lca_depth(members[j],members[j+1]):-1;
                const int need=1+std::max(previous_lca,next_lca);
                witness_depth_[members[j]]=std::min(witness_depth_[members[j]],need);
                previous_lca=next_lca;
            }
            int start=0;
            for(int v:members) {
                if(start<v) {
                    ++result_.edge_intervals;
                    add(1,0,n_-1,start,v-1,static_cast<int>(eid));
                }
                start=v+1;
            }
            if(start<n_) {
                ++result_.edge_intervals;
                add(1,0,n_-1,start,n_-1,static_cast<int>(eid));
            }
        }
        aggregate(1,0,n_-1);
        visit(1,0,n_-1,0);
        return result_;
    }
};

inline Result find_cut(int n,const std::vector<std::vector<int>>& edges,
                       Mode selected,bool trace=false) {
    if(n<0)throw std::invalid_argument("negative vertex count");
    if(selected==Mode::Full) {
        Result result;
        static_cast<robust_batch::Result&>(result)=robust_batch::find_cut(n,edges);
        return result;
    }
    return Oracle(n,edges,trace).run();
}

inline Result find_cut(int n,const std::vector<std::vector<int>>& edges) {
    return find_cut(n,edges,mode);
}

} // namespace robust_interval
