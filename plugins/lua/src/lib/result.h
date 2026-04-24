#ifndef RESULT_H
#define RESULT_H

#include <variant>
#include <utility>

template <typename T, typename E>
struct Result {
  // 静态构造方法
  static Result<T, E> Ok(T t) {
    return Result(std::variant<T, E>(std::in_place_index<0>, std::move(t)));
  }
  static Result<T, E> Err(E e) {
    return Result(std::variant<T, E>(std::in_place_index<1>, std::move(e)));
  }

  // 依赖 C++ 的 Rule of Zero，默认的移动/拷贝构造和析构被完美支持
  Result(Result&&) = default;
  Result(const Result&) = default;
  Result& operator=(Result&&) = default;
  Result& operator=(const Result&) = default;
  ~Result() = default;

  // 保持原有接口不变
  bool ok() { return data_.index() == 0; }
  T& get() { return std::get<0>(data_); }
  E& get_err() { return std::get<1>(data_); }

 private:
  std::variant<T, E> data_;
  // 私有构造，仅允许通过 Ok() 和 Err() 创建
  Result(std::variant<T, E> data) : data_(std::move(data)) {}
};

// 针对 void 类型的特化版本
template <typename E>
struct Result<void, E> {
  static Result<void, E> Ok() {
    return Result(std::variant<std::monostate, E>(std::in_place_index<0>));
  }
  static Result<void, E> Err(E e) {
    return Result(
        std::variant<std::monostate, E>(std::in_place_index<1>, std::move(e)));
  }

  Result(Result&&) = default;
  Result(const Result&) = default;
  Result& operator=(Result&&) = default;
  Result& operator=(const Result&) = default;
  ~Result() = default;

  bool ok() { return data_.index() == 0; }
  E& get_err() { return std::get<1>(data_); }

 private:
  std::variant<std::monostate, E> data_;
  Result(std::variant<std::monostate, E> data) : data_(std::move(data)) {}
};

#endif  // RESULT_H