//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "xmath.h"
#include "omni/omni_utils.hpp"

#include <cmath>
#include <map>
#include <limits>

//#define MERGE_DATA_RELATIVE_DIFFERENCE 0.3

namespace omnigraph {

/**
 * PairInfo class represents basic data unit for paired information: edges first and second appear
 * in genome at distance d_ and this information has weight weight_.
 */
struct Point {

 public:
  double d;
  double weight;
  double var;

  explicit Point(double distance, double weight, double variance)
  : d(distance),
    weight(weight),
    var(variance)
  {
  }

  Point(const Point& rhs)
  : d(rhs.d),
    weight(rhs.weight),
    var(rhs.var)
  {
  }

  string str() const {
    stringstream ss;
    ss << "Point: " << " distance = " << this->d
      << ", weight = " << this->weight
      << ", variance = " << this->var;
    return ss.str();
  }

  Point& operator=(const Point& rhs) {
    using namespace math;
    update_value_if_needed<double>(d, rhs.d);
    update_value_if_needed<double>(weight, rhs.weight);
    update_value_if_needed<double>(var, rhs.var);
    return *this;
  }

  bool operator<(const Point& rhs) const {
    return math::ls(this->d, rhs.d);
  }

  bool operator==(const Point& rhs) const {
    return math::eq(this->d, rhs.d);
  }

  bool operator!=(const Point& rhs) const {
    return !(operator==(rhs));
  }

  const Point operator-() const {
    Point negated_this(-(this->d), this->weight, this->var);
    return negated_this;
  }
};

inline int rounded_d(Point p) {
  return math::round_to_zero(p.d);
}

ostream& operator<<(ostream& os, Point point) {
  return os << point.str();
}

// tuple of a pair of edges @first, @second, and a @point
template<typename EdgeId>
struct PairInfo {
  EdgeId first;
  EdgeId second;
  Point point;

  PairInfo(const PairInfo& pair_info) :
      first(pair_info.first), second(pair_info.second), point(pair_info.point)
  {
  }

  PairInfo(EdgeId first, EdgeId second, double d, double weight, double var) :
      first(first), second(second), point(d, weight, var)
  {
  }

  PairInfo(EdgeId first, EdgeId second, Point point) :
      first(first), second(second), point(point)
  {
  }

  // Two paired infos are considered equal
  // if they coinside in all parameters except for weight and variance.
  bool operator==(const PairInfo& rhs) const {
    const PairInfo &lhs = *this;
    return lhs.first == rhs.first && lhs.second == rhs.second && lhs.point == rhs.point;
  }

  bool operator!=(const PairInfo& rhs) const {
    return !(*this == rhs);
  }

  bool operator<(const PairInfo<EdgeId>& rhs) const {
    const PairInfo<EdgeId>& lhs = *this;
    return lhs.first == rhs.first ?
           lhs.second == rhs.second ?
           lhs.point < rhs.point : lhs.second < rhs.second
                         : lhs.first  < rhs.first;
  }

  double d() const      { return point.d;      }
  double weight() const { return point.weight; }
  double var() const    { return point.var;    }

  double& d()                 { return point.d;      }
  double& weight()            { return point.weight; }
  double& var()               { return point.var;    }
};

template<typename EdgeId>
ostream& operator<<(ostream& os, const PairInfo<EdgeId>& info) {
  return os << "PairInfo: first = " << info.first << ", second = " << info.second
            << "Point : " << info.point;
}

template<typename EdgeId>
const PairInfo<EdgeId> MinPairInfo(EdgeId id) {
  return PairInfo<EdgeId>(id, EdgeId(typename EdgeId::pointer_type(1)),
      -10000000000, 0., 0.);
}

template<typename EdgeId>
const PairInfo<EdgeId> MaxPairInfo(EdgeId id) {
  return PairInfo<EdgeId>(id, EdgeId(typename EdgeId::pointer_type(-1)),
       10000000000, 0., 0.);
}

template<typename EdgeId>
const PairInfo<EdgeId> MinPairInfo(EdgeId e1, EdgeId e2) {
  PairInfo<EdgeId> info = MinPairInfo(e1);
  info.second = e2;
  return info;
}

template<typename EdgeId>
const PairInfo<EdgeId> MaxPairInfo(EdgeId e1, EdgeId e2) {
  PairInfo<EdgeId> info = MaxPairInfo(e1);
  info.second = e2;
  return info;
}

/**
 * Method returns approximate distance between occurrences of edges in genome rounded to the nearest
 * integer. In case of a tie closest to 0 value is chosen thus one can assume that distance
 * is rounded the same way as opposite one.
 * todo check that written here is true
 */
template<typename EdgeId>
inline int rounded_d(PairInfo<EdgeId> const& pi) {
  return math::round_to_zero(pi.d());
}

inline bool ClustersIntersect(Point p1, Point p2) {
  return math::le(p1.d, p2.d + p1.var + p2.var) &&
         math::le(p2.d, p1.d + p1.var + p2.var);
}

inline Point ConjugatePoint(size_t l1, size_t l2, const Point& point) {
  return Point(point.d + (double) l2 - (double) l1, point.weight, point.var);
}

template<typename EdgeId>
PairInfo<EdgeId> BackwardInfo(const PairInfo<EdgeId>& pi) {
  return PairInfo<EdgeId>(pi.second, pi.first, -pi.point);
}

template<typename EdgeId>
inline bool IsSymmetric(PairInfo<EdgeId> const& pi) {
  return pi.first == pi.second && math::eq(pi.d(), 0.);
}

//TODO: try storing set<PairInfo>
template <typename EdgeId>
class PairInfoIndexData {
public:
  typedef set<PairInfo<EdgeId> > Data;
  typedef typename Data::iterator data_iterator;
  typedef vector<PairInfo<EdgeId> > PairInfos;

  //  we can not update elements in the std::set,
  //  although we can delete element,
  //  and then insert a modified version of it
  //  but here we do not care about safety, and making illegal @const_cast on the std::set element
  void UpdateSingleInfo(const PairInfo<EdgeId>& info, double new_dist, double new_weight, double new_variance) {
    TRACE(info << " is about to be merged with " << new_dist << " " << new_weight << " " << new_variance);
    PairInfo<EdgeId>& info_to_update = const_cast<PairInfo<EdgeId>&>(*data_.find(info));
    using namespace math;
    update_value_if_needed<double>(info_to_update.d(), new_dist);
    update_value_if_needed<double>(info_to_update.weight(), new_weight);
    update_value_if_needed<double>(info_to_update.var, new_variance);
  }

