#pragma once

#include "interval_oracle.hpp"
#include "canonical_gap_cover.hpp"
#include <chrono>

// Four schedulers share the same traversal, rollback DSU and certificate.
// Bulk is the same lazy event sequence with aggregate retirement of buckets
// whose original gap endpoints all lie inside a certified subtree.
namespace robust_bulk {

using Node = gap_cover::Node;
using Clock = std::chrono::steady_clock;
enum class Scheduler { Recursive, Canonical, Lazy, Bulk };
using Mode = robust_interval::Mode;
inline Mode mode = Mode::Interval;
inline Scheduler scheduler = Scheduler::Bulk;

struct Result : robust_interval::Result {
    std::uint64_t recursive_search_visits=0, scheduler_next_calls=0;
    std::uint64_t scheduler_lca_queries=0, scheduler_emitted_nodes=0;
    std::uint64_t applied_payloads=0, gap_records=0, gap_jump_events=0;
    std::uint64_t skipped_bucket_slots=0, peak_pending_records=0;
    std::uint64_t pending_at_exit=0, schedule_container_capacity_bytes=0;
    // gap_jump_events remains a LOGICAL count, identical to Lazy. A bulk
    // retirement does not read individual Gap records. These counters expose
    // that distinction rather than relabeling the old jump count as a speedup.
    std::uint64_t pruned_record_touches=0, bulk_retired_records=0;
    std::uint64_t bulk_retired_buckets=0, bulk_fallback_buckets=0;
    double preparation_ms=0, traversal_ms=0;
    // Populated only by the correctness probe, never by timed hierarchy runs.
    std::vector<std::pair<Node,int>> applied_trace;
};

class Oracle {
    static constexpr std::size_t NONE=std::numeric_limits<std::size_t>::max();
    struct Gap { int edge,end; std::size_t next; };
    int n_;
    const std::vector<std::vector<int>>& edges_;
    Scheduler scheduler_;
    bool prune_,trace_;
    gap_cover::Layout layout_;
    std::vector<std::vector<int>> segment_;
    std::vector<std::size_t> heads_;
    std::vector<std::uint64_t> bucket_counts_;
    std::vector<int> bucket_max_end_;
    std::vector<Gap> gaps_;
    robust_batch::RollbackDSU dsu_;
    std::vector<int> witness_depth_,maximum_witness_depth_;
    gap_cover::Stats cover_stats_;
    std::uint64_t pending_=0;
    Result result_;

    bool dynamic_schedule() const {
        return scheduler_==Scheduler::Lazy || scheduler_==Scheduler::Bulk;
    }

    void clear_summary(std::size_t p) {
        if(scheduler_==Scheduler::Bulk) {
            bucket_counts_[p]=0;
            bucket_max_end_[p]=-1;
        }
    }

    void push(std::size_t record,Node node) {
        const auto p=layout_.tin(node);
        gaps_[record].next=heads_[p];heads_[p]=record;
        if(scheduler_==Scheduler::Bulk) {
            ++bucket_counts_[p];
            bucket_max_end_[p]=std::max(bucket_max_end_[p],gaps_[record].end);
        }
        ++pending_;++result_.scheduler_emitted_nodes;
        result_.peak_pending_records=std::max(result_.peak_pending_records,pending_);
    }

    void advance(std::size_t record,int p) {
        if(p<=gaps_[record].end)
            push(record,layout_.next(p,gaps_[record].end,cover_stats_));
    }

    void recursive_add(Node node,int left,int right,int ql,int qr,int eid) {
        ++result_.recursive_search_visits;
        if(ql<=left && right<=qr) {
            segment_[node].push_back(eid);++result_.scheduler_emitted_nodes;return;
        }
        const int mid=left+(right-left)/2;
        if(ql<=mid)recursive_add(node*2,left,mid,ql,qr,eid);
        if(qr>mid)recursive_add(node*2+1,mid+1,right,ql,qr,eid);
    }

