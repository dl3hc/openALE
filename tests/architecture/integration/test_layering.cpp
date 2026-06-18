/**
 * \file tests/architecture/integration/test_layering.cpp
 * \brief Architecture guard test — ALE data-link sublayer separation
 *
 * Covers: AC-GEN-001-001 (FEAT-GEN-001 / REQ-GEN-001, REQ-GEN-002)
 *
 * The MIL-STD-188-141B ALE data-link is organised into separately
 * compilable sublayers.  This test enforces, at build time, the two
 * structural invariants the acceptance criterion demands:
 *
 *   1. The three named sublayers exist as separate directories that each
 *      own at least one header:
 *          FEC/      — FEC sublayer  (Golay, interleaver)
 *          Word/     — LP / word sublayer (24-bit word coding)
 *          Protocol/ — ALE sublayer (state machine, framing)
 *
 *   2. The project header-include graph is ACYCLIC ("keine zirkulären
 *      Abhängigkeiten").  The graph is evaluated at FILE granularity —
 *      the relevant unit for a C/C++ build — rather than at directory
 *      granularity, because a single dependency-free leaf header may be
 *      filed under any directory without creating a real build cycle.
 *
 *   3. The three named sublayers obey a strict downward layering on their
 *      DIRECT includes: FEC depends on neither Word nor Protocol; Word
 *      does not depend directly on Protocol.  (Protocol -> Word -> FEC is
 *      the permitted top-to-bottom direction.)
 *
 * No protocol behaviour is exercised here; this is a pure source-tree
 * structural check, so it lives under tests/architecture/integration.
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef PROJECT_SOURCE_ROOT
#error "PROJECT_SOURCE_ROOT must be defined by the build system"
#endif

namespace {

// The module directories that make up the ALE stack (under include/ and src/).
const std::vector<std::string> kModules = {
    "FEC", "Word", "Protocol", "FSK", "LQA", "Stores", "Codec", "Modem"
};

bool is_source_file(const fs::path& p) {
    const auto ext = p.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cc";
}

// Collect every "Module/..." quoted include from a single file.
std::vector<std::string> quoted_project_includes(const fs::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash == std::string::npos) continue;
        const auto inc = line.find("include", hash);
        if (inc == std::string::npos) continue;
        const auto q1 = line.find('"', inc);
        if (q1 == std::string::npos) continue;
        const auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        out.push_back(line.substr(q1 + 1, q2 - q1 - 1));  // e.g. "FEC/golay.h"
    }
    return out;
}

// Which top-level module owns this quoted include path, if any ("FEC/golay.h" -> "FEC").
std::string module_of_include(const std::string& inc) {
    const auto slash = inc.find('/');
    if (slash == std::string::npos) return {};
    const std::string head = inc.substr(0, slash);
    for (const auto& m : kModules)
        if (head == m) return m;
    return {};
}

struct Graph {
    // Canonical node id = quoted-include form for headers ("Module/file.h"),
    // or "src:Module/file.cpp" for sources (sources are never include targets).
    std::map<std::string, std::set<std::string>> edges;
    std::set<std::string> nodes;
};

void add_tree(const fs::path& root, const std::string& prefix, const fs::path& include_root,
              Graph& g) {
    if (!fs::exists(root)) return;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || !is_source_file(e.path())) continue;
        const std::string rel = fs::relative(e.path(), root).generic_string();
        const std::string node = prefix.empty() ? rel : (prefix + rel);
        g.nodes.insert(node);
        for (const auto& inc : quoted_project_includes(e.path())) {
            if (module_of_include(inc).empty()) continue;          // not a project module include
            if (!fs::exists(include_root / inc)) continue;         // unresolved → ignore
            g.edges[node].insert(inc);                             // edge: file -> header
            g.nodes.insert(inc);
        }
    }
}

// DFS cycle detection; fills `cycle` with the offending path if one is found.
bool find_cycle(const Graph& g, std::vector<std::string>& cycle) {
    enum Color { White, Grey, Black };
    std::map<std::string, Color> color;
    std::vector<std::string> stack;

    std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
        color[u] = Grey;
        stack.push_back(u);
        auto it = g.edges.find(u);
        if (it != g.edges.end()) {
            for (const auto& v : it->second) {
                if (color[v] == Grey) {
                    auto from = std::find(stack.begin(), stack.end(), v);
                    cycle.assign(from, stack.end());
                    cycle.push_back(v);
                    return true;
                }
                if (color[v] == White && dfs(v)) return true;
            }
        }
        stack.pop_back();
        color[u] = Black;
        return false;
    };

    for (const auto& n : g.nodes)
        if (color[n] == White && dfs(n)) return true;
    return false;
}

bool check_sublayers_exist(const fs::path& include_root) {
    bool ok = true;
    for (const char* m : {"FEC", "Word", "Protocol"}) {
        const fs::path dir = include_root / m;
        bool has_header = false;
        if (fs::exists(dir)) {
            for (const auto& e : fs::recursive_directory_iterator(dir))
                if (e.is_regular_file() && e.path().extension() == ".h") { has_header = true; break; }
        }
        if (!has_header) {
            std::cout << "  [FAIL] sublayer directory include/" << m
                      << " missing or has no header\n";
            ok = false;
        } else {
            std::cout << "  [ok]   sublayer include/" << m << "/ present with header(s)\n";
        }
    }
    return ok;
}

// Direct (non-transitive) downward layering of the three named sublayers.
bool check_direct_layering(const fs::path& root, const std::string& module,
                           const std::vector<std::string>& forbidden_heads) {
    bool ok = true;
    for (const std::string& base : {std::string("include/"), std::string("src/")}) {
        const fs::path dir = root / (base + module);
        if (!fs::exists(dir)) continue;
        for (const auto& e : fs::recursive_directory_iterator(dir)) {
            if (!e.is_regular_file() || !is_source_file(e.path())) continue;
            for (const auto& inc : quoted_project_includes(e.path())) {
                const std::string head = module_of_include(inc);
                if (std::find(forbidden_heads.begin(), forbidden_heads.end(), head)
                    != forbidden_heads.end()) {
                    std::cout << "  [FAIL] " << base << module << "/"
                              << fs::relative(e.path(), dir).generic_string()
                              << " includes \"" << inc << "\" (upward dependency)\n";
                    ok = false;
                }
            }
        }
    }
    return ok;
}

} // namespace

int main() {
    const fs::path root = PROJECT_SOURCE_ROOT;
    const fs::path include_root = root / "include";

    std::cout << "\n===========================================\n";
    std::cout << "  PC-ALE Architecture — Sublayer separation\n";
    std::cout << "  AC-GEN-001-001 (MIL-STD-188-141B A.5)\n";
    std::cout << "===========================================\n";

    bool ok = true;

    std::cout << "\n[1] Sublayer directories exist with own headers\n";
    ok &= check_sublayers_exist(include_root);

    std::cout << "\n[2] Project header-include graph is acyclic\n";
    Graph g;
    add_tree(include_root, "", include_root, g);
    add_tree(root / "src", "src:", include_root, g);
    std::vector<std::string> cycle;
    if (find_cycle(g, cycle)) {
        std::cout << "  [FAIL] circular include dependency detected:\n      ";
        for (size_t i = 0; i < cycle.size(); ++i)
            std::cout << cycle[i] << (i + 1 < cycle.size() ? " -> " : "\n");
        ok = false;
    } else {
        std::cout << "  [ok]   " << g.nodes.size()
                  << " files form an acyclic include graph\n";
    }

    std::cout << "\n[3] Three sublayers obey downward layering (direct includes)\n";
    bool layering = true;
    layering &= check_direct_layering(root, "FEC",  {"Word", "Protocol"});
    layering &= check_direct_layering(root, "Word", {"Protocol"});
    if (layering)
        std::cout << "  [ok]   FEC depends on neither Word nor Protocol; "
                     "Word not directly on Protocol\n";
    ok &= layering;

    std::cout << "\n===========================================\n";
    std::cout << (ok ? "  RESULT: PASS\n" : "  RESULT: FAIL\n");
    std::cout << "===========================================\n\n";
    return ok ? 0 : 1;
}
