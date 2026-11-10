#pragma once

#include "bulk_scheduled_oracle.hpp"
#include "rank_two_certificate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "factor_budget.hpp"

// Exact minimum-cardinality, size-capped queries under whole-edge induction
// and strong single-member deletion. No inherited v7 implementation is changed.
namespace robust_local_v14 {

using U64=std::uint64_t;
using Clock=std::chrono::steady_clock;

struct Graph {
    int n=0;
    U64 raw_edges=0, duplicate_edges=0, duplicate_members=0, small_edges=0;
    std::vector<std::vector<int>> edges;
    std::vector<std::vector<int>> incident;
    std::vector<int> ranks;
};

class GraphTextReader {
    std::unique_ptr<std::FILE,decltype(&std::fclose)> file;
    std::vector<unsigned char> buffer=std::vector<unsigned char>(256*1024);
    std::size_t next=0,end=0;

    int get() {
        if(next==end) {
            end=std::fread(buffer.data(),1,buffer.size(),file.get());
            next=0;
            if(!end) {
                if(std::ferror(file.get()))throw std::runtime_error("cannot read input");
                return EOF;
            }
        }
        return buffer[next++];
    }
    int token_start() {
        int c;
        do {c=get();} while(c!=EOF&&std::isspace(c));
        return c;
    }
public:
    explicit GraphTextReader(const std::string& path):file(std::fopen(path.c_str(),"rb"),&std::fclose) {
        if(!file)throw std::invalid_argument("cannot open input");
    }
    int nonnegative(const char* error) {
        int c=token_start();
        const bool negative=c=='-';
        if(c=='+'||negative)c=get();
        if(c<'0'||c>'9')throw std::invalid_argument(error);
        std::int64_t value=0;
        do {
            value=value*10+(c-'0');
            if(value>std::numeric_limits<int>::max())throw std::invalid_argument(error);
            c=get();
        } while(c>='0'&&c<='9');
        if((negative&&value!=0)||(c!=EOF&&!std::isspace(c)))throw std::invalid_argument(error);
        return static_cast<int>(value);
    }
    bool done() {return token_start()==EOF;}
};

inline void build_incidence(Graph& g) {
    g.incident.resize(static_cast<std::size_t>(g.n));
    for(std::size_t i=0;i<g.edges.size();++i)
        for(int v:g.edges[i])g.incident[v].push_back(static_cast<int>(i));
}

inline Graph read_graph(const std::string& path,bool incidence=true) {
    GraphTextReader in(path);
    Graph g;
    g.n=in.nonnegative("invalid n m header");
    const int m=in.nonnegative("invalid n m header");
    if(g.n==std::numeric_limits<int>::max())
        throw std::invalid_argument("invalid n m header");
    g.raw_edges=static_cast<U64>(m);g.edges.reserve(static_cast<std::size_t>(m));
    for(int i=0;i<m;++i) {
        const int rank=in.nonnegative("invalid rank");
        std::vector<int> e(static_cast<std::size_t>(rank));
        for(int& v:e){v=in.nonnegative("invalid member");if(v>=g.n)throw std::invalid_argument("invalid member");}
        if(!std::is_sorted(e.begin(),e.end()))std::sort(e.begin(),e.end());
        const auto tail=std::unique(e.begin(),e.end());
        g.duplicate_members+=static_cast<U64>(e.end()-tail);e.erase(tail,e.end());
        if(e.size()<2)++g.small_edges;else g.edges.push_back(std::move(e));
    }
    if(!in.done())throw std::invalid_argument("extra input after edges");
    if(!std::is_sorted(g.edges.begin(),g.edges.end()))std::sort(g.edges.begin(),g.edges.end());
    const auto tail=std::unique(g.edges.begin(),g.edges.end());
    g.duplicate_edges=static_cast<U64>(g.edges.end()-tail);g.edges.erase(tail,g.edges.end());
    if(incidence)build_incidence(g);
    g.ranks.reserve(g.edges.size());
    for(const auto& edge:g.edges)g.ranks.push_back(static_cast<int>(edge.size()));
    std::sort(g.ranks.begin(),g.ranks.end());g.ranks.erase(std::unique(g.ranks.begin(),g.ranks.end()),g.ranks.end());
    return g;
}

// Number of distinct incident sets over a bounded number of other members.
inline int rank_capacity(const std::vector<int>& ranks,int peers,int threshold) {
    if(peers<=0)return 0;
    int degree=0;
    for(int rank:ranks) {
        if(rank>peers+1)break;
        long long count=1;
        for(int j=1;j<rank;++j)count=count*(peers-j+1)/j;
        if(count>=threshold-degree)return threshold;
        degree+=static_cast<int>(count);
    }
    return degree;
}

enum class Algorithm { Enum, Region, Budget, Joint, A1, A2, A3, SECore };

struct Query {std::vector<int> members;int k=1,max_size=3;};

#include "seed_profile.inc"
#include "rank_index.hpp"

struct Counters {
    U64 target_sizes=0,completed_target_sizes=0,search_nodes=0,branch_nodes=0;
    U64 candidate_checks=0,candidate_failure_checks=0,candidate_support_rejections=0;
    U64 contexts=0,edge_scans=0,member_tests=0,pair_updates=0;
    U64 support_rounds=0,support_prunes=0,support_vertices_removed=0;
    U64 initial_component_calls=0,strong_component_calls=0,component_edge_scans=0,component_union_attempts=0;
    U64 connectivity_prunes=0,component_vertices_removed=0,region_rounds=0;
    U64 rank_two_calls=0,rank_two_successes=0,bulk_calls=0,bulk_union_attempts=0,bulk_leaf_checks=0;
    U64 budget_edges_excluded=0,capacity_pair_checks=0,capacity_edge_scans=0,capacity_prunes=0;
    U64 degree_capacity_checks=0,degree_capacity_prunes=0,degree_forced_members=0;
    U64 forced_members=0,forced_budget_prunes=0,cardinality_prunes=0;
    U64 joint_checks=0,joint_edge_scans=0,joint_prunes=0;
    U64 seed_profile_inspections=0,seed_profile_activations=0,early_candidate_attempts=0,early_candidate_successes=0,subset_edge_lookups=0;
    U64 published_dense_targets=0,published_sparse_targets=0;
    U64 unusable_rank_edges=0;
    U64 projection_domain_calls=0,projection_domain_removed=0;
    U64 scalar_forced=0,obligation_skips=0,closed_form_skips=0;
    U64 factor_calls=0,factor_used=0,factor_prunes=0,factor_subsets=0,factor_transitions=0,
        truncated_compilations=0,truncated_prunes=0,truncated_subsets=0;
    U64 deadline_checks=0,peak_depth=0;
    U64 cross_joint_checks=0,cross_joint_prunes=0,dominated_skips=0;
    U64 active_skips=0,activation_branches=0,clause_prunes=0,clause_exclusions=0;
    U64 multiplicity_prunes=0,compatibility_prunes=0,repair_compilations=0,repair_cache_hits=0,repair_prunes=0,repair_subsets=0,repair_forced=0;
    U64 index_core_checks=0,index_core_rejections=0,index_core_removed=0,index_root_edges=0,index_leaf_edges=0;
    U64 failure_projection_builds=0,failure_projection_edges=0,failure_projection_tight=0;
    U64 failure_projection_nodes=0,failure_projection_removed=0,failure_projection_prunes=0;
    U64 optional_builds=0,optional_edges=0,optional_records=0,optional_nodes=0,optional_prunes=0,optional_removed=0;
    U64 cap_attempts=0,cap_rejects=0,cap_removed=0,cap_incidence_scans=0;
    U64 activation_reused=0,cap_skipped_sizes=0;
    U64 switch_pairs=0,switch_masks=0,seed_partition_prunes=0,frontier_screen_prunes=0;
    double bulk_ms=0;
};

struct Result {
    Query query;
    std::string status;
    bool complete=false,optimal=false,feasible=false;
    int lower_bound=3,effective_max_size=3,initial_lower_bound=3;
    std::vector<int> witness;
    Counters counters;
    double elapsed_ms=0,time_limit_ms=0;
};

class DSU {
    std::vector<int> p,size;
public:
    explicit DSU(int n):p(n),size(n,1){std::iota(p.begin(),p.end(),0);}
    int find(int v){while(p[v]!=v){p[v]=p[p[v]];v=p[v];}return v;}
    void unite(int a,int b){a=find(a);b=find(b);if(a==b)return;if(size[a]<size[b])std::swap(a,b);p[b]=a;size[a]+=size[b];}
};

class Solver {
    friend struct RepairProbe; // Access only for independent kernel verification.
    const Graph& graph;
    const RankIndex* index;
    const bool failure_projection,optional_projection,cap_certificate,direct_completion;
    Algorithm algorithm;
    Result result;
    Clock::time_point start;
    double limit_ms;
    int target=3;
    struct Timeout{};
    struct State {
        std::vector<unsigned char> selected,allowed;
        std::vector<int> edge_ids;
        std::vector<std::vector<int>> forbidden;
        int selected_count=0,allowed_count=0;
    };
    struct Context {
        std::vector<int> original,local,edge_ids,degree,sigma,missing_cost;
        std::vector<std::vector<int>> edges,incident;
    };

