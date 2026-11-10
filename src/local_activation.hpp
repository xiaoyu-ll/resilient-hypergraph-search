#pragma once

#include "query.hpp"
#include <optional>

namespace robust_local_v14 {

// Only ranks that can survive a failure within a batch cap are indexed.
class LocalIncidence {
    const Graph& graph;
    const bool ranked;
    std::vector<std::size_t> offsets;
    std::vector<int> ids;

public:
    LocalIncidence(const Graph& graph,int cap,bool ranked=false)
        :graph(graph),ranked(ranked),offsets(static_cast<std::size_t>(graph.n)+1,0) {
        for(const auto& edge:graph.edges)if(static_cast<int>(edge.size())<cap)
            for(int v:edge)++offsets[v+1];
        std::partial_sum(offsets.begin(),offsets.end(),offsets.begin());
        ids.resize(offsets.back());
        auto cursor=offsets;
        if(ranked) {
            std::vector<std::vector<int>> by_rank(cap);
            for(std::size_t id=0;id<graph.edges.size();++id) {
                const auto rank=graph.edges[id].size();
                if(rank<static_cast<std::size_t>(cap))by_rank[rank].push_back(static_cast<int>(id));
            }
            for(const auto& bucket:by_rank)for(int id:bucket)
                for(int v:graph.edges[id])ids[cursor[v]++]=id;
        } else {
            for(std::size_t id=0;id<graph.edges.size();++id) {
                const auto& edge=graph.edges[id];
                if(static_cast<int>(edge.size())<cap)
                    for(int v:edge)ids[cursor[v]++]=static_cast<int>(id);
            }
        }
    }

    auto at(int v,int rank_limit=25) const {
        const auto begin=ids.begin()+offsets[v],end=ids.begin()+offsets[v+1];
        if(!ranked||rank_limit>=25)return std::make_pair(begin,end);
        return std::make_pair(begin,std::lower_bound(begin,end,rank_limit,[&](int id,int r) {
            return static_cast<int>(graph.edges[id].size())<r;
        }));
    }
};

enum class LocalSchedule { Guarded, Ascending };

class LocalActivation {
    const Graph& graph;
    const LocalIncidence& incidence;
    Algorithm algorithm;
    const double limit;
    const LocalSchedule schedule;
    const bool transfer,incremental,structure,direct_completion,joint_pruning;
    Clock::time_point start;
    Clock::time_point probe_until=Clock::time_point::max();
    Result result;
    int target=3;
    int cap_lower=0;
    struct Timeout{};
    struct ProbeExpired{};

    struct Edge {
        unsigned selected;
        std::vector<int> missing;
    };
    using Choices=std::vector<std::vector<int>>;
    // The greatest cap already ruled out for this unrestricted selected set.
    std::map<std::vector<int>,int> exhausted;
    struct SupportInterval {int no=0,yes=25;};
    std::array<std::unordered_map<int,SupportInterval>,3> support_cache;

    void deadline() {
        ++result.counters.deadline_checks;
        const auto now=Clock::now();
        if(limit>0&&std::chrono::duration<double,std::milli>(now-start).count()>=limit)throw Timeout{};
        if(now>=probe_until)throw ProbeExpired{};
    }

    // A vertex in an answer needs k+1 distinct incident edges and its
    // rank-prefix support must tolerate the loss of each other member.
    bool supported(int v,int rank_limit,int layer) {
        auto& cache=support_cache[layer];
        auto& interval=cache[v];
        if(rank_limit<=interval.no)return false;
        if(rank_limit>=interval.yes)return true;
        const auto [begin,end]=incidence.at(v,rank_limit);
        const int k=result.query.k;
        if(end-begin<=k){interval.no=rank_limit;return false;}
        std::unordered_map<int,int> codegree;
        int degree=0,worst=0;
        std::size_t work=0;
        for(auto it=begin;it!=end;++it) {
            if((work++&255)==0)deadline();
            const auto& edge=graph.edges[*it];
            if(static_cast<int>(edge.size())>=rank_limit)continue;
            if(layer>0&&std::any_of(edge.begin(),edge.end(),[&](int u) {
                return !supported(u,rank_limit,layer-1);
            }))continue;
            ++degree;
            for(int u:edge)if(u!=v)worst=std::max(worst,++codegree[u]);
            if(degree-worst>=k){interval.yes=rank_limit;return true;}
        }
        interval.no=rank_limit;
        return false;
    }

    bool support_filter(int v) {
        return algorithm==Algorithm::A1 ? supported(v,result.effective_max_size,0) : supported(v,target,2);
    }

