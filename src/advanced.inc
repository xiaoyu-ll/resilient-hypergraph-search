// Included inside Solver. All IDs in obligations/clauses are original IDs.
struct Obligation {std::vector<std::vector<int>> masks;int demand=0;};
std::unordered_map<std::string,std::vector<std::vector<int>>> repair_cache;

bool propagate_clauses(State& s,bool& changed) {
    for(;;) {
        bool again=false;std::vector<std::vector<int>> keep;
        for(const auto& a:s.forbidden) {
            int missing=0,last=-1;bool satisfied=false;
            for(int u:a){if(!s.allowed[u]){satisfied=true;break;}if(!s.selected[u]){++missing;last=u;}}
            if(satisfied)continue;
            if(missing==0){++result.counters.clause_prunes;return false;}
            if(missing==1){s.allowed[last]=0;--s.allowed_count;++result.counters.clause_exclusions;again=changed=true;}
            else keep.push_back(a);
        }
        s.forbidden=std::move(keep);if(!again)return true;
    }
}

static std::vector<std::vector<int>> minimal_masks(const std::vector<std::vector<int>>& masks) {
    auto all=masks;std::sort(all.begin(),all.end(),[](const auto& a,const auto& b){return a.size()!=b.size()?a.size()<b.size():a<b;});
    all.erase(std::unique(all.begin(),all.end()),all.end());std::vector<std::vector<int>> out;
    for(auto& a:all){bool dominated=false;for(const auto& b:out)if(std::includes(a.begin(),a.end(),b.begin(),b.end())){dominated=true;break;}if(!dominated)out.push_back(std::move(a));}
    return out;
}

bool missing_bound(const Obligation& ob,int b) {
    std::map<std::vector<int>,int> multiplicity;
    for(const auto& a:ob.masks)++multiplicity[a];
    std::map<int,std::vector<int>> buckets;
    for(const auto& entry:multiplicity)buckets[static_cast<int>(entry.first.size())].push_back(entry.second);
    U64 upper=0;
    for(auto& entry:buckets) {
        auto& weights=entry.second;const U64 count=choose_cap(b,entry.first,static_cast<U64>(weights.size()));
        std::sort(weights.begin(),weights.end(),std::greater<int>());
        for(std::size_t j=0;j<count;++j)upper+=weights[j];
    }
    if(upper<static_cast<U64>(ob.demand)){++result.counters.multiplicity_prunes;return false;}
    if(algorithm==Algorithm::A3&&ob.demand>1&&buckets.size()==1&&buckets.begin()->first==2) {
        std::map<int,std::set<int>> adj;int maximum=0;
        for(const auto& entry:multiplicity){int u=entry.first[0],v=entry.first[1];adj[u].insert(v);adj[v].insert(u);maximum=std::max(maximum,entry.second);}
        bool triangle=false;
        for(const auto& entry:multiplicity){const auto& a=adj[entry.first[0]];const auto& c=adj[entry.first[1]];
            auto i=a.begin(),j=c.begin();while(i!=a.end()&&j!=c.end()){if(*i==*j){triangle=true;break;}if(*i<*j)++i;else ++j;}if(triangle)break;}
        if(!triangle&&static_cast<U64>(maximum)*(static_cast<U64>(b)*b/4)<static_cast<U64>(ob.demand)) {
            ++result.counters.compatibility_prunes;return false;
        }
    }
    return true;
}

