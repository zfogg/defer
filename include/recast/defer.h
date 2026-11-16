/**
 * @file recast/defer.h
 * @brief Recast's defer transformation interface.
 *
 * The defer source spellings remain unchanged by design.
 */
#pragma once

#if defined(__cplusplus) && (!defined(__OBJC__) || (defined(OBJC_ZEROCOST_EXCEPTIONS) && OBJC_ZEROCOST_EXCEPTIONS))
extern "C++" {
namespace recast_detail {
template <typename F> class guard {
public:
  explicit guard(F &&action) : action_(static_cast<F &&>(action)), active_(true) {}
  guard(guard &&other) : action_(static_cast<F &&>(other.action_)), active_(other.active_) { other.active_ = false; }
  guard(const guard &) = delete;
  guard &operator=(const guard &) = delete;
  ~guard() noexcept { if (active_) action_(); }
  void cancel() noexcept { active_ = false; }
private:
  F action_;
  bool active_;
};
template <typename F> guard<F> make_guard(F action) { return guard<F>(static_cast<F &&>(action)); }
} // namespace recast_detail
} // extern "C++"
#define recast_defer_join_impl(a, b) a##b
#define recast_defer_join(a, b) recast_defer_join_impl(a, b)
#define defer(...) auto recast_defer_join(__defer_guard_, __COUNTER__) = ::recast_detail::make_guard([&]() { __VA_ARGS__; })
#define defer_named(name, ...) auto name = ::recast_detail::make_guard([&]() { __VA_ARGS__; })
#define defer_cancel(name) (name).cancel()
#elif defined(RECAST_DEFER_PARSING)
void __recast_defer_marker(void);
#define defer(...) do { __recast_defer_marker(); } while (0)
#define defer_named(name, ...) do { __recast_defer_marker(); } while (0)
#define defer_cancel(name) do { __recast_defer_marker(); } while (0)
#else
#define defer(...) do { } while (0)
#define defer_named(name, ...) do { } while (0)
#define defer_cancel(name) do { } while (0)
#endif

/* Keep the historical lowercase spellings available to C sources that include
 * the namespaced compatibility header. */
#define defer(...) DEFER(__VA_ARGS__)
#define defer_named(name, ...) DEFER_NAMED(name, __VA_ARGS__)
#define defer_cancel(name) DEFER_CANCEL(name)
