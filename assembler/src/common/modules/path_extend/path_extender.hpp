//***************************************************************************
//* Copyright (c) 2011-2014 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * path_extender.hpp
 *
 *  Created on: Mar 5, 2012
 *      Author: andrey
 */

#pragma once

#include "extension_chooser.hpp"
#include "path_filter.hpp"
#include "overlap_analysis.hpp"
#include "assembly_graph/graph_support/scaff_supplementary.hpp"
#include <cmath>

namespace path_extend {

class ShortLoopResolver {
public:
    ShortLoopResolver(const Graph& g)
            : g_(g) { }

    virtual ~ShortLoopResolver() { }

    virtual void ResolveShortLoop(BidirectionalPath& path) const = 0;

protected:
    DECL_LOGGER("PathExtender")
    const Graph& g_;

    void UndoCycles(BidirectionalPath& p, EdgeId next_edge) const {
        if (p.Size() <= 2) {
            return;
        }
        EdgeId first_edge = p.Back();
        EdgeId second_edge = next_edge;
        while (p.Size() > 2) {
            if (p.At(p.Size() - 1) == first_edge && p.At(p.Size() - 2) == second_edge) {
                p.PopBack(2);
            } else {
                return;;
            }
        }
    }

    void MakeCycleStep(BidirectionalPath& path, EdgeId e) const {
        if (path.Size() == 0) {
            return;
        }
        EdgeId pathEnd = path.Back();
        path.PushBack(e);
        path.PushBack(pathEnd);
    }
};

class CovShortLoopResolver : public ShortLoopResolver {
public:
    CovShortLoopResolver(const conj_graph_pack& gp)
            : ShortLoopResolver(gp.g), gp_(gp) {

    }

    void ResolveShortLoop(BidirectionalPath& path) const override {
        DEBUG("resolve short loop by coverage");
        path.Print();

        pair<EdgeId, EdgeId> edges;
        if (path.Size() >= 1 && GetLoopAndExit(g_, path.Back(), edges)) {
            DEBUG("Coverage Short Loop Resolver");
            UndoCycles(path, edges.first);
            EdgeId e1 = path.Back();
            EdgeId e2 = edges.first;
            EdgeId e_out = edges.second;
            auto prob_e_in = g_.IncomingEdges(g_.EdgeEnd(e2));
            EdgeId e_in = *prob_e_in.begin();
            size_t count = 0;
            for (auto edge = prob_e_in.begin(); edge != prob_e_in.end(); ++edge) {
                if (*edge != e2)
                    e_in = *edge;
                count++;
            }
            if (count != 2) {
                return;
            }
            double in_cov = gp_.flanking_cov.GetOutCov(e_in); //g_.coverage(e_in);
            double out_cov = gp_.flanking_cov.GetInCov(e_out); //g_.coverage(e_out);
            double cov = (in_cov + out_cov) / 2.0;
            //what are time variables???
            double time1 = math::round(gp_.g.coverage(e1) / cov);
            double time2 = math::round(gp_.g.coverage(e2) / cov);
            size_t time = (size_t) std::max(0.0, std::min(time1 - 1.0, time2));
            for (size_t i = 0; i < time; ++i) {
                MakeCycleStep(path, edges.first);
            }
            path.PushBack(edges.second);
            DEBUG("loop with start " << g_.int_id(e_in)
                    <<" e1 " << g_.int_id(e1)
                    << " e2 " << g_.int_id(e2)
                    << " out " <<g_.int_id(e_out)
                    << " cov in = " << in_cov
                    << " cov out " << out_cov
                    << " cov " << cov
                  << " cov e1 = " << gp_.g.coverage(e1)
                  << " cov e2 = " << gp_.g.coverage(e2)
                  << " time1 = " << time1
                  << " time2 = " << time2
                  << " time = " << time);
        }
    }
private:
    const conj_graph_pack& gp_;
};

class SimpleLoopResolver : public ShortLoopResolver {

public:
    SimpleLoopResolver(Graph& g) : ShortLoopResolver(g) { }

    void ResolveShortLoop(BidirectionalPath& path) const override {
        pair<EdgeId, EdgeId> edges;
        if (path.Size() >= 1 && GetLoopAndExit(g_, path.Back(), edges)) {
            DEBUG("Resolving short loop...");
            EdgeId e = path.Back();
            path.PushBack(edges.first);
            path.PushBack(e);
            path.PushBack(edges.second);
            DEBUG("Resolving short loop done");
        }
    }

protected:
    DECL_LOGGER("PathExtender")
};

class LoopResolver : public ShortLoopResolver {
    static const size_t ITER_COUNT = 10;
    const WeightCounter& wc_;

private:
    bool CheckLoopPlausible(EdgeId froward_loop_edge, EdgeId backward_loop_edge) const {
        size_t single_loop_length = 2 * g_.length(froward_loop_edge) + g_.length(backward_loop_edge);
        return single_loop_length <= wc_.get_libptr()->GetISMax();
    }

public:
    LoopResolver(const Graph& g, const WeightCounter& wc)
            : ShortLoopResolver(g),
              wc_(wc) { }
    //This code works only if loop wasn't fairly resolved
    //
    //Weird interface; need comments
    void MakeBestChoice(BidirectionalPath& path, pair<EdgeId, EdgeId>& edges) const {
        UndoCycles(path, edges.first);
        BidirectionalPath experiment(path);
        double max_weight = wc_.CountWeight(experiment, edges.second);
        double diff = max_weight - wc_.CountWeight(experiment, edges.first);
        size_t maxIter = 0;
        for (size_t i = 1; i <= ITER_COUNT; ++i) {
            double weight = wc_.CountWeight(experiment, edges.first);
            if (weight > 0) {
                MakeCycleStep(experiment, edges.first);
                weight = wc_.CountWeight(experiment, edges.second);
                double weight2 = wc_.CountWeight(experiment, edges.first);
                if (weight > max_weight || (weight == max_weight && weight - weight2 > diff)
                    || (weight == max_weight && weight - weight2 == diff && i == 1)) {
                    max_weight = weight;
                    maxIter = i;
                    diff = weight - weight2;
                }
            }
        }

        if (!CheckLoopPlausible(path.Back(), edges.first) && maxIter > 0) {
            MakeCycleStep(path, edges.first);
            path.PushBack(edges.second, Gap(int(g_.k() + 100)));
        }
        else {
            for (size_t i = 0; i < maxIter; ++i) {
                MakeCycleStep(path, edges.first);
            }
            path.PushBack(edges.second);
        }

    }

