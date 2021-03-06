#ifndef QUERY_PROCESSING_H
#define QUERY_PROCESSING_H

#include <vector>
#include <array>
#include <cassert>
#include <unordered_map>
#include <queue>

#include <glog/logging.h>

#include "doc_length_store.h"
#include "engine_services.h"
#include "posting_list_delta.h"
#include "scoring.h"
#include "utils.h"



struct PositionInfo {
  int pos = 0;
  //                   hello readers, this is my hello world program
  // term_appearance   0                         1
  int term_appearance = 0;

  constexpr PositionInfo() {}

  void Update(const int pos_in, const int term_appr_in) {
    pos = pos_in;
    term_appearance = term_appr_in;
  }
};

typedef std::vector<PositionInfo> PositionInfoVec;
typedef std::vector<PositionInfoVec> PositionInfoTable;


class PositionInfoArray {
 public:
  PositionInfoArray() {} 
  PositionInfoArray(const int n_cols) :arr_(n_cols) {}

  void FastClear() {
    next_ = 0;
  }

  int UsedSize() const {
    return next_;
  }

  int Capacity() const {
    return arr_.size();
  }

  PositionInfoArray CompactCopy() const {
    PositionInfoArray ret(next_);
    for (std::size_t i = 0; i < next_; i++) {
      ret.Append(arr_[i].pos, arr_[i].term_appearance);
    }
    return ret;
  }

  // next_ may overflow, be very carefule
  void Append(const int pos, const int term_appearance) {
    if (next_ >= arr_.size()) {
      arr_.resize(next_ + 1);
    }
    arr_[next_].Update(pos, term_appearance);
    next_++;
  }

  const PositionInfo &operator[](const int i) const {
    return arr_[i];
  }

 private:
  std::vector<PositionInfo> arr_;
  std::size_t next_ = 0;
};


// This class is performance-critical, we do not do any checking...
// The Table cannot be resized.
class PositionInfoTable2 {
 public:
  PositionInfoTable2(const int n_rows, const int n_cols) {
    for (int i = 0; i < n_rows; i++) {
      rows_.emplace_back(n_cols);
    }
  }

  std::vector<int> RowCapacity() const {
    std::vector<int> ret;
    for (auto &row : rows_) {
      ret.push_back(row.Capacity());
    }

    return ret;
  }

  int NumOfRows() const {
    return rows_.size();
  }

  int NumUsedCols() const {
    if (rows_.size() == 0) 
      return 0;
    else 
      return rows_[0].UsedSize();
  }

  int NumUsedRows() const {
    int count = 0;
    for (auto &row : rows_) {
      if (row.UsedSize() > 0) {
        count++;
      } else {
        break;
      }
    }
    return count;
  }

  PositionInfoTable2 CompactCopy() const {
    PositionInfoTable2 ret(0, 0);

    for (auto &row : rows_) {
      if (row.UsedSize() > 0) 
        ret.rows_.push_back(row.CompactCopy());
      else
        break;
    }

    return ret;
  }

  void FastClear() {
    for (auto &row : rows_) {
      row.FastClear();
    }
  }

  void Append(const std::size_t i, const int pos, const int term_appearance) {
    if (i >= rows_.size()) {
      rows_.resize(i + 1);
    }
    rows_[i].Append(pos, term_appearance);
  }

  const PositionInfoArray &operator[](const std::size_t i) const {
    return rows_[i];
  }

 private:
  std::vector<PositionInfoArray> rows_;
};



typedef std::vector<std::shared_ptr<OffsetPairsIteratorService>> OffsetIterators;



// PosIter_T is subclass of PopIteratorService
// For example, it can be CompressedPositionIterator
//
// PosIter_T is a position iterator, it must have methods:
//  IsEnd()
//  Pop()
template <typename PosIter_T>
class PhraseQueryProcessor2 {
 public:
  PhraseQueryProcessor2(int capacity)
    :solid_iterators_(capacity), 
     last_orig_popped_(capacity), 
     pos_table_(0, 0) {
  }

