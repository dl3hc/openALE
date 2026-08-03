/**
 * \file apps/bridge/minijson.h
 * \brief Minimal JSON value type + parser/serializer — header-only.
 *
 * Scoped to what the ale_bridge wire protocol needs: flat and nested
 * objects/arrays of null/bool/number/string. Not a general-purpose,
 * spec-exhaustive JSON library (no \uXXXX surrogate pairs, no big-number
 * precision handling) — both ends of the protocol are ours to design, so
 * "good enough for our own messages" is the actual requirement.
 *
 * Parser is defensive (never throws across the WS recv thread boundary):
 * malformed input degrades to Value::null() / partially-built values rather
 * than crashing; callers check has()/is_null() rather than assuming success.
 */
#pragma once

#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace bridge {
namespace minijson {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}

    static Value null()                  { return Value(); }
    static Value boolean(bool b)         { Value v; v.type_ = Type::Bool;   v.bool_ = b; return v; }
    static Value number(double n)        { Value v; v.type_ = Type::Number; v.num_ = n; return v; }
    static Value string(std::string s)   { Value v; v.type_ = Type::String; v.str_ = std::move(s); return v; }
    static Value array()                 { Value v; v.type_ = Type::Array; return v; }
    static Value object()                { Value v; v.type_ = Type::Object; return v; }

    Type type() const     { return type_; }
    bool is_null() const  { return type_ == Type::Null; }
    bool is_object() const{ return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }

    bool   as_bool(bool def = false) const     { return type_ == Type::Bool   ? bool_ : def; }
    double as_number(double def = 0) const     { return type_ == Type::Number ? num_  : def; }
    std::string as_string(const std::string& def = "") const { return type_ == Type::String ? str_ : def; }

    // ── Object access ───────────────────────────────────────────────────
    void set(const std::string& key, Value v) {
        for (auto& kv : obj_) if (kv.first == key) { kv.second = std::move(v); return; }
        obj_.emplace_back(key, std::move(v));
    }
    const Value* find(const std::string& key) const {
        for (auto& kv : obj_) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool has(const std::string& key) const { return find(key) != nullptr; }
    std::string get_string(const std::string& key, const std::string& def = "") const {
        const Value* v = find(key);
        return v ? v->as_string(def) : def;
    }
    double get_number(const std::string& key, double def = 0) const {
        const Value* v = find(key);
        return v ? v->as_number(def) : def;
    }
    bool get_bool(const std::string& key, bool def = false) const {
        const Value* v = find(key);
        return v ? v->as_bool(def) : def;
    }
    const std::vector<std::pair<std::string, Value>>& object_items() const { return obj_; }

    // ── Array access ────────────────────────────────────────────────────
    void push_back(Value v) { arr_.push_back(std::move(v)); }
    const std::vector<Value>& items() const { return arr_; }
    std::vector<std::string> as_string_array() const {
        std::vector<std::string> out;
        for (const auto& v : arr_) if (v.type_ == Type::String) out.push_back(v.str_);
        return out;
    }

private:
    Type type_;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

// ── Builder convenience ──────────────────────────────────────────────────
inline Value obj() { return Value::object(); }
inline Value arr() { return Value::array(); }

// ── Serialize ────────────────────────────────────────────────────────────
inline void escape_into(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

inline void dump_into(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Type::Null:  out += "null"; break;
        case Value::Type::Bool:  out += v.as_bool() ? "true" : "false"; break;
        case Value::Type::Number: {
            const double d = v.as_number();
            if (d == static_cast<double>(static_cast<long long>(d))) {
                out += std::to_string(static_cast<long long>(d));
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%g", d);
                out += buf;
            }
            break;
        }
        case Value::Type::String: escape_into(v.as_string(), out); break;
        case Value::Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : v.items()) {
                if (!first) out.push_back(',');
                first = false;
                dump_into(item, out);
            }
            out.push_back(']');
            break;
        }
        case Value::Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& kv : v.object_items()) {
                if (!first) out.push_back(',');
                first = false;
                escape_into(kv.first, out);
                out.push_back(':');
                dump_into(kv.second, out);
            }
            out.push_back('}');
            break;
        }
    }
}

inline std::string dump(const Value& v) {
    std::string out;
    dump_into(v, out);
    return out;
}

// ── Parse ────────────────────────────────────────────────────────────────
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s), i_(0) {}

    Value parse() {
        skip_ws();
        return parse_value();
    }

private:
    const std::string& s_;
    size_t i_;
    bool failed_ = false;

    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    char get()        { return i_ < s_.size() ? s_[i_++] : '\0'; }
    void skip_ws()    { while (i_ < s_.size() && (s_[i_]==' '||s_[i_]=='\t'||s_[i_]=='\n'||s_[i_]=='\r')) ++i_; }

    Value parse_value() {
        skip_ws();
        if (failed_ || i_ >= s_.size()) { failed_ = true; return Value::null(); }
        const char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Value::string(parse_string());
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') { i_ += 4; return Value::null(); }  // "null"
        return parse_number();
    }

    Value parse_object() {
        Value v = Value::object();
        ++i_;  // '{'
        skip_ws();
        if (peek() == '}') { ++i_; return v; }
        while (true) {
            skip_ws();
            if (peek() != '"') { failed_ = true; return v; }
            const std::string key = parse_string();
            skip_ws();
            if (get() != ':') { failed_ = true; return v; }
            v.set(key, parse_value());
            skip_ws();
            const char c = get();
            if (c == ',') continue;
            if (c == '}') break;
            failed_ = true; break;
        }
        return v;
    }

    Value parse_array() {
        Value v = Value::array();
        ++i_;  // '['
        skip_ws();
        if (peek() == ']') { ++i_; return v; }
        while (true) {
            v.push_back(parse_value());
            skip_ws();
            const char c = get();
            if (c == ',') continue;
            if (c == ']') break;
            failed_ = true; break;
        }
        return v;
    }

    std::string parse_string() {
        std::string out;
        ++i_;  // opening quote
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c == '\\' && i_ < s_.size()) {
                const char esc = s_[i_++];
                switch (esc) {
                    case 'n':  out.push_back('\n'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'r':  out.push_back('\r'); break;
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'u': {
                        // Basic Latin only (sufficient for this protocol's
                        // ASCII command set); non-ASCII code points become '?'.
                        if (i_ + 4 <= s_.size()) {
                            int code = 0;
                            for (int k = 0; k < 4; ++k) {
                                const char h = s_[i_++];
                                code <<= 4;
                                if (h >= '0' && h <= '9') code |= (h - '0');
                                else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            }
                            out.push_back(code < 128 ? static_cast<char>(code) : '?');
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        if (i_ < s_.size()) ++i_;  // closing quote
        else failed_ = true;
        return out;
    }

    Value parse_bool() {
        if (s_.compare(i_, 4, "true") == 0)  { i_ += 4; return Value::boolean(true); }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return Value::boolean(false); }
        failed_ = true;
        return Value::null();
    }

    Value parse_number() {
        const size_t start = i_;
        if (peek() == '-') ++i_;
        while (i_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[i_])) ||
                s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' ||
                s_[i_] == '+' || s_[i_] == '-'))
            ++i_;
        if (i_ == start) { failed_ = true; return Value::null(); }
        try {
            return Value::number(std::stod(s_.substr(start, i_ - start)));
        } catch (...) {
            failed_ = true;
            return Value::null();
        }
    }
};

inline Value parse(const std::string& s) {
    Parser p(s);
    return p.parse();
}

} // namespace minijson
} // namespace bridge
