#ifndef KJ_ASYNC_WIN_H_
#define KJ_ASYNC_WIN_H_

#include "async.h"

namespace kj {

class WinEventPort : public EventPort {
public:
  WinEventPort() = default;
  ~WinEventPort() = default;

  Promise<void> waitFor(void* handle);

  void wait() override;
  void poll() override;

private:
  class HandlePromiseAdapter;
  class WaitHandleContext;

  HandlePromiseAdapter* handleHead = nullptr;
  HandlePromiseAdapter** handleTail = &handleHead;
};

}  // namespace kj

#endif // KJ_ASYNC_UNIX_H_