    void add_gap(int left,int right,int eid) {
        ++result_.edge_intervals;
        if(scheduler_==Scheduler::Recursive) {
            recursive_add(1,0,n_-1,left,right,eid);
        } else if(scheduler_==Scheduler::Canonical) {
            while(left<=right) {
                const auto node=layout_.next(left,right,cover_stats_);
                segment_[node].push_back(eid);++result_.scheduler_emitted_nodes;
                left=layout_.right(node)+1;
            }
        } else {
            const auto record=gaps_.size();gaps_.push_back({eid,right,NONE});
            advance(record,left);
        }
    }

    int aggregate(Node node,int left,int right) {
        if(left==right)return maximum_witness_depth_[node]=witness_depth_[left];
        const int mid=left+(right-left)/2;
        return maximum_witness_depth_[node]=std::max(
            aggregate(node*2,left,mid),aggregate(node*2+1,mid+1,right));
    }

    void apply(Node node,int eid) {
        ++result_.applied_payloads;
        if(trace_)result_.applied_trace.emplace_back(node,eid);
        const auto& e=edges_[eid];
        for(std::size_t j=1;j<e.size();++j) {
            ++result_.union_attempts;dsu_.unite(e[0],e[j]);
        }
    }

    void apply_bucket(Node node,int right) {
        if(!dynamic_schedule()) {
            for(const int eid:segment_[node])apply(node,eid);
            return;
        }
        const auto p=layout_.tin(node);
        auto& head=heads_[p];
        while(head!=NONE) {
            const auto record=head;head=gaps_[record].next;--pending_;
            apply(node,gaps_[record].edge);
            // The next disjoint canonical node lies after this entire subtree.
            advance(record,right+1);
        }
        // No push can return to this bucket: every next node starts after its
        // entire subtree. Thus summaries need no per-record decrement or heap.
        clear_summary(p);
    }

    void skip_descendants(Node node,int right) {
        if(!dynamic_schedule())return;
        for(auto p=layout_.tin(node)+1;p<layout_.tout(node);++p) {
            ++result_.skipped_bucket_slots;
            auto& head=heads_[p];
            if(head==NONE)continue;
            if(scheduler_==Scheduler::Bulk) {
                if(bucket_max_end_[p]<=right) {
                    const auto count=bucket_counts_[p];
                    result_.gap_jump_events+=count;
                    result_.bulk_retired_records+=count;
                    ++result_.bulk_retired_buckets;
                    pending_-=count;
                    head=NONE;
                    clear_summary(p);
                    continue;
                }
                ++result_.bulk_fallback_buckets;
            }
            while(head!=NONE) {
                const auto record=head;head=gaps_[record].next;--pending_;
                ++result_.gap_jump_events;
                ++result_.pruned_record_touches;
                // At most two certified, disjoint subtrees can contain an
                // endpoint of this original gap. This skips all its tasks here.
                advance(record,right+1);
            }
            clear_summary(p);
        }
    }

    bool visit(Node node,int left,int right,int depth) {
        ++result_.visited_nodes;
        const auto mark=dsu_.snapshot();
        apply_bucket(node,right);
        bool failed=false;
        if(left==right) {
            ++result_.leaf_checks;
            if(dsu_.components()>2) {result_.cut_vertex=left;failed=true;}
        } else {
            bool certified=false;
            if(prune_) {
                ++result_.certificate_checks;
                const int count=right-left+1;
                const bool outside=count<n_ && dsu_.components()==count+1;
                if(outside)++result_.outside_connected;
                certified=outside && maximum_witness_depth_[node]<=depth;
                if(certified) {
                    ++result_.interval_certificates;
                    result_.certified_failure_vertices+=count;
                    if(trace_)result_.certified_ranges.push_back({left,right});
                    skip_descendants(node,right);
                }
            }
            if(!certified) {
                const int mid=left+(right-left)/2;
                failed=visit(node*2,left,mid,depth+1);
                if(!failed)failed=visit(node*2+1,mid+1,right,depth+1);
            }
        }
        dsu_.rollback(mark);
        return failed;
    }

public:
    Oracle(int n,const std::vector<std::vector<int>>& edges,Scheduler selected,
           Mode pruning,bool trace)
        :n_(n),edges_(edges),scheduler_(selected),prune_(pruning==Mode::Interval),trace_(trace),
         layout_(n),
         segment_((selected==Scheduler::Lazy || selected==Scheduler::Bulk)?0:layout_.capacity()),
         heads_((selected==Scheduler::Lazy || selected==Scheduler::Bulk)?layout_.node_count():0,NONE),
         bucket_counts_(selected==Scheduler::Bulk?layout_.node_count():0),
         bucket_max_end_(selected==Scheduler::Bulk?layout_.node_count():0,-1),dsu_(n),
         witness_depth_(n,std::numeric_limits<int>::max()),
         maximum_witness_depth_(layout_.capacity()) {}

