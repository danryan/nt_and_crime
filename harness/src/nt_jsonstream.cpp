#include "nt_jsonstream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace nt {
namespace {

// ----------------------------------------------------------------------------
// Writer state
// ----------------------------------------------------------------------------

enum class CtxKind { TopLevel, Object, Array };

struct WriterCtx {
    CtxKind kind;
    bool first;          // true until first member/element emitted
    bool name_pending;   // object only: true after addMemberName, awaiting value
};

struct WriterState {
    std::string out;
    std::vector<WriterCtx> stack;
    bool auto_root_opened = false;

    WriterState() {
        stack.push_back({CtxKind::TopLevel, true, false});
    }

    void before_value() {
        WriterCtx& top = stack.back();
        if (top.kind == CtxKind::Array) {
            if (!top.first) out += ',';
            top.first = false;
        } else if (top.kind == CtxKind::Object) {
            // value follows addMemberName: comma/colon already handled
            top.name_pending = false;
        }
    }
};

void writer_quote(std::string& out, const char* s) {
    out += '"';
    for (const char* p = s; *p; ++p) {
        char c = *p;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

WriterState* writer_from(void* rc) {
    return reinterpret_cast<WriterState*>(rc);
}

// ----------------------------------------------------------------------------
// Parser tokens / tree
// ----------------------------------------------------------------------------

enum class NodeKind { Object, Array, Number, String, Boolean, Null };

struct Node {
    NodeKind kind;
    // For Object: pairs of (name_storage_index, value_node_index).
    // For Array: child node indices.
    std::vector<int>          children;
    std::vector<std::string>  names;     // object only, parallel to every-other child or used as map
    // For Number: holds both representations; the parser preserves text.
    int    int_val   = 0;
    float  float_val = 0.0f;
    bool   is_int    = true;
    bool   bool_val  = false;
    std::string str_val;  // String node only; also reused via lifetime of ParserState
};

struct Frame {
    int  node_idx;   // container node
    int  cursor;     // next child to consume
};

struct ParserState {
    std::string         source;     // owned copy of JSON text
    std::vector<Node>   nodes;      // arena
    int                 root = -1;
    std::vector<Frame>  stack;
};

ParserState* parser_from(void* rc) {
    return reinterpret_cast<ParserState*>(rc);
}

// ----------------------------------------------------------------------------
// Tokeniser / recursive-descent parser
// ----------------------------------------------------------------------------

struct Lexer {
    const std::string& s;
    size_t pos = 0;
    explicit Lexer(const std::string& src) : s(src) {}

    void skip_ws() { while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos; }

    char peek() { skip_ws(); return pos < s.size() ? s[pos] : '\0'; }

    bool match(char c) { skip_ws(); if (pos < s.size() && s[pos] == c) { ++pos; return true; } return false; }

    bool parse_string(std::string& out) {
        skip_ws();
        if (pos >= s.size() || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < s.size() && s[pos] != '"') {
            char c = s[pos++];
            if (c == '\\' && pos < s.size()) {
                char e = s[pos++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (pos + 4 > s.size()) return false;
                        unsigned cp = (unsigned)std::strtoul(s.substr(pos, 4).c_str(), nullptr, 16);
                        pos += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xc0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3f));
                        } else {
                            out += (char)(0xe0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3f));
                            out += (char)(0x80 | (cp & 0x3f));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out += c;
            }
        }
        if (pos >= s.size()) return false;
        ++pos;  // consume closing "
        return true;
    }
};

int parse_value(Lexer& L, ParserState& P);

int parse_object(Lexer& L, ParserState& P) {
    int idx = (int)P.nodes.size();
    P.nodes.push_back(Node{});
    P.nodes[idx].kind = NodeKind::Object;
    if (!L.match('{')) return -1;
    if (L.match('}')) return idx;
    for (;;) {
        std::string name;
        if (!L.parse_string(name)) return -1;
        if (!L.match(':')) return -1;
        int v = parse_value(L, P);
        if (v < 0) return -1;
        P.nodes[idx].names.push_back(name);
        P.nodes[idx].children.push_back(v);
        if (L.match(',')) continue;
        if (L.match('}')) break;
        return -1;
    }
    return idx;
}

int parse_array(Lexer& L, ParserState& P) {
    int idx = (int)P.nodes.size();
    P.nodes.push_back(Node{});
    P.nodes[idx].kind = NodeKind::Array;
    if (!L.match('[')) return -1;
    if (L.match(']')) return idx;
    for (;;) {
        int v = parse_value(L, P);
        if (v < 0) return -1;
        P.nodes[idx].children.push_back(v);
        if (L.match(',')) continue;
        if (L.match(']')) break;
        return -1;
    }
    return idx;
}

int parse_value(Lexer& L, ParserState& P) {
    char c = L.peek();
    if (c == '{') return parse_object(L, P);
    if (c == '[') return parse_array(L, P);
    if (c == '"') {
        int idx = (int)P.nodes.size();
        P.nodes.push_back(Node{});
        P.nodes[idx].kind = NodeKind::String;
        if (!L.parse_string(P.nodes[idx].str_val)) return -1;
        return idx;
    }
    if (c == 't' || c == 'f') {
        bool want_true = (c == 't');
        const char* lit = want_true ? "true" : "false";
        size_t n = std::strlen(lit);
        if (L.s.compare(L.pos, n, lit) != 0) return -1;
        L.pos += n;
        int idx = (int)P.nodes.size();
        P.nodes.push_back(Node{});
        P.nodes[idx].kind = NodeKind::Boolean;
        P.nodes[idx].bool_val = want_true;
        return idx;
    }
    if (c == 'n') {
        if (L.s.compare(L.pos, 4, "null") != 0) return -1;
        L.pos += 4;
        int idx = (int)P.nodes.size();
        P.nodes.push_back(Node{});
        P.nodes[idx].kind = NodeKind::Null;
        return idx;
    }
    // number
    L.skip_ws();
    size_t start = L.pos;
    if (L.pos < L.s.size() && (L.s[L.pos] == '-' || L.s[L.pos] == '+')) ++L.pos;
    bool has_digit = false, has_dot = false, has_exp = false;
    while (L.pos < L.s.size()) {
        char ch = L.s[L.pos];
        if (std::isdigit((unsigned char)ch)) { has_digit = true; ++L.pos; }
        else if (ch == '.' && !has_dot)      { has_dot = true; ++L.pos; }
        else if ((ch == 'e' || ch == 'E') && !has_exp) {
            has_exp = true; ++L.pos;
            if (L.pos < L.s.size() && (L.s[L.pos] == '+' || L.s[L.pos] == '-')) ++L.pos;
        }
        else break;
    }
    if (!has_digit) return -1;
    std::string tok = L.s.substr(start, L.pos - start);
    int idx = (int)P.nodes.size();
    P.nodes.push_back(Node{});
    P.nodes[idx].kind = NodeKind::Number;
    if (has_dot || has_exp) {
        P.nodes[idx].is_int = false;
        P.nodes[idx].float_val = (float)std::strtod(tok.c_str(), nullptr);
        P.nodes[idx].int_val = (int)P.nodes[idx].float_val;
    } else {
        P.nodes[idx].is_int = true;
        P.nodes[idx].int_val = (int)std::strtol(tok.c_str(), nullptr, 10);
        P.nodes[idx].float_val = (float)P.nodes[idx].int_val;
    }
    return idx;
}

// ----------------------------------------------------------------------------
// Cursor helpers
// ----------------------------------------------------------------------------

// Return index of the "current value" the cursor is positioned at without advancing.
// For object frames the value is children[cursor]; same for array frames.
int peek_value(ParserState& P) {
    if (P.stack.empty()) return -1;
    Frame& f = P.stack.back();
    const Node& c = P.nodes[f.node_idx];
    if (f.cursor >= (int)c.children.size()) return -1;
    return c.children[f.cursor];
}

void advance(ParserState& P) {
    if (P.stack.empty()) return;
    Frame& f = P.stack.back();
    ++f.cursor;
    while (!P.stack.empty()) {
        Frame& tf = P.stack.back();
        const Node& c = P.nodes[tf.node_idx];
        if (tf.cursor < (int)c.children.size()) break;
        P.stack.pop_back();
    }
}

}  // namespace

}  // namespace nt