    // Build only affordable edges touching a selected vertex, once per node.
    std::vector<Edge> collect(const std::vector<int>& selected) {
        const int budget=target-static_cast<int>(selected.size());
        std::vector<Edge> edges;
        for(std::size_t i=0;i<selected.size();++i) {
            const auto [begin,end]=incidence.at(selected[i],target);
            for(auto it=begin;it!=end;++it) {
                if((result.counters.edge_scans++&255)==0)deadline();
                const auto& edge=graph.edges[*it];
                if(static_cast<int>(edge.size())>=target)continue;
                unsigned mask=0;
                std::vector<int> missing;
                for(int v:edge) {
                    const auto found=std::lower_bound(selected.begin(),selected.end(),v);
                    if(found==selected.end()||*found!=v)missing.push_back(v);
                    else mask|=1u<<(found-selected.begin());
                }
                if((mask&((1u<<i)-1))||static_cast<int>(missing.size())>budget)continue;
                if(std::any_of(missing.begin(),missing.end(),
                    [&](int v){return !support_filter(v);}))continue;
                edges.push_back({mask,std::move(missing)});
            }
        }
        return edges;
    }

    // Previously incident edges can only lose affordability as S grows.
    std::vector<Edge> frontier(const std::vector<int>& selected,const std::vector<int>& added,
                               const std::vector<Edge>& previous,const std::vector<int>* closed,bool& rejected) {
        const int budget=target-static_cast<int>(selected.size());
        std::vector<Edge> edges;
        edges.reserve(previous.size());
        std::array<unsigned,24> perm;
        unsigned new_mask=0;
        int parent_pos=0;
        for(std::size_t i=0;i<selected.size();++i) {
            const unsigned bit=1u<<i;
            if(std::binary_search(added.begin(),added.end(),selected[i]))new_mask|=bit;
            else perm[parent_pos++]=bit;
        }
        for(const auto& e:previous) {
            if((result.counters.edge_scans++&255)==0)deadline();
            unsigned mask=0;
            for(unsigned bits=e.selected;bits;bits&=bits-1)mask|=perm[__builtin_ctz(bits)];
            std::array<int,24> missing;
            int count=0;
            auto a=added.begin();
            for(int v:e.missing) {
                while(a!=added.end()&&*a<v)++a;
                if(a!=added.end()&&*a==v)
                    mask|=1u<<(std::lower_bound(selected.begin(),selected.end(),v)-selected.begin());
                else if(count<budget)missing[count++]=v;
                else {count=-1;break;}
            }
            if(count>=0)edges.push_back({mask,std::vector<int>(missing.begin(),missing.begin()+count)});
        }

        // At one slot below a two-slot state, every possible last member
        // already had a singleton-missing support edge at the parent.
        if(structure&&budget==1&&added.size()==1) {
            std::vector<int> candidates;
            for(const auto& e:previous)if(e.missing.size()==1&&e.missing.front()!=added.front())
                candidates.push_back(e.missing.front());
            std::sort(candidates.begin(),candidates.end());
            candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
            const int u=added.front();
            const unsigned owner=1u<<(std::lower_bound(selected.begin(),selected.end(),u)-selected.begin());
            for(int v:candidates) {
                if((result.counters.edge_scans++&255)==0)deadline();
                const std::vector<int> edge{std::min(u,v),std::max(u,v)};
                if(std::binary_search(graph.edges.begin(),graph.edges.end(),edge))
                    edges.push_back({owner,{v}});
            }
            return edges;
        }

        // An edge first exposed by A is disjoint from the parent S.
        const int rank_limit=std::min(target,budget+static_cast<int>(added.size())+1);
        if(algorithm==Algorithm::A3&&joint_pruning&&!closed) {
            std::size_t expansion=0;
            for(int v:added) {
                const auto [first,last]=incidence.at(v,rank_limit);
                expansion+=last-first;
            }
            if(expansion>edges.size()*2) {
                const int n=static_cast<int>(selected.size()),k=result.query.k;
                std::array<int,24> degree{},short_degree{};
                std::array<std::array<int,24>,24> co{};
                const unsigned old=((1u<<n)-1)^new_mask;
                for(const auto& edge:edges)for(unsigned bits=edge.selected&old;bits;bits&=bits-1) {
                    const int v=__builtin_ctz(bits);
                    ++degree[v];
                    short_degree[v]+=static_cast<int>(edge.missing.size())<budget;
                    for(unsigned peers=edge.selected;peers;peers&=peers-1)++co[v][__builtin_ctz(peers)];
                }
                for(unsigned bits=old;bits;bits&=bits-1) {
                    const int v=__builtin_ctz(bits);
                    co[v][v]=0;
                    const int worst=*std::max_element(co[v].begin(),co[v].begin()+n);
                    if(degree[v]-worst<k||(budget>0&&short_degree[v]<k)) {
                        ++result.counters.frontier_screen_prunes;
                        rejected=true;
                        return {};
                    }
                }
            }
        }
        for(int v:added) {
            const auto [first,last]=incidence.at(v,rank_limit);
            const unsigned owner=1u<<(std::lower_bound(selected.begin(),selected.end(),v)-selected.begin());
            if(closed) {
                std::vector<int> candidates;
                for(int u:*closed)
                    if(u!=v&&(u>v||!std::binary_search(added.begin(),added.end(),u)))candidates.push_back(u);
                const std::size_t scan=last-first;
                std::uint64_t cost=0,coefficient=1;
                const int free=static_cast<int>(candidates.size());
                for(int r=1;r<rank_limit&&r<=free+1&&cost<=scan&&coefficient<=scan;++r) {
                    if(r>=2&&std::binary_search(graph.ranks.begin(),graph.ranks.end(),r))cost+=coefficient;
                    coefficient=coefficient*(free-r+1)/r;
                }
                if(cost<=scan&&coefficient<=scan) {
                    std::vector<int> term{v};
                    const auto expose=[&](auto&& self,int pos,int left)->void {
                        if(left==0) {
                            if((result.counters.edge_scans++&255)==0)deadline();
                            auto edge=term;
                            std::sort(edge.begin(),edge.end());
                            if(!std::binary_search(graph.edges.begin(),graph.edges.end(),edge))return;
                            unsigned mask=0;
                            std::vector<int> missing;
                            for(int u:edge) {
                                auto it=std::lower_bound(selected.begin(),selected.end(),u);
                                if(it!=selected.end()&&*it==u)mask|=1u<<(it-selected.begin());
                                else missing.push_back(u);
                            }
                            if(static_cast<int>(missing.size())<=budget&&std::all_of(missing.begin(),missing.end(),
                                [&](int u){return support_filter(u);}))edges.push_back({mask,std::move(missing)});
                            return;
                        }
                        for(int i=pos;i<=free-left;++i) {
                            term.push_back(candidates[i]);
                            self(self,i+1,left-1);
                            term.pop_back();
                        }
                    };
                    for(int rank:graph.ranks) {
                        if(rank>=rank_limit||rank>free+1)break;
                        expose(expose,0,rank-1);
                    }
                    continue;
                }
            }
            for(auto it=first;it!=last;++it) {
                if((result.counters.edge_scans++&255)==0)deadline();
                const auto& e=graph.edges[*it];
                if(closed&&!std::includes(closed->begin(),closed->end(),e.begin(),e.end()))continue;
                unsigned mask=0;
                std::array<int,24> missing;
                int count=0;
                for(int u:e) {
                    const auto found=std::lower_bound(selected.begin(),selected.end(),u);
                    if(found!=selected.end()&&*found==u)mask|=1u<<(found-selected.begin());
                    else if(count<budget)missing[count++]=u;
                    else {count=-1;break;}
                }
                if(count<0||(mask&~new_mask)||(mask&(owner-1)))continue;
                if(std::any_of(missing.begin(),missing.begin()+count,
                    [&](int u){return !support_filter(u);}))continue;
                edges.push_back({mask,std::vector<int>(missing.begin(),missing.begin()+count)});
            }
        }
        return edges;
    }

