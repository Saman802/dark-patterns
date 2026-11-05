// engine.cpp - Darkweb detection pipeline (zero ML):
//   1. parse flat DOM dump -> N-ary tree (DomTree)
//   2. Aho-Corasick over every node's own text  -> keyword hits + context bits
//   3. post-order DFS aggregates context bits / subtree size up the tree
//   4. structural rules inspect parent/child/sibling relations + style flags
//   5. every finding goes into a MaxHeap keyed by risk score -> ranked report
// Input line format (one DOM element per line, document order):  parent \t TAG \t flags \t own text
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#include "aho_corasick.hpp"
#include "dom_tree.hpp"
#include "hash_map.hpp"
#include "max_heap.hpp"
#include "rules.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define DW_API extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define DW_API extern "C"
#endif

using namespace dw;

namespace {

struct Finding {
    double score = 0, conf = 1;
    int node = 0;
    std::string rule, reason, evidence;
    // "greater" = higher score; ties broken by earlier node so output is deterministic
    bool operator<(const Finding& o) const { return score != o.score ? score < o.score : node > o.node; }
};

struct Engine {
    std::unique_ptr<RuleBook> book;
    std::unique_ptr<AhoCorasick> ac;
    std::string out;
    int init(const char* custom) {
        book.reset(new RuleBook());
        book->loadBuiltins();
        book->loadCustom(custom);
        ac.reset(new AhoCorasick());
        for (size_t i = 0; i < book->patterns.size(); ++i) ac->insert(book->patterns[i].text, (int)i);
        ac->build();
        return (int)book->patterns.size();
    }
} G;

std::string jq(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); r += b; }
                else r.push_back((char)c);
        }
    }
    return r + "\"";
}
std::string num(double v) { char b[48]; snprintf(b, sizeof b, "%.2f", v); return b; }
std::string inum(long v) { return std::to_string(v); }

std::string clip(const std::string& s, size_t n) {  // UTF-8 safe truncation
    if (s.size() <= n) return s;
    size_t k = n;
    while (k > 0 && (((unsigned char)s[k]) & 0xC0) == 0x80) --k;
    return s.substr(0, k) + "...";
}

bool isWord(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
bool isDig(char c) { return c >= '0' && c <= '9'; }
// whole-word match only: "ends in" must not fire inside "trends in"
bool boundaryOk(const std::string& t, size_t st, size_t en) {
    if (isWord(t[st]) && st > 0 && isWord(t[st - 1])) return false;
    if (isWord(t[en]) && en + 1 < t.size() && isWord(t[en + 1])) return false;
    return true;
}
// hand-written scanner for  H:MM:SS / HH:MM:SS  (a tiny finite-state recogniser, no regex)
bool hasClock(const std::string& t) {
    size_t n = t.size();
    for (size_t i = 0; i < n; ++i) {
        if (!isDig(t[i])) continue;
        if (i > 0 && (isDig(t[i - 1]) || t[i - 1] == ':')) continue;
        size_t j = i; int d = 0;
        while (j < n && isDig(t[j]) && d < 2) { ++j; ++d; }
        if (j >= n || t[j] != ':') { continue; }
        ++j;
        if (j + 1 >= n || !isDig(t[j]) || !isDig(t[j + 1])) { continue; }
        j += 2;
        if (j >= n || t[j] != ':') { continue; }
        ++j;
        if (j + 1 >= n || !isDig(t[j]) || !isDig(t[j + 1])) { continue; }
        j += 2;
        if (j < n && (isDig(t[j]) || t[j] == ':')) continue;
        return true;
    }
    return false;
}

// Recognises unit-style countdowns such as "00h : 03m : 10s", "3m 10s", "1d 02h 05m".
// Units must descend (d > h > m > s). Returns 0 = none, 1 = weak (no seconds, e.g. "2h 30m": could be a
// film runtime, so urgency wording is required), 2 = strong (ends in seconds: a real ticking clock).
int unitClockKind(const std::string& t) {
    size_t n = t.size();
    int best = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!isDig(t[i]) || (i > 0 && isWord(t[i - 1]))) continue;
        size_t j = i; int pairs = 0, last = -1; bool sec = false;
        for (;;) {
            size_t k = j; int d = 0;
            while (k < n && isDig(t[k]) && d < 3) { ++k; ++d; }
            if (d == 0) break;
            size_t m = k;
            while (m < n && t[m] == ' ') ++m;
            if (m >= n) break;
            int u = t[m] == 'd' ? 0 : t[m] == 'h' ? 1 : t[m] == 'm' ? 2 : t[m] == 's' ? 3 : -1;
            if (u < 0 || u <= last) break;
            size_t e = m + 1;
            if (e < n && t[e] >= 'a' && t[e] <= 'z') break;   // "3 min", "2 days": not a bare unit letter
            last = u; ++pairs; sec = (u == 3);
            j = e;
            while (j < n && (t[j] == ' ' || t[j] == ':')) ++j;
            if (sec) break;
        }
        if (pairs >= 2) { int kind = sec ? 2 : 1; if (kind > best) best = kind; }
    }
    return best;
}
int clockKind(const std::string& t) { return hasClock(t) ? 1 : unitClockKind(t); }

