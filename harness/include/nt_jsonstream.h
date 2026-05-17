#pragma once

#include <memory>
#include <string>

#define _DISTINGNT_SERIALISATION_INTERNAL 1
#include <distingnt/serialisation.h>

namespace nt {

// Host-side concrete subclass of _NT_jsonStream that exposes the
// serialised JSON text. The firmware never exposes a buffer accessor;
// this is purely so tests can round-trip stream -> parse.
class JsonStreamHost : public _NT_jsonStream {
public:
    JsonStreamHost();
    ~JsonStreamHost();
    const std::string& buffer();
private:
    explicit JsonStreamHost(void* state);
    void* state_;  // owned WriterState* (opaque to keep impl headers private)
};

// Host-side concrete subclass of _NT_jsonParse that owns the JSON
// source text and the tokenised tree built from it.
class JsonParseHost : public _NT_jsonParse {
public:
    explicit JsonParseHost(const std::string& json);
    ~JsonParseHost();
private:
    JsonParseHost(const std::string& json, void* state);
    void* state_;  // owned ParserState*
};

std::unique_ptr<JsonStreamHost> make_json_stream();
std::unique_ptr<JsonParseHost>  make_json_parse(const std::string& json);

}  // namespace nt
