// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Task<T>: what `async function … : Promise<T>` is in the original.
 *
 *     Task<Bytes> read(std::string path) {            // async function read(path)
 *       Response r = co_await plat::fetch(path);      //   const r = await fetch(path)
 *       co_return r.body;                             //   return r.body
 *     }
 *
 * Like a JavaScript promise, a task starts running when it is called and runs on
 * whether or not anybody waits for it; it may be awaited any number of times, also
 * after it has finished.  Everything happens on one thread: a task that waits is
 * resumed from the host's event loop (the browser's, or plat::pump() natively).
 *
 * Parameters of a coroutine live in its frame: take them BY VALUE (a reference to a
 * caller's temporary is gone after the first co_await).
 *
 * Resolver<T> is the other end, for wrapping callbacks (`new Promise(resolve => …)`):
 *
 *     Resolver<int> r;  later(…, [r] { r.resolve(42); });  int v = co_await r.task();
 */
#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace fs {

namespace detail {

struct TaskStateBase {
  bool done = false;
  std::exception_ptr error;
  /** who waits for it (resumed in the order they began to wait) */
  std::vector<std::coroutine_handle<>> waiting;

  void finish() {
    done = true;
    std::vector<std::coroutine_handle<>> w;
    w.swap(waiting);
    for (auto h : w) h.resume();
  }
};

template <class T>
struct TaskState : TaskStateBase {
  std::optional<T> value;
};
template <>
struct TaskState<void> : TaskStateBase {};

/** called when a task nobody waits for ends with an exception (logs it) */
void taskFailedUnobserved(std::exception_ptr e);

}  // namespace detail

template <class T = void>
class [[nodiscard]] Task {
 public:
  using State = detail::TaskState<T>;

  struct promise_type;
  using Handle = std::coroutine_handle<promise_type>;

  Task() = default;
  explicit Task(std::shared_ptr<State> s) : state_(std::move(s)) {}

  /** has it finished (with a value or an exception)? */
  bool done() const { return state_ && state_->done; }
  bool valid() const { return static_cast<bool>(state_); }
  /** let it run on without waiting for it (`void f()` in the original) */
  void detach() const {}

  struct Awaiter {
    std::shared_ptr<State> s;
    bool take;
    bool await_ready() const noexcept { return s->done; }
    void await_suspend(std::coroutine_handle<> h) { s->waiting.push_back(h); }
    T await_resume() {
      if (s->error) std::rethrow_exception(s->error);
      if constexpr (!std::is_void_v<T>) {
        if (take) return std::move(*s->value);
        return *s->value;
      }
    }
  };
  /** `co_await f()` takes the value; `co_await kept` (a task that is kept) copies it */
  Awaiter operator co_await() && { return Awaiter{state_, true}; }
  Awaiter operator co_await() const& { return Awaiter{state_, false}; }

  /** the value of a finished task (tests, and code that polls) */
  template <class U = T>
  const U& get() const
    requires(!std::is_void_v<U>)
  {
    if (state_->error) std::rethrow_exception(state_->error);
    return *state_->value;
  }

 private:
  std::shared_ptr<State> state_;
};

namespace detail {

template <class T>
struct TaskPromiseBase {
  std::shared_ptr<TaskState<T>> state = std::make_shared<TaskState<T>>();

  std::suspend_never initial_suspend() noexcept { return {}; }
  struct Final {
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) const noexcept {
      // the frame goes first (the state lives on in the tasks that hold it), then those
      // who wait are resumed
      std::shared_ptr<TaskState<T>> s = h.promise().state;
      h.destroy();
      bool unobserved = s->error && s->waiting.empty() && s.use_count() == 1;
      std::exception_ptr e = s->error;
      s->finish();
      if (unobserved) taskFailedUnobserved(e);
    }
    void await_resume() const noexcept {}
  };
  Final final_suspend() noexcept { return {}; }
  void unhandled_exception() { state->error = std::current_exception(); }
};

}  // namespace detail

template <class T>
struct Task<T>::promise_type : detail::TaskPromiseBase<T> {
  Task<T> get_return_object() { return Task<T>(this->state); }
  template <class U>
  void return_value(U&& v) {
    this->state->value.emplace(std::forward<U>(v));
  }
};

template <>
struct Task<void>::promise_type : detail::TaskPromiseBase<void> {
  Task<void> get_return_object() { return Task<void>(this->state); }
  void return_void() {}
};

/** The other end of a task: resolve it later, from a callback. */
template <class T = void>
class Resolver {
 public:
  Resolver() : state_(std::make_shared<detail::TaskState<T>>()) {}
  Task<T> task() const { return Task<T>(state_); }
  bool done() const { return state_->done; }

  template <class U = T>
  void resolve(U v) const
    requires(!std::is_void_v<T>)
  {
    if (state_->done) return;
    state_->value.emplace(std::move(v));
    state_->finish();
  }
  template <class U = T>
  void resolve() const
    requires(std::is_void_v<U>)
  {
    if (state_->done) return;
    state_->finish();
  }
  void reject(std::exception_ptr e) const {
    if (state_->done) return;
    state_->error = e;
    state_->finish();
  }

 private:
  std::shared_ptr<detail::TaskState<T>> state_;
};

/** A task that is already finished (`Promise.resolve(v)`). */
template <class T>
Task<std::decay_t<T>> resolved(T&& v) {
  Resolver<std::decay_t<T>> r;
  r.resolve(std::forward<T>(v));
  return r.task();
}
inline Task<void> resolved() {
  Resolver<void> r;
  r.resolve();
  return r.task();
}

/** `await Promise.all(tasks)`: they all run already; this waits for each in turn. */
template <class T>
Task<std::vector<T>> whenAll(std::vector<Task<T>> tasks) {
  std::vector<T> out;
  out.reserve(tasks.size());
  for (auto& t : tasks) out.push_back(co_await std::move(t));
  co_return out;
}
inline Task<void> whenAll(std::vector<Task<void>> tasks) {
  for (auto& t : tasks) co_await std::move(t);
}

}  // namespace fs