    void deadline() {
        ++result.counters.deadline_checks;
        if(limit_ms>0&&std::chrono::duration<double,std::milli>(Clock::now()-start).count()>=limit_ms)throw Timeout{};
    }

    bool budget_enabled()const{return algorithm!=Algorithm::Enum&&algorithm!=Algorithm::Region;}

    // The capped binomial cannot overflow: before multiplication c<cap<=INT_MAX
    // and N<=INT_MAX. Symmetry makes every intermediate coefficient increase.
    static U64 choose_cap(int N,int j,U64 cap) {
        if(j<0||j>N)return 0;
        j=std::min(j,N-j);U64 c=1;
        for(int i=1;i<=j;++i){c=c*static_cast<U64>(N-j+i)/static_cast<U64>(i);if(c>=cap)return cap;}
        return c;
    }

    int cardinality_lower_bound(int cap) {
        const int base=std::max(3,static_cast<int>(result.query.members.size()));
        if(graph.ranks.empty())return std::max(base,cap+1);
        for(int t=base;t<=cap;++t) {
            result.lower_bound=t;
            if((t&63)==0)deadline();
            U64 count=0;
            for(int r:graph.ranks) {
                if(r>=t)break;
                count+=choose_cap(t-2,r-1,static_cast<U64>(result.query.k));
                if(count>=static_cast<U64>(result.query.k))return t;
            }
        }
        return std::max(base,cap+1);
    }

    #include "indexed_search.inc"
    #include "failure_projection.inc"
    #include "optional_failure.inc"
    #include "cap_certificate.inc"