    //TODO: rename the method
  void ReplaceFirstEdge(const PairInfo<EdgeId>& info, EdgeId newId) {
    data_.insert(PairInfo<EdgeId>(newId, info.second, info.point));
  }

  PairInfoIndexData() :
      data_() {
  }

  data_iterator begin() const {
    return data_.begin();
  }

  data_iterator end() const {
    return data_.end();
  }

  size_t size() const {
    return data_.size();
  }

  void AddPairInfo(const PairInfo<EdgeId>& pair_info, bool add_reversed = 1) {
        data_.insert(pair_info);

    if (!IsSymmetric(pair_info) && add_reversed)
      data_.insert(BackwardInfo(pair_info));
  }

  void UpdateInfo(const PairInfo<EdgeId>& info, double new_dist, double new_weight, double new_variance, bool add_reversed) {
        // first we update backward info in order to leave @info not modified
    if (add_reversed && !IsSymmetric(info))
      UpdateSingleInfo(BackwardInfo(info), -new_dist, new_weight, new_variance);

    UpdateSingleInfo(info, new_dist, new_weight, new_variance);
  }

  void DeleteEdgeInfo(EdgeId e) {
    set<PairInfo<EdgeId> > paired_edges;

    for (auto lower = LowerBound(e), upper = UpperBound(e); lower != upper;
        ++lower) {
      paired_edges.insert(BackwardInfo(*lower));
    }

    for (auto it = paired_edges.begin(); it != paired_edges.end(); ++it) {
      data_.erase(*it);
    }

    data_.erase(LowerBound(e), UpperBound(e));
  }

  void DeletePairInfo(const PairInfo<EdgeId>& info) {
    VERIFY(data_.find(info) != data_.end());
    data_.erase(info);
  }

  void DeleteEdgePairInfo(EdgeId e1, EdgeId e2) {
    data_.erase(LowerBound(e1, e2), UpperBound(e1, e2));
    if (e1 != e2)
      data_.erase(LowerBound(e2, e1), UpperBound(e2, e1));
  }

  PairInfos GetEdgeInfos(EdgeId e) const {
    return PairInfos(LowerBound(e), UpperBound(e));
  }

  PairInfos GetEdgePairInfos(EdgeId e1, EdgeId e2) const {
    return PairInfos(LowerBound(e1, e2), UpperBound(e1, e2));
  }

  void clear() {
    data_.clear();
  }

    data_iterator Find(const PairInfo<EdgeId>& pair_info) const {
        return data_.find(pair_info);
    }

  data_iterator LowerBound(const PairInfo<EdgeId>& pair_info) const {
    return data_.lower_bound(pair_info);
  }

  data_iterator UpperBound(const PairInfo<EdgeId>& pair_info) const {
    return data_.upper_bound(pair_info);
  }

  data_iterator LowerBound(EdgeId e) const {
    return data_.lower_bound(MinPairInfo(e));
  }

  data_iterator UpperBound(EdgeId e) const {
    return data_.upper_bound(MaxPairInfo(e));
  }

  data_iterator LowerBound(EdgeId e1, EdgeId e2) const {
    return data_.lower_bound(MinPairInfo(e1, e2));
  }

  data_iterator UpperBound(EdgeId e1, EdgeId e2) const {
    return data_.upper_bound(MaxPairInfo(e1, e2));
  }

private:
  Data data_;

    DECL_LOGGER("PairedInfoIndexData");
};

// wrapper for a map { (EdgeId -> (d, weight, var)) }
template<class Graph>
class InnerMap {
  typedef typename Graph::EdgeId EdgeId;
  typedef set<Point> Histogram;
  typedef map<EdgeId, Histogram> base_map_t;
  typedef typename base_map_t::value_type value_type;

 public:
  typedef typename base_map_t::const_iterator c_iterator;
  typedef typename base_map_t::iterator iterator;

  // we need this iterator
  // to iterate through map<EdgeId, set<Point> > in a smart way (faster)
  class FastIterator {
    typedef typename Histogram::const_iterator histogram_iterator;

   public:
    FastIterator(c_iterator position, const InnerMap& map) :
      position_(position), inner_pos_(position->second.begin()), map_(&map)
    {
    }

    FastIterator(c_iterator position,
                 histogram_iterator inner_pos,
                 const InnerMap& map) :
      position_(position), inner_pos_(inner_pos), map_(&map)
    {
    }

    bool operator==(const FastIterator& other) const {
      return (this->position_ == other.position_) && (this->inner_pos_ == other.inner_pos_);
    }

    bool operator!=(const FastIterator& other) const {
      return !operator==(other);
    }

    const pair<EdgeId, Point> operator*() const {
      return make_pair(position_->first, *inner_pos_);
    }

    FastIterator& operator++() {
      if (position_ == (map_->wrapped_map_).end())
        return *this;

      ++inner_pos_;
      const Histogram& hist = position_->second;
      if (inner_pos_ == hist.end()) {
        ++position_;
        if (position_ != (map_->wrapped_map_).end())
          inner_pos_ = (position_->second).begin();
      }
      return *this;
    }

   private:
    c_iterator position_;          // iterator in map<EdgeId, Histogram>
    histogram_iterator inner_pos_; // iterator in Histogram
    const InnerMap* map_;          // wrapper of map<EdgeId, Histogram>
  };

 public:
  InnerMap()
  {
  }

  template<class Iterator>
  InnerMap(Iterator begin, Iterator end) :
    wrapped_map_(begin, end, 10)
  {
  }

  // these two methods require a wrapper for map<EdgeId, Histogram>
  // we need them to iterate through map<EdgeId, set<Point> > in a smart way
  FastIterator Begin() const {
    if (wrapped_map_.size() == 0)
      return this->End();
    return FastIterator(wrapped_map_.begin(), *this);
  }

  FastIterator End() const {
    if (wrapped_map_.size() == 0) {
      static Histogram EMPTY_HISTOGRAM;
      return FastIterator(wrapped_map_.end(), EMPTY_HISTOGRAM.end(), *this);
    }
    const Histogram& last_histogram = (wrapped_map_.rbegin())->second;
    return FastIterator(wrapped_map_.end(), last_histogram.end(), *this);
  }

  c_iterator begin() const  { return wrapped_map_.begin(); }
  c_iterator end()   const  { return wrapped_map_.end();   }

  iterator begin()     { return wrapped_map_.begin(); }
  iterator end  ()     { return wrapped_map_.end  (); }

  c_iterator find(EdgeId edge) const { return wrapped_map_.find(edge); }

  iterator find(EdgeId edge) { return wrapped_map_.find(edge); }

