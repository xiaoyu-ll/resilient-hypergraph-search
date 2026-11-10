    // If the outside-only degree cannot reach k, every new vertex
    // must obtain the missing quota from edges crossing the boundary.
    void boundary_filter(std::vector<Edge>& edges,int budget,std::vector<int>* closed) {
        const int demand=result.query.k-rank_capacity(graph.ranks,budget-2,result.query.k);
        if(demand<=0)return;
        struct Entry {int degree=0,worst=0; std::unordered_map<int,int> co;};
        std::unordered_map<int,Entry> stats;
        std::size_t work=0;
        for(const auto& e:edges) {
            if((work++&255)==0)deadline();
            if(static_cast<int>(e.missing.size())>=budget)continue;
            for(int v:e.missing) {
                auto& d=stats[v];
                ++d.degree;
                if(algorithm!=Algorithm::A1)for(int u:e.missing)if(u!=v)
                    d.worst=std::max(d.worst,++d.co[u]);
            }
        }
        if(closed) {
            for(const auto& [v,entry]:stats)if(entry.degree-entry.worst>=demand) {
                closed->push_back(v);
                if(closed->size()>64){closed->clear();break;}
            }
            std::sort(closed->begin(),closed->end());
        }
        edges.erase(std::remove_if(edges.begin(),edges.end(),[&](const Edge& e) {
            if((work++&255)==0)deadline();
            return std::any_of(e.missing.begin(),e.missing.end(),[&](int v) {
                const auto it=stats.find(v);
                const bool removed=it==stats.end()||it->second.degree-it->second.worst<demand;
                if(removed)++result.counters.optional_prunes;
                return removed;
            });
        }),edges.end());
    }

    // With three slots, an exterior vertex needs two distinct rank-two
    // links to vertices that have singleton crossing support.
    std::vector<int> three_slot_domain(const std::vector<Edge>& edges,const std::vector<int>& selected) {
        std::set<int> boundary,singletons;
        for(const auto& e:edges)if(e.missing.size()<3) {
            boundary.insert(e.missing.begin(),e.missing.end());
            if(e.missing.size()==1)singletons.insert(e.missing.front());
            if(boundary.size()>64||singletons.size()>12)return {};
        }
        std::size_t scan=0;
        for(int v:singletons) {
            const auto [begin,end]=incidence.at(v,3);
            scan+=end-begin;
        }
        if(scan>edges.size()*2)return {};
        std::unordered_map<int,unsigned char> seen;
        for(int v:singletons) {
            const auto [begin,end]=incidence.at(v,3);
            for(auto it=begin;it!=end;++it) {
                if((result.counters.edge_scans++&255)==0)deadline();
                const auto& e=graph.edges[*it];
                const int u=e[0]==v?e[1]:e[0];
                if(boundary.count(u)||std::binary_search(selected.begin(),selected.end(),u))continue;
                if(++seen[u]==2)boundary.insert(u);
                if(boundary.size()>64)return {};
            }
        }
        return {boundary.begin(),boundary.end()};
    }

    void tail_filter(std::vector<Edge>& edges,int count,int budget) {
        const int k=result.query.k;
        if(budget>3||(budget==3&&k<2))return;
        struct Degree {int degree=0,partners=0; unsigned touches=0; std::array<int,24> co{};};
        std::array<int,24> active{};
        std::array<unsigned,24> components{};
        {
            DSU d(count);
            for(const auto& e:edges)if(e.missing.empty()) {
                const int first=__builtin_ctz(e.selected);
                for(unsigned bits=e.selected;bits;bits&=bits-1) {
                    const int v=__builtin_ctz(bits);
                    ++active[v];
                    d.unite(first,v);
                }
            }
            for(int i=0;i<count;++i)components[d.find(i)]|=1u<<i;
        }
        const auto connects=[&](unsigned touches) {
            return std::all_of(components.begin(),components.begin()+count,[&](unsigned side) {
                return !side||(side&touches);
            });
        };
        std::unordered_map<int,Degree> entries;
        for(const auto& e:edges)if(e.missing.size()==1) {
            auto& d=entries[e.missing.front()];
            ++d.degree;
            d.touches|=e.selected;
            for(unsigned bits=e.selected;bits;bits&=bits-1)++d.co[__builtin_ctz(bits)];
        }
        if(budget==3) {
            std::map<std::pair<int,int>,Degree> pairs;
            for(const auto& e:edges)if(e.missing.size()==2) {
                auto& p=pairs[{e.missing[0],e.missing[1]}];
                ++p.degree;
                p.touches|=e.selected;
                for(unsigned bits=e.selected;bits;bits&=bits-1)++p.co[__builtin_ctz(bits)];
            }
            for(const auto& entry:pairs) {
                const auto& p=entry.second;
                entries.try_emplace(entry.first.first);
                auto& y=entries[entry.first.second];
                auto& x=entries.at(entry.first.first);
                const auto x_possible=static_cast<long long>(x.degree)+p.degree+1>=k;
                const auto y_possible=static_cast<long long>(y.degree)+p.degree+1>=k;
                if(!x_possible||!y_possible||!connects(x.touches|y.touches|p.touches))continue;
                bool degrees_ok=true;
                for(int i=0;i<count;++i)if(static_cast<long long>(active[i])+x.co[i]+y.co[i]+p.co[i]<k) {
                    degrees_ok=false;
                    break;
                }
                if(degrees_ok){++x.partners;++y.partners;}
            }
        }
        edges.erase(std::remove_if(edges.begin(),edges.end(),[&](const Edge& e) {
            return std::any_of(e.missing.begin(),e.missing.end(),[&](int v) {
                const auto it=entries.find(v);
                if(it==entries.end()) {
                    ++result.counters.support_prunes;
                    return true;
                }
                const auto& d=it->second;
                bool remove=d.degree<k-1&&d.partners<2;
                if(budget==2) {
                    remove=d.degree<k||!connects(d.touches);
                    for(int i=0;i<count&&!remove;++i)
                        if(static_cast<long long>(active[i])+d.co[i]<k)remove=true;
                }
                if(remove)++result.counters.support_prunes;
                return remove;
            });
        }),edges.end());
    }

    // Decide a one-slot state using the common selected failure snapshots.
    bool close_tail(const std::vector<Edge>& edges,const std::vector<int>& selected,int budget) {
        struct Snapshot {
            std::array<int,24> degree{};
            std::array<unsigned,24> components{};
            int count=0;
        };
        const int n=static_cast<int>(selected.size()),k=result.query.k;
        std::array<Snapshot,25> snapshots;
        std::map<int,std::vector<unsigned>> options;
        for(const auto& e:edges)if(budget&&e.missing.size()==1)options[e.missing.front()].push_back(e.selected);
        bool selected_valid=n>=3;
        for(int x=-1;x<n;++x) {
            deadline();
            const unsigned failed=x<0?0:1u<<x;
            auto& view=snapshots[x+1];
            DSU d(n);
            for(const auto& e:edges)if(e.missing.empty()&&!(e.selected&failed)) {
                const int first=__builtin_ctz(e.selected);
                for(unsigned bits=e.selected;bits;bits&=bits-1) {
                    const int v=__builtin_ctz(bits);
                    ++view.degree[v];
                    d.unite(first,v);
                }
            }
            std::array<unsigned,24> sides{};
            bool supported=true;
            for(int v=0;v<n;++v)if(v!=x) {
                sides[d.find(v)]|=1u<<v;
                supported&=view.degree[v]>=k;
            }
            for(unsigned side:sides)if(side)view.components[view.count++]=side;
            const bool viable=supported&&view.count==1;
            if(x==-1&&!viable)return false;
            selected_valid&=viable;
        }
        if(selected_valid) {
            result.witness=selected;
            return true;
        }
        for(const auto& entry:options) {
            deadline();
            if(entry.second.size()<=static_cast<std::size_t>(k))continue;
            bool viable=true;
            for(int x=0;x<n&&viable;++x) {
                const auto& view=snapshots[x+1];
                auto degrees=view.degree;
                int optional_degree=0;
                unsigned touches=0;
                for(unsigned e:entry.second)if(!(e&(1u<<x))) {
                    ++optional_degree;
                    touches|=e;
                    for(unsigned bits=e;bits;bits&=bits-1)++degrees[__builtin_ctz(bits)];
                }
                if(optional_degree<k){viable=false;break;}
                for(int v=0;v<n;++v)if(v!=x&&degrees[v]<k){viable=false;break;}
                for(int c=0;c<view.count&&viable;++c)if(!(view.components[c]&touches))viable=false;
            }
            if(viable) {
                result.witness=selected;
                result.witness.insert(std::lower_bound(result.witness.begin(),result.witness.end(),entry.first),entry.first);
                return true;
            }
        }
        return false;
    }


    // A two-slot state is closed by the singleton crossing supports.
    // Reuse the selected failure snapshots across all admissible pairs.
    std::optional<bool> close_pair(const std::vector<Edge>& edges,
                                   const std::vector<int>& selected,const Choices& choices) {
        if(choices.empty())return std::nullopt;
        const int n=static_cast<int>(selected.size()),k=result.query.k;
        std::map<int,std::vector<unsigned>> singles;
        std::map<std::pair<int,int>,std::vector<unsigned>> pairs;
        std::vector<unsigned> fixed;
        for(const auto& e:edges) {
            if(e.missing.empty())fixed.push_back(e.selected);
            else if(e.missing.size()==1)singles[e.missing[0]].push_back(e.selected);
            else pairs[{e.missing[0],e.missing[1]}].push_back(e.selected);
        }
        // Canonicalize the overlapping cones of the row cover.
        std::set<std::pair<int,int>> candidates;
        for(const auto& a:choices) {
            if(!singles.count(a[0]))continue;
            if(a.size()==2) {
                if(singles.count(a[1]))candidates.emplace(a[0],a[1]);
            } else {
                for(const auto& [v,terms]:singles)if(v!=a[0])
                    candidates.emplace(std::min(v,a[0]),std::max(v,a[0]));
            }
            if(candidates.size()>256)return std::nullopt;
        }
        if(candidates.empty())return false;
        std::size_t terms=fixed.size();
        for(const auto& candidate:candidates) {
            const auto [u,v]=candidate;
            const auto it=pairs.find(candidate);
            terms+=singles.at(u).size()+singles.at(v).size()+1;
            if(it!=pairs.end())terms+=it->second.size();
        }
        if(terms>edges.size()*choices.size()*2)return std::nullopt;

        struct Snapshot {
            std::array<int,26> degree{};
            std::array<unsigned,24> components{};
            int count=0;
        };
        std::array<Snapshot,24> snapshots;
        for(int x=0;x<n;++x) {
            deadline();
            auto& view=snapshots[x];
            DSU d(n);
            for(unsigned e:fixed)if(!(e&(1u<<x))) {
                const int first=__builtin_ctz(e);
                for(unsigned bits=e;bits;bits&=bits-1) {
                    const int v=__builtin_ctz(bits);
                    ++view.degree[v];
                    d.unite(first,v);
                }
            }
            std::array<unsigned,24> sides{};
            for(int v=0;v<n;++v)if(v!=x)sides[d.find(v)]|=1u<<v;
            for(unsigned side:sides)if(side)view.components[view.count++]=side;
        }
        std::map<int,int> single_min;
        for(const auto& [v,terms]:singles) {
            std::array<int,24> lost{};
            for(unsigned e:terms)for(unsigned bits=e;bits;bits&=bits-1)
                ++lost[__builtin_ctz(bits)];
            single_min[v]=static_cast<int>(terms.size())-*std::max_element(lost.begin(),lost.begin()+n);
        }
        const std::vector<unsigned> none;
        const auto check=[&](const std::vector<unsigned>& a,const std::vector<unsigned>& b,
                             const std::vector<unsigned>& joint,bool pair_edge) {
            ++result.counters.repair_subsets;
            for(int x=0;x<n;++x) {
                const auto& view=snapshots[x];
                auto degree=view.degree;
                DSU d(n+2);
                unsigned touches=0;
                const auto add=[&](const std::vector<unsigned>& terms,int v,bool both) {
                    for(unsigned e:terms)if(!(e&(1u<<x))) {
                        ++degree[v];
                        if(both)++degree[n+1];
                        for(unsigned bits=e;bits;bits&=bits-1) {
                            const int u=__builtin_ctz(bits);
                            ++degree[u];
                            d.unite(v,u);
                        }
                        if(both)d.unite(v,n+1);
                        touches|=e;
                    }
                };
                add(a,n,false);
                add(b,n+1,false);
                add(joint,n,true);
                if(pair_edge){++degree[n];++degree[n+1];d.unite(n,n+1);}
                for(int v=0;v<n+2;++v)if(v!=x&&degree[v]<k)return false;
                for(int i=0;i<view.count;++i) {
                    const unsigned side=view.components[i];
                    if(!(side&touches))return false;
                    const int first=__builtin_ctz(side);
                    for(unsigned bits=side;bits;bits&=bits-1)d.unite(first,__builtin_ctz(bits));
                }
                if(d.find(n)!=d.find(n+1))return false;
                for(int i=0;i<view.count;++i)if(d.find(__builtin_ctz(view.components[i]))!=d.find(n))return false;
            }
            return true;
        };
        ++result.counters.switch_pairs;
        for(const auto& [u,v]:candidates) {
            deadline();
            const int least=std::min(single_min.at(u),single_min.at(v));
            const auto found=pairs.find({u,v});
            const auto& joint=found==pairs.end()?none:found->second;
            if(static_cast<long long>(least)+joint.size()+1<k)continue;
            const std::vector<int> edge{u,v};
            const bool pair_edge=std::binary_search(graph.edges.begin(),graph.edges.end(),edge);
            if(static_cast<long long>(least)+joint.size()+pair_edge<k)continue;
            if(check(singles.at(u),singles.at(v),joint,pair_edge)) {
                result.witness=combine(selected,edge);
                return true;
            }
        }
        ++result.counters.repair_prunes;
        return false;
    }

    // An engaged result is exact for the state. The null result falls back
    // to ordinary branching when the closed domain is too large.
    std::optional<bool> close_boundary(const std::vector<Edge>& edges,
                                       const std::vector<int>& selected,int budget,
                                       const Choices& choices,const std::vector<int>* closed) {
        const int k=result.query.k;
        if(probe_until!=Clock::time_point::max())return std::nullopt;
        if(budget==2&&structure)return close_pair(edges,selected,choices);
        if(budget<=1||(!closed&&rank_capacity(graph.ranks,budget-2,k)>=k))return std::nullopt;
        std::set<int> unique;
        if(closed)unique.insert(closed->begin(),closed->end());
        else for(const auto& e:edges)if(static_cast<int>(e.missing.size())<budget)
            for(int v:e.missing) {
                unique.insert(v);
                if(unique.size()>11)return std::nullopt;
            }
        if(unique.size()>11)return std::nullopt;
        if(unique.size()<static_cast<std::size_t>(budget))return false;
        std::vector<int> domain(unique.begin(),unique.end());
        const unsigned n=static_cast<unsigned>(selected.size()),u=static_cast<unsigned>(domain.size());
        const unsigned limit=1u<<u;
        auto encode=[&](const std::vector<int>& a) {
            unsigned bits=0;
            for(int v:a) {
                const auto pos=std::lower_bound(domain.begin(),domain.end(),v);
                if(pos==domain.end()||*pos!=v)return limit;
                bits|=1u<<(pos-domain.begin());
            }
            return bits;
        };
        std::vector<unsigned> alternatives;
        std::uint64_t candidates=0;
        for(const auto& a:choices) {
            const unsigned mask=encode(a);
            if(mask==limit)continue;
            alternatives.push_back(mask);
            const int free=static_cast<int>(u-a.size());
            const int room=budget-static_cast<int>(a.size());
            if(room<0||room>free)continue;
            unsigned combinations=1;
            for(int j=0;j<room;++j)combinations=combinations*(free-j)/(j+1);
            candidates+=combinations;
        }
        if(alternatives.empty())return false;
        if(candidates>64||candidates*(n+u+edges.size())+limit>edges.size()*choices.size()*3)
            return std::nullopt;
        ++result.counters.switch_masks;
        struct Term {std::uint64_t members; unsigned extra;};
        std::vector<Term> terms;
        for(const auto& e:edges) {
            const unsigned mask=encode(e.missing);
            if(mask!=limit)terms.push_back({e.selected|(std::uint64_t{mask}<<n),mask});
        }
        for(unsigned mask=1;mask<limit;++mask) {
            if((mask&63)==0)deadline();
            const int r=__builtin_popcount(mask);
            if(r<2||r>budget||!std::binary_search(graph.ranks.begin(),graph.ranks.end(),r))continue;
            std::vector<int> edge;
            for(unsigned bits=mask;bits;bits&=bits-1)edge.push_back(domain[__builtin_ctz(bits)]);
            if(std::binary_search(graph.edges.begin(),graph.edges.end(),edge))
                terms.push_back({std::uint64_t{mask}<<n,mask});
        }
        const std::uint64_t fixed=(std::uint64_t{1}<<n)-1;
        const unsigned width=n+u;
        std::vector<std::uint64_t> active;
        for(unsigned extra=0;extra<limit;++extra) {
            if((extra&31)==0)deadline();
            if(__builtin_popcount(extra)!=budget)continue;
            if(!choices.empty()&&std::none_of(alternatives.begin(),alternatives.end(),[&](unsigned a) {
                return (extra&a)==a;
            }))continue;
            active.clear();
            for(const auto& e:terms)if((extra&e.extra)==e.extra)active.push_back(e.members);
            const auto members=fixed|(std::uint64_t{extra}<<n);
            bool valid=true;
            for(auto bits=members;bits&&valid;bits&=bits-1) {
                const auto fail=bits&-bits;
                const auto survivors=members^fail;
                std::array<int,36> degree{};
                DSU connected(width);
                for(auto e:active)if(!(e&fail)) {
                    const int first=__builtin_ctzll(e);
                    for(auto v=e;v;v&=v-1) {
                        const int i=__builtin_ctzll(v);
                        ++degree[i];
                        connected.unite(first,i);
                    }
                }
                const int root=connected.find(__builtin_ctzll(survivors));
                for(auto v=survivors;v;v&=v-1) {
                    const int i=__builtin_ctzll(v);
                    if(degree[i]<k||connected.find(i)!=root){valid=false;break;}
                }
            }
            if(valid) {
                result.witness=selected;
                for(unsigned v=extra;v;v&=v-1)result.witness.push_back(domain[__builtin_ctz(v)]);
                std::sort(result.witness.begin(),result.witness.end());
                return true;
            }
        }
        ++result.counters.repair_prunes;
        return false;
    }
