kj
==

capnproto.kj ported to VS2013


文档
===

Own
---

一个独占所有权的智能指针, 内部持有对象指针和删除器指针. 

除了模拟指针的接口之外, `Own`还提供了一个`downcast<U>`方法, 它会将自己持有的指针转换成子类`U`的指针然后返回`Own<U>`, 需要注意的是这里也是一次所有权的转换, 之前自己持有的对象现在归返回的`Own<U>`所有了, 自己就不再持有对象了.

###删除器

删除器`Disposer`是单独的一个继承体系, 定义了如何销毁`Own`持有的对象指针指向的对象.

```
class Disposer {
protected:
  virtual void disposeImpl(void* pointer) const = 0;
  
public:
  template <typename T>
  void dispose(T* object) const;
};
```

子类需要重写`disposeImpl`来实现自己的销毁对象的方式, `dispose`在将指针转发到`disposeImpl`时会根据对象是否是多态类型来选择是用`dynamic_cast<void*>`还是`static_cast<void*>`进行指针的类型转换. `dynamic_cast<void*>`是不需要RTTI的, 因为当前父类子对象到外层的子类对象顶端的偏移就在虚表里.

另外还有两个预定义删除器: 

- `DestructorOnlyDisposer`: 只析构, 不释放
- `NullDisposer`: 什么都不做

这两个预定义删除器都以一个自己类型的静态数据成员`instance`, 在构造`Own`的时候可以直接传这个`instance`的指针.

###其他

```
template <typename T, typename... Params>
Own<T> heap(Params&&... params) {
  return Own<T>(new T(kj::fwd<Params>(params)...), _::HeapDisposer<T>::instance);
}

template <typename T>
Own<Decay<T>> heap(T&& orig) {
  typedef Decay<T> T2;
  return Own<T2>(new T2(kj::fwd<T>(orig)), _::HeapDisposer<T2>::instance);
}
```

`heap`简化了在堆上构造一个新对象的过程.

另外还有一个叫`SpaceFor<T>`的类, 它会在对象内部留一个`T`对象大小的空间, 但是不初始化, 直到等到用户调用`construct(Params&&... params)`方法时才会用给出的参数将内部的空间初始化成`T`对象然后返回一个`Own<T>`

Defer
---

注册一段在退出作用域时会被执行的代码

```
namespace _ {  // private

template <typename Func>
class Deferred {
public:
  inline Deferred(Func func): func(func), canceled(false) {}
  inline ~Deferred() { if (!canceled) func(); }
  KJ_DISALLOW_COPY(Deferred);

  // This move constructor is usually optimized away by the compiler.
  inline Deferred(Deferred&& other): func(kj::mv(other.func)), canceled(false) {
    other.canceled = true;
  }
private:
  Func func;
  bool canceled;
};

}  // namespace _ (private)

template <typename Func>
_::Deferred<Decay<Func>> defer(Func&& func) {
  return _::Deferred<Decay<Func>>(kj::fwd<Func>(func));
}

#define KJ_DEFER(code) auto KJ_UNIQUE_NAME(_kjDefer) = ::kj::defer([&](){code;})
```

异常
---

###Exception

`kj::Exception`是一个能携带很多信息的异常类, 数据成员有:

```
enum class Nature {
    // What kind of failure?  This is informational, not intended for programmatic use.
    
    PRECONDITION,
    LOCAL_BUG,
    OS_ERROR,
    NETWORK_FAILURE,
    OTHER
};

enum class Durability {
    PERMANENT,  // Retrying the exact same operation will fail in exactly the same way.
    TEMPORARY,  // Retrying the exact same operation might succeed.
    OVERLOADED  // The error was possibly caused by the system being overloaded.  Retrying the
                // operation might work at a later point in time, but the caller should NOT retry
                // immediately as this will probably exacerbate the problem.
};

String ownFile;
const char* file;
int line;
Nature nature;
Durability durability;
String description;
Maybe<Own<Context>> context;
// 这两个是调用栈信息
void* trace[16];
uint traceCount;
```

