// hash_map.hpp - hand-written separate-chaining hash table (no std::unordered_map).
// Hash: 64-bit FNV-1a. Capacity is a power of two so slot = hash & (cap-1).
// Load factor is kept <= 0.75 by doubling + rehashing  => O(1) amortised put/get.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

template <typename V>
class HashMap {
    struct Node {
        std::string key;
        V value;
        Node* next;
        Node(const std::string& k, const V& v) : key(k), value(v), next(nullptr) {}
    };
    std::vector<Node*> table_;
    size_t size_ = 0;
    size_t collisions_ = 0;  // inserts that landed in an already-occupied bucket

    static uint64_t fnv1a(const std::string& s) {
        uint64_t h = 14695981039346656037ULL;  // offset basis
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }  // FNV prime
        return h;
    }
    static size_t slot(const std::string& k, size_t cap) { return (size_t)(fnv1a(k) & (cap - 1)); }

    void link(Node* n, std::vector<Node*>& t) {  // push-front into chain
        size_t i = slot(n->key, t.size());
        if (t[i]) ++collisions_;
        n->next = t[i];
        t[i] = n;
    }
    void grow() {
        std::vector<Node*> nt(table_.size() * 2, nullptr);
        collisions_ = 0;
        for (Node* head : table_)
            while (head) { Node* nx = head->next; link(head, nt); head = nx; }
        table_.swap(nt);
    }

public:
    explicit HashMap(size_t cap = 16) : table_(cap, nullptr) {}  // cap: power of two
    ~HashMap() {
        for (Node* h : table_) while (h) { Node* nx = h->next; delete h; h = nx; }
    }
    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    V* get(const std::string& k) {
        for (Node* n = table_[slot(k, table_.size())]; n; n = n->next)
            if (n->key == k) return &n->value;
        return nullptr;
    }
    bool contains(const std::string& k) { return get(k) != nullptr; }

    void put(const std::string& k, const V& v) {
        if (V* e = get(k)) { *e = v; return; }
        if ((double)(size_ + 1) / (double)table_.size() > 0.75) grow();
        link(new Node(k, v), table_);
        ++size_;
    }

    size_t size() const { return size_; }
    size_t bucketCount() const { return table_.size(); }
    size_t collisions() const { return collisions_; }
    double loadFactor() const { return (double)size_ / (double)table_.size(); }
    size_t maxChain() const {
        size_t m = 0;
        for (Node* h : table_) { size_t c = 0; for (; h; h = h->next) ++c; if (c > m) m = c; }
        return m;
    }
    // Visit every non-empty bucket: f(bucketIndex, vector<key>) - used by Viva Mode.
    template <class F> void forEachBucket(F f) const {
        for (size_t i = 0; i < table_.size(); ++i) {
            if (!table_[i]) continue;
            std::vector<std::string> keys;
            for (Node* n = table_[i]; n; n = n->next) keys.push_back(n->key);
            f(i, keys);
        }
    }
};
