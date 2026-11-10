// Query-local necessary projection constraints. All edge IDs originate in
// the affordable root of the current target.
struct ProjectionLayer {
    int failed=-1,degree=0,slack=0;
    std::vector<U64> adjacency;
};
std::vector<int> projection_members,projection_local;
std::vector<ProjectionLayer> projection_layers;
int projection_words=0;

static int bit_count(U64 bits) {return __builtin_popcountll(bits);}

const U64* projection_row(const ProjectionLayer& layer,int v) const {
    return layer.adjacency.data()+static_cast<std::size_t>(v)*projection_words;
}

void link(ProjectionLayer& layer,int u,int v) {
    layer.adjacency[static_cast<std::size_t>(u)*projection_words+v/64]|=U64{1}<<(v%64);
    layer.adjacency[static_cast<std::size_t>(v)*projection_words+u/64]|=U64{1}<<(u%64);
}

int minimum_projection_degree(const std::vector<int>& ranks,int limit) const {
    for(int d=1;d<=limit;++d) {
        U64 capacity=0;
        for(int r:ranks) {
            capacity+=choose_cap(d,r-1,result.query.k);
            if(capacity>=static_cast<U64>(result.query.k))return d;
        }
    }
    return limit+1;
}

bool compile_projections(const State& state) {
    projection_layers.clear();
    if(!failure_projection||target>12||state.allowed_count>1200||
       state.edge_ids.size()>45000)return true;
    std::vector<int> ranks;
    for(int r:graph.ranks)if(r<target)ranks.push_back(r);
    const int survivor=minimum_projection_degree(ranks,target-2);
    if(survivor>target-2)return false;
    if(target-2-survivor>3)return true;
    projection_members.clear();
    projection_local.assign(graph.n,-1);
    for(int v=0;v<graph.n;++v)if(state.allowed[v]){
        projection_local[v]=static_cast<int>(projection_members.size());
        projection_members.push_back(v);
    }
    const int n=static_cast<int>(projection_members.size());
    projection_words=(n+63)/64;
    const auto words=static_cast<std::size_t>(n)*projection_words;
    ++result.counters.failure_projection_builds;
    if(survivor==target-2) {
        // A pair that always occurs with a third member cannot remain a
        // projected pair after that member fails.
        const auto cells=static_cast<std::size_t>(n)*n;
        std::vector<int> first(cells,-1);
        std::vector<unsigned> blockers(cells,0);
        for(int eid:state.edge_ids) {
            if((result.counters.failure_projection_edges++&127)==0)deadline();
            const auto& edge=graph.edges[eid];
            const unsigned all=(1u<<edge.size())-1;
            for(std::size_t i=0;i<edge.size();++i) for(std::size_t j=i+1;j<edge.size();++j) {
                const auto key=static_cast<std::size_t>(projection_local[edge[i]])*n+projection_local[edge[j]];
                if(first[key]<0) {
                    first[key]=eid;
                    blockers[key]=all&~((1u<<i)|(1u<<j));
                } else if(blockers[key]) {
                    const auto& origin=graph.edges[first[key]];
                    for(unsigned active=blockers[key];active;active&=active-1) {
                        const int p=__builtin_ctz(active);
                        if(!std::binary_search(edge.begin(),edge.end(),origin[p]))blockers[key]&=~(1u<<p);
                    }
                }
            }
        }
        ProjectionLayer layer{-1,target-1,0,std::vector<U64>(words,0)};
        for(int u=0;u<n;++u) {
            if((u&31)==0)deadline();
            for(int v=u+1;v<n;++v) {
                const auto key=static_cast<std::size_t>(u)*n+v;
                if(first[key]>=0&&blockers[key]==0)link(layer,u,v);
            }
        }
        projection_layers.push_back(std::move(layer));
        ++result.counters.failure_projection_tight;
        return true;
    }

    projection_layers.push_back({-1,survivor+1,target-2-survivor,std::vector<U64>(words,0)});
    for(int x:result.query.members) {
        if(projection_layers.size()==5)break;
        projection_layers.push_back({projection_local[x],0,0,std::vector<U64>(words,0)});
    }
    std::vector<unsigned> seen_ranks(projection_layers.size(),0);
    for(int eid:state.edge_ids) {
        if((result.counters.failure_projection_edges++&127)==0)deadline();
        const auto& edge=graph.edges[eid];
        for(std::size_t i=0;i<projection_layers.size();++i) {
            auto& layer=projection_layers[i];
            if(layer.failed>=0&&std::binary_search(edge.begin(),edge.end(),projection_members[layer.failed]))continue;
            seen_ranks[i]|=1u<<edge.size();
            for(std::size_t j=0;j<edge.size();++j)for(std::size_t l=j+1;l<edge.size();++l)
                link(layer,projection_local[edge[j]],projection_local[edge[l]]);
        }
    }
    for(std::size_t i=1;i<projection_layers.size();++i) {
        ranks.clear();
        for(unsigned bits=seen_ranks[i];bits;bits&=bits-1)ranks.push_back(__builtin_ctz(bits));
        auto& layer=projection_layers[i];
        layer.degree=minimum_projection_degree(ranks,target-2);
        layer.slack=target-2-layer.degree;
        if(layer.slack<0)return false;
    }
    return true;
}

