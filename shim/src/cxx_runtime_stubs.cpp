// Stubs for C++ runtime symbols the NT firmware does not provide.
// Validated via applets/aeabi_probe.cpp deployments: firmware loads .o
// files but does not bundle libgcc / libsupc++ / libstdc++. Plugin author
// must satisfy any C++ runtime reference via partial-link (libgcc helpers)
// or in-tree stubs (this file).
//
// Scope:
//   __aeabi_atexit, __dso_handle: trivial. NT plug-ins never tear down;
//     the firmware re-loads the binary fresh, so destructor registration
//     is a no-op.
//   _Znwj (operator new(size_t)): NT plug-ins do all allocation via the
//     SDK's pre-allocated SRAM region (placement-new). Any call to plain
//     `new` is a bug. Provide a stub that returns nullptr to satisfy the
//     linker; downstream code is expected to never reach it.
//   _ZdlPv (operator delete(void*)): paired with _Znwj. No-op.
//   _ZSt25__throw_bad_function_callv: invoked when a null std::function is
//     called. dep-clock-mgr uses std::function for BeatSync callbacks; the
//     bad-call path is unreachable in our use because all queued functions
//     are constructed with valid callables before being run. Provide an
//     infinite-loop stub that hangs the plug-in if reached, surfacing the
//     bug instead of silent corruption.

#include <cstddef>

extern "C" {

// ARM C++ ABI: per-object destructor registration. Returns 0 on success.
int __aeabi_atexit(void* /*object*/, void (* /*destructor*/)(void*),
                   void* /*dso_handle*/) {
    return 0;
}

// CRT-provided normally; missing on bare-metal target without CRT0.
void* __dso_handle = nullptr;

}  // extern "C"

// Operator new / delete. Required by C++ ABI any time the `new` keyword is
// used (vendor applets may have legitimate uses; we route to nullptr so the
// linker resolves and downstream code surfaces the bug at first use).
#if defined(__arm__)
// operator new stub for bare-metal arm. Returns nullptr; any call is a
// bug surfaced as a null-pointer dereference downstream. The host build
// links the real libstdc++ operator new (Catch2 + STL containers in the
// harness require it), so guard the override.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnew-returns-null"
void* operator new(std::size_t)                                { return nullptr; }
void* operator new[](std::size_t)                              { return nullptr; }
#pragma GCC diagnostic pop
#endif
// operator delete already provided in shim/src/globals.cpp (Phase 4
// baseline). cxx_runtime_stubs.cpp only adds the missing-from-Phase-4
// piece (operator new on arm).

#if defined(__arm__)
namespace std {
// Stand-in for std::__throw_bad_function_call on bare-metal arm where
// libstdc++ is not linked. Host build links libc++/libstdc++ which already
// provides this; guard so we do not collide.
[[noreturn]] void __throw_bad_function_call() {
    while (true) { /* spin */ }
}
}  // namespace std
#endif