  Position FindMaxAdjustedLastPopped() {
    Position max = 0;
    Position adjusted_pos;

    // original - i = adjusted
    //
    //      hello world program
    // orig:    0     1       2
    // adj:     0     0       0
    //
    // if the adjusted pos are the same, it is a phrase match
    for(int i = 0; i < n_terms_; i++) {
      adjusted_pos = last_orig_popped_[i].pos - i;
      if (adjusted_pos > max) {
        max = adjusted_pos;
      }
    }
    return max;
  }

  // Return false if any of the lists has no pos larger than or equal to
  // max_adjusted_pos
  bool MovePoppedBeyond(Position max_adjusted_pos) {
    bool ret;
    for (int i = 0; i < n_terms_; i++) {
      ret = MovePoppedBeyond(i, max_adjusted_pos);
      if (ret == false) {
        return false;
      }
    }

    return true;
  }

  bool MovePoppedBeyond(int i, Position max_adjusted_pos) {
    // the last popped will be larger than or equal to max_pos
    // If the last popped is larger than or equal to max_adjusted_pos
    auto &it = solid_iterators_[i];

    while (it.IsEnd() == false && last_orig_popped_[i].pos - i < max_adjusted_pos) {
      last_orig_popped_[i].pos = it.Pop();
      last_orig_popped_[i].term_appearance++;
    }
    
    if (it.IsEnd() == true && last_orig_popped_[i].pos - i < max_adjusted_pos) {
      return false;
    } else {
      return true;
    }
  }

  bool IsPoppedMatch(Position max_adjusted_pos) {
    Position adjusted_pos;

    for(int i = 0; i < n_terms_; i++) {
      adjusted_pos = last_orig_popped_[i].pos - i;
      if (adjusted_pos != max_adjusted_pos) {
        return false;
      }
    }
    return true;
  }

  bool InitializeLastPopped() {
    for (int i = 0; i < n_terms_; i++) {
      if (solid_iterators_[i].IsEnd()) {
        // one list is empty
        return false;
      }
      last_orig_popped_[i].pos = solid_iterators_[i].Pop();
      last_orig_popped_[i].term_appearance = 0;
    }
    return true;
  }

  //          --- table ---
  // term01: info_col  info_col ...
  // term02: info_col  info_col ...
  // term03: info_col  info_col ...
  // ...
  void AppendPositionCol(PositionInfoTable2 *table) {
    for (int i = 0; i < n_terms_; i++) {
      PositionInfo &info = last_orig_popped_[i];
      table->Append(i, info.pos, info.term_appearance);
    }
  }

  void Process() {
    if (n_terms_ == 2) {
      ProcessTwoTerm();
    } else {
      ProcessGeneral();
    }
  }

  int NumOfMatches() const {
    return pos_table_.NumUsedCols();
  }

  PositionInfoTable2 Table() const {
    return pos_table_.CompactCopy();
  }

  void ProcessTwoTerm() {
    pos_table_.FastClear();

    auto *it0 = &solid_iterators_[0]; 
    auto *it1 = &solid_iterators_[1];
    int pos0, pos1;
    int apr0 = -1, apr1 = -1;

    pos0 = -100;
    pos1 = -200;

    // loop inv: 
    //   iterator points to first unpoped item
    //   pos is the last poped (it - 1). Pos is adjusted
    //   everything before pos has been checked
    //   apr is the appearance of the iterator
    bool tried_pop_end = false;
    while(tried_pop_end == false) {
      if (pos0 < pos1) {
        if (it0->IsEnd() == false) {
          pos0 = it0->Pop();  
          apr0++;
        } else {
          tried_pop_end = true;
        }
      } else if (pos0 > pos1) {
        if (it1->IsEnd() == false) { // add test for it0
          pos1 = it1->Pop() - 1;  
          apr1++;
        } else {
          tried_pop_end = true;
        }
      } else {
        pos_table_.Append(0, pos0, apr0);
        pos_table_.Append(1, pos1 + 1, apr1);

        if (it0->IsEnd() == false) {
          pos0 = it0->Pop();  
          apr0++;
        } else {
          tried_pop_end = true;
        }

        if (it1->IsEnd() == false) {
          pos1 = it1->Pop() - 1;  
          apr1++;
        } else {
          tried_pop_end = true;
        }
      }
    }
  }