  size_t count(EdgeId item) const { return wrapped_map_.count(item); }

  Histogram& operator[](EdgeId x) { return wrapped_map_[x]; }

  pair<iterator, bool> insert(const value_type& value) { return wrapped_map_.insert(value); }

  size_t erase(EdgeId x) { return wrapped_map_.erase(x); }

  void erase(iterator position) { wrapped_map_.erase(position); }

  bool empty() const { return wrapped_map_.empty(); }

  void clear() { wrapped_map_.clear(); }

  size_t size() const {
    size_t size = 0;
    for (auto I = wrapped_map_.begin(), E = wrapped_map_.end(); I != E; ++I)
      size += I->second.size();
    return size;
  }

  bool operator==(const map<EdgeId, Histogram>& rhs) const
  {
    if (size() != rhs.size())
      return false;

    for (auto i = wrapped_map_.begin(), j = rhs.wrapped_map_.begin();
        i != wrapped_map_.end() && j != rhs.wrapped_map_.end();
        ++i, ++j)
    {
      if(*i != *j)
        return false;
    }
    return true;
  }

  bool operator!=(const map<EdgeId, Histogram>& rhs) const
  {
    return !(*this == rhs);
  }

 private:
  base_map_t wrapped_map_;
};

// new map { EdgeId -> (EdgeId -> (d, weight, var)) }
template<class Graph>
class PairedInfoIndexT: public GraphActionHandler<Graph> {

 public:
  typedef typename Graph::EdgeId EdgeId;
  typedef set<Point> Histogram;
  typedef typename Histogram::const_iterator HistIterator;
  typedef map<EdgeId, InnerMap<Graph> > IndexDataType;     // @InnerMap is a wrapper for map<EdgeId, Histogram>
  typedef typename IndexDataType::const_iterator DataIterator;

  // This class helps us to iterate through the compound structure
  class EdgePairIterator {
    typedef typename InnerMap<Graph>::c_iterator InnerIterator;

   public:
    EdgePairIterator(DataIterator position, const PairedInfoIndexT& index) :
      position_(position), inner_pos_((position->second).begin()), index_(index)
    {
    }

    EdgePairIterator(DataIterator position,
                     InnerIterator inner_pos,
                     const PairedInfoIndexT& index) :
      position_(position), inner_pos_(inner_pos), index_(index)
    {
    }

    bool operator==(const EdgePairIterator& other) {
      return this->position_ == other.position_ && this->inner_pos_ == other.inner_pos_;
    }

    bool operator!=(const EdgePairIterator& other) {
      return !operator==(other);
    }

    Histogram operator*() const {
      VERIFY(position_ != index_.index_.end());
      return inner_pos_->second;
    }

    const EdgeId first() const {
      VERIFY(position_ != index_.index_.end());
      return position_->first;
    }

    const EdgeId second() const {
      VERIFY(position_ != index_.index_.end());
      const auto& inner_map = position_->second;
      VERIFY(inner_pos_ != inner_map.end());
      return inner_pos_->first;
    }

    const Histogram* operator->() const {
      VERIFY(position_ != index_.index_.end());
      const Histogram& hist = inner_pos_->second;
      return &hist;
    }

    EdgePairIterator& operator++() {
      if (position_ == index_.index_.end())
        return *this;

      ++inner_pos_;
      const InnerMap<Graph>& inner_map = position_->second;
      VERIFY(inner_map.size() > 0);
      if (inner_pos_ == inner_map.end()) {
        ++position_;
        if (position_ != index_.index_.end()) {
          inner_pos_ = (position_->second).begin();
          VERIFY(inner_pos_ != (position_->second).end());
        }
      }
      return *this;
    }

    void operator++(int) {
      EdgePairIterator tmp(*this);
      this->operator++();
      return tmp;
    }

    friend ostream& operator<<(ostream& os, const EdgePairIterator& iter) {
      return os << iter.first() << " " << iter.second();
    }

   private:
    DataIterator position_;
    InnerIterator inner_pos_;
    const PairedInfoIndexT& index_;
  };

  PairedInfoIndexT(const Graph& graph) :
    GraphActionHandler<Graph>(graph, "PairedInfoIndexT"), size_(0)
  {
  }

  ~PairedInfoIndexT()
  {
    TRACE("~PairedInfoIndexT ok");
  }

  EdgePairIterator begin() const {
    VERIFY(this->IsAttached());
    if (index_.size() == 0)
      return this->end();
    return EdgePairIterator(index_.begin(), *this);
  }

  EdgePairIterator end() const {
    VERIFY(this->IsAttached());
    if (index_.size() == 0) {
      static InnerMap<Graph> EMPTY_MAP; // we need this map to act somehow when the index is empty
      return EdgePairIterator(index_.end(), EMPTY_MAP.end(), *this);
    }

    const InnerMap<Graph>& last_inner_map = (index_.rbegin())->second;
    return EdgePairIterator(index_.end(), last_inner_map.end(), *this);
  }

  DataIterator Begin() const {
    VERIFY(this->IsAttached());
    return index_.begin();
  }

  DataIterator End() const {
    VERIFY(this->IsAttached());
    return index_.end();
  }

  // adding pair infos
  void AddPairInfo(
      const pair<EdgeId, EdgeId>& edge_pair,
      Point point_to_add,
      bool add_reversed = 1)
  {
    this->AddPairInfo(edge_pair.first, edge_pair.second, point_to_add, add_reversed);
  }

  void AddPairInfo(
      const pair<EdgeId, EdgeId>& edge_pair,
      double d,
      double weight,
      double var,
      bool add_reversed = 1)
  {
    this->AddPairInfo(edge_pair.first, edge_pair.second, Point(d, weight, var), add_reversed);
  }

  void AddPairInfo(EdgeId e1,
      EdgeId e2,
      double d,
      double weight,
      double var,
      bool add_reversed = 1)
  {
    this->AddPairInfo(e1, e2, Point(d, weight, var), add_reversed);
  }

  void AddPairInfo(EdgeId e1,
                   EdgeId e2,
                   Point point_to_add,
                   bool add_reversed = 1)
  {
    VERIFY(this->IsAttached());
    Histogram& histogram = index_[e1][e2];
    HistIterator iterator_to_point = histogram.find(point_to_add);
    TRACE("Adding info " << this->g().int_id(e1)
                  << " " << this->g().int_id(e2)
                  << " " << point_to_add.str());

    if (iterator_to_point != histogram.end())
    {
      TRACE("Such pair info exists, merging now");
      const Point& existing_point = *iterator_to_point;
      VERIFY(existing_point == point_to_add);
      MergeData(e1, e2, existing_point, point_to_add, add_reversed);
    }
    else {
      TRACE("Such pair info does not exist");
      InsertPoint(e1, e2, histogram, point_to_add, add_reversed);
    }
  }

