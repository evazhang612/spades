//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************



/* Feeling responsible for this poor code, let me explain what is written here.
 * At first, forget about rectangles abstraction. This is another approach.
 * This repeat resolver deals with compressed de Bruijn graph and transforms it into "splitted" compressed de Bruijn graph - the same object with condition on unique placement of k-mers broken.
 * If some edge is detected to be repeat, it can be present multiple times.
 * Initial compressed de Bruijn graph edges are "atomic" - they never can be broken at any point
 *
 *
 * Main idea is quite simple - we iterate over vertices of the graph. For each given vertex V, we consider it's incoming and outgoing edges
 * For each of this edges, we look on "paired information" - received with paired-end reads mapping and distance estimation module info about edges folllowing it.
 * We have same "backward" info too, but we can not use forward and backward info simultaniously(will be explained later why).
 * Thus information allows us to determine the expexted multiplicity of each incoming/outgoing edges.
 * For example, if we have two elements of paired information matching one edge to unrelated places in graph, than that means that this edge has multiplicity at least two.
 * Moreover, comparing such information from incoming and outgoing edges, we can determine which of them can follow each other.
 * So, we color all incoming and outgoing edges(some - possibly with multiple colors), and then split vertex and possibly edges.
 *
 * NB - because we look on only forward information, when splitting edges we also are able to split paired information(splitting is done with help of this paired info)
 *
 * That's the main idea of this algorithm. Code related to components(i.e. fillVerticesComponents) is responsible to reverse-compliment structure handling
 */

#ifndef REPEAT_RESOLVER_HPP_
#define REPEAT_RESOLVER_HPP_
#include <cmath>
#include <set>
#include <map>
#include <vector>
#include <algorithm>

#include "logger/logger.hpp"
#include "simple_tools.hpp"
#include "de/paired_info.hpp"
#include "deleted_vertex_handler.hpp"
#include "config_struct.hpp"

#include "omni/omni_utils.hpp"
#include "omni/omni_tools.hpp"
#include "omni/splitters.hpp"
#include "omni/id_track_handler.hpp"
#include "omni/edges_position_handler.hpp"
#include "omni/total_labeler.hpp"
#include "omni/dijkstra.hpp"

#include "debruijn_graph.hpp"
#include "perfcounter.hpp"
#include "split_path_constructor.hpp"

namespace debruijn_graph {

#define MAX_DISTANCE_CORRECTION 10

using omnigraph::SmartVertexIterator;
using omnigraph::Compressor;
using omnigraph::PairedInfoIndexT;
using omnigraph::PairInfoIndexData;
using debruijn_graph::DeletedVertexHandler;

template<class Graph>
class RepeatResolver {
	typedef PathInfoClass<Graph> PathInfo;
	typedef typename Graph::VertexId VertexId;

	typedef omnigraph::PairedInfoIndexT<Graph> PIIndex;
	typedef omnigraph::PairInfo<EdgeId> PairInfo;
	typedef vector<PairInfo> PairInfos;
	typedef PairInfoIndexData<EdgeId> MixedData;

	typedef set<Point> Histogram;

public:

	class FastDistanceCounter {

	private:
		map<VertexId, map<VertexId, size_t> > distances;
		Graph &graph_;
		int depth_;
		BoundedDijkstra<Graph, int> dij;

	public:

		FastDistanceCounter(Graph &graph, int depth) :
				graph_(graph), depth_(depth), dij(graph_, depth_) {
			distances.clear();
		}
		int GetDistances(VertexId start, VertexId end) {
			if (distances.find(start) == distances.end()) {
				dij.run(start);
				auto interval = dij.GetDistances();
				map<VertexId, size_t> inserting_map;
				for (auto ind = interval.first; ind != interval.second; ++ind) {
					inserting_map.insert(make_pair(ind->first, ind->second));
				}
				distances.insert(make_pair(start, inserting_map));
			}
			if (distances[start].find(end) == distances[start].end())
				return 1e9;
			else
				return distances[start][end];
		}
	};

	class EdgeInfo {
	public:

		EdgeInfo(const PairInfo &lp_, const int dir_, const EdgeId edge_,
				const double d_) :
				lp(lp_), dir(dir_), edge(edge_), d(d_) {

		}
		inline EdgeId getEdge() {
			return edge;
		}

		bool isClose(double a, double b, double max_diff) {
			return (abs(a - b) < max_diff);
		}

		bool follow(EdgeInfo &other_info, const Graph &old_graph) {
			bool res = (old_graph.EdgeEnd(other_info.lp.second)
					== old_graph.EdgeStart(lp.second))
					&& (isClose(
							old_graph.length(other_info.lp.second)
									+ other_info.lp.d(), lp.d(),
							0.1 + lp.var() + other_info.lp.var()));
			return (res);
		}

		bool IsEdgesOnDistanceAdjacent(EdgeId edge, double d, EdgeId other_edge,
				double other_d, const Graph &old_graph, double max_diff,
				bool first_equal, const IdTrackHandler<Graph> &old_IDs,
				FastDistanceCounter &distance_counter) {

			VertexId v_s = old_graph.EdgeStart(edge);
			VertexId v_e = old_graph.EdgeEnd(edge);
			int flag = 0;

			VertexId other_v_s = old_graph.EdgeStart(other_edge);
			VertexId other_v_e = old_graph.EdgeEnd(other_edge);
			//TODO: insert distance!!!
			int len = old_graph.length(edge);
			int other_len = old_graph.length(other_edge);

			int forward_distance = distance_counter.GetDistances(v_e,
					other_v_s);
			int backward_distance = distance_counter.GetDistances(other_v_e,
					v_s);
			if (isClose(d + len + forward_distance, other_d, max_diff)) {
				if (flag) {
					DEBUG("first true");
				}
				return true;
			}

			if (isClose(other_d + other_len + backward_distance, d, max_diff)) {
				if (flag) {
					DEBUG("2nd true");
				}
				return true;
			}

			if ((other_edge == edge && isClose(d, other_d, max_diff)))
				return true;

			if (first_equal) {
				if ((v_e == other_v_s && isClose(d + len, other_d, max_diff))
						|| (v_s == other_v_e
								&& isClose(d, other_d + other_len, max_diff))
						|| (other_edge == edge && isClose(d, other_d, max_diff))) {
					if (flag) {
						DEBUG("3rd true");
					}
					return true;
				} else {
					if (flag) {
						DEBUG("first false");
					}
					return false;
				}
			}
			if (flag) {
				DEBUG("second false");
			}
			return false;
		}

		bool isAdjacent(EdgeInfo other_info, const Graph &old_graph,
				const Graph &new_graph, EdgeLabelHandler<Graph> &labels_after,
				const TotalLabeler<Graph>& tot_lab,
				const IdTrackHandler<Graph> &old_IDs,
				FastDistanceCounter &distance_counter) {
			if ((lp.first != other_info.lp.first)
					&& (new_graph.EdgeStart(lp.first)
							!= new_graph.EdgeEnd(lp.first))
					&& (new_graph.EdgeStart(other_info.lp.first)
							!= new_graph.EdgeEnd(other_info.lp.first))) {
				if ((new_graph.EdgeStart(lp.first)
						== new_graph.EdgeStart(other_info.lp.first))
						|| (new_graph.EdgeEnd(lp.first)
								== new_graph.EdgeEnd(other_info.lp.first))) {
					TRACE("isAdjacent false on 1 condition");
					return false;
				}
			}

			if (lp.first == other_info.lp.first)
				if (new_graph.length(lp.first)
						> cfg::get().rr.max_repeat_length) {
					TRACE("isAdjacent true on 2 condition");
					return true;
				}

			double max_diff = max(lp.var(), other_info.lp.var()) + 0.5 + 1e-9;

			bool old_res = IsEdgesOnDistanceAdjacent(this->edge, this->d,
					other_info.getEdge(), other_info.getDistance(), old_graph,
					max_diff, lp.first == other_info.lp.first, old_IDs,
					distance_counter);
			return old_res;
		}