    #include "local_tail.inc"

    static void consider(Choices& best,Choices options) {
        std::sort(options.begin(),options.end());
        options.erase(std::unique(options.begin(),options.end()),options.end());
        if(best.empty()||options.size()<best.size())best=std::move(options);
    }

    struct Row {int demand; Choices masks;};

    static std::vector<int> combine(const std::vector<int>& a,const std::vector<int>& b) {
        std::vector<int> both;
        both.reserve(a.size()+b.size());
        std::set_union(a.begin(),a.end(),b.begin(),b.end(),std::back_inserter(both));
        return both;
    }

    // Branch on minimal completions for two whole obligations when bounded.
    bool joint_repair(const std::vector<Row>& rows,int budget,Choices& choices) {
        if(rows.empty())return true;
        ++result.counters.factor_calls;
        constexpr std::size_t work_limit=1200, family_limit=48;
        std::size_t work=0;
        Choices completions;
        std::set<std::vector<int>> visited;
        struct Decline{};
        auto visit=[&](auto&& self,const std::vector<int>& picked)->void {
            if(!visited.insert(picked).second)return;
            if((visited.size()&31)==0)deadline();
            Choices best;
            for(const auto& row:rows) {
                int covered=0;
                Choices next;
                for(const auto& term:row.masks) {
                    if(++work>work_limit)throw Decline{};
                    auto joined=combine(picked,term);
                    if(joined.size()==picked.size())++covered;
                    else if(joined.size()<=static_cast<std::size_t>(budget))next.push_back(std::move(joined));
                }
                if(covered>=row.demand)continue;
                if(covered+static_cast<int>(next.size())<row.demand)return;
                consider(best,std::move(next));
            }
            if(best.empty()) {
                if(completions.size()>=family_limit)throw Decline{};
                completions.push_back(picked);
                return;
            }
            for(const auto& next:best)self(self,next);
        };
        try {visit(visit,{});} catch(const Decline&) {return true;}
        ++result.counters.factor_used;
        if(completions.empty()){++result.counters.factor_prunes;return false;}
        std::sort(completions.begin(),completions.end(),[](const auto& a,const auto& b) {
            return a.size()!=b.size()?a.size()<b.size():a<b;
        });
        Choices minimal;
        for(auto& add:completions)
            if(std::none_of(minimal.begin(),minimal.end(),[&](const auto& x) {
                return std::includes(add.begin(),add.end(),x.begin(),x.end());
            }))minimal.push_back(std::move(add));
        Choices replacement, anchored;
        for(const auto& original:choices) {
            std::vector<int> common;
            bool feasible=false;
            for(const auto& repair:minimal) {
                auto merged=combine(original,repair);
                if(merged.size()>static_cast<std::size_t>(budget))continue;
                if(!feasible)common=merged;
                else {
                    std::vector<int> next;
                    std::set_intersection(common.begin(),common.end(),merged.begin(),merged.end(),
                                          std::back_inserter(next));
                    common=std::move(next);
                }
                feasible=true;
                if(replacement.size()<=choices.size())replacement.push_back(std::move(merged));
            }
            if(feasible)anchored.push_back(std::move(common));
        }
        if(anchored.empty()){++result.counters.factor_prunes;return false;}
        choices=replacement.size()<=choices.size()?std::move(replacement):std::move(anchored);
        return true;
    }