    void ResolveShortLoop(BidirectionalPath& path) const override {
        pair<EdgeId, EdgeId> edges;
        if (path.Size() >=1 && GetLoopAndExit(g_, path.Back(), edges)) {
            DEBUG("Resolving short loop...");
            MakeBestChoice(path, edges);
            DEBUG("Resolving short loop done");
        }
    }

};

//FIXME move to gap_closing.hpp
typedef omnigraph::GapDescription<Graph> GapDescription;
class GapAnalyzer {

public:
    static const int INVALID_GAP = GapDescription::INVALID_GAP;
    GapAnalyzer(const Graph& g)
            : g_(g) { }

    virtual GapDescription FixGap(const GapDescription &gap) const = 0;

    virtual ~GapAnalyzer() { }
protected:
    const Graph& g_;
};

class HammingGapAnalyzer: public GapAnalyzer {
    const double min_gap_score_;
    const size_t short_overlap_threshold_;
    const size_t basic_overlap_length_;

    static constexpr double MIN_OVERLAP_COEFF = 0.05;

    size_t HammingDistance(const Sequence& s1, const Sequence& s2) const {
        VERIFY(s1.size() == s2.size());
        size_t dist = 0;
        for (size_t i = 0; i < s1.size(); ++i) {
            if (s1[i] != s2[i]) {
                dist++;
            }
        }
        return dist;
    }

    double ScoreGap(const Sequence& s1, const Sequence& s2) const {
        VERIFY(s1.size() == s2.size());
        return 1.0 - (double) HammingDistance(s1, s2) / (double) s1.size();
    }

public:

    //todo review parameters in usages
    HammingGapAnalyzer(const Graph& g,
            double min_gap_score,
            size_t short_overlap_threshold,
            size_t basic_overlap_length):
                GapAnalyzer(g),
                min_gap_score_(min_gap_score),
                short_overlap_threshold_(short_overlap_threshold),
                basic_overlap_length_(basic_overlap_length)
    {
        DEBUG("HammingGapAnalyzer params: \n min_gap_score " << min_gap_score_ <<
              "\n short_overlap_threshold " << short_overlap_threshold_ <<
              "\n basic_overlap_length " << basic_overlap_length_);
    }

    GapDescription FixGap(const GapDescription &gap) const override {
        VERIFY_MSG(gap.no_trim(), "Trims not supported yet");

        size_t max_overlap = basic_overlap_length_;
        if (gap.estimated_dist() < 0) {
            max_overlap -= gap.estimated_dist();
        }

        max_overlap = min(max_overlap,
                                      g_.k() + min(g_.length(gap.left()), g_.length(gap.right())));

        DEBUG("Corrected max overlap " << max_overlap);

        double best_score = min_gap_score_;
        int fixed_gap = GapDescription::INVALID_GAP;

        size_t min_overlap = 1;
        if (gap.estimated_dist() < 0) {
            min_overlap = max(min_overlap, size_t(math::round(MIN_OVERLAP_COEFF * double(-gap.estimated_dist()))));
        }
        //todo better usage of estimated overlap
        DEBUG("Min overlap " << min_overlap);

        for (size_t l = max_overlap; l >= min_overlap; --l) {
            //TRACE("Sink: " << g_.EdgeNucls(sink).Subseq(g_.length(sink) + g_.k() - l).str());
            //TRACE("Source: " << g_.EdgeNucls(source).Subseq(0, l));
            double score = 0;
            score = ScoreGap(g_.EdgeNucls(gap.left()).Subseq(g_.length(gap.left()) + g_.k() - l),
                                    g_.EdgeNucls(gap.right()).Subseq(0, l));
            if (math::gr(score, best_score)) {
                TRACE("Curr overlap " << l);
                TRACE("Score: " << score);
                best_score = score;
                fixed_gap = -int(l);
            }

            if (l == short_overlap_threshold_ && fixed_gap != GapDescription::INVALID_GAP) {
                //look at "short" overlaps only if long overlaps couldn't be found
                DEBUG("Not looking at short overlaps");
                break;
            }
        }

        if (fixed_gap != INVALID_GAP) {
            DEBUG("Found candidate gap length with score " << best_score);
            DEBUG("Estimated gap: " << gap.estimated_dist() <<
                  ", fixed gap: " << fixed_gap << " (overlap " << (-fixed_gap) << ")");

            auto answer = gap;
            answer.set_estimated_dist(fixed_gap);
            return answer;
        } else {
            return GapDescription();
        }
    }

private:
    DECL_LOGGER("HammingGapAnalyzer");
};

//LA stands for Local Alignment
//TODO if current setting will work -- get rid of flank_*_coefficient params
class LAGapAnalyzer: public GapAnalyzer {
public:
    LAGapAnalyzer(const Graph& g, size_t min_la_length,
            double flank_multiplication_coefficient,
            int flank_addition_coefficient) :
            GapAnalyzer(g),
            min_la_length_(min_la_length),
            flank_multiplication_coefficient_(flank_multiplication_coefficient),
            flank_addition_coefficient_(flank_addition_coefficient) {
        DEBUG("flank_multiplication_coefficient - " << flank_multiplication_coefficient_);
        DEBUG("flank_addition_coefficient  - " << flank_addition_coefficient_ );
    }

