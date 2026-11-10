// Included after Graph and Query inside robust_local_v14.
class RankIndex {
public:
    struct Level {
        int rank;
        std::vector<int> core;
    };

    explicit RankIndex(const Graph& graph, bool build_cores)
        : graph(graph), edges(graph.edges.size()), incident(graph.incident), owned(graph.n) {
        std::iota(edges.begin(), edges.end(), 0);
        std::sort(edges.begin(), edges.end(), [&](int a, int b) {
            const auto x=graph.edges[a].size(), y=graph.edges[b].size();
            return x!=y ? x<y : a<b;
        });
        for (int id : edges) {
            const auto& edge=graph.edges[id];
            const int owner=*std::min_element(edge.begin(), edge.end(), [&](int a, int b) {
                const auto x=graph.incident[a].size(), y=graph.incident[b].size();
                return x!=y ? x<y : a<b;
            });
            owned[owner].push_back(id);
        }
        for (auto& ids : incident) ids.clear();
        for (int id : edges) for (int v : graph.edges[id]) incident[v].push_back(id);
        if (build_cores) build_levels();
    }

    auto below(const std::vector<int>& ids, int rank) const {
        return std::lower_bound(ids.begin(), ids.end(), rank, [&](int id, int bound) {
            return static_cast<int>(graph.edges[id].size())<bound;
        });
    }

    template<class Tick>
    SeedRankBound seed_bound(const Query& query, int cap, int initial, Tick tick) const {
        SeedRankBound answer{initial};
        if (initial>cap) return answer;
        std::vector<int> co(graph.n, 0), touched;
        for (int v : query.members) {
            tick();
            int degree=0, worst=0, minimum=cap+1;
            for (int id : incident[v]) {
                const auto& edge=graph.edges[id];
                if (static_cast<int>(edge.size())>=cap) break;
                if ((answer.activated&63)==0) tick();
                ++answer.activated;
                ++answer.inspected;
                ++degree;
                for (int u : edge) if (u!=v) {
                    if (co[u]==0) touched.push_back(u);
                    worst=std::max(worst, ++co[u]);
                }
                if (degree-worst>=query.k) {
                    minimum=static_cast<int>(edge.size())+1;
                    break;
                }
            }
            answer.lower=std::max(answer.lower, minimum);
            if (answer.lower>cap) return answer;
            for (int u : touched) co[u]=0;
            touched.clear();
        }
        return answer;
    }

    const Level* level_for(int target) const {
        const auto rank=std::lower_bound(graph.ranks.begin(), graph.ranks.end(), target);
        if (rank==graph.ranks.begin()) return nullptr;
        const auto level=std::lower_bound(levels.begin(), levels.end(), *std::prev(rank),
            [](const Level& a, int r) { return a.rank<r; });
        return level==levels.end() ? nullptr : &*level;
    }

    const Graph& graph;
    std::vector<int> edges;
    std::vector<std::vector<int>> incident, owned;
    std::vector<Level> levels;

private:
    std::vector<int> peel(std::size_t count) const {
        const int n=graph.n;
        std::vector<std::unordered_map<int,int>> co(n);
        std::vector<std::map<int,int>> frequency(n);
        std::vector<int> degree(n,0), core(n,0);
        std::vector<unsigned char> live(n,1), active(count,1);
        std::vector<std::vector<std::size_t>> prefix_incidence(n);
        for (std::size_t i=0; i<count; ++i) {
            const auto& edge=graph.edges[edges[i]];
            for (int v : edge) {
                ++degree[v];
                prefix_incidence[v].push_back(i);
                for (int u : edge) if (u!=v) ++co[v][u];
            }
        }
        for (int v=0; v<n; ++v) {
            for (const auto& entry : co[v]) ++frequency[v][entry.second];
        }
        auto sigma=[&](int v) {
            return degree[v]-(frequency[v].empty() ? 0 : frequency[v].rbegin()->first);
        };
        std::set<std::pair<int,int>> queue;
        for (int v=0; v<n; ++v) queue.emplace(sigma(v), v);
        int threshold=0;
        while (!queue.empty()) {
            const auto [support, v]=*queue.begin();
            queue.erase(queue.begin());
            live[v]=0;
            threshold=std::max(threshold, support);
            core[v]=threshold;
            for (std::size_t i : prefix_incidence[v]) if (active[i]) {
                active[i]=0;
                const auto& edge=graph.edges[edges[i]];
                for (int u : edge) if (live[u]) {
                    queue.erase({sigma(u),u});
                    --degree[u];
                    // A dead member may still occur in other edges deleted in
                    // this round. Remove its co-degree along with every edge.
                    for (int w : edge) if (w!=u) {
                        auto pair=co[u].find(w);
                        auto bucket=frequency[u].find(pair->second);
                        if (--bucket->second==0) frequency[u].erase(bucket);
                        if (--pair->second==0) co[u].erase(pair);
                        else ++frequency[u][pair->second];
                    }
                    queue.emplace(sigma(u), u);
                }
            }
        }
        return core;
    }

    void build_levels() {
        // Bound the total ordered-pair initialization volume and the number
        // of vertex labels. Missing snapshots use an available superset or
        // skip the index.
        constexpr U64 pair_budget=5000000, label_budget=1000000;
        U64 used=0, prefix_work=0;
        std::size_t end=0;
        for (int rank : graph.ranks) {
            const auto next=below(edges, rank+1)-edges.begin();
            const U64 work_per_edge=static_cast<U64>(rank)*(rank-1);
            if (prefix_work>pair_budget-used ||
                static_cast<U64>(next-end)>(pair_budget-used-prefix_work)/work_per_edge) break;
            prefix_work+=static_cast<U64>(next-end)*work_per_edge;
            if (static_cast<U64>(levels.size()+1)*graph.n>label_budget) break;
            end=static_cast<std::size_t>(next);
            levels.push_back(Level{rank,peel(end)});
            used+=prefix_work;
        }
    }
};
