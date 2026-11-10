// Included after Graph and Query inside robust_local_v14.
struct SeedRankBound {int lower=3;U64 inspected=0,activated=0;};
template<class Tick>
SeedRankBound seed_rank_bound(const Graph& graph,const Query& query,int cap,int initial,Tick tick) {
    SeedRankBound answer;answer.lower=initial;if(initial>cap)return answer;
    for(int v:query.members) {
        tick();std::vector<int> edges;
        for(int eid:graph.incident[v]){if((answer.inspected&63)==0)tick();++answer.inspected;if(graph.edges[eid].size()<static_cast<std::size_t>(cap))edges.push_back(eid);}
        std::sort(edges.begin(),edges.end(),[&](int a,int b){return graph.edges[a].size()!=graph.edges[b].size()?graph.edges[a].size()<graph.edges[b].size():a<b;});
        std::vector<int> co(graph.n,0);int degree=0,worst=0,minimum=cap+1;
        for(int eid:edges){if((answer.activated&63)==0)tick();++answer.activated;++degree;
            for(int u:graph.edges[eid])if(u!=v)worst=std::max(worst,++co[u]);
            if(degree-worst>=query.k){minimum=static_cast<int>(graph.edges[eid].size())+1;break;}}
        answer.lower=std::max(answer.lower,minimum);if(answer.lower>cap)return answer;
    }
    return answer;
}
