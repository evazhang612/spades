#include "common.hpp"

#include "constructHashTable.hpp"
#include "graphConstruction.hpp"
#include "graphSimplification.hpp"
#include "graphio.hpp"
#include "readTracing.hpp"
#include "sequence.hpp"

using namespace paired_assembler;

LOGGER("p.main");

PairedGraph graph;

int main() {
	initConstants(ini_file);
	initGlobal();
	freopen(error_log.c_str(), "w",stderr);
	INFO("Constants inited...");

	cerr << l << " " << k;
//	LOG_ASSERT(1 == 0, "Something wrong");
	if (needPairs) {
		cerr << endl << " constructing pairs" << endl;
		readsToPairs(parsed_reads, parsed_k_l_mers);
	}
	if (needLmers) {
		cerr << endl << " constructing Lmers" << endl;
		pairsToLmers(parsed_k_l_mers, parsed_l_mers);
	}
	if (needSequences) {
		cerr << endl << " constructing Sequences" << endl;
		pairsToSequences(parsed_k_l_mers, parsed_l_mers, parsed_k_sequence);
	}
	//	map<>sequencesToMap(parsed_k_sequence);

	if (needGraph) {
		cerr << endl << " constructing Graph" << endl;
		constructGraph(graph);
	}
	outputLongEdges(graph.longEdges, graph, "data/beforeExpand.dot");

	expandDefinite(graph.longEdges, graph, graph.VertexCount, true);
	outputLongEdges(graph.longEdges, graph, "data/afterExpand.dot");
	outputLongEdgesThroughGenome(graph, "data/afterExpand_g.dot");

	char str[100];
	sprintf(str, "data/graph.txt");
	save(str,graph);

	traceReads(graph.verts, graph.longEdges, graph, graph.VertexCount, graph.EdgeId);
	outputLongEdges(graph.longEdges,"data/ReadsTraced.dot");
	outputLongEdgesThroughGenome(graph, "data/ReadsTraced_g.dot");

	graph.recreateVerticesInfo(graph.VertexCount, graph.longEdges);
	while (processLowerSequence(graph.longEdges, graph, graph.VertexCount))
	{
		graph.recreateVerticesInfo(graph.VertexCount, graph.longEdges);
		expandDefinite(graph.longEdges , graph, graph.VertexCount);
	}
	outputLongEdges(graph.longEdges,"data/afterLowers.dot");
	outputLongEdgesThroughGenome(graph, "data/afterLowers_g.dot");

	graph.recreateVerticesInfo(graph.VertexCount, graph.longEdges);
	outputLongEdges(graph.longEdges, graph, "data/afterLowers_info.dot");

	cerr << "\n Finished";
	return 0;
}