bool repair_table(const State& state,const std::vector<Obligation>& obs,
                  std::vector<std::vector<int>>& choices,std::vector<int>& forced,bool& used) {
    const int width=10,b=target-state.selected_count;
    std::set<int> universe;std::vector<const Obligation*> chosen;
    for(const auto& ob:obs) {
        auto next=universe;for(const auto& a:ob.masks)next.insert(a.begin(),a.end());
        if(static_cast<int>(next.size())<=width){universe=std::move(next);chosen.push_back(&ob);}
    }
    if(chosen.empty())return true;
    // One singleton-only obligation has no activation compatibility to compile.
    if(chosen.size()==1&&std::all_of(chosen[0]->masks.begin(),chosen[0]->masks.end(),[](const auto& a){return a.size()==1;}))return true;
    std::vector<int> members(universe.begin(),universe.end());const int h=static_cast<int>(members.size()),size=1<<h;
    std::map<int,int> index;for(int j=0;j<h;++j)index[members[j]]=1<<j;
    auto local_mask=[&](const std::vector<int>& a){int out=0;for(int u:a)out|=index.at(u);return out;};
    std::vector<int> clauses;
    for(const auto& a:state.forbidden) {
        int mask=0;bool outside=false;for(int u:a)if(!state.selected[u]){auto it=index.find(u);if(it==index.end()){outside=true;break;}mask|=it->second;}
        if(!outside)clauses.push_back(mask);
    }
    std::ostringstream key;for(int u:members)key<<u<<',';key<<';';
    for(const auto* ob:chosen){key<<ob->demand<<':';for(const auto& a:ob->masks)key<<local_mask(a)<<',';key<<';';}
    key<<'|';for(int a:clauses)key<<a<<',';
    auto it=repair_cache.find(key.str());std::vector<std::vector<int>> repairs;
    if(it!=repair_cache.end()){++result.counters.repair_cache_hits;repairs=it->second;}
    else {
        ++result.counters.repair_compilations;result.counters.repair_subsets+=size;
        std::vector<unsigned char> feasible(size,1);
        for(int t=0;t<size;++t)for(int a:clauses)if((t&a)==a){feasible[t]=0;break;}
        for(const auto* ob:chosen) {
            deadline();std::vector<int> gain(size,0);for(const auto& a:ob->masks)++gain[local_mask(a)];
            for(int step=1;step<size;step*=2)for(int base=0;base<size;base+=2*step)for(int off=0;off<step;++off)gain[base+step+off]+=gain[base+off];
            for(int t=0;t<size;++t)feasible[t]&=gain[t]>=ob->demand;
        }
        for(int t=0;t<size;++t)if(feasible[t]) {
            bool minimal=true;for(int j=0;j<h;++j)if((t&(1<<j))&&feasible[t^(1<<j)]){minimal=false;break;}
            if(minimal){std::vector<int> a;for(int j=0;j<h;++j)if(t&(1<<j))a.push_back(members[j]);repairs.push_back(std::move(a));}
        }
        if(repair_cache.size()>=64)repair_cache.clear();
        repair_cache.emplace(key.str(),repairs);
    }
    for(auto& a:repairs)if(static_cast<int>(a.size())<=b)choices.push_back(std::move(a));
    used=true;
    if(choices.empty()){++result.counters.repair_prunes;return false;}
    forced=choices.front();
    for(const auto& a:choices){std::vector<int> common;std::set_intersection(forced.begin(),forced.end(),a.begin(),a.end(),std::back_inserter(common));forced=std::move(common);}
    // If the repair antichain is too large, keep the bound and forcing only.
    if(choices.size()>64)choices.clear();
    return true;
}

