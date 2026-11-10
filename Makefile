CXX = g++
CXXFLAGS = -std=c++17 -O3 -DNDEBUG
HEADERS = $(wildcard src/*.hpp src/*.h src/*.inc)
CASES = experiments/case_study

.PHONY: all case-audits check-server
all: bin/hypergraph_query

case-audits: bin/dblp_audit bin/senate_audit

check-server:
	@test "$$(uname -s)" = Linux || { echo 'Compile and run C++ on the authorized Linux server.' >&2; exit 1; }

bin/hypergraph_query: src/main.cpp $(HEADERS) | check-server
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $< -o $@

bin/dblp_audit: $(CASES)/community_audit.cpp $(HEADERS) | check-server
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

bin/senate_audit: $(CASES)/strength/audit.cpp $(HEADERS) | check-server
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
