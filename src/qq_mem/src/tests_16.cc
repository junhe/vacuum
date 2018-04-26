#include "catch.hpp"

#include "query_pool.h"
#include "packed_value.h"

extern "C" {
#include "bitpacking.h"
}

TEST_CASE( "Query pools", "[qpool]" ) {

  SECTION("QueryPool") {
    QueryPool pool;
    pool.Add(SearchQuery({"hello"}));
    pool.Add(SearchQuery({"world"}));
    REQUIRE(pool.Size() == 2);

    REQUIRE(pool.Next().terms == TermList{"hello"});
    REQUIRE(pool.Next().terms == TermList{"world"});
    REQUIRE(pool.Next().terms == TermList{"hello"});
    REQUIRE(pool.Next().terms == TermList{"world"});
  }

  SECTION("Query pool array") {
    QueryPoolArray pool(3);
    pool.Add(0, SearchQuery({"hello"}));
    pool.Add(1, SearchQuery({"world"}));
    pool.Add(2, SearchQuery({"!"}));
    REQUIRE(pool.Size() == 3);

    REQUIRE(pool.Next(0).terms == TermList{"hello"});
    REQUIRE(pool.Next(0).terms == TermList{"hello"});
    REQUIRE(pool.Next(1).terms == TermList{"world"});
    REQUIRE(pool.Next(1).terms == TermList{"world"});
    REQUIRE(pool.Next(2).terms == TermList{"!"});
    REQUIRE(pool.Next(2).terms == TermList{"!"});
  }

  SECTION("Query producer from realistic log") {
    auto path = "src/testdata/query_log_with_phrases";
    QueryProducerByLog producer(path, 2);

    auto q = producer.NextNativeQuery(0);
    REQUIRE(q.terms == TermList{"greek", "armi"});
    REQUIRE(q.is_phrase == true);

    q = producer.NextNativeQuery(1);
    REQUIRE(q.terms == TermList{"nightt", "rain", "nashvil"});
    REQUIRE(q.is_phrase == false);
  }

  SECTION("Query producer from realistic log with lock") {
    auto path = "src/testdata/query_log_with_phrases";
    QueryProducerNoLoop producer(path);

    auto q = producer.NextNativeQuery(0);
    REQUIRE(q.terms == TermList{"greek", "armi"});
    REQUIRE(q.is_phrase == true);

    q = producer.NextNativeQuery(0);
    REQUIRE(q.terms == TermList{"nightt", "rain", "nashvil"});
    REQUIRE(q.is_phrase == false);

    REQUIRE(producer.Size() == 10);

    for (int i = 0; i < 8; i++) {
      REQUIRE(producer.IsEnd() == false);
      q = producer.NextNativeQuery(0);
    }
    REQUIRE(producer.IsEnd() == true);

  }

}


TEST_CASE( "Little packed ints", "[pack]" ) {
  SECTION("lib") {
    uint32_t original_array[128];
    uint8_t buf[128 * 4];
    uint32_t unpacked[128];

    for (int i = 0; i < 128; i++) {
      original_array[i] = i;
    }

    turbopack32(original_array, 128, 8, buf);
    turbounpack32(buf, 128, 8, unpacked);

    for (int i = 0; i < 128; i++) {
      REQUIRE(unpacked[i] == i);
    }
  }

  SECTION("Write and Read all 0") {
    LittlePackedIntsWriter writer;

    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      writer.Add(0);  
    }

    std::string data = writer.Serialize();

    LittlePackedIntsReader reader((uint8_t *)data.data());
    REQUIRE(reader.NumBits() == 1);
    reader.DecodeToCache();
    
    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      REQUIRE(reader.Get(i) == 0);
    }
  }

  SECTION("Write and Read all 1") {
    LittlePackedIntsWriter writer;

    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      writer.Add(1); 
    }

    std::string data = writer.Serialize();

    LittlePackedIntsReader reader((uint8_t *)data.data());
    REQUIRE(reader.NumBits() == 1);
    reader.DecodeToCache();
    
    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      REQUIRE(reader.Get(i) == 1);
    }
  }

  SECTION("Write and Read") {
    LittlePackedIntsWriter writer;

    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      writer.Add(i);  
    }

    std::string data = writer.Serialize();
    REQUIRE(data.size() == 112 + 2); // 2 is the size of the header

    LittlePackedIntsReader reader((uint8_t *)data.data());
    REQUIRE(reader.NumBits() == 7);
    reader.DecodeToCache();
    
    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      REQUIRE(reader.Get(i) == i);
    }
  }

  SECTION("Write and Read rand numbers") {
    LittlePackedIntsWriter writer;

    srand(0);
    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      writer.Add(rand() % 10000000); 
    }

    std::string data = writer.Serialize();

    LittlePackedIntsReader reader((uint8_t *)data.data());
    reader.DecodeToCache();
    
    srand(0);
    for (int i = 0; i < PACK_ITEM_CNT; i++) {
      REQUIRE(reader.Get(i) == rand() % 10000000);
    }
  }
}





