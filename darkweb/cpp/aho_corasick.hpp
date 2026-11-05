// aho_corasick.hpp - trie + failure links + dictionary links.
// Build: O(sum of pattern lengths).  Search: O(N + Z)  (N = text length, Z = reported matches).
// Alphabet: 7-bit ASCII (text is normalised to lowercase ASCII before it gets here).
#pragma once
#include <algorithm>
#include <queue>
#include <string>
#include <vector>

class AhoCorasick {
public:
    struct Node {
        int go[128];          // trie edges (-1 = none)
        int fail = 0;         // longest proper suffix that is also a trie prefix
        int dict = 0;         // nearest node on the fail chain that ends a pattern (0 = none)
        int parent = -1;
        int depth = 0;
        char ch = 0;
        std::vector<int> out;  // ids of patterns that END exactly at this node
        Node() { std::fill(go, go + 128, -1); }
    };
    std::vector<Node> nodes;

    AhoCorasick() { nodes.emplace_back(); }

    void insert(const std::string& pat, int id) {
        int s = 0;
        for (unsigned char c : pat) {
            c &= 127;
            if (nodes[s].go[c] < 0) {
                nodes.emplace_back();
                int v = (int)nodes.size() - 1;
                nodes[v].parent = s; nodes[v].depth = nodes[s].depth + 1; nodes[v].ch = (char)c;
                nodes[s].go[c] = v;
            }
            s = nodes[s].go[c];
        }
        nodes[s].out.push_back(id);
    }

    // BFS over the trie: a node's fail link depends only on shallower nodes, which BFS has finished.
    void build() {
        std::queue<int> q;
        for (int c = 0; c < 128; ++c) { int v = nodes[0].go[c]; if (v >= 0) { nodes[v].fail = 0; q.push(v); } }
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (int c = 0; c < 128; ++c) {
                int v = nodes[u].go[c];
                if (v < 0) continue;
                int f = nodes[u].fail;
                while (f && nodes[f].go[c] < 0) f = nodes[f].fail;
                int t = nodes[f].go[c];
                nodes[v].fail = (t >= 0 && t != v) ? t : 0;
                int fv = nodes[v].fail;
                nodes[v].dict = nodes[fv].out.empty() ? nodes[fv].dict : fv;
                q.push(v);
            }
        }
    }

    // One automaton transition. Follows fail links while the edge is missing (amortised O(1)).
    int step(int s, unsigned char c, std::vector<int>* hops = nullptr) const {
        c &= 127;
        while (s && nodes[s].go[c] < 0) { if (hops) hops->push_back(s); s = nodes[s].fail; }
        int t = nodes[s].go[c];
        return t < 0 ? 0 : t;
    }

    // Report every pattern ending at state s (own outputs, then dictionary-link chain).
    template <class F> void outputs(int s, F f) const {
        int n = nodes[s].out.empty() ? nodes[s].dict : s;
        for (; n; n = nodes[n].dict) for (int id : nodes[n].out) f(id);
    }

    template <class F> void search(const std::string& text, F onMatch) const {  // onMatch(patternId, endIndex)
        int s = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            s = step(s, (unsigned char)text[i]);
            outputs(s, [&](int id) { onMatch(id, i); });
        }
    }

    std::string pathOf(int s) const {  // string spelled by root->s
        std::string p;
        for (; s > 0; s = nodes[s].parent) p.push_back(nodes[s].ch);
        std::reverse(p.begin(), p.end());
        return p;
    }
};