    Result run(Clock::time_point start) {
        if(n_<3) {
            result_.preparation_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            return std::move(result_);
        }
        for(std::size_t eid=0;eid<edges_.size();++eid) {
            if(eid>static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::length_error("too many hyperedges for integer IDs");
            auto members=edges_[eid];
            std::sort(members.begin(),members.end());
            members.erase(std::unique(members.begin(),members.end()),members.end());
            for(int v:members)if(v<0 || v>=n_)
                throw std::invalid_argument("non-local vertex in scheduled oracle");
            if(members.size()<2 || members.size()==static_cast<std::size_t>(n_))continue;
            if(prune_) {
                int previous=-1;
                for(std::size_t j=0;j<members.size();++j) {
                    ++result_.witness_incidence_scans;
                    int next=-1;
                    if(j+1<members.size()) {
                        ++result_.witness_lca_queries;
                        next=layout_.lca_depth(members[j],members[j+1]);
                    }
                    witness_depth_[members[j]]=std::min(witness_depth_[members[j]],1+std::max(previous,next));
                    previous=next;
                }
            }
            int begin=0;
            for(int v:members) {
                if(begin<v)add_gap(begin,v-1,static_cast<int>(eid));
                begin=v+1;
            }
            if(begin<n_)add_gap(begin,n_-1,static_cast<int>(eid));
        }
        if(prune_)aggregate(1,0,n_-1);
        result_.gap_records=gaps_.size();
        result_.schedule_container_capacity_bytes=
            heads_.capacity()*sizeof(std::size_t)+gaps_.capacity()*sizeof(Gap)+
            bucket_counts_.capacity()*sizeof(std::uint64_t)+bucket_max_end_.capacity()*sizeof(int)+
            segment_.capacity()*sizeof(std::vector<int>);
        for(const auto& items:segment_)
            result_.schedule_container_capacity_bytes+=items.capacity()*sizeof(int);
        const auto prepared=Clock::now();
        result_.preparation_ms=std::chrono::duration<double,std::milli>(prepared-start).count();
        visit(1,0,n_-1,0);
        result_.traversal_ms=std::chrono::duration<double,std::milli>(Clock::now()-prepared).count();
        result_.scheduler_next_calls=cover_stats_.next_calls;
        result_.scheduler_lca_queries=cover_stats_.lca_queries;
        result_.segment_assignments=result_.scheduler_emitted_nodes;
        result_.pending_at_exit=pending_;
        return std::move(result_);
    }
};

inline Result find_cut(int n,const std::vector<std::vector<int>>& edges,
                       Scheduler selected,Mode pruning,bool trace=false) {
    if(n<0)throw std::invalid_argument("negative vertex count");
    const auto start=Clock::now();
    return Oracle(n,edges,selected,pruning,trace).run(start);
}
inline Result find_cut(int n,const std::vector<std::vector<int>>& edges) {
    return find_cut(n,edges,scheduler,mode);
}

} // namespace robust_bulk
