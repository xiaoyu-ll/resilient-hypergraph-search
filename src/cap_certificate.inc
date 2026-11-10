// Query-local support peeling at the common rank and budget prefix. Each
// retained vertex keeps the endpoint of an incident-edge certificate.
bool cap_certificates(State& initial,int cap) {
    if(!cap_certificate||cap>64||graph.n>20000||graph.edges.size()>50000)return true;
    ++result.counters.cap_attempts;
    std::vector<std::vector<int>> incidence(graph.n);
    std::vector<unsigned char> active(graph.edges.size(),0);
    DSU components(graph.n);
    U64 work=0;
    constexpr U64 limit=1800000;
    const int budget=cap-initial.selected_count;
    const auto end=index->below(index->edges,cap);
    for(auto it=index->edges.begin();it!=end;++it) {
        deadline();
        const auto& edge=graph.edges[*it];
        work+=edge.size();
        if(work>limit)return true;
        int missing=0;
        for(int v:edge)missing+=!initial.selected[v];
        if(missing>budget)continue;
        active[*it]=1;
        for(int v:edge) {
            incidence[v].push_back(*it);
            components.unite(edge.front(),v);
        }
    }
    const int anchor=components.find(result.query.members.front());
    for(int seed:result.query.members)if(components.find(seed)!=anchor) {
        ++result.counters.cap_rejects;
        return false;
    }
    std::vector<unsigned char> alive(graph.n,0),pending(graph.n,0);
    std::vector<int> queue,degree(graph.n,0),position(graph.n,-1);
    for(int v=0;v<graph.n;++v)if(components.find(v)==anchor) {
        alive[v]=pending[v]=1;
        degree[v]=static_cast<int>(incidence[v].size());
        queue.push_back(v);
    }
    // Incidences follow rank-index order. A certificate includes all still
    // active incidences through its endpoint in this fixed order.
    std::vector<int> order(graph.edges.size(),-1);
    for(auto it=index->edges.begin();it!=end;++it)order[*it]=static_cast<int>(it-index->edges.begin());
    std::vector<int> co(graph.n,0),touched;
    const int k=result.query.k;
    for(std::size_t q=0;q<queue.size();++q) {
        if((q&63)==0)deadline();
        const int v=queue[q];
        pending[v]=0;
        bool supported=false;
        if(degree[v]>k) {
            int count=0,worst=0;
            for(int id:incidence[v]) {
                ++result.counters.cap_incidence_scans;
                if(++work>limit)return true;
                if(!active[id])continue;
                ++count;
                const auto& edge=graph.edges[id];
                work+=edge.size();
                for(int u:edge)if(u!=v) {
                    if(!co[u])touched.push_back(u);
                    worst=std::max(worst,++co[u]);
                }
                if(count-worst>=k) {
                    position[v]=order[id];
                    supported=true;
                    break;
                }
                if(degree[v]-worst<k)break;
            }
            for(int u:touched)co[u]=0;
            touched.clear();
        }
        if(supported)continue;
        if(initial.selected[v]) {
            ++result.counters.cap_rejects;
            return false;
        }
        alive[v]=0;
        for(int id:incidence[v])if(active[id]) {
            active[id]=0;
            const auto& edge=graph.edges[id];
            work+=edge.size();
            if(work>limit)return true;
            for(int u:edge)if(alive[u]) {
                --degree[u];
                if(!pending[u]&&order[id]<=position[u]) {
                    pending[u]=1;
                    queue.push_back(u);
                }
            }
        }
    }
    for(int v=0;v<graph.n;++v)if(initial.allowed[v]&&!alive[v]) {
        initial.allowed[v]=0;
        --initial.allowed_count;
        ++result.counters.cap_removed;
    }
    return true;
}