    // Reduce each alternative independently. A stronger row cannot replace
    // a branch with several branches.
    bool joint_cover(const std::vector<Row>& rows,int budget,Choices& choices) {
        if(rows.empty()||choices.size()<=1||choices.size()>32)return true;
        ++result.counters.factor_calls;
        Choices tightened;
        for(const auto& a:choices) {
            auto forced=a;
            bool possible=true;
            for(;;) {
                bool changed=false;
                std::vector<int> combined=forced;
                int minimum=0;
                std::vector<int> footprint;
                for(const auto& row:rows) {
                    int need=row.demand;
                    Choices terms;
                    for(const auto& mask:row.masks) {
                        if(std::includes(forced.begin(),forced.end(),mask.begin(),mask.end())) {
                            if(--need==0)break;
                        } else {
                            std::vector<int> residual;
                            std::set_difference(mask.begin(),mask.end(),forced.begin(),forced.end(),
                                                std::back_inserter(residual));
                            if(forced.size()+residual.size()<=static_cast<std::size_t>(budget))
                                terms.push_back(std::move(residual));
                        }
                    }
                    if(need<=0)continue;
                    if(static_cast<int>(terms.size())<need){possible=false;break;}
                    int min_size=budget;
                    std::map<int,int> frequency;
                    for(const auto& term:terms) {
                        min_size=std::min(min_size,static_cast<int>(term.size()));
                        for(int v:term)++frequency[v];
                    }
                    bool disjoint=true;
                    std::vector<int> domain,mandatory;
                    for(const auto& [v,occurrences]:frequency) {
                        disjoint&=!std::binary_search(footprint.begin(),footprint.end(),v);
                        domain.push_back(v);
                        if(static_cast<int>(terms.size())-occurrences<need)mandatory.push_back(v);
                    }
                    if(disjoint){minimum+=min_size;footprint=combine(footprint,domain);}
                    combined=combine(combined,mandatory);
                }
                if(!possible||combined.size()>static_cast<std::size_t>(budget)||
                   forced.size()+minimum>static_cast<std::size_t>(budget)){possible=false;break;}
                changed=combined.size()>forced.size();
                if(!changed)break;
                forced=std::move(combined);
                deadline();
            }
            if(possible)tightened.push_back(std::move(forced));
        }
        if(tightened.empty()){++result.counters.factor_prunes;return false;}
        if(tightened!=choices) {
            ++result.counters.factor_used;
            std::sort(tightened.begin(),tightened.end());
            tightened.erase(std::unique(tightened.begin(),tightened.end()),tightened.end());
            choices=std::move(tightened);
        }
        return true;
    }