`context`包含了额外的上下文信息, 它是一个链表的头节点, 相关代码如下

```
struct Context {
    // Describes a bit about what was going on when the exception was thrown.
    
    const char* file;
    int line;
    String description;
    Maybe<Own<Context>> next;
    
    Context(const char* file, int line, String&& description, Maybe<Own<Context>>&& next)
        : file(file), line(line), description(mv(description)), next(mv(next)) {}
    Context(const Context& other) noexcept;
};

inline Maybe<const Context&> getContext() const {
    KJ_IF_MAYBE(c, context) {
      return **c;
    } else {
      return nullptr;
    }
}

// 加入新的上下文信息, 作为链表的新的头节点
void wrapContext(const char* file, int line, String&& description);
```

###ExceptionCallback

实现了一套新的"异常"处理机制, 这里的异常不是真正的c++异常, 而是表示错误的普通`kj::Exception`对象. 这套机制的核心是`ExceptionCallback`基类

```
class ExceptionCallback {
public:
// ...

  // 在出现可恢复异常时触发
  virtual void onRecoverableException(Exception&& exception);
  // 在出现致命异常时触发
  virtual void onFatalException(Exception&& exception);
  // 需要log debug信息时触发
  virtual void logMessage(const char* file, int line, int contextDepth, String&& text);
  
// ...
};
```

想要实现自己的异常处理, 只需继承这个接口类然后重写这三个事件方法即可. 

除了接口外, 这个基类还有一个`next`数据成员, 用来将所有处理对象串成一个链表, 链表的头部指针是线程局部存储的, 可以使用自由函数`getExceptionCallback()`获得头部节点的引用. `ExceptionCallback`的构造函数会将自己从头部插入并更新头部指针, 析构函数会将自己从链表中删除, "异常"会被传递给头节点进行处理. **这个类的对象必须放在栈上**, 这样才能保证在栈展开时及时从链表中去除不在范围的异常处理对象.

```
ExceptionCallback& getExceptionCallback() {
  static ExceptionCallback::RootExceptionCallback defaultCallback;
  ExceptionCallback* scoped = threadLocalCallback;
  // 如果没有注册过自定义的处理对象则返回兜底处理对象
  return scoped != nullptr ? *scoped : defaultCallback;
}

// 致命异常处理函数的正常返回会导致`abort()`
void throwFatalException(kj::Exception&& exception) {
  getExceptionCallback().onFatalException(kj::mv(exception));
  abort();
}

void throwRecoverableException(kj::Exception&& exception) {
  getExceptionCallback().onRecoverableException(kj::mv(exception));
}
```

兜底异常处理类`ExceptionCallback::RootExceptionCallback`提供了默认的处理实现:

- 对于可恢复异常, 如果当前有一个未捕获的c++异常, 则只是简单的log一下; 如果当前没有c++异常, 则将处理的`Exception`作为异常抛出.
- 对于致命异常, 直接将当前处理的`Exception`对象抛出
- log则会直接输出到`stderr`中.

基类`ExceptionCallback`也提供了这三个事件回调的默认实现, 全部都是直接传递给下一个处理对象.

###UnwindDetector

首先是一个自由函数`runCatchingExceptions`

```
template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func) noexcept;
```

这个函数执行给定函数并且捕获其可能抛出的异常, 将捕获到的异常作为返回值传出.

`UnwindDetector`是一个很简单的类, 用来检测析构函数是否是因为异常导致栈展开才被调用的. 原理是在析构时保存当前未捕获的异常数量, 之后再进行对比, 数量不一致则说明是因为异常导致的栈展开.

```
class UnwindDetector {
public:
  // 检测是否在因为异常导致的栈展开过程中
  bool isUnwinding() const;
  // 这个函数还没完工, 目前的行为是如果当前不是栈展开过程中, 则直接执行回调; 如果在栈展开过程中, 则执行回调并丢弃掉它抛出的异常.
  template <typename Func>
  void catchExceptionsIfUnwinding(Func&& func) const;
};
```

