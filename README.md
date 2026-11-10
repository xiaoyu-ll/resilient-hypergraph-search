# Resilient Hypergraph Search

Boundary Activation Search (BAS) and Failure-Aware Boundary Search (FABS) find a minimum-size group containing the query members whose induced hypergraph remains connected and meets the support threshold after any single member fails. A failure removes the member and every incident hyperedge.

Build the solver and the DBLP and Senate snapshot auditors on the authorized Linux server with a C++17 compiler.

    make
    make case-audits

The solver preserves the experimental command-line IDs. Use A1 for BAS and A2-OC for FABS.

    bin/hypergraph_query --input graph.hg --query 0,1 --k 1 --max-size 16 --algorithm A1 --time-limit-ms 750
    bin/hypergraph_query --input graph.hg --query 0,1 --k 1 --max-size 16 --algorithm A2-OC --time-limit-ms 750

The input starts with the vertex and hyperedge counts. Each subsequent edge record contains its rank followed by its zero-based vertex IDs. Use the original normalized datasets to reproduce the case-study IDs.

    bin/dblp_audit data/coauth-DBLP.hg
    bin/senate_audit data/senate-bills.hg "150 153 176 181 187 188 191" 4

The auditors emit JSON with the original induced hyperedges and the support and connectivity of each failure snapshot. The optional rendering scripts under experiments/case_study also require the original case-study metadata and layout inputs. Their dependencies include Matplotlib, SciPilot for the panel scripts, and Gephi 0.10.1 for the Gephi layouts.