    Context context(const State& state,bool filter_budget) {
        ++result.counters.contexts;
        Context c;c.local.assign(graph.n,-1);
        for(int v=0;v<graph.n;++v)if(state.allowed[v]){c.local[v]=static_cast<int>(c.original.size());c.original.push_back(v);}
        c.incident.resize(c.original.size());
        const int b=target-state.selected_count;
        for(int eid:state.edge_ids) {
            if((result.counters.edge_scans&255)==0)deadline();
            ++result.counters.edge_scans;
            const auto& e=graph.edges[eid];
            if(filter_budget&&e.size()>=static_cast<std::size_t>(target)){++result.counters.unusable_rank_edges;continue;}
            bool allowed=true;int missing=0;
            for(int v:e){++result.counters.member_tests;if(!state.allowed[v]){allowed=false;break;}missing+=!state.selected[v];}
            if(!allowed)continue;
            if(filter_budget&&missing>b){++result.counters.budget_edges_excluded;continue;}
            const int id=static_cast<int>(c.edges.size());c.edge_ids.push_back(eid);c.missing_cost.push_back(missing);c.edges.emplace_back();
            auto& local_edge=c.edges.back();local_edge.reserve(e.size());
            for(int v:e){const int u=c.local[v];local_edge.push_back(u);c.incident[u].push_back(id);}
        }
        return c;
    }

    void support(Context& c) {
        ++result.counters.support_rounds;
        const int n=static_cast<int>(c.original.size());c.degree.assign(n,0);c.sigma.assign(n,0);
        if(n<=512) {
            std::vector<int> pair(static_cast<std::size_t>(n)*n,0);
            for(const auto& e:c.edges) {
                deadline();
                for(int v:e){++c.degree[v];for(int w:e)if(w!=v){++pair[static_cast<std::size_t>(v)*n+w];++result.counters.pair_updates;}}
            }
            for(int v=0;v<n;++v){int maximum=0;for(int w=0;w<n;++w)maximum=std::max(maximum,pair[static_cast<std::size_t>(v)*n+w]);c.sigma[v]=c.degree[v]-maximum;}
        } else {
            std::vector<std::unordered_map<int,int>> pair(n);
            for(const auto& e:c.edges) {
                deadline();
                for(int v:e){++c.degree[v];for(int w:e)if(w!=v){++pair[v][w];++result.counters.pair_updates;}}
            }
            for(int v=0;v<n;++v){int maximum=0;for(const auto& p:pair[v])maximum=std::max(maximum,p.second);c.sigma[v]=c.degree[v]-maximum;}
        }
    }

    // Returns false precisely when mandatory surviving members occupy different
    // components. Otherwise removes every other component, retaining failed x.
    bool restrict_component(State& state,const Context& c,int failed,bool& changed) {
        if(failed<0)++result.counters.initial_component_calls;else ++result.counters.strong_component_calls;
        const int n=static_cast<int>(c.original.size());DSU d(n);
        for(const auto& e:c.edges) {
            if((result.counters.component_edge_scans&255)==0)deadline();
            ++result.counters.component_edge_scans;
            if(failed>=0&&std::binary_search(e.begin(),e.end(),failed))continue;
            for(std::size_t i=1;i<e.size();++i){d.unite(e[0],e[i]);++result.counters.component_union_attempts;}
        }
        int component=-1;
        for(int i=0;i<n;++i)if(i!=failed&&state.selected[c.original[i]]) {
            const int root=d.find(i);
            if(component<0)component=root;
            else if(component!=root){++result.counters.connectivity_prunes;return false;}
        }
        if(component<0)throw std::logic_error("missing selected survivor");
        for(int i=0;i<n;++i)if(i!=failed&&d.find(i)!=component) {
            const int v=c.original[i];
            if(state.selected[v])throw std::logic_error("selected member removed by component");
            state.allowed[v]=0;--state.allowed_count;++result.counters.component_vertices_removed;changed=true;
        }
        return true;
    }

