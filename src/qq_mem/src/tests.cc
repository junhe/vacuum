// #define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include <sys/time.h>
#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/lambda/lambda.hpp>    
#include "catch.hpp"
#include <glog/logging.h>

#include "posting_message.pb.h"

#include "doc_store.h"
#include "utils.h"
#include "grpc_server_impl.h"
#include "qq_mem_engine.h"

#include "posting_list_vec.h"

#include "query_processing.h"
#include "scoring.h"

#include "highlighter.h"

// cereal
#include <cereal/archives/binary.hpp>


unsigned int Factorial( unsigned int number ) {
    return number <= 1 ? number : Factorial(number-1)*number;
}

TEST_CASE( "glog can print", "[glog]" ) {
  LOG(ERROR) << "Found " << 4 << " cookies in GLOG";
}


TEST_CASE( "Factorials are computed", "[factorial]" ) {
    REQUIRE( Factorial(1) == 1 );
    REQUIRE( Factorial(2) == 2 );
    REQUIRE( Factorial(3) == 6 );
    REQUIRE( Factorial(10) == 3628800 );
}


TEST_CASE( "Compressed Doc Store", "[docstore]" ) {
  SECTION("Regular") {
    CompressedDocStore store;
    int doc_id = 88;
    std::string doc = "it is a doc";

    REQUIRE(store.Has(doc_id) == false);

    store.Add(doc_id, doc);
    REQUIRE(store.Size() == 1);
    REQUIRE(store.Get(doc_id) == doc);

    store.Add(89, "doc89");
    REQUIRE(store.Size() == 2);

    store.Remove(89);
    REQUIRE(store.Size() == 1);

    REQUIRE(store.Has(doc_id) == true);

    store.Clear();
    REQUIRE(store.Has(doc_id) == false);
  }

  SECTION("Empty doc") {
    CompressedDocStore store;
    int doc_id = 88;
    std::string doc = "";
    REQUIRE(store.Has(doc_id) == false);

    store.Add(doc_id, doc);
    REQUIRE(store.Get(doc_id) == doc);
  }
}


TEST_CASE( "Utilities", "[utils]" ) {
    SECTION("Leading space and Two spaces") {
        std::vector<std::string> vec = utils::explode(" hello  world", ' ');
        REQUIRE(vec.size() == 2);
        REQUIRE(vec[0] == "hello");
        REQUIRE(vec[1] == "world");
    }

    SECTION("Empty string") {
        std::vector<std::string> vec = utils::explode("", ' ');
        REQUIRE(vec.size() == 0);
    }

    SECTION("All spaces") {
        std::vector<std::string> vec = utils::explode("   ", ' ');
        REQUIRE(vec.size() == 0);
    }

    SECTION("Explode by some commone spaces") {
        std::vector<std::string> vec = utils::explode_by_non_letter(" hello\t world yeah");
        REQUIRE(vec.size() == 3);
        REQUIRE(vec[0] == "hello");
        REQUIRE(vec[1] == "world");
        REQUIRE(vec[2] == "yeah");
    }
}

TEST_CASE("Strict spliting", "[utils]") {
    SECTION("Regular case") {
        std::vector<std::string> vec = utils::explode_strict("a\tb", '\t');
        REQUIRE(vec.size() == 2);
        REQUIRE(vec[0] == "a");
        REQUIRE(vec[1] == "b");
    }

    SECTION("Separator at beginning and end of line") {
        std::vector<std::string> vec = utils::explode_strict("\ta\tb\t", '\t');

        REQUIRE(vec.size() == 4);
        REQUIRE(vec[0] == "");
        REQUIRE(vec[1] == "a");
        REQUIRE(vec[2] == "b");
        REQUIRE(vec[3] == "");
    }
}

TEST_CASE("Filling zeros", "[utils]") {
    REQUIRE(utils::fill_zeros("abc", 5) == "00abc");
    REQUIRE(utils::fill_zeros("abc", 0) == "abc");
    REQUIRE(utils::fill_zeros("abc", 3) == "abc");
    REQUIRE(utils::fill_zeros("", 3) == "000");
    REQUIRE(utils::fill_zeros("", 0) == "");
}

