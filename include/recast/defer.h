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
#define RECAST_JOIN_IMPL(a, b) a##b
#define RECAST_JOIN(a, b) RECAST_JOIN_IMPL(a, b)
#define DEFER(...) auto RECAST_JOIN(__defer_guard_, __COUNTER__) = ::recast_detail::make_guard([&]() { __VA_ARGS__; })
#define DEFER_NAMED(name, ...) auto name = ::recast_detail::make_guard([&]() { __VA_ARGS__; })
#define DEFER_CANCEL(name) (name).cancel()
#elif defined(RECAST_DEFER_PARSING)
void __recast_defer_marker(void);
#define DEFER(...) do { __recast_defer_marker(); } while (0)
#define DEFER_NAMED(name, ...) do { __recast_defer_marker(); } while (0)
#define DEFER_CANCEL(name) do { __recast_defer_marker(); } while (0)
#else
#define DEFER(...) _Static_assert(0, "DEFER() requires the source transformation")
#define DEFER_NAMED(name, ...) _Static_assert(0, "DEFER_NAMED() requires the source transformation")
#define DEFER_CANCEL(name) _Static_assert(0, "DEFER_CANCEL() requires the source transformation")
#endif