  // method adds paired info to the conjugate edges
  void AddConjPairInfo(EdgeId e1,
                       EdgeId e2,
                       Point point_to_add,
                       bool add_reversed = 1)
  {
    const Graph& g = this->g();
    this->AddPairInfo(g.conjugate(e2),
                      g.conjugate(e1),
                      ConjugatePoint(g.length(e1), g.length(e2), point_to_add),
                      add_reversed);
  }

  // erasing specific entry from the index
  size_t RemovePairInfo(EdgeId e1, EdgeId e2, const Point& point_to_remove) {
    VERIFY(this->IsAttached());
    auto iter = index_.find(e1);
    if (iter != index_.end())
    {
      InnerMap<Graph>& map = iter->second;
      auto iter2 = map.find(e2);
      if (iter2 != map.end())
      {
        Histogram& hist = iter2->second;
        size_t success = hist.erase(point_to_remove);
        if (success == 1)
          --size_;
        if (hist.empty())
          map.erase(iter2);
        if (map.empty())
          index_.erase(iter);

        return success;
      }
    }
    return 0;
  }

  // method adds paired info to the conjugate edges
  void RemoveConjPairInfo(EdgeId e1,
                          EdgeId e2,
                          Point point_to_remove)
  {
    const Graph& g = this->g();
    this->RemovePairInfo(g.conjugate(e2),
                         g.conjugate(e1),
                         ConjugatePoint(g.length(e1), g.length(e2), point_to_remove));
  }

  void RemovePairInfo(const PairInfo<EdgeId>& info) {
    this->RemovePairInfo(info.first, info.second, info.point);
  }

  // removing all points from @e1, @e2 histogram,
  // returns 1 if operation was successful, 0 if not
  size_t RemoveEdgePairInfo(EdgeId e1, EdgeId e2) {
    VERIFY(this->IsAttached());
    auto iter = index_.find(e1);
    if (iter != index_.end())
    {
      InnerMap<Graph>& map = iter->second;
      auto iter2 = map.find(e2);
      if (iter2 != map.end())
      {
        Histogram& hist = iter2->second;
        size_t size_decrease = hist.size();
        map.erase(iter2);
        size_ -= size_decrease;
        if (map.empty())
          index_.erase(iter);

        return 1;
      }
    }
    return 0;
  }

  // removes all points, which refer to this edge
  // also removes all backward information
  void RemoveEdgeInfo(EdgeId edge) {
    VERIFY(this->IsAttached());
    InnerMap<Graph>& inner_map = index_[edge];
    for (auto iter = inner_map.begin(); iter != inner_map.end(); ++iter) {
      EdgeId e2 = iter->first;
      if (edge != e2) {
        this->RemoveEdgePairInfo(e2, edge);
      }
    }
    size_t size_of_removed = inner_map.size();
    index_.erase(edge);
    size_ -= size_of_removed;
  }

  void Clear() {
    index_.clear();
    size_ = 0;
  }

  void Init() {
    for (auto it = this->g().SmartEdgeBegin(); !it.IsEnd(); ++it) {
      this->HandleAdd(*it);
    }
  }

  // here we trying to insert PairInfo,
  // if there is no existing PairInfo with the same key
  // very complicated implementation, but it seems to be faster.
  void AddAll(const PairedInfoIndexT& index_to_add) {
    typedef typename IndexDataType::iterator data_iterator;
    VERIFY(this->IsAttached());
    IndexDataType& base_index = this->index_;
    const IndexDataType& index = index_to_add.index_;
    for (auto AddI = index.begin(), E = index.end(); AddI != E; ++AddI) {
      EdgeId e1_to_add = AddI->first;
      const InnerMap<Graph>& map_to_add = AddI->second;
      const pair<data_iterator, bool>& result = base_index.insert(*AddI);
      if (!result.second) {
        InnerMap<Graph>& map_already_exists = (result.first)->second; // data_iterator points to <EdgeId, InnerMap>
        this->MergeInnerMaps(e1_to_add, map_to_add, map_already_exists);
      }
      else
        size_ += map_to_add.size();
    }
  }

  // prints the contents of index
  void PrintAll() const {
    size_t size = 0;
    for (auto I = this->begin(), E = this->end(); I != E; ++I) {
      EdgeId e1 = I.first();
      EdgeId e2 = I.second();
      const Histogram& histogram = *I;
      size += histogram.size();
      INFO("Histogram for edges "
          << this->g().int_id(e1) << " "
          << this->g().int_id(e2));
      for (auto it = histogram.begin(); it != histogram.end(); ++it) {
        INFO("    Entry " << it->str());
      }
    }
    VERIFY_MSG(size_ == size, "Size " << size << " must have been equal to " << size_);
  }

  // usual implementation, the same as in the old paired index
  vector<PairInfo<EdgeId> > GetEdgeInfo(EdgeId edge) const {
    VERIFY(this->IsAttached());
    typename IndexDataType::const_iterator iter = index_.find(edge);
    TRACE("Getting edge info");
    if (iter == index_.end())
      return vector<PairInfo<EdgeId> >();
    else {
      const InnerMap<Graph>& i_map= iter->second;
      vector<PairInfo<EdgeId> > result;
      result.reserve(i_map.size());
      for (auto I = i_map.Begin(), E = i_map.End(); I != E; ++I) {
        EdgeId e2 = (*I).first;
        Point point = (*I).second;
        result.push_back(PairInfo<EdgeId>(edge, e2, point));
      }
      return result;
    }
  }

  // faster implementation, but less resolver-friendly
  // returns InnerMap instead of vector<>,
  // one can iterate it using FastIterator class
  const InnerMap<Graph> GetEdgeInfo(EdgeId edge, int) const {
    VERIFY(this->IsAttached());
    typename IndexDataType::const_iterator iter = index_.find(edge);
    if (iter == index_.end())
      return InnerMap<Graph>();
    else
      return iter->second;
  }

  const Histogram GetEdgePairInfo(EdgeId e1, EdgeId e2) const
  {
    VERIFY(this->IsAttached());
    typename IndexDataType::const_iterator iter = index_.find(e1);
    if (iter == index_.end())
      return Histogram();
    else
    {
      const InnerMap<Graph>& inner_map = iter->second;
      typename InnerMap<Graph>::c_iterator iter2 = inner_map.find(e2);
      if (iter2 == inner_map.end())
        return Histogram();
      else
        return iter2->second;
    }
  }

