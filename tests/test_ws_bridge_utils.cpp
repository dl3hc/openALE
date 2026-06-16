/**
 * \file test_ws_bridge_utils.cpp
 * \brief Unit tests for apps/bridge/{sha1,base64,ws_handshake,minijson}.h
 *
 * Pure header tests — no socket code, no ws2_32 link needed.
 */

#include "../apps/bridge/sha1.h"
#include "../apps/bridge/base64.h"
#include "../apps/bridge/ws_handshake.h"
#include "../apps/bridge/minijson.h"

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace bridge;

static std::string to_hex(const uint8_t* data, size_t n) {
    std::ostringstream oss;
    for (size_t i = 0; i < n; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(data[i]);
    return oss.str();
}

// ============================================================================
// SHA-1
// ============================================================================

bool test_sha1_known_vector() {
    std::cout << "\n[SHA1] Known test vector \"abc\"\n";
    const auto digest = sha1_digest("abc");
    const std::string hex = to_hex(digest.data(), digest.size());
    const bool ok = hex == "a9993e364706816aba3e25717850c26c9cd0d89d";
    std::cout << "  sha1(\"abc\") = " << hex << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_sha1_empty_string() {
    std::cout << "\n[SHA1] Known test vector \"\" (empty)\n";
    const auto digest = sha1_digest("");
    const std::string hex = to_hex(digest.data(), digest.size());
    const bool ok = hex == "da39a3ee5e6b4b0d3255bfef95601890afd80709";
    std::cout << "  sha1(\"\") = " << hex << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// ============================================================================
// Base64
// ============================================================================

bool test_base64_known_vectors() {
    std::cout << "\n[Base64] Known vectors\n";
    bool ok = true;

    auto check = [&](const std::string& in, const std::string& expect) {
        const std::string got = base64_encode(in);
        const bool pass = got == expect;
        std::cout << "  base64(\"" << in << "\") = " << got << ": " << (pass ? "PASS" : "FAIL") << "\n";
        ok = ok && pass;
    };

    check("", "");
    check("f", "Zg==");
    check("fo", "Zm8=");
    check("foo", "Zm9v");
    check("foobar", "Zm9vYmFy");
    return ok;
}

// ============================================================================
// RFC6455 handshake — the spec's own worked example
// ============================================================================

bool test_ws_accept_key_rfc_example() {
    std::cout << "\n[WS-Handshake] RFC6455 worked example\n";
    const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string got = compute_accept_key(key);
    const bool ok = got == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    std::cout << "  accept-key(" << key << ") = " << got << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// ============================================================================
// minijson
// ============================================================================

bool test_minijson_flat_object_roundtrip() {
    std::cout << "\n[minijson] Flat object roundtrip\n";
    minijson::Value v = minijson::obj();
    v.set("cmd", minijson::Value::string("CALL"));
    v.set("id", minijson::Value::number(42));
    v.set("ok", minijson::Value::boolean(true));

    const std::string text = minijson::dump(v);
    const minijson::Value parsed = minijson::parse(text);

    const bool ok = parsed.get_string("cmd") == "CALL"
                  && parsed.get_number("id") == 42
                  && parsed.get_bool("ok") == true;
    std::cout << "  dump: " << text << "\n";
    std::cout << "  roundtrip preserves fields: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_minijson_string_array() {
    std::cout << "\n[minijson] String array\n";
    minijson::Value v = minijson::arr();
    v.push_back(minijson::Value::string("C-1"));
    v.push_back(minijson::Value::string("C-2"));

    const std::string text = minijson::dump(v);
    const minijson::Value parsed = minijson::parse(text);
    const auto items = parsed.as_string_array();

    const bool ok = items.size() == 2 && items[0] == "C-1" && items[1] == "C-2";
    std::cout << "  dump: " << text << "\n";
    std::cout << "  roundtrip preserves array: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_minijson_nested_object_array() {
    std::cout << "\n[minijson] Nested array of objects (channel-list shape)\n";
    minijson::Value list = minijson::arr();
    for (int i = 1; i <= 2; ++i) {
        minijson::Value ch = minijson::obj();
        ch.set("id", minijson::Value::string("C-" + std::to_string(i)));
        ch.set("rx_hz", minijson::Value::number(14000000 + i));
        list.push_back(ch);
    }
    minijson::Value root = minijson::obj();
    root.set("data", list);

    const std::string text = minijson::dump(root);
    const minijson::Value parsed = minijson::parse(text);
    const minijson::Value* data = parsed.find("data");

    bool ok = data != nullptr && data->is_array() && data->items().size() == 2;
    if (ok) {
        ok = data->items()[0].get_string("id") == "C-1"
          && data->items()[0].get_number("rx_hz") == 14000001
          && data->items()[1].get_string("id") == "C-2";
    }
    std::cout << "  dump: " << text << "\n";
    std::cout << "  roundtrip preserves nested structure: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_minijson_escaping() {
    std::cout << "\n[minijson] String escaping (quotes/backslash/newline)\n";
    const std::string raw = "AMD: \"hello\"\\world\nline2";
    minijson::Value v = minijson::obj();
    v.set("text", minijson::Value::string(raw));

    const std::string text = minijson::dump(v);
    const minijson::Value parsed = minijson::parse(text);

    const bool ok = parsed.get_string("text") == raw;
    std::cout << "  dump: " << text << "\n";
    std::cout << "  roundtrip preserves special chars: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_minijson_malformed_input_does_not_crash() {
    std::cout << "\n[minijson] Malformed input degrades safely\n";
    const minijson::Value v1 = minijson::parse("{not valid json");
    const minijson::Value v2 = minijson::parse("");
    const minijson::Value v3 = minijson::parse("{\"cmd\":}");
    const bool ok = true;  // reaching this line without crashing/hanging is the test
    std::cout << "  three malformed inputs parsed without crashing: " << (ok ? "PASS" : "FAIL") << "\n";
    (void)v1; (void)v2; (void)v3;
    return ok;
}

// ============================================================================
// Runner
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  ale_bridge utility tests (sha1/base64/ws-handshake/minijson)\n";
    std::cout << "========================================\n";

    int pass_count = 0, fail_count = 0;
    auto run = [&](const char* name, bool result) {
        if (result) ++pass_count; else { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    run("SHA1 known vector \"abc\"",        test_sha1_known_vector());
    run("SHA1 known vector \"\"",           test_sha1_empty_string());
    run("Base64 known vectors",             test_base64_known_vectors());
    run("RFC6455 worked example",           test_ws_accept_key_rfc_example());
    run("minijson flat object roundtrip",   test_minijson_flat_object_roundtrip());
    run("minijson string array",            test_minijson_string_array());
    run("minijson nested object array",     test_minijson_nested_object_array());
    run("minijson escaping",                test_minijson_escaping());
    run("minijson malformed input safety",  test_minijson_malformed_input_does_not_crash());

    std::cout << "\n========================================\n";
    std::cout << "  Passed: " << pass_count << "  Failed: " << fail_count << "\n";
    std::cout << "========================================\n\n";

    return (fail_count == 0) ? 0 : 1;
}