		inline double getDistance() {
			return d;
		}
	public:
		PairInfo lp;
		int dir;
		EdgeId edge;
		double d;

	};

	map<EdgeId, EdgeId> GetEdgeLabels() {
		return edge_labels;
	}

	RepeatResolver(Graph &old_graph_,
	        IdTrackHandler<Graph> &old_IDs_,
			const PIIndex &ind, EdgesPositionHandler<Graph> &old_pos_,
			Graph &new_graph_, IdTrackHandler<Graph> &new_IDs_,
			EdgesPositionHandler<Graph> &new_pos_,
			DeletedVertexHandler<Graph> &deleted_handler_,
			EdgeLabelHandler<Graph> &LabelsAfter_, bool developer_mode,
            const io::SequencingLibrary<debruijn_config::DataSetData> &lib
			) :
			new_graph(new_graph_), old_graph(old_graph_), new_IDs(new_IDs_), old_IDs(
					old_IDs_), new_pos(new_pos_), old_pos(old_pos_), deleted_handler(
					deleted_handler_), labels_after(LabelsAfter_), distance_counter(
					old_graph_, cfg::get().rr.max_distance), developer_mode_(
					developer_mode), lib_(lib) {

		TRACE("Constructor started");
		map<VertexId, VertexId> old_to_new;
		map<EdgeId, EdgeId> old_to_new_edge;
		cheating_mode = 0;
		rc_mode = cfg::get().rr.symmetric_resolve;
		global_cheating_edges.clear();
		size_t paired_size = 0;
		set<VertexId> vertices;
		set<VertexId> rc_vertices;
		vertices.clear();
		set<EdgeId> edges;
		set<EdgeId> rc_edges;
		edges.clear();
		near_vertex = cfg::get().rr.near_vertex;
		for (auto v_iter = old_graph.SmartVertexBegin(); !v_iter.IsEnd();
				++v_iter) {
			{
				vertices.insert(*v_iter);
				TRACE(*v_iter);
			}
		}
		for (auto e_iter = old_graph.SmartEdgeBegin(); !e_iter.IsEnd();
				++e_iter) {
			{
				edges.insert(*e_iter);
				TRACE("edge added to array " << *e_iter);
			}
		}
		for (auto v_iter = vertices.begin(); v_iter != vertices.end();
				++v_iter) {
			if (rc_mode) {
				if (rc_vertices.find(*v_iter) == rc_vertices.end())
					rc_vertices.insert(conj_wrap(old_graph, *v_iter));
				else
					continue;
			}

			size_t degree = old_graph.IncomingEdgeCount(*v_iter)
					+ old_graph.OutgoingEdgeCount(*v_iter);
			if (degree > 0) {
				VertexId new_vertex = new_graph.AddVertex();

				TRACE("Added vertex" << new_vertex);
				vertex_labels[new_vertex] = *v_iter;
				old_to_new[*v_iter] = new_vertex;
				new_IDs.AddVertexIntId(new_vertex,
						old_IDs.ReturnIntId(*v_iter));
				if (rc_mode) {
					VertexId new_rc_vertex = conj_wrap(new_graph, new_vertex);
					VertexId old_rc_vertex = conj_wrap(old_graph, *v_iter);
					vertex_labels[new_rc_vertex] = old_rc_vertex;
					old_to_new[old_rc_vertex] = new_rc_vertex;
					new_IDs.AddVertexIntId(new_rc_vertex,
							old_IDs.ReturnIntId(old_rc_vertex));
				}
			}

		}
		DEBUG("vertices copied");
		for (auto e_iter = edges.begin(); e_iter != edges.end(); ++e_iter) {
			if (rc_mode) {
				if (rc_edges.find(*e_iter) == rc_edges.end())
					rc_edges.insert(conj_wrap(old_graph, *e_iter));
				else
					continue;
			}
			TRACE(
					"Adding edge from " << old_to_new[old_graph.EdgeStart(*e_iter)] <<" to " << old_to_new[old_graph.EdgeEnd(*e_iter)]);
			EdgeId new_edge = new_graph.AddEdge(
					old_to_new[old_graph.EdgeStart(*e_iter)],
					old_to_new[old_graph.EdgeEnd(*e_iter)],
					old_graph.EdgeNucls(*e_iter));
			new_IDs.AddEdgeIntId(new_edge, old_IDs.ReturnIntId(*e_iter));
			WrappedSetCoverage(new_edge,
					old_graph.coverage(*e_iter) * old_graph.length(*e_iter));

			edge_labels[new_edge] = *e_iter;
			TRACE("Adding edge " << new_edge<< " from" << *e_iter);
			old_to_new_edge[*e_iter] = new_edge;
			if (developer_mode_)
				new_pos.AddEdgePosition(new_edge,
						old_pos.edges_positions().find(*e_iter)->second);

			if (rc_mode) {
				EdgeId new_rc_edge = conj_wrap(new_graph, new_edge);
				EdgeId old_rc_edge = conj_wrap(old_graph, *e_iter);
				edge_labels[new_rc_edge] = old_rc_edge;
				old_to_new_edge[old_rc_edge] = new_rc_edge;
				if (developer_mode_)
					new_pos.AddEdgePosition(new_rc_edge,
							old_pos.edges_positions().find(old_rc_edge)->second);
				TRACE("rc edge added");
				new_IDs.AddEdgeIntId(new_rc_edge,
						old_IDs.ReturnIntId(old_rc_edge));
			}

		}
		TRACE("Edge Adding finished");
		old_to_new.clear();

		DEBUG("edges copied");
		DEBUG("Copying of paired info started");
		for (auto p_iter = ind.begin(), p_end_iter = ind.end();
				p_iter != p_end_iter; ++p_iter) {
			EdgeId e1 = p_iter.first();
			EdgeId e2 = p_iter.second();
			const Histogram& hist = *p_iter;
			paired_size += hist.size();
			for (auto it = hist.begin(); it != hist.end(); ++it) {
				const Point& p = *it;
				if (old_to_new_edge.find(e1) != old_to_new_edge.end()
						&& old_to_new_edge.find(e2) != old_to_new_edge.end()) {
					TRACE(
							"Adding pair " << e1<<"  " <<old_to_new_edge[e1] << "  " <<e2);
					PairInfo tmp(old_to_new_edge[e1], e2, p.d, p.weight, p.var);
					paired_di_data.AddPairInfo(tmp, 0);
				} else {
					DEBUG(
							"Paired Info with deleted edge! " << e1 << "  " << e2);
				}
			}
		}

		DEBUG("paired info copied");
		int zero_paired_length = 0;
		for (auto e_iter = edges.begin(); e_iter != edges.end(); ++e_iter) {
			const PairInfos &pi = paired_di_data.GetEdgeInfos(
					old_to_new_edge[*e_iter]);
			bool cheat_edge = true;

			for (size_t i = 0; i < pi.size(); ++i) {
				if ((pi[i].weight() > 1e-8) && (pi[i].d() >= 0)) {
					cheat_edge = false;
					break;
				}
			}
			if (cheat_edge) {
				zero_paired_length += old_graph.length(*e_iter);
				global_cheating_edges.insert(old_to_new_edge[*e_iter]);
				TRACE(
						"Global cheater add "<<old_to_new_edge[*e_iter]<<" id "<<new_graph.int_id(old_to_new_edge[*e_iter]));
			}
		}
		INFO(
				"Total length of edges with no paired info: " << zero_paired_length);
		DEBUG("May be size is " << ind.size());
		INFO("Paired info size: " << paired_size);
	}
	void ResolveRepeats(const string& output_folder);

private:

	static size_t low_cov_count;
	static size_t max_low_cov_count;