  void ProcessGeneral() {
    bool any_list_exhausted = false;
    pos_table_.FastClear();

    if (InitializeLastPopped() == false) {
      return;
    }

    while (any_list_exhausted == false) {
      Position max_adjusted_pos = FindMaxAdjustedLastPopped(); 
      bool found = MovePoppedBeyond(max_adjusted_pos);

      if (found == false) {
        any_list_exhausted = true;
        continue;
      } 

      bool match = IsPoppedMatch(max_adjusted_pos);        
      if (match == true) {
        AppendPositionCol(&pos_table_);

        bool found = MovePoppedBeyond(max_adjusted_pos + 1);
        if (found == false) {
          any_list_exhausted = true;
        }
      }
    }
  }

  PosIter_T *Iterator(int i) {
    return &solid_iterators_[i];
  }

  std::vector<PosIter_T> *Iterators() {
    return &solid_iterators_;
  }

  void SetNumTerms(int n) {
    n_terms_ = n;
  }

 private:
  int n_terms_;
  std::vector<PosIter_T> solid_iterators_;
  std::vector<PositionInfo> last_orig_popped_;
  PositionInfoTable2 pos_table_;
  bool list_exhausted_ = false;
};


// PLIter_T is PostingListDeltaIterator for MemEngine
// It must have:
template <typename PLIter_T>
struct ResultDocEntry {
  DocIdType doc_id;
  qq_float score;
  
  std::vector<PLIter_T> pl_iterators;
  // table[0]: row of position info structs for the first term
  // table[1]: row of position info structs for the second term
  PositionInfoTable2 position_table;
  // offset_iters[0]: offset iterator for the first term
  // offset_iters[1]: offset iterator for the second term
  // ...
  OffsetIterators offset_iters;
  bool is_phrase = false;

  ResultDocEntry(const DocIdType &doc_id_in, const qq_float &score_in)
    :doc_id(doc_id_in), score(score_in), position_table(0, 0) {}

  ResultDocEntry(const DocIdType &doc_id_in, 
                 const qq_float &score_in, 
                 IteratorPointers &pl_iters,
                 const PositionInfoTable2 position_table_in, 
                 const bool is_phrase_in)
    :doc_id(doc_id_in), 
     score(score_in), 
     position_table(position_table_in), 
     is_phrase(is_phrase_in) {

    for (auto &p : pl_iters) {
      PLIter_T *pp = 
        dynamic_cast<PLIter_T *>(p.get());
      LOG_IF(FATAL, p == nullptr) << "Cannot convert";

      pl_iterators.push_back(*pp);
    }
  }

  ResultDocEntry(const DocIdType &doc_id_in, 
                 const qq_float &score_in, 
                 const OffsetIterators offset_iters_in, 
                 const bool is_phrase_in)
    :doc_id(doc_id_in), 
     score(score_in), 
     position_table(0, 0),
     offset_iters(offset_iters_in),
     is_phrase(is_phrase_in)
  {}

  ResultDocEntry(const DocIdType &doc_id_in, 
                 const qq_float &score_in, 
                 const OffsetIterators offset_iters_in, 
                 const PositionInfoTable2 position_table_in, 
                 const bool is_phrase_in)
    :doc_id(doc_id_in), 
     score(score_in), 
     position_table(position_table_in), 
     offset_iters(offset_iters_in),
     is_phrase(is_phrase_in) {}

  std::vector<OffsetPairs> OffsetsForHighliting() {
    if (is_phrase == true) {
      return FilterOffsetByPosition();
    } else {
      return ExpandOffsets();
    }
  }

  std::vector<OffsetPairs> ExpandOffsets() {
    std::vector<OffsetPairs> table(offset_iters.size());
    for (std::size_t i = 0; i < offset_iters.size(); i++) {
      auto &it = offset_iters[i];
      while (it->IsEnd() == false) {
        OffsetPair pair;
        it->Pop(&pair);
        table[i].push_back(pair);
      }
    }
    return table;
  }

