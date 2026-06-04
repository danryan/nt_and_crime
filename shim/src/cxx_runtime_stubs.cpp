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
#if defined(NT_HEM_NEED_HEAP_ARENA)
// Bump-allocator operator new for the few applets whose vendor source uses the
// `new` keyword for a one-time static singleton (e.g. ASR's RingBufferManager,
// CVRecV2's recorder buffers). NT plug-ins have no heap, and the default stub
// below returns nullptr which hard-faults the device when the result is
// dereferenced at add-algorithm time. A TU opts in by defining
// NT_HEM_NEED_HEAP_ARENA before the shim aggregation; it then gets a fixed
// static arena (plug-in .bss). Allocation is one-shot at construct; operator
// delete is the existing no-op, so nothing is ever freed (acceptable: the
// instances live for the plug-in's lifetime). Out-of-arena returns nullptr,
// preserving the original surfacing behavior if the budget is exceeded.
#include <cstdint>
namespace {
constexpr std::size_t kHeapArenaBytes = 4096;
alignas(8) unsigned char g_heap_arena[kHeapArenaBytes];
std::size_t g_heap_arena_used = 0;
void* heap_arena_alloc(std::size_t n) {
    std::size_t aligned = (n + 7u) & ~std::size_t(7);
    if (g_heap_arena_used + aligned > kHeapArenaBytes) return nullptr;
    void* p = g_heap_arena + g_heap_arena_used;
    g_heap_arena_used += aligned;
    return p;
}
}  // namespace
void* operator new(std::size_t n)                              { return heap_arena_alloc(n); }
void* operator new[](std::size_t n)                            { return heap_arena_alloc(n); }
#else
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
#endif
// operator delete is provided in shim/src/globals.cpp.
// cxx_runtime_stubs.cpp adds the missing operator new on arm.

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