TEST_CASE( "LineDoc", "[line_doc]" ) {
    SECTION("Small size") {
        utils::LineDoc linedoc("src/testdata/tokenized_wiki_abstract_line_doc");
        std::vector<std::string> items;

        auto ret = linedoc.GetRow(items); 
        REQUIRE(ret == true);
        REQUIRE(items[0] == "col1");
        REQUIRE(items[1] == "col2");
        REQUIRE(items[2] == "col3");
        // for (auto s : items) {
            // std::cout << "-------------------" << std::endl;
            // std::cout << s << std::endl;
        // }
        
        ret = linedoc.GetRow(items);
        REQUIRE(ret == true);
        REQUIRE(items[0] == "Wikipedia: Anarchism");
        REQUIRE(items[1] == "Anarchism is a political philosophy that advocates self-governed societies based on voluntary institutions. These are often described as stateless societies,\"ANARCHISM, a social philosophy that rejects authoritarian government and maintains that voluntary institutions are best suited to express man's natural social tendencies.");
        REQUIRE(items[2] == "anarch polit philosophi advoc self govern societi base voluntari institut often describ stateless social reject authoritarian maintain best suit express man natur tendenc ");

        ret = linedoc.GetRow(items);
        REQUIRE(ret == false);
    }

    SECTION("Large size") {
        utils::LineDoc linedoc("src/testdata/test_doc_tokenized");
        std::vector<std::string> items;

        while (true) {
            std::vector<std::string> items;
            bool has_it = linedoc.GetRow(items);
            if (has_it) {
                REQUIRE(items.size() == 3);    
            } else {
                break;
            }
        }
    }
}

TEST_CASE( "boost library is usable", "[boost]" ) {
    using namespace boost::filesystem;

    path p{"./somefile-not-exist"};
    try
    {
        file_status s = status(p);
        REQUIRE(is_directory(s) == false);
        REQUIRE(exists(s) == false);
    }
    catch (filesystem_error &e)
    {
        std::cerr << e.what() << '\n';
    }
}


TEST_CASE( "Offsets Parser essential operations are OK", "[offsets_parser]" ) {
    std::string offsets = "1,2;.3,4;5,6;.7,8;.";
    std::vector<Offsets> offset_parsed = utils::parse_offsets(offsets);
    REQUIRE(offset_parsed.size() == 3);
    REQUIRE(offset_parsed[0].size() == 1);
    REQUIRE(offset_parsed[1].size() == 2);
    REQUIRE(offset_parsed[2].size() == 1);
    REQUIRE(offset_parsed[0][0]  == std::make_tuple(1,2));
    REQUIRE(offset_parsed[1][0]  == std::make_tuple(3,4));
    REQUIRE(offset_parsed[1][1]  == std::make_tuple(5,6));
    REQUIRE(offset_parsed[2][0]  == std::make_tuple(7,8));
}

TEST_CASE( "Passage of Unified Highlighter essential operations are OK", "[passage_unified_highlighter]" ) {
    // construct a passage
    Passage test_passage;
    test_passage.startoffset = 0;
    test_passage.endoffset = 10;

    // add matches
    test_passage.addMatch(0, 4);
    test_passage.addMatch(6, 10);
    REQUIRE(test_passage.matches.size() == 2);
    REQUIRE(test_passage.matches[0] == std::make_tuple(0,4));
    REQUIRE(test_passage.matches[1] == std::make_tuple(6,10));

    // to_string
    std::string doc = "Hello world. This is Kan.";
    std::string res = test_passage.to_string(&doc);
    // check 
    REQUIRE(res == "<b>Hello<\\b> <b>world<\\b>\n");

    // reset
    test_passage.reset();
    REQUIRE(test_passage.matches.size() == 0);
}