想使用这个类, 可以私有继承它或者作为数据成员.

还有两个相关的工具宏

```
// 在当前作用域正常结束时执行`code`
#define KJ_ON_SCOPE_SUCCESS(code) \
  ::kj::UnwindDetector KJ_UNIQUE_NAME(_kjUnwindDetector); \
  KJ_DEFER(if (!KJ_UNIQUE_NAME(_kjUnwindDetector).isUnwinding()) { code; })

// 在当前作用域因异常退出时执行`code`
#define KJ_ON_SCOPE_FAILURE(code) \
  ::kj::UnwindDetector KJ_UNIQUE_NAME(_kjUnwindDetector); \
  KJ_DEFER(if (KJ_UNIQUE_NAME(_kjUnwindDetector).isUnwinding()) { code; })
```

Maybe
---

表示一个不一定存在的值, 通常用于作为一个可能会失败的函数的返回值.

```
template<typename T>
Maybe { // for T or nullptr
    _::NullableValue<T> ptr;
public:
    // ...
    
    bool operator==(decltype(nullptr)) const;
    bool operator!=(decltype(nullptr)) const;

    T &orDefault(T& defaultValue) {
        if (ptr == nullptr) {
          return defaultValue;
        } else {
          return *ptr;
        }
    }

    template <typename Func>
    auto map(Func&& f) -> Maybe<decltype(f(instance<T&>()))> {
        if (ptr == nullptr) {
            return nullptr;
        } else {
            return f(*ptr);
        }
    }
};
```

`NullableValue<T>`是一个`T`大小的空间 + 一个表示是否初始化过的`bool`变量, 所以整个`Maybe<T>`就类似于添加了几个便利方法的`std::pair<T, bool>`.

不过`Maybe`对引用和`Own<T>`是有特化的, 内部只会持有一个指针.

`Maybe`有一个配套的宏, 可以取出一个指向内部对象的指针:

```
KJ_IF_MAYBE(ptr, maybe) {
    // ptr->...
} else {
    // no value
}
```

OneOf
---

接收一组类型, 在同一时刻只可能初始化成其中某一个类型的值

```
template <typename... Variants>
class OneOf {
    // ...
public:
    // ...

    // 持有的是否是指定类型的对象
    template <typename T>
    bool is() const;

    // 获取持有对象的引用. 需要用户保证获取的类型就是持有的对象的类型
    template <typename T>
    T& get();
    template <typename T>
    const T& get() const;

    // 初始化指定类型的对象, 如果之前已经持有一个对象, 不管类型是否一致, 都会销毁原先持有的对象.
    template <typename T, typename... Params>
    void init(Params&&... params);
};
```

Function
---

函数对象的容器, 特殊之处在于, 如果它是用左值构造的, 则只持有这个左值对象的引用; 用右值构造它, 它才会将这个函数对象移动到内部保存起来.

```
struct AddN {
  int n;
  int operator(int i) { return i + n; }
}

Function<int(int, int)> f1 = AddN{2};
// f1 owns an instance of AddN.  It may safely be moved out
// of the local scope.

AddN adder(2);
Function<int(int, int)> f2 = adder;
// f2 contains a reference to `adder`.  Thus, it becomes invalid
// when `adder` goes out-of-scope.

AddN adder2(2);
Function<int(int, int)> f3 = kj::mv(adder2);
// f3 owns an insatnce of AddN moved from `adder2`.  f3 may safely
// be moved out of the local scope.
```

另外还有一个用来绑定对象和成员函数的宏

```
class Printer {
public:
  void print(int i);
  void print(kj::StringPtr s);
};

Printer p;

Function<void(uint)> intPrinter = KJ_BIND_METHOD(p, print);
// Will call Printer::print(int).

Function<void(const char*)> strPrinter = KJ_BIND_METHOD(p, print);
// Will call Printer::print(kj::StringPtr).
```

