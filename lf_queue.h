#pragma once 

#include <iostream>
#include <vector>
#include <atomic>

#include "macros.h"

using namespace std;

namespace Common {
    template<typename T>
    class LFQueue final {
        public:
        LFQueue(size_t num_elems) : store_(num_elems, T()) {}

        auto getNextToWriteTo() noexcept {
            return &store_[next_write_index_];
        }

        auto updateWriteIndex() noexcept {
            next_write_index_ = (next_write_index_ + 1) % store_.size();
            num_elements_++;
        }

        auto getNextToRead() const noexcept -> const T* {
            return (size() ? &store_[next_read_index_] : nullptr);
        }

        auto updateReadIndex() noexcept {
            next_read_index_ = (next_read_index_ + 1) % store_.size();
            ASSERT(num_elements_ != 0, "Read an invalid element in:" + to_string(pthread_self()));
            num_elements_--;
        }

        auto size() const noexcept {
            return num_elements_.load();
        }

        LFQueue() = delete;

        LFQueue(const LFQueue &) = delete;

        LFQueue(const LFQueue &&) = delete;

        LFQueue &operator=(const LFQueue &) = delete;

        LFQueue &operator=(const LFQueue &&) = delete;

        private:

        vector<T> store_;
        atomic<size_t> next_write_index_ = {0};
        atomic<size_t> next_read_index_ = {0};
        atomic<size_t> num_elements_ = {0};
    };
}