struct MatchLog { int node, pat, pos; };

}  // namespace

DW_API int dw_init(const char* customRules) { return G.init(customRules); }

DW_API const char* dw_scan(const char* data) {
    if (!G.book) G.init(nullptr);
    clock_t t0 = clock();
    const std::vector<PatternDef>& pats = G.book->patterns;

    // ---------- 1. parse into arrays + N-ary tree ----------
    DomTree tree;
    std::vector<std::string> tag, orig, norm;
    std::vector<int> flags;
    {
        std::string s(data ? data : "");
        size_t pos = 0;
        while (pos < s.size()) {
            size_t e = s.find('\n', pos); if (e == std::string::npos) e = s.size();
            if (e > pos) {
                std::string line = s.substr(pos, e - pos);
                size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1),
                       c = b == std::string::npos ? b : line.find('\t', b + 1);
                int parent = atoi(line.c_str());
                tag.push_back(a == std::string::npos ? "?" : line.substr(a + 1, (b == std::string::npos ? line.size() : b) - a - 1));
                flags.push_back(b == std::string::npos ? 0 : atoi(line.c_str() + b + 1));
                orig.push_back(c == std::string::npos ? "" : line.substr(c + 1));
                norm.push_back(normalizeText(orig.back()));
                tree.addNode(parent);
            }
            pos = e + 1;
        }
    }
    const int n = tree.size();

    // ---------- finding sink: MaxHeap + HashMap dedupe ----------
    MaxHeap<Finding> heap;
    HashMap<bool> seen;
    auto addFinding = [&](int node, const std::string& rid, const std::string& evidence, const std::string& extra, double conf, bool boost) {
        std::string key = std::to_string(node) + "|" + rid;
        if (seen.contains(key)) return;
        seen.put(key, true);
        Rule* r = G.book->rules.get(rid);                  // O(1) rule lookup
        if (!r) return;
        double w = 1.0; if (double* pw = G.book->categoryWeight.get(r->category)) w = *pw;  // O(1) weight lookup
        std::string reason = r->explanation + extra;
        if (boost && (flags[node] & (F_TINY | F_LOWCONTRAST))) {
            conf *= 1.4; reason += " It is also rendered in tiny or low-contrast text, which hides it from the shopper.";
        }
        Finding f; f.score = r->severity * w * conf; f.conf = conf; f.node = node; f.rule = rid; f.reason = reason; f.evidence = evidence;
        heap.push(f);
    };

    // ---------- 2. Aho-Corasick over each node's own text ----------
    std::vector<int> own(n, 0), agg(n, 0), sub(n, 0);
    std::vector<MatchLog> mlog;
    long totalMatches = 0;
    for (int u = 0; u < n; ++u) {
        if (norm[u].empty()) continue;
        const std::string& t = norm[u];
        G.ac->search(t, [&](int pid, size_t endPos) {
            const PatternDef& pd = pats[pid];
            size_t st = endPos + 1 - pd.text.size();
            if (!boundaryOk(t, st, endPos)) return;
            own[u] |= pd.ctx; ++totalMatches;
            if (mlog.size() < 300) mlog.push_back({u, pid, (int)st});
            if (!pd.ruleId.empty() && !(flags[u] & F_HIDDEN)) addFinding(u, pd.ruleId, pd.text, "", 1.0, true);
        });
    }

    // ---------- 3. bottom-up aggregation with iterative post-order DFS ----------
    tree.postOrder([&](int u) {
        agg[u] |= own[u]; sub[u] += 1;
        int p = tree.parent(u);
        if (p >= 0) { agg[p] |= agg[u]; sub[p] += sub[u]; }
    });

    auto snippet = [&](int root, size_t cap) {  // concatenated own text of a subtree (pre-order DFS)
        std::string s;
        tree.preOrder(root, [&](int v) { if (s.size() < cap && !orig[v].empty()) { if (!s.empty()) s += " "; s += orig[v]; } });
        return clip(s, cap);
    };
    const int BOTH = C_FREE_TRIAL | C_RENEW;
    auto obscured = [&](int u) { return (flags[u] & (F_TINY | F_LOWCONTRAST)) != 0; };

    // ---------- 4. structural rules ----------
    for (int u = 0; u < n; ++u) {
        // (a) pre-ticked / hidden opt-in checkboxes: climb <=3 ancestors to the nearest *small* subtree mentioning a subscription-like word
        if ((flags[u] & F_CHECKBOX) && (flags[u] & F_CHECKED)) {
            int p = tree.parent(u), found = -1;
            for (int lvl = 0; lvl < 3 && p >= 0; ++lvl, p = tree.parent(p))
                if ((agg[p] & C_SUBSCRIBE) && sub[p] <= 40) { found = p; break; }
            if (found >= 0) {
                if (flags[u] & F_HIDDEN) addFinding(found, "S_HIDDEN_PRESELECT", snippet(found, 110), "", 1.0, false);
                else addFinding(found, "S_PRESELECTED_OPT_IN", snippet(found, 110), "", 1.0, false);
            }
        }
        // (b) misdirection: tiny/faded decline control that has a prominent BUTTON sibling
        if ((own[u] & C_DECLINE) && obscured(u)) {
            int p = tree.parent(u); bool prominent = false;
            if (p >= 0) tree.forEachChild(p, [&](int c) { if (c != u && (flags[c] & F_BUTTON) && !obscured(c)) prominent = true; });
            if (prominent) addFinding(u, "S_MISDIRECTION", orig[u], "", 1.0, false);
        }
        // (c) roach motel: cancel / unsubscribe control that is tiny or faded
        if ((own[u] & C_CANCEL) && obscured(u)) addFinding(u, "S_HIDDEN_CANCEL", orig[u], "", 1.0, false);
        // (d) forced continuity: the SMALLEST subtree (no child already has both) containing trial + renewal wording
        if ((agg[u] & BOTH) == BOTH && sub[u] <= 60) {
            bool childHasBoth = false;
            tree.forEachChild(u, [&](int c) { if ((agg[c] & BOTH) == BOTH) childHasBoth = true; });
            if (!childHasBoth) {
                bool hid = false;
                tree.preOrder(u, [&](int v) { if ((own[v] & C_RENEW) && obscured(v)) hid = true; });
                addFinding(u, "S_FORCED_CONTINUITY", snippet(u, 140), hid ? " The auto-renewal terms are visually suppressed." : "", hid ? 1.4 : 1.0, false);
            }
        }
    }

    // (e) countdown clocks. A clock may be split over several tiny spans ("00h" ":" "03m" ":" "10s"), so a node
    //     whose own text has no clock but whose SMALL subtree (<= 12 nodes) reads like one is a candidate too.
    {
        std::vector<int> ck(n, 0);
        for (int u = 0; u < n; ++u) {
            if (flags[u] & F_HIDDEN) continue;
            int kind = norm[u].empty() ? 0 : clockKind(norm[u]);
            if (!kind && sub[u] > 1 && sub[u] <= 12) {
                std::string joined;
                tree.preOrder(u, [&](int v) { if (!norm[v].empty()) { joined += norm[v]; joined += ' '; } });
                kind = clockKind(joined);
            }
            ck[u] = kind;
        }
        for (int u = 0; u < n; ++u) {
            if (!ck[u]) continue;
            bool childHas = false;
            tree.forEachChild(u, [&](int c) { if (ck[c]) childHas = true; });
            if (childHas) continue;                       // report the smallest node that holds the whole clock
            bool urgent = false;
            int a = u;
            for (int lvl = 0; lvl < 3 && a >= 0; ++lvl, a = tree.parent(a))
                if ((agg[a] & C_URGENT) && sub[a] <= 50) { urgent = true; break; }
            if (ck[u] == 2) addFinding(u, "S_COUNTDOWN", snippet(u, 100), urgent ? "" : " No deadline wording is shown, but a clock that counts down in seconds is pure time pressure.", urgent ? 1.0 : 0.85, false);
            else if (urgent) addFinding(u, "S_COUNTDOWN", snippet(u, 100), "", 1.0, false);
        }
    }

    // ---------- 5. drain the max-heap => findings in descending risk order ----------
    struct HeapItem { double s; int n; std::string r; };
    std::vector<HeapItem> snap;
    for (const Finding& f : heap.raw()) snap.push_back({f.score, f.node, f.rule});
    size_t heapSwaps = heap.swaps();
    std::vector<Finding> ranked;
    while (!heap.empty()) ranked.push_back(heap.pop());
    double raw = 0, w = 1.0;
    for (const Finding& f : ranked) { raw += f.score * w; w *= 0.9; }  // diminishing returns for repeats
    int deception = (int)std::lround(100.0 * (1.0 - std::exp(-raw / 35.0)));
    const char* level = deception < 20 ? "Low" : deception < 45 ? "Moderate" : deception < 70 ? "High" : "Severe";

    // ---------- 6. JSON report ----------
    std::vector<char> keep(n, 0);
    std::vector<std::vector<std::string>> ruleOf(n);
    for (const Finding& f : ranked) { ruleOf[f.node].push_back(f.rule); for (int a = f.node; a >= 0 && !keep[a]; a = tree.parent(a)) keep[a] = 1; }
    for (const Finding& f : ranked) keep[f.node] = 1;

    long micros = (long)((clock() - t0) * 1000000.0 / CLOCKS_PER_SEC);
    std::string& o = G.out; o.clear(); o.reserve(1 << 15);
    o += "{\"ok\":true,\"stats\":{\"nodes\":" + inum(n) + ",\"maxDepth\":" + inum(tree.maxDepth()) + ",\"matches\":" + inum(totalMatches) +
         ",\"findings\":" + inum((long)ranked.size()) + ",\"deception\":" + inum(deception) + ",\"level\":" + jq(level) +
         ",\"rawScore\":" + num(raw) + ",\"trieNodes\":" + inum((long)G.ac->nodes.size()) + ",\"patterns\":" + inum((long)pats.size()) +
         ",\"rules\":" + inum((long)G.book->rules.size()) + ",\"hashBuckets\":" + inum((long)G.book->rules.bucketCount()) +
         ",\"hashLoad\":" + num(G.book->rules.loadFactor()) + ",\"hashCollisions\":" + inum((long)G.book->rules.collisions()) +
         ",\"heapSwaps\":" + inum((long)heapSwaps) + ",\"micros\":" + inum(micros) + "},\"findings\":[";
    for (size_t i = 0; i < ranked.size(); ++i) {
        const Finding& f = ranked[i]; Rule* r = G.book->rules.get(f.rule);
        if (i) o += ",";
        o += "{\"rank\":" + inum((long)i + 1) + ",\"node\":" + inum(f.node) + ",\"rule\":" + jq(f.rule) + ",\"category\":" + jq(r ? r->category : "") +
             ",\"severity\":" + num(r ? r->severity : 0) + ",\"score\":" + num(f.score) + ",\"confidence\":" + num(f.conf) +
             ",\"structural\":" + (r && r->structural ? "true" : "false") + ",\"reason\":" + jq(f.reason) + ",\"evidence\":" + jq(clip(f.evidence, 140)) + "}";
    }
    o += "],\"matches\":[";
    for (size_t i = 0; i < mlog.size(); ++i) {
        const PatternDef& pd = pats[mlog[i].pat];
        if (i) o += ",";
        o += "{\"node\":" + inum(mlog[i].node) + ",\"pattern\":" + jq(pd.text) + ",\"rule\":" + jq(pd.ruleId) + ",\"ctx\":" + inum(pd.ctx) + ",\"pos\":" + inum(mlog[i].pos) + "}";
    }
    o += "],\"heap\":{\"array\":[";
    for (size_t i = 0; i < snap.size(); ++i) { if (i) o += ","; o += "{\"s\":" + num(snap[i].s) + ",\"n\":" + inum(snap[i].n) + ",\"r\":" + jq(snap[i].r) + "}"; }
    o += "]},\"tree\":[";
    bool first = true;
    for (int u = 0; u < n; ++u) {
        if (!keep[u]) continue;
        if (!first) o += ",";
        first = false;
        o += "{\"id\":" + inum(u) + ",\"p\":" + inum(tree.parent(u)) + ",\"tag\":" + jq(tag[u]) + ",\"f\":" + inum(flags[u]) + ",\"m\":" + inum(agg[u]) +
             ",\"s\":" + inum(sub[u]) + ",\"t\":" + jq(clip(orig[u], 70)) + ",\"r\":[";
        for (size_t k = 0; k < ruleOf[u].size(); ++k) { if (k) o += ","; o += jq(ruleOf[u][k]); }
        o += "]}";
    }
    o += "]}";
    return o.c_str();
}