命令行处理
---

KJ程序流程处理比较特殊, 一般情况下会把命令通过`runMainAndExit`转发给一个自定义的入口函数, 并且`runMainAndExit`不返回, 直接退出程序, 希望依赖系统清理那些泄露的资源.

```
#define KJ_MAIN(MainClass) \
  int main(int argc, char* argv[]) { \
    ::kj::TopLevelProcessContext context(argv[0]); \
    MainClass mainObject(context); \
    return ::kj::runMainAndExit(context, mainObject.getMain(), argc, argv); \
  }
```

使用这个宏就必须定义一个`MainClass`, 它需要接受一个`ProcessContext&`作为构造函数的参数, 并且还要有一个`getMain()`方法, 能够返回一个签名为`void(StringPtr programName, ArrayPtr<const StringPtr> params)`的函数对象. 

KJ提供了`MainBuilder`来简化`MainClass`的构建. 其中内置了命令行处理功能, 只需要简单地将命令和对应处理函数一起注册即可, 并且还会根据注册信息自动添加`--help`的反馈.

```
class MainBuilder {
public:
  MainBuilder(ProcessContext& context, StringPtr version,
              StringPtr briefDescription, StringPtr extendedDescription = nullptr);
  ~MainBuilder() noexcept(false);

  class OptionName {
  public:
    OptionName() = default;
    inline OptionName(char shortName): isLong(false), shortName(shortName) {}
    inline OptionName(const char* longName): isLong(true), longName(longName) {}

  private:
    bool isLong;
    union {
      char shortName;
      const char* longName;
    };
    friend class MainBuilder;
  };

  class Validity {
  public:
    inline Validity(bool valid) {
      if (!valid) errorMessage = heapString("invalid argument");
    }
    inline Validity(const char* errorMessage)
        : errorMessage(heapString(errorMessage)) {}
    inline Validity(String&& errorMessage)
        : errorMessage(kj::mv(errorMessage)) {}

    inline const Maybe<String>& getError() const { return errorMessage; }
    inline Maybe<String> releaseError() { return kj::mv(errorMessage); }

  private:
    Maybe<String> errorMessage;
    friend class MainBuilder;
  };

  // 注册一个新选项.
  // names 是选项名的列表, 单个字符的选项目应该和`-`一起使用; 多字符的选项目应该和`--`一起使用
  // callback 是此选项的处理函数, 它的返回值表示此选项是否能被接收, 如果不能接受则停止处理后续参数并输出错误信息然后退出程序
  // helpText 是这个选项功能的自然语言描述
  // 示例 builder.addOption({'a', "all"}, KJ_BIND_METHOD(*this, showAll), "Show all files.");
  MainBuilder& addOption(std::initializer_list<OptionName> names, Function<Validity()> callback,
                         StringPtr helpText);

  // 注册一个可以带一个参数的选项
  // argumentTitle 是用在帮助信息中的参数名
  // 示例 builder.addOptionWithArg({'o', "output"}, KJ_BIND_METHOD(*this, setOutput),
  //                              "<filename>", "Output to <filename>.");
  MainBuilder& addOptionWithArg(std::initializer_list<OptionName> names,
                                Function<Validity(StringPtr)> callback,
                                StringPtr argumentTitle, StringPtr helpText);

  // 注册一个子命令
  // name 是子命令名称, 如果出现这个命令, 则将剩余参数传递给 getSubParser 返回的解析器.
  MainBuilder& addSubCommand(StringPtr name, Function<MainFunc()> getSubParser,
                             StringPtr briefHelpText);

  // 按顺序注册参数处理函数
  // title 是用于命令说明的参数描述
  // 比如, 代码是:
  //     builder.expectArg("<foo>", ...);
  //     builder.expectOptionalArg("<bar>", ...);
  //     builder.expectArg("<baz>", ...);
  //     builder.expectZeroOrMoreArgs("<qux>", ...);
  //     builder.expectArg("<corge>", ...);
  // 这个程序最少需要三个参数: foo, baz, corge.
  // 如果给出了四个参数, 则第二个参数会作为 bar
  // 如果给出了五个或更多的参数, 则第三到最后一个参数之间的参数都算作 qux
  MainBuilder& expectArg(StringPtr title, Function<Validity(StringPtr)> callback);
  MainBuilder& expectOptionalArg(StringPtr title, Function<Validity(StringPtr)> callback);
  MainBuilder& expectZeroOrMoreArgs(StringPtr title, Function<Validity(StringPtr)> callback);
  MainBuilder& expectOneOrMoreArgs(StringPtr title, Function<Validity(StringPtr)> callback);

  // 注册一个在所有参数被处理完之后触发的回调
  MainBuilder& callAfterParsing(Function<Validity()> callback);

  // 构造主函数. 一旦这个函数返回, 此对象不再有效
  MainFunc build();
};

// 示例
class FooMain {
public:
  FooMain(kj::ProcessContext& context): context(context) {}

  bool setAll() { all = true; return true; }
  // Enable the --all flag.

  kj::MainBuilder::Validity setOutput(kj::StringPtr name) {
    // Set the output file.

    if (name.endsWith(".foo")) {
      outputFile = name;
      return true;
    } else {
      return "Output file must have extension .foo.";
    }
  }

  kj::MainBuilder::Validity processInput(kj::StringPtr name) {
    // Process an input file.

    if (!exists(name)) {
      return kj::str(name, ": file not found");
    }
    // ... process the input file ...
    return true;
  }

  kj::MainFunc getMain() {
    return MainBuilder(context, "Foo Builder v1.5", "Reads <source>s and builds a Foo.")
        .addOption({'a', "all"}, KJ_BIND_METHOD(*this, setAll),
            "Frob all the widgets.  Otherwise, only some widgets are frobbed.")
        .addOptionWithArg({'o', "output"}, KJ_BIND_METHOD(*this, setOutput),
            "<filename>", "Output to <filename>.  Must be a .foo file.")
        .expectOneOrMoreArgs("<source>", KJ_BIND_METHOD(*this, processInput))
        .build();
  }

private:
  bool all = false;
  kj::StringPtr outputFile;
  kj::ProcessContext& context;
};
```