    // Necessary budget certificate for every selected member / possible failed
    // member pair. The cost prefix counts missing-member incidences, including
    // overlaps. Top-b incidence capacity is an upper bound on any chosen set.
    bool capacity(State& state,const Context& c,bool& changed) {
        const int n=static_cast<int>(c.original.size()),b=target-state.selected_count;
        std::vector<unsigned char> selected(n,0);
        for(int i=0;i<n;++i)selected[i]=state.selected[c.original[i]];
        std::vector<int> load(n),hist(static_cast<std::size_t>(b)+1),values;
        values.reserve(n);
        for(int v=0;v<n;++v)if(selected[v]) {
            std::vector<int> active_codegree(n,0),all_codegree(n,0);int active_degree=0;
            if(algorithm==Algorithm::A1||algorithm==Algorithm::A2||algorithm==Algorithm::A3) {
                for(int eid:c.incident[v]) {const auto& e=c.edges[eid];bool complete=true;
                    for(int u:e)++all_codegree[u];
                    for(int u:e)if(!selected[u]){complete=false;break;}
                    if(complete){++active_degree;for(int u:e)++active_codegree[u];}
                }
            }
            for(int failed=-1;failed<n;++failed)if(failed!=v) {
                deadline();
                if((algorithm==Algorithm::A1||algorithm==Algorithm::A2||algorithm==Algorithm::A3)&&failed>=0&&all_codegree[failed]<=1){++result.counters.dominated_skips;continue;}
                if((algorithm==Algorithm::A1||algorithm==Algorithm::A2||algorithm==Algorithm::A3)&&
                   active_degree-(failed<0?0:active_codegree[failed])>=result.query.k+(failed<0?1:0)) {
                    ++result.counters.active_skips;continue;
                }
                if(failed<0)++result.counters.degree_capacity_checks;
                else ++result.counters.capacity_pair_checks;
                const U64 required=static_cast<U64>(result.query.k)+(failed<0?1:0);
                std::fill(load.begin(),load.end(),0);std::fill(hist.begin(),hist.end(),0);
                int active=0,potential=0;
                for(int eid:c.incident[v]) {
                    ++result.counters.capacity_edge_scans;
                    const auto& e=c.edges[eid];
                    if(failed>=0&&std::binary_search(e.begin(),e.end(),failed))continue;
                    const int cost=c.missing_cost[eid];
                    if(cost==0){++active;continue;}
                    if(cost>b)throw std::logic_error("unfiltered budget edge");
                    ++potential;++hist[cost];for(int u:e)if(!selected[u])++load[u];
                }
                if(static_cast<U64>(active)>=required)continue;
                const U64 deficit=required-static_cast<U64>(active);
                if(static_cast<U64>(potential)<deficit){
                    if(failed<0)++result.counters.degree_capacity_prunes;else ++result.counters.capacity_prunes;
                    return false;
                }
                U64 minimum_cost=0;int left=static_cast<int>(deficit);
                for(int cost=1;cost<=b&&left;++cost){const int take=std::min(left,hist[cost]);minimum_cost+=static_cast<U64>(take)*cost;left-=take;}
                values.clear();for(int u=0;u<n;++u)if(!selected[u])values.push_back(load[u]);
                if(static_cast<int>(values.size())<b)throw std::logic_error("invalid optional budget");
                if(b<static_cast<int>(values.size()))std::nth_element(values.begin(),values.begin()+b,values.end(),std::greater<int>());
                U64 capacity_sum=0;for(int i=0;i<b;++i)capacity_sum+=static_cast<U64>(values[i]);
                if(minimum_cost>capacity_sum){
                    if(failed<0)++result.counters.degree_capacity_prunes;else ++result.counters.capacity_prunes;
                    return false;
                }
                // Any member whose exclusion leaves fewer than k possible
                // supporting edges is mandatory. Rebuild after these additions.
                const int available=active+potential;
                for(int u=0;u<n;++u)if(!selected[u]&&static_cast<U64>(available-load[u])<required) {
                    state.selected[c.original[u]]=1;++state.selected_count;++result.counters.forced_members;changed=true;
                    if(failed<0)++result.counters.degree_forced_members;
                }
                if(changed) {
                    if(state.selected_count>target){++result.counters.forced_budget_prunes;return false;}
                    return true;
                }
            }
        }
        return true;
    }

    // Joint constraints compete for the same optional-member budget. For one
    // selected v, consider prefixes of its 2--4 tightest failure constraints.
    // An edge contributes its number of satisfied constraints as an integer
    // weight. For missing-cost r, top-b weighted incidence / r upper-bounds
    // the gain of all simultaneously completable edges in that cost bucket.
    #include "advanced.inc"

    bool joint_capacity(const State& state,const Context& c) {
        const int n=static_cast<int>(c.original.size()),b=target-state.selected_count;
        std::vector<unsigned char> selected(n,0);
        for(int i=0;i<n;++i)selected[i]=state.selected[c.original[i]];
        for(int v=0;v<n;++v)if(selected[v]) {
            std::vector<int> codegree(n,0);
            for(int eid:c.incident[v])for(int u:c.edges[eid])if(u!=v)++codegree[u];
            std::vector<std::pair<int,int>> failures;
            for(int x=0;x<n;++x)if(x!=v)failures.push_back({c.degree[v]-codegree[x],x});
            std::sort(failures.begin(),failures.end());
            const int maximum=std::min(4,static_cast<int>(failures.size()));
            for(int number=2;number<=maximum;++number) {
                deadline();++result.counters.joint_checks;
                std::vector<std::vector<U64>> load(static_cast<std::size_t>(b)+1);
                std::vector<U64> weight_total(static_cast<std::size_t>(b)+1,0);
                U64 active=0;
                for(int eid:c.incident[v]) {
                    ++result.counters.joint_edge_scans;
                    const auto& e=c.edges[eid];int weight=0,cost=0;
                    for(int i=0;i<number;++i)weight+=!std::binary_search(e.begin(),e.end(),failures[i].second);
                    if(!weight)continue;
                    for(int u:e)cost+=!selected[u];
                    if(cost==0){active+=static_cast<U64>(weight);continue;}
                    if(cost>b)throw std::logic_error("unfiltered joint edge");
                    if(load[cost].empty())load[cost].assign(n,0);
                    weight_total[cost]+=static_cast<U64>(weight);
                    for(int u:e)if(!selected[u])load[cost][u]+=static_cast<U64>(weight);
                }
                const U64 required=static_cast<U64>(number)*static_cast<U64>(result.query.k);
                U64 upper=active;
                for(int cost=1;cost<=b&&upper<required;++cost)if(!load[cost].empty()) {
                    auto& row=load[cost];
                    if(b<n)std::nth_element(row.begin(),row.begin()+b,row.end(),std::greater<U64>());
                    U64 maximum_incidence=0;for(int i=0;i<b;++i)maximum_incidence+=row[i];
                    upper+=std::min(weight_total[cost],maximum_incidence/static_cast<U64>(cost));
                }
                if(upper<required){++result.counters.joint_prunes;return false;}
            }
        }
        return true;
    }

