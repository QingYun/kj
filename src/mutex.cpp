// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "mutex.h"
#include "debug.h"

#if KJ_USE_FUTEX
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <limits.h>
#endif

namespace kj {
namespace _ {  // private

// =======================================================================================
// Generic pthreads-based implementation

Mutex::Mutex() {}
Mutex::~Mutex() {}

void Mutex::lock(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      mutex.lock();
      break;
    case SHARED:
      mutex.lock_read();
      break;
  }
}

void Mutex::unlock(Exclusivity exclusivity) {
  mutex.unlock();
}

void Mutex::assertLockedByCaller(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      // A read lock should fail if the mutex is already held for writing.
      if (mutex.try_lock_read()) {
        mutex.unlock();
        KJ_FAIL_ASSERT("Tried to call getAlreadyLocked*() but lock is not held.");
      }
      break;
    case SHARED:
      // A write lock should fail if the mutex is already held for reading or writing.  We don't
      // have any way to prove that the lock is held only for reading.
      if (mutex.try_lock()) {
        mutex.unlock();
        KJ_FAIL_ASSERT("Tried to call getAlreadyLocked*() but lock is not held.");
      }
      break;
  }
}

Once::Once(bool startInitialized)
    : state(startInitialized ? INITIALIZED : UNINITIALIZED) {}
Once::~Once() {}

void Once::runOnce(Initializer& init) {
  mutex.lock();
  KJ_DEFER(mutex.unlock());

  if (state != UNINITIALIZED) {
    return;
  }

  init.run();

  state.store(INITIALIZED, std::memory_order_release);
}

void Once::reset() {
  State oldState = INITIALIZED;
  if (!state.compare_exchange_strong(oldState, UNINITIALIZED,
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
    KJ_REQUIRE(oldState == DISABLED, "reset() called while not initialized.");
  }
}

void Once::disable()  {
  mutex.lock();
  KJ_DEFER(mutex.unlock());

  state.store(DISABLED, std::memory_order_release);
}

}  // namespace _ (private)
}  // namespace kj