	avg_perf_counter adjacent_time;
	avg_perf_counter rectangle_resolve_1_time;
	avg_perf_counter rectangle_resolve_2_time;
	avg_perf_counter rectangle_resolve_3_time;
	avg_perf_counter produce_pair_info_time;
	avg_perf_counter multisplit_time;
	avg_perf_counter resolve_time;
	int near_vertex;
	map<int, VertexId> fillVerticesAuto();
	map<int, VertexId> fillVerticesComponents();
	map<int, VertexId> fillVerticesComponentsInNonVariableOrder();
	size_t SplitResolveVertex(VertexId vid, TotalLabeler<Graph>& tot_labler);
	void BanRCVertex(VertexId v);
	ConjugateDeBruijnGraph::VertexId conj_wrap(const ConjugateDeBruijnGraph& g,
			ConjugateDeBruijnGraph::VertexId v);
	NonconjugateDeBruijnGraph::VertexId conj_wrap(
			const NonconjugateDeBruijnGraph& g,
			NonconjugateDeBruijnGraph::VertexId v);

	ConjugateDeBruijnGraph::EdgeId conj_wrap(const ConjugateDeBruijnGraph& g,
			ConjugateDeBruijnGraph::EdgeId e);
	NonconjugateDeBruijnGraph::EdgeId conj_wrap(
			const NonconjugateDeBruijnGraph& g,
			NonconjugateDeBruijnGraph::EdgeId e);
	bool rc_mode;
	void WrappedSetCoverage(EdgeId e, int cov);
	size_t GenerateVertexPairedInfo(Graph &g, MixedData &ind, VertexId vid);
	vector<typename Graph::VertexId> MultiSplit(VertexId v);
	int ColoringEdgesInfosByPathes(int &size, TotalLabeler<Graph>& tot_labler,
			VertexId V_Id);
	vector<PathInfo> ConvertEdgeInfosToPathes();
	vector<int> ColoringPathes(vector<PathInfo> &pathes, VertexId V_Id);
	bool pathesAdjacent(PathInfo& path1, PathInfo & path2, VertexId V_Id);
	int prefix_or_included(PathInfo&path1, PathInfo&path2, int shift1,
			int shift2);

	size_t original_id(typename Graph::EdgeId e) {
		return old_graph.int_id(labels_after.edge_labels[e][0]);
	}

	std::string PrintPath(PathInfo &path) {
		std::ostringstream ss;
		ss << " " << new_graph.int_id(path[0].first) << "("
				<< original_id(path[0].first) << "): ";
		for (size_t j = 1; j < path.size(); j++) {
			ss << "(" << old_graph.int_id(path[j].first) << ", "
					<< path[j].second << "), ";
		}
		return ss.str();
	}

	void dfs(vector<vector<int> > &edge_list, vector<int> &colors, int cur_vert,
			int cur_color);

	Graph &new_graph;
	const Graph &old_graph;
	IdTrackHandler<Graph> &new_IDs;
	const IdTrackHandler<Graph> &old_IDs;
	EdgesPositionHandler<Graph> &new_pos;
	const EdgesPositionHandler<Graph> &old_pos;
	DeletedVertexHandler<Graph> &deleted_handler;
	EdgeLabelHandler<Graph> &labels_after;
	vector<int> edge_info_colors;
	vector<EdgeInfo> edge_infos;
	MixedData paired_di_data;
	map<VertexId, VertexId> vertex_labels;
	map<EdgeId, EdgeId> edge_labels;

	int cheating_mode;
	map<EdgeId, int> local_cheating_edges;
	set<EdgeId> global_cheating_edges;
	map<VertexId, int> resolving_vertices_degrees;
	int sum_count;
	FastDistanceCounter distance_counter;
	const bool developer_mode_;