// ----------------------------------------------------------------------------
// _NT_jsonStream method definitions (the upstream class is non-virtual; we
// supply its concrete behaviour here for host builds).
// ----------------------------------------------------------------------------

_NT_jsonStream::_NT_jsonStream(void* rc) : refCon(rc) {}
_NT_jsonStream::~_NT_jsonStream() {}

void _NT_jsonStream::openArray() {
    auto* w = nt::writer_from(refCon);
    if (!w->stack.empty() && w->stack.back().kind == nt::CtxKind::Array) {
        if (!w->stack.back().first) w->out += ',';
        w->stack.back().first = false;
    }
    w->out += '[';
    w->stack.push_back({nt::CtxKind::Array, true, false});
}

void _NT_jsonStream::closeArray() {
    auto* w = nt::writer_from(refCon);
    w->out += ']';
    w->stack.pop_back();
}

void _NT_jsonStream::openObject() {
    auto* w = nt::writer_from(refCon);
    if (!w->stack.empty() && w->stack.back().kind == nt::CtxKind::Array) {
        if (!w->stack.back().first) w->out += ',';
        w->stack.back().first = false;
    }
    w->out += '{';
    w->stack.push_back({nt::CtxKind::Object, true, false});
}

void _NT_jsonStream::closeObject() {
    auto* w = nt::writer_from(refCon);
    w->out += '}';
    w->stack.pop_back();
    if (w->auto_root_opened && w->stack.size() == 1) {
        w->auto_root_opened = false;
    }
}