    bool short_failure_rows(State& state,const Context& c,bool& changed) {
        const int b=target-state.selected_count,k=result.query.k;
        if(b<1||b>12||(algorithm!=Algorithm::A2&&algorithm!=Algorithm::A3))return true;
        const int n=static_cast<int>(c.original.size());
        std::vector<unsigned char> selected(n,0),forced(n,0);
        for(int v=0;v<n;++v)selected[v]=state.selected[c.original[v]];
        std::vector<int> co(n);
        for(int v=0;v<n;++v)if(selected[v]) {
            deadline();
            std::fill(co.begin(),co.end(),0);
            int degree=0,worst=0,failed=-1;
            for(int id:c.incident[v])if(c.missing_cost[id]<b) {
                ++degree;
                for(int u:c.edges[id])if(!selected[u]&&++co[u]>worst){worst=co[u];failed=u;}
            }
            if(degree-worst<k){++result.counters.optional_prunes;return false;}
            if(algorithm==Algorithm::A3&&failed>=0&&degree-worst==k)
                for(int id:c.incident[v])if(c.missing_cost[id]<b&&
                    !std::binary_search(c.edges[id].begin(),c.edges[id].end(),failed))
                    for(int u:c.edges[id])if(!selected[u])forced[u]=1;
        }
        const int count=std::accumulate(forced.begin(),forced.end(),0);
        if(count>b){++result.counters.forced_budget_prunes;return false;}
        if(count) {
            for(int v=0;v<n;++v)if(forced[v])state.selected[c.original[v]]=1;
            state.selected_count+=count;
            result.counters.repair_forced+=count;
            changed=true;
        }
        return true;
    }

    bool reduce(State& state,Context& c) {
        for(;;) {
            deadline();++result.counters.region_rounds;
            bool clause_changed=false;if(!propagate_clauses(state,clause_changed))return false;
            if(state.selected_count>target||state.allowed_count<target){++result.counters.cardinality_prunes;return false;}
            c=context(state,budget_enabled());state.edge_ids=c.edge_ids;
            bool changed=false;
            if(!short_failure_rows(state,c,changed))return false;
            if(changed)continue;
            support(c);
            for(std::size_t i=0;i<c.original.size();++i)if(c.sigma[i]<result.query.k) {
                const int v=c.original[i];
                if(state.selected[v]){++result.counters.support_prunes;return false;}
                state.allowed[v]=0;--state.allowed_count;++result.counters.support_vertices_removed;changed=true;
            }
            if(changed)continue;
            if(budget_enabled()){if(!capacity(state,c,changed))return false;if(changed)continue;}
            if(algorithm==Algorithm::Joint&&!joint_capacity(state,c))return false;
            if(!restrict_component(state,c,-1,changed))return false;
            if(changed)continue;
            if(c.original.size()<3){++result.counters.cardinality_prunes;return false;}
            rank_two_certificate::Stats rank_stats;++result.counters.rank_two_calls;
            if(rank_two_certificate::certify(static_cast<int>(c.original.size()),c.edges,rank_stats)) {
                ++result.counters.rank_two_successes;return true;
            }
            deadline();++result.counters.bulk_calls;const auto before=Clock::now();
            const auto cut=robust_bulk::find_cut(static_cast<int>(c.original.size()),c.edges,robust_bulk::Scheduler::Bulk,robust_bulk::Mode::Interval);
            result.counters.bulk_ms+=std::chrono::duration<double,std::milli>(Clock::now()-before).count();
            result.counters.bulk_union_attempts+=cut.union_attempts;result.counters.bulk_leaf_checks+=cut.leaf_checks;deadline();
            if(cut.cut_vertex<0)return true;
            if(!restrict_component(state,c,cut.cut_vertex,changed))return false;
            if(!changed)throw std::logic_error("strong cut did not reduce allowed domain");
        }
    }

    bool small_candidate_valid(const std::vector<int>& ids) {
        deadline();++result.counters.candidate_checks;
        const int n=static_cast<int>(ids.size());if(n<3||n!=target||n>6)return false;
        std::vector<std::vector<int>> active;std::vector<int> degree(n,0),co(static_cast<std::size_t>(n)*n,0);
        for(int mask=1;mask<(1<<n);++mask) {
            if((mask&15)==0)deadline();if(__builtin_popcount(static_cast<unsigned>(mask))<2)continue;
            std::vector<int> original,local;
            for(int j=0;j<n;++j)if(mask&(1<<j)){original.push_back(ids[j]);local.push_back(j);}
            ++result.counters.subset_edge_lookups;
            if(!std::binary_search(graph.edges.begin(),graph.edges.end(),original))continue;
            for(int u:local){++degree[u];for(int v:local)if(u!=v)++co[static_cast<std::size_t>(u)*n+v];}
            active.push_back(std::move(local));
        }
        for(int u=0;u<n;++u){int worst=0;for(int v=0;v<n;++v)if(u!=v)worst=std::max(worst,co[static_cast<std::size_t>(u)*n+v]);
            if(degree[u]-worst<result.query.k){++result.counters.candidate_support_rejections;return false;}}
        for(int failed=-1;failed<n;++failed){deadline();if(failed>=0)++result.counters.candidate_failure_checks;DSU d(n);
            for(const auto& e:active){if(failed>=0&&std::binary_search(e.begin(),e.end(),failed))continue;
                for(std::size_t j=1;j<e.size();++j)d.unite(e[0],e[j]);}
            int root=-1;for(int u=0;u<n;++u)if(u!=failed){if(root<0)root=d.find(u);else if(d.find(u)!=root)return false;}}
        deadline();result.witness=ids;return true;
    }

