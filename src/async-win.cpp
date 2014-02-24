#include "async-win.h"
#include "vector.h"
#include "debug.h"
#include <Windows.h>

namespace kj {

class WinEventPort::HandlePromiseAdapter {
 public:
  inline HandlePromiseAdapter(PromiseFulfiller<void>& fulfiller,
    WinEventPort& loop, PromiseFulfiller<void>&, HANDLE handle)
      : loop(loop), fulfiller(fulfiller), handle(handle) {
    prev = loop.handleTail;
    *loop.handleTail = this;
    loop.handleTail = &next;
  }

  ~HandlePromiseAdapter() {
    if (prev != nullptr) {
      if (next == nullptr) {
        loop.handleTail = prev;
      } else {
        next->prev = prev;
      }
      *prev = next;
    }
  }

  void removeFromList() {
    if (next == nullptr) {
      loop.handleTail = prev;
    }
    else {
      next->prev = prev;
    }
    *prev = next;
    next = nullptr;
    prev = nullptr;
  }

  WinEventPort& loop;
  HANDLE handle;
  PromiseFulfiller<void>& fulfiller;
  HandlePromiseAdapter* next = nullptr;
  HandlePromiseAdapter** prev = nullptr;
};

class WinEventPort::WaitHandleContext {
public:
  WaitHandleContext(HandlePromiseAdapter* ptr) {
    while(ptr != nullptr) {
      handles.add(ptr->handle);
      handleEvents.add(ptr);
      ptr = ptr->next;
    }
  }

  void run(DWORD timeout) {
    auto result =
        WaitForMultipleObjects(handles.size(), handles.begin(), FALSE, timeout);

    if (WAIT_FAILED == result)
      KJ_FAIL_SYSCALL("WaitForMultipleObjects()", GetLastError());

    if (WAIT_TIMEOUT == result)
      return ;

    auto index = result - WAIT_OBJECT_0;

    if (index >= handles.size())
      return ;

    handleEvents[index]->fulfiller.fulfill();
    handleEvents[index]->removeFromList();

    for (auto i = index + 1; i < handles.size(); i++) {
      if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) {
        handleEvents[i]->fulfiller.fulfill();
        handleEvents[i]->removeFromList();
      }
    }
  }

private:
  Vector<HANDLE> handles;
  Vector<HandlePromiseAdapter*> handleEvents;
};

Promise<void> WinEventPort::waitFor(HANDLE handle) {
  return newAdaptedPromise<void, HandlePromiseAdapter>(*this, handle);
}

void WinEventPort::wait() {
  WaitHandleContext waitHandleContext{handleHead};
  waitHandleContext.run(INFINITE);
}

void WinEventPort::poll() {
  WaitHandleContext waitHandleContext{ handleHead };
  waitHandleContext.run(0);
}

}  // namespace kj