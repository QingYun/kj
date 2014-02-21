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

#include "common.h"
#include <gtest/gtest.h>

namespace kj {
namespace {

TEST(Common, Size) {
  int arr[] = {12, 34, 56, 78};

  size_t expected = 0;
  for (size_t i: indices(arr)) {
    EXPECT_EQ(expected++, i);
  }
  EXPECT_EQ(4u, expected);
}

TEST(Common, Maybe) {
  {
    Maybe<int> m = 123;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(123, m.orDefault(456));
  }

  {
    Maybe<int> m = nullptr;
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  int i = 234;
  {
    Maybe<int&> m = i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int&> m = nullptr;
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  {
    Maybe<int&> m = &i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int&> m = implicitCast<int*>(nullptr);
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  {
    Maybe<int> m = &i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_NE(v, &i);
      EXPECT_EQ(234, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_NE(v, &i);
      EXPECT_EQ(234, *v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<int> m = implicitCast<int*>(nullptr);
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
  }
}

TEST(Common, MaybeConstness) {
  int i;

  Maybe<int&> mi = i;
  const Maybe<int&> cmi = mi;
//  const Maybe<int&> cmi2 = cmi;    // shouldn't compile!  Transitive const violation.

  KJ_IF_MAYBE(i2, cmi) {
    EXPECT_EQ(&i, i2);
  } else {
    ADD_FAILURE();
  }

  Maybe<const int&> mci = mi;
  const Maybe<const int&> cmci = mci;
  const Maybe<const int&> cmci2 = cmci;

  KJ_IF_MAYBE(i2, cmci2) {
    EXPECT_EQ(&i, i2);
  } else {
    ADD_FAILURE();
  }
}

class Foo {
public:
  KJ_DISALLOW_COPY(Foo);
  virtual ~Foo() {}
protected:
  Foo() = default;
};

class Bar: public Foo {
public:
  Bar() = default;
  KJ_DISALLOW_COPY(Bar);
  virtual ~Bar() {}
};

class Baz: public Foo {
public:
  Baz() = delete;
  KJ_DISALLOW_COPY(Baz);
  virtual ~Baz() {}
};

TEST(Common, Downcast) {
  Bar bar;
  Foo& foo = bar;

  EXPECT_EQ(&bar, &downcast<Bar>(foo));
#if defined(KJ_DEBUG) && !KJ_NO_RTTI
#if KJ_NO_EXCEPTIONS
#ifdef KJ_DEBUG
  EXPECT_DEATH_IF_SUPPORTED(downcast<Baz>(foo), "Value cannot be downcast");
#endif
#else
  EXPECT_ANY_THROW(downcast<Baz>(foo));
#endif
#endif

#if KJ_NO_RTTI
  EXPECT_TRUE(dynamicDowncastIfAvailable<Bar>(foo) == nullptr);
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz>(foo) == nullptr);
#else
  KJ_IF_MAYBE(m, dynamicDowncastIfAvailable<Bar>(foo)) {
    EXPECT_EQ(&bar, m);
  } else {
    ADD_FAILURE() << "Dynamic downcast returned null.";
  }
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz>(foo) == nullptr);
#endif
}

TEST(Common, MinMax) {
  EXPECT_EQ(5, kj::min(5, 9));
  EXPECT_EQ(5, kj::min(9, 5));
  EXPECT_EQ(5, kj::min(5, 5));
  EXPECT_EQ(9, kj::max(5, 9));
  EXPECT_EQ(9, kj::max(9, 5));
  EXPECT_EQ(5, kj::min(5, 5));

  // Hey look, we can handle the types mismatching.  Eat your heart out, std.
  EXPECT_EQ(5, kj::min(5, 'a'));
  EXPECT_EQ(5, kj::min('a', 5));
  EXPECT_EQ('a', kj::max(5, 'a'));
  EXPECT_EQ('a', kj::max('a', 5));

  EXPECT_EQ('a', kj::min(1234567890123456789ll, 'a'));
  EXPECT_EQ('a', kj::min('a', 1234567890123456789ll));
  EXPECT_EQ(1234567890123456789ll, kj::max(1234567890123456789ll, 'a'));
  EXPECT_EQ(1234567890123456789ll, kj::max('a', 1234567890123456789ll));
}

TEST(Common, MinMaxValue) {
  EXPECT_EQ(0x7f, int8_t(maxValue));
  EXPECT_EQ(0xffu, uint8_t(maxValue));
  EXPECT_EQ(0x7fff, int16_t(maxValue));
  EXPECT_EQ(0xffffu, uint16_t(maxValue));
  EXPECT_EQ(0x7fffffff, int32_t(maxValue));
  EXPECT_EQ(0xffffffffu, uint32_t(maxValue));
  EXPECT_EQ(0x7fffffffffffffffll, int64_t(maxValue));
  EXPECT_EQ(0xffffffffffffffffull, uint64_t(maxValue));

  EXPECT_EQ(-0x80, int8_t(minValue));
  EXPECT_EQ(0, uint8_t(minValue));
  EXPECT_EQ(-0x8000, int16_t(minValue));
  EXPECT_EQ(0, uint16_t(minValue));
  EXPECT_EQ(-0x80000000ll, int32_t(minValue));
  EXPECT_EQ(0, uint32_t(minValue));
  EXPECT_EQ(-0x8000000000000000ll, int64_t(minValue));
  EXPECT_EQ(0, uint64_t(minValue));

  double f = inf();
  EXPECT_TRUE(f * 2 == f);

  f = nan();
  EXPECT_FALSE(f == f);
}

TEST(Common, Defer) {
  uint i = 0;
  uint j = 1;
  bool k = false;

  {
    KJ_DEFER(++i);
    KJ_DEFER(j += 3; k = true);
    EXPECT_EQ(0u, i);
    EXPECT_EQ(1u, j);
    EXPECT_FALSE(k);
  }

  EXPECT_EQ(1u, i);
  EXPECT_EQ(4u, j);
  EXPECT_TRUE(k);
}

TEST(Common, CanConvert) {
  static_assert(CanConvert<long, int>::value, "failure");
  static_assert(!CanConvert<long, void*>::value, "failure");

  struct Super {};
  struct Sub: public Super {};

  static_assert(CanConvert<Sub, Super>::value, "failure");
  static_assert(!CanConvert<Super, Sub>::value, "failure");
  static_assert(CanConvert<Sub*, Super*>::value, "failure");
  static_assert(!CanConvert<Super*, Sub*>::value, "failure");

  static_assert(CanConvert<void*, const void*>::value, "failure");
  static_assert(!CanConvert<const void*, void*>::value, "failure");
}

}  // namespace
}  // namespace kj
