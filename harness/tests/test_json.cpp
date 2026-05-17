#include "catch.hpp"
#include "nt_jsonstream.h"
#include <distingnt/serialisation.h>
#include <cstring>

TEST_CASE("serialise roundtrip: simple object", "[json]") {
    auto stream = nt::make_json_stream();
    stream->openObject();
    stream->addMemberName("answer");
    stream->addNumber(42);
    stream->addMemberName("name");
    stream->addString("banana");
    stream->closeObject();

    auto parse = nt::make_json_parse(stream->buffer());
    int n_members = 0;
    REQUIRE(parse->numberOfObjectMembers(n_members));
    REQUIRE(n_members == 2);
    REQUIRE(parse->matchName("answer"));
    int v = 0;
    REQUIRE(parse->number(v));
    REQUIRE(v == 42);
    REQUIRE(parse->matchName("name"));
    const char* s = nullptr;
    REQUIRE(parse->string(s));
    REQUIRE(strcmp(s, "banana") == 0);
}

TEST_CASE("serialise roundtrip: array of mixed primitives", "[json]") {
    auto stream = nt::make_json_stream();
    stream->openObject();
    stream->addMemberName("test_array");
    stream->openArray();
    stream->addNumber(1);
    stream->addString("banana");
    stream->addFourCC(('b' << 24) | ('e' << 16) | ('e' << 8) | 'f');
    stream->addBoolean(true);
    stream->addNull();
    stream->closeArray();
    stream->closeObject();

    auto parse = nt::make_json_parse(stream->buffer());
    int n = 0;
    REQUIRE(parse->numberOfObjectMembers(n));
    REQUIRE(n == 1);
    REQUIRE(parse->matchName("test_array"));
    int len = 0;
    REQUIRE(parse->numberOfArrayElements(len));
    REQUIRE(len == 5);

    int iv = 0;
    REQUIRE(parse->number(iv));
    REQUIRE(iv == 1);

    const char* s = nullptr;
    REQUIRE(parse->string(s));
    REQUIRE(strcmp(s, "banana") == 0);
    REQUIRE(parse->string(s));
    REQUIRE(strcmp(s, "beef") == 0);

    bool b = false;
    REQUIRE(parse->boolean(b));
    REQUIRE(b == true);

    REQUIRE(parse->null());
}

TEST_CASE("nested object serialise/parse", "[json]") {
    auto stream = nt::make_json_stream();
    stream->openObject();
    stream->addMemberName("outer");
    stream->openObject();
    stream->addMemberName("inner");
    stream->addNumber(7);
    stream->closeObject();
    stream->closeObject();

    auto parse = nt::make_json_parse(stream->buffer());
    int n = 0;
    REQUIRE(parse->numberOfObjectMembers(n));
    REQUIRE(n == 1);
    REQUIRE(parse->matchName("outer"));
    int n_inner = 0;
    REQUIRE(parse->numberOfObjectMembers(n_inner));
    REQUIRE(n_inner == 1);
    REQUIRE(parse->matchName("inner"));
    int v = 0;
    REQUIRE(parse->number(v));
    REQUIRE(v == 7);
}

TEST_CASE("skipMember bypasses values of any shape", "[json]") {
    auto stream = nt::make_json_stream();
    stream->openObject();
    stream->addMemberName("skip_me");
    stream->openArray();
    stream->addNumber(1);
    stream->addNumber(2);
    stream->closeArray();
    stream->addMemberName("answer");
    stream->addNumber(42);
    stream->closeObject();

    auto parse = nt::make_json_parse(stream->buffer());
    int n = 0;
    REQUIRE(parse->numberOfObjectMembers(n));
    REQUIRE(n == 2);
    REQUIRE_FALSE(parse->matchName("answer"));
    REQUIRE(parse->skipMember());
    REQUIRE(parse->matchName("answer"));
    int v = 0;
    REQUIRE(parse->number(v));
    REQUIRE(v == 42);
}

TEST_CASE("float numbers round-trip through addNumber(float)", "[json]") {
    auto stream = nt::make_json_stream();
    stream->openObject();
    stream->addMemberName("pi");
    stream->addNumber(3.5f);
    stream->closeObject();

    auto parse = nt::make_json_parse(stream->buffer());
    int n = 0;
    REQUIRE(parse->numberOfObjectMembers(n));
    REQUIRE(parse->matchName("pi"));
    float f = 0.0f;
    REQUIRE(parse->number(f));
    REQUIRE(f == Catch::Approx(3.5f));
}