  // Make sure you have valid offset and positions in this object
  // before calling this function
  std::vector<OffsetPairs> FilterOffsetByPosition() {
    std::vector<OffsetPairs> offset_table(position_table.NumUsedRows());

    for (int row_i = 0; row_i < position_table.NumUsedRows(); row_i++) {
      auto offset_iter = offset_iters[row_i];
      const auto &row = position_table[row_i];

      int offset_cur = -1;
      for (int col_i = 0; col_i < row.UsedSize(); col_i++) {
        OffsetPair pair;
        while (offset_cur < row[col_i].term_appearance) {
          // assert(offset_iter->IsEnd() == false);
          LOG_IF(FATAL, offset_iter->IsEnd()) 
            << "offset_iter does not suppose to reach the end." << std::endl;
          offset_iter->Pop(&pair);
          offset_cur++;
        }

        offset_table[row_i].push_back(pair);
      }
    }

    return offset_table;
  }

  friend bool operator<(const ResultDocEntry<PLIter_T> &a, const ResultDocEntry<PLIter_T> &b)
  {
    return a.score < b.score;
  }

  friend bool operator>(const ResultDocEntry<PLIter_T> &a, const ResultDocEntry<PLIter_T> &b)
  {
    return a.score > b.score;
  }

  std::string ToStr() {
    return std::to_string(doc_id) + " (" + std::to_string(score) + ")";
  }
};


template <typename PLIter_T>
class EntryGreater {
 public:
  bool operator() (const std::unique_ptr<ResultDocEntry<PLIter_T>> &a, 
                   const std::unique_ptr<ResultDocEntry<PLIter_T>> &b) {
    return a->score > b->score;
  }
};


template <typename PLIter_T>
using MinPointerHeap = std::priority_queue<
          std::unique_ptr<ResultDocEntry<PLIter_T>>, 
          std::vector<std::unique_ptr<ResultDocEntry<PLIter_T>>>, 
          EntryGreater<PLIter_T>>;


template <typename PLIter_T>
class ProcessorBase {
 public:
  ProcessorBase(
    const Bm25Similarity &similarity,
    std::vector<PLIter_T> *pl_iterators, 
    const DocLengthCharStore &doc_lengths,
    const int n_total_docs_in_index,
    const int k)
   : similarity_(similarity),
     doc_lengths_(doc_lengths),
     pl_iterators_(*pl_iterators),
     n_lists_(pl_iterators->size()),
     k_(k),
     idfs_of_terms_(pl_iterators->size()),
     n_total_docs_in_index_(n_total_docs_in_index)
  {
    for (std::size_t i = 0; i < pl_iterators_.size(); i++) {
      idfs_of_terms_[i] = calc_es_idf(n_total_docs_in_index_, 
                                      pl_iterators_[i].Size());
    }
  }

 protected:
  std::vector<ResultDocEntry<PLIter_T>> SortHeap() {
    std::vector<ResultDocEntry<PLIter_T>> ret;

    int kk = k_;
    while(!min_heap_.empty() && kk != 0) {
      ret.push_back(*min_heap_.top().get());
      min_heap_.pop();
      kk--;
    }
    std::reverse(ret.begin(), ret.end());
    return ret;
  }

  const Bm25Similarity &similarity_;
  const DocLengthCharStore &doc_lengths_;
  std::vector<PLIter_T> &pl_iterators_;
  const int n_lists_;
  const int k_;
  std::vector<qq_float> idfs_of_terms_;
  const int n_total_docs_in_index_;
  MinPointerHeap<PLIter_T> min_heap_;
};


template <typename PLIter_T>
class NonPhraseProcessorBase: public ProcessorBase<PLIter_T> {
 public:
  NonPhraseProcessorBase(
    const Bm25Similarity &similarity,
    std::vector<PLIter_T> *pl_iterators, 
    const DocLengthCharStore &doc_lengths,
    const int n_total_docs_in_index,
    const int k)
   :ProcessorBase<PLIter_T>(similarity,            pl_iterators, doc_lengths, 
                  n_total_docs_in_index, k) {}

 protected:
  void RankDoc(const DocIdType &max_doc_id) {
    qq_float score_of_this_doc = CalcDocScore<PLIter_T>(
        this->pl_iterators_,
        this->idfs_of_terms_,
        this->doc_lengths_.GetLength(max_doc_id),
        this->similarity_);

    if (this->min_heap_.size() < (std::size_t) this->k_) {
      InsertToHeap(max_doc_id, score_of_this_doc);
    } else {
      if (score_of_this_doc > this->min_heap_.top()->score) {
        this->min_heap_.pop();
        InsertToHeap(max_doc_id, score_of_this_doc);
      }
    }
  }