    GapDescription FixGap(const GapDescription &gap) const override {
        VERIFY_MSG(gap.no_trim(), "Trims not supported yet");
        //estimated_gap is in k-mers

        size_t estimated_overlap = gap.estimated_dist() < 0 ? size_t(abs(gap.estimated_dist())) : 0;
        SWOverlapAnalyzer overlap_analyzer(size_t(math::round(double(estimated_overlap) * ESTIMATED_GAP_MULTIPLIER))
                                           + GAP_ADDITIONAL_COEFFICIENT);

        auto overlap_info = overlap_analyzer.AnalyzeOverlap(g_, gap.left(), gap.right());
        DEBUG(overlap_info);

        if (overlap_info.size() < min_la_length_) {
            DEBUG("Low alignment size");
            return GapDescription();
        }

        size_t max_flank_length = max(overlap_info.r2.start_pos,
                g_.length(gap.left()) + g_.k() - overlap_info.r1.end_pos);
        DEBUG("Max flank length - " << max_flank_length);

        if (int(math::round(double(max_flank_length) * flank_multiplication_coefficient_))
                + flank_addition_coefficient_ > int(overlap_info.size())) {
            DEBUG("Too long flanks for such alignment");
            return GapDescription();
        }

        if (math::ls(overlap_info.identity(), IDENTITY_RATIO)) {
            DEBUG("Low identity score");
            return GapDescription();
        }

        if (overlap_info.r1.end_pos <= g_.k() || overlap_info.r2.start_pos >= g_.length(gap.right())) {
            DEBUG("Less than k+1 nucleotides were left of one of the edges");
            return GapDescription();
        }

        //TODO Is it ok to have a non-symmetric overlap gap description
        return GapDescription(gap.left(), gap.right(),
                              -int(overlap_info.r1.size()),
                              g_.length(gap.left()) + g_.k() - overlap_info.r1.end_pos,
                              overlap_info.r2.start_pos);
    }

private:
    DECL_LOGGER("LAGapAnalyzer");
    const size_t min_la_length_;
    const double flank_multiplication_coefficient_;
    const int flank_addition_coefficient_;

    static constexpr double IDENTITY_RATIO = 0.9;
    static constexpr double ESTIMATED_GAP_MULTIPLIER = 2.0;
    static constexpr size_t GAP_ADDITIONAL_COEFFICIENT = 30;
};


class CompositeGapAnalyzer: public GapAnalyzer {
public:

    CompositeGapAnalyzer(const Graph& g,
                       const vector<shared_ptr<GapAnalyzer>>& joiners,
                       size_t may_overlap_threshold,
                       int must_overlap_threshold,
                       size_t artificial_gap) :
            GapAnalyzer(g),
            joiners_(joiners),
            may_overlap_threshold_(may_overlap_threshold),
            must_overlap_threshold_(must_overlap_threshold),
            artificial_gap_(artificial_gap)
    {  }

    GapDescription FixGap(const GapDescription &gap) const override {
        VERIFY_MSG(gap.right_trim() == 0 && gap.left_trim() == 0, "Not supported yet");
        DEBUG("Trying to fix estimated gap " << gap.estimated_dist() <<
              " between " << g_.str(gap.left()) << " and " << g_.str(gap.right()));

        if (gap.estimated_dist() > int(may_overlap_threshold_)) {
            DEBUG("Edges are supposed to be too far to check overlaps");
            return gap;
        }

        for (auto joiner : joiners_) {
            GapDescription fixed_gap = joiner->FixGap(gap);
            if (fixed_gap != GapDescription()) {
                return fixed_gap;
            }
        }

        //couldn't find decent overlap
        if (gap.estimated_dist() < must_overlap_threshold_) {
            DEBUG("Estimated gap looks unreliable");
            return GapDescription();
        } else {
            DEBUG("Overlap was not found");
            auto answer = gap;
            answer.set_estimated_dist(max(gap.estimated_dist(), int(artificial_gap_)));
            return answer;
        }
    }

private:
    vector<shared_ptr<GapAnalyzer>> joiners_;
    const size_t may_overlap_threshold_;
    const int must_overlap_threshold_;
    const size_t artificial_gap_;

    DECL_LOGGER("CompositeGapAnalyzer");
};

//Detects a cycle as a minsuffix > IS present earlier in the path. Overlap is allowed.
class InsertSizeLoopDetector {
protected:
    GraphCoverageMap visited_cycles_coverage_map_;
    PathContainer path_storage_;
    size_t min_cycle_len_;

public:
    InsertSizeLoopDetector(const Graph& g, size_t is):
        visited_cycles_coverage_map_(g),
        path_storage_(),
        min_cycle_len_(is) {
    }

    ~InsertSizeLoopDetector() {
        path_storage_.DeleteAllPaths();
    }

    bool CheckCycledNonIS(const BidirectionalPath& path) const {
        if (path.Size() <= 2) {
            return false;
        }
        BidirectionalPath last = path.SubPath(path.Size() - 2);
        int pos = path.FindFirst(last);
        VERIFY(pos >= 0);
        return size_t(pos) != path.Size() - 2;
    }

    bool CheckCycled(const BidirectionalPath& path) const {
        return FindCycleStart(path) != -1;
    }
//first suffix longer than min_cycle_len
    int FindPosIS(const BidirectionalPath& path) const {
        int i = (int) path.Size() - 1;
        while (i >= 0 && path.LengthAt(i) < min_cycle_len_) {
            --i;
        }
        return i;
    }
    int FindCycleStart(const BidirectionalPath& path) const {
        TRACE("Looking for IS cycle " << min_cycle_len_);
        int i = FindPosIS(path);
        TRACE("last is pos " << i);
        if (i < 0) return -1;
//Tail
        BidirectionalPath last = path.SubPath(i);
        //last.Print();

        int pos = path.FindFirst(last);
// not cycle
        if (pos == i) pos = -1;
        TRACE("looking for 1sr IS cycle " << pos);
        return pos;
    }

//After cycle detected, removes min suffix > IS.
//returns the beginning of the cycle.
    int RemoveCycle(BidirectionalPath& path) const {
        int pos = FindCycleStart(path);
        DEBUG("Found IS cycle " << pos);
        if (pos == -1) {
            return -1;
        }

        int last_edge_pos = FindPosIS(path);
        VERIFY(last_edge_pos > -1);
        DEBUG("last edge pos " << last_edge_pos);
        VERIFY(last_edge_pos > pos);
        for (int i = (int) path.Size() - 1; i >= last_edge_pos; --i) {
            path.PopBack();
        }
        VERIFY((int) path.Size() == last_edge_pos);
        VERIFY(pos < (int) path.Size());
        DEBUG("result pos " <<pos);
        return pos;
    }

