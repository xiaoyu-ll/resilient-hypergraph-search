// A pair's sparse blockers encode the projections for every possible failure.
struct BlockedPair {int neighbor,failed;};
std::vector<std::vector<BlockedPair>> optional_blockers;
std::vector<int> optional_slack, narrow_failures;

bool compile_optional(const State& state) {
    optional_blockers.clear();
    narrow_failures.clear();
    if(!optional_projection||projection_layers.size()<2)return true;
    const int n=static_cast<int>(projection_members.size()), base=projection_layers.front().slack;
    const auto cells=static_cast<std::size_t>(n)*n;
    std::vector<int> first(cells,-1);
    std::vector<unsigned> common(cells,0);
    std::vector<std::array<int,12>> incident(n);
    std::array<int,12> rank_counts{};
    ++result.counters.optional_builds;
    for(int id:state.edge_ids) {
        if((result.counters.optional_edges++&127)==0)deadline();
        const auto& edge=graph.edges[id];
        ++rank_counts[edge.size()];
        const unsigned all=(1u<<edge.size())-1;
        for(std::size_t i=0;i<edge.size();++i) {
            const int u=projection_local[edge[i]];
            ++incident[u][edge.size()];
            for(std::size_t j=i+1;j<edge.size();++j) {
                const auto cell=static_cast<std::size_t>(u)*n+projection_local[edge[j]];
                if(first[cell]<0) {
                    first[cell]=id;
                    common[cell]=all&~((1u<<i)|(1u<<j));
                } else if(common[cell]) {
                    const auto& previous=graph.edges[first[cell]];
                    for(unsigned bits=common[cell];bits;bits&=bits-1) {
                        const int bit=__builtin_ctz(bits);
                        if(!std::binary_search(edge.begin(),edge.end(),previous[bit]))
                            common[cell]&=~(1u<<bit);
                    }
                }
            }
        }
    }
    optional_slack.assign(n,base);
    for(int x=0;x<n;++x) {
        std::vector<int> ranks;
        for(int rank=2;rank<target;++rank)
            if(rank_counts[rank]>incident[x][rank])ranks.push_back(rank);
        const int slack=target-2-minimum_projection_degree(ranks,target-2);
        if(slack<0) {
            if(!state.selected[projection_members[x]])++result.counters.optional_prunes;
            return false;
        }
        optional_slack[x]=slack;
        if(slack<base)narrow_failures.push_back(x);
    }
    optional_blockers.resize(n);
    U64 records=0;
    for(int u=0;u<n;++u) {
        if((u&31)==0)deadline();
        for(int v=u+1;v<n;++v) {
            const auto cell=static_cast<std::size_t>(u)*n+v;
            if(!common[cell])continue;
            const auto& origin=graph.edges[first[cell]];
            for(unsigned bits=common[cell];bits;bits&=bits-1) {
                const int x=projection_local[origin[__builtin_ctz(bits)]];
                optional_blockers[u].push_back({v,x});
                optional_blockers[v].push_back({u,x});
                if(++records>250000) {
                    optional_blockers.clear();
                    return true;
                }
            }
        }
    }
    result.counters.optional_records+=records;
    return true;
}

bool optional_state(State& state) {
    if(optional_blockers.empty())return true;
    ++result.counters.optional_nodes;
    const int n=static_cast<int>(projection_members.size());
    std::vector<U64> live(projection_words,0),chosen(projection_words,0);
    std::vector<int> selected;
    for(int i=0;i<n;++i) {
        const int id=projection_members[i];
        if(state.allowed[id])live[i/64]|=U64{1}<<(i%64);
        if(state.selected[id]) {
            chosen[i/64]|=U64{1}<<(i%64);
            selected.push_back(i);
        }
    }
    auto present=[](const std::vector<U64>& bits,int v){return (bits[v/64]>>(v%64))&1;};
    const auto& intact=projection_layers.front();
    std::vector<int> fixed_loss(n,0),possible_loss(n,0),touched;
    std::vector<unsigned char> marked(n,0);
    for(int v:selected) {
        deadline();
        const auto* row=projection_row(intact,v);
        auto adjacent=[&](int u){return (row[u/64]>>(u%64))&1;};
        int missing=-1,degree=0;
        for(int w=0;w<projection_words;++w) {
            missing+=bit_count(chosen[w]&~row[w]);
            degree+=bit_count(live[w]&row[w]);
        }
        auto mark=[&](int x){
            if(!marked[x]){marked[x]=1;touched.push_back(x);}
        };
        for(int x:narrow_failures)if(x!=v)mark(x);
        for(const auto& pair:optional_blockers[v])if(present(live,pair.neighbor)) {
            mark(pair.failed);
            ++possible_loss[pair.failed];
            if(present(chosen,pair.neighbor))++fixed_loss[pair.failed];
        }
        int only_exception=-1;
        bool needs_intact_neighbor=false;
        for(int x:touched) {
            // The layer bound applies even after x has been excluded.
            const int defects=missing-(present(chosen,x)&&!adjacent(x))+fixed_loss[x];
            const int neighbors=degree-(present(live,x)&&adjacent(x))-possible_loss[x];
            const int required=target-2-optional_slack[x]+!present(live,x);
            if(defects>optional_slack[x]||neighbors<required) {
                ++result.counters.optional_prunes;
                return false;
            }
            if(defects==optional_slack[x]) {
                only_exception=needs_intact_neighbor ? -1 : x;
                needs_intact_neighbor=true;
            }
        }
        if(needs_intact_neighbor)for(int w=0;w<projection_words;++w) {
            U64 exception=only_exception>=0&&only_exception/64==w ? U64{1}<<(only_exception%64) : 0;
            live[w]&=row[w]|chosen[w]|exception;
        }
        for(const auto& pair:optional_blockers[v])if(present(live,pair.neighbor)&&!present(chosen,pair.neighbor)) {
            const int x=pair.failed;
            const int defects=missing-(present(chosen,x)&&!adjacent(x))+fixed_loss[x];
            if(defects==optional_slack[x])live[pair.neighbor/64]&=~(U64{1}<<(pair.neighbor%64));
        }
        for(int x:touched){marked[x]=0;fixed_loss[x]=possible_loss[x]=0;}
        touched.clear();
    }
    for(int v=0;v<n;++v)if(state.allowed[projection_members[v]]&&!present(live,v)) {
        state.allowed[projection_members[v]]=0;
        --state.allowed_count;
        ++result.counters.optional_removed;
    }
    if(state.allowed_count<target) {
        ++result.counters.optional_prunes;
        return false;
    }
    return true;
}