字符串
---

###StringPtr

只是`Array<const char>`的一个简单封装, 没什么特别的功能, 只是多了几个实用算法: `startsWith`, `endsWith`, `findFirst`, `findLast`

###String

`Array<char>`的简单封装, 构造时不会有任何隐式的堆内存分配, 只会简单的引用给出缓冲区. 想要在堆上分配一个新字符串, 必须显式地使用自由函数`heapString`

```
String heapString(size_t size);
// Allocate a String of the given size on the heap, not including NUL terminator.  The NUL
// terminator will be initialized automatically but the rest of the content is not initialized.

String heapString(const char* value);
String heapString(const char* value, size_t size);
String heapString(StringPtr value);
String heapString(const String& value);
String heapString(ArrayPtr<const char> value);
// Allocates a copy of the given value on the heap.
```

###字符串化

字符串化的实现围绕`kj::_::Stringifier`类, 重载的`operator*(Stringifier, T)`负责把各种类型转换成一个可迭代的字符容器. `toCharSequence(T)` 是这一过程的包装.

最常用的接口是`str`, 它可以接受任意数量任意类型的参数按顺序组成一个字符串

```
template <typename... Params>
String str(Params&&... params) {
  // Magic function which builds a string from a bunch of arbitrary values.  Example:
  //     str(1, " / ", 2, " = ", 0.5)
  // returns:
  //     "1 / 2 = 0.5"
  // To teach `str` how to stringify a type, see `Stringifier`.

  return _::concat(toCharSequence(kj::fwd<Params>(params))...);
}

inline String str(String&& s) { return mv(s); }
// Overload to prevent redundant allocation.
```

