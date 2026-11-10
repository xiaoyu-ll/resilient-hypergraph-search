#include "query.hpp"
#include <iostream>
using namespace robust_local_v14;
static void arr(const std::vector<int>&a){std::cout<<'[';for(size_t i=0;i<a.size();++i){if(i)std::cout<<',';std::cout<<a[i];}std::cout<<']';}
int main(int argc,char**argv){if(argc<2)return 2;auto g=read_graph(argv[1],false);std::vector<int>B={6466,6467,6527,6765,30517,63021,88621,107893,134496,139584,197410};if(argc>2){B.clear();std::istringstream row(argv[2]);int u;while(row>>u)B.push_back(u);std::sort(B.begin(),B.end());}std::vector<std::vector<int>>es;std::vector<int>small;
for(auto&e:g.edges){if(std::includes(B.begin(),B.end(),e.begin(),e.end()))es.push_back(e);if(std::binary_search(e.begin(),e.end(),6765)&&std::binary_search(e.begin(),e.end(),139584)&&(small.empty()||e.size()<small.size()))small=e;}
std::cout<<"{\"members\":";arr(B);std::cout<<",\"smallest_joint_edge\":";arr(small);std::cout<<",\"edges\":[";for(size_t i=0;i<es.size();++i){if(i)std::cout<<',';arr(es[i]);}std::cout<<"],\"snapshots\":[";bool first=true;
for(int x:B){std::map<int,int>deg;DSU d(g.n);int survived=0;for(int v:B)if(v!=x)deg[v]=0;for(auto&e:es)if(!std::binary_search(e.begin(),e.end(),x)){++survived;for(int v:e){++deg[v];d.unite(e.front(),v);}}int minimum=100000;std::set<int>components;for(auto [v,z]:deg){minimum=std::min(minimum,z);components.insert(d.find(v));}if(!first)std::cout<<',';first=false;std::cout<<"{\"failed\":"<<x<<",\"min_support\":"<<minimum<<",\"components\":"<<components.size()<<",\"surviving_edges\":"<<survived<<'}';}std::cout<<"]}\n";}
