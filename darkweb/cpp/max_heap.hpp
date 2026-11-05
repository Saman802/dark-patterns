// max_heap.hpp - array-backed binary max-heap (priority queue).
// Parent(i)=(i-1)/2, children 2i+1 / 2i+2. push = sift-up O(log n), pop = sift-down O(log n).
#pragma once
#include <functional>
#include <utility>
#include <vector>

template <typename T, typename Less = std::less<T>>
class MaxHeap {
    std::vector<T> a_;
    Less less_;
    size_t swaps_ = 0;

    void siftUp(size_t i) {
        while (i > 0) {
            size_t p = (i - 1) / 2;
            if (!less_(a_[p], a_[i])) break;  // parent already >= child
            std::swap(a_[p], a_[i]); ++swaps_; i = p;
        }
    }
    void siftDown(size_t i) {
        const size_t n = a_.size();
        for (;;) {
            size_t l = 2 * i + 1, r = l + 1, big = i;
            if (l < n && less_(a_[big], a_[l])) big = l;
            if (r < n && less_(a_[big], a_[r])) big = r;
            if (big == i) break;
            std::swap(a_[i], a_[big]); ++swaps_; i = big;
        }
    }

public:
    bool empty() const { return a_.empty(); }
    size_t size() const { return a_.size(); }
    const T& top() const { return a_.front(); }
    const std::vector<T>& raw() const { return a_; }  // level-order array (for visualisation)
    size_t swaps() const { return swaps_; }

    void push(const T& v) { a_.push_back(v); siftUp(a_.size() - 1); }
    T pop() {
        T t = a_.front();
        a_.front() = a_.back();
        a_.pop_back();
        if (!a_.empty()) siftDown(0);
        return t;
    }
};