	const io::SequencingLibrary<debruijn_config::DataSetData> &lib_;

private:
	DECL_LOGGER("RepeatResolver")
};

template<class Graph> size_t RepeatResolver<Graph>::low_cov_count = 0;
template<class Graph> size_t RepeatResolver<Graph>::max_low_cov_count = 1;

template<class Graph>
vector<typename Graph::VertexId> RepeatResolver<Graph>::MultiSplit(VertexId v) {
	multisplit_time.start();
	int k = 0;
	vector<EdgeId> edgeIds[2];
	edgeIds[0] = new_graph.OutgoingEdges(v);
	edgeIds[1] = new_graph.IncomingEdges(v);
	map<EdgeId, int> edgeCounts;
	for (int i = 0; i < 2; i++) {
		for (size_t j = 0; j < edgeIds[i].size(); j++) {
			edgeCounts.insert(make_pair(edgeIds[i][j], 0));
		}
	}

	vector<VertexId> res;

	if (!(new_graph.SplitCondition(v, edgeIds[0])
			&& new_graph.SplitCondition(v, edgeIds[1]))) {
		DEBUG("Splitting blocked by both edges (conjugate and normal)");
		res.resize(1);
		res[0] = v;
		return res;
	}

	for (size_t i = 0; i < edge_info_colors.size(); i++) {
		if (edge_info_colors[i] >= k)
			k = edge_info_colors[i];
		EdgeId le = edge_infos[i].lp.first;
		edgeCounts[le]++;
	}

	if (k == 0) {
		DEBUG("NOTHING TO SPLIT:( ");
		res.resize(1);
		res[0] = v;
		return res;
	}

	size_t nonpaired = 0;
	for (auto iter = edgeCounts.begin(); iter != edgeCounts.end(); ++iter) {
		if (iter->second == 0) {
			DEBUG(
					"Adding non-paired edge " << new_IDs.ReturnIntId(iter->first) << " (potential bug here)");
			++nonpaired;
			if (cheating_mode == 2) {
				PairInfos tmp = paired_di_data.GetEdgeInfos(iter->first);
				int fl = 0;
				for (size_t j = 0; j < tmp.size(); j++) {
					EdgeId right_id = tmp[j].second;
					double d = tmp[j].d();
					int w = tmp[j].weight();
					if (w < 1e-8)
						continue;
					int dif_d = 0;
					fl++;
					int dir = 0;
					// it doesn't matter now
					EdgeInfo ei(tmp[j], dir, right_id, int(d - dif_d));
					edge_infos.push_back(ei);
					edge_info_colors.push_back(k);
				}
				if (fl)
					k++;

			} else {
				DEBUG("Edge without pair info blocking split");
				res.resize(1);
				res[0] = v;
				return res;
			}

		}
	}
	if (nonpaired) {
		WARN("Added " << nonpaired << " non-paired edges");
	}
	k++;
	DEBUG("splitting to "<< k <<" parts");

	if (k == 1) {
		DEBUG("NOTHING TO SPLIT:( ");
		res.resize(1);
		res[0] = v;
		return res;
	}

	for (auto iter = edgeCounts.begin(); iter != edgeCounts.end(); ++iter) {
		if (iter->second > 1) {
			paired_di_data.DeleteEdgeInfo(iter->first);

		} else if (iter->second == 1) {
			int updated_edge_color = -1;
			for (size_t j = 0; j < edge_infos.size(); j++) {
				if (edge_infos[j].lp.first == iter->first) {
					if (updated_edge_color == -1) {
						updated_edge_color = edge_info_colors[j];
					} else {
						if (updated_edge_color != edge_info_colors[j]) {
							WARN(
									"Different colors found for one colored edge info");
						}
					}
				}
			}

			if (updated_edge_color > -1) {
				for (size_t j = 0; j < edge_infos.size(); j++) {
					if ((edge_info_colors[j] == updated_edge_color)
							&& (edge_infos[j].lp.first == iter->first)) {
						edge_info_colors.erase(edge_info_colors.begin() + j);
						edge_infos.erase(edge_infos.begin() + j);
						j--;
					}
				}
				PairInfos tmp = paired_di_data.GetEdgeInfos(iter->first);
				for (size_t info_j = 0; info_j < tmp.size(); info_j++) {
					EdgeInfo ei(tmp[info_j], 0, tmp[info_j].second, 0); //check
					edge_infos.push_back(ei);
					edge_info_colors.push_back(updated_edge_color);
				}
				paired_di_data.DeleteEdgeInfo(iter->first);
			}
		}
	}

	vector<vector<EdgeId> > edges_for_split(k);

	map<EdgeId, double> old_paired_coverage;
	vector<map<EdgeId, double>> colored_paired_coverage(k);
	//Remember: because of loops there can be same edges in edgeIds[0] and edgeIds[1]. Only possible for loops of length 1.

	for (size_t i = 0; i < edge_info_colors.size(); i++) {
		EdgeId le = edge_infos[i].lp.first;
		int color = edge_info_colors[i];
		if (old_paired_coverage.find(le) == old_paired_coverage.end())
			old_paired_coverage[le] = 0;
		old_paired_coverage[le] += edge_infos[i].lp.weight();
		if (colored_paired_coverage[color].find(le)
				== colored_paired_coverage[color].end())
			colored_paired_coverage[color][le] = 0;
		colored_paired_coverage[color][le] += edge_infos[i].lp.weight();
	}

	map<EdgeId, int> OldCopyCnt;
	vector<EdgeId> LiveNewEdges;
	vector<EdgeId> LiveProtoEdges;

	size_t not_found = 0;
	size_t low_coverage = 0;

	double cutting_coverage = 0;

	VERIFY(cfg::get().ds.avg_coverage());
	cutting_coverage = cfg::get().ds.avg_coverage() * cfg::get().rr.inresolve_cutoff_proportion / 2;

	for (int i = 0; i < k; i++) {
		vector<EdgeId> split_edge;
		vector<double> split_coeff;
		for (auto it = colored_paired_coverage[i].begin();
				it != colored_paired_coverage[i].end(); it++) {
			if (it->second != 0) {
				split_edge.push_back(it->first);
				if (local_cheating_edges.find(it->first)
						!= local_cheating_edges.end()) {
					DEBUG("local_cheater found");
					local_cheating_edges[it->first]++;
				}
				split_coeff.push_back(
						((double) it->second) / old_paired_coverage[it->first]);
			} else {
				DEBUG("Zero covered pair info");
			}
		}
		DEBUG("split_edge size " << split_edge.size());
		if (split_edge.size() > 0 && new_graph.SplitCondition(v, split_edge)) {
			pair<VertexId, vector<pair<EdgeId, EdgeId>>> split_pair = new_graph.SplitVertex(v, split_edge, split_coeff);
			res.push_back(split_pair.first);
			if (rc_mode) {
				for( auto it = split_pair.second.begin(); it != split_pair.second.end(); ++it) {
					WrappedSetCoverage(conj_wrap(new_graph, it->second), new_graph.coverage(it->second) * new_graph.length(it->second));
				}
			}
			map<EdgeId, EdgeId> old_to_new_edgeId;
			for(auto it = split_pair.second.begin(); it != split_pair.second.end(); ++it) {
				old_to_new_edgeId[it->first] = it->second;
				OldCopyCnt[it->first]++;
				edge_labels[it->second] = edge_labels[it->first];

				if (cheating_mode) {
					if (local_cheating_edges.find(it->first) != local_cheating_edges.end()) {
						if (local_cheating_edges[it->first] == 0 ) {
							DEBUG("0 copies of edge " << new_IDs.ReturnIntId(it->first) << " found");
							++not_found;
						} else {
							if (local_cheating_edges[it->first] == 1 ) {
								DEBUG( "cheating OK, no global cheaters needed(but actually added)");
							} else {
								DEBUG( "cheating OK");
							}

							global_cheating_edges.insert(it->second);
							TRACE("Global cheater add "<<it->second<<" id "<<new_graph.int_id(it->second));

						}
					}
				}
			}

			for (size_t j = 0; j < edge_infos.size(); j++) {
				if (edge_info_colors[j] == i) {
					paired_di_data.ReplaceFirstEdge(edge_infos[j].lp, old_to_new_edgeId[edge_infos[j].lp.first]);
					DEBUG("Replace first edge: new info is " << new_IDs.ReturnIntId(old_to_new_edgeId[edge_infos[j].lp.first])<<" << "<< new_IDs.ReturnIntId(edge_infos[j].lp.first)<<"  "<< old_IDs.ReturnIntId(edge_infos[j].lp.second) <<" "<< edge_infos[j].lp.d());

				}
			}

			for(auto it = split_pair.second.begin(); it != split_pair.second.end(); ++it) {

				if ((OldCopyCnt[it->first] > 1 )&&(new_graph.coverage(it->second) < cutting_coverage) && ((new_graph.IsDeadStart(split_pair.first)&& !(new_graph.IsDeadStart(v))) || (new_graph.IsDeadEnd(split_pair.first)&& !(new_graph.IsDeadEnd(v))))&&(edgeCounts[it->first] > 1)) {
					OldCopyCnt[it->first]--;
					DEBUG("Deleting just created copy of edge " << new_IDs.ReturnIntId(it->first) << " because of low coverage");
					++low_coverage;

					paired_di_data.DeleteEdgeInfo(it->second);
					global_cheating_edges.erase(it->second);
					if (rc_mode) {
						paired_di_data.DeleteEdgeInfo(conj_wrap(new_graph,it->second));
						global_cheating_edges.erase(conj_wrap(new_graph,it->second));
					}

					VertexId v_start = new_graph.EdgeStart(it->second);
					VertexId v_end = new_graph.EdgeEnd(it->second);
					new_graph.DeleteEdge(it->second);

					if (rc_mode) {
						if ((v_start == v_end) || (v_start == conj_wrap(new_graph, v_end))) {
							if (new_graph.IncomingEdgeCount(v_start) + new_graph.OutgoingEdgeCount(v_start) == 0) {
								new_graph.DeleteVertex(v_start);
								DEBUG(" Vertex removed");
							}
						} else {
							if (new_graph.IncomingEdgeCount(v_start) + new_graph.OutgoingEdgeCount(v_start) == 0) {
								new_graph.DeleteVertex(v_start);
								DEBUG(" Vertex removed");
							}
							if (new_graph.IncomingEdgeCount(v_end) + new_graph.OutgoingEdgeCount(v_end) == 0) {
								new_graph.DeleteVertex(v_end);
								DEBUG(" Vertex removed");
							}
						}
					} else {
						if ((v_start == v_end)) {
							if (new_graph.IncomingEdgeCount(v_start) + new_graph.OutgoingEdgeCount(v_start) == 0) {
								new_graph.DeleteVertex(v_start);
								DEBUG(" Vertex removed");
							}
						} else {
							if (new_graph.IncomingEdgeCount(v_start) + new_graph.OutgoingEdgeCount(v_start) == 0) {
								new_graph.DeleteVertex(v_start);
								DEBUG(" Vertex removed");
							}
							if (new_graph.IncomingEdgeCount(v_end) + new_graph.OutgoingEdgeCount(v_end) == 0) {
								new_graph.DeleteVertex(v_end);
								DEBUG(" Vertex removed");
							}
						}
					}

				} else {
					LiveNewEdges.push_back(it->second);
					LiveProtoEdges.push_back(it->first);
				}
			}
		}
		if (not_found) {
			WARN("For " << not_found << " edges, no copies of them were found");
		}
		if (low_coverage) {
			low_cov_count += low_coverage;
			if (low_cov_count > max_low_cov_count) {
				max_low_cov_count = max_low_cov_count * 2;
				WARN(
						"Deleted " << low_cov_count << " just-created edges due to low coverage");
				low_cov_count = 0;
			}
		}
	}

	TRACE("process global cheaters");
	if (rc_mode) {
		for (size_t i = 0; i < LiveNewEdges.size(); i++) {
			if (OldCopyCnt[LiveProtoEdges[i]] > 1) {
				global_cheating_edges.insert(
						conj_wrap(new_graph, LiveNewEdges[i]));
				TRACE(
						"Global cheater add "<<conj_wrap(new_graph, LiveNewEdges[i])<<" id "<<new_graph.int_id(conj_wrap(new_graph, LiveNewEdges[i])));

			} else if (OldCopyCnt[LiveProtoEdges[i]] == 1) {
				EdgeId tmp_ei = conj_wrap(new_graph, LiveProtoEdges[i]);
				EdgeId tmp_ei_new = conj_wrap(new_graph, LiveNewEdges[i]);
				if (tmp_ei_new != LiveNewEdges[i]) {
					PairInfos conj_tmp = paired_di_data.GetEdgeInfos(tmp_ei);
					for (size_t info_cj = 0; info_cj < conj_tmp.size();
							info_cj++) {
						DEBUG(
								"Pi fi "<<new_IDs.str(conj_tmp[info_cj].first)<<" to "<< new_IDs.str(tmp_ei_new));
						paired_di_data.ReplaceFirstEdge(conj_tmp[info_cj],
								tmp_ei_new);
					}
				}
			}
		}
	}

	TRACE("split finished, deleting vertex");
	for (int i = 0; i < 2; i++) {
		for (size_t j = 0; j < edgeIds[i].size(); j++) {
			paired_di_data.DeleteEdgeInfo(edgeIds[i][j]);
			global_cheating_edges.erase(edgeIds[i][j]);
			if (rc_mode) {
				paired_di_data.DeleteEdgeInfo(
						conj_wrap(new_graph, edgeIds[i][j]));
				global_cheating_edges.erase(
						conj_wrap(new_graph, edgeIds[i][j]));
			}
		}
	}
	new_graph.ForceDeleteVertex(v);
	TRACE("Delete ok");

	DEBUG("Res size "<<res.size());

	multisplit_time.stop();
	return res;

}
template<class Graph>
void RepeatResolver<Graph>::BanRCVertex(VertexId v) {
	int id = new_IDs.ReturnIntId(v);
	VertexId rv = conj_wrap(new_graph, v);
	int rc_id = new_IDs.ReturnIntId(rv);
	DEBUG("added vertex " << id << " banning vertex "<< rc_id);
	vector<EdgeId> tmp = new_graph.IncomingEdges(rv);
	for (size_t i = 0; i < tmp.size(); i++) {
		global_cheating_edges.insert(tmp[i]);
		TRACE("Global cheater add "<<tmp[i]<<" id "<<new_graph.int_id(tmp[i]));

	}
	TRACE("incoming cheaters added");
	tmp = new_graph.OutgoingEdges(rv);
	for (size_t i = 0; i < tmp.size(); i++) {
		global_cheating_edges.insert(tmp[i]);
		TRACE("Global cheater add "<<tmp[i]<<" id "<<new_graph.int_id(tmp[i]));

	}
	TRACE("outgoing cheaters added");
}

template<class Graph>
ConjugateDeBruijnGraph::VertexId RepeatResolver<Graph>::conj_wrap(
		const ConjugateDeBruijnGraph& g, ConjugateDeBruijnGraph::VertexId v) {
	return g.conjugate(v);
}

template<class Graph>
NonconjugateDeBruijnGraph::VertexId RepeatResolver<Graph>::conj_wrap(
		const NonconjugateDeBruijnGraph& g,
		NonconjugateDeBruijnGraph::VertexId v) {
	VERIFY(0);
	return v;
}

template<class Graph>
ConjugateDeBruijnGraph::EdgeId RepeatResolver<Graph>::conj_wrap(
		const ConjugateDeBruijnGraph& g, ConjugateDeBruijnGraph::EdgeId e) {
	return g.conjugate(e);
}

template<class Graph>
NonconjugateDeBruijnGraph::EdgeId RepeatResolver<Graph>::conj_wrap(
		const NonconjugateDeBruijnGraph& g,
		NonconjugateDeBruijnGraph::EdgeId e) {
	VERIFY(0);
	return e;
}

template<class Graph>
void RepeatResolver<Graph>::WrappedSetCoverage(EdgeId e, int cov) {
	if (rc_mode == 0) {
		new_graph.coverage_index().SetCoverage(e, cov);
	} else {
		new_graph.coverage_index().SetCoverage(e, cov);
		EdgeId rc_e = conj_wrap(new_graph, e);
		new_graph.coverage_index().SetCoverage(rc_e, cov);

	}

}

template<class Graph>
map<int, typename Graph::VertexId> RepeatResolver<Graph>::fillVerticesAuto() {
	map<int, typename Graph::VertexId> vertices;
	vertices.clear();
	for (auto v_iter = new_graph.SmartVertexBegin(); !v_iter.IsEnd();
			++v_iter) {
		vertices.insert(
				make_pair(10000 - new_IDs.ReturnIntId(*v_iter), *v_iter));
	}
	return vertices;
}

namespace details {
template<class Graph>
struct VertexCompositId {
	typename Graph::VertexId Id;
	size_t intId;
	size_t componentId;
};

struct CompositIdCompare {
	template<class Id>
	bool operator()(Id i, Id j) {
		if (i.componentId < j.componentId)
			return true;
		if (j.componentId < i.componentId)
			return false;
		if (i.intId < j.intId)
			return true;
		return false;
	}
};
}

template<class Graph>
map<int, typename Graph::VertexId> RepeatResolver<Graph>::fillVerticesComponentsInNonVariableOrder() {
	vector<details::VertexCompositId<Graph>> TemporaryOrderVect;
	map<int, typename Graph::VertexId> vertices;
	vertices.clear();
	LongEdgesExclusiveSplitter<Graph> splitter(new_graph, size_t(lib_.data().mean_insert_size));

	vector<VertexId> comps;
	DEBUG("comp filling started");
	if (!splitter.Finished())
		comps = splitter.NextComponent();
	int count = 0;
	int comp_count = 0;

	while (comps.size() != 0) {

		DEBUG("filling component " << comp_count);
		comp_count++;

		size_t CompId = new_graph.int_id(comps[0]);
		for (size_t i = 1; i < comps.size(); i++) {
			if (CompId > new_graph.int_id(comps[i]))
				CompId = new_graph.int_id(comps[i]);
		}

		for (size_t i = 0; i < comps.size(); i++) {
			details::VertexCompositId<Graph> curVertex;
			curVertex.Id = comps[i];
			curVertex.componentId = CompId;
			curVertex.intId = new_graph.int_id(comps[i]);
			TemporaryOrderVect.push_back(curVertex);
		}

		if (splitter.Finished())
			break;

		comps = splitter.NextComponent();
		DEBUG("finished filling component " << comp_count);

	}
	sort(TemporaryOrderVect.begin(), TemporaryOrderVect.end(),
			details::CompositIdCompare());
	for (size_t i = 0; i < TemporaryOrderVect.size(); i++) {
		vertices.insert(make_pair(count, TemporaryOrderVect[i].Id));
		count++;
	}
	return vertices;
}

template<class Graph>
map<int, typename Graph::VertexId> RepeatResolver<Graph>::fillVerticesComponents() {
	map<int, typename Graph::VertexId> vertices;
	vertices.clear();
	LongEdgesExclusiveSplitter<Graph> splitter(new_graph, size_t(lib_.data().mean_insert_size));

	vector<VertexId> comps;
	DEBUG("comp filling started");
	if (!splitter.Finished())
		comps = splitter.NextComponent();
	int count = 0;
	int comp_count = 0;
	while (comps.size() != 0) {

		DEBUG("filling component " << comp_count);
		comp_count++;
		for (size_t i = 0; i < comps.size(); i++) {
			vertices.insert(make_pair(count, comps[i]));
			count++;
		}
		if (splitter.Finished())
			break;
		comps = splitter.NextComponent();
		DEBUG("finished filling component " << comp_count);
	}
	return vertices;

}

template<class Graph>
void RepeatResolver<Graph>::ResolveRepeats(const string& output_folder) {
	perf_counter RR_time;

	INFO("SUBSTAGE == Resolving non-primitive repeats");
	sum_count = 0;

	TotalLabelerGraphStruct<Graph> graph_struct_before(old_graph, &old_IDs,
			&old_pos, NULL);
	TotalLabelerGraphStruct<Graph> graph_struct_after(new_graph, &new_IDs,
			&new_pos, NULL);
	TotalLabeler<Graph> TotLabAfter(&graph_struct_after, &graph_struct_before);
	cheating_mode = 0;

	bool changed = true;
	map<int, VertexId> vertices;
	int GraphCnt = 0;
	set<VertexId> available_verices;
	for (auto v_iter = new_graph.SmartVertexBegin(); !v_iter.IsEnd();
			++v_iter) {
		available_verices.insert(*v_iter);
	}
	while (changed) {
		changed = false;
		if (rc_mode)
//				vertices = fillVerticesComponents();
			vertices = fillVerticesComponentsInNonVariableOrder();
		else
			vertices = fillVerticesAuto();
		INFO(
				"Got "<< vertices.size() << " paired vertices, trying to split");
		RealIdGraphLabeler<Graph> IdTrackLabelerAfter(new_graph, new_IDs);

		long long counter = 0;
		for (auto v_iter = vertices.begin(), v_end = vertices.end();
				v_iter != v_end; ++v_iter) {

			DEBUG(
					" resolving vertex "<<new_IDs.ReturnIntId(v_iter->second));
			VERBOSE_POWER(++counter, " vertices processed");

			if (rc_mode
					&& deleted_handler.live_vertex.find(v_iter->second)
							== deleted_handler.live_vertex.end()) {
				DEBUG("already deleted");
				continue;
			} else {
				DEBUG("not deleted");
			}

			vector<EdgeId> edgeIds[2];
			int flag = 1;

			if (available_verices.find(v_iter->second)
					== available_verices.end()) {
				continue;
			}

			edgeIds[0] = new_graph.OutgoingEdges(v_iter->second);
			edgeIds[1] = new_graph.IncomingEdges(v_iter->second);

			set<VertexId> neighbours;
			for (size_t j = 0; j < edgeIds[0].size(); j++) {
				neighbours.insert(new_graph.EdgeEnd(edgeIds[0][j]));
			}
			for (size_t j = 0; j < edgeIds[1].size(); j++) {
				neighbours.insert(new_graph.EdgeStart(edgeIds[1][j]));
			}

			for (int i = 0; i < 2; i++) {
				for (size_t j = 0; j < edgeIds[i].size(); j++)
					if (global_cheating_edges.find(edgeIds[i][j])
							!= global_cheating_edges.end()) {
						TRACE(
								"Global cheater found "<<edgeIds[i][j]<<" id "<<new_graph.int_id(edgeIds[i][j]));
						flag = 0;
						break;
					}
			}
			if (!flag) {
				DEBUG(
						"Cheaters are near" << new_IDs.ReturnIntId(v_iter->second));
				continue;
			}
			size_t p_size = GenerateVertexPairedInfo(new_graph,
					paired_di_data, v_iter->second);
			DEBUG("paired info size: " << p_size);

			int tcount;
			resolve_time.start();
			tcount = SplitResolveVertex(v_iter->second, TotLabAfter);
			resolve_time.stop();
			available_verices.erase(v_iter->second);

			DEBUG("Vertex "<< v_iter->first<< " resolved to "<< tcount);
			sum_count += tcount;
			if (tcount > 1) {
				for (auto it = neighbours.begin(); it != neighbours.end();
						++it) {
					available_verices.insert(*it);
				}
				changed = true;
				GraphCnt++;
				if (cheating_mode == 0)
					changed = true;
			}
		}
	}

	INFO(
			sum_count << " vertices processed while resolving non-primitive repeats");
	INFO("Repeat resolver running time was "<< RR_time.time_ms()<< " ms");
	DEBUG(
			"Generate pair infos got "<< produce_pair_info_time.time_ms()<< " ms and runed "<<produce_pair_info_time.counts()<< " times.");
	DEBUG(
			"Resolve single vertex "<< resolve_time.time_ms()<< " ms and runed "<<resolve_time.counts()<< " times.");
	DEBUG(
			"MultiSplit got "<< multisplit_time.time_ms()<< " ms and runed "<<multisplit_time.counts()<< " times.");
	DEBUG(
			"Adjacency check got "<< adjacent_time.time_ms()<< " ms and runed "<<adjacent_time.counts()<< " times.");
	DEBUG(
			"DFS got "<< rectangle_resolve_1_time.time_ms()<< " ms and runed "<<rectangle_resolve_1_time.counts()<< " times.");
	DEBUG(
			"RR2 got "<< rectangle_resolve_2_time.time_ms()<< " ms and runed "<<rectangle_resolve_2_time.counts()<< " times.");
	DEBUG(
			"RR3 got "<< rectangle_resolve_3_time.time_ms()<< " ms and runed "<<rectangle_resolve_3_time.counts()<< " times.");
}

template<class Graph>
void RepeatResolver<Graph>::dfs(vector<vector<int> > &edge_list,
		vector<int> &colors, int cur_vert, int cur_color) {
	colors[cur_vert] = cur_color;
	for (int i = 0, sz = edge_list[cur_vert].size(); i < sz; i++) {
		if (colors[edge_list[cur_vert][i]] > -1) {
			if (colors[edge_list[cur_vert][i]] != cur_color) {
				ERROR(
						"error in dfs, neighbour to " << edge_list[cur_vert][i] << " cur_color: "<< cur_color);
			}
		} else {
			if (edge_list[cur_vert][i] != cur_vert) {
				dfs(edge_list, colors, edge_list[cur_vert][i], cur_color);
			}
		}
	}
}

namespace details {
template<class Graph>
struct EdgeInfoCompare {
	const Graph* new_graph;
	const Graph* old_graph;
	template<class EI>
	bool operator()(EI i, EI j) {
		if (new_graph->int_id(i.lp.first) < new_graph->int_id(j.lp.first))
			return true;
		if (new_graph->int_id(i.lp.first) > new_graph->int_id(j.lp.first))
			return false;
		if (i.lp.d() < j.lp.d() - 1e-5)
			return true;
		if (i.lp.d() > j.lp.d() + 1e-5)
			return false;
		if (old_graph->int_id(i.lp.second) < old_graph->int_id(j.lp.second))
			return true;
		return false;
	}
};
}

template<class Graph>
size_t RepeatResolver<Graph>::GenerateVertexPairedInfo(Graph &new_graph,
		MixedData &paired_data, VertexId vid) {
	produce_pair_info_time.start();

	details::EdgeInfoCompare<Graph> EI_comparator;

	EI_comparator.new_graph = &new_graph;
	EI_comparator.old_graph = &old_graph;

	DEBUG(
			"---- Generate vertex paired info for:  " << vid <<" ("<<new_IDs.ReturnIntId(vid) <<") -----------------------------");
	edge_infos.clear();
	local_cheating_edges.clear();
	vector<EdgeId> edgeIds[2];
	edgeIds[0] = new_graph.OutgoingEdges(vid);
	edgeIds[1] = new_graph.IncomingEdges(vid);
	DEBUG("out: " << edgeIds[0].size()<< "  in:" << edgeIds[1].size());

	int mult = 1;
	set<EdgeId> right_edges;
	for (int dir = 0; dir < 2; dir++) {
		for (int i = 0, n = edgeIds[dir].size(); i < n; i++) {
			PairInfos tmp = paired_di_data.GetEdgeInfos(edgeIds[dir][i]);
			vector<EdgeInfo> tmp_edge_infos;
			TRACE("Paired Info about vertex: " << tmp.size());
			for (int j = 0, sz = tmp.size(); j < sz; j++) {
				if (tmp[j].weight() < 1e-8)
					continue;
				EdgeId right_id = tmp[j].second;
				EdgeId left_id = tmp[j].first;
				double d = tmp[j].d();
				int dif_d = 0;
				{
					if (dir == 1) {
						dif_d = new_graph.length(left_id);
					}

					if (d * mult >= -1e-8) {
						TRACE(
								"PairInfo: " << new_IDs.ReturnIntId(tmp[j].first)<<" "<<old_IDs.ReturnIntId(labels_after.edge_labels[tmp[j].first][0]) << " " << old_IDs.ReturnIntId(tmp[j].second) <<" "<< tmp[j].d());
						TRACE("try to correct")

						EdgeInfo ei(tmp[j], dir, right_id, tmp[j].d() - dif_d);

						int trusted_dist = size_t(lib_.data().mean_insert_size) - lib_.data().read_length;
						if (cheating_mode == 2
								&& ((tmp[j].d() - dif_d
										+ old_graph.length(right_id)
										< trusted_dist - near_vertex)
										|| (tmp[j].d() - dif_d
												> trusted_dist + near_vertex))) {
							local_cheating_edges.insert(make_pair(left_id, 0));
							TRACE(
									"ignored paired_info between " << new_IDs.ReturnIntId(left_id) <<" and " <<old_IDs.ReturnIntId(right_id) <<" with distance " << tmp[j].d() - dif_d);
						} else {
							tmp_edge_infos.push_back(ei);
							TRACE(right_id);
							right_edges.insert(right_id);
						}
					}

				}
			}
			for (int j = 0; j < (int) tmp_edge_infos.size(); j++) {
				edge_infos.push_back(tmp_edge_infos[j]);
			}
			TRACE(" all info getted");
		}
	}
	DEBUG(" all info getted for all edges")

	sort(edge_infos.begin(), edge_infos.end(), EI_comparator);

	for (int j = 0; j < (int) edge_infos.size(); j++) {
		PairInfo tmp = edge_infos[j].lp;
		DEBUG(
				"Edge infos "<<j<<":" << new_IDs.ReturnIntId(tmp.first)<<" ("<<old_IDs.ReturnIntId(labels_after.edge_labels[tmp.first][0]) << ") -- " << old_IDs.ReturnIntId(tmp.second) <<" "<< tmp.d() << " from vertex: "<<edge_infos[j].d<< " weigth "<<tmp.weight());
	}

	produce_pair_info_time.stop();
	return right_edges.size();
}

template<class Graph>
vector<typename RepeatResolver<Graph>::PathInfo> RepeatResolver<Graph>::ConvertEdgeInfosToPathes() {
	DEBUG("ConvertEdgeInfosToPathes start");
	vector<PathInfo> ret;

	set<EdgeId> used_edges;
	details::EdgeInfoCompare<Graph> EI_comparator;

	EI_comparator.new_graph = &new_graph;
	EI_comparator.old_graph = &old_graph;

	for (size_t i = 0; i < edge_infos.size(); i++) {
		EdgeId cur_edge = edge_infos[i].lp.first;
		if (used_edges.find(cur_edge) == used_edges.end()) {
			used_edges.insert(cur_edge);
			vector<EdgeInfo> cur_edge_infos;
			for (size_t j = i; j < edge_infos.size(); j++) {
				if (edge_infos[j].lp.first == cur_edge)
					cur_edge_infos.push_back(edge_infos[j]);
			}
			sort(cur_edge_infos.begin(), cur_edge_infos.end(), EI_comparator);
			set<size_t> used_indexes;
			vector<PathInfo> edge_pathes;
			DEBUG("Generating pathes for edge "<<new_IDs.ReturnIntId(cur_edge));
			for (int j = 0; j < (int) cur_edge_infos.size(); j++) {
				PairInfo tmp = cur_edge_infos[j].lp;
				DEBUG(
						"Edge infos "<<j<<":" << new_IDs.ReturnIntId(tmp.first)<<" ("<<old_IDs.ReturnIntId(labels_after.edge_labels[tmp.first][0]) << ") -- " << old_IDs.ReturnIntId(tmp.second) <<" "<< tmp.d() << " from vertex: "<<cur_edge_infos[j].d<< " weigth "<<tmp.weight());
			}

			for (size_t ext_edge_num = 0; ext_edge_num < cur_edge_infos.size();
					ext_edge_num++) {
				if (used_indexes.find(ext_edge_num) == used_indexes.end()) {
					TRACE("Check info with index "<<ext_edge_num);
					vector<size_t> backwards;
					size_t back_index = ext_edge_num;
					while (1) {
						size_t new_back_ind = back_index;
						for (size_t test_ind = back_index; test_ind > 0;
								test_ind--) {
							TRACE("Check backward for "<<back_index);
							if (cur_edge_infos[back_index].follow(
									cur_edge_infos[test_ind - 1], old_graph)) {
								if (new_back_ind != back_index) {
									TRACE(
											"Multiple backwards "<<new_back_ind<<" and "<<test_ind-1);
									new_back_ind = back_index;
									break;
								} else {
									new_back_ind = test_ind - 1;
								}
							}
						}
						if (new_back_ind != back_index) {
							TRACE("Found step back "<<new_back_ind);
							backwards.push_back(new_back_ind);
							back_index = new_back_ind;
						} else
							break;
					}

					vector<size_t> forwards;
					size_t forward_index = ext_edge_num;
					while (1) {
						size_t new_forward_ind = forward_index;
						for (size_t test_ind = forward_index + 1;
								test_ind < cur_edge_infos.size(); test_ind++) {
							if (cur_edge_infos[test_ind].follow(
									cur_edge_infos[forward_index], old_graph)) {
								if (new_forward_ind != forward_index) {
									new_forward_ind = forward_index;
									break;
								} else {
									new_forward_ind = test_ind;
								}
							}
						}
						if (new_forward_ind != forward_index) {
							forwards.push_back(new_forward_ind);
							forward_index = new_forward_ind;
							used_indexes.insert(forward_index);
						} else
							break;
					}
					PathInfo cur_path(cur_edge);
					for (size_t j = backwards.size(); j > 0; j--) {
						cur_path.push_back(cur_edge_infos[backwards[j - 1]].lp);
					}
					cur_path.push_back(cur_edge_infos[ext_edge_num].lp);
					for (size_t j = 0; j < forwards.size(); j++) {
						cur_path.push_back(cur_edge_infos[forwards[j]].lp);
					}
					bool new_path = true;
					for (size_t j = 0; j < edge_pathes.size(); j++) {
						if (prefix_or_included(cur_path, edge_pathes[j], 0, 0)
								== 2) {
							DEBUG(
									"PATH "<<PrintPath(cur_path)<< " inside "<< PrintPath(edge_pathes[j]));
							new_path = false;
							break;
						}
					}
					if (new_path) {
						edge_pathes.push_back(cur_path);
					}
				}
			}

			for (size_t j = 0; j < edge_pathes.size(); j++) {
				bool new_path = true;
				for (size_t k = j + 1; k < edge_pathes.size(); k++) {
					if (prefix_or_included(edge_pathes[j], edge_pathes[k], 0, 0)
							== 2) {
						DEBUG(
								"PATH "<<PrintPath(edge_pathes[j])<< " inside "<< PrintPath(edge_pathes[k]));
						new_path = false;
						break;
					}
				}
				if (new_path) {
					ret.push_back(edge_pathes[j]);
				}
			}
		}
	}
	DEBUG("ConvertEdgeInfosToPathes end");
	return ret;
}

template<class Graph>
int RepeatResolver<Graph>::prefix_or_included(PathInfo&path1, PathInfo&path2,
		int shift1, int shift2) {
	size_t j = 1;
	size_t i = 1;
	while (((path2[j].first != path1[i].first)
			|| (abs(path1[i].second - shift1 - path2[j].second + shift2)
					> path1.path[i - 1].var() + path2.path[j - 1].var() + 0.1))) {
		j++;
		if (j == path2.size())
			break;
	}
	if (j < path2.size()) {
		while (j < path2.size() && i < path1.size()) {
			if ((path2[j].first != path1[i].first)
					|| (abs(path1[i].second - shift1 - path2[j].second + shift2)
							> path1.path[i - 1].var() + path2.path[j - 1].var()
									+ 0.1)) {
				return 0;
			}
			i++;
			j++;
		}
		if (i < path1.size())
			return 1;
		else
			return 2;
	} else {
		int dist = distance_counter.GetDistances(
				old_graph.EdgeEnd(path2[j - 1].first),
				old_graph.EdgeStart(path1[1].first));
		TRACE(
				"variances " <<path1.path[0].var() << " "<< path2.path[j-2].var());
		if (abs(
				path1[1].second - shift1 - path2[j - 1].second + shift2
						- old_graph.length(path2[j - 1].first) - dist)
				< 0.1 + path1.path[0].var() + path2.path[j - 2].var()) {
			return 1;
		}
	}
	return 0;
}

template<class Graph>
bool RepeatResolver<Graph>::pathesAdjacent(PathInfo& path1, PathInfo & path2,
		VertexId V_Id) {
	if ((path1[0].first == path2[0].first)
			&& (new_graph.length(path1[0].first)
					> cfg::get().rr.max_repeat_length)) {
		return true;
	}
	int shift1 = 0;
	int shift2 = 0;
	if (new_graph.EdgeEnd(path1[0].first) == V_Id) {
		shift1 = new_graph.length(path1[0].first);
	} else if (new_graph.EdgeStart(path1[0].first) == V_Id) {
		shift1 = 0;
	} else {
		WARN("PATH 1 not from vertex");
	}

	if (new_graph.EdgeEnd(path2[0].first) == V_Id) {
		shift2 = new_graph.length(path2[0].first);
	} else if (new_graph.EdgeStart(path2[0].first) == V_Id) {
		shift2 = 0;
	} else {
		WARN("PATH 2 not from vertex");
	}
	if (((shift1 == 0) ^ (shift2 == 0)) || (path1[0].first == path2[0].first)) {
		if (prefix_or_included(path1, path2, shift1, shift2))
			return true;
		else if (prefix_or_included(path2, path1, shift2, shift1))
			return true;
		else
			return false;
	} else {
		return false;
	}
	return false;
}

template<class Graph>
vector<int> RepeatResolver<Graph>::ColoringPathes(vector<PathInfo> &pathes,
		VertexId V_Id) {
	DEBUG("ColoringPathes start");

	int size = (int) pathes.size();

	vector<int> ret;
	for (int i = 0; i < size; i++) {
		ret.push_back(-1);
		DEBUG("PATH "<<i<<" "<<PrintPath(pathes[i]));
	}
	vector<vector<int> > neighbours;
	neighbours.resize(size);

	for (int i = 0; i < size; i++) {
		neighbours[i].resize(0);
	}

	for (int i = 0; i < size; i++) {
		for (int j = i + 1; j < size; j++) {
			if (pathesAdjacent(pathes[i], pathes[j], V_Id)) {
				neighbours[i].push_back(j);
				neighbours[j].push_back(i);
			}
		}
	}
	for (int i = 0; i < size; i++) {
		DEBUG("Neighbours  "<<ToString(neighbours[i]));
	}

	int cur_color = 0;
	for (int i = 0; i < size; i++) {
		if (ret[i] == -1) {
			dfs(neighbours, ret, i, cur_color);
			cur_color++;
		}
	}

	DEBUG("Path colors "<<ToString(ret));
	DEBUG("ColoringPathes end");
	return ret;
}

template<class Graph>
int RepeatResolver<Graph>::ColoringEdgesInfosByPathes(int &size,
		TotalLabeler<Graph>& tot_labler, VertexId V_Id) {
	DEBUG("ColoringEdgesInfosByPathes start");
	rectangle_resolve_3_time.start();
	edge_info_colors.resize(size);
	for (int i = 0; i < size; i++) {
		edge_info_colors[i] = -1;
	}

	vector<PathInfo> split_pathes = ConvertEdgeInfosToPathes();
	vector<int> path_colors = ColoringPathes(split_pathes, V_Id);

	size_t info_size = edge_infos.size();
	for (size_t i = 0; i < info_size; i++) {
		set<int> info_color_set;
		for (size_t j = 0; j < split_pathes.size(); j++) {
			if (edge_infos[i].lp.first == split_pathes[j][0].first) {
				for (size_t l = 1; l < split_pathes[j].size(); l++) {
					if ((edge_infos[i].lp.second == split_pathes[j][l].first)
							&& (abs(
									edge_infos[i].lp.d()
											- split_pathes[j][l].second) < 1e-5)) {
						info_color_set.insert(path_colors[j]);
						break;
					}
				}
			}
		}
		if (info_color_set.size() == 1) {
			edge_info_colors[i] = *(info_color_set.begin());
		} else if (info_color_set.size() > 1) {
			edge_infos[i].lp.weight() = edge_infos[i].lp.weight()
					/ info_color_set.size();
			auto iter = info_color_set.begin();
			edge_info_colors[i] = *iter;
			++iter;
			while (iter != info_color_set.end()) {
				edge_infos.push_back(edge_infos[i]);
				edge_info_colors.push_back(*iter);
				++iter;
			}
		} else if (info_color_set.size() < 1) {
			WARN(
					"Info "<<new_graph.int_id(edge_infos[i].lp.first)<<"("<<original_id(edge_infos[i].lp.first)<< ") "<<old_graph.int_id(edge_infos[i].lp.second) <<" "<<edge_infos[i].lp.d() <<" " << edge_infos[i].lp.var()<<" not included in any path");
		}
	}
	DEBUG("ColoringEdgesInfosByPathes end");

	return 0;
}

template<class Graph>
size_t RepeatResolver<Graph>::SplitResolveVertex(VertexId vid,
		TotalLabeler<Graph>& tot_labler) {

	rectangle_resolve_2_time.start();
	DEBUG(" resolve vertex started");
	int size = edge_infos.size();
	if (cheating_mode) {
		vector<EdgeId> edgeIds[2];
		edgeIds[0] = new_graph.OutgoingEdges(vid);
		edgeIds[1] = new_graph.IncomingEdges(vid);
		for (int i = 0; i < 2; i++) {
			for (size_t j = 0; j < edgeIds[i].size(); j++) {
				if (global_cheating_edges.find(edgeIds[i][j])
						!= global_cheating_edges.end()) {
					DEBUG(
							"Can not resolve vertex " << new_IDs.ReturnIntId(vid) <<" because of incident cheater edge "<< new_IDs.ReturnIntId(edgeIds[i][j]));
					TRACE(
							"Global cheater found "<<edgeIds[i][j]<<" id "<<new_graph.int_id(edgeIds[i][j]));
					return 1;
				}
				if (size == 0) {
					DEBUG(
							"Can not resolve vertex " << new_IDs.ReturnIntId(vid) <<" because of zero sized info ");
					return 1;
				}
			}
		}

	}
	rectangle_resolve_2_time.stop();
	int cur_color = ColoringEdgesInfosByPathes(size, tot_labler, vid);
	DEBUG("Edge color info " << edge_info_colors << "cur color" << cur_color);

	vector<typename Graph::VertexId> new_vertices = MultiSplit(vid);
	return new_vertices.size();
}

}

#endif /* REPEAT_RESOLVER_HPP_ */
