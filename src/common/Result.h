#pragma once

#include <string>
#include <utility>

namespace remote {

class Result {
public:
    static Result Ok() { return Result(true, {}); }
    static Result Fail(std::string message) { return Result(false, std::move(message)); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    explicit operator bool() const noexcept { return ok_; }

private:
    Result(bool ok, std::string error) : ok_(ok), error_(std::move(error)) {}

    bool ok_ = false;
    std::string error_;
};

template <typename T>
class ResultT {
public:
    static ResultT Ok(T value) { return ResultT(std::move(value)); }
    static ResultT Fail(std::string message) { return ResultT(std::move(message)); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] T& value() & { return value_; }
    [[nodiscard]] const T& value() const& { return value_; }
    [[nodiscard]] T&& value() && { return std::move(value_); }

private:
    explicit ResultT(T value) : ok_(true), value_(std::move(value)) {}
    explicit ResultT(std::string error) : ok_(false), error_(std::move(error)) {}

    bool ok_ = false;
    T value_{};
    std::string error_;
};

} // namespace remote

