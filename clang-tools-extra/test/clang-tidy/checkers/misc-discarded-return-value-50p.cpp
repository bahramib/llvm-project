// RUN: %check_clang_tidy %s misc-discarded-return-value -std=c++17 %t \
// RUN:   -config='{CheckOptions: [ \
// RUN:     {key: misc-discarded-return-value.ConsumeThreshold, value: 50} \
// RUN:   ]}'

extern bool Coin;
extern int sink(int);
extern int sink2(int, int);

namespace std {

using size_t = decltype(sizeof(void *));

int printf(const char *Format, ...);

template <class T>
T &&declval() noexcept;

template <class T>
struct default_delete {};

template <class T>
struct initializer_list {
  initializer_list(const T *, std::size_t) {}
};

template <class T>
struct numeric_limits {
  static constexpr std::size_t min() noexcept { return 0; }
  static constexpr std::size_t max() noexcept { return 4; }
};

template <class T>
struct remove_reference { typedef T type; };
template <class T>
struct remove_reference<T &> { typedef T type; };
template <class T>
struct remove_reference<T &&> { typedef T type; };

template <class T>
typename remove_reference<T>::type &&move(T &&V) noexcept {
  return static_cast<typename remove_reference<T>::type &&>(V);
}

template <class T, class D = default_delete<T>>
class unique_ptr {
public:
  unique_ptr();
  explicit unique_ptr(T *);
  template <typename U, typename E>
  unique_ptr(unique_ptr<U, E> &&);
};

} // namespace std

void voidFn();
void voidTest() {
  for (voidFn();; voidFn())
    ;
  voidFn(); // NO-WARN: void functions do not count for usages.
}

[[nodiscard]] int nodiscard();
void nodiscardTest() {
  int Consume = nodiscard();
  nodiscard(); // NO-WARN from the check - [[nodiscard]] handled by Sema.
}

int silence();
void silenceTest() {
  (void)silence();
  static_cast<long>(silence());
  reinterpret_cast<void *>(silence());
  silence();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'silence' is used in most calls, but not in this one [misc-discarded-return-value]
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 75% (3 out of 4) of cases
}

int varInit();
int varInit2();
void varinitTest() {
  int X = varInit();
  varInit();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'varInit' is used in most calls, but not in this one [misc-discarded-return-value]
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2) of cases

  int Y = varInit2(), Z = varInit2();
  varInit2();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'varInit2' is used in most calls, but not in this one [misc-discarded-return-value]
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3) of cases
}

int passToFn();
void passToFnTest() {
  sink(passToFn());
  passToFn();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'passToFn'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

int *array();
int index();
void indexTest() {
  int T[4];
  array()[index()];
  T[index()];
  array()[0];

  index();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'index'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)

  array();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'array'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)
}

int varargVal();
void varargTest() {
  std::printf("%d %d", varargVal(), varargVal());
  varargVal();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'varargVal'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)
}

int unary();
void unaryTest() {
  if (!unary())
    unary();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'unary'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)
}

bool lhs();
bool rhs();
void lhsrhsTest() {
  bool X = true;
  lhs() == X;
  X == rhs();

  lhs();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'lhs'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
  rhs();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'rhs'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

bool inIf();
bool inElseIf();
void ifTest() {
  if (inIf())
    inIf();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'inIf'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)

  if (Coin)
    voidFn();
  else if (inElseIf())
    inElseIf();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'inElseIf'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)
}

int forInit();
int forCondition();
int forIncrement();
int inFor();
struct Range {
  int *begin();
  int *end();
};
Range makeRange();
void forTest() {
  for (forInit();;)
    forInit();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'forInit'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)

  for (; forCondition();)
    forCondition();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'forCondition'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)

  for (;; forIncrement())
    forIncrement();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'forIncrement'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)

  for (inFor(); inFor(); inFor())
    inFor();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'inFor'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 75% (3 out of 4)

  for (int &I : makeRange())
    ;

  makeRange();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'makeRange'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

int whileCond();
int doCond();
void doWhileTest() {
  while (whileCond())
    whileCond();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'whileCond'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)

  do
    doCond();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'doCond'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)
  while (doCond());
}

