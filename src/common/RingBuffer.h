#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace remote {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : storage_(capacity) {}

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == storage_.size(); }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return storage_.size(); }

    bool push(T value) {
        if (storage_.empty() || full()) {
            return false;
        }
        storage_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % storage_.size();
        ++size_;
        return true;
    }

    std::optional<T> pop() {
        if (empty()) {
            return std::nullopt;
        }
        auto value = std::move(storage_[head_]);
        head_ = (head_ + 1) % storage_.size();
        --size_;
        return value;
    }

private:
    std::vector<T> storage_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};

} // namespace remote