    // The two required survivors draw support from shared and private
    // vertices. Each private slot can be charged to just one row.
    bool seed_rows_fit(const std::vector<Edge>& edges,const std::vector<int>& selected,int budget) {
        if(result.query.members.size()!=2||budget<2||result.query.k<2)return true;
        const unsigned a=1u<<(std::lower_bound(selected.begin(),selected.end(),result.query.members[0])-selected.begin());
        const unsigned b=1u<<(std::lower_bound(selected.begin(),selected.end(),result.query.members[1])-selected.begin());
        std::array<std::vector<const Edge*>,2> rows;
        std::array<int,2> demand{result.query.k,result.query.k};
        for(const auto& e:edges) {
            const unsigned side=e.selected&(a|b);
            if(side!=a&&side!=b)continue;
            const int i=side==b;
            if(e.missing.empty())--demand[i];
            else rows[i].push_back(&e);
        }
        if(demand[0]<=0||demand[1]<=0)return true;
        std::unordered_map<int,unsigned> ownership;
        for(int i=0;i<2;++i)for(const auto* e:rows[i])for(int v:e->missing)ownership[v]|=1u<<i;
        int shared=0;
        for(const auto& item:ownership)shared+=item.second==3;
        if(shared>budget/2)return true;
        using Group=std::array<unsigned,3>;
        std::array<std::map<Group,int>,2> groups;
        for(int i=0;i<2;++i)for(const auto* e:rows[i]) {
            unsigned q=0;
            for(int v:e->missing)q+=ownership.at(v)==3;
            ++groups[i][{e->selected,q,static_cast<unsigned>(e->missing.size())-q}];
        }
        std::array<std::array<unsigned,25>,25> choose{};
        for(int n=0;n<=budget;++n) {
            choose[n][0]=1;
            for(int j=1;j<=n;++j)choose[n][j]=choose[n-1][j]+choose[n-1][j-1];
        }
        for(int q=0;q<=shared;++q) {
            int private_slots=0;
            for(int i=0;i<2;++i) {
                int p=0;
                for(;q+private_slots+p<=budget;++p) {
                    std::uint64_t upper=0;
                    for(const auto& [type,frequency]:groups[i]) {
                        upper+=std::min<std::uint64_t>(frequency,
                                    std::uint64_t{choose[q][type[1]]}*choose[p][type[2]]);
                        if(upper>=static_cast<unsigned>(demand[i]))break;
                    }
                    if(upper>=static_cast<unsigned>(demand[i]))break;
                }
                private_slots+=p;
            }
            if(q+private_slots<=budget)return true;
        }
        ++result.counters.seed_partition_prunes;
        return false;
    }