    //seems that it is outofdate
    bool InExistingLoop(const BidirectionalPath& path) {
        DEBUG("Checking existing loops");
        auto visited_cycles = visited_cycles_coverage_map_.GetEdgePaths(path.Back());
        for (auto cycle : *visited_cycles) {
            DEBUG("checking  cycle ");
            int pos = path.FindLast(*cycle);
            if (pos == -1)
                continue;

            int start_cycle_pos = pos + (int) cycle->Size();
            bool only_cycles_in_tail = true;
            int last_cycle_pos = start_cycle_pos;
            DEBUG("start_cycle pos "<< last_cycle_pos);
            for (int i = start_cycle_pos; i < (int) path.Size() - (int) cycle->Size(); i += (int) cycle->Size()) {
                if (!path.CompareFrom(i, *cycle)) {
                    only_cycles_in_tail = false;
                    break;
                } else {
                    last_cycle_pos = i + (int) cycle->Size();
                    DEBUG("last cycle pos changed " << last_cycle_pos);
                }
            }
            DEBUG("last_cycle_pos " << last_cycle_pos);
            only_cycles_in_tail = only_cycles_in_tail && cycle->CompareFrom(0, path.SubPath(last_cycle_pos));
            if (only_cycles_in_tail) {
// seems that most of this is useless, checking
                VERIFY (last_cycle_pos == start_cycle_pos);
                DEBUG("find cycle " << last_cycle_pos);
                DEBUG("path");
                path.Print();
                DEBUG("last subpath");
                path.SubPath(last_cycle_pos).Print();
                DEBUG("cycle");
                cycle->Print();
                DEBUG("last_cycle_pos " << last_cycle_pos << " path size " << path.Size());
                VERIFY(last_cycle_pos <= (int)path.Size());
                DEBUG("last cycle pos + cycle " << last_cycle_pos + (int)cycle->Size());
                VERIFY(last_cycle_pos + (int)cycle->Size() >= (int)path.Size());

                return true;
            }
        }
        return false;
    }

    void AddCycledEdges(const BidirectionalPath& path, size_t pos) {
        if (pos >= path.Size()) {
            DEBUG("Wrong position in IS cycle");
            return;
        }
        BidirectionalPath * p = new BidirectionalPath(path.SubPath(pos));
        BidirectionalPath * cp = new BidirectionalPath(p->Conjugate());
        visited_cycles_coverage_map_.Subscribe(p);
        visited_cycles_coverage_map_.Subscribe(cp);
        DEBUG("add cycle");
        p->Print();
    }
};

class RepeatDetector {
public:
    RepeatDetector(const Graph& g, const GraphCoverageMap& cov_map, size_t max_repeat_len)
            : g_(g),
              cov_map_(cov_map),
              used_paths_(),
              repeat_len_(max_repeat_len){
        empty_ = new BidirectionalPath(g_);
    }
    ~RepeatDetector() {
        delete empty_;
    }

    BidirectionalPath* RepeatPath(const BidirectionalPath& p) {
        if (p.Size() == 0) {
            return empty_;
        }
        EdgeId last_e = p.Back();
        BidirectionalPathSet cov_paths = cov_map_.GetCoveringPaths(last_e);
        DEBUG("cov paths for e " << g_.int_id(last_e) << " size " << cov_paths.size());
        size_t max_common_size = 0;
        BidirectionalPath* result_p = empty_;
        for (BidirectionalPath* cov_p : cov_paths) {
            if (used_paths_.find(cov_p) == used_paths_.end() || cov_p == &p || cov_p == p.GetConjPath()) {
                continue;
            }
            size_t common_size = MaxCommonSize(p, *cov_p);
            DEBUG("max comon size with path " << cov_p->GetId() << " is " << common_size);
            if (common_size == 0) {
                continue;
            }
            VERIFY(common_size <= p.Size());
            if (p.LengthAt(p.Size() - common_size) > repeat_len_) {
                DEBUG("repeat from " << (p.Size() - common_size) << " length " << p.LengthAt(p.Size() - common_size) << " repeat length " << repeat_len_);
                max_common_size = max(common_size, max_common_size);
                result_p = cov_p;
            }
        }
        used_paths_.insert(&p);
        DEBUG("max common size " << max_common_size);
        return result_p;
    }
    size_t MaxCommonSize(const BidirectionalPath& p1, const BidirectionalPath& p2) const {
        DEBUG("max coomon size ")
        EdgeId last_e = p1.Back();
        vector<size_t> positions2 = p2.FindAll(last_e);
        DEBUG("pos size " << positions2.size())
        size_t max_common_size = 0;
        for (size_t pos2 : positions2) {
            size_t common_size = MaxCommonSize(p1, p1.Size() - 1, p2, pos2);
            DEBUG("max common size from " << pos2 << " is " << common_size);
            max_common_size = max(max_common_size, common_size);
        }
        return max_common_size;
    }
private:
    size_t MaxCommonSize(const BidirectionalPath& p1, size_t pos1, const BidirectionalPath& p2, size_t pos2) const {
        int i1 = (int) pos1;
        int i2 = (int) pos2;
        while (i1 >= 0 && i2 >= 0 &&
                p1.At((size_t) i1) == p2.At((size_t) i2) &&
                p1.GapAt((size_t) i1) == p2.GapAt((size_t) i2)) {
            i1--;
            i2--;
        }
        if (i1 >=0 && i2>=0 && p1.At((size_t) i1) == p2.At((size_t) i2)) {
            i1--;
            i2--;
        }

        VERIFY(i1 <= (int)pos1);
        return std::max(size_t((int) pos1 - i1), (size_t)1);
    }
    const Graph& g_;
    const GraphCoverageMap& cov_map_;
    set<const BidirectionalPath*> used_paths_;
    size_t repeat_len_;
    BidirectionalPath* empty_;
};

class ContigsMaker {
public:
    ContigsMaker(const Graph & g)
            : g_(g) { }

    virtual ~ContigsMaker() { }

    virtual void GrowPath(BidirectionalPath& path, PathContainer* paths_storage = nullptr) = 0;

    virtual void GrowPathSimple(BidirectionalPath& path, PathContainer* paths_storage = nullptr) = 0;

    virtual void GrowAll(PathContainer & paths, PathContainer& paths_storage) = 0;

protected:
    const Graph& g_;
    DECL_LOGGER("PathExtender")
};

struct UsedUniqueStorage {
    set<EdgeId> used_;

    const ScaffoldingUniqueEdgeStorage& unique_;

    UsedUniqueStorage(const ScaffoldingUniqueEdgeStorage& unique ):used_(), unique_(unique) {}

    void insert(EdgeId e) {
        if (unique_.IsUnique(e)) {
            used_.insert(e);
            used_.insert(e->conjugate());
        }
    }

    bool IsUsedAndUnique(EdgeId e) const {
        return (unique_.IsUnique(e) && used_.find(e) != used_.end());
    }

    bool UniqueCheckEnabled() const {
        return unique_.size() > 0;
    }


};

class PathExtender {
public:
    PathExtender(const Graph & g): g_(g) {}

    virtual ~PathExtender() { }

    virtual bool MakeGrowStep(BidirectionalPath& path, PathContainer* paths_storage = nullptr) = 0;

    void AddUniqueEdgeStorage(shared_ptr<UsedUniqueStorage> used_storage) {
        used_storage_ = used_storage;
    }
protected:
    const Graph& g_;
    shared_ptr<UsedUniqueStorage> used_storage_;
    DECL_LOGGER("PathExtender")
};

class CompositeExtender : public ContigsMaker {
public:
    CompositeExtender(const Graph &g, GraphCoverageMap& cov_map,
                      size_t max_diff_len,
                      size_t max_repeat_length,
                      bool detect_repeats_online)
            : ContigsMaker(g),
              cover_map_(cov_map),
              repeat_detector_(g, cover_map_, 2 * max_repeat_length),
              extenders_(),
              max_diff_len_(max_diff_len),
              max_repeat_len_(max_repeat_length),
              detect_repeats_online_(detect_repeats_online) {
    }

    CompositeExtender(const Graph & g, GraphCoverageMap& cov_map,
                      vector<shared_ptr<PathExtender> > pes,
                      const ScaffoldingUniqueEdgeStorage& unique,
                      size_t max_diff_len,
                      size_t max_repeat_length,
                      bool detect_repeats_online)
            : ContigsMaker(g),
              cover_map_(cov_map),
              repeat_detector_(g, cover_map_, 2 * max_repeat_length),
              extenders_(),
              max_diff_len_(max_diff_len),
              max_repeat_len_(max_repeat_length),
              detect_repeats_online_(detect_repeats_online) {
        extenders_ = pes;
        used_storage_ = make_shared<UsedUniqueStorage>(UsedUniqueStorage(unique));
        for (auto ex: extenders_) {
            ex->AddUniqueEdgeStorage(used_storage_);
        }
    }

    void AddExtender(shared_ptr<PathExtender> pe) {
        extenders_.push_back(pe);
        pe->AddUniqueEdgeStorage(used_storage_);
    }

    void GrowAll(PathContainer& paths, PathContainer& result) override {
        result.clear();
        GrowAllPaths(paths, result);
        LengthPathFilter filter(g_, 0);
        filter.filter(result);
    }

    void GrowPath(BidirectionalPath& path, PathContainer* paths_storage) override {
        while (MakeGrowStep(path, paths_storage)) { }
    }

    void GrowPathSimple(BidirectionalPath& path, PathContainer* paths_storage) override {
        while (MakeGrowStep(path, paths_storage, false)) { }
    }


    bool MakeGrowStep(BidirectionalPath& path, PathContainer* paths_storage,
                      bool detect_repeats_online_local = true) {
        DEBUG("make grow step composite extender");
        if (detect_repeats_online_ && detect_repeats_online_local) {
            BidirectionalPath *repeat_path = repeat_detector_.RepeatPath(path);
            size_t repeat_size = repeat_detector_.MaxCommonSize(path, *repeat_path);

            if (repeat_size > 0) {
                DEBUG("repeat with length " << repeat_size);
                path.Print();
                repeat_path->Print();
                BidirectionalPath repeat = path.SubPath(path.Size() - repeat_size);
                int begin_repeat = repeat_path->FindLast(repeat);
                VERIFY(begin_repeat > -1);
                size_t end_repeat = (size_t) begin_repeat + repeat_size;
                DEBUG("not consistent subpaths ");
                BidirectionalPath begin1 = path.SubPath(0, path.Size() - repeat_size);
                begin1.Print();
                BidirectionalPath begin2 = repeat_path->SubPath(0, begin_repeat);
                begin2.Print();
                int gpa_in_repeat_path = repeat_path->GapAt(begin_repeat).gap;
                BidirectionalPath end2 = repeat_path->SubPath(end_repeat);
                BidirectionalPath begin1_conj = path.SubPath(0, path.Size() - repeat_size + 1).Conjugate();
                BidirectionalPath begin2_conj = repeat_path->SubPath(0, begin_repeat + 1).Conjugate();
                pair<size_t, size_t> last = ComparePaths(0, 0, begin1_conj, begin2_conj, max_diff_len_);
                DEBUG("last " << last.first << " last2 " << last.second);
                Gap gap = path.GapAt(path.Size() - repeat_size);
                path.Clear();
                repeat_path->Clear();

                if (begin2.Size() == 0 || last.second != 0) { //TODO: incorrect: common edges, but then different ends
                    path.PushBack(begin1);
                    repeat_path->PushBack(begin2);
                } else {
                    gap = Gap(gpa_in_repeat_path);
                    path.PushBack(begin2);
                    repeat_path->PushBack(begin1);
                }

                path.PushBack(repeat, gap);
                path.PushBack(end2);
                DEBUG("new path");
                path.Print();
                return false;
            }
        }

        size_t current = 0;
        while (current < extenders_.size()) {
            DEBUG("step " << current << " of total " << extenders_.size());
            if (extenders_[current]->MakeGrowStep(path, paths_storage)) {
                return true;
            }
           ++current;
        }
        return false;
    }
    
private:
    GraphCoverageMap& cover_map_;
    RepeatDetector repeat_detector_;
    vector<shared_ptr<PathExtender> > extenders_;
    size_t max_diff_len_;
    size_t max_repeat_len_;
    bool detect_repeats_online_;
    shared_ptr<UsedUniqueStorage> used_storage_;

    void SubscribeCoverageMap(BidirectionalPath * path) {
        path->Subscribe(&cover_map_);
        for (size_t i = 0; i < path->Size(); ++i) {
            cover_map_.BackEdgeAdded(path->At(i), path, path->GapAt(i));
        }
    }