  size_t size() const {
    return size_;
  }

  size_t int_id(EdgeId edge) const {
    return this->g().int_id(edge);
  }
  // Handlers
  virtual void HandleAdd(EdgeId edge) {
    TRACE("Handling Addition " << int_id(edge));
    this->AddPairInfo(edge, edge, 0., 0., 0.);
  }

  virtual void HandleDelete(EdgeId edge) {
    TRACE("Handling Deleting " << int_id(edge));
    this->RemoveEdgeInfo(edge);
  }

  virtual void HandleMerge(const vector<EdgeId>& old_edges, EdgeId new_edge) {
    TRACE("Handling Merging");
    this->AddPairInfo(new_edge, new_edge, 0., 0., 0.);
    int shift = 0;
    const Graph& graph = this->g();
    for (size_t i = 0; i < old_edges.size(); ++i) {
      EdgeId old_edge = old_edges[i];
      TransferInfo(old_edge, new_edge, shift);
      shift -= (int) graph.length(old_edge);
    }
  }

  virtual void HandleGlue(EdgeId new_edge, EdgeId e1, EdgeId e2) {
    TRACE("Handling Glueing " << int_id(new_edge) << " " << int_id(e1) << " "
        << int_id(e2));
    TransferInfo(e2, new_edge);
    TransferInfo(e1, new_edge);
  }

  virtual void HandleSplit(EdgeId old_edge, EdgeId new_edge_1, EdgeId new_edge_2) {
    TRACE("Handling Splitting " << int_id(old_edge) << " " << int_id(new_edge_1)
        << " " << int_id(new_edge_2));
    const Graph& graph = this->g();
    double ratio = (double) graph.length(new_edge_1) * 1. / (double) graph.length(old_edge);
    TransferInfo(old_edge, new_edge_1, 0, ratio);
    TransferInfo(old_edge, new_edge_2, (int) graph.length(new_edge_1), 1. - ratio);
  }

 private:
  inline bool IsSymmetric(EdgeId e1,
                          EdgeId e2,
                          Point point) const
  {
    return (e1 == e2) && math::eq(point.d, 0.);
  }

  // modifying the histogram
  void InsertPoint(EdgeId e1,
                   EdgeId e2,
                   Histogram& histogram,
                   Point new_point,
                   bool add_reversed)
  {
    // first backwards
    if (add_reversed && !IsSymmetric(e1, e2, new_point))
    {
      index_[e2][e1].insert(-new_point);
      ++size_;
    }

    histogram.insert(new_point);
    ++size_;
  }

  //  we can not update elements in the std::set,
  //  although we can delete element,
  //  and then insert a modified version of it.
  //  But here we do not care about the safety,
  //  and make an illegal @const_cast on the std::set element
  void UpdateSinglePoint(const Point& point_to_update, Point new_point)
  {
    Point& point_we_can_update = const_cast<Point&>(point_to_update);
    point_we_can_update = new_point;
  }

  void UpdateInfo(EdgeId e1,
      EdgeId e2,
      const Point& existing_point,
      Point new_point,
      bool add_reversed)
  {
    // first we update backward info in order to leave @existing_point not modified
    if (add_reversed && !IsSymmetric(e1, e2, new_point))
    {
      Point new_point_negated = -new_point;
      const Histogram& histogram = index_[e2][e1];
      const Point& existing_point_negated = *histogram.find(new_point_negated);
      UpdateSinglePoint(existing_point_negated, new_point_negated);
    }
    UpdateSinglePoint(existing_point, new_point);
  }

  void MergeData(EdgeId e1,
                 EdgeId e2,
                 const Point& point_to_update,
                 Point point_to_add,
                 bool add_reversed)
  {
    double weight_to_add = point_to_add.weight;
    // counting new bounds in the case, when we are merging pair infos with var != 0
    double left_bound = min(point_to_update.d - point_to_update.var,
        point_to_add.d - point_to_add.var);
    double right_bound = max(point_to_update.d + point_to_update.var,
        point_to_add.d + point_to_add.var);
    double new_dist = (left_bound + right_bound) * 0.5;
    double new_weight = point_to_update.weight + weight_to_add;
    double new_variance = (right_bound - left_bound) * 0.5;

    Point new_point(new_dist, new_weight, new_variance);
    this->UpdateInfo(e1, e2, point_to_update, new_point, add_reversed);
  }

  void TransferInfo(EdgeId old_edge,
                    EdgeId new_edge,
                    int shift = 0,
                    double weight_scale = 1.)
  {
    const InnerMap<Graph>& inner_map = this->GetEdgeInfo(old_edge, 0);
    for (auto iter = inner_map.begin(); iter != inner_map.end(); ++iter)
    {
      EdgeId e2 = iter->first;
      const Histogram& histogram = iter->second;
      for (auto point_iter = histogram.begin(); point_iter != histogram.end(); ++point_iter)
      {
        Point cur_point = *point_iter;
        if (old_edge != e2) {
          AddPairInfo(new_edge, e2,
              cur_point.d - shift,
              weight_scale * cur_point.weight,
              cur_point.var);
        }
        else if (!math::eq(cur_point.d, 0.))
        {
          AddPairInfo(new_edge, new_edge,
              cur_point.d,
              weight_scale * 0.5 * cur_point.weight,
              cur_point.var);
        }
        else {
          AddPairInfo(new_edge, new_edge,
              cur_point.d,
              weight_scale * cur_point.weight,
              cur_point.var);
        }
      }
    }
  }

  void MergeInnerMaps(EdgeId e1_to_add,
                      const InnerMap<Graph>& map_to_add,
                            InnerMap<Graph>& map)
  {
    typedef typename Histogram::iterator hist_iterator;
    typedef typename InnerMap<Graph>::iterator map_iterator;
    for (auto I = map_to_add.begin(), E = map_to_add.end(); I != E; ++I) {
      EdgeId e2_to_add = I->first;
      const Histogram& hist_to_add = I->second;
      const pair<map_iterator, bool>& result = map.insert(*I);
      if (!result.second) { // in this case we need to merge two hists
        Histogram& hist_exists = (result.first)->second;
        // pretty much the same
        for (auto p_it = hist_to_add.begin(), E = hist_to_add.end(); p_it != E; ++p_it) {
          Point new_point = *p_it;
          const pair<hist_iterator, bool>& result = hist_exists.insert(new_point);
          if (!result.second) // in this case we need to merge two points
          {
            const Point& point_exists = *result.first;
            this->MergeData(e1_to_add, e2_to_add, point_exists, new_point, false);
          } else
            ++size_;
        }
      } else
        size_ += hist_to_add.size();
    }
  }

