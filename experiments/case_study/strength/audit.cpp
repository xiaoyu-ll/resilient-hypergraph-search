// Independent original-edge and every-failure audit for a supplied community.
#include "query.hpp"
#include <iostream>
using namespace robust_local_v14;
void array(const std::vector<int>&v){std::cout<<'[';for(size_t i=0;i<v.size();++i){if(i)std::cout<<',';std::cout<<v[i];}std::cout<<']';}
int main(int argc,char**argv){if(argc!=4)return 2;auto g=read_graph(argv[1],false);int k=std::stoi(argv[3]);std::vector<int>B;std::istringstream s(argv[2]);int u;while(s>>u)B.push_back(u);std::sort(B.begin(),B.end());std::vector<std::vector<int>>edges;for(auto&e:g.edges)if(std::includes(B.begin(),B.end(),e.begin(),e.end()))edges.push_back(e);
std::cout<<"{\"k\":"<<k<<",\"members\":";array(B);std::cout<<",\"edges\":[";for(size_t i=0;i<edges.size();++i){if(i)std::cout<<',';array(edges[i]);}std::cout<<"],\"snapshots\":[";bool first=true,valid=true;int worst=1000000;
for(int x:B){DSU d(g.n);std::map<int,int>degree;for(int v:B)if(v!=x)degree[v]=0;int count=0;for(auto&e:edges)if(!std::binary_search(e.begin(),e.end(),x)){++count;for(int v:e){++degree[v];d.unite(e.front(),v);}}int low=1000000;std::set<int>components;for(auto[v,z]:degree){low=std::min(low,z);components.insert(d.find(v));}worst=std::min(worst,low);valid&=low>=k&&components.size()==1;if(!first)std::cout<<',';first=false;std::cout<<"{\"failed\":"<<x<<",\"min_support\":"<<low<<",\"components\":"<<components.size()<<",\"surviving_edges\":"<<count<<'}';}
std::cout<<"],\"minimum_failure_support\":"<<worst<<",\"valid\":"<<(valid?"true":"false")<<"}\n";return valid?0:1;}
