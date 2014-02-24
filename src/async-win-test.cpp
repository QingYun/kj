#include "async-win.h"
#include "thread.h"
#include <windows.h>
#include <gtest/gtest.h>

namespace kj {

inline void delay() { Sleep(10); }

TEST(AsyncWinTest, WaitFor) {
  WinEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  HANDLE event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  KJ_DEFER({ CloseHandle(event); });
  SetEvent(event);

  EXPECT_TRUE(port.waitFor(event).then([]() { return true; }).wait(waitScope));
}

TEST(AsyncWinTest, WaitForMultiListen) {
  WinEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  HANDLE bogusEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  KJ_DEFER({ CloseHandle(bogusEvent); });

  port.waitFor(bogusEvent)
      .then([]() { ADD_FAILURE() << "wrong handle"; })
      .detach([](Exception&& exception) {
         ADD_FAILURE() << str(exception).cStr();
       });

  HANDLE event = CreateEvent(nullptr, TRUE, FALSE, false);
  KJ_DEFER({ CloseHandle(event); });
  SetEvent(event);

  EXPECT_TRUE(port.waitFor(event).then([]() { return true; }).wait(waitScope));
}

TEST(AsyncWinTest, WaitForMultiReceive) {
  WinEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  HANDLE event1 = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  KJ_DEFER({ CloseHandle(event1); });
  SetEvent(event1);

  HANDLE event2 = CreateEvent(nullptr, TRUE, FALSE, false);
  KJ_DEFER({ CloseHandle(event2); });
  SetEvent(event2);

  EXPECT_TRUE(port.waitFor(event1).then([]() { return true; }).wait(waitScope));
  EXPECT_TRUE(port.waitFor(event2).then([]() { return true; }).wait(waitScope));
}

TEST(AsyncWinTest, WaitForAsync) {
  WinEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  HANDLE event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  KJ_DEFER({ CloseHandle(event); });
  Thread t([&]() {
    delay();
    SetEvent(event);
  });

  EXPECT_TRUE(port.waitFor(event).then([]() { return true; }).wait(waitScope));
}

TEST(AsyncWinTest, WaitForNoWait) {
  WinEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  HANDLE event1 = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  KJ_DEFER({ CloseHandle(event1); });

  HANDLE event2 = CreateEvent(nullptr, TRUE, FALSE, false);
  KJ_DEFER({ CloseHandle(event2); });

  int receivedCount = 0;
  port.waitFor(event1).then([&]() { receivedCount++; }).detach([](
      Exception&& e) { ADD_FAILURE() << str(e).cStr(); });
  port.waitFor(event2).then([&]() { receivedCount++; }).detach([](
      Exception&& e) { ADD_FAILURE() << str(e).cStr(); });

  SetEvent(event1);
  SetEvent(event2);

  EXPECT_EQ(0, receivedCount);

  loop.run();

  EXPECT_EQ(0, receivedCount);

  port.poll();

  EXPECT_EQ(0, receivedCount);

  loop.run();

  EXPECT_EQ(2, receivedCount);
}

}  // namespace kj