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

#include "thread.h"
#include "debug.h"
#include <Windows.h>

namespace kj {

Thread::Thread(Function<void()> func): func(kj::mv(func)) {
  threadHandle = CreateThread(NULL, 0, runThread, this, 0, NULL);
  if (threadHandle == NULL) {
    KJ_FAIL_SYSCALL("CreateThread", GetLastError());
  }
}

Thread::~Thread() {
  if (!detached) {
    auto pthreadResult = WaitForSingleObject(threadHandle, INFINITE);
    if (pthreadResult != WAIT_OBJECT_0) {
      KJ_FAIL_SYSCALL("WaitForSingleObject", GetLastError(), pthreadResult) { break; }
    }

    KJ_IF_MAYBE(e, exception) {
      kj::throwRecoverableException(kj::mv(*e));
    }
  }
}

void Thread::detach() {
  CloseHandle(threadHandle);
  detached = true;
}

DWORD CALLBACK Thread::runThread(void* ptr) {
  Thread* thread = reinterpret_cast<Thread*>(ptr);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    thread->func();
  })) {
    thread->exception = kj::mv(*exception);
  }
  return 0;
}

}  // namespace kj