TEST_CASE( "SentenceBreakIterator essential operations are OK", "[sentence_breakiterator]" ) {

    // init 
    std::string test_string = "hello Wisconsin, This is Kan.  Im happy.";
    int breakpoint[2] = {30, 39};
    SentenceBreakIterator breakiterator(test_string, create_locale());

    // iterate on a string
    int i = 0;
    while ( breakiterator.next() > 0 ) {
        int start_offset = breakiterator.getStartOffset();
        int end_offset = breakiterator.getEndOffset();
        REQUIRE(end_offset == breakpoint[i++]);
    }

    // test offset-based next
    REQUIRE(i == 2);
    breakiterator.next(3);
    REQUIRE(breakiterator.getEndOffset() == 30);
    breakiterator.next(33);
    REQUIRE(breakiterator.getEndOffset() == 39);
    REQUIRE(breakiterator.next(40) == 0);
}

TEST_CASE("String to char *, back to string works", "[String_To_Char*]") {
    std::string test_str= "hello world";
    char * buffer;
    int ret = posix_memalign((void **)&buffer, PAGE_SIZE, PAGE_SIZE);
    
    std::copy(test_str.begin(), test_str.end(), buffer);
    buffer[test_str.size()] = '\0';

    std::string str(buffer);
    REQUIRE(test_str.compare(str) == 0 );
}

TEST_CASE( "Vector-based posting list works fine", "[posting_list]" ) {
  SECTION("New postings can be added and retrieved") {
    PostingList_Vec<PostingSimple> pl("hello");   

    REQUIRE(pl.Size() == 0);
    pl.AddPosting(PostingSimple(10, 88, Positions{28}));
    REQUIRE(pl.Size() == 1);
    REQUIRE(pl.GetPosting(0).GetDocId() == 10);
    REQUIRE(pl.GetPosting(0).GetTermFreq() == 88);
    REQUIRE(pl.GetPosting(0).GetPositions() == Positions{28});

    pl.AddPosting(PostingSimple(11, 889, Positions{28, 230}));
    REQUIRE(pl.Size() == 2);
    REQUIRE(pl.GetPosting(1).GetDocId() == 11);
    REQUIRE(pl.GetPosting(1).GetTermFreq() == 889);
    REQUIRE(pl.GetPosting(1).GetPositions() == Positions{28, 230});
  }


  SECTION("Skipping works") {
    PostingList_Vec<PostingSimple> pl("hello");   
    for (int i = 0; i < 100; i++) {
      pl.AddPosting(PostingSimple(i, 1, Positions{28}));
    }
    REQUIRE(pl.Size() == 100);

    SECTION("It can stay at starting it") {
      PostingList_Vec<PostingSimple>::iterator_t it;
      it = pl.SkipForward(0, 0);
      REQUIRE(it == 0);

      it = pl.SkipForward(8, 8);
      REQUIRE(it == 8);
    }

    SECTION("It can skip multiple postings") {
      PostingList_Vec<PostingSimple>::iterator_t it;
      it = pl.SkipForward(0, 8);
      REQUIRE(it == 8);

      it = pl.SkipForward(0, 99);
      REQUIRE(it == 99);
    }

    SECTION("It returns pl.Size() when we cannot find doc id") {
      PostingList_Vec<PostingSimple>::iterator_t it;
      it = pl.SkipForward(0, 1000);
      REQUIRE(it == pl.Size());
    }
  }

}


TEST_CASE( "DocLengthStore", "[ranking]" ) {
  SECTION("It can add some lengths.") {
    DocLengthStore store;
    store.AddLength(1, 7);
    store.AddLength(2, 8);
    store.AddLength(3, 9);

    REQUIRE(store.Size() == 3);
    REQUIRE(store.GetAvgLength() == 8);
  }
}


TEST_CASE( "Old scoring", "[ranking]" ) {
  SECTION("TF is correct") {
    REQUIRE(calc_tf(1) == 1.0); // sample in ES document
    REQUIRE(calc_tf(4) == 2.0);
    REQUIRE(calc_tf(100) == 10.0);

    REQUIRE(utils::format_double(calc_tf(2), 3) == "1.41");
  }

  SECTION("IDF is correct (sample score in ES document)") {
    REQUIRE(utils::format_double(calc_idf(1, 1), 3) == "0.307");
  }

  SECTION("Field length norm is correct") {
    REQUIRE(calc_field_len_norm(4) == 0.5);
  }
}