`strArray`接受一个支持`size()`和`operator[]`的容器和一个分隔字符串, 然后将数组中每个元素都字符串化, 最后将每个小字符串中间拼接上分隔字符串组成结果字符串.

`KJ_STRINGIFY`宏用来定义自定义类型的字符串化过程, 它应该放在目标类型所在的命名空间或者是全局命名空间内.

```
class Foo {...};
inline StringPtr KJ_STRINGIFY(const Foo& foo) { return foo.name(); }
```

async
---

###用法

`Promise`用来作为那些还未完成的异步操作的结果的占位符, 我们可以先把要对结果进行的操作注册好了, 等结果真正计算出来之后(`Promise`被"解决"或"满足"), 在将这个结果应用到之前注册好的处理函数上; 也有可能中途失败, 无法得到想要的结果(`Promise`被"拒绝"), 这时进入的是之前注册的错误处理函数.

```
kj::Promise<kj::String> fetchHttp(kj::StringPtr url);
// Asynchronously fetches an HTTP document and returns
// the content as a string.

kj::Promise<void> sendEmail(kj::StringPtr address,
    kj::StringPtr title, kj::StringPtr body);
// Sends an e-mail to the given address with the given title
// and body.  The returned promise resolves (to nothing) when
// the message has been successfully sent.
```

处理函数是通过`Promise`的`then`方法注册的, 它也会返回一个`Promise`, 作为它的结果的占位符. 如果处理函数本身返回的就是一个`Promise<T>`, `then`不会返回一个`Promise<Promise<T>>`, 而是会将它折叠成`Promise<T>`. 需要注意的是, `Promise`是一次性的 -- `then`会消耗掉它, 也就是说不能在一个`Promise`上调用两次`then`, 如果必须要在多处使用同一个`Promise`, 可以使用`fork`方法来"fork"出一个`Promise`(很明显, `Promise`是不支持复制的, 他只有移动构造函数).

```
kj::Promise<kj::String> contentPromise =
    fetchHttp("http://example.com");

kj::Promise<int> lineCountPromise =
    promise.then([](kj::String&& content) {
  return countChars(content, '\n');
});
```

错误处理函数是`then`的可选的第二个参数, 它的作用类似于同步代码中的`catch`块, 但是接受的异常类型只能是`kj::Exception&&`的. 如果没有给出错误处理函数, 异常会继续向下游传递. 如果结果处理函数或者异常处理函数中抛出了异常, 这个异常也会向下游传递.

```
kj::Promise<int> lineCountPromise =
    promise.then([](kj::String&& content) {
  return countChars(content, '\n');
}, [](kj::Exception&& exception) {
  // Error!  Pretend the document was empty.
  return 0;
});
```

可以阻塞地等待一个`Promise`解决, 等待的过程实际就是一直运行事件循环(详细描述在下面), 直到指定的`Promise`被解决. 用`then`注册的处理函数中是不能等待的, 因为这会卡死事件循环(在事件循环中阻塞地等待事件循环触发自己的解决事件 - -).

```
kj::EventLoop loop;
kj::WaitScope waitScope(loop);

kj::Promise<kj::String> contentPromise =
    fetchHttp("http://example.com");

kj::String content = contentPromise.wait(waitScope);

int lineCount = countChars(content, '\n');
```

###实现

实现中最核心的类就是`EventPort`, 第一手的`Promise`就是由它创建的, 这些`Promise`的解决或拒绝也是由它负责的. 因为涉及到异步任务(主要是IO)完成时机的判定, 所以`EventPort`的实现是和平台紧密相关的, KJ原来有一份`UnixEventPort`的实现, 不过现在移植到vs2013上之后那份实现就没什么用了, 所以我写了一份`WinEventPort`, 提供了`Promise<void> waitFor(HANDLE)`方法, 用来创建一个在指定句柄被触发时解决的`Promise`.  