bool project_state(State& state,bool root) {
    if(projection_layers.empty())return true;
    ++result.counters.failure_projection_nodes;
    const int n=static_cast<int>(projection_members.size());
    std::vector<U64> allowed(projection_words,0),selected(projection_words,0);
    std::vector<int> mandatory;
    for(int v=0;v<n;++v) {
        const int id=projection_members[v];
        if(state.allowed[id])allowed[v/64]|=U64{1}<<(v%64);
        if(state.selected[id]) {
            selected[v/64]|=U64{1}<<(v%64);
            mandatory.push_back(v);
        }
    }
    std::vector<U64> excess(static_cast<std::size_t>(4)*projection_words,0);
    for(const auto& layer:projection_layers) {
        deadline();
        std::fill(excess.begin(),excess.end(),0);
        for(int v:mandatory)if(v!=layer.failed) {
            const U64* row=projection_row(layer,v);
            int missing=-1,neighbors=0;
            for(int w=0;w<projection_words;++w) {
                U64 chosen=selected[w];
                if(layer.failed>=0&&w==layer.failed/64)chosen&=~(U64{1}<<(layer.failed%64));
                missing+=bit_count(chosen&~row[w]);
                neighbors+=bit_count(allowed[w]&row[w]);
            }
            if(missing>layer.slack||neighbors<layer.degree)return false;
            for(int w=0;w<projection_words;++w) {
                if(missing==layer.slack)allowed[w]&=row[w]|selected[w];
                const U64 nonneighbors=~row[w];
                for(int j=layer.slack;j>0;--j)
                    excess[j*projection_words+w]|=excess[(j-1)*projection_words+w]&nonneighbors;
                excess[w]|=nonneighbors;
            }
        }
        for(int w=0;w<projection_words;++w)
            allowed[w]&=~excess[layer.slack*projection_words+w]|selected[w];

        if(root) {
            // Optional vertices also need sufficient surviving projected
            // neighbors. Simultaneous deletions remain safe in a relaxation.
            for(int v=0;v<n;++v)if(v!=layer.failed&&(allowed[v/64]&(U64{1}<<(v%64)))) {
                const U64* row=projection_row(layer,v);
                int neighbors=0;
                for(int w=0;w<projection_words;++w)neighbors+=bit_count(allowed[w]&row[w]);
                if(neighbors>=layer.degree)continue;
                if(selected[v/64]&(U64{1}<<(v%64)))return false;
                allowed[v/64]&=~(U64{1}<<(v%64));
            }
        }
    }
    for(int v=0;v<n;++v)if(state.allowed[projection_members[v]]&&!(allowed[v/64]&(U64{1}<<(v%64)))) {
        state.allowed[projection_members[v]]=0;
        --state.allowed_count;
        ++result.counters.failure_projection_removed;
    }
    return state.allowed_count>=target;
}