TEST_CASE( "Utilities work", "[utils]" ) {
  SECTION("Count terms") {
    REQUIRE(utils::count_terms("hello world") == 2);
  }

  SECTION("Count tokens") {
    utils::CountMapType counts = utils::count_tokens("hello hello you");
    assert(counts["hello"] == 2);
    assert(counts["you"] == 1);
    assert(counts.size() == 2);
  }
}


void setup_inverted_index(InvertedIndexService &inverted_index) {
  inverted_index.AddDocument(0, DocInfo("hello world", "hello world", "", "", "TOKEN_ONLY"));
  inverted_index.AddDocument(1, DocInfo("hello wisconsin", "hello wisconsin", "", "", "TOKEN_ONLY"));
  REQUIRE(inverted_index.Size() == 3);
}

void test_inverted_index(InvertedIndexService &inverted_index) {
    auto iterators = inverted_index.FindIterators(TermList{"hello"});
    auto it = iterators[0].get();
    {
      REQUIRE(it->DocId() == 0);
      auto pairs_it = it->OffsetPairsBegin();
      OffsetPair pair;
      pairs_it->Pop(&pair);
      REQUIRE(pair == std::make_tuple(0, 4)); // in doc 0
    }

    auto ret = it->IsEnd();
    it->Advance();
    REQUIRE(ret == false);
    {
      REQUIRE(it->DocId() == 1);
      auto pairs_it = it->OffsetPairsBegin();
      OffsetPair pair;
      pairs_it->Pop(&pair);
      REQUIRE(pair == std::make_tuple(0, 4)); // in doc 0
    }
}

TEST_CASE( "Inverted index", "[engine]" ) {
  SECTION("Compressed") {
    InvertedIndexQqMemDelta inverted_index;

    setup_inverted_index(inverted_index);
    test_inverted_index(inverted_index);
  }
}


std::unique_ptr<SearchEngineServiceNew> Test_GetEngine(std::string engine_type) {
  std::unique_ptr<SearchEngineServiceNew> engine = CreateSearchEngine(engine_type);

  engine->AddDocument(
      DocInfo("hello world", "hello world", "", "", "TOKEN_ONLY"));
  REQUIRE(engine->TermCount() == 2);

  engine->AddDocument(
      DocInfo("hello wisconsin", "hello wisconsin", "", "", "TOKEN_ONLY"));
  REQUIRE(engine->TermCount() == 3);

  engine->AddDocument(
      DocInfo("hello world big world", "hello world big world", "", "", "TOKEN_ONLY"));
  REQUIRE(engine->TermCount() == 4);

  return engine;
}



void test_01(SearchEngineServiceNew *engine) {
    SearchResult result = engine->Search(SearchQuery(TermList{"wisconsin"}));

    REQUIRE(result.Size() == 1);
    REQUIRE(result.entries[0].doc_id == 1);
    REQUIRE(utils::format_double(result.entries[0].doc_score, 3) == "1.09");
}

void test_02(SearchEngineServiceNew *engine) {
    SearchResult result = engine->Search(SearchQuery(TermList{"hello"}));
    REQUIRE(result.Size() == 3);

    // The score below is produced by ../tools/es_index_docs.py in this
    // same git commit
    // You can reproduce the elasticsearch score by checking out this
    // commit and run `python tools/es_index_docs.py`.
    REQUIRE(utils::format_double(result.entries[0].doc_score, 3) == "0.149");
    REQUIRE(utils::format_double(result.entries[1].doc_score, 3) == "0.149");
    REQUIRE(utils::format_double(result.entries[2].doc_score, 3) == "0.111");
}

void test_03(SearchEngineServiceNew *engine) {
    SearchResult result = engine->Search(SearchQuery(TermList{"hello", "world"}));

    REQUIRE(result.Size() == 2);
    // The scores below are produced by ../tools/es_index_docs.py in this
    // same git commit
    // You can reproduce the elasticsearch scores by checking out this
    // commit and run `python tools/es_index_docs.py`.
    REQUIRE(utils::format_double(result.entries[0].doc_score, 3) == "0.677");
    REQUIRE(utils::format_double(result.entries[1].doc_score, 3) == "0.672");
}