  IndexDataType index_;
  size_t size_;

  DECL_LOGGER("PairedInfoIndexT");
};

template <class Graph>
struct PairedInfoIndicesT {
    typedef PairedInfoIndexT<Graph> IndexT;

    vector< IndexT* > data_;

    PairedInfoIndicesT(const Graph& graph, size_t lib_num) {
        for (size_t i = 0; i < lib_num; ++i) {
            data_.push_back(new IndexT(graph));
        }
    }

    void Init() {
        for (auto it = data_.begin(); it != data_.end(); ++it) {
            (*it)->Init();
        }
    }

    void Attach() {
        for (auto it = data_.begin(); it != data_.end(); ++it) {
            (*it)->Attach();
        }
    }

    void Detach() {
        for (auto it = data_.begin(); it != data_.end(); ++it) {
            (*it)->Detach();
        }
    }

    IndexT& operator[](size_t i) {
        return *data_[i];
    }

    const IndexT& operator[](size_t i) const {
        return *data_[i];
    }

};

/*----------------------------------------Old Index----------------------------------------------*/

/**
 * PairedInfoIndex stores information about edges connected by paired reads
 * and synchronizes this info with the graph.
 */
template<class Graph>
class PairedInfoIndex: public GraphActionHandler<Graph> {

public:
  typedef typename Graph::EdgeId EdgeId;
  typedef typename Graph::VertexId VertexId;
  typedef const vector<PairInfo<EdgeId> > PairInfos;
  typedef typename PairInfoIndexData<EdgeId>::data_iterator data_iterator;

public:
  /**
   * Class EdgePairIterator is used to iterate through paires of edges which have information about distance
   * between them stored in PairedInfoIndex.
   */
  class EdgePairIterator {
    typename PairInfoIndexData<EdgeId>::data_iterator position_;
    const PairedInfoIndex<Graph>& index_;
  public:
    EdgePairIterator(
        typename PairInfoIndexData<EdgeId>::data_iterator position,
        const PairedInfoIndex<Graph> &index) :
        position_(position), index_(index)
    {
    }

    bool operator==(const EdgePairIterator &other) {
      return this->position_ == other.position_;
    }

    bool operator!=(const EdgePairIterator &other) {
      return this->position_ != other.position_;
    }

    PairInfos operator*() const {
      return index_.GetEdgePairInfo(position_->first, position_->second);
    }

    void operator++() {
      position_ = index_.index_data_.UpperBound(position_->first,
          position_->second);
    }

    EdgeId first() const {
      return position_->first;
    }

    EdgeId second() const {
      return position_->second;
    }
  };

  EdgePairIterator begin() const {
    VERIFY(this->IsAttached());
    return EdgePairIterator(index_data_.begin(), *this);
  }

  EdgePairIterator end() const {
    VERIFY(this->IsAttached());
    return EdgePairIterator(index_data_.end(), *this);
  }

  //begin-end insert size supposed
  PairedInfoIndex(const Graph &g) :
      GraphActionHandler<Graph>(g, "PairedInfoIndex")
    {
  }

  virtual ~PairedInfoIndex() {
    TRACE("~PairedInfoIndex ok");
  }

public:

  void Init() {
    for (auto it = this->g().SmartEdgeBegin(); !it.IsEnd(); ++it) {
      HandleAdd(*it);
    }
  }

  /*
   * @return quantity of paired info
   */
  size_t size() const {
    return index_data_.size();
  }

  /**
   * Method returns all data about given edge
   */
  PairInfos GetEdgeInfo(EdgeId edge) const {
    VERIFY(this->IsAttached());
    return index_data_.GetEdgeInfos(edge);
  }

  /**
   * Method returns all data about distance between two given edges
   */
  PairInfos GetEdgePairInfo(EdgeId first, EdgeId second) const {
    VERIFY(this->IsAttached());
    return index_data_.GetEdgePairInfos(first, second);
  }

  virtual void HandleAdd(EdgeId e) {
    this->AddPairInfo(PairInfo<EdgeId>(e, e, 0., 0., 0.));
  }

  virtual void HandleDelete(EdgeId e) {
    this->RemoveEdgeInfo(e);
  }

  virtual void HandleMerge(const vector<EdgeId>& old_edges, EdgeId new_edge) {
    this->AddPairInfo(PairInfo<EdgeId>(new_edge, new_edge, 0., 0., 0.));
    int shift = 0;
    for (size_t i = 0; i < old_edges.size(); ++i) {
      EdgeId old_edge = old_edges[i];
      TransferInfo(old_edge, new_edge, shift);
      shift -= this->g().length(old_edge);
    }
  }

  virtual void HandleGlue(EdgeId new_edge, EdgeId edge1, EdgeId edge2) {
    TransferInfo(edge2, new_edge);
    TransferInfo(edge1, new_edge);
  }

  virtual void HandleSplit(EdgeId old_edge, EdgeId new_edge1, EdgeId new_edge2)
    {
    double prop = (double) this->g().length(new_edge1)
        / this->g().length(old_edge);
    //    size_t shift = graph_.length(new_edge1);
    TransferInfo(old_edge, new_edge1, 0, prop);
    //    PassEdge(graph_.length(new_edge1), shift);
    TransferInfo(old_edge, new_edge2, this->g().length(new_edge1),
        1 - prop);
  }

  /**
   * Method allows to add pair info to index directly instead of filling it from stream.
   */
  void AddPairInfo(const PairInfo<EdgeId>& pair_info, bool add_reversed = 1) {
    VERIFY(this->IsAttached());
    TRACE("Adding pair info to pi index: " << pair_info.first
        << " " << pair_info.second << " " << index_data_.size());

    data_iterator cluster_it = index_data_.Find(pair_info);
    if (cluster_it != index_data_.end()) {
      TRACE("Such pair info exists, merging now");
      const PairInfo<EdgeId>& existing_info = *cluster_it;
      VERIFY(existing_info == pair_info);
      MergeData(existing_info, pair_info, add_reversed);
    } else {
      TRACE("Such pair info does not exist");
      index_data_.AddPairInfo(pair_info, add_reversed);
    }
  }

    void AddAll(const PairedInfoIndex<Graph>& paired_index) {
    VERIFY(this->IsAttached());
        for (auto iter = paired_index.begin(); iter != paired_index.end(); ++iter) {
            const vector<PairInfo<EdgeId> >& infos = *iter;
            for (auto pi_iter = infos.begin(); pi_iter != infos.end(); ++pi_iter) {
                this->AddPairInfo(*pi_iter, false);
            }
        }
    }