// Step-by-step automaton trace of arbitrary text (Viva Mode)
DW_API const char* dw_trace_text(const char* text) {
    if (!G.book) G.init(nullptr);
    const std::vector<PatternDef>& pats = G.book->patterns;
    std::string t = clip(normalizeText(text ? text : ""), 400);
    if (t.size() > 3 && t.compare(t.size() - 3, 3, "...") == 0) t.resize(t.size() - 3);
    std::string& o = G.out; o = "{\"text\":" + jq(t) + ",\"steps\":[";
    int s = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        std::vector<int> hops;
        int to = G.ac->step(s, (unsigned char)t[i], &hops);
        if (i) o += ",";
        o += "{\"i\":" + inum((long)i) + ",\"ch\":" + jq(std::string(1, t[i])) + ",\"from\":" + inum(s) + ",\"fromPath\":" + jq(G.ac->pathOf(s)) + ",\"hops\":[";
        for (size_t k = 0; k < hops.size(); ++k) { if (k) o += ","; o += jq(G.ac->pathOf(hops[k])); }
        o += "],\"to\":" + inum(to) + ",\"toPath\":" + jq(G.ac->pathOf(to)) + ",\"out\":[";
        bool f = true;
        G.ac->outputs(to, [&](int id) { if (!f) o += ",";
        f = false; o += jq(pats[id].text); });
        o += "]}";
        s = to;
    }
    o += "]}";
    return o.c_str();
}