int inSwitch();
void switchTest() {
  switch (inSwitch()) {
  case 0:
    break;
  }
  inSwitch();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'inSwitch'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

int returned();
int returnTest() {
  returned();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'returned'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
  return returned();
}

struct S {
  S();
  S(int);
  int member();
  int operator()();
  bool operator==(const S &);
  bool operator!=(const S &);
  int operator[](int);
  int operator*();
  void *operator&();
  S operator++(int);
  S &operator++();
  S &operator+(int);
  int plusValueMaker();

  int memberA();
  int memberB();

  void memberTestInside() {
    sink(member());
    member();
    // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'member'
    // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (2 out of 4)

    sink((*this)());
    (*this)();
    // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'operator()'
    // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (2 out of 4)

    sink2(memberA(), memberB());
    memberA();
    memberB();
    // CHECK-MESSAGES: :[[@LINE-2]]:5: warning: return value of 'memberA'
    // CHECK-MESSAGES: :[[@LINE-3]]:5: note: value consumed or checked in 50% (1 out of 2)
    // CHECK-MESSAGES: :[[@LINE-3]]:5: warning: return value of 'memberB'
    // CHECK-MESSAGES: :[[@LINE-4]]:5: note: value consumed or checked in 50% (1 out of 2)
  }
};

int ctorParam();
void constructorTest() {
  S Obj = ctorParam();
  S Obj2{ctorParam()};
  ctorParam();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'ctorParam'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)
}

void memberTest() {
  S Obj;
  int I = Obj.member();
  Obj.member();
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: return value of 'member'
  // CHECK-MESSAGES: :[[@LINE-2]]:7: note: value consumed or checked in 50% (2 out of 4)

  sink(Obj());
  Obj();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'operator()'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (2 out of 4)

  int E = Obj[I];
  Obj[I + 1];
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'operator[]'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)

  // NO-WARN: Matching operators would be too noisy, and
  // -Wunused-(comparison|value) takes care of this case.
  S O2 = ++Obj;
  ++Obj;

  S O3 = Obj++;
  Obj++;

  bool B = O2 == O3;
  O2 == O3;

  B = O2 != O3;
  O2 != O3;

  auto &OR = Obj = O2;
  Obj = O3;

  E = *Obj;
  *Obj;
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'operator*'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)

  void *P = &Obj;
  &Obj;
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'operator&'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)

  Obj + Obj.plusValueMaker();
  Obj.plusValueMaker();
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: return value of 'plusValueMaker'
  // CHECK-MESSAGES: :[[@LINE-2]]:7: note: value consumed or checked in 50% (1 out of 2)
}