    bool supports(const std::vector<Edge>& edges,int count,int budget,Choices& choices,std::vector<Row>& rows) {
        const int k=result.query.k;
        std::vector<int> degree(count,0),active(count,0),pair(count*count,0),active_pair(count*count,0);
        std::vector<std::vector<const Edge*>> incident(count);
        for(const auto& e:edges) {
            unsigned vertices=e.selected;
            while(vertices) {
                const int v=__builtin_ctz(vertices);vertices&=vertices-1;
                ++degree[v];
                incident[v].push_back(&e);
                if(e.missing.empty())++active[v];
                unsigned peers=e.selected&~(1u<<v);
                while(peers) {
                    const int x=__builtin_ctz(peers);peers&=peers-1;
                    ++pair[v*count+x];
                    if(e.missing.empty())++active_pair[v*count+x];
                }
            }
        }
        const bool light=algorithm!=Algorithm::A1&&target<=6;
        const bool advanced=algorithm!=Algorithm::A1&&!light;
        const auto optional_affordable=[&](const Edge* e) {
            return !structure||e->missing.size()<=static_cast<std::size_t>(std::max(0,budget-1));
        };
        std::set<int> forced,blockers;
        std::vector<int> optional_failure(count,-1);
        for(int v=0;v<count;++v) {
            if(advanced&&degree[v]<=k)return false;
            for(int x=0;x<count;++x)if(v!=x) {
                const int upper=degree[v]-pair[v*count+x];
                if(upper<k)return false;
                if(advanced&&upper==k)for(const Edge* e:incident[v])
                    if(!(e->selected&(1u<<x)))forced.insert(e->missing.begin(),e->missing.end());
            }
            if(algorithm==Algorithm::A3&&active[v]>k&&std::all_of(active_pair.begin()+v*count,
                active_pair.begin()+(v+1)*count,[&](int co){return active[v]-co>=k;}))continue;
            if(advanced||light) {
                std::unordered_map<int,int> codegree;
                int optional_degree=0;
                for(const Edge* e:incident[v])if(optional_affordable(e)) {
                    ++optional_degree;
                    for(int u:e->missing)++codegree[u];
                }
                if(optional_degree<k){++result.counters.optional_prunes;return false;}
                int worst=0;
                for(const auto& p:codegree) {
                    if(p.second>optional_degree-k) {
                        if(advanced||structure) {
                            ++result.counters.optional_prunes;
                            return false;
                        }
                        blockers.insert(p.first);
                    }
                    if(p.second>worst||(p.second==worst&&p.first<optional_failure[v])) {
                        worst=p.second;
                        optional_failure[v]=p.first;
                    }
                }
                if(advanced&&optional_failure[v]>=0&&optional_degree-worst==k)
                    for(const Edge* e:incident[v])
                        if(optional_affordable(e)&&!std::binary_search(e->missing.begin(),e->missing.end(),optional_failure[v]))
                            forced.insert(e->missing.begin(),e->missing.end());
            }
            if(forced.size()>static_cast<std::size_t>(budget)) {
                ++result.counters.forced_budget_prunes;
                return false;
            }
        }
        if(!forced.empty()) {
            result.counters.forced_members+=forced.size();
            choices.emplace_back(forced.begin(),forced.end());
            return true;
        }
        constexpr std::uint64_t scale=5354228880ULL; // lcm(1,...,24)
        // -1 is the intact row, and count is the tightest optional-failure row.
        for(int v=0;v<count;++v)for(int x=advanced?-1:0;x<count+advanced;++x)if(v!=x) {
            if(x==count&&optional_failure[v]<0)continue;
            deadline();
            const int demand=x<0 ? k+1-active[v] : k-active[v]+(x<count?active_pair[v*count+x]:0);
            if(demand<=0)continue;
            Choices options;
            std::unordered_map<int,std::uint64_t> load;
            for(const Edge* e:incident[v]) {
                if((x>=0&&x<count&&(e->selected&(1u<<x)))||e->missing.empty())continue;
                if(x==count&&(!optional_affordable(e)||
                    std::binary_search(e->missing.begin(),e->missing.end(),optional_failure[v])))continue;
                if(light&&std::any_of(e->missing.begin(),e->missing.end(),
                                     [&](int u){return blockers.count(u);}))continue;
                options.push_back(e->missing);
                if(advanced||light)
                    for(int u:e->missing)load[u]+=scale/e->missing.size();
            }
            if(options.size()<static_cast<std::size_t>(demand))return false;
            if((advanced||light)&&demand>1&&load.size()>static_cast<std::size_t>(budget)) {
                std::vector<std::uint64_t> weights;
                weights.reserve(load.size());
                for(const auto& p:load)weights.push_back(p.second);
                std::nth_element(weights.begin(),weights.begin()+budget,weights.end(),std::greater<std::uint64_t>());
                if(std::accumulate(weights.begin(),weights.begin()+budget,std::uint64_t{0})<scale*demand)return false;
            }
            if(advanced&&algorithm==Algorithm::A3&&joint_pruning&&options.size()<=32&&(k>1||demand>1)) {
                std::sort(options.begin(),options.end());
                const auto same=[&](const Row& row){return row.demand==demand&&row.masks==options;};
                if(std::none_of(rows.begin(),rows.end(),same)) {
                    rows.push_back({demand,options});
                    std::sort(rows.begin(),rows.end(),[](const Row& a,const Row& b) {
                        return a.masks.size()*b.demand<b.masks.size()*a.demand;
                    });
                    if(rows.size()>2)rows.pop_back();
                }
            }
            consider(choices,std::move(options));
            if(!advanced&&choices.size()==1)return true;
        }
        return true;
    }

    bool cuts(const std::vector<Edge>& edges,int count,Choices& choices) {
        const unsigned all=(1u<<count)-1;
        for(int x=0;x<count;++x) {
            deadline();
            const unsigned failed=1u<<x;
            DSU components(count);
            for(const auto& e:edges)if(e.missing.empty()&&!(e.selected&failed)) {
                unsigned bits=e.selected;
                const int first=__builtin_ctz(bits);
                for(bits&=bits-1;bits;bits&=bits-1)components.unite(first,__builtin_ctz(bits));
            }
            const int root=components.find(__builtin_ctz(all^failed));
            unsigned side=0;
            for(int i=0;i<count;++i)if(i!=x&&components.find(i)==root)side|=1u<<i;
            if(side==(all^failed))continue;
            Choices options;
            for(const auto& e:edges)if(!(e.selected&failed)&&(e.selected&side)&&!e.missing.empty())
                options.push_back(e.missing);
            if(options.empty())return false;
            consider(choices,std::move(options));
            if(choices.size()==1)return true;
        }
        return true;
    }

