#ifndef QQ_MEM_ENGINE_H
#define QQ_MEM_ENGINE_H

#include <assert.h>

#include "doc_length_store.h"
#include "intersect.h"
#include "utils.h"
#include "doc_store.h"
#include "highlighter.h"
#include "engine_loader.h"
#include "posting_list_delta.h"
#include "general_config.h"


class DocInfo {
 public:
  DocInfo(std::string body, std::string tokens, std::string token_offsets, 
          std::string token_positions) 
    :body_(body), tokens_(tokens), token_offsets_(token_offsets), 
     token_positions_(token_positions) {}

  TermList GetTokens() const {
    return utils::explode(tokens_, ' ');
  }

  // return a table of offset pairs
  // Each row is for a term
  std::vector<OffsetPairs> GetOffsetPairsVec() const {
    return utils::parse_offsets(token_offsets_);
  }

  // return a table of positions
  // Each row is for a term
  std::vector<Positions> GetPositions() const {
    std::vector<std::string> groups = utils::explode(token_positions_, '.');
    
    std::vector<Positions> table(groups.size());

    for (int i = 0; i < groups.size(); i++) {
      // group: 1;3;8
      std::vector<std::string> pos_strs = utils::explode(groups[i], ';');
      for (auto & pos_str : pos_strs) {
        std::cout << "pos_str: " << pos_str << std::endl;
        table[i].push_back(std::stoi(pos_str));
      }
    }

    return table;
  }

 private:
  const std::string body_;
  const std::string tokens_;
  const std::string token_offsets_;
  const std::string token_positions_;
};


class InvertedIndexQqMemVec: public InvertedIndexService {
 private:
  typedef PostingListStandardVec PostingListType;
  typedef std::unordered_map<Term, PostingListType> IndexStore;
  IndexStore index_;

 public:
  typedef IndexStore::const_iterator const_iterator;
  typedef std::vector<const PostingListType*> PlPointers;

  PlPointers FindPostinglists(const TermList &terms) const {
    PlPointers postinglist_pointers;

    for (auto term : terms) {
      auto it = index_.find(term);
      if (it == index_.end()) {
        break;
      }

      postinglist_pointers.push_back(&it->second);
    }

    return postinglist_pointers;
  }

  IteratorPointers FindIterators(const TermList &terms) const {
    IteratorPointers it_pointers;
    PlPointers pl_pointers = FindPostinglists(terms);
    for (auto &pl : pl_pointers) {
      it_pointers.push_back(std::move(pl->Begin()));
    }

    return it_pointers;
  }

  std::map<std::string, int> PostinglistSizes(const TermList &terms) const {
    std::map<std::string, int> ret;

    auto pointers = FindPostinglists(terms);
    for (auto p : pointers) {
      ret[p->GetTerm()] = p->Size();
    }

    return ret;
  }

  void AddDocument(const int doc_id, const DocInfo doc_info) {
    TermList token_vec = doc_info.GetTokens();
    std::vector<Offsets> offsets_parsed = doc_info.GetOffsetPairsVec();
    
    assert(token_vec.size() == offsets_parsed.size());

    for (int i = 0; i < token_vec.size(); i++) {
      IndexStore::iterator it = index_.find(token_vec[i]);

      if (it == index_.cend()) {
        // term does not exist
        std::pair<IndexStore::iterator, bool> ret;
        ret = index_.insert( std::make_pair(token_vec[i], PostingListType(token_vec[i])) );
        it = ret.first;
      } 
      
      PostingListType &postinglist = it->second;
      postinglist.AddPosting(
          StandardPosting(doc_id, offsets_parsed[i].size(), offsets_parsed[i]));
    }
  }

  void AddDocument(const int &doc_id, const std::string &body, 
      const std::string &tokens) {
    utils::CountMapType token_counts = utils::count_tokens(tokens);
    std::map<Term, OffsetPairs> term_offsets = utils::extract_offset_pairs(tokens);

    for (auto token_it = token_counts.begin(); token_it != token_counts.end(); 
        token_it++) {
      IndexStore::iterator it;
      auto term = token_it->first;
      it = index_.find(term);

      if (it == index_.cend()) {
        // term does not exist
        std::pair<IndexStore::iterator, bool> ret;
        ret = index_.insert( std::make_pair(term, PostingListType(term)) );
        it = ret.first;
      } 

      PostingListType &postinglist = it->second;        
      postinglist.AddPosting(
          StandardPosting(doc_id, token_it->second, term_offsets[term]));
    }
  }