  void InsertToHeap(const DocIdType &doc_id, 
                    const qq_float &score_of_this_doc)
  {
    OffsetIterators offset_iters;
    for (int i = 0; i < this->n_lists_; i++) {
      auto p = this->pl_iterators_[i].OffsetPairsBegin();
      offset_iters.push_back(std::move(p));
    }

    this->min_heap_.emplace(new ResultDocEntry<PLIter_T>(doc_id, score_of_this_doc, offset_iters, 
        false));
  }
};


template <typename PLIter_T>
class SingleTermQueryProcessor :public NonPhraseProcessorBase<PLIter_T> {
 public:
  SingleTermQueryProcessor(
    const Bm25Similarity &similarity,
    std::vector<PLIter_T> *pl_iterators, 
    const DocLengthCharStore &doc_lengths,
    const int n_total_docs_in_index,
    const int k = 5)
   :NonPhraseProcessorBase<PLIter_T>(similarity,            pl_iterators, doc_lengths, 
                           n_total_docs_in_index, k) {}

  std::vector<ResultDocEntry<PLIter_T>> Process() {
    auto &it = this->pl_iterators_[0];

    while (it.IsEnd() == false) {
      this->RankDoc(it.DocId());
      it.Advance();
    }

    return this->SortHeap();
  }
};

template <typename PLIter_T>
class TwoTermNonPhraseQueryProcessor: public NonPhraseProcessorBase<PLIter_T> {
 public:
  TwoTermNonPhraseQueryProcessor(
    const Bm25Similarity &similarity,
    std::vector<PLIter_T> *pl_iterators, 
    const DocLengthCharStore &doc_lengths,
    const int n_total_docs_in_index,
    const int k = 5)
   :NonPhraseProcessorBase<PLIter_T>(similarity,            pl_iterators, doc_lengths, 
                           n_total_docs_in_index, k) {}

  std::vector<ResultDocEntry<PLIter_T>> Process() {
    auto &it_0 = this->pl_iterators_[0];
    auto &it_1 = this->pl_iterators_[1];
    DocIdType doc0, doc1;

    while (it_0.IsEnd() == false && it_1.IsEnd() == false) {
      doc0 = it_0.DocId();
      doc1 = it_1.DocId();
      if (doc0 > doc1) {
        it_1.SkipForward(doc0);
      } else if (doc0 < doc1) {
        it_0.SkipForward(doc1);
      } else {
        this->RankDoc(doc0); 

        it_0.Advance();
        it_1.Advance();
      }
    }

    return this->SortHeap();
  }
};


template <typename PLIter_T, typename PosIter_T>
class QueryProcessor: public ProcessorBase<PLIter_T> {
 public:
  QueryProcessor(
    const Bm25Similarity &similarity,
    std::vector<PLIter_T> *pl_iterators,
    const DocLengthCharStore &doc_lengths,
    const int n_total_docs_in_index,
    const int k = 5,
    const bool is_phrase = false,
    const int bloom_enable_factor = 1
    )
   :ProcessorBase<PLIter_T>(similarity,            pl_iterators, doc_lengths, 
                  n_total_docs_in_index, k),
    phrase_qp_(8),
    is_phrase_(is_phrase),
    bloom_enable_factor_(bloom_enable_factor)
  {}

  std::vector<ResultDocEntry<PLIter_T>> Process() {
    if (this->pl_iterators_.size() == 1) {
      return ProcessSingleTerm();
    } else if (this->pl_iterators_.size() == 2) {
      return ProcessTwoTerm();
    } else {
      return ProcessMultipleTerms();
    }
  }

  std::vector<ResultDocEntry<PLIter_T>> ProcessMultipleTerms() {
    bool finished = false;
    DocIdType max_doc_id;

    // loop inv: all intersections before iterators have been found
    while (finished == false) {
      // find max doc id
      max_doc_id = -1;
      finished = FindMax(&max_doc_id);

      if (finished == true) {
        break;
      }

      finished = FindMatch(max_doc_id);
    } // while

    return this->SortHeap();
  }

