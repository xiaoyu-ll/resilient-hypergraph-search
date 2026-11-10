#include "local_activation.hpp"
#include <memory>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace robust_local_v14;

static int integer(const std::string& s) {
    std::size_t p=0;const long long value=std::stoll(s,&p);
    if(p!=s.size()||value<std::numeric_limits<int>::min()||value>std::numeric_limits<int>::max())throw std::invalid_argument("invalid integer");
    return static_cast<int>(value);
}

static std::vector<int> members(std::string s) {
    for(char& c:s)if(c==',')c=' ';
    std::istringstream in(s);std::vector<int> out;std::string v;
    while(in>>v)out.push_back(integer(v));
    return out;
}

static void print_array(const std::vector<int>& values) {
    std::cout<<'[';for(std::size_t i=0;i<values.size();++i){if(i)std::cout<<',';std::cout<<values[i];}std::cout<<']';
}

static void print_result(const Result& r) {
    std::cout<<"{\"query\":";print_array(r.query.members);
    std::cout<<",\"k\":"<<r.query.k<<",\"max_size\":"<<r.query.max_size<<",\"effective_max_size\":"<<r.effective_max_size
             <<",\"status\":\""<<r.status<<"\",\"complete\":"<<(r.complete?"true":"false")
             <<",\"optimal\":"<<(r.optimal?"true":"false")<<",\"feasible\":"<<(r.feasible?"true":"false")
             <<",\"lower_bound\":"<<r.lower_bound<<",\"initial_lower_bound\":"<<r.initial_lower_bound<<",\"incumbent_size\":";
    if(r.feasible)std::cout<<r.witness.size();else std::cout<<"null";
    std::cout<<",\"witness\":";print_array(r.witness);
    std::cout<<",\"elapsed_ms\":"<<r.elapsed_ms<<",\"time_limit_ms\":"<<r.time_limit_ms<<",\"counters\":{";
    const auto& c=r.counters;
#define FIELD(name) std::cout<<"\"" #name "\":"<<c.name<<','
    FIELD(target_sizes);FIELD(completed_target_sizes);FIELD(search_nodes);FIELD(branch_nodes);
    FIELD(candidate_checks);FIELD(candidate_failure_checks);FIELD(candidate_support_rejections);
    FIELD(contexts);FIELD(edge_scans);FIELD(member_tests);FIELD(pair_updates);
    FIELD(support_rounds);FIELD(support_prunes);FIELD(support_vertices_removed);
    FIELD(initial_component_calls);FIELD(strong_component_calls);FIELD(component_edge_scans);FIELD(component_union_attempts);
    FIELD(connectivity_prunes);FIELD(component_vertices_removed);FIELD(region_rounds);
    FIELD(rank_two_calls);FIELD(rank_two_successes);FIELD(bulk_calls);FIELD(bulk_union_attempts);FIELD(bulk_leaf_checks);
    FIELD(budget_edges_excluded);FIELD(capacity_pair_checks);FIELD(capacity_edge_scans);FIELD(capacity_prunes);
    FIELD(degree_capacity_checks);FIELD(degree_capacity_prunes);FIELD(degree_forced_members);
    FIELD(forced_members);FIELD(forced_budget_prunes);FIELD(cardinality_prunes);FIELD(joint_checks);FIELD(joint_edge_scans);FIELD(joint_prunes);
    FIELD(cross_joint_checks);FIELD(cross_joint_prunes);FIELD(dominated_skips);FIELD(active_skips);FIELD(activation_branches);FIELD(clause_prunes);FIELD(clause_exclusions);
    FIELD(multiplicity_prunes);FIELD(compatibility_prunes);FIELD(repair_compilations);FIELD(repair_cache_hits);
    FIELD(repair_prunes);FIELD(repair_subsets);FIELD(repair_forced);
    FIELD(seed_profile_inspections);FIELD(seed_profile_activations);FIELD(early_candidate_attempts);FIELD(early_candidate_successes);FIELD(subset_edge_lookups);
    FIELD(published_dense_targets);FIELD(published_sparse_targets);
    FIELD(unusable_rank_edges);
    FIELD(projection_domain_calls);FIELD(projection_domain_removed);
    FIELD(scalar_forced);FIELD(obligation_skips);FIELD(closed_form_skips);
    FIELD(factor_calls);FIELD(factor_used);FIELD(factor_prunes);FIELD(factor_subsets);FIELD(factor_transitions);
    FIELD(truncated_compilations);FIELD(truncated_prunes);FIELD(truncated_subsets);
    FIELD(deadline_checks);FIELD(peak_depth);
    FIELD(index_core_checks);FIELD(index_core_rejections);FIELD(index_core_removed);FIELD(index_root_edges);FIELD(index_leaf_edges);
    FIELD(failure_projection_builds);FIELD(failure_projection_edges);FIELD(failure_projection_tight);
    FIELD(failure_projection_nodes);FIELD(failure_projection_removed);FIELD(failure_projection_prunes);
    FIELD(optional_builds);FIELD(optional_edges);FIELD(optional_records);FIELD(optional_nodes);FIELD(optional_prunes);FIELD(optional_removed);
    FIELD(cap_attempts);FIELD(cap_rejects);FIELD(cap_removed);FIELD(cap_incidence_scans);
    FIELD(activation_reused);FIELD(cap_skipped_sizes);
    FIELD(switch_pairs);FIELD(switch_masks);FIELD(seed_partition_prunes);FIELD(frontier_screen_prunes);
#undef FIELD
    std::cout<<"\"bulk_ms\":"<<c.bulk_ms<<"}}";
}