bool truncated_repair(const State& state,const std::vector<Obligation>& obs,
                      std::vector<std::vector<int>>& choices,std::vector<int>& forced,bool& used) {
    const int b=target-state.selected_count;
    if(b>3||b<1)return true;
    std::set<int> universe;std::vector<const Obligation*> chosen;int terms=0;
    for(const auto& ob:obs) {
        auto next=universe;for(const auto& a:ob.masks)next.insert(a.begin(),a.end());
        if(next.size()<=24&&terms+static_cast<int>(ob.masks.size())<=96){universe=std::move(next);chosen.push_back(&ob);terms+=static_cast<int>(ob.masks.size());}
    }
    const int h=static_cast<int>(universe.size());
    if(h<=10||chosen.empty())return true;
    U64 volume=0;for(int r=0;r<=b;++r)volume+=choose_cap(h,r,4097);
    if(volume>4096||volume*static_cast<U64>(terms)>180000)return true;
    if(chosen.size()==1&&std::all_of(chosen[0]->masks.begin(),chosen[0]->masks.end(),[](const auto& a){return a.size()==1;}))return true;
    std::vector<int> members(universe.begin(),universe.end());std::map<int,U64> bits;
    for(int j=0;j<h;++j)bits[members[j]]=U64{1}<<j;
    std::vector<std::vector<U64>> masks;
    for(const auto* ob:chosen){masks.emplace_back();for(const auto& a:ob->masks){U64 mask=0;for(int u:a)mask|=bits.at(u);masks.back().push_back(mask);}}
    std::vector<U64> clauses;
    for(const auto& a:state.forbidden){U64 mask=0;bool outside=false;
        for(int u:a)if(!state.selected[u]){auto it=bits.find(u);if(it==bits.end()){outside=true;break;}mask|=it->second;}
        if(!outside)clauses.push_back(mask);
    }
    std::vector<U64> repairs;
    auto evaluate=[&](U64 t){
        ++result.counters.truncated_subsets;
        for(U64 a:repairs)if((t&a)==a)return;
        for(U64 a:clauses)if((t&a)==a)return;
        for(std::size_t j=0;j<chosen.size();++j){int gain=0;for(U64 a:masks[j])if((t&a)==a)++gain;if(gain<chosen[j]->demand)return;}
        repairs.push_back(t);
    };
    ++result.counters.truncated_compilations;
    for(int r=0;r<=std::min(b,h);++r) {
        if(r==0){evaluate(0);continue;}
        U64 t=(U64{1}<<r)-1,limit=U64{1}<<h;
        while(t<limit){evaluate(t);const U64 low=t&(~t+1),next=t+low;t=next+(((next^t)/low)>>2);}
    }
    used=true;
    if(repairs.empty()){++result.counters.truncated_prunes;return false;}
    U64 common=repairs.front();for(U64 a:repairs)common&=a;
    for(int j=0;j<h;++j)if(common&(U64{1}<<j))forced.push_back(members[j]);
    if(repairs.size()<=64)for(U64 t:repairs){std::vector<int> a;for(int j=0;j<h;++j)if(t&(U64{1}<<j))a.push_back(members[j]);choices.push_back(std::move(a));}
    return true;
}

bool cross_joint_bound(const std::vector<Obligation>& obs,int b) {
    if(obs.size()<2)return true;
    ++result.counters.cross_joint_checks;
    std::map<std::vector<int>,U64> weights;U64 demand=0;
    for(const auto& ob:obs){demand+=ob.demand;for(const auto& a:ob.masks)++weights[a];}
    struct Bucket{U64 total=0;std::map<int,U64> load;std::vector<U64> weights;};
    std::map<int,Bucket> buckets;
    for(const auto& entry:weights){auto& group=buckets[static_cast<int>(entry.first.size())];
        group.total+=entry.second;group.weights.push_back(entry.second);for(int u:entry.first)group.load[u]+=entry.second;}
    U64 upper=0;
    for(auto& entry:buckets){int r=entry.first;auto& group=entry.second;
        std::vector<U64> loads;for(const auto& a:group.load)loads.push_back(a.second);
        std::sort(loads.begin(),loads.end(),std::greater<U64>());U64 total=0;
        for(int j=0;j<b&&j<static_cast<int>(loads.size());++j)total+=loads[j];
        std::sort(group.weights.begin(),group.weights.end(),std::greater<U64>());
        U64 distinct=choose_cap(b,r,static_cast<U64>(group.weights.size())),subset_upper=0;
        for(std::size_t j=0;j<distinct;++j)subset_upper+=group.weights[j];
        upper+=std::min({group.total,total/static_cast<U64>(r),subset_upper});
    }
    if(upper<demand){++result.counters.cross_joint_prunes;return false;}
    return true;
}

bool factor_check(const std::vector<Obligation>& obs,int b) {
    if(obs.size()==1&&std::all_of(obs[0].masks.begin(),obs[0].masks.end(),[](const auto& a){return a.size()==1;})){++result.counters.closed_form_skips;return true;}
    ++result.counters.factor_calls;
    auto answer=factor_budget::solve(obs,b);
    result.counters.factor_subsets+=answer.subsets;result.counters.factor_transitions+=answer.transitions;
    if(answer.used)++result.counters.factor_used;
    if(answer.used&&!answer.feasible){++result.counters.factor_prunes;return false;}
    return true;
}

// Closed-form residual oracle for singleton masks. It is independent of
// failure profiles and remains necessary when other clauses are omitted.
bool singleton_forcing(const Obligation& ob,int b,std::vector<int>& forced) {
    if(!std::all_of(ob.masks.begin(),ob.masks.end(),[](const auto& a){return a.size()==1;}))return true;
    std::map<int,int> weights;for(const auto& a:ob.masks)++weights[a.front()];
    std::vector<std::pair<int,int>> ranked;for(const auto& x:weights)ranked.push_back({x.second,x.first});
    std::sort(ranked.begin(),ranked.end(),std::greater<std::pair<int,int>>());
    const int count=std::min(b,static_cast<int>(ranked.size()));U64 upper=0;
    for(int j=0;j<count;++j)upper+=ranked[j].first;
    if(upper<static_cast<U64>(ob.demand))return false;
    const int replacement=count<static_cast<int>(ranked.size())?ranked[count].first:0;
    for(int j=0;j<count;++j)if(upper-ranked[j].first+replacement<static_cast<U64>(ob.demand))forced.push_back(ranked[j].second);
    result.counters.scalar_forced+=forced.size();return true;
}

bool advanced(const State& state,const Context& c,
              std::vector<std::vector<int>>& choices,std::vector<int>& forced) {
    const int n=static_cast<int>(c.original.size()),b=target-state.selected_count;
    struct Pending{int v,x,d,available;};std::vector<Pending> pending;
    std::vector<unsigned char> selected(n,0);for(int j=0;j<n;++j)selected[j]=state.selected[c.original[j]];
    for(int v=0;v<n;++v)if(selected[v]) {
        deadline();int active=0;std::vector<int> all_co(n,0),active_co(n,0);
        for(int eid:c.incident[v]){bool complete=true;for(int u:c.edges[eid])if(!selected[u]){complete=false;break;}
            if(complete)++active;
            for(int u:c.edges[eid]){++all_co[u];if(complete)++active_co[u];}
        }
        if(active<result.query.k+1)pending.push_back({v,-1,result.query.k+1-active,c.degree[v]-active});
        for(int x=0;x<n;++x)if(x!=v&&all_co[x]>1&&active-active_co[x]<result.query.k)
            pending.push_back({v,x,result.query.k-(active-active_co[x]),c.degree[v]-all_co[x]-(active-active_co[x])});
    }
    std::sort(pending.begin(),pending.end(),[](const Pending& a,const Pending& c){
        long long left=static_cast<long long>(a.available)*c.d,right=static_cast<long long>(c.available)*a.d;
        return left!=right?left<right:std::tie(a.available,a.v,a.x)<std::tie(c.available,c.v,c.x);
    });
    std::vector<Obligation> obs;
    // Only whole obligations are selected; no contributing edge is dropped.
    for(std::size_t j=0;j<pending.size()&&j<24&&obs.size()<6;++j) {
        const auto p=pending[j];Obligation ob;ob.demand=p.d;
        for(int eid:c.incident[p.v]) {
            const auto& e=c.edges[eid];if(p.x>=0&&std::binary_search(e.begin(),e.end(),p.x))continue;
            std::vector<int> missing;for(int u:e)if(!selected[u])missing.push_back(c.original[u]);
            if(!missing.empty())ob.masks.push_back(std::move(missing));
        }
        std::sort(ob.masks.begin(),ob.masks.end());
        if(!missing_bound(ob,b))return false;
        if(!singleton_forcing(ob,b,forced))return false;
        if(!forced.empty())return true;
        bool redundant=false;
        for(const auto& prior:obs)if(prior.demand>=ob.demand&&std::includes(ob.masks.begin(),ob.masks.end(),prior.masks.begin(),prior.masks.end())){redundant=true;break;}
        if(redundant){++result.counters.obligation_skips;continue;}
        obs.erase(std::remove_if(obs.begin(),obs.end(),[&](const auto& prior){
            bool dominated=ob.demand>=prior.demand&&std::includes(prior.masks.begin(),prior.masks.end(),ob.masks.begin(),ob.masks.end());
            if(dominated)++result.counters.obligation_skips;return dominated;
        }),obs.end());
        obs.push_back(std::move(ob));
    }
    if(obs.empty()) {
        // Generate one necessary cut obligation from a selected snapshot.
        for(int failed=-1;failed<n;++failed)if(failed<0||selected[failed]) {
            deadline();DSU d(n);
            for(const auto& e:c.edges){bool active=true;for(int u:e)if(!selected[u]){active=false;break;}
                if(!active||(failed>=0&&std::binary_search(e.begin(),e.end(),failed)))continue;
                for(std::size_t j=1;j<e.size();++j)d.unite(e[0],e[j]);}
            std::set<int> roots;for(int u=0;u<n;++u)if(selected[u]&&u!=failed)roots.insert(d.find(u));
            if(roots.size()<2)continue;
            const int root=*roots.begin();Obligation ob;ob.demand=1;
            for(const auto& e:c.edges){if(failed>=0&&std::binary_search(e.begin(),e.end(),failed))continue;
                bool inside=false,outside=false;std::vector<int> missing;
                for(int u:e){bool in=selected[u]&&u!=failed&&d.find(u)==root;inside|=in;outside|=!in;if(!selected[u])missing.push_back(c.original[u]);}
                if(inside&&outside&&!missing.empty())ob.masks.push_back(std::move(missing));}
            if(ob.masks.empty())return false;
            std::sort(ob.masks.begin(),ob.masks.end());obs.push_back(std::move(ob));break;
        }
    }
    if(obs.empty())return true;
    auto best_factor=std::min_element(obs.begin(),obs.end(),[](const auto& a,const auto& z){return a.masks.size()<z.masks.size();});
    if(algorithm!=Algorithm::A1&&!factor_check(std::vector<Obligation>{*best_factor},b))return false;
    if(algorithm==Algorithm::A3){
        // With one positive demand-one row and no negative clauses, its
        // exact minimal repair family is precisely its activation antichain.
        // A singleton-only row is already solved by the scalar oracle.
        if(obs.size()==1&&state.forbidden.empty()&&obs[0].masks.size()<=256&&
           (obs[0].demand==1||std::all_of(obs[0].masks.begin(),obs[0].masks.end(),[](const auto& a){return a.size()==1;}))) {
            choices=minimal_masks(obs[0].masks);
            if(obs[0].demand==1&&!choices.empty()) {
                forced=choices.front();for(const auto& a:choices){std::vector<int> common;std::set_intersection(forced.begin(),forced.end(),a.begin(),a.end(),std::back_inserter(common));forced=std::move(common);}
            }
            if(choices.size()>64)choices.clear();++result.counters.closed_form_skips;return true;
        }
        std::vector<Obligation> joint;
        for(const auto& ob:obs){bool duplicate=false;for(const auto& prior:joint)if(prior.demand==ob.demand&&prior.masks==ob.masks){duplicate=true;break;}
            if(!duplicate)joint.push_back(ob);if(joint.size()==3)break;}
        if(joint.size()>1&&!factor_check(joint,b))return false;
        if(!cross_joint_bound(obs,b))return false;
        bool used=false;if(!truncated_repair(state,obs,choices,forced,used))return false;
        if(!forced.empty()||!choices.empty())return true;
        if(!repair_table(state,obs,choices,forced,used))return false;if(!forced.empty()||!choices.empty())return true;
    }
    auto best=std::min_element(obs.begin(),obs.end(),[](const auto& a,const auto& b){return a.masks.size()<b.masks.size();});
    // Large OR families revert to the exact member branch, avoiding O(m^2)
    // antichain construction; this cutoff affects cost, not correctness.
    if(best->masks.size()<=256){choices=minimal_masks(best->masks);if(choices.size()>64)choices.clear();}
    return true;
}