    bool early_certificate(const State& state) {
        const int need=target-static_cast<int>(result.query.members.size());
        if(target>6||need<0||need>2)return false;
        if(need==0)return small_candidate_valid(result.query.members);
        std::vector<U64> weight(graph.n,0);std::vector<int> hits(graph.n,0);
        for(int q:result.query.members){std::vector<unsigned char> seen(graph.n,0);
            for(int eid:graph.incident[q]){deadline();const auto& e=graph.edges[eid];if(e.size()>=static_cast<std::size_t>(target))continue;
                for(int u:e)if(state.allowed[u]&&!state.selected[u]){weight[u]+=1024ULL/static_cast<U64>(e.size()-1);if(!seen[u]){seen[u]=1;++hits[u];}}}}
        std::vector<int> members;for(int u=0;u<graph.n;++u)if(hits[u]==static_cast<int>(result.query.members.size()))members.push_back(u);
        std::sort(members.begin(),members.end(),[&](int a,int b){return weight[a]!=weight[b]?weight[a]>weight[b]:a<b;});
        if(need==1&&members.size()>64)members.resize(64);
        if(need==2&&members.size()>12)members.resize(12);
        auto check=[&](int a,int b){auto ids=result.query.members;ids.push_back(a);if(b>=0)ids.push_back(b);std::sort(ids.begin(),ids.end());
            ++result.counters.early_candidate_attempts;if(small_candidate_valid(ids)){++result.counters.early_candidate_successes;return true;}return false;};
        if(need==1){for(int u:members)if(check(u,-1))return true;return false;}
        std::vector<std::pair<int,int>> pairs;
        for(int a=0;a<static_cast<int>(members.size());++a)for(int b=a+1;b<static_cast<int>(members.size());++b)pairs.push_back({a,b});
        std::stable_sort(pairs.begin(),pairs.end(),[](const auto& a,const auto& b){return a.first+a.second<b.first+b.second;});
        int count=0;for(const auto& pair:pairs){if(count++>=64)break;if(check(members[pair.first],members[pair.second]))return true;}
        return false;
    }

    bool candidate_valid(const std::vector<unsigned char>& mask) {
        if(target<=6){std::vector<int> ids;for(int u=0;u<graph.n;++u)if(mask[u])ids.push_back(u);return small_candidate_valid(ids);}
        deadline();++result.counters.candidate_checks;
        State s;s.allowed=mask;s.selected=mask;
        for(unsigned char x:mask)s.allowed_count+=x;
        s.selected_count=s.allowed_count;
        if(s.allowed_count<3||s.allowed_count>target)return false;
        if (index) {
            for (int v=0; v<graph.n; ++v) if (mask[v]) {
                const auto& ids=index->owned[v];
                const auto end=index->below(ids,target);
                for (auto it=ids.begin(); it!=end; ++it) {
                    if ((result.counters.index_leaf_edges&255)==0) deadline();
                    ++result.counters.index_leaf_edges;
                    s.edge_ids.push_back(*it);
                }
            }
        } else {
            s.edge_ids.resize(graph.edges.size());std::iota(s.edge_ids.begin(),s.edge_ids.end(),0);
        }
        Context c=context(s,false);support(c);
        for(int value:c.sigma)if(value<result.query.k){++result.counters.candidate_support_rejections;return false;}
        const int n=static_cast<int>(c.original.size());
        // Independent direct whole-edge deletion checks for every candidate;
        // positive rank-two and bulk certificates are not used here.
        for(int failed=-1;failed<n;++failed) {
            deadline();if(failed>=0)++result.counters.candidate_failure_checks;
            DSU d(n);
            for(const auto& e:c.edges) {
                ++result.counters.component_edge_scans;
                if(failed>=0&&std::binary_search(e.begin(),e.end(),failed))continue;
                for(std::size_t i=1;i<e.size();++i){d.unite(e[0],e[i]);++result.counters.component_union_attempts;}
            }
            int root=-1;
            for(int v=0;v<n;++v)if(v!=failed){if(root<0)root=d.find(v);else if(d.find(v)!=root)return false;}
        }
        deadline();result.witness=c.original;return true;
    }

    std::vector<U64> member_scores(const State& state,const Context& c) {
        std::vector<U64> score(c.original.size(),0);
        for(const auto& e:c.edges) {
            int active=0,missing=0;
            for(int v:e){active+=state.selected[c.original[v]];missing+=!state.selected[c.original[v]];}
            if(active==0||missing==0)continue;
            const U64 increment=static_cast<U64>(active)*(1024ULL+static_cast<U64>(missing)-1)/static_cast<U64>(missing);
            for(int v:e)if(!state.selected[c.original[v]])score[v]+=increment;
        }
        return score;
    }

    int branch_member(const State& state,const Context& c) {
        const auto score=member_scores(state,c);
        int best=-1;
        for(std::size_t i=0;i<c.original.size();++i)if(!state.selected[c.original[i]]) {
            if(best<0||score[i]>score[best]||(score[i]==score[best]&&c.degree[i]>c.degree[best]))best=static_cast<int>(i);
        }
        if(best<0)throw std::logic_error("branch without optional member");
        return c.original[best];
    }