    bool search(const std::vector<int>& selected,bool all_completions=true,
                const std::vector<int>* added=nullptr,const std::vector<Edge>* parent=nullptr,
                const std::vector<int>* inherited=nullptr) {
        deadline();
        ++result.counters.search_nodes;
        const auto ruled_out=exhausted.find(selected);
        if(ruled_out!=exhausted.end()&&ruled_out->second>=target) {
            ++result.counters.activation_reused;
            return false;
        }
        bool rejected=false;
        auto edges=incremental&&parent?frontier(selected,*added,*parent,inherited,rejected):collect(selected);
        if(rejected) {
            if(exhausted.size()<100000)exhausted[selected]=target;
            return false;
        }
        const int count=static_cast<int>(selected.size()),budget=target-count;
        if(budget<=1) {
            if(close_tail(edges,selected,budget))return true;
            if(exhausted.size()<100000)exhausted[selected]=target;
            return false;
        }
        std::vector<int> narrowed;
        const bool keep_domain=algorithm==Algorithm::A3&&incremental;
        boundary_filter(edges,budget,keep_domain?&narrowed:nullptr);
        if(keep_domain&&!inherited&&narrowed.empty()&&budget==3&&result.query.k==1)
            narrowed=three_slot_domain(edges,selected);
        const auto* closed=narrowed.empty()?inherited:&narrowed;
        std::vector<int> remaining;
        if(closed==inherited&&inherited&&added) {
            std::set_difference(inherited->begin(),inherited->end(),added->begin(),added->end(),
                                std::back_inserter(remaining));
            closed=&remaining;
        }
        if(closed) {
            if(probe_until==Clock::time_point::max()&&closed->size()<static_cast<std::size_t>(budget)) {
                if(exhausted.size()<100000)exhausted[selected]=target;
                return false;
            }
            edges.erase(std::remove_if(edges.begin(),edges.end(),[&](const auto& e) {
                return !std::includes(closed->begin(),closed->end(),e.missing.begin(),e.missing.end());
            }),edges.end());
        }
        if(structure)tail_filter(edges,count,budget);
        if(algorithm==Algorithm::A3&&joint_pruning&&!seed_rows_fit(edges,selected,budget)) {
            if(exhausted.size()<100000)exhausted[selected]=target;
            return false;
        }
        Choices choices;
        std::vector<Row> rows;
        if(supports(edges,count,budget,choices,rows)) {
            if(!choices.empty()||cuts(edges,count,choices)) {
                if(closed&&!choices.empty()) {
                    choices.erase(std::remove_if(choices.begin(),choices.end(),[&](const auto& a) {
                        return !std::includes(closed->begin(),closed->end(),a.begin(),a.end());
                    }),choices.end());
                    if(choices.empty()) {
                        if(exhausted.size()<100000)exhausted[selected]=target;
                        return false;
                    }
                }
                if(algorithm==Algorithm::A3&&direct_completion&&choices.size()>1)
                    if(const auto decision=close_boundary(edges,selected,budget,choices,closed)) {
                        if(!*decision&&exhausted.size()<100000)exhausted[selected]=target;
                        return *decision;
                    }
                if(choices.size()>1&&(rows.size()==2||(rows.size()==1&&rows.front().demand>1))) {
                    const bool survives=result.query.k>1 ? joint_repair(rows,budget,choices)
                                                         : joint_cover(rows,budget,choices);
                    if(!survives) {
                        if(exhausted.size()<100000)exhausted[selected]=target;
                        return false;
                    }
                }
                if(choices.empty()&&count>=3) {
                    result.witness=selected;
                    return true;
                }
                std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b) {
                    return a.size()!=b.size()?a.size()<b.size():a<b;
                });
                if(structure&&choices.size()<=128) {
                    Choices minimal;
                    for(auto& add:choices) {
                        if(std::none_of(minimal.begin(),minimal.end(),[&](const auto& earlier) {
                            return earlier.size()<add.size()&&
                                std::includes(add.begin(),add.end(),earlier.begin(),earlier.end());
                        }))minimal.push_back(std::move(add));
                        else ++result.counters.dominated_skips;
                    }
                    choices=std::move(minimal);
                }
                if(all_completions&&probe_until!=Clock::time_point::max())
                    cap_lower=std::max(cap_lower,static_cast<int>(selected.size()+choices.front().size()));
                for(const auto& add:choices) {
                    std::vector<int> next;
                    next.reserve(selected.size()+add.size());
                    std::set_union(selected.begin(),selected.end(),add.begin(),add.end(),std::back_inserter(next));
                    ++result.counters.activation_branches;
                    if(search(next,all_completions&&choices.size()==1,&add,&edges,closed))return true;
                }
            }
        }
        if(exhausted.size()<100000)exhausted[selected]=target;
        return false;
    }

