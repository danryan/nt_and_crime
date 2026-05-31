// Shim shadow of vendor util/util_sync.h.
//
// Defines the vendor include guard UTIL_SYNC_H_ (poison): when this header is
// pulled before the vendor sibling in an O_C app TU, the vendor util_sync.h
// self-suppresses and this portable implementation is used instead. The vendor
// header is not host-portable: it #includes <arm_math.h> and implements its
// lock with the ARM CMSIS exclusive-access intrinsics (__LDREXW / __STREXW /
// __DMB / __CLREX), none of which exist on the host compiler.
//
// The shim runs the vendor isr cadence inside step() on the audio thread; there
// is no preemptive ISR racing the main loop the way there is on the Teensy. A
// plain non-atomic lock therefore has the same observable behavior as the
// vendor exclusive-access lock on both host and ARM: TryLock always succeeds
// when the lock is free and reports failure when already held within the same
// call chain (the AutomatonnetzState ISR uses TryLock<CRITICAL_SECTION_ID_ISR>
// only to skip a grid update while ClearGrid holds the lock, which never
// overlaps under the single-threaded shim model).
//
// The public interface mirrors the vendor header exactly (Init / Unlock / Lock
// / TryLock plus the scoped TryLock<id> / Lock<id> templates with locked());
// only the implementation differs.

#ifndef UTIL_SYNC_H_
#define UTIL_SYNC_H_

#include <cstdint>

#include "util_macros.h"

namespace util {

class CriticalSection {
public:
  CriticalSection() { }
  void Init() { id_ = 0; }

  void Unlock() { id_ = 0; }

  void Lock(uint32_t id) { id_ = id; }

  // Non-blocking: claim the lock if free, otherwise report failure.
  bool TryLock(uint32_t id) {
    if (id_) return false;
    id_ = id;
    return true;
  }

private:
  volatile uint32_t id_ = 0;
  DISALLOW_COPY_AND_ASSIGN(CriticalSection);
};

// Automatic scoped TryLock.
template <uint32_t id>
class TryLock {
public:
  TryLock(CriticalSection &critical_section)
  : critical_section_(critical_section)
  , locked_(critical_section.TryLock(id)) {
  }

  ~TryLock() {
    if (locked_)
      critical_section_.Unlock();
  }

  bool locked() const { return locked_; }

private:
  CriticalSection &critical_section_;
  const bool locked_;

  DISALLOW_COPY_AND_ASSIGN(TryLock);
};

// Automatic scoped Lock.
template <uint32_t id>
class Lock {
public:
  Lock(CriticalSection &critical_section)
  : critical_section_(critical_section) {
    critical_section.Lock(id);
  }

  ~Lock() { critical_section_.Unlock(); }

private:
  CriticalSection &critical_section_;
  DISALLOW_COPY_AND_ASSIGN(Lock);
};

}  // namespace util

#endif  // UTIL_SYNC_H_