static int internal() { return 1; }
void internalTest() {
  int I = internal();
  internal();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'internal'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

template <class T>
struct Integer {
  struct Unsigned {
    static constexpr unsigned Max = static_cast<unsigned>(-1);
    Unsigned(int);
  };
};
template <class T>
struct IntegerInfo { using UnsignedIntegerTag = typename Integer<T>::Unsigned; };
constexpr int intNoMask() { return 0; }
template <class IntTy>
struct TaggedInteger {
  using IntegerType = IntTy;

  static constexpr IntegerType max() {
    return IntegerType(
        typename IntegerInfo<IntegerType>::UnsignedIntegerTag(intNoMask()));
  }
};
struct MyInteger {
  int Value;
  template <class IntegerTag>
  MyInteger(IntegerTag IT) : Value(IT.Max) {}
};
int unresolvedCtorExprTest() {
  intNoMask();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'intNoMask'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
  return TaggedInteger<MyInteger>::max().Value;
}

struct W1 {};
struct W2 {};
struct WMaker {
  W1 w1() { return {}; }
  W2 w2() { return {}; }
};
template <class T>
struct Wrapper1 {
  Wrapper1(T) {}
};
template <class T, class U>
struct Wrapper2 {
  Wrapper2(T, U) {}
};
template <class T>
struct WrapMaker1 {
  Wrapper1<T> make() {
    WMaker *WP;
    Wrapper1<T> Wrapper(WP->w1());
    return Wrapper;
  }
};
template <class T, class U>
struct WrapMaker2 {
  Wrapper2<T, U> make() {
    WMaker *WP;
    Wrapper2<T, U> Wrapper(WP->w1(), WP->w2());
    return Wrapper;
  }
};
void parenListExprTest() {
  WMaker{}.w1();
  // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: return value of 'w1'
  // CHECK-MESSAGES: :[[@LINE-2]]:12: note: value consumed or checked in 66% (2 out of 3)
  WMaker{}.w2();
  // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: return value of 'w2'
  // CHECK-MESSAGES: :[[@LINE-2]]:12: note: value consumed or checked in 50% (1 out of 2)
}

struct T {
  S make();
  T &operator+(S);
};

void utilityTest() {
  T TObj;
  S SObj;

  TObj + std::move(SObj);
  std::move(SObj);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'move<S &>'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

struct La {
  int throughThis();
  void testThisCapture() {
    auto L = [this] { return throughThis(); };
    throughThis();
    // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'throughThis'
    // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)
  }
};

int lambda();
int lambda1();
int lambda2();
void lambdaTest() {
  auto L = [X = lambda()] { return X; };
  lambda();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'lambda'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)

  auto L2 = [X = lambda1(), Y = lambda2()] { return X + Y; };
  lambda1();
  lambda2();
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: return value of 'lambda1'
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: value consumed or checked in 50% (1 out of 2)
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: return value of 'lambda2'
  // CHECK-MESSAGES: :[[@LINE-4]]:3: note: value consumed or checked in 50% (1 out of 2)
}

struct P {
  struct Inner {
    int func();
    int operator[](int);
  };
  Inner *get();
  Inner &getRefForIndex();
};
void dereferenceTest() {
  P Obj;
  Obj.get()->func();
  Obj.get();
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: return value of 'get'
  // CHECK-MESSAGES: :[[@LINE-2]]:7: note: value consumed or checked in 50% (1 out of 2)

  Obj.getRefForIndex()[1];
  Obj.getRefForIndex();
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: return value of 'getRefForIndex'
  // CHECK-MESSAGES: :[[@LINE-2]]:7: note: value consumed or checked in 50% (1 out of 2)
}

struct ZP {};
struct ZQ {};
struct Z {
  struct Context {
    template <class T>
    T getAs() { return T{}; }
  };
  struct Ctx {
    Context *getContext(ZP *);
  };

  Ctx ContextObj;

  template <class T>
  T getAs() {
    ZP X{};

    Context *C = ContextObj.getContext(&X);
    ContextObj.getContext(&X);
    return ContextObj.getContext(&X)->getAs<T>();
    // CHECK-MESSAGES: :[[@LINE-2]]:16: warning: return value of 'getContext'
    // CHECK-MESSAGES: :[[@LINE-3]]:16: note: value consumed or checked in 66% (2 out of 3)
  }
};
void templateFunctionDerefTest() {
  Z Obj;
  auto Got = Obj.getAs<ZQ>();
  Obj.getAs<ZQ>();
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: return value of 'getAs<ZQ>'
  // CHECK-MESSAGES: :[[@LINE-2]]:7: note: value consumed or checked in 50% (1 out of 2)
}

int ctorMemberHelper();
struct Q {
  static int transform(int);

  int M;
  int N;
  Q(int I) : M(I), N(transform(I)) {}
};
void ctorMemberInitializerTest() {
  Q Obj{ctorMemberHelper()};
  ctorMemberHelper();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'ctorMemberHelper'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)

  Q::transform(1);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'transform'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

int inlineMemberHelper();
struct V {
  int X = inlineMemberHelper();
};
void inlineMemberInitTest() {
  inlineMemberHelper();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'inlineMemberHelper'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

int defaultParam();
struct W {
  W(int I = defaultParam());
};
void defaultSink(int P = defaultParam());
void defaultParamTest() {
  defaultParam();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'defaultParam'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)
}

constexpr int enumValue() { return 1; }
enum EA { EA_A = enumValue() };
enum EB : long { EB_A = enumValue() };
enum class EC { A = enumValue() };
enum class ED : short { A = enumValue() };
void enumValueTest() {
  enumValue();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'enumValue'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 80% (4 out of 5)
}

struct InDecltype {};
void decltypeTest() {
  using T = decltype(std::declval<InDecltype>());
  std::declval<InDecltype>();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'declval<InDecltype>'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

struct InSizeof {
  char C[8];
};
InSizeof makeSizeof();
void sizeofTest() {
  auto X = sizeof(makeSizeof());
  makeSizeof();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'makeSizeof'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

struct InDecltypeMember {};
InDecltypeMember dtMemberHelper();
struct DecltypeMember {
  decltype(dtMemberHelper()) Member;
};
void decltypeMemberTest() {
  dtMemberHelper();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'dtMemberHelper'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

constexpr bool inStaticAssert() { return true; }
void staticTest() {
  static_assert(inStaticAssert(), "");
  inStaticAssert();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'inStaticAssert'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}

int inTernaryLHS();
int inTernaryRHS();
void ternaryTest() {
  int X = Coin ? inTernaryLHS() : inTernaryRHS();

  inTernaryLHS();
  inTernaryRHS();
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: return value of 'inTernaryLHS'
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: value consumed or checked in 50% (1 out of 2)
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: return value of 'inTernaryRHS'
  // CHECK-MESSAGES: :[[@LINE-4]]:3: note: value consumed or checked in 50% (1 out of 2)
}

template <class Lambda>
auto call(Lambda &&L) {
  return L();
}
void lambdaCallerTest() {
  auto L1 = [] { return 1; };
  auto L2 = [] { return 2; };

  auto Take1 = call(L1);
  call(L1);

  auto Take2 = call(L2);
  auto Take2b = call(L2);
  call(L2);

  // CHECK-MESSAGES: :[[@LINE-6]]:3: warning: return value of 'call<{{.*}}[[@LINE-10]]:13{{.*}}>'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: value consumed or checked in 60% (3 out of 5)
  // CHECK-MESSAGES: :[[@LINE-4]]:3: warning: return value of 'call<{{.*}}[[@LINE-12]]:13{{.*}}>'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: value consumed or checked in 60% (3 out of 5)

  // FIXME: Because call<Lambda> is a template, calls to it, even with different
  // lambdas are calculated together, but the resulting diagnostic message is
  // wrong. The culprit seems to be the USR generated for
  // 'call<(lambda in lambdaCallerTest)>' being
  // "c:misc-discarded-return-value-50p.cpp@F@call<#&$@F@lambdaCallerTest#@Sa>#S0_#"
}
void lambdaCallerTest2() {
  auto X = [] { return 4; };

  auto Var = call(X);
  call(X);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'call<{{.*}}[[@LINE-4]]:12{{.*}}>'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)

  // Here, call<...> has a different key:
  // "c:misc-discarded-return-value-50p.cpp@F@call<#&$@F@lambdaCallerTest2#@Sa>#S0_#"
}

int calledThroughLambda();
void calledThroughLambdaTest() {
  int I = calledThroughLambda(), J = calledThroughLambda();

  call([] { calledThroughLambda(); });
  // CHECK-MESSAGES: :[[@LINE-1]]:13: warning: return value of 'calledThroughLambda'
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: value consumed or checked in 50% (2 out of 4)
  calledThroughLambda();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'calledThroughLambda'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (2 out of 4)
}

struct Factory {
  struct Element {};

  static Element make();
  static Element make2();
};
struct ElementArray {
  ElementArray(std::initializer_list<Factory::Element>) {}
};
struct InitListIniter {
  ElementArray Arr;

  InitListIniter() : Arr({Factory::make(), Factory::make(), Factory::make()}) {
    Factory::make();
    // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'make'
    // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 75% (3 out of 4)
  }
};
void initListTest() {
  Factory::Element Array[] = {Factory::make2(), Factory::make2(), Factory::make2()};
  Factory::make2();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'make2'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 75% (3 out of 4)
}

struct Container {
  int begin();
  int end();
};
struct BaseRange {
  int Begin, End;
  BaseRange(int B, int E) : Begin(B), End(E) {}
};
struct DerivedRange : BaseRange {
  DerivedRange(Container C) : BaseRange(C.begin(), C.end()) {}
};
void baseClassInitTest() {
  Container Cont;
  DerivedRange DR{Cont};

  Cont.begin();
  Cont.end();
  // CHECK-MESSAGES: :[[@LINE-2]]:8: warning: return value of 'begin'
  // CHECK-MESSAGES: :[[@LINE-3]]:8: note: value consumed or checked in 50% (1 out of 2)
  // CHECK-MESSAGES: :[[@LINE-3]]:8: warning: return value of 'end'
  // CHECK-MESSAGES: :[[@LINE-4]]:8: note: value consumed or checked in 50% (1 out of 2)
}

template <class T>
struct ContainerT {
  T *begin();
  T *end();
};
struct CharContainer {
  char *begin();
  char *end();
};
template <class T>
struct TypedRange {
  T *Begin, *End;
  TypedRange(T *B, T *E) : Begin(B), End(E) {}
};
struct CharRange : TypedRange<char> {
  CharRange(CharContainer C) : TypedRange<char>(C.begin(), C.end()) {}
};
template <class T, unsigned N>
struct SmallTypedRange : TypedRange<T> {
  SmallTypedRange(T *B, T *E) : TypedRange<T>(B, E) {}
};
template <unsigned N>
struct SmallString : SmallTypedRange<char, N> {
  SmallString(CharContainer C) : SmallTypedRange<char, N>(C.begin(), C.end()) {}
};
void baseTemplateClassInitTest() {
  CharContainer Cont;
  CharRange CR{Cont};

  Cont.begin();
  Cont.end();
  // CHECK-MESSAGES: :[[@LINE-2]]:8: warning: return value of 'begin'
  // CHECK-MESSAGES: :[[@LINE-3]]:8: note: value consumed or checked in 66% (2 out of 3)
  // CHECK-MESSAGES: :[[@LINE-3]]:8: warning: return value of 'end'
  // CHECK-MESSAGES: :[[@LINE-4]]:8: note: value consumed or checked in 66% (2 out of 3)
}

struct CharContainer2 {
  char *begin();
  char *end();
};
TypedRange<char> rangeTemporaryTest() {
  CharContainer2 CC;
  CC.begin();
  CC.end();
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: return value of 'begin'
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: value consumed or checked in 50% (1 out of 2)
  // CHECK-MESSAGES: :[[@LINE-3]]:6: warning: return value of 'end'
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: value consumed or checked in 50% (1 out of 2)

  return TypedRange<char>(CC.begin(), CC.end());
}

void *rawAllocate();
int aObjectParam();
struct AObjectParamMaker {
  int Param();
};
struct AObject {
  AObject();
  AObject(int);
};
AObject *aoAddress();
struct Allocator {
};
Allocator createAlloc();
void *operator new(std::size_t, void *);
void *operator new(std::size_t N, const Allocator &Arg);
std::size_t padding();
Allocator createOtherAlloc();
void *operator new(std::size_t N, const Allocator &Alloc, std::size_t Padding);
std::size_t count();
void newDeleteTest() {
  AObject *AO = new (rawAllocate()) AObject{};
  delete aoAddress();

  rawAllocate();
  aoAddress();
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: return value of 'rawAllocate'
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: value consumed or checked in 50% (1 out of 2)
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: return value of 'aoAddress'
  // CHECK-MESSAGES: :[[@LINE-4]]:3: note: value consumed or checked in 50% (1 out of 2)

  AObject *AO2 = new (createAlloc()) AObject{aObjectParam()};
  createAlloc();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'createAlloc'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50%

  AObject *AO3 = new (createOtherAlloc(), padding()) AObject{aObjectParam()};
  createOtherAlloc();
  padding();
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: return value of 'createOtherAlloc'
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: value consumed or checked in 50% (1 out of 2)
  // CHECK-MESSAGES: :[[@LINE-3]]:3: warning: return value of 'padding'
  // CHECK-MESSAGES: :[[@LINE-4]]:3: note: value consumed or checked in 50% (1 out of 2)

  aObjectParam();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'aObjectParam'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)

  AObject *AO4 = new AObject(AObjectParamMaker{}.Param());
  AObjectParamMaker{}.Param();
  // CHECK-MESSAGES: :[[@LINE-1]]:23: warning: return value of 'Param'
  // CHECK-MESSAGES: :[[@LINE-2]]:23: note: value consumed or checked in 50% (1 out of 2)

  AObject **AObjArr = new AObject *[count()];
  AObject VLA[count()];
  count();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'count'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 66% (2 out of 3)
}

class Empty {};
class Payload {
public:
  std::unique_ptr<Empty> take();
};

template <class T>
class FromPayload {
public:
  using empty_base = std::unique_ptr<Empty>;
  empty_base *base();

  FromPayload(Payload P) {
    new (base()) empty_base(P.take());
  }
};
void nonTrivialInitExprInNewTest() {
  Payload P;
  P.take();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: return value of 'take'
  // CHECK-MESSAGES: :[[@LINE-2]]:5: note: value consumed or checked in 50% (1 out of 2)
}

template <std::size_t M, std::size_t N>
struct NTTP {};
extern NTTP<std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max()>
    X1;
void nttpTest() {
  std::numeric_limits<int>::min();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'min'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
  std::numeric_limits<int>::max();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: return value of 'max'
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: value consumed or checked in 50% (1 out of 2)
}