  void RemoveEdgeInfo(EdgeId edge) {
    VERIFY(this->IsAttached());
    index_data_.DeleteEdgeInfo(edge);
  }

  void RemovePairInfo(const PairInfo<EdgeId>& pair_info) {
    VERIFY(this->IsAttached());
    index_data_.DeletePairInfo(pair_info);
  }

  void Clear() {
      index_data_.clear();
  }

private:
  PairInfoIndexData<EdgeId> index_data_;

  void MergeData(const PairInfo<EdgeId>& info_to_update, const PairInfo<EdgeId>& info_to_add,
      bool add_reversed) {
        double weight_to_add = info_to_add.weight();

        // counting new bounds in the case, when we are merging pair infos with non-zero var
        double left_bound = min(info_to_update.d() - info_to_update.var,
                info_to_add.d() - info_to_add.var);
        double right_bound = max(info_to_update.d() + info_to_update.var,
                info_to_add.d() + info_to_add.var);
        double new_dist = (left_bound + right_bound) * 0.5;
        double new_weight = info_to_update.weight() + weight_to_add;
        double new_variance = (right_bound - left_bound) * 0.5;

        index_data_.UpdateInfo(info_to_update, new_dist, new_weight, new_variance, add_reversed);
  }

  //  void OutputEdgeData(EdgeId edge1, EdgeId edge2, ostream &os = cout) {
  //    PairInfos vec = GetEdgePairInfo(edge1, edge2);
  //    if (vec.size() != 0) {
  //      os << edge1 << " " << this->g().length(edge1) << " " << edge2 << " "
  //          << this->g().length(edge2) << endl;
  //      if (this->g().EdgeEnd(edge1) == this->g().EdgeStart(edge2))
  //        os << "+" << endl;
  //      if (this->g().EdgeEnd(edge2) == this->g().EdgeStart(edge1))
  //        os << "-" << endl;
  //      int min = INT_MIN;
  //      for (size_t i = 0; i < vec.size(); i++) {
  //        int next = -1;
  //        for (size_t j = 0; j < vec.size(); j++) {
  //          if (vec[j].d() > min
  //              && (next == -1 || vec[next].d() > vec[j].d())) {
  //            next = j;
  //          }
  //        }
  //        os << vec[next].d() << " " << vec[next].weight() << endl;
  //        if (next == -1) {
  //          VERIFY(false);
  //        }
  //        if (vec[next].d() > 100000) {
  //          VERIFY(false);
  //        }
  //        min = vec[next].d();
  //      }
  //    }
  //  }

  void TransferInfo(EdgeId old_edge, EdgeId new_edge, int shift = 0,
      double weight_scale = 1.0) {
    PairInfos pair_infos = GetEdgeInfo(old_edge);
    for (size_t j = 0; j < pair_infos.size(); ++j) {
      PairInfo<EdgeId> old_pair_info = pair_infos[j];
      if (old_edge != old_pair_info.second) {
        AddPairInfo(
            PairInfo<EdgeId>(new_edge, old_pair_info.second,
                old_pair_info.d() - shift,
                weight_scale * old_pair_info.weight(),
                old_pair_info.var));
      } else if (!math::eq(old_pair_info.d(), 0.)) {
        AddPairInfo(
            PairInfo<EdgeId>(new_edge, new_edge, old_pair_info.d(),
                weight_scale * 0.5 * old_pair_info.weight(),
                old_pair_info.var));
      } else {
        AddPairInfo(
            PairInfo<EdgeId>(new_edge, new_edge, old_pair_info.d(),
                weight_scale * old_pair_info.weight(),
                old_pair_info.var));
      }
    }
  }

  size_t CorrectLength(const Path<EdgeId>& path, size_t index) const {
    size_t result = this->g().length(path[index]);
    if (index == 0) {
      result -= path.start_pos();
    }
    if (index == path.size() - 1) {
      result -= this->g().length(path[index]) - path.end_pos();
    }
    return result;
  }

    DECL_LOGGER("PairedInfoIndex");
};

template<class Graph, class SequenceMapper, class PairedStream>
class PairedIndexFiller {
private:
  typedef typename Graph::EdgeId EdgeId;

  const Graph &graph_;

  const SequenceMapper& mapper_;

  std::vector< PairedStream* > streams_;

  size_t CorrectLength(Path<EdgeId> path, size_t idx) {
    size_t answer = graph_.length(path[idx]);
    if (idx == 0)
      answer -= path.start_pos();
    if (idx == path.size() - 1)
      answer -= graph_.length(path[idx]) - path.end_pos();
    return answer;
  }

  template<class PairedRead>
  void ProcessPairedRead(omnigraph::PairedInfoIndex<Graph> &paired_index,
      const PairedRead& p_r) {
    Sequence read1 = p_r.first().sequence();
    Sequence read2 = p_r.second().sequence();
    Path<EdgeId> path1 = mapper_.MapSequence(read1);
    Path<EdgeId> path2 = mapper_.MapSequence(read2);
    size_t distance = p_r.distance();
    int current_distance1 = distance + path1.start_pos()
        - path2.start_pos();
    for (size_t i = 0; i < path1.size(); ++i) {
      int current_distance2 = current_distance1;
      for (size_t j = 0; j < path2.size(); ++j) {
        double weight = CorrectLength(path1, i)
            * CorrectLength(path2, j);
        PairInfo<EdgeId> new_info(path1[i], path2[j], current_distance2,
            weight, 0.);
        paired_index.AddPairInfo(new_info);
        current_distance2 += graph_.length(path2[j]);
      }
      current_distance1 -= graph_.length(path1[i]);
    }
  }

    /**
     * Method reads paired data from stream, maps it to genome and stores it in this PairInfoIndex.
     */
    void FillUsualIndex(omnigraph::PairedInfoIndex<Graph> &paired_index) {
        for (auto it = graph_.SmartEdgeBegin(); !it.IsEnd(); ++it) {
            paired_index.AddPairInfo(PairInfo<EdgeId>(*it, *it, 0., 0., 0.));
        }

        INFO("Processing paired reads (takes a while)");

        PairedStream& stream = *(streams_.front());
        stream.reset();
        size_t n = 0;
        while (!stream.eof()) {
            typename PairedStream::read_type p_r;
            stream >> p_r;
            ProcessPairedRead(paired_index, p_r);
            VERBOSE_POWER(++n, " paired reads processed");
        }
    }


