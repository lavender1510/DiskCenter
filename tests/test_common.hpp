#pragma once
#include "core.hpp"
#include <cstdlib>
#include <iostream>

inline void expect(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED " << what << '\n'; std::exit(1); }
}

template <class Function>
void expect_error(dc::Error error, Function run, const char* what) {
    try { run(); }
    catch (const dc::Failure& failure) { expect(failure.code() == error, what); return; }
    expect(false, what);
}