  void AddDocument(const int &doc_id, const std::string &body, 
      const std::string &tokens, const std::string &token_offsets) {
    TermList token_vec = utils::explode(tokens, ' ');
    std::vector<Offsets> offsets_parsed = utils::parse_offsets(token_offsets);
    
    assert(token_vec.size() == offsets_parsed.size());

    //std::cout << "Add document " << doc_id << ": " << token_vec.size() << " : ";
    for (int i = 0; i < token_vec.size(); i++) {
      IndexStore::iterator it = index_.find(token_vec[i]);

      if (it == index_.cend()) {
        // term does not exist
        std::pair<IndexStore::iterator, bool> ret;
        ret = index_.insert( std::make_pair(token_vec[i], PostingListType(token_vec[i])) );
        it = ret.first;
      } 
      
      PostingListType &postinglist = it->second;
      //std::cout << i << ", " << token_vec[i];
      postinglist.AddPosting(
          StandardPosting(doc_id, offsets_parsed[i].size(), offsets_parsed[i]));
      //std::cout << ";" ;
    }
  }

  int Size() const {
    return index_.size();
  }
};


class InvertedIndexQqMemDelta: public InvertedIndexService {
 private:
  typedef PostingListDelta PostingListType;
  typedef std::unordered_map<Term, PostingListType> IndexStore;
  IndexStore index_;

 public:
  typedef IndexStore::const_iterator const_iterator;
  typedef std::vector<const PostingListType*> PlPointers;

  IteratorPointers FindIterators(const TermList &terms) const {
    IteratorPointers it_pointers;
    PlPointers pl_pointers = FindPostinglists(terms);
    for (auto &pl : pl_pointers) {
      it_pointers.push_back(std::move(pl->Begin()));
    }

    return it_pointers;
  }

  PlPointers FindPostinglists(const TermList &terms) const {
    PlPointers postinglist_pointers;

    for (auto term : terms) {
      auto it = index_.find(term);
      if (it == index_.end()) {
        break;
      }

      postinglist_pointers.push_back(&it->second);
    }

    return postinglist_pointers;
  }

  std::map<std::string, int> PostinglistSizes(const TermList &terms) const {
    std::map<std::string, int> ret;

    auto pointers = FindPostinglists(terms);
    for (int i = 0; i < terms.size(); i++) {
      ret[terms[i]] = pointers[i]->Size();
    }

    return ret;
  }

  void AddDocument(const int &doc_id, const std::string &body, 
      const std::string &tokens) {
    utils::CountMapType token_counts = utils::count_tokens(tokens);
    std::map<Term, OffsetPairs> term_offsets = utils::extract_offset_pairs(tokens);

    for (auto token_it = token_counts.begin(); token_it != token_counts.end(); 
        token_it++) {
      IndexStore::iterator it;
      auto term = token_it->first;
      it = index_.find(term);

      if (it == index_.cend()) {
        // term does not exist
        std::pair<IndexStore::iterator, bool> ret;
        ret = index_.insert( std::make_pair(term, PostingListType(term)) );
        it = ret.first;
      } 

      PostingListType &postinglist = it->second;        
      postinglist.AddPosting(
          StandardPosting(doc_id, token_it->second, term_offsets[term]));
    }
  }

  void AddDocument(const int &doc_id, const std::string &body, 
      const std::string &tokens, const std::string &token_offsets) {
    TermList token_vec = utils::explode(tokens, ' ');
    std::vector<Offsets> offsets_parsed = utils::parse_offsets(token_offsets);
    
    assert(token_vec.size() == offsets_parsed.size());

    for (int i = 0; i < token_vec.size(); i++) {
      IndexStore::iterator it = index_.find(token_vec[i]);

      if (it == index_.cend()) {
        // term does not exist
        std::pair<IndexStore::iterator, bool> ret;
        ret = index_.insert( std::make_pair(token_vec[i], PostingListType(token_vec[i])) );
        it = ret.first;
      } 
      
      PostingListType &postinglist = it->second;
      postinglist.AddPosting(
          StandardPosting(doc_id, offsets_parsed[i].size(), offsets_parsed[i]));
    }
  }

  // Return number of posting lists
  int Size() const {
    return index_.size();
  }
};


class QqMemEngine : public SearchEngineServiceNew {
 public:
  QqMemEngine(GeneralConfig config) {
    if (config.HasKey("inverted_index") == false || 
        config.GetString("inverted_index") == "compressed") {
      inverted_index_.reset(new InvertedIndexQqMemDelta);
    } else if (config.GetString("inverted_index") == "uncompressed") {
      inverted_index_.reset(new InvertedIndexQqMemVec);
    } else {
      LOG(FATAL) << "inverted_index: " << config.GetString("inverted_index") 
        << " not supported" << std::endl;
    }
  }

  // colum 2 should be tokens
  int LoadLocalDocuments(const std::string &line_doc_path, 
      int n_rows, const std::string loader) {
    int ret;
    if (loader == "naive") {
      ret = engine_loader::load_body_and_tokenized_body(
         this, line_doc_path, n_rows, 2, 2);
    } else if (loader == "with-offsets") {
      ret = engine_loader::load_body_and_tokenized_body_and_token_offsets(
          this, line_doc_path, n_rows, 1, 2, 3);
    } else {
      throw std::runtime_error("Loader " + loader + " is not supported");
    }

    LOG(WARNING) << "Number of terms in inverted index: " << TermCount();
    return ret;
  }

  void AddDocument(const std::string &body, const std::string &tokenized_body) {
    AddDocumentReturnId(body, tokenized_body);
  }

  void AddDocument(const std::string &body, const std::string &tokenized_body, const std::string &token_offsets) {
    AddDocumentReturnId(body, tokenized_body, token_offsets);
  }

  DocIdType AddDocumentReturnId(const std::string &body, const std::string &tokens) {
    int doc_id = NextDocId();

    doc_store_.Add(doc_id, body);
    inverted_index_->AddDocument(doc_id, body, tokens);
    doc_lengths_.AddLength(doc_id, utils::count_terms(body));
    return doc_id;
  }

  DocIdType AddDocumentReturnId(const std::string &body, const std::string &tokens, const std::string &token_offsets) {
    int doc_id = NextDocId();

    doc_store_.Add(doc_id, body);
    inverted_index_->AddDocument(doc_id, body, tokens, token_offsets);
    doc_lengths_.AddLength(doc_id, utils::count_terms(body)); // TODO modify to count on offsets?
    return doc_id;
  }
  
  std::string GetDocument(const DocIdType &doc_id) {
    return doc_store_.Get(doc_id);
  }

  int TermCount() const {
    return inverted_index_->Size();
  }

  std::map<std::string, int> PostinglistSizes(const TermList &terms) {
    return inverted_index_->PostinglistSizes(terms);
  }

  int GetDocLength(const DocIdType &doc_id) {
    return doc_lengths_.GetLength(doc_id);
  }

  SearchResult ProcessQueryTogether(const SearchQuery &query) {
    SearchResult result;

    if (query.n_results == 0) {
      return result;
    }

    IteratorPointers iterators = inverted_index_->FindIterators(query.terms);

    if (iterators.size() == 0) {
      return result;
    }

    auto top_k = qq_search::ProcessQuery(&iterators, doc_lengths_, doc_lengths_.Size(), 
                             query.n_results);  
    for (auto & top_doc_entry : top_k) {
      SearchResultEntry result_entry;
      result_entry.doc_id = top_doc_entry.doc_id;
      result_entry.doc_score = top_doc_entry.score;

      if (query.return_snippets == true) {
        result_entry.snippet = GenerateSnippet(top_doc_entry.doc_id,
            top_doc_entry.postings, query.n_snippet_passages);
      }

      result.entries.push_back(result_entry);
    }

    return result;
  }

  std::string GenerateSnippet(const DocIdType &doc_id, 
      const std::vector<StandardPosting> &postings, 
      const int n_passages) {
    OffsetsEnums res = {};

    for (int i = 0; i < postings.size(); i++) {
      res.push_back(Offset_Iterator(*postings[i].GetOffsetPairs()));
    }

    return highlighter_.highlightOffsetsEnums(res, n_passages, doc_store_.Get(doc_id));
  }

  SearchResult Search(const SearchQuery &query) {
    return ProcessQueryTogether(query);
  }

 private:
  int next_doc_id_ = 0;
  SimpleDocStore doc_store_;
  // InvertedIndexQqMemDelta inverted_index_;
  std::unique_ptr<InvertedIndexService> inverted_index_;
  DocLengthStore doc_lengths_;
  SimpleHighlighter highlighter_;

  int NextDocId() {
    return next_doc_id_++;
  }


};



std::unique_ptr<SearchEngineServiceNew> CreateSearchEngine(
    std::string engine_type);

#endif