    bool dfs(State state,U64 depth) {
        deadline();++result.counters.search_nodes;result.counters.peak_depth=std::max(result.counters.peak_depth,depth);
        bool clause_changed=false;if(!propagate_clauses(state,clause_changed))return false;
        if(!project_state(state,depth==0)){++result.counters.failure_projection_prunes;return false;}
        if(!optional_state(state))return false;
        if(state.selected_count>target||state.allowed_count<target){++result.counters.cardinality_prunes;return false;}
        if(state.selected_count==target)return candidate_valid(state.selected);
        if(state.allowed_count==target){if(!state.forbidden.empty())return false;return candidate_valid(state.allowed);}
        Context c;if(!reduce(state,c))return false;
        if(algorithm==Algorithm::A3&&direct_completion&&state.forbidden.empty()) {
            const int budget=target-state.selected_count;
            const int width=state.allowed_count-state.selected_count;
            if(budget>0&&budget<=5&&width<=12&&choose_cap(width,budget,65)<=64) {
                std::vector<int> options;
                for(int v:c.original)if(!state.selected[v])options.push_back(v);
                return enumerate(state.selected,options,0,budget,depth);
            }
        }
        if(state.selected_count==target)return candidate_valid(state.selected);
        if(state.allowed_count==target){if(!state.forbidden.empty())return false;return candidate_valid(state.allowed);}
        if(algorithm==Algorithm::A1||algorithm==Algorithm::A2||algorithm==Algorithm::A3) {
            std::vector<std::vector<int>> choices;std::vector<int> forced;
            if(!advanced(state,c,choices,forced))return false;
            if(!forced.empty()) {
                for(int u:forced)if(!state.selected[u]){state.selected[u]=1;++state.selected_count;++result.counters.repair_forced;}
                return dfs(std::move(state),depth+1);
            }
            if(!choices.empty()) {
                const auto scores=member_scores(state,c);
                auto merit=[&](const std::vector<int>& a){U64 sum=0;for(int u:a)sum+=scores[c.local[u]];return sum;};
                std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& z){
                    if(a.size()!=z.size())return a.size()<z.size();
                    U64 x=merit(a),y=merit(z);return x!=y?x>y:a<z;
                });
                for(const auto& a:choices) {
                    State next=state;for(int u:a)if(!next.selected[u]){next.selected[u]=1;++next.selected_count;}
                    ++result.counters.activation_branches;
                    if(dfs(std::move(next),depth+1))return true;
                    if(a.size()==1){int u=a.front();if(state.allowed[u]){state.allowed[u]=0;--state.allowed_count;++result.counters.clause_exclusions;}}
                    else state.forbidden.push_back(a);
                }
                return false;
            }
        }
        const int v=branch_member(state,c);++result.counters.branch_nodes;
        State include=state;include.selected[v]=1;++include.selected_count;
        if(dfs(std::move(include),depth+1))return true;
        state.allowed[v]=0;--state.allowed_count;
        return dfs(std::move(state),depth+1);
    }

    bool root_boundary(State& state) {
        const int b=target-state.selected_count,k=result.query.k;
        if(b<2||b>12)return true;
        const int need=k-rank_capacity(graph.ranks,b-2,k);
        if(need<=0)return true;
        std::vector<int> degree(graph.n),worst(graph.n);
        std::vector<std::unordered_map<int,int>> codegree;
        if(algorithm!=Algorithm::A1)codegree.resize(graph.n);
        std::vector<int> missing;
        for(int v:result.query.members)for(int id:graph.incident[v]) {
            deadline();
            const auto& e=graph.edges[id];
            if(static_cast<int>(e.size())>=target)continue;
            missing.clear();
            bool usable=true;
            for(int u:e) {
                if(!state.allowed[u]||(state.selected[u]&&u<v)){usable=false;break;}
                if(!state.selected[u])missing.push_back(u);
            }
            if(!usable||static_cast<int>(missing.size())>=b)continue;
            for(int u:missing) {
                ++degree[u];
                if(!codegree.empty())for(int x:missing)if(x!=u)
                    worst[u]=std::max(worst[u],++codegree[u][x]);
            }
        }
        for(int v=0;v<graph.n;++v)if(state.allowed[v]&&!state.selected[v]&&degree[v]-worst[v]<need) {
            state.allowed[v]=0;
            --state.allowed_count;
            ++result.counters.optional_removed;
        }
        return state.allowed_count>=target;
    }

    bool root_projection_domain(State& state) {
        int delta=2;
        for(;delta<target;++delta){U64 upper=0;for(int r:graph.ranks)if(r<=target){upper+=choose_cap(delta-1,r-1,result.query.k);if(upper>=static_cast<U64>(result.query.k))break;}
            if(upper>=static_cast<U64>(result.query.k))break;}
        if(delta>=target)return false;
        if(target>2*delta+1)return true;
        ++result.counters.projection_domain_calls;
        std::vector<unsigned char> seen_vertex(graph.n,0),seen_edge(graph.edges.size(),0);
        std::vector<int> frontier{result.query.members.front()};seen_vertex[frontier.front()]=1;
        for(int round=0;round<2;++round){std::vector<int> next;
            for(int u:frontier){deadline();for(int eid:graph.incident[u]) {
                if(seen_edge[eid]||graph.edges[eid].size()>=static_cast<std::size_t>(target))continue;
                seen_edge[eid]=1;
                for(int v:graph.edges[eid])if(!seen_vertex[v]){seen_vertex[v]=1;next.push_back(v);}
            }}
            frontier=std::move(next);
        }
        for(int u:result.query.members)if(!seen_vertex[u])return false;
        for(int u=0;u<graph.n;++u)if(!seen_vertex[u]&&state.allowed[u]){state.allowed[u]=0;--state.allowed_count;++result.counters.projection_domain_removed;}
        return state.allowed_count>=target;
    }

    bool enumerate(std::vector<unsigned char>& mask,const std::vector<int>& optional,int pos,int remaining,U64 depth) {
        deadline();++result.counters.search_nodes;result.counters.peak_depth=std::max(result.counters.peak_depth,depth);
        if(remaining==0)return candidate_valid(mask);
        if(static_cast<int>(optional.size())-pos<remaining){++result.counters.cardinality_prunes;return false;}
        for(int i=pos;i<=static_cast<int>(optional.size())-remaining;++i) {
            mask[optional[i]]=1;
            if(enumerate(mask,optional,i+1,remaining-1,depth+1))return true;
            mask[optional[i]]=0;
        }
        return false;
    }