  std::vector<ResultDocEntry<PLIter_T>> ProcessSingleTerm() {
    auto &it = this->pl_iterators_[0];

    while (it.IsEnd() == false) {
      DocIdType doc_id = it.DocId();
      RankDocNonPhrase(doc_id);
      it.Advance();
    }

    return this->SortHeap();
  }

  std::vector<ResultDocEntry<PLIter_T>> ProcessTwoTerm() {
    auto &it_0 = this->pl_iterators_[0];
    auto &it_1 = this->pl_iterators_[1];
    DocIdType doc0, doc1;

    while (it_0.IsEnd() == false && it_1.IsEnd() == false) {
      doc0 = it_0.DocId();
      doc1 = it_1.DocId();
      if (doc0 > doc1) {
        it_1.SkipForward(doc0);
      } else if (doc0 < doc1) {
        it_0.SkipForward(doc1);
      } else {
        HandleTheFoundDoc(doc0); 

        it_0.Advance();
        it_1.Advance();
      }
    }

    return this->SortHeap();
  }

 private:
  bool CheckByFirstPostingList() {
    if (this->pl_iterators_[0].HasNextTerm(
          this->pl_iterators_[1].Term()) == BLM_NOT_PRESENT) {
      return false;
    } else {
      return true;
    }
  }

  bool CheckBySecondPostingList() {
    if (this->pl_iterators_[1].HasPriorTerm(
          this->pl_iterators_[0].Term()) == BLM_NOT_PRESENT) {
      return false;
    } else {
      return true;
    }
  }

  bool CheckBloomFallBack() {
    // Can be further improve by starting with the smallest posting list
    for (std::size_t i = 0; i < this->pl_iterators_.size() - 1; i++) {
      if (this->pl_iterators_[i].HasNextTerm(
            this->pl_iterators_[i + 1].Term()) == BLM_NOT_PRESENT)
      {
        return false;
      }
    }
    return true;
  }

  bool CheckBloomWithEnableFactor() {
    const std::size_t size1 = this->pl_iterators_[0].Size();
    const std::size_t size2 = this->pl_iterators_[1].Size();

    if (bloom_enable_factor_ * size1 <= size2) {
      return CheckByFirstPostingList();
    } else if (bloom_enable_factor_ * size2 < size1) {
      return CheckBySecondPostingList();
    } else {
      return true;
    }
  }

  // return true: end reached
  bool FindMax(DocIdType * max_doc_id) {
    for (int list_i = 0; list_i < this->n_lists_; list_i++) {
      auto &it = this->pl_iterators_[list_i];

      if (it.IsEnd()) {
        return true;
      }

      const DocIdType cur_doc_id = it.DocId(); 
      if (cur_doc_id > *max_doc_id) {
        *max_doc_id = cur_doc_id; 
      }
    }
    return false;
  }

  // return true: end reached
  bool FindMatch(DocIdType max_doc_id) {
    // Try to reach max_doc_id in all posting lists_
    int list_i;
    for (list_i = 0; list_i < this->n_lists_; list_i++) {
      auto &it = this->pl_iterators_[list_i];

      it.SkipForward(max_doc_id);
      if (it.IsEnd()) {
        return true;
      }

      if (it.DocId() != max_doc_id) {
        break;
      }

      if (list_i == this->n_lists_ - 1) {
        // HandleTheFoundDoc(max_doc_id);
        HandleTheFoundDoc(max_doc_id);
        // Advance iterators
        for (int i = 0; i < this->n_lists_; i++) {
          this->pl_iterators_[i].Advance();
        }
      }
    }
    return false;
  }

  int FindPhrase() {
    if (IsPossibleToPresent() == false)
      return 0;

    for (std::size_t i = 0; i < this->pl_iterators_.size(); i++) {
      PosIter_T *p = phrase_qp_.Iterator(i);
      this->pl_iterators_[i].AssignPositionBegin(p);
    }
    phrase_qp_.SetNumTerms(this->pl_iterators_.size());

    phrase_qp_.Process();

    return phrase_qp_.NumOfMatches();
  }