public:
    LocalActivation(const Graph& graph,const LocalIncidence& incidence,Algorithm algorithm,double limit,
                    LocalSchedule schedule=LocalSchedule::Ascending,bool transfer=true,
                    bool incremental=true,bool structure=true,
                    bool direct_completion=true,bool joint_pruning=true)
        :graph(graph),incidence(incidence),algorithm(algorithm),limit(limit),schedule(schedule),
         transfer(transfer),incremental(incremental),
         structure(algorithm!=Algorithm::A1&&structure),direct_completion(direct_completion),
          joint_pruning(joint_pruning) {}

    // A caller may supply a lower bound already certified for this query.
    Result run(Query query,int proved_lower=3) {
        start=Clock::now();
        probe_until=Clock::time_point::max();
        cap_lower=0;
        result=Result{};
        exhausted.clear();
        for(auto& cache:support_cache)cache.clear();
        if(query.k<1||query.max_size<0)throw std::invalid_argument("invalid query threshold or size cap");
        for(int v:query.members)if(v<0||v>=graph.n)throw std::invalid_argument("invalid query member");
        std::sort(query.members.begin(),query.members.end());
        query.members.erase(std::unique(query.members.begin(),query.members.end()),query.members.end());
        if(query.members.size()<2)throw std::invalid_argument("query needs at least two distinct members");
        result.query=std::move(query);
        result.time_limit_ms=limit;
        const int cap=result.effective_max_size=std::min(graph.n,result.query.max_size);
        target=result.initial_lower_bound=result.lower_bound=
            std::max({3,proved_lower,static_cast<int>(result.query.members.size())});
        try {
            if(algorithm==Algorithm::A1&&target<=cap&&std::any_of(result.query.members.begin(),result.query.members.end(),
                [&](int v){return !support_filter(v);}))target=cap+1;
            auto attempt=[&](int t) {
                target=t;
                ++result.counters.target_sizes;
                if(!transfer) {
                    exhausted.clear();
                    if(algorithm==Algorithm::A3)for(auto& cache:support_cache)cache.clear();
                }
                const bool eligible=algorithm==Algorithm::A1||
                    std::all_of(result.query.members.begin(),result.query.members.end(),[&](int v){return support_filter(v);});
                const bool found=eligible&&search(result.query.members);
                if(!found)++result.counters.completed_target_sizes;
                return found;
            };
            auto optimal=[&]() {
                result.status="optimal";
                result.complete=result.optimal=result.feasible=true;
                result.lower_bound=static_cast<int>(result.witness.size());
            };
            double charged=0,next_gate=8;
            int ceiling=cap;
            std::vector<int> best;
            for(int t=target;t<=ceiling;++t) {
                result.lower_bound=t;
                if(attempt(t)){optimal();break;}
                result.lower_bound=t+1;
                if(algorithm!=Algorithm::A3||schedule!=LocalSchedule::Guarded||
                   !best.empty()||t<8||t+1>=cap)continue;

                const auto now=Clock::now();
                const double primary=std::chrono::duration<double,std::milli>(now-start).count()-charged;
                if(primary<next_gate)continue;
                next_gate=2*primary;
                // Charge failed probes as well. A later probe can spend only
                // credit earned by the ascending path and not yet consumed.
                const double credit=std::min(150.0,primary/4-charged);
                if(credit<1)continue;
                ++result.counters.cap_attempts;
                probe_until=now+std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double,std::milli>(credit));
                bool found=false,finished=false;
                try {found=attempt(cap);finished=true;} catch(const ProbeExpired&) {}
                probe_until=Clock::time_point::max();
                charged+=std::chrono::duration<double,std::milli>(Clock::now()-now).count();
                if(finished&&!found){++result.counters.cap_rejects;break;}
                if(found) {
                    best=std::exchange(result.witness,std::vector<int>{});
                    ceiling=static_cast<int>(best.size())-1;
                } else if(cap_lower>t+1) {
                    // Only a complete branch cover can raise this bound.
                    result.counters.cap_skipped_sizes+=cap_lower-(t+1);
                    result.lower_bound=cap_lower;
                    t=cap_lower-1;
                }
            }
            if(result.status.empty()&&!best.empty()) {
                result.witness=std::move(best);
                optimal();
            }
            if(result.status.empty()) {
                result.status="infeasible";
                result.complete=true;
                result.lower_bound=std::max(result.lower_bound,cap+1);
            }
        } catch(const Timeout&) {result.status="timeout";}
        result.elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        return result;
    }
};

} // namespace robust_local_v14