    void GrowAllPaths(PathContainer& paths, PathContainer& result) {
        for (size_t i = 0; i < paths.size(); ++i) {
            VERBOSE_POWER_T2(i, 100, "Processed " << i << " paths from " << paths.size() << " (" << i * 100 / paths.size() << "%)");
            if (paths.size() > 10 && i % (paths.size() / 10 + 1) == 0) {
                INFO("Processed " << i << " paths from " << paths.size() << " (" << i * 100 / paths.size() << "%)");
            }
            //In 2015 modes do not use a seed already used in paths.
            if (used_storage_->UniqueCheckEnabled()) {
                bool was_used = false;
                for (size_t ind =0; ind < paths.Get(i)->Size(); ind++) {
                    EdgeId eid = paths.Get(i)->At(ind);
                    if (used_storage_->IsUsedAndUnique(eid)) {
                        DEBUG("Used edge " << g_.int_id(eid));
                        was_used = true;
                        break;
                    } else {
                        used_storage_->insert(eid);
                    }
                }
                if (was_used) {
                    DEBUG("skipping already used seed");
                    continue;
                }
            }

            if (!cover_map_.IsCovered(*paths.Get(i))) {
                BidirectionalPath * path = new BidirectionalPath(*paths.Get(i));
                BidirectionalPath * conjugatePath = new BidirectionalPath(*paths.GetConjugate(i));
                result.AddPair(path, conjugatePath);
                SubscribeCoverageMap(path);
                SubscribeCoverageMap(conjugatePath);
                size_t count_trying = 0;
                size_t current_path_len = 0;
                do {
                    current_path_len = path->Length();
                    count_trying++;
                    GrowPath(*path, &result);
                    GrowPath(*conjugatePath, &result);
                } while (count_trying < 10 && (path->Length() != current_path_len));
                path->CheckConjugateEnd(max_repeat_len_);
                DEBUG("result path " << path->GetId());
                path->Print();
            }
        }
    }

};

//All Path-Extenders inherit this one
class LoopDetectingPathExtender : public PathExtender {

protected:
    bool investigate_short_loops_;
    bool use_short_loop_cov_resolver_;
    CovShortLoopResolver cov_loop_resolver_;

    InsertSizeLoopDetector is_detector_;
    const GraphCoverageMap& cov_map_;

public:
    LoopDetectingPathExtender(const conj_graph_pack &gp,
                                  const GraphCoverageMap &cov_map,
                                  bool investigate_short_loops,
                                  bool use_short_loop_cov_resolver,
                                  size_t is)
            : PathExtender(gp.g),
              investigate_short_loops_(investigate_short_loops),
              use_short_loop_cov_resolver_(use_short_loop_cov_resolver),
              cov_loop_resolver_(gp),
              is_detector_(gp.g, is),
              cov_map_(cov_map) {

    }

    bool isInvestigateShortLoops() const {
        return investigate_short_loops_;
    }

    void setInvestigateShortLoops(bool investigateShortLoops) {
        this->investigate_short_loops_ = investigateShortLoops;
    }

    bool DetectCycle(BidirectionalPath& path) {
        DEBUG("detect cycle");
        if (is_detector_.CheckCycled(path)) {
            DEBUG("Checking IS cycle");
            int loop_pos = is_detector_.RemoveCycle(path);
            DEBUG("Removed IS cycle");
            if (loop_pos != -1) {
                is_detector_.AddCycledEdges(path, loop_pos);
                return true;
            }
        }
        return false;
    }

    bool DetectCycleScaffolding(BidirectionalPath& path, EdgeId e) {
        BidirectionalPath temp_path(path);
        temp_path.PushBack(e);
        return is_detector_.CheckCycledNonIS(temp_path);
    }

    virtual bool MakeSimpleGrowStep(BidirectionalPath& path, PathContainer* paths_storage = nullptr) = 0;

    virtual bool ResolveShortLoopByCov(BidirectionalPath& path) = 0;

    virtual bool ResolveShortLoopByPI(BidirectionalPath& path) = 0;

    virtual bool CanInvestigateShortLoop() const {
        return false;
    }

    bool MakeGrowStep(BidirectionalPath& path, PathContainer* paths_storage) override {
        if (is_detector_.InExistingLoop(path)) {
            DEBUG("in existing loop");
            return false;
        }
        DEBUG("un ch enabled " << used_storage_->UniqueCheckEnabled());
        bool result;
        LoopDetector loop_detector(&path, cov_map_);
        if (DetectCycle(path)) {
            result = false;
        } else if (path.Size() >= 1 && InvestigateShortLoop() && loop_detector.EdgeInShortLoop(path.Back()) && use_short_loop_cov_resolver_) {
            DEBUG("edge in short loop");
            result = ResolveShortLoop(path);
        } else if (InvestigateShortLoop() && loop_detector.PrevEdgeInShortLoop() && use_short_loop_cov_resolver_) {
            DEBUG("Prev edge in short loop");
            path.PopBack();
            result = ResolveShortLoop(path);
        } else {
            DEBUG("Making step");
            result = MakeSimpleGrowStep(path, paths_storage);
            DEBUG("Made step");
            if (DetectCycle(path)) {
                result = false;
            } else if (path.Size() >= 1 && InvestigateShortLoop() && loop_detector.EdgeInShortLoop(path.Back())) {
                DEBUG("Edge in short loop");
                result = ResolveShortLoop(path);
            } else if (InvestigateShortLoop() && loop_detector.PrevEdgeInShortLoop()) {
                DEBUG("Prev edge in short loop");
                path.PopBack();
                result = ResolveShortLoop(path);
            }
        }
        return result;
    }

private:
    bool ResolveShortLoop(BidirectionalPath& p) {
        if (use_short_loop_cov_resolver_) {
            return ResolveShortLoopByCov(p);
        } else {
            return ResolveShortLoopByPI(p);
        }
    }

    bool InvestigateShortLoop() {
        return investigate_short_loops_ && (use_short_loop_cov_resolver_ || CanInvestigateShortLoop());
    }
protected:
    DECL_LOGGER("LoopDetectingPathExtender")
};

class SimpleExtender: public LoopDetectingPathExtender {

protected:

    shared_ptr<ExtensionChooser> extensionChooser_;

    void FindFollowingEdges(BidirectionalPath& path, ExtensionChooser::EdgeContainer * result) {
        DEBUG("Looking for the following edges")
        result->clear();
        vector<EdgeId> edges;
        DEBUG("Pushing back")
        utils::push_back_all(edges, g_.OutgoingEdges(g_.EdgeEnd(path.Back())));
        result->reserve(edges.size());
        for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
            DEBUG("Adding edge w distance " << g_.int_id(*iter));
            result->push_back(EdgeWithDistance(*iter, 0));
        }
        DEBUG("Following edges found");
    }


public:

    SimpleExtender(const conj_graph_pack &gp,
                       const GraphCoverageMap &cov_map,
                       shared_ptr<ExtensionChooser> ec,
                       size_t is,
                       bool investigate_short_loops,
                       bool use_short_loop_cov_resolver) :
        LoopDetectingPathExtender(gp, cov_map, investigate_short_loops, use_short_loop_cov_resolver, is),
        extensionChooser_(ec) {
            used_storage_ = make_shared<UsedUniqueStorage>(UsedUniqueStorage(ScaffoldingUniqueEdgeStorage()));
        }

    std::shared_ptr<ExtensionChooser> GetExtensionChooser() const {
        return extensionChooser_;
    }

    bool CanInvestigateShortLoop() const override {
        return extensionChooser_->WeightCounterBased();
    }

    bool ResolveShortLoopByCov(BidirectionalPath& path) override {
        LoopDetector loop_detector(&path, cov_map_);
        size_t init_len = path.Length();
        bool result = false;
        while (path.Size() >= 1 && loop_detector.EdgeInShortLoop(path.Back())) {
            cov_loop_resolver_.ResolveShortLoop(path);
            if (init_len == path.Length()) {
                return result;
            } else {
                result = true;
            }
            init_len = path.Length();
        }
        return true;
    }

    bool ResolveShortLoopByPI(BidirectionalPath& path) override {
        if (extensionChooser_->WeightCounterBased()) {
            LoopResolver loop_resolver(g_, extensionChooser_->wc());
            LoopDetector loop_detector(&path, cov_map_);
            size_t init_len = path.Length();
            bool result = false;
            while (path.Size() >= 1 && loop_detector.EdgeInShortLoop(path.Back())) {
                loop_resolver.ResolveShortLoop(path);
                if (init_len == path.Length()) {
                    return result;
                } else {
                    result = true;
                }
                init_len = path.Length();
            }
            return true;
        }
        return false;
    }

    bool MakeSimpleGrowStep(BidirectionalPath& path, PathContainer* paths_storage) override {
        ExtensionChooser::EdgeContainer candidates;
        return FilterCandidates(path, candidates) && AddCandidates(path, paths_storage, candidates);
    }

protected:
    virtual bool FilterCandidates(BidirectionalPath& path, ExtensionChooser::EdgeContainer& candidates) {
        if (path.Size() == 0) {
            return false;
        }
        DEBUG("Simple grow step");
        path.Print();
        FindFollowingEdges(path, &candidates);
        DEBUG("found candidates");
        DEBUG(candidates.size())
        if (candidates.size() == 1) {
            LoopDetector loop_detector(&path, cov_map_);
            if (!investigate_short_loops_ && (loop_detector.EdgeInShortLoop(path.Back()) or loop_detector.EdgeInShortLoop(candidates.back().e_))
                && extensionChooser_->WeightCounterBased()) {
                return false;
            }
        }
        DEBUG("more filtering");
        candidates = extensionChooser_->Filter(path, candidates);
        DEBUG("filtered candidates");
        DEBUG(candidates.size())
        return true;
    }

    virtual bool AddCandidates(BidirectionalPath& path, PathContainer* /*paths_storage*/, ExtensionChooser::EdgeContainer& candidates) {
        if (candidates.size() != 1)
            return false;

        LoopDetector loop_detector(&path, cov_map_);
        DEBUG("loop detecor");
        if (!investigate_short_loops_ &&
            (loop_detector.EdgeInShortLoop(path.Back()) or loop_detector.EdgeInShortLoop(candidates.back().e_))
            && extensionChooser_->WeightCounterBased()) {
            return false;
        }
        DEBUG("push");
        EdgeId eid = candidates.back().e_;
//In 2015 modes when trying to use already used unique edge, it is not added and path growing stops.
//That allows us to avoid overlap removal hacks used earlier.
        if (used_storage_->UniqueCheckEnabled()) {
            DEBUG("un chk enabled");
            if (used_storage_->IsUsedAndUnique(eid)) {
                return false;
            } else {
                used_storage_->insert(eid);
            }
        }
        path.PushBack(eid, Gap(candidates.back().d_));
        DEBUG("push done");
        return true;
    }

    DECL_LOGGER("SimpleExtender")

};


class MultiExtender: public SimpleExtender {
    size_t max_candidates_;

public:
    MultiExtender(const conj_graph_pack &gp,
                      const GraphCoverageMap &cov_map,
                      shared_ptr<ExtensionChooser> ec,
                      size_t is,
                      bool investigate_short_loops,
                      bool use_short_loop_cov_resolver,
                      size_t max_candidates = 0) :
        SimpleExtender(gp, cov_map, ec, is, investigate_short_loops, use_short_loop_cov_resolver),
        max_candidates_(max_candidates) {
    }

protected:
    bool AddCandidates(BidirectionalPath& path, PathContainer* paths_storage, ExtensionChooser::EdgeContainer& candidates) override {
        if (candidates.size() == 0)
            return false;

        bool res = false;
        LoopDetector loop_detector(&path, cov_map_);
        DEBUG("loop detecor");
        if (!investigate_short_loops_ &&
            (loop_detector.EdgeInShortLoop(path.Back()) or loop_detector.EdgeInShortLoop(candidates.back().e_))
            && extensionChooser_->WeightCounterBased()) {
	    DEBUG("loop deteced");
            return false;
        }
        if (candidates.size() == 1) {
            DEBUG("push");
            EdgeId eid = candidates.back().e_;
            path.PushBack(eid, Gap(candidates.back().d_));
            DEBUG("push done");
            return true;
        }
        else if (candidates.size() == 2) {
             //Check for bulge
            auto v = g_.EdgeStart(candidates.front().e_);
            auto u = g_.EdgeEnd(candidates.front().e_);
            for (auto edge : candidates) {
                if (v != g_.EdgeStart(edge.e_) || u != g_.EdgeEnd(edge.e_))
                    return false;
            }

            //Creating new paths for other than new candidate.
            for (size_t i = 1; i < candidates.size(); ++i) {
                DEBUG("push other candidates " << i);
                BidirectionalPath *p = new BidirectionalPath(path);
                p->PushBack(candidates[i].e_, Gap(candidates[i].d_));
                BidirectionalPath *cp = new BidirectionalPath(p->Conjugate());
                paths_storage->AddPair(p, cp);
            }

            DEBUG("push");
            path.PushBack(candidates.front().e_, Gap(candidates.front().d_));
            DEBUG("push done");
            res = true;

            if (candidates.size() > 1) {
                DEBUG("Found " << candidates.size() << " candidates");
            }
        }

        return res;
    }

protected:
    DECL_LOGGER("MultiExtender")

};