int main(int argc,char** argv) {
    const auto beginning=Clock::now();
    try {
        std::string input,query_text,query_file,algorithm_name="budget";double time_limit_ms=0;
        int k=1,max_size=0;bool has_k=false,has_size=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--help") {
                std::cout<<"local_query --input graph.hg (--query 0,1 --k K --max-size S | --query-file queries.txt) --algorithm enum|region|budget|joint|A1|A2|A3|SECore --time-limit-ms MS\n"
                         <<"Query file: one k s q1 q2 ... per nonempty line. Limit 0 is unlimited.\n";return 0;
            }
            if(i+1==argc)throw std::invalid_argument("missing option value");
            const std::string value=argv[++i];
            if(arg=="--input")input=value;
            else if(arg=="--query")query_text=value;
            else if(arg=="--query-file")query_file=value;
            else if(arg=="--k"){k=integer(value);has_k=true;}
            else if(arg=="--max-size"){max_size=integer(value);has_size=true;}
            else if(arg=="--algorithm")algorithm_name=value;
            else if(arg=="--time-limit-ms") {std::size_t p=0;time_limit_ms=std::stod(value,&p);if(p!=value.size()||!std::isfinite(time_limit_ms)||time_limit_ms<0)throw std::invalid_argument("invalid time limit");}
            else throw std::invalid_argument("unknown option: "+arg);
        }
        if(input.empty()||query_text.empty()==query_file.empty())throw std::invalid_argument("specify input and exactly one query source");
        const bool ascending=algorithm_name=="A3-P-ascending";
        const bool no_transfer=algorithm_name=="A3-P-no-transfer";
        const bool frontier_only=algorithm_name=="A3-P-frontier";
        const bool structure_only=algorithm_name=="A3-P-structure";
        const bool no_direct=algorithm_name=="A3-P-no-direct";
        const bool no_joint=algorithm_name=="A3-P-no-local-joint";
        const bool switched=algorithm_name=="A3-P"||no_direct||no_joint;
        const bool guarded=algorithm_name=="A3-P-guarded"||no_transfer;
        const bool local_ablation=ascending||guarded||frontier_only||structure_only;
        Algorithm algorithm;
        if(algorithm_name=="enum")algorithm=Algorithm::Enum;
        else if(algorithm_name=="region")algorithm=Algorithm::Region;
        else if(algorithm_name=="budget")algorithm=Algorithm::Budget;
        else if(algorithm_name=="joint")algorithm=Algorithm::Joint;
        else if(algorithm_name=="A1")algorithm=Algorithm::A1;
        else if(algorithm_name=="A2"||algorithm_name=="A2-P"||algorithm_name=="A2-O"||algorithm_name=="A2-C"||algorithm_name=="A2-OC")algorithm=Algorithm::A2;
        else if(algorithm_name=="A3"||switched||local_ablation)algorithm=Algorithm::A3;
        else if(algorithm_name=="SECore")algorithm=Algorithm::SECore;
        else throw std::invalid_argument("unknown algorithm");
        std::vector<Query> queries;
        if(!query_file.empty()) {
            if(has_k||has_size)throw std::invalid_argument("batch file supplies k and size per line");
            std::ifstream in(query_file);if(!in)throw std::invalid_argument("cannot open query file");
            std::string line;
            while(std::getline(in,line)) {
                std::istringstream row(line);std::string token;
                if(!(row>>token)||token[0]=='#')continue;
                Query q;q.k=integer(token);if(!(row>>token))throw std::invalid_argument("query line lacks size");q.max_size=integer(token);
                while(row>>token)q.members.push_back(integer(token));
                queries.push_back(std::move(q));
            }
            if(queries.empty())throw std::invalid_argument("empty query file");
        } else {
            if(!has_k||!has_size)throw std::invalid_argument("single query requires explicit k and max-size");
            queries.push_back(Query{members(query_text),k,max_size});
        }
        const auto before_read=Clock::now();Graph graph=read_graph(input,false);
        const bool native=algorithm_name=="A1"||algorithm_name=="A2-OC"||switched||local_ablation;
        int batch_cap=0;
        for(const auto& q:queries)batch_cap=std::max(batch_cap,std::min(graph.n,q.max_size));
        const bool local=native&&(graph.n>=1000||local_ablation)&&batch_cap<=24;
        if(!local&&local_ablation)throw std::invalid_argument("A3 ablation requires the local activation path");
        if(!local)build_incidence(graph);
        const double preprocessing_ms=std::chrono::duration<double,std::milli>(Clock::now()-before_read).count();
        const auto before_index=Clock::now();
        std::unique_ptr<RankIndex> index;
        std::unique_ptr<LocalIncidence> local_incidence;
        if(local)local_incidence=std::make_unique<LocalIncidence>(graph,batch_cap,true);
        else if (algorithm==Algorithm::A2||algorithm==Algorithm::A3)
            index=std::make_unique<RankIndex>(graph,algorithm==Algorithm::A3&&!native);
        const double index_ms=std::chrono::duration<double,std::milli>(Clock::now()-before_index).count();
        std::cout<<std::setprecision(12)<<"{\"schema\":\"robust-local-v22-optional\",\"algorithm\":\""<<algorithm_name
                 <<"\",\"objective\":\"minimum-cardinality-with-size-cap\",\"n\":"<<graph.n<<",\"m\":"<<graph.edges.size()
                 <<",\"raw_m\":"<<graph.raw_edges<<",\"duplicate_edges\":"<<graph.duplicate_edges
                 <<",\"duplicate_members\":"<<graph.duplicate_members<<",\"small_edges_removed\":"<<graph.small_edges
                 <<",\"index_ms\":"<<index_ms<<",\"core_levels\":"<<(index ? index->levels.size() : 0)<<",\"local_activation\":"<<(local?"true":"false")<<",\"preprocessing_ms\":"<<preprocessing_ms<<",\"query_count\":"<<queries.size()<<",\"query_results\":[";
        double search_ms=0;
        for(std::size_t i=0;i<queries.size();++i) {
            const bool use_optional=algorithm_name=="A2-O"||algorithm_name=="A2-OC";
            const bool use_cap=algorithm_name=="A2-C"||algorithm_name=="A2-OC";
            const auto r=local ? LocalActivation(graph,*local_incidence,algorithm,time_limit_ms,
                                     guarded?LocalSchedule::Guarded:LocalSchedule::Ascending,!no_transfer,
                                     !structure_only,!frontier_only,!no_direct,!no_joint).run(queries[i])
                               : Solver(graph,algorithm,time_limit_ms,index.get(),
                                        algorithm_name=="A2-P"||switched||use_optional||use_cap,
                                        use_optional,use_cap,!no_direct).run(queries[i]);
            search_ms+=r.elapsed_ms;
            if(i)std::cout<<',';
            print_result(r);
        }
        const double total_ms=std::chrono::duration<double,std::milli>(Clock::now()-beginning).count();
        std::cout<<"],\"search_ms\":"<<search_ms<<",\"pipeline_ms\":"<<(preprocessing_ms+index_ms+search_ms)<<",\"total_ms\":"<<total_ms<<"}\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