    void FillParallelIndex(omnigraph::PairedInfoIndex<Graph> &paired_index) {
        for (auto it = graph_.SmartEdgeBegin(); !it.IsEnd(); ++it) {
            paired_index.AddPairInfo(PairInfo<EdgeId>(*it, *it, 0, 0.0, 0.));
        }

        INFO("Processing paired reads (takes a while)");

        size_t nthreads = streams_.size();
        std::vector< omnigraph::PairedInfoIndex<Graph>* > buffer_pi(nthreads);
        buffer_pi[0] = &paired_index;

        for (size_t i = 1; i < nthreads; ++i) {
            buffer_pi[i] = new omnigraph::PairedInfoIndex<Graph>(graph_, paired_index.GetMaxDifference());
        }

        size_t counter = 0;
        #pragma omp parallel num_threads(nthreads)
        {
            #pragma omp for reduction(+ : counter)
            for (size_t i = 0; i < nthreads; ++i) {

                typename PairedStream::read_type r;
                PairedStream& stream = *streams_[i];
                stream.reset();

                while (!stream.eof()) {
                    stream >> r;
                    ++counter;

                    ProcessPairedRead(*buffer_pi[i], r);
                }
            }
        }

        INFO("Used " << counter << " paired reads");

        INFO("Merging paired indices");
        for (size_t i = 1; i < nthreads; ++i) {
            buffer_pi[0]->AddAll(*(buffer_pi[i]));
            delete buffer_pi[i];
        }

    }

public:

  PairedIndexFiller(const Graph &graph, const SequenceMapper& mapper,
          PairedStream& stream) :
      graph_(graph), mapper_(mapper), streams_() {

      streams_.push_back(&stream);
  }

    PairedIndexFiller(const Graph &graph, const SequenceMapper& mapper,
            std::vector<PairedStream*>& streams) :
            graph_(graph), mapper_(mapper), streams_(streams.begin(), streams.end()) {
    }

    void FillIndex(omnigraph::PairedInfoIndex<Graph> &paired_index) {
        if (streams_.size() == 1) {
            FillUsualIndex(paired_index);
        } else {
            FillParallelIndex(paired_index);
        }
    }


private:
  DECL_LOGGER("PairedIndexFiller");

};

//New metric weight normalizer
template<class Graph>
class PairedInfoWeightNormalizer {
  typedef typename Graph::EdgeId EdgeId;
  const Graph& g_;
  const size_t insert_size_;
  //todo use this param!
  const double is_var_;
  const size_t read_length_;
  const size_t k_;
  const double avg_coverage_;
public:

  //Delta better to be around 5-10% of insert size
  PairedInfoWeightNormalizer(const Graph& g, size_t insert_size,
      double is_var, size_t read_length, size_t k, double avg_coverage) :
      g_(g), insert_size_(insert_size), is_var_(is_var), read_length_(
          read_length), k_(k), avg_coverage_(avg_coverage) {
  }

  const PairInfo<EdgeId> NormalizeWeightWithCoverage(const PairInfo<EdgeId>& pair_info) {
      PairInfo<EdgeId> new_info = pair_info;
      new_info.weight() *= g_.length(pair_info.first) * g_.length(pair_info.second) * 1.
                        / (g_.coverage(pair_info.first) * g_.coverage(pair_info.second));
      return new_info;
  }

  const Point NormalizeWeight(EdgeId e1, EdgeId e2, Point point) const {
    double w = 0.;
    if (math::eq(point.d, 0.) && e1 == e2) {
      w = 0. + g_.length(e1) - insert_size_ + 2. * read_length_ + 1. - k_;
    }
    else {
      if (math::ls(point.d, 0.)) {
        using std::swap;
        swap(e1, e2);
      }
      int gap_len = abs(rounded_d(point)) - g_.length(e1);
      int right = std::min(insert_size_, gap_len + g_.length(e2) + read_length_);
      int left = std::max(gap_len, int(insert_size_) - int(read_length_) - int(g_.length(e1)));
      w = 0. + right - left + 1 - k_;
    }

    double result_weight = point.weight;
    if (math::gr(w, /*-10.*/0.)) {
      result_weight /= w; //(w + 10);
    } else
      result_weight = 0.;

    double cov_norm_coeff = avg_coverage_ / (2. * (read_length_ - k_));
    result_weight /= cov_norm_coeff;

    Point result(point);
    result.weight = result_weight;
    return result;
  }
};

template<class Graph>
class JumpingNormalizerFunction {
private:
  typedef typename Graph::EdgeId EdgeId;
  const Graph& graph_;
  size_t read_length_;
  size_t max_norm_;

public:
  JumpingNormalizerFunction(const Graph& graph, size_t read_length,
      size_t max_norm) :
      graph_(graph), read_length_(read_length), max_norm_(max_norm) {
  }

  size_t norm(EdgeId e1, EdgeId e2) const {
    return std::min(std::min(graph_.length(e1), graph_.length(e2)),
        max_norm_) + read_length_ - graph_.k();
  }

  const Point operator()(EdgeId e1, EdgeId e2, Point point) const {
    return Point(point.d,
                 point.weight * 1. / norm(e1, e2),
                 point.var);
  }
};

template<class Graph>
const Point TrivialWeightNormalization(typename Graph::EdgeId,
                                       typename Graph::EdgeId,
                                       Point point)
{
  return point;
}

template<class Graph>
class PairedInfoNormalizer {
  typedef typename Graph::EdgeId EdgeId;
  typedef set<Point> Histogram;

 public:
  typedef boost::function<const Point(EdgeId, EdgeId, Point)> WeightNormalizer;

  PairedInfoNormalizer(WeightNormalizer normalizing_function) :
      normalizing_function_(normalizing_function)
  {
  }

// temporary due to path_extend absolute thresholds
  void FillNormalizedIndex(const PairedInfoIndexT<Graph>& paired_index,
                                 PairedInfoIndexT<Graph>& normalized_index,
                                 double coeff = 1.) const
  {
    for (auto I = paired_index.begin(), E = paired_index.end(); I != E; ++I) {
      const Histogram& hist = *I;
      EdgeId e1 = I.first();
      EdgeId e2 = I.second();
      TRACE("first second " << e1 << " " << e2);
      for (auto it2 = hist.begin(); it2 != hist.end(); ++it2) {
        Point tmp(*it2);
        TRACE("TEMP point " << tmp);
        tmp.weight *= coeff;
        TRACE("Normalized pair info " << tmp << " " << normalizing_function_(e1, e2, tmp));
        normalized_index.AddPairInfo(e1, e2, normalizing_function_(e1, e2, tmp), false);
      }
    }
  }

 private:
  WeightNormalizer normalizing_function_;
  DECL_LOGGER("PairedInfoNormalizer");
};

}