和`EventPort`配合工作的还有`EventLoop`, 它只是简单地维护了一个`Event`对象的链表. `EventLoop`的构造函数需要一个`EventPort`的引用, 但它并不会直接要求`EventPort`去判断`Promise`是否解决, 而只是简单的记录哪个`EventPort`与自己关联. 有两个方法可以运行事件循环: `turn`会触发链表头部的一个事件; `run(count)`会触发链表前端最多`count`个事件, 如果事件数量不足, 它会直接返回.

`Event`是一个很简单的类, 子类需要重写它的`fire`方法来定义事件被触发时的逻辑. 另外插入事件列表的功能也是由`Event`自己实现的, `armDepthFirst`用来将自己插入列表头部; `armBreadthFirst`用来将自己插入列表尾端.

`WaitScope`很简单, 它只是在构造函数中将作为参数的`EventLoop`设为本线程的当前事件循环, 然后在析构函数中取消这一设置.

比如一个`Promise<size_t> readAsync(file, buf, size, eventPort);`的实现大概是这样的:

```
Promise<size_t> readAsync(HANDLE file, void* buf, size_t size, EventPort& eventPort) {
  OVERLAPPED *ol = ...;
  // ...
  HANDLE event = CreateEvent(...);
  ol->event = event;
  // ...
  ReadFile(file, buf, size, NULL, ol);
  return eventPort.waitFor(event).then([ol](){
    return ol->InternalHigh;
  });
}
```

`eventPort.waitFor(event)`的实现大概是这样的:

```
Promise<void> WinEventPort::waitFor(HANDLE event) {
  handles.push_back(event);
  auto paf = newPromiseAndFulfiller
  handleToFulfiller[evnet] = paf.fulfiller;
  return move(paf.promise);
}
```

用户的使用代码类似这样:

```
kj::WinEventPort eventPort;
kj::EventLoop loop(eventPort);
kj::WaitScope waitScope(loop);

kj::Promise<size_t> readPromise =
    readAsync(file, buf, size, eventPort);

size_t bytesRead = readPromise.wait(waitScope);
```

`readPromise.wait(waitScope)`的简易实现:

```
class BoolEvent: public _::Event {
public:
  bool fired = false;

  void fire() override {
    fired = true;
  }
};

template<typename T>
T Promise<T>::wait(WaitScope& ws) {
  done = new BoolEvent;
  
  if (flag == FINISH)
    done->armDepthFirst();
  else
    flag = WAIT;
  
  EventLoop& loop = ws.loop;
  while (!done->fired) {
    if (!loop.ture())     // 事件循环中的事件耗尽
      loop.port.wait();   // 要求EventPort等待异步任务完成
  }
  return result;
}
```

最后是`EventPort::wait`的实现:

```
void WinEventPort::wait() {
  auto result = WaitForMultipleObjects(handles.asArray(), ...);
  auto index = result - WAIT_OBJECT_0;
  handleToFulfiller[handles[index]].fulfill();
}

void PromiseFulfiller::fulfill() {
  // promise.result = ...  非void的Promise的结果在这里设置.
  if (promise.flag == WAIT)
    promise.done->armDepthFirst();
  else
    flag = FINISH
}
```

也就是说, 不管`Promise`是先被等待还是先完成, 最后肯定会有一个事件被插入事件循环, `wait`方法就一直运行事件循环, 直到自己注册的事件被触发.

###其他

还有不少和`Promise`相关的设施, 比如`READY_NOW`和`NEVER_DONE`这两个常量可以分别隐式转换为一个已经解决了的`Promise<void>`和一个被拒绝了的任意类型的`Promise`.

`evalLater(Func&& func)`将`func`插入事件循环, 它实际上就是`Promise<void>(READY_NOW).then(func);`.

`Promise<Array<T>> joinPromises(Array<Promise<T>>&& promises)`将一个`Promise`的数组转换为一个数组的`Promise`

`TaskSet`负责管理一组`Promise`, 如果你有许多`Promise`又不想挨个等待它们, 那就直接扔进一个`TaskSet`就行了(`Promise`析构了的话, 在它上面注册的处理函数就永远不会被调用了).