public:
    Solver(const Graph& g,Algorithm a,double time_limit_ms,const RankIndex* idx=nullptr,bool projection=false,bool optional=false,bool cap=false,bool direct_completion=true)
        :graph(g),index(idx),failure_projection(projection),optional_projection(optional),cap_certificate(cap),direct_completion(direct_completion),algorithm(a),limit_ms(time_limit_ms) {
        if(limit_ms<0)throw std::invalid_argument("negative time limit");
    }

    Result run(Query query) {
        start=Clock::now();result=Result{};result.time_limit_ms=limit_ms;
        if(query.k<1||query.max_size<0)throw std::invalid_argument("invalid query threshold or size cap");
        for(int v:query.members)if(v<0||v>=graph.n)throw std::invalid_argument("invalid query member");
        std::sort(query.members.begin(),query.members.end());query.members.erase(std::unique(query.members.begin(),query.members.end()),query.members.end());
        if(query.members.size()<2)throw std::invalid_argument("query needs at least two distinct members");
        result.query=std::move(query);const int cap=std::min(graph.n,result.query.max_size);result.effective_max_size=cap;
        target=std::max(3,static_cast<int>(result.query.members.size()));result.initial_lower_bound=result.lower_bound=target;
        try {
            target=cardinality_lower_bound(cap);
            if(algorithm==Algorithm::A1||algorithm==Algorithm::A2||algorithm==Algorithm::A3){
                auto tick=[&](){deadline();};
                auto profile=index ? index->seed_bound(result.query,cap,target,tick) : seed_rank_bound(graph,result.query,cap,target,tick);
                result.counters.seed_profile_inspections+=profile.inspected;result.counters.seed_profile_activations+=profile.activated;
                target=std::max(target,profile.lower);
            }
            result.initial_lower_bound=result.lower_bound=target;
            if(target>cap){result.status="infeasible";result.complete=true;result.lower_bound=std::max(target,cap+1);
                result.elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();return result;}
            deadline();
            State initial;initial.selected.assign(graph.n,0);initial.allowed.assign(graph.n,1);
            initial.selected_count=static_cast<int>(result.query.members.size());initial.allowed_count=graph.n;
            for(int v:result.query.members)initial.selected[v]=1;
            if (!index) {
                initial.edge_ids.resize(graph.edges.size());std::iota(initial.edge_ids.begin(),initial.edge_ids.end(),0);
            }
            std::vector<int> optional;
            if(algorithm==Algorithm::Enum)for(int v=0;v<graph.n;++v)if(!initial.selected[v])optional.push_back(v);
            bool cap_checked=false;
            for(;target<=cap;++target) {
                deadline();result.lower_bound=target;++result.counters.target_sizes;
                bool found;
                if(algorithm==Algorithm::Enum){auto mask=initial.selected;found=enumerate(mask,optional,0,target-initial.selected_count,0);}
                else {
                    State working=initial;
                    const bool use_projection=algorithm==Algorithm::A1||algorithm==Algorithm::A2||algorithm==Algorithm::A3;
                    found=false;
                    if ((!index||indexed_root(working)) && (!use_projection||root_projection_domain(working))) {
                        found=use_projection&&early_certificate(working);
                        if(!found&&(!use_projection||root_boundary(working))) {
                            if(cap_certificate&&!cap_checked) {
                                cap_checked=true;
                                if(!cap_certificates(initial,cap)) {
                                    result.status="infeasible";result.complete=true;result.lower_bound=cap+1;
                                    break;
                                }
                                for(int v=0;v<graph.n;++v)if(working.allowed[v]&&!initial.allowed[v]) {
                                    working.allowed[v]=0;
                                    --working.allowed_count;
                                }
                            }
                            if(working.allowed_count>=target) {
                                if(index)indexed_root_edges(working);
                                if(compile_projections(working)&&compile_optional(working))found=dfs(std::move(working),0);
                                else ++result.counters.failure_projection_prunes;
                            }
                        }
                    }
                }
                if(found){result.status="optimal";result.complete=result.optimal=result.feasible=true;result.lower_bound=static_cast<int>(result.witness.size());break;}
                ++result.counters.completed_target_sizes;result.lower_bound=target+1;
            }
            if(result.status.empty()){result.status="infeasible";result.complete=true;result.lower_bound=std::max(result.lower_bound,cap+1);}
        } catch(const Timeout&) {result.status="timeout";result.complete=false;result.optimal=false;result.feasible=!result.witness.empty();}
        result.elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();return result;
    }
};

} // namespace robust_local_v14