void test_04(SearchEngineServiceNew *engine) {
    auto result = engine->Search(SearchQuery(TermList{"hello"}, true));
    REQUIRE(result.Size() == 3);
    // We cannot test the snippets of the first entries because we do not know
    // their order (the first two entries have the same score).
    REQUIRE(result.entries[2].snippet == "<b>hello<\\b> world big world\n");
}

void test_05(SearchEngineServiceNew *engine) {
    auto result = engine->Search(SearchQuery(TermList{"hello", "world"}, true));
    REQUIRE(result.Size() == 2);

    REQUIRE(result.entries[0].snippet == "<b>hello<\\b> <b>world<\\b> big <b>world<\\b>\n");
    REQUIRE(result.entries[1].snippet == "<b>hello<\\b> <b>world<\\b>\n");
}

void test_06(SearchEngineServiceNew *engine) {
    auto query = SearchQuery(TermList{"hello", "world"}, true);
    query.n_results = 0;
    auto result = engine->Search(query);
    REQUIRE(result.Size() == 0);
}

TEST_CASE( "QQ Mem Compressed Engine works", "[engine]" ) {
  auto uniq_engine = Test_GetEngine("qq_mem_compressed");
  auto engine = uniq_engine.get();

  SECTION("The engine can serve single-term queries") {
    test_01(engine);
  }

  SECTION("The engine can serve single-term queries with multiple results") {
    test_02(engine);
  }

  SECTION("The engine can server two-term queries") {
    test_03(engine);
  }

  SECTION("It can generate snippets") {
    test_04(engine);
  }

  SECTION("It can generate snippets for two-term query") {
    test_05(engine);
  }

  SECTION("The engine behaves correct when n_results is 0") {
    test_06(engine);
  }
}


TEST_CASE( "Sorting document works", "[ranking]" ) {
  SECTION("A regular case") {
    DocScoreVec scores{{0, 0.8}, {1, 3.0}, {2, 2.1}};    
    REQUIRE(utils::find_top_k(scores, 4) == std::vector<DocIdType>{1, 2, 0});
    REQUIRE(utils::find_top_k(scores, 3) == std::vector<DocIdType>{1, 2, 0});
    REQUIRE(utils::find_top_k(scores, 2) == std::vector<DocIdType>{1, 2});
    REQUIRE(utils::find_top_k(scores, 1) == std::vector<DocIdType>{1});
    REQUIRE(utils::find_top_k(scores, 0) == std::vector<DocIdType>{});
  }

  SECTION("Empty scores") {
    DocScoreVec scores{};    
    REQUIRE(utils::find_top_k(scores, 4) == std::vector<DocIdType>{});
  }

  SECTION("Identical scores") {
    DocScoreVec scores{{0, 0.8}, {1, 0.8}, {2, 2.1}};    
    REQUIRE(utils::find_top_k(scores, 1) == std::vector<DocIdType>{2});
  }
}


TEST_CASE( "Class Staircase can generate staircase strings", "[utils]" ) {
  SECTION("Simple case") {
    utils::Staircase staircase(1, 1, 2);
    
    REQUIRE(staircase.MaxWidth() == 2);

    REQUIRE(staircase.NextLayer() == "0");
    REQUIRE(staircase.NextLayer() == "0 1");
    REQUIRE(staircase.NextLayer() == "");
  }

  SECTION("Simple case 2") {
    utils::Staircase staircase(2, 1, 2);

    REQUIRE(staircase.MaxWidth() == 2);

    REQUIRE(staircase.NextLayer() == "0");
    REQUIRE(staircase.NextLayer() == "0");
    REQUIRE(staircase.NextLayer() == "0 1");
    REQUIRE(staircase.NextLayer() == "0 1");
    REQUIRE(staircase.NextLayer() == "");
  }

  SECTION("Wide steps") {
    utils::Staircase staircase(2, 3, 2);

    REQUIRE(staircase.MaxWidth() == 6);

    REQUIRE(staircase.NextLayer() == "0 1 2");
    REQUIRE(staircase.NextLayer() == "0 1 2");
    REQUIRE(staircase.NextLayer() == "0 1 2 3 4 5");
    REQUIRE(staircase.NextLayer() == "0 1 2 3 4 5");
    REQUIRE(staircase.NextLayer() == "");
  }
}


