//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * pe_io.hpp
 *
 *  Created on: Mar 15, 2012
 *      Author: andrey
 */

#ifndef PE_IO_HPP_
#define PE_IO_HPP_


#include "bidirectional_path.hpp"

namespace path_extend {

using namespace debruijn_graph;

class ContigWriter {

protected:
	Graph& g_;
    size_t k_;


	string ToString(const BidirectionalPath& path) const{
		stringstream ss;
		if (!path.Empty()) {
			ss <<g_.EdgeNucls(path[0]).Subseq(0, k_).str();
		}

		for (size_t i = 0; i < path.Size(); ++i) {
			int gap = i == 0 ? 0 : path.GapAt(i);
			if (gap > (int) k_) {
				for (size_t j = 0; j < gap - k_; ++j) {
					ss << "N";
				}
				ss << g_.EdgeNucls(path[i]).str();
			} else {
				int overlapLen = k_ - gap;
				if (overlapLen >= (int) g_.length(path[i]) + (int) k_) {
					continue;
				}

				ss << g_.EdgeNucls(path[i]).Subseq(overlapLen).str();
			}
		}
		return ss.str();
	}

    Sequence ToSequence(const BidirectionalPath& path) const {
        SequenceBuilder result;

        if (!path.Empty()) {
            result.append(g_.EdgeNucls(path[0]).Subseq(0, k_));
        }
        for (size_t i = 0; i < path.Size(); ++i) {
            result.append(g_.EdgeNucls(path[i]).Subseq(k_));
        }

        return result.BuildSequence();
    }



public:
    ContigWriter(Graph& g): g_(g), k_(g.k()){

    }

    void writeEdges(const string& filename) {
        INFO("Outputting edges to " << filename);
        osequencestream_with_data_for_scaffold oss(filename);

        set<EdgeId> included;
        for (auto iter = g_.SmartEdgeBegin(); !iter.IsEnd(); ++iter) {
            if (included.count(*iter) == 0) {
                oss.setCoverage(g_.coverage(*iter));
                oss.setID(g_.int_id(*iter));
                oss << g_.EdgeNucls(*iter);

                included.insert(*iter);
                included.insert(g_.conjugate(*iter));
            }
        }
        INFO("Contigs written");
    }


    void writePathEdges(PathContainer& paths, const string& filename){
		INFO("Outputting path data to " << filename);
		ofstream oss;
        oss.open(filename.c_str());
        int i = 1;
        for (auto iter = paths.begin(); iter != paths.end(); ++iter) {
			oss << i << endl;
			i++;
            BidirectionalPath* path = iter.get();
            if (path->GetId() % 2 != 0) {
                path = path->getConjPath();
            }
            oss << "PATH " << path->GetId() << " " << path->Size() << " " << path->Length() + k_ << endl;
            for (size_t j = 0; j < path->Size(); ++j) {
			    oss << g_.int_id(path->At(j)) << " " << g_.length(path->At(j)) << endl;
            }
            oss << endl;
		}
		oss.close();
		INFO("Edges written");
	}

    void writePaths(PathContainer& paths, const string& filename) {

        INFO("Writing contigs to " << filename);
        osequencestream_with_data_for_scaffold oss(filename);
        int i = 0;
        for (auto iter = paths.begin(); iter != paths.end(); ++iter) {
        	if (iter.get()->Length() <= 0){
        		continue;
        	}
        	DEBUG("NODE " << ++i);
            BidirectionalPath* path = iter.get();
            if (path->GetId() % 2 != 0) {
                path = path->getConjPath();
            }
            path->Print();
        	oss.setID(path->GetId());
            oss.setCoverage(path->Coverage());
            oss << ToString(*path);
        }
        INFO("Contigs written");
    }


};


class PathInfoWriter {


public:

    void writePaths(PathContainer& paths, const string& filename){
        ofstream oss(filename);

        for (auto iter = paths.begin(); iter != paths.end(); ++iter) {
            iter.get()->Print(oss);
        }

        oss.close();
    }
};

}

#endif /* PE_IO_HPP_ */
