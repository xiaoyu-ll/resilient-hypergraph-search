bool indexed_root(State& state) {
    if (algorithm==Algorithm::A3) {
        if (const auto* level=index->level_for(target)) {
            ++result.counters.index_core_checks;
            for (int v : result.query.members) if (level->core[v]<result.query.k) {
                ++result.counters.index_core_rejections;
                return false;
            }
            for (int v=0; v<graph.n; ++v) if (level->core[v]<result.query.k && state.allowed[v]) {
                state.allowed[v]=0;
                --state.allowed_count;
                ++result.counters.index_core_removed;
            }
        }
    }
    return state.allowed_count>=target;
}

void indexed_root_edges(State& state) {
    const int budget=target-state.selected_count;
    auto add=[&](int id) {
        if ((result.counters.index_root_edges&255)==0) deadline();
        ++result.counters.index_root_edges;
        const auto& edge=graph.edges[id];
        int missing=0;
        for (int v : edge) {
            if (!state.allowed[v]) return;
            missing+=!state.selected[v];
        }
        if (missing<=budget) state.edge_ids.push_back(id);
    };
    for (auto it=index->edges.begin(), end=index->below(index->edges,budget+1); it!=end; ++it) add(*it);
    for (int seed : result.query.members) {
        const auto& ids=index->incident[seed];
        for (auto it=index->below(ids,budget+1), end=index->below(ids,target); it!=end; ++it) {
            // The least selected member uniquely owns each boundary edge.
            const auto& edge=graph.edges[*it];
            const auto owner=std::find_if(edge.begin(),edge.end(),[&](int v){return state.selected[v];});
            if (*owner==seed) add(*it);
        }
    }
    std::sort(state.edge_ids.begin(),state.edge_ids.end());
}