// ---- dumps used by the dashboard's Viva Mode ----
DW_API const char* dw_rules_dump() {
    if (!G.book) G.init(nullptr);
    std::string& o = G.out; o = "{\"rules\":[";
    for (size_t i = 0; i < G.book->ruleOrder.size(); ++i) {
        Rule* r = G.book->rules.get(G.book->ruleOrder[i]);
        if (i) o += ",";
        o += "{\"id\":" + jq(r->id) + ",\"category\":" + jq(r->category) + ",\"severity\":" + num(r->severity) + ",\"structural\":" + (r->structural ? "true" : "false") +
             ",\"custom\":" + (r->custom ? "true" : "false") + ",\"explanation\":" + jq(r->explanation) + ",\"patterns\":[";
        bool f = true;
        for (const PatternDef& pd : G.book->patterns) if (pd.ruleId == r->id) { if (!f) o += ",";
        f = false; o += jq(pd.text); }
        o += "]}";
    }
    o += "],\"contextPatterns\":" + inum((long)std::count_if(G.book->patterns.begin(), G.book->patterns.end(), [](const PatternDef& p) { return p.ruleId.empty(); })) + ",\"categories\":[";
    bool f = true;
    for (size_t i = 0; i < G.book->ruleOrder.size(); ++i) {
        Rule* r = G.book->rules.get(G.book->ruleOrder[i]);
        double* w = G.book->categoryWeight.get(r->category);
        if (!f) o += ",";
        f = false;
        o += "{\"rule\":" + jq(r->id) + ",\"category\":" + jq(r->category) + ",\"weight\":" + num(w ? *w : 1.0) + "}";
    }
    o += "]}";
    return o.c_str();
}

