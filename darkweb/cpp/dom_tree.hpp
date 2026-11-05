// dom_tree.hpp - N-ary tree in "left-child / right-sibling" form.
// Every node stores: parent, firstChild, lastChild, nextSibling. Any number of children, O(1) append.
// All traversals are ITERATIVE with an explicit stack (deep DOMs cannot overflow the call stack).
#pragma once
#include <utility>
#include <vector>

class DomTree {
    std::vector<int> parent_, first_, last_, next_, depth_, roots_;

public:
    // Nodes are added in document (pre-)order, so a parent always has a smaller index than its children.
    int addNode(int parent) {
        int id = (int)parent_.size();
        if (parent >= id) parent = -1;
        parent_.push_back(parent); first_.push_back(-1); last_.push_back(-1); next_.push_back(-1);
        if (parent < 0) { depth_.push_back(0); roots_.push_back(id); }
        else {
            depth_.push_back(depth_[parent] + 1);
            if (first_[parent] < 0) first_[parent] = id; else next_[last_[parent]] = id;
            last_[parent] = id;
        }
        return id;
    }
    int size() const { return (int)parent_.size(); }
    int parent(int u) const { return parent_[u]; }
    int depth(int u) const { return depth_[u]; }
    int maxDepth() const { int m = 0; for (int d : depth_) if (d > m) m = d; return m; }

    template <class F> void forEachChild(int u, F f) const {
        for (int c = first_[u]; c >= 0; c = next_[c]) f(c);
    }

    // Pre-order DFS of the subtree rooted at `root` (document order).
    // Trick for LCRS: when visiting u push nextSibling FIRST, then firstChild, so the child subtree
    // is fully processed before the sibling is popped. The root's own sibling is never pushed.
    template <class F> void preOrder(int root, F f) const {
        std::vector<int> st{root};
        while (!st.empty()) {
            int u = st.back(); st.pop_back();
            f(u);
            if (u != root && next_[u] >= 0) st.push_back(next_[u]);
            if (first_[u] >= 0) st.push_back(first_[u]);
        }
    }

    // Post-order DFS (children before parent) of one subtree: used for bottom-up aggregation.
    template <class F> void postOrderFrom(int root, F f) const {
        std::vector<std::pair<int, bool>> st{{root, false}};
        while (!st.empty()) {
            auto [u, expanded] = st.back(); st.pop_back();
            if (expanded) { f(u); continue; }
            st.push_back({u, true});
            for (int c = first_[u]; c >= 0; c = next_[c]) st.push_back({c, false});
        }
    }
    template <class F> void postOrder(F f) const { for (int r : roots_) postOrderFrom(r, f); }
};
