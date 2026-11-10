#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <numeric>
#include <vector>

// Exact selected-obligation relaxation. Clauses not represented here are
// deliberately omitted, so failure is a safe certificate for the full search.
namespace factor_budget {
struct Answer {bool used=false,feasible=true;int minimum=0;std::uint64_t subsets=0,transitions=0;};
template<class Obligation>
Answer solve(const std::vector<Obligation>& obs,int budget,int width=8,int state_cap=1024,
             std::uint64_t operation_cap=300000) {
    Answer out;if(obs.empty()||obs.size()>3||budget<0)return out;
    std::map<int,int> index;int terms=0;
    for(const auto& ob:obs)for(const auto& a:ob.masks){++terms;for(int u:a)index.emplace(u,0);}
    // These cutoffs only decline compilation; they never reject a search state.
    if(index.size()<=10||terms>128)return out;
    int h=0;for(auto& x:index)x.second=h++;
    std::vector<int> parent(h);std::iota(parent.begin(),parent.end(),0);
    auto root=[&](int u){while(parent[u]!=u){parent[u]=parent[parent[u]];u=parent[u];}return u;};
    for(const auto& ob:obs)for(const auto& a:ob.masks)for(std::size_t j=1;j<a.size();++j){
        int u=root(index.at(a[0])),v=root(index.at(a[j]));parent[u]=v;
    }
    std::map<int,std::vector<int>> groups;
    for(int j=0;j<h;++j)groups[root(j)].push_back(j);
    if(groups.size()<2)return out;
    int states=1;std::vector<int> stride,radix;
    for(const auto& ob:obs){if(ob.demand<=0)return out;stride.push_back(states);radix.push_back(ob.demand+1);
        if(ob.demand>=state_cap||states>state_cap/(ob.demand+1))return out;states*=ob.demand+1;}
    std::uint64_t estimate=0,eval_estimate=0;
    for(const auto& group:groups){int n=static_cast<int>(group.second.size());if(n>width)return out;
        const auto count=std::uint64_t{1}<<n;estimate+=static_cast<std::uint64_t>(states)*std::min<std::uint64_t>(states,count);
        eval_estimate+=count*terms;}
    if(estimate+eval_estimate>operation_cap)return out;
    const int infinity=budget+1;
    std::vector<int> dp(states,infinity);dp[0]=0;
    for(const auto& group:groups) {
        const auto& members=group.second;const int n=static_cast<int>(members.size()),count=1<<n;
        std::map<int,int> bit;for(int j=0;j<n;++j)bit[members[j]]=1<<j;
        struct Term {int mask,row;};std::vector<Term> local;
        for(std::size_t row=0;row<obs.size();++row)for(const auto& a:obs[row].masks) {
            if(a.empty())return out;
            if(root(index.at(a[0]))!=group.first)continue;
            int mask=0;for(int u:a)mask|=bit.at(index.at(u));
            local.push_back({mask,static_cast<int>(row)});
        }
        std::vector<int> costs(states,infinity);
        for(int t=0;t<count;++t){int cost=__builtin_popcount(static_cast<unsigned>(t));if(cost>budget)continue;++out.subsets;
            std::vector<int> gains(obs.size(),0);
            for(const auto& term:local)if((t&term.mask)==term.mask)++gains[term.row];
            int code=0;for(std::size_t j=0;j<obs.size();++j)code+=std::min(gains[j],obs[j].demand)*stride[j];
            costs[code]=std::min(costs[code],cost);
        }
        std::vector<std::pair<int,int>> options;for(int z=0;z<states;++z)if(costs[z]<=budget)options.push_back({z,costs[z]});
        std::vector<int> next(states,infinity);
        for(int a=0;a<states;++a)if(dp[a]<=budget)for(const auto& option:options)if(option.second<=budget-dp[a]) {
            const int z=option.first;
            ++out.transitions;int code=0;
            for(std::size_t j=0;j<obs.size();++j){int x=(a/stride[j])%radix[j],y=(z/stride[j])%radix[j];
                code+=std::min(x+y,obs[j].demand)*stride[j];}
            next[code]=std::min(next[code],dp[a]+costs[z]);
        }
        dp=std::move(next);
    }
    out.used=true;out.minimum=dp.back();out.feasible=out.minimum<=budget;return out;
}
} // namespace factor_budget