  // Return false if it is impossible to have a match
  // Return true if:
  //  1. possible to match
  //  2. bloom filter is disabled
  bool IsPossibleToPresent() {
    if (bloom_enable_factor_ == BLOOM_NEVER_USE) {
      return true;
    }

    if (this->pl_iterators_.size() != 2) 
    {
      return CheckBloomFallBack();
    }

    return CheckBloomWithEnableFactor();
  }

  void HandleTheFoundDoc(const DocIdType &max_doc_id) {
    if (this->is_phrase_ == true && this->pl_iterators_.size() > 1 ) {
      int n_matches = FindPhrase();
      if (n_matches > 0) {
        RankDocForPhrase(max_doc_id);
      }
    } else {
      RankDocNonPhrase(max_doc_id);
    }
  }

  void RankDocForPhrase(const DocIdType &max_doc_id) {
    qq_float score_of_this_doc = CalcDocScore<PLIter_T>(
        this->pl_iterators_,
        this->idfs_of_terms_,
        this->doc_lengths_.GetLength(max_doc_id),
        this->similarity_);

    if (this->min_heap_.size() < (std::size_t) this->k_) {
      InsertToHeap(max_doc_id, score_of_this_doc, phrase_qp_.Table());
    } else {
      if (score_of_this_doc > this->min_heap_.top()->score) {
        this->min_heap_.pop();
        InsertToHeap(max_doc_id, score_of_this_doc, phrase_qp_.Table());
      }
    }
  }

  void RankDocNonPhrase(const DocIdType &max_doc_id) {
    qq_float score_of_this_doc = CalcDocScore<PLIter_T>(
        this->pl_iterators_,
        this->idfs_of_terms_,
        this->doc_lengths_.GetLength(max_doc_id),
        this->similarity_);

    if (this->min_heap_.size() < (std::size_t)this->k_) {
      PositionInfoTable2 position_table(0, 0);
      InsertToHeap(max_doc_id, score_of_this_doc, position_table);
    } else {
      if (score_of_this_doc > this->min_heap_.top()->score) {
        this->min_heap_.pop();

        PositionInfoTable2 position_table(0, 0);
        InsertToHeap(max_doc_id, score_of_this_doc, position_table);
      }
    }
  }

  void InsertToHeap(const DocIdType &doc_id, 
                    const qq_float &score_of_this_doc,
                    const PositionInfoTable2 &position_table)
  {
    OffsetIterators offset_iters;
    for (int i = 0; i < this->n_lists_; i++) {
      auto p = this->pl_iterators_[i].OffsetPairsBegin();
      offset_iters.push_back(std::move(p));
    }
    this->min_heap_.emplace(new ResultDocEntry<PLIter_T>(doc_id, score_of_this_doc, 
			offset_iters, position_table, this->is_phrase_));
  }

  PhraseQueryProcessor2<PosIter_T> phrase_qp_;
  bool is_phrase_;
  int bloom_enable_factor_;
};



namespace qq_search {

template <typename PLIter_T, typename PosIter_T>
std::vector<ResultDocEntry<PLIter_T>> ProcessQueryDelta(
     const Bm25Similarity &similarity,
     std::vector<PLIter_T> *pl_iterators, 
     const DocLengthCharStore &doc_lengths,
     const int n_total_docs_in_index,
     const int k,
     const bool is_phase,
     const int bloom_enable_factor = 1
     ) {
  if (pl_iterators->size() == 1) {
    SingleTermQueryProcessor<PLIter_T> qp(similarity, pl_iterators, doc_lengths, 
        n_total_docs_in_index, k);
    return qp.Process();
  } else if (pl_iterators->size() == 2 && is_phase == false) {
    TwoTermNonPhraseQueryProcessor<PLIter_T> qp(similarity, pl_iterators, doc_lengths, 
        n_total_docs_in_index, k);
    return qp.Process();
  } else {
    QueryProcessor<PLIter_T, PosIter_T> qp(similarity, pl_iterators, doc_lengths, 
        n_total_docs_in_index, k, is_phase, bloom_enable_factor);
    return qp.Process();
  }
}

}


#endif
