#include <condition_variable>
#include <coroutine>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <stop_token>
#include <utility>

#include "fmt/format.h"

template <typename T>
class Mtx {
 public:
  template <typename Func>
  auto with_lock(Func &&f) {
    std::lock_guard<std::shared_mutex> lock{mtx_};
    return std::invoke(std::forward<decltype(f)>(f), value_);
  }

  template <typename Func>
  auto with_lock(Func &&f) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return std::invoke(std::forward<decltype(f)>(f), value_);
  }

 protected:
  T value_;
  mutable std::shared_mutex mtx_;
};

template <typename T>
class CV : public Mtx<T> {
 public:
  auto notify_one() { return cv_.notify_one(); }
  auto notify_all() { return cv_.notify_all(); }

  template <typename Func>
  auto wait(Func &&func) {
    auto lock = std::unique_lock{this->mtx_};
    return cv_.wait(lock,
                    [&, this] { return std::invoke(func, this->value_); });
  }

  template <typename Func>
  auto wait(Func &&func, std::stop_token stop) {
    auto lock = std::unique_lock{this->mtx_};
    return cv_.wait(lock, stop,
                    [&, this] { return std::invoke(func, this->value_); });
  }

 private:
  std::condition_variable_any cv_;
};

struct Awaiter {
 public:
  Awaiter() = default;
  ~Awaiter() {
    if (handle_) {
      handle_.destroy();
    }
  }

  template <typename... Args>
  Awaiter(std::coroutine_handle<Args...> handle) : handle_(handle) {}

  Awaiter(const Awaiter &) = delete;
  Awaiter(Awaiter &&rhs) {
    if (&rhs == this) {
      return;
    }

    if (handle_) {
      handle_.destroy();
    }
    handle_ = rhs.handle_;
    rhs.handle_ = nullptr;
  }

  auto resume() {
    handle_();
    handle_ = nullptr;
  }

 private:
  std::coroutine_handle<> handle_ = nullptr;
};

template <typename T>
class SafeQueue {
 public:
  auto Push(const T value) {
    queue_.with_lock([&](auto &queue) { queue.push(std::move(value)); });
  }

  auto Pop() {
    std::optional<T> ret;
    queue_.with_lock([&](auto &queue) {
      if (!queue.empty()) {
        ret.emplace(queue.front());
        queue.pop();
      }
    });

    return ret;
  }

  auto WaitForOne(std::stop_token stop) {
    queue_.wait([](auto &queue) { return queue.empty(); }, stop);
  }

 private:
  CV<std::queue<T>> queue_;
};

int main(int argc, char **argv) {
  Awaiter awaiter;
  {
    SafeQueue<int> queue;
    queue.Push(1);
    auto v = queue.Pop();
    queue.WaitForOne({});
  }
  return 0;
}