DW_API const char* dw_trie_dump() {
    if (!G.book) G.init(nullptr);
    std::string& o = G.out; o = "{\"nodes\":[";
    const auto& N = G.ac->nodes;
    for (size_t i = 0; i < N.size(); ++i) {
        if (i) o += ",";
        o += "{\"id\":" + inum((long)i) + ",\"p\":" + inum(N[i].parent) + ",\"c\":" + jq(std::string(1, N[i].ch ? N[i].ch : ' ')) + ",\"f\":" + inum(N[i].fail) +
             ",\"d\":" + inum(N[i].depth) + ",\"o\":" + inum((long)N[i].out.size()) + "}";
    }
    o += "],\"patternCount\":" + inum((long)G.book->patterns.size()) + "}";
    return o.c_str();
}

DW_API const char* dw_hash_dump() {
    if (!G.book) G.init(nullptr);
    const HashMap<Rule>& H = G.book->rules;
    std::string& o = G.out;
    o = "{\"buckets\":" + inum((long)H.bucketCount()) + ",\"size\":" + inum((long)H.size()) + ",\"load\":" + num(H.loadFactor()) +
        ",\"collisions\":" + inum((long)H.collisions()) + ",\"maxChain\":" + inum((long)H.maxChain()) + ",\"table\":[";
    bool f = true;
    H.forEachBucket([&](size_t b, const std::vector<std::string>& keys) {
        if (!f) o += ",";
        f = false;
        o += "{\"b\":" + inum((long)b) + ",\"keys\":[";
        for (size_t k = 0; k < keys.size(); ++k) { if (k) o += ","; o += jq(keys[k]); }
        o += "]}";
    });
    o += "]}";
    return o.c_str();
}