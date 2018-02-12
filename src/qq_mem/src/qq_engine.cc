#include "qq_engine.h"
#include "engine_services.h"
#include "unifiedhighlighter.h" // TODO change the API
#include "utils.h"
#include <assert.h>


FlashReader * Global_Posting_Store;   // defined in qq_engine.h
FlashReader * Global_Document_Store;  // store both documents and document passage segements 
UnifiedHighlighter tmp_highlighter;   // for scoring 

// For precomputation, insert splits into offsets(with fake offset)
void QQSearchEngine::precompute_insert_splits(const Passage_Segments & splits, Offsets & offsets_in) {
    // insert split (after split, there are offset ranges for each split)
    int num_offsets = offsets_in.size();
    if (num_offsets > 0)
        offsets_in.push_back(Offset(-1, -1));

    // calculate scores, add to final
    int start_one; 
    int end_one = -1;
    for (int i = 0; i < splits.size(); i++) {
        // calculate range for splits[i]
        start_one = end_one + 1;
        int start_offset = splits[i].first;
        int end_offset = splits[i].second + start_offset - 1;
        // find offsets in this passage
        while(std::get<0>(offsets_in[start_one]) < start_offset && start_one < num_offsets)
            start_one ++;
        if (start_one == num_offsets)  // no more possible passages contaning this term
            break;
        end_one = start_one;
        while (std::get<1>(offsets_in[end_one]) <= end_offset && end_one < num_offsets)
            end_one ++;
        end_one--;

        // calculate the score
        int len = end_one - start_one + 1;
        if (len > 0) {
            // store the score
            int passage_length = splits[i].second;
            int score = (int)(tmp_highlighter.passage_norm(splits[i].first)*tmp_highlighter.tf_norm(len, passage_length)*100000000);
            offsets_in.push_back(Offset(i, score));       // (passage ID, score)
            offsets_in.push_back(Offset(start_one, len)); // (star offset, len)
        }
    }
    return ;
}

void QQSearchEngine::AddDocument(const std::string &title, const std::string &url, 
        const std::string &body, const std::string &tokens,  const std::string &offsets) {
    int doc_id = NextDocId();
    doc_store_.Add(doc_id, body);
    
    // Tokenize the document(already pre-processed using scripts)
    // get terms
    std::vector<std::string> terms = utils::explode(tokens, ' ');
    std::vector<Offsets> offsets_parsed = utils::parse_offsets(offsets);
    // construct term with offset objects
    assert(terms.size() == offsets_parsed.size());
    TermWithOffsetList terms_with_offset = {};
    for (int i = 0; i < terms.size(); i++) {
        Offsets tmp_offsets = offsets_parsed[i];
        if (FLAG_SNIPPETS_PRECOMPUTE) {
            precompute_insert_splits(doc_store_.GetPassages(doc_id), tmp_offsets);
        }
        TermWithOffset cur_term(terms[i], tmp_offsets);
        terms_with_offset.push_back(cur_term);
    }
    // add document
    inverted_index_.AddDocument(doc_id, terms_with_offset);
}

// The parameters here will certainly change. Do not rely too much on this.
void QQSearchEngine::AddDocument(const std::string &title, const std::string &url, 
        const std::string &body) {
    int doc_id = NextDocId();
    doc_store_.Add(doc_id, body);
    // Tokenize the document(already pre-processed using scripts)
    std::vector<std::string> terms = utils::explode(body, ' ');
    
    inverted_index_.AddDocument(doc_id, terms);
}

const std::string & QQSearchEngine::GetDocument(const int &doc_id) {
    return doc_store_.Get(doc_id);
}

int QQSearchEngine::NextDocId() {
    return next_doc_id_++;
}


std::vector<int> QQSearchEngine::Search(const TermList &terms, const SearchOperator &op) {
    return inverted_index_.Search(terms, op); 
}

const Passage_Segments & QQSearchEngine::GetDocumentPassages(const int &doc_id) {
    return doc_store_.GetPassages(doc_id);
}


// Return the number of document indexed
int QQSearchEngine::LoadLocalDocuments(const std::string &line_doc_path, int n_rows) {
  utils::LineDoc linedoc(line_doc_path);
  std::vector<std::string> items;
  bool has_it;

  int count = 0;
  for (int i = 0; i < n_rows; i++) {
    has_it = linedoc.GetRow(items);
    if (has_it) {
      AddDocument(items[0], "http://wiki", items[2]);
      count++;
    } else {
      break;
    }

    if (count % 10000 == 0) {
      std::cout << "Indexed " << count << " documents" << std::endl;
    }
  }

  return count;
}

// This function is currently for benchmarking
// It returns an iterator without parsing the posting list
InvertedIndex::const_iterator QQSearchEngine::Find(const Term &term) {
  auto it = inverted_index_.Find(term);
  return it;
}