void _NT_jsonStream::addMemberName(const char* str) {
    auto* w = nt::writer_from(refCon);
    if (w->stack.back().kind == nt::CtxKind::TopLevel) {
        // Auto-open a root object to mirror firmware behaviour.
        openObject();
        w->auto_root_opened = true;
    }
    auto& top = w->stack.back();
    if (!top.first) w->out += ',';
    top.first = false;
    nt::writer_quote(w->out, str);
    w->out += ':';
    top.name_pending = true;
}

void _NT_jsonStream::addNumber(int v) {
    auto* w = nt::writer_from(refCon);
    w->before_value();
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d", v);
    w->out += buf;
}

void _NT_jsonStream::addNumber(float v) {
    auto* w = nt::writer_from(refCon);
    w->before_value();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", (double)v);
    w->out += buf;
}

void _NT_jsonStream::addString(const char* str) {
    auto* w = nt::writer_from(refCon);
    w->before_value();
    nt::writer_quote(w->out, str);
}

void _NT_jsonStream::addFourCC(uint32_t v) {
    char s[5] = { (char)((v >> 24) & 0xff), (char)((v >> 16) & 0xff),
                  (char)((v >> 8) & 0xff),  (char)(v & 0xff), 0 };
    addString(s);
}

void _NT_jsonStream::addBoolean(bool v) {
    auto* w = nt::writer_from(refCon);
    w->before_value();
    w->out += v ? "true" : "false";
}

void _NT_jsonStream::addNull() {
    auto* w = nt::writer_from(refCon);
    w->before_value();
    w->out += "null";
}

// ----------------------------------------------------------------------------
// _NT_jsonParse method definitions
// ----------------------------------------------------------------------------

_NT_jsonParse::_NT_jsonParse(void* rc, int idx) : refCon(rc), i(idx) {}
_NT_jsonParse::~_NT_jsonParse() {}

bool _NT_jsonParse::numberOfArrayElements(int& num) {
    auto* P = nt::parser_from(refCon);
    int v = nt::peek_value(*P);
    if (v < 0 || P->nodes[v].kind != nt::NodeKind::Array) return false;
    num = (int)P->nodes[v].children.size();
    // Sequence note: advance() pops parent frames whose cursor reached end BEFORE
    // we push the new child frame. Order matters: pushing first would put the
    // about-to-be-popped parent above our new frame and corrupt the cursor stack.
    nt::advance(*P);
    P->stack.push_back({v, 0});
    return true;
}

bool _NT_jsonParse::numberOfObjectMembers(int& num) {
    auto* P = nt::parser_from(refCon);
    int v = nt::peek_value(*P);
    if (v < 0 || P->nodes[v].kind != nt::NodeKind::Object) return false;
    num = (int)P->nodes[v].children.size();
    nt::advance(*P);
    P->stack.push_back({v, 0});
    return true;
}

bool _NT_jsonParse::matchName(const char* name) {
    auto* P = nt::parser_from(refCon);
    if (P->stack.empty()) return false;
    nt::Frame& f = P->stack.back();
    const nt::Node& c = P->nodes[f.node_idx];
    if (c.kind != nt::NodeKind::Object) return false;
    if (f.cursor >= (int)c.names.size()) return false;
    return c.names[f.cursor] == name;
}