class ScaffoldingPathExtender: public LoopDetectingPathExtender {
    std::shared_ptr<ExtensionChooser> extension_chooser_;
    ExtensionChooser::EdgeContainer sources_;
    std::shared_ptr<GapAnalyzer> gap_analyzer_;
    bool avoid_rc_connections_;

//When check_sink_ set to false we can scaffold not only tips
    bool check_sink_;

    void InitSources() {
        sources_.clear();

        for (auto iter = g_.ConstEdgeBegin(); !iter.IsEnd(); ++iter) {
            if (g_.IncomingEdgeCount(g_.EdgeStart(*iter)) == 0) {
                sources_.push_back(EdgeWithDistance(*iter, 0));
            }
        }
    }

    bool IsSink(EdgeId e) const {
        return g_.OutgoingEdgeCount(g_.EdgeEnd(e)) == 0;
    }

    Gap ConvertGapDescription(const GapDescription &gap) const {
        if (gap == GapDescription()) {
            return Gap::INVALID();
        }
        return Gap(gap.estimated_dist() + int(g_.k())
                   - int(gap.left_trim()) - int(gap.right_trim()),
                   uint32_t(gap.left_trim()), uint32_t(gap.right_trim()));
    }

protected:
    virtual bool CheckGap(const Gap &/*gap*/) const {
        return true;
    }

    //TODO fix awful design with virtual CheckGap and must_overlap flag!
    bool MakeSimpleGrowStepForChooser(BidirectionalPath& path, std::shared_ptr<ExtensionChooser> ec,
                                      bool must_overlap = false) {
        if (path.Size() < 1 || (check_sink_ && !IsSink(path.Back()))) {
            return false;
        }

        DEBUG("Simple grow step, growing path");
        path.Print();
        ExtensionChooser::EdgeContainer candidates = ec->Filter(path, sources_);
        DEBUG("scaffolding candidates " << candidates.size() << " from sources " << sources_.size());

        DEBUG("Candidate size = " << candidates.size())
        if (candidates.size() != 1) {
            DEBUG("scaffolding end");
            return false;
        }

        EdgeId e = candidates.back().e_;
        if (e == path.Back()
            || (avoid_rc_connections_ && e == g_.conjugate(path.Back()))) {
            return false;
        }

        if (this->DetectCycleScaffolding(path, e)) {
            return false;
        }

        Gap gap;
        //TODO is it ok that we either force joining or ignore its possibility
        if (check_sink_) {
            gap = ConvertGapDescription(gap_analyzer_->FixGap(GapDescription(path.Back(), e,
                                                                             candidates.back().d_ -
                                                                             int(g_.k()))));

            if (gap == Gap::INVALID()) {
                DEBUG("Looks like wrong scaffolding. PathId: "
                              << path.GetId() << " path length: " << path.Length()
                              << ", estimated gap length: " << candidates.back().d_);
                return false;
            }

            DEBUG("Gap after fixing " << gap.gap << " (was " << candidates.back().d_ << ")");

            if (must_overlap && !CheckGap(gap)) {
                DEBUG("Overlap is not large enough")
                return false;
            }
        } else {
            DEBUG("Gap joiners off");
            VERIFY(candidates.back().d_ > int(g_.k()));
            gap = Gap(candidates.back().d_);
        }

        if (used_storage_->UniqueCheckEnabled()) {
            if (used_storage_->IsUsedAndUnique(e)) {
                return false;
            } else {
                used_storage_->insert(e);
            }
        }
        DEBUG("Scaffolding. PathId: " << path.GetId() << " path length: " << path.Length() << ", fixed gap : "
                                      << gap.gap << ", trash length: " << gap.trash_previous << "-" << gap.trash_current);
        path.PushBack(e, gap);
        return true;
    }

public:

    ScaffoldingPathExtender(const conj_graph_pack &gp,
                            const GraphCoverageMap &cov_map,
                            std::shared_ptr<ExtensionChooser> extension_chooser,
                            std::shared_ptr<GapAnalyzer> gap_analyzer,
                            size_t is,
                            bool investigate_short_loops,
                            bool avoid_rc_connections,
                            bool check_sink = true):
        LoopDetectingPathExtender(gp, cov_map, investigate_short_loops, false, is),
        extension_chooser_(extension_chooser),
        gap_analyzer_(gap_analyzer),
        avoid_rc_connections_(avoid_rc_connections),
        check_sink_(check_sink)
    {
        InitSources();
    }

    bool MakeSimpleGrowStep(BidirectionalPath& path, PathContainer* /*paths_storage*/) override {
        return MakeSimpleGrowStepForChooser(path, extension_chooser_);
    }

    bool ResolveShortLoopByCov(BidirectionalPath&) override {
        return false;
    }

    bool ResolveShortLoopByPI(BidirectionalPath&) override {
        return false;
    }

    std::shared_ptr<ExtensionChooser> GetExtensionChooser() const {
        return extension_chooser_;
    }

protected:
    DECL_LOGGER("ScaffoldingPathExtender");
};


class RNAScaffoldingPathExtender: public ScaffoldingPathExtender {
    std::shared_ptr<ExtensionChooser> strict_extension_chooser_;

    int min_overlap_;

protected:
    bool CheckGap(const Gap &gap) const override {
        return gap.overlap_after_trim(g_.k()) >= min_overlap_;
    }

public:

    RNAScaffoldingPathExtender(const conj_graph_pack &gp,
                               const GraphCoverageMap &cov_map,
                               std::shared_ptr<ExtensionChooser> extension_chooser,
                               std::shared_ptr<ExtensionChooser> strict_extension_chooser,
                               std::shared_ptr<GapAnalyzer> gap_joiner,
                               size_t is,
                               bool investigate_short_loops,
                               int min_overlap = 0):
        ScaffoldingPathExtender(gp, cov_map, extension_chooser, gap_joiner, is, investigate_short_loops, true),
        strict_extension_chooser_(strict_extension_chooser), min_overlap_(min_overlap) {}


    bool MakeSimpleGrowStep(BidirectionalPath& path, PathContainer* /*paths_storage*/) override {
        return MakeSimpleGrowStepForChooser(path, GetExtensionChooser(), true) ||
            MakeSimpleGrowStepForChooser(path, strict_extension_chooser_);
    }

};

}
