#ifndef INCLUDE_DEFER_HPP
#define INCLUDE_DEFER_HPP

template <typename F>
struct Defer{
    F f;
    Defer(F _f) : f(_f) {}
    ~Defer(){  f(); }
};


template <typename F>
Defer(F) -> Defer<F>;

#define DEFER_JOIN(x, y) x##y
#define DEFER_NAME(n) DEFER_JOIN(defer_object, n)
#define defer(code) Defer DEFER_NAME(__LINE__) {[&]() code }

#endif