bool _NT_jsonParse::skipMember() {
    auto* P = nt::parser_from(refCon);
    if (P->stack.empty()) return false;
    nt::Frame& f = P->stack.back();
    const nt::Node& c = P->nodes[f.node_idx];
    if (c.kind != nt::NodeKind::Object) return false;
    if (f.cursor >= (int)c.children.size()) return false;
    nt::advance(*P);
    return true;
}

bool _NT_jsonParse::number(int& v) {
    auto* P = nt::parser_from(refCon);
    int idx = nt::peek_value(*P);
    if (idx < 0 || P->nodes[idx].kind != nt::NodeKind::Number) return false;
    v = P->nodes[idx].is_int ? P->nodes[idx].int_val : (int)P->nodes[idx].float_val;
    nt::advance(*P);
    return true;
}

bool _NT_jsonParse::number(float& v) {
    auto* P = nt::parser_from(refCon);
    int idx = nt::peek_value(*P);
    if (idx < 0 || P->nodes[idx].kind != nt::NodeKind::Number) return false;
    v = P->nodes[idx].float_val;
    nt::advance(*P);
    return true;
}

bool _NT_jsonParse::string(const char*& str) {
    auto* P = nt::parser_from(refCon);
    int idx = nt::peek_value(*P);
    if (idx < 0 || P->nodes[idx].kind != nt::NodeKind::String) return false;
    str = P->nodes[idx].str_val.c_str();
    nt::advance(*P);
    return true;
}

bool _NT_jsonParse::boolean(bool& v) {
    auto* P = nt::parser_from(refCon);
    int idx = nt::peek_value(*P);
    if (idx < 0 || P->nodes[idx].kind != nt::NodeKind::Boolean) return false;
    v = P->nodes[idx].bool_val;
    nt::advance(*P);
    return true;
}

bool _NT_jsonParse::null() {
    auto* P = nt::parser_from(refCon);
    int idx = nt::peek_value(*P);
    if (idx < 0 || P->nodes[idx].kind != nt::NodeKind::Null) return false;
    nt::advance(*P);
    return true;
}

// ----------------------------------------------------------------------------
// Host factories
// ----------------------------------------------------------------------------

namespace nt {

JsonStreamHost::JsonStreamHost(void* state)
    : _NT_jsonStream(state), state_(state) {}

JsonStreamHost::JsonStreamHost() : JsonStreamHost(new WriterState()) {}

JsonStreamHost::~JsonStreamHost() {
    delete reinterpret_cast<WriterState*>(state_);
}

const std::string& JsonStreamHost::buffer() {
    auto* w = reinterpret_cast<WriterState*>(state_);
    while (w->auto_root_opened) {
        closeObject();
    }
    return w->out;
}

namespace {
ParserState* build_parser_state(const std::string& json) {
    auto* P = new ParserState();
    P->source = json;
    Lexer L(P->source);
    int r = parse_value(L, *P);
    P->root = r;
    if (r < 0) {
        // Malformed JSON: leave the cursor stack empty so all parse ops
        // return false. Tests that round-trip well-formed JSON are unaffected.
        return P;
    }
    int virtual_idx = (int)P->nodes.size();
    P->nodes.push_back(Node{});
    P->nodes[virtual_idx].kind = NodeKind::Array;
    P->nodes[virtual_idx].children.push_back(r);
    P->stack.push_back({virtual_idx, 0});
    return P;
}
}  // namespace

JsonParseHost::JsonParseHost(const std::string& json, void* state)
    : _NT_jsonParse(state, 0), state_(state) {}

JsonParseHost::JsonParseHost(const std::string& json)
    : JsonParseHost(json, build_parser_state(json)) {}

JsonParseHost::~JsonParseHost() {
    delete reinterpret_cast<ParserState*>(state_);
}

std::unique_ptr<JsonStreamHost> make_json_stream() {
    return std::unique_ptr<JsonStreamHost>(new JsonStreamHost());
}

std::unique_ptr<JsonParseHost> make_json_parse(const std::string& json) {
    return std::unique_ptr<JsonParseHost>(new JsonParseHost(json));
}

}  // namespace nt
