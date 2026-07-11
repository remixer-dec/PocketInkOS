#ifndef PINKJS_H
#define PINKJS_H

// PinkJS is a deliberately small JavaScript subset for embedded applications.
// The interpreter owns no memory: all bindings, GC metadata, strings, objects,
// arrays, Sets, and functions live in the caller-provided workspace.
// PinkJS is distributed as a part of PocketInkOS (https://github.com/remixer-dec/PocketInkOS)
// under CC BY-NC-SA 4.0 License: https://creativecommons.org/licenses/by-nc-sa/4.0

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pinkjs {

struct Value {
  uint64_t bits = 0;
};

enum class ValueType : uint8_t {
  Undefined,
  Null,
  Boolean,
  Number,
  String,
  Object,
  Array,
  Set,
  Promise,
  Function,
};

struct StringView {
  const char *data = nullptr;
  size_t size = 0;
};

enum class ErrorCode : uint8_t {};

inline void formatErrorCode(ErrorCode code, char *out) {
  const uint8_t value = static_cast<uint8_t>(code);
  out[0] = 'E';
  out[1] = static_cast<char>('0' + value / 100U);
  out[2] = static_cast<char>('0' + (value / 10U) % 10U);
  out[3] = static_cast<char>('0' + value % 10U);
  out[4] = '\0';
}

class Runtime;
using NativeFunction = Value (*)(Runtime &runtime, const Value *args,
                                 size_t argc, void *userData);
using NativeMethodFunction = Value (*)(Runtime &runtime, Value receiver,
                                       const Value *args, size_t argc,
                                       void *userData);
using JsonParseFunction = Value (*)(Runtime &runtime, StringView json,
                                    void *userData);
using AwaitPendingFunction = bool (*)(Runtime &runtime, Value promise,
                                      void *userData);

enum class PromiseState : uint8_t { Pending, Fulfilled, Rejected };

using RootHandle = uint16_t;
static constexpr RootHandle kInvalidRoot = UINT16_MAX;

struct Options {
  uint16_t maxBindings = 160;
  uint16_t maxNatives = 48;
  uint16_t maxTemporaryRoots = 48;
  uint16_t maxHostRoots = 32;
  uint16_t maxGcWork = 256;
  uint8_t maxCallDepth = 12;
  uint8_t maxCallArguments = 16;
  uint8_t maxParseDepth = 32;
  uint32_t maxSteps = 120000;
  int64_t (*nowMilliseconds)(void *userData) = nullptr;
  void *nowUserData = nullptr;
  JsonParseFunction jsonParse = nullptr;
  void *jsonUserData = nullptr;
  AwaitPendingFunction awaitPending = nullptr;
  void *awaitUserData = nullptr;
};

struct Error {
  char message[96] = {};
  uint32_t line = 0;
  uint32_t column = 0;
};

struct RunResult {
  bool ok = false;
  Value value = {};
};

struct MemoryStats {
  size_t workspaceBytes = 0;
  size_t metadataBytes = 0;
  size_t heapBytes = 0;
  size_t heapUsedBytes = 0;
  size_t heapFreeBytes = 0;
  size_t peakHeapUsedBytes = 0;
  size_t bindingCount = 0;
};

struct ScratchAllocator {
  void *context = nullptr;
  void *(*allocate)(void *context, size_t bytes, size_t alignment) = nullptr;
  void (*release)(void *context, void *memory) = nullptr;
  size_t (*available)(void *context) = nullptr;
};

namespace detail {

enum class Tag : uint8_t {
  NumberNaN = 0,
  Undefined = 1,
  Null = 2,
  Boolean = 3,
  String = 4,
  Object = 5,
  Array = 6,
  Set = 7,
  Function = 8,
  Native = 9,
  BuiltinFunction = 10,
  BuiltinObject = 11,
  Source = 12,
  Promise = 13,
  NativeMethod = 14,
  Resolver = 15,
  Date = 16,
};

static constexpr uint64_t kBoxMask = UINT64_C(0x7ff8000000000000);
static constexpr uint64_t kPayloadMask = UINT64_C(0x00000fffffffffff);
static constexpr uint8_t kTagShift = 44;

// Ordinary doubles stay unmodified. Quiet-NaN space stores a 7-bit type and a
// 44-bit payload, keeping every runtime value at eight bytes without unions of
// pointer-sized C++ objects.

inline Value boxed(Tag tag, uint64_t payload = 0) {
  return Value{kBoxMask | (static_cast<uint64_t>(tag) << kTagShift) |
               (payload & kPayloadMask)};
}

inline bool isBoxed(Value value) {
  return (value.bits & kBoxMask) == kBoxMask;
}

inline Tag tagOf(Value value) {
  if (!isBoxed(value)) {
    return Tag::NumberNaN;
  }
  return static_cast<Tag>((value.bits >> kTagShift) & 0x7fU);
}

inline uint64_t payloadOf(Value value) { return value.bits & kPayloadMask; }

inline Value number(double numberValue) {
  Value result;
  std::memcpy(&result.bits, &numberValue, sizeof(result.bits));
  if (isBoxed(result)) {
    return boxed(Tag::NumberNaN);
  }
  return result;
}

inline double numberOf(Value value) {
  if (isBoxed(value)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double result = 0;
  std::memcpy(&result, &value.bits, sizeof(result));
  return result;
}

inline size_t align8(size_t value) { return (value + 7U) & ~size_t(7U); }

inline uint32_t toUint32(double value) {
  if (!std::isfinite(value) || value == 0.0) {
    return 0;
  }
  double wrapped = std::fmod(std::trunc(value), 4294967296.0);
  if (wrapped < 0) {
    wrapped += 4294967296.0;
  }
  return static_cast<uint32_t>(wrapped);
}

inline int32_t toInt32(double value) {
  const uint32_t wrapped = toUint32(value);
  return wrapped < UINT32_C(0x80000000)
             ? static_cast<int32_t>(wrapped)
             : static_cast<int32_t>(
                   -static_cast<int64_t>(UINT64_C(0x100000000) - wrapped));
}

enum class BlockKind : uint8_t {
  Free,
  String,
  Source,
  Object,
  Property,
  Array,
  Set,
  Values,
  Function,
  Promise,
  Date,
  ExternalScratch,
};

// Heap links and boxed object payloads are 32-bit offsets from heap_, not native
// pointers. This halves links on 64-bit tests and keeps PSRAM structures dense
// and relocatable on ESP32.

struct BlockHeader {
  uint32_t size = 0;
  BlockKind kind = BlockKind::Free;
  uint8_t marked = 0;
  uint16_t reserved = 0;
};

struct ObjectPayload {
  uint32_t firstProperty = 0;
  uint32_t reserved = 0;
};

struct PropertyPayload {
  uint32_t next = 0;
  uint32_t reserved = 0;
  Value key = {};
  Value value = {};
};

struct CollectionPayload {
  uint32_t values = 0;
  uint32_t reserved = 0;
};

struct ValuesPayload {
  uint32_t length = 0;
  uint32_t capacity = 0;
};

struct ParameterSpan {
  uint32_t start = 0;
  uint16_t length = 0;
  uint16_t reserved = 0;
};

struct FunctionPayload {
  uint32_t source = 0;
  uint32_t bodyStart = 0;
  uint32_t bodyEnd = 0;
  uint16_t parameterCount = 0;
  uint8_t expressionBody = 0;
  uint8_t asyncBody = 0;
  ParameterSpan name = {};
};

struct PromisePayload {
  Value result = {};
  Value onFulfilled = {};
  Value onRejected = {};
  Value child = {};
  uint8_t state = 0; // 0 pending, 1 fulfilled, 2 rejected, 3 adopted.
  uint8_t queued = 0;
  uint16_t reserved = 0;
};

struct DatePayload {
  double milliseconds = 0;
};

struct ScratchPayload {
  uint32_t next = 0;
  uint32_t dataOffset = 0;
};

struct Binding {
  const char *name = nullptr;
  Value value = {};
  uint32_t nameOwner = 0;
  uint16_t nameLength = 0;
  uint8_t constant = 0;
  uint8_t scopeDepth = 0;
};

struct NativeRecord {
  union {
    NativeFunction function;
    NativeMethodFunction method;
  } callback = {};
  void *userData = nullptr;
};

enum class BuiltinObject : uint8_t {
  Math,
  Json,
  Date,
  Array,
  Object,
  String,
  Number,
  Boolean,
  Promise,
};

enum class BuiltinFunction : uint16_t {
  SetConstructor,
  ParseInt,
  ParseFloat,
  IsNaN,
  MathAbs,
  MathFloor,
  MathCeil,
  MathRound,
  MathMin,
  MathMax,
  MathPow,
  MathSqrt,
  MathTrunc,
  MathRandom,
  JsonParse,
  JsonStringify,
  DateCall,
  DateNow,
  DateParse,
  DateUtc,
  DateToIsoString,
  ArrayIsArray,
  ArrayFrom,
  ObjectKeys,
  ObjectValues,
  ObjectEntries,
  ObjectAssign,
  ObjectHasOwn,
  ObjectFromEntries,
  ArrayPush,
  ArrayPop,
  ArrayShift,
  ArrayUnshift,
  ArraySlice,
  ArrayIndexOf,
  ArrayIncludes,
  ArrayJoin,
  ArrayConcat,
  ArrayForEach,
  ArrayMap,
  ArrayFilter,
  ArrayReduce,
  ArrayFind,
  ArraySome,
  ArrayEvery,
  ArrayReverse,
  ArraySort,
  ArraySplice,
  ArrayAt,
  ArrayEntries,
  ArrayKeys,
  ArrayValues,
  ArrayFindIndex,
  ArrayLastIndexOf,
  ArrayFill,
  ArrayFlat,
  ArrayFlatMap,
  SetAdd,
  SetHas,
  SetDelete,
  SetClear,
  SetValues,
  SetForEach,
  SetEntries,
  StringCharAt,
  StringSlice,
  StringSubstring,
  StringIncludes,
  StringIndexOf,
  StringStartsWith,
  StringEndsWith,
  StringTrim,
  StringUpper,
  StringLower,
  StringSplit,
  StringAt,
  StringReplace,
  StringReplaceAll,
  StringRepeat,
  StringPadStart,
  StringPadEnd,
  StringTrimStart,
  StringTrimEnd,
  StringLastIndexOf,
  StringConcat,
  StringCharCodeAt,
  NumberIsFinite,
  NumberIsNaN,
  NumberIsInteger,
  NumberToString,
  NumberToFixed,
  NumberValueOf,
  StringFromCharCode,
  PromiseResolve,
  PromiseReject,
  PromiseThen,
  PromiseCatch,
  PromiseFinally,
  DateGetTime,
  DateInstanceToIsoString,
  DateGetFullYear,
  DateGetMonth,
  DateGetDate,
  DateGetDay,
  DateGetHours,
  DateGetMinutes,
  DateGetSeconds,
  DateGetMilliseconds,
};

static_assert(static_cast<uint16_t>(BuiltinFunction::DateGetMilliseconds) <
              256U);

template <size_t Width> struct BuiltinProperty {
  char name[Width] = {};
  uint8_t function = 0;
};

template <size_t Width, size_t NameSize>
constexpr BuiltinProperty<Width> builtinProperty(
    const char (&name)[NameSize], BuiltinFunction function) {
  static_assert(NameSize <= Width);
  BuiltinProperty<Width> entry;
  for (size_t i = 0; i < NameSize; i++)
    entry.name[i] = name[i];
  entry.function = static_cast<uint8_t>(function);
  return entry;
}

template <size_t Width, size_t Count>
inline Value findBuiltinProperty(const BuiltinProperty<Width> (&entries)[Count],
                                 const char *key, size_t keyLength) {
  if (keyLength >= Width)
    return boxed(Tag::Undefined);
  for (const BuiltinProperty<Width> &entry : entries) {
    if (entry.name[keyLength] == '\0' &&
        std::memcmp(entry.name, key, keyLength) == 0) {
      return boxed(Tag::BuiltinFunction, entry.function);
    }
  }
  return boxed(Tag::Undefined);
}

} // namespace detail

class Parser;

class Runtime {
public:
  class ScratchLease {
  public:
    ScratchLease() = default;
    ScratchLease(const ScratchLease &) = delete;
    ScratchLease &operator=(const ScratchLease &) = delete;
    ScratchLease(ScratchLease &&) = delete;
    ScratchLease &operator=(ScratchLease &&) = delete;
    ~ScratchLease();

    void *allocate(size_t bytes,
                   size_t alignment = alignof(std::max_align_t));
    void release(void *memory);
    size_t available() const;
    ScratchAllocator allocator();
    explicit operator bool() const { return runtime_ != nullptr; }

  private:
    friend class Runtime;
    explicit ScratchLease(Runtime *runtime) : runtime_(runtime) {}

    static void *allocateCallback(void *context, size_t bytes,
                                  size_t alignment);
    static void releaseCallback(void *context, void *memory);
    static size_t availableCallback(void *context);

    Runtime *runtime_ = nullptr;
    uint32_t firstBlock_ = 0;
  };

  Runtime(void *workspace, size_t workspaceBytes,
          const Options &options = Options{})
      : workspace_(static_cast<uint8_t *>(workspace)),
        workspaceBytes_(workspaceBytes), options_(options) {
    initializeWorkspace();
  }

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  bool ready() const { return ready_; }
  const Error &error() const { return error_; }

  Value undefined() const { return detail::boxed(detail::Tag::Undefined); }
  Value null() const { return detail::boxed(detail::Tag::Null); }
  Value boolean(bool value) const {
    return detail::boxed(detail::Tag::Boolean, value ? 1U : 0U);
  }
  Value number(double value) const { return detail::number(value); }

  ValueType typeOf(Value value) const;
  bool isCallable(Value value) const;
  bool isTruthy(Value value) const;
  bool isUndefined(Value value) const {
    return detail::tagOf(value) == detail::Tag::Undefined;
  }
  bool isString(Value value) const {
    return detail::tagOf(value) == detail::Tag::String;
  }
  bool isNumber(Value value) const {
    return !detail::isBoxed(value) ||
           detail::tagOf(value) == detail::Tag::NumberNaN;
  }
  double toNumber(Value value) const;
  bool toBoolean(Value value) const { return isTruthy(value); }
  StringView stringView(Value value) const;
  size_t format(Value value, char *out, size_t outSize) const;

  Value makeString(const char *text, size_t length);
  Value makeString(const char *text) {
    return makeString(text, text != nullptr ? std::strlen(text) : 0);
  }
  Value makeStringBuffer(size_t length, char **data);
  Value makeObject();
  Value makeArray(size_t reserve = 0);
  Value makeSet();
  Value makePromise();
  bool arrayPush(Value array, Value value);
  size_t arrayLength(Value array) const;
  Value arrayAt(Value array, size_t index) const;
  bool arraySet(Value array, size_t index, Value value);
  bool objectSet(Value object, const char *key, Value value) {
    return objectSet(object, key, key != nullptr ? std::strlen(key) : 0, value);
  }
  bool objectSet(Value object, const char *key, size_t keyLength, Value value);
  Value objectGet(Value object, const char *key) const {
    return objectGet(object, key, key != nullptr ? std::strlen(key) : 0);
  }
  Value objectGet(Value object, const char *key, size_t keyLength) const;
  size_t objectPropertyCount(Value object) const;
  bool objectPropertyAt(Value object, size_t index, Value *key,
                        Value *value) const;

  bool bind(const char *path, NativeFunction function,
            void *userData = nullptr);
  bool bindMethod(const char *path, NativeMethodFunction function,
                  void *userData = nullptr);
  bool set(const char *path, Value value, bool constant = true);
  Value get(const char *name) const;

  RunResult eval(const char *source, size_t length);
  RunResult eval(const char *source) {
    return eval(source, source != nullptr ? std::strlen(source) : 0);
  }
  RunResult call(const char *name, const Value *args = nullptr,
                 size_t argc = 0);
  RunResult call(Value function, const Value *args = nullptr, size_t argc = 0);
  RunResult callIfFunction(const char *name, const Value *args, size_t argc,
                           bool *called);

  void collectGarbage();
  RootHandle retain(Value value);
  void release(RootHandle handle);
  Value retained(RootHandle handle) const;
  PromiseState promiseState(Value promise) const;
  Value promiseResult(Value promise) const;
  bool settlePromise(Value promise, Value result, bool rejected = false);
  Value parseJsonPromise(Value text);
  RunResult drainJobs(size_t maximum = 8);
  MemoryStats memoryStats() const;
  ScratchLease leaseScratch();

  // Native functions use fail() to raise a normal script error.
  Value fail(const char *message) {
    if (message != nullptr) {
      setError(message, nullptr, 0);
    } else {
      setError(ErrorCode{219}, nullptr, 0);
    }
    return undefined();
  }
  Value fail(ErrorCode code) {
    setError(code, nullptr, 0);
    return undefined();
  }

private:
  friend class Parser;
  friend class JsonReader;

  struct NameRef {
    const char *data = nullptr;
    uint16_t length = 0;
    uint32_t owner = 0;
  };

  uint8_t *workspace_ = nullptr;
  size_t workspaceBytes_ = 0;
  Options options_ = {};
  detail::Binding *bindings_ = nullptr;
  detail::NativeRecord *natives_ = nullptr;
  Value *temporaryRoots_ = nullptr;
  Value *hostRoots_ = nullptr;
  uint8_t *hostRootUsed_ = nullptr;
  uint32_t *gcWork_ = nullptr;
  uint8_t *heap_ = nullptr;
  uint32_t heapCapacity_ = 0;
  uint32_t heapUsed_ = 0;
  uint32_t peakHeapUsed_ = 0;
  uint16_t bindingCount_ = 0;
  uint16_t globalBindingCount_ = 0;
  uint16_t nativeCount_ = 0;
  uint16_t temporaryRootCount_ = 0;
  uint16_t frameBase_ = 0;
  uint16_t scopeDepth_ = 0;
  uint8_t callDepth_ = 0;
  uint8_t parseDepth_ = 0;
  uint32_t stepsRemaining_ = 0;
  uint32_t randomState_ = 0x91e10da5U;
  Value lastResult_ = {};
  Error error_ = {};
  bool ready_ = false;
  bool running_ = false;

  void initializeWorkspace();
  void clearError() { error_ = Error{}; }
  void setError(const char *message, const char *source, size_t offset);
  void setError(ErrorCode code, const char *source, size_t offset);
  void setErrorLocation(const char *source, size_t offset);
  bool step(const char *source, size_t offset);

  detail::BlockHeader *header(uint32_t offset) {
    return reinterpret_cast<detail::BlockHeader *>(heap_ + offset);
  }
  const detail::BlockHeader *header(uint32_t offset) const {
    return reinterpret_cast<const detail::BlockHeader *>(heap_ + offset);
  }
  void *payload(uint32_t offset) {
    return heap_ + offset + sizeof(detail::BlockHeader);
  }
  const void *payload(uint32_t offset) const {
    return heap_ + offset + sizeof(detail::BlockHeader);
  }
  bool validOffset(uint32_t offset, detail::BlockKind expected) const;
  uint32_t allocateBlock(detail::BlockKind kind, size_t payloadBytes,
                         bool reportOutOfMemory = true);
  void *allocateScratch(ScratchLease &lease, size_t bytes, size_t alignment);
  void releaseScratch(ScratchLease &lease, void *memory);
  void releaseScratch(ScratchLease &lease);
  size_t scratchAvailable() const;
  void coalesceFreeBlocks();
  Value makeTextBlock(detail::BlockKind kind, detail::Tag tag,
                      const char *text, size_t length, char **data = nullptr);
  uint32_t sourceOffset(Value source) const;
  const char *sourceData(uint32_t sourceOffset) const;
  size_t sourceLength(uint32_t sourceOffset) const;
  void setTextLength(Value value, size_t length);

  detail::CollectionPayload *collection(Value value);
  const detail::CollectionPayload *collection(Value value) const;
  detail::ValuesPayload *valuesPayload(uint32_t offset);
  const detail::ValuesPayload *valuesPayload(uint32_t offset) const;
  Value *valuesData(detail::ValuesPayload *values) const {
    return reinterpret_cast<Value *>(reinterpret_cast<uint8_t *>(values) +
                                     sizeof(*values));
  }
  const Value *valuesData(const detail::ValuesPayload *values) const {
    return reinterpret_cast<const Value *>(
        reinterpret_cast<const uint8_t *>(values) + sizeof(*values));
  }
  bool reserveCollection(Value collectionValue, size_t capacity);
  bool collectionPush(Value collectionValue, Value value, bool unique);
  bool collectionRemoveAt(Value collectionValue, size_t index, Value *removed);

  int findBinding(NameRef name) const;
  bool defineBinding(NameRef name, Value value, bool constant);
  bool assignBinding(size_t index, Value value);
  Value builtinGlobal(NameRef name) const;
  uint16_t enterScope();
  void leaveScope(uint16_t marker);

  int findProperty(Value object, const char *key, size_t keyLength) const;
  detail::PropertyPayload *propertyPayload(uint32_t offset) {
    return static_cast<detail::PropertyPayload *>(payload(offset));
  }
  const detail::PropertyPayload *propertyPayload(uint32_t offset) const {
    return static_cast<const detail::PropertyPayload *>(payload(offset));
  }
  Value getProperty(Value receiver, const char *key, size_t keyLength);
  bool setProperty(Value receiver, const char *key, size_t keyLength,
                   Value value);
  bool getIndex(Value receiver, Value key, Value *result, NameRef *name,
                int64_t *index, bool *numeric);
  bool setIndex(Value receiver, NameRef name, int64_t index, bool numeric,
                Value value);

  Value makeFunction(uint32_t source, uint32_t bodyStart, uint32_t bodyEnd,
                     const detail::ParameterSpan *parameters,
                     size_t parameterCount, bool expressionBody,
                     bool asyncBody = false,
                     detail::ParameterSpan name = {});
  Value makeDate(double milliseconds);
  detail::PromisePayload *promisePayload(Value promise);
  const detail::PromisePayload *promisePayload(Value promise) const;
  Value awaitValue(Value value);
  bool adoptPromise(Value promise, Value result, bool rejected);
  Value promiseThen(Value promise, Value onFulfilled, Value onRejected);
  Value callValue(Value function, Value receiver, const Value *args,
                  size_t argc);
  Value callBuiltin(detail::BuiltinFunction function, Value receiver,
                    const Value *args, size_t argc);
  Value builtinProperty(detail::BuiltinObject object, const char *key,
                        size_t keyLength) const;

  Value toStringValue(Value value);
  size_t formatDepth(Value value, char *out, size_t outSize,
                     uint8_t depth) const;
  bool strictEqual(Value left, Value right) const;
  int compare(Value left, Value right) const;
  Value concatenate(Value left, Value right);
  bool appendStringAssignment(Value left, Value right, Value *result);
  size_t valueReferenceCount(Value target, size_t limit) const;

  bool pushRoot(Value value);
  void popRoots(uint16_t count);
  void markOffset(uint32_t offset, uint32_t &workCount, bool &overflow);
  void markValue(Value value, uint32_t &workCount, bool &overflow);

  Value jsonParse(StringView json);
  Value jsonStringify(Value value);
  size_t jsonMeasure(Value value, uint32_t *parents, size_t depth, bool &ok);
  char *jsonWrite(Value value, char *out, uint32_t *parents, size_t depth,
                  bool &ok);

  int64_t nowMilliseconds() const;
  static int64_t daysFromCivil(int year, unsigned month, unsigned day);
  static void civilFromDays(int64_t days, int &year, unsigned &month,
                            unsigned &day);
  Value isoDate(double milliseconds);
};

inline Runtime::ScratchLease::~ScratchLease() {
  if (runtime_ != nullptr)
    runtime_->releaseScratch(*this);
}

inline void *Runtime::ScratchLease::allocate(size_t bytes, size_t alignment) {
  return runtime_ != nullptr
             ? runtime_->allocateScratch(*this, bytes, alignment)
             : nullptr;
}

inline void Runtime::ScratchLease::release(void *memory) {
  if (runtime_ != nullptr)
    runtime_->releaseScratch(*this, memory);
}

inline size_t Runtime::ScratchLease::available() const {
  return runtime_ != nullptr ? runtime_->scratchAvailable() : 0;
}

inline ScratchAllocator Runtime::ScratchLease::allocator() {
  return {this, allocateCallback, releaseCallback, availableCallback};
}

inline void *Runtime::ScratchLease::allocateCallback(void *context,
                                                     size_t bytes,
                                                     size_t alignment) {
  return context != nullptr
             ? static_cast<ScratchLease *>(context)->allocate(bytes, alignment)
             : nullptr;
}

inline void Runtime::ScratchLease::releaseCallback(void *context,
                                                   void *memory) {
  if (context != nullptr)
    static_cast<ScratchLease *>(context)->release(memory);
}

inline size_t Runtime::ScratchLease::availableCallback(void *context) {
  return context != nullptr
             ? static_cast<ScratchLease *>(context)->available()
             : 0;
}

inline void Runtime::initializeWorkspace() {
  if (workspace_ == nullptr || workspaceBytes_ < 1024 ||
      options_.maxBindings == 0 || options_.maxNatives == 0 ||
      options_.maxTemporaryRoots == 0 || options_.maxHostRoots == 0 ||
      options_.maxGcWork == 0 ||
      options_.maxCallArguments == 0 || options_.maxParseDepth == 0) {
    setError(ErrorCode{1}, nullptr, 0);
    return;
  }

  uintptr_t cursor = reinterpret_cast<uintptr_t>(workspace_);
  const uintptr_t end = cursor + workspaceBytes_;
  cursor = (cursor + 7U) & ~uintptr_t(7U);
  auto take = [&cursor, end](size_t bytes) -> void * {
    const size_t aligned = detail::align8(bytes);
    if (cursor > end || aligned > end - cursor) {
      return nullptr;
    }
    void *result = reinterpret_cast<void *>(cursor);
    cursor += aligned;
    return result;
  };

  bindings_ = static_cast<detail::Binding *>(
      take(sizeof(detail::Binding) * options_.maxBindings));
  natives_ = static_cast<detail::NativeRecord *>(
      take(sizeof(detail::NativeRecord) * options_.maxNatives));
  temporaryRoots_ = static_cast<Value *>(
      take(sizeof(Value) * options_.maxTemporaryRoots));
  hostRoots_ =
      static_cast<Value *>(take(sizeof(Value) * options_.maxHostRoots));
  hostRootUsed_ =
      static_cast<uint8_t *>(take(sizeof(uint8_t) * options_.maxHostRoots));
  gcWork_ = static_cast<uint32_t *>(
      take(sizeof(uint32_t) * options_.maxGcWork));
  if (bindings_ == nullptr || natives_ == nullptr ||
      temporaryRoots_ == nullptr || hostRoots_ == nullptr ||
      hostRootUsed_ == nullptr || gcWork_ == nullptr || cursor >= end ||
      end - cursor < 512 || end - cursor > UINT32_MAX) {
    bindings_ = nullptr;
    setError(ErrorCode{2}, nullptr, 0);
    return;
  }

  std::memset(workspace_, 0, workspaceBytes_);
  heap_ = reinterpret_cast<uint8_t *>(cursor);
  heapCapacity_ = static_cast<uint32_t>(end - cursor);
  heapUsed_ = 8; // Offset zero remains the null link in heap structures.
  peakHeapUsed_ = heapUsed_;
  lastResult_ = undefined();
  randomState_ ^= static_cast<uint32_t>(nowMilliseconds());
  ready_ = true;
}

inline void Runtime::setError(const char *message, const char *source,
                              size_t offset) {
  if (error_.message[0] != '\0') {
    return;
  }
  if (message == nullptr) {
    setError(ErrorCode{220}, source, offset);
    return;
  }
  std::snprintf(error_.message, sizeof(error_.message), "%s", message);
  setErrorLocation(source, offset);
}

inline void Runtime::setError(ErrorCode code, const char *source,
                              size_t offset) {
  if (error_.message[0] != '\0') {
    return;
  }
  formatErrorCode(code, error_.message);
  setErrorLocation(source, offset);
}

inline void Runtime::setErrorLocation(const char *source, size_t offset) {
  if (source == nullptr) {
    return;
  }
  uint32_t line = 1;
  uint32_t column = 1;
  for (size_t i = 0; i < offset && source[i] != '\0'; i++) {
    if (source[i] == '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  error_.line = line;
  error_.column = column;
}

inline bool Runtime::step(const char *source, size_t offset) {
  if (stepsRemaining_ == 0) {
    setError(ErrorCode{3}, source, offset);
    return false;
  }
  stepsRemaining_--;
  return true;
}

inline bool Runtime::validOffset(uint32_t offset,
                                 detail::BlockKind expected) const {
  return offset >= 8 && offset <= heapUsed_ &&
         sizeof(detail::BlockHeader) <= heapUsed_ - offset &&
         header(offset)->size >= sizeof(detail::BlockHeader) &&
         header(offset)->size <= heapUsed_ - offset &&
         header(offset)->kind == expected;
}

inline uint32_t Runtime::allocateBlock(detail::BlockKind kind,
                                      size_t payloadBytes,
                                      bool reportOutOfMemory) {
  constexpr size_t kAlignmentMask = 7U;
  if (!ready_ ||
      payloadBytes > UINT32_MAX - sizeof(detail::BlockHeader) -
                         kAlignmentMask) {
    if (reportOutOfMemory)
      setError(ErrorCode{4}, nullptr, 0);
    return 0;
  }
  const size_t wantedSize = detail::align8(
      sizeof(detail::BlockHeader) + payloadBytes);
  const uint32_t wanted = static_cast<uint32_t>(wantedSize);

  for (uint32_t offset = 8; offset < heapUsed_;) {
    detail::BlockHeader *candidate = header(offset);
    if (candidate->size < sizeof(detail::BlockHeader) ||
        candidate->size > heapUsed_ - offset) {
      setError(ErrorCode{5}, nullptr, 0);
      return 0;
    }
    const uint32_t oldSize = candidate->size;
    if (candidate->kind == detail::BlockKind::Free && oldSize >= wanted) {
      const uint32_t remainder = oldSize - wanted;
      candidate->size = wanted;
      candidate->kind = kind;
      candidate->marked = 0;
      candidate->reserved = 0;
      std::memset(payload(offset), 0, wanted - sizeof(*candidate));
      if (remainder >= detail::align8(sizeof(detail::BlockHeader) + 8U)) {
        detail::BlockHeader *tail = header(offset + wanted);
        *tail = detail::BlockHeader{};
        tail->size = remainder;
      } else {
        candidate->size = oldSize;
      }
      return offset;
    }
    offset += oldSize;
  }

  if (wanted > heapCapacity_ - heapUsed_) {
    if (reportOutOfMemory)
      setError(ErrorCode{4}, nullptr, 0);
    return 0;
  }
  const uint32_t offset = heapUsed_;
  detail::BlockHeader *created = header(offset);
  *created = detail::BlockHeader{};
  created->size = wanted;
  created->kind = kind;
  std::memset(payload(offset), 0, wanted - sizeof(*created));
  heapUsed_ += wanted;
  if (heapUsed_ > peakHeapUsed_) {
    peakHeapUsed_ = heapUsed_;
  }
  return offset;
}

inline Runtime::ScratchLease Runtime::leaseScratch() {
  if (!ready_)
    return ScratchLease();
  collectGarbage();
  return ScratchLease(this);
}

inline void *Runtime::allocateScratch(ScratchLease &lease, size_t bytes,
                                     size_t alignment) {
  if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1U)) != 0)
    return nullptr;
  if (alignment < alignof(void *))
    alignment = alignof(void *);
  if (bytes > UINT32_MAX - sizeof(detail::ScratchPayload) ||
      alignment - 1U > UINT32_MAX - sizeof(detail::ScratchPayload) - bytes) {
    return nullptr;
  }
  const size_t payloadBytes =
      sizeof(detail::ScratchPayload) + alignment - 1U + bytes;
  const uint32_t offset = allocateBlock(detail::BlockKind::ExternalScratch,
                                        payloadBytes, false);
  if (offset == 0)
    return nullptr;
  auto *scratch = static_cast<detail::ScratchPayload *>(payload(offset));
  const uintptr_t begin = reinterpret_cast<uintptr_t>(scratch + 1);
  const uintptr_t aligned = (begin + alignment - 1U) & ~(alignment - 1U);
  const uintptr_t blockEnd =
      reinterpret_cast<uintptr_t>(header(offset)) + header(offset)->size;
  if (aligned > blockEnd || bytes > blockEnd - aligned) {
    header(offset)->kind = detail::BlockKind::Free;
    coalesceFreeBlocks();
    return nullptr;
  }
  scratch->next = lease.firstBlock_;
  scratch->dataOffset =
      static_cast<uint32_t>(aligned - reinterpret_cast<uintptr_t>(header(offset)));
  lease.firstBlock_ = offset;
  return reinterpret_cast<void *>(aligned);
}

inline void Runtime::releaseScratch(ScratchLease &lease, void *memory) {
  if (memory == nullptr)
    return;
  uint32_t *link = &lease.firstBlock_;
  while (*link != 0) {
    const uint32_t offset = *link;
    if (!validOffset(offset, detail::BlockKind::ExternalScratch))
      return;
    auto *scratch = static_cast<detail::ScratchPayload *>(payload(offset));
    if (reinterpret_cast<uintptr_t>(header(offset)) + scratch->dataOffset ==
        reinterpret_cast<uintptr_t>(memory)) {
      *link = scratch->next;
      header(offset)->kind = detail::BlockKind::Free;
      coalesceFreeBlocks();
      return;
    }
    link = &scratch->next;
  }
}

inline void Runtime::releaseScratch(ScratchLease &lease) {
  uint32_t offset = lease.firstBlock_;
  lease.firstBlock_ = 0;
  while (offset != 0) {
    if (!validOffset(offset, detail::BlockKind::ExternalScratch))
      return;
    auto *scratch = static_cast<detail::ScratchPayload *>(payload(offset));
    const uint32_t next = scratch->next;
    header(offset)->kind = detail::BlockKind::Free;
    offset = next;
  }
  coalesceFreeBlocks();
}

inline size_t Runtime::scratchAvailable() const {
  size_t bytes = heapCapacity_ > heapUsed_ ? heapCapacity_ - heapUsed_ : 0;
  for (uint32_t offset = 8; offset < heapUsed_;
       offset += header(offset)->size) {
    if (header(offset)->kind == detail::BlockKind::Free)
      bytes += header(offset)->size;
  }
  return bytes;
}

inline void Runtime::coalesceFreeBlocks() {
  uint32_t offset = 8;
  while (offset < heapUsed_) {
    detail::BlockHeader *block = header(offset);
    if (block->size < sizeof(detail::BlockHeader) ||
        block->size > heapUsed_ - offset) {
      setError(ErrorCode{5}, nullptr, 0);
      return;
    }
    if (block->kind == detail::BlockKind::Free) {
      while (offset + block->size < heapUsed_) {
        const uint32_t nextOffset = offset + block->size;
        detail::BlockHeader *next = header(nextOffset);
        if (next->size < sizeof(detail::BlockHeader) ||
            next->size > heapUsed_ - nextOffset) {
          setError(ErrorCode{5}, nullptr, 0);
          return;
        }
        if (next->kind != detail::BlockKind::Free)
          break;
        block->size += next->size;
      }
      if (offset + block->size == heapUsed_) {
        heapUsed_ = offset;
        return;
      }
    }
    offset += block->size;
  }
}

inline Value Runtime::makeTextBlock(detail::BlockKind kind, detail::Tag tag,
                                    const char *text, size_t length,
                                    char **data) {
  if (length > UINT32_MAX - sizeof(uint32_t) - 1U) {
    return fail(ErrorCode{6});
  }
  const uint32_t offset =
      allocateBlock(kind, sizeof(uint32_t) + length + 1U);
  if (offset == 0) {
    return undefined();
  }
  uint8_t *blockData = static_cast<uint8_t *>(payload(offset));
  const uint32_t storedLength = static_cast<uint32_t>(length);
  std::memcpy(blockData, &storedLength, sizeof(storedLength));
  char *characters = reinterpret_cast<char *>(blockData + sizeof(uint32_t));
  if (text != nullptr && length > 0) {
    std::memcpy(characters, text, length);
  }
  characters[length] = '\0';
  if (data != nullptr) {
    *data = characters;
  }
  return detail::boxed(tag, offset);
}

inline Value Runtime::makeString(const char *text, size_t length) {
  return makeTextBlock(detail::BlockKind::String, detail::Tag::String, text,
                       length);
}

inline Value Runtime::makeStringBuffer(size_t length, char **data) {
  if (data != nullptr) {
    *data = nullptr;
  }
  return makeTextBlock(detail::BlockKind::String, detail::Tag::String, nullptr,
                       length, data);
}

inline void Runtime::setTextLength(Value value, size_t length) {
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(value));
  const detail::Tag tag = detail::tagOf(value);
  const detail::BlockKind kind =
      tag == detail::Tag::String ? detail::BlockKind::String
                                 : detail::BlockKind::Source;
  if (!validOffset(offset, kind) || length > UINT32_MAX) {
    return;
  }
  uint8_t *blockData = static_cast<uint8_t *>(payload(offset));
  uint32_t capacity = header(offset)->size - sizeof(detail::BlockHeader) -
                      sizeof(uint32_t) - 1U;
  if (length > capacity) {
    return;
  }
  const uint32_t stored = static_cast<uint32_t>(length);
  std::memcpy(blockData, &stored, sizeof(stored));
  reinterpret_cast<char *>(blockData + sizeof(uint32_t))[length] = '\0';
}

inline uint32_t Runtime::sourceOffset(Value source) const {
  return detail::tagOf(source) == detail::Tag::Source
             ? static_cast<uint32_t>(detail::payloadOf(source))
             : 0;
}

inline const char *Runtime::sourceData(uint32_t offset) const {
  if (!validOffset(offset, detail::BlockKind::Source)) {
    return nullptr;
  }
  return reinterpret_cast<const char *>(payload(offset)) + sizeof(uint32_t);
}

inline size_t Runtime::sourceLength(uint32_t offset) const {
  if (!validOffset(offset, detail::BlockKind::Source)) {
    return 0;
  }
  uint32_t length = 0;
  std::memcpy(&length, payload(offset), sizeof(length));
  return length;
}

inline StringView Runtime::stringView(Value value) const {
  if (detail::tagOf(value) != detail::Tag::String) {
    return {};
  }
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(value));
  if (!validOffset(offset, detail::BlockKind::String)) {
    return {};
  }
  uint32_t length = 0;
  std::memcpy(&length, payload(offset), sizeof(length));
  return {reinterpret_cast<const char *>(payload(offset)) + sizeof(uint32_t),
          length};
}

inline ValueType Runtime::typeOf(Value value) const {
  if (!detail::isBoxed(value) ||
      detail::tagOf(value) == detail::Tag::NumberNaN) {
    return ValueType::Number;
  }
  switch (detail::tagOf(value)) {
  case detail::Tag::Undefined:
    return ValueType::Undefined;
  case detail::Tag::Null:
    return ValueType::Null;
  case detail::Tag::Boolean:
    return ValueType::Boolean;
  case detail::Tag::String:
    return ValueType::String;
  case detail::Tag::Object:
  case detail::Tag::Date:
    return ValueType::Object;
  case detail::Tag::Promise:
    return ValueType::Promise;
  case detail::Tag::BuiltinObject: {
    const detail::BuiltinObject object =
        static_cast<detail::BuiltinObject>(detail::payloadOf(value));
    return object == detail::BuiltinObject::Math ||
                   object == detail::BuiltinObject::Json
               ? ValueType::Object
               : ValueType::Function;
  }
  case detail::Tag::Array:
    return ValueType::Array;
  case detail::Tag::Set:
    return ValueType::Set;
  case detail::Tag::Function:
  case detail::Tag::Native:
  case detail::Tag::NativeMethod:
  case detail::Tag::Resolver:
  case detail::Tag::BuiltinFunction:
    return ValueType::Function;
  default:
    return ValueType::Undefined;
  }
}

inline bool Runtime::isCallable(Value value) const {
  const detail::Tag tag = detail::tagOf(value);
  return tag == detail::Tag::Function || tag == detail::Tag::Native ||
         tag == detail::Tag::NativeMethod || tag == detail::Tag::Resolver ||
         tag == detail::Tag::BuiltinFunction ||
         (tag == detail::Tag::BuiltinObject &&
          static_cast<detail::BuiltinObject>(detail::payloadOf(value)) !=
              detail::BuiltinObject::Math &&
          static_cast<detail::BuiltinObject>(detail::payloadOf(value)) !=
              detail::BuiltinObject::Json);
}

inline bool Runtime::isTruthy(Value value) const {
  switch (typeOf(value)) {
  case ValueType::Undefined:
  case ValueType::Null:
    return false;
  case ValueType::Boolean:
    return detail::payloadOf(value) != 0;
  case ValueType::Number: {
    const double numeric = detail::numberOf(value);
    return numeric != 0.0 && !std::isnan(numeric);
  }
  case ValueType::String:
    return stringView(value).size != 0;
  default:
    return true;
  }
}

inline double Runtime::toNumber(Value value) const {
  switch (typeOf(value)) {
  case ValueType::Number:
    return detail::numberOf(value);
  case ValueType::Boolean:
    return detail::payloadOf(value) != 0 ? 1.0 : 0.0;
  case ValueType::Null:
    return 0.0;
  case ValueType::String: {
    const StringView text = stringView(value);
    if (text.data == nullptr || text.size == 0) {
      return 0.0;
    }
    char *end = nullptr;
    const double parsed = std::strtod(text.data, &end);
    return end == text.data ? std::numeric_limits<double>::quiet_NaN() : parsed;
  }
  default:
    return std::numeric_limits<double>::quiet_NaN();
  }
}

inline size_t Runtime::format(Value value, char *out, size_t outSize) const {
  return formatDepth(value, out, outSize, 0);
}

inline size_t Runtime::formatDepth(Value value, char *out, size_t outSize,
                                  uint8_t depth) const {
  size_t written = 0;
  auto append = [&out, outSize, &written](const char *text, size_t length) {
    if (out != nullptr && outSize > 0 && written < outSize - 1U) {
      const size_t room = outSize - 1U - written;
      const size_t amount = length < room ? length : room;
      std::memcpy(out + written, text, amount);
    }
    written += length;
  };

  char numberText[40];
  switch (typeOf(value)) {
  case ValueType::Undefined:
    append("undefined", 9);
    break;
  case ValueType::Null:
    append("null", 4);
    break;
  case ValueType::Boolean:
    if (detail::payloadOf(value) != 0) {
      append("true", 4);
    } else {
      append("false", 5);
    }
    break;
  case ValueType::Number: {
    const double numeric = detail::numberOf(value);
    if (std::isnan(numeric)) {
      append("NaN", 3);
    } else if (std::isinf(numeric)) {
      append(numeric < 0 ? "-Infinity" : "Infinity", numeric < 0 ? 9 : 8);
    } else {
      const int length = std::snprintf(numberText, sizeof(numberText), "%.15g",
                                       numeric);
      if (length > 0) {
        append(numberText, static_cast<size_t>(length));
      }
    }
    break;
  }
  case ValueType::String: {
    const StringView text = stringView(value);
    append(text.data != nullptr ? text.data : "", text.size);
    break;
  }
  case ValueType::Array: {
    if (depth >= 12) {
      append("[...]", 5);
      break;
    }
    const size_t length = arrayLength(value);
    for (size_t i = 0; i < length; i++) {
      if (i != 0) {
        append(",", 1);
      }
      char *elementOut =
          out != nullptr && written < outSize ? out + written : nullptr;
      const size_t elementRoom =
          out != nullptr && written < outSize ? outSize - written : 0;
      written +=
          formatDepth(arrayAt(value, i), elementOut, elementRoom, depth + 1U);
    }
    break;
  }
  case ValueType::Set:
    append("[object Set]", 12);
    break;
  case ValueType::Promise:
    append("[object Promise]", 16);
    break;
  case ValueType::Object:
    if (detail::tagOf(value) == detail::Tag::Date) {
      append("[object Date]", 13);
    } else {
      append("[object Object]", 15);
    }
    break;
  case ValueType::Function:
    append("[function]", 10);
    break;
  }
  if (out != nullptr && outSize > 0) {
    out[written < outSize ? written : outSize - 1U] = '\0';
  }
  return written;
}

inline Value Runtime::toStringValue(Value value) {
  if (isString(value)) {
    return value;
  }
  const size_t length = format(value, nullptr, 0);
  char *buffer = nullptr;
  Value result = makeStringBuffer(length, &buffer);
  if (buffer != nullptr) {
    format(value, buffer, length + 1U);
  }
  return result;
}

inline Value Runtime::makeObject() {
  const uint32_t offset = allocateBlock(detail::BlockKind::Object,
                                        sizeof(detail::ObjectPayload));
  return offset != 0 ? detail::boxed(detail::Tag::Object, offset)
                     : undefined();
}

inline Value Runtime::makeArray(size_t reserve) {
  const uint32_t offset = allocateBlock(detail::BlockKind::Array,
                                        sizeof(detail::CollectionPayload));
  if (offset == 0) {
    return undefined();
  }
  Value array = detail::boxed(detail::Tag::Array, offset);
  if (!reserveCollection(array, reserve)) {
    return undefined();
  }
  return array;
}

inline Value Runtime::makeSet() {
  const uint32_t offset = allocateBlock(detail::BlockKind::Set,
                                        sizeof(detail::CollectionPayload));
  if (offset == 0) {
    return undefined();
  }
  Value set = detail::boxed(detail::Tag::Set, offset);
  if (!reserveCollection(set, 4)) {
    return undefined();
  }
  return set;
}

inline Value Runtime::makePromise() {
  const uint32_t offset = allocateBlock(detail::BlockKind::Promise,
                                        sizeof(detail::PromisePayload));
  if (offset == 0) {
    return undefined();
  }
  detail::PromisePayload *promise =
      static_cast<detail::PromisePayload *>(payload(offset));
  promise->result = undefined();
  promise->onFulfilled = undefined();
  promise->onRejected = undefined();
  promise->child = undefined();
  return detail::boxed(detail::Tag::Promise, offset);
}

inline Value Runtime::makeDate(double milliseconds) {
  const uint32_t offset =
      allocateBlock(detail::BlockKind::Date, sizeof(detail::DatePayload));
  if (offset == 0) {
    return undefined();
  }
  static_cast<detail::DatePayload *>(payload(offset))->milliseconds =
      milliseconds;
  return detail::boxed(detail::Tag::Date, offset);
}

inline detail::PromisePayload *Runtime::promisePayload(Value promise) {
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(promise));
  return detail::tagOf(promise) == detail::Tag::Promise &&
                 validOffset(offset, detail::BlockKind::Promise)
             ? static_cast<detail::PromisePayload *>(payload(offset))
             : nullptr;
}

inline const detail::PromisePayload *
Runtime::promisePayload(Value promise) const {
  return const_cast<Runtime *>(this)->promisePayload(promise);
}

inline detail::CollectionPayload *Runtime::collection(Value value) {
  const detail::Tag tag = detail::tagOf(value);
  const detail::BlockKind kind =
      tag == detail::Tag::Array
          ? detail::BlockKind::Array
          : (tag == detail::Tag::Set ? detail::BlockKind::Set
                                     : detail::BlockKind::Free);
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(value));
  return kind != detail::BlockKind::Free && validOffset(offset, kind)
             ? static_cast<detail::CollectionPayload *>(payload(offset))
             : nullptr;
}

inline const detail::CollectionPayload *Runtime::collection(Value value) const {
  return const_cast<Runtime *>(this)->collection(value);
}

inline detail::ValuesPayload *Runtime::valuesPayload(uint32_t offset) {
  return validOffset(offset, detail::BlockKind::Values)
             ? static_cast<detail::ValuesPayload *>(payload(offset))
             : nullptr;
}

inline const detail::ValuesPayload *
Runtime::valuesPayload(uint32_t offset) const {
  return const_cast<Runtime *>(this)->valuesPayload(offset);
}

inline bool Runtime::reserveCollection(Value collectionValue,
                                       size_t capacity) {
  detail::CollectionPayload *owner = collection(collectionValue);
  if (owner == nullptr) {
    setError(ErrorCode{7}, nullptr, 0);
    return false;
  }
  detail::ValuesPayload *oldValues = valuesPayload(owner->values);
  const size_t oldCapacity = oldValues != nullptr ? oldValues->capacity : 0;
  if (capacity <= oldCapacity) {
    return true;
  }
  size_t nextCapacity = oldCapacity == 0 ? 4 : oldCapacity;
  while (nextCapacity < capacity) {
    if (nextCapacity > (UINT32_MAX / 2U)) {
      setError(ErrorCode{8}, nullptr, 0);
      return false;
    }
    nextCapacity *= 2U;
  }
  if (nextCapacity > (UINT32_MAX - sizeof(detail::ValuesPayload)) /
                         sizeof(Value)) {
    setError(ErrorCode{8}, nullptr, 0);
    return false;
  }
  const uint32_t nextOffset = allocateBlock(
      detail::BlockKind::Values,
      sizeof(detail::ValuesPayload) + nextCapacity * sizeof(Value));
  if (nextOffset == 0) {
    return false;
  }
  detail::ValuesPayload *nextValues = valuesPayload(nextOffset);
  nextValues->capacity = static_cast<uint32_t>(nextCapacity);
  if (oldValues != nullptr) {
    nextValues->length = oldValues->length;
    std::memcpy(valuesData(nextValues), valuesData(oldValues),
                oldValues->length * sizeof(Value));
  }
  owner->values = nextOffset;
  return true;
}

inline size_t Runtime::arrayLength(Value array) const {
  const detail::CollectionPayload *owner = collection(array);
  const detail::ValuesPayload *values =
      owner != nullptr ? valuesPayload(owner->values) : nullptr;
  return values != nullptr ? values->length : 0;
}

inline Value Runtime::arrayAt(Value array, size_t index) const {
  const detail::CollectionPayload *owner = collection(array);
  const detail::ValuesPayload *values =
      owner != nullptr ? valuesPayload(owner->values) : nullptr;
  return values != nullptr && index < values->length ? valuesData(values)[index]
                                                     : undefined();
}

inline bool Runtime::arraySet(Value array, size_t index, Value value) {
  detail::CollectionPayload *owner = collection(array);
  detail::ValuesPayload *values =
      owner != nullptr ? valuesPayload(owner->values) : nullptr;
  if (owner == nullptr || index > UINT32_MAX) {
    setError(ErrorCode{9}, nullptr, 0);
    return false;
  }
  if (values == nullptr || index >= values->capacity) {
    if (!reserveCollection(array, index + 1U)) {
      return false;
    }
    values = valuesPayload(owner->values);
  }
  Value *items = valuesData(values);
  while (values->length <= index) {
    items[values->length++] = undefined();
  }
  items[index] = value;
  return true;
}

inline bool Runtime::collectionPush(Value collectionValue, Value value,
                                    bool unique) {
  if (unique) {
    const size_t length = arrayLength(collectionValue);
    for (size_t i = 0; i < length; i++) {
      if (strictEqual(arrayAt(collectionValue, i), value)) {
        return true;
      }
    }
  }
  const size_t length = arrayLength(collectionValue);
  return arraySet(collectionValue, length, value);
}

inline bool Runtime::arrayPush(Value array, Value value) {
  if (detail::tagOf(array) != detail::Tag::Array) {
    setError(ErrorCode{9}, nullptr, 0);
    return false;
  }
  return collectionPush(array, value, false);
}

inline bool Runtime::collectionRemoveAt(Value collectionValue, size_t index,
                                        Value *removed) {
  detail::CollectionPayload *owner = collection(collectionValue);
  detail::ValuesPayload *values =
      owner != nullptr ? valuesPayload(owner->values) : nullptr;
  if (values == nullptr || index >= values->length) {
    if (removed != nullptr) {
      *removed = undefined();
    }
    return false;
  }
  Value *items = valuesData(values);
  if (removed != nullptr) {
    *removed = items[index];
  }
  if (index + 1U < values->length) {
    std::memmove(items + index, items + index + 1U,
                 (values->length - index - 1U) * sizeof(Value));
  }
  values->length--;
  return true;
}

inline int Runtime::findProperty(Value object, const char *key,
                                 size_t keyLength) const {
  if (detail::tagOf(object) != detail::Tag::Object) {
    return 0;
  }
  const uint32_t objectOffset =
      static_cast<uint32_t>(detail::payloadOf(object));
  if (!validOffset(objectOffset, detail::BlockKind::Object)) {
    return 0;
  }
  const detail::ObjectPayload *objectData =
      static_cast<const detail::ObjectPayload *>(payload(objectOffset));
  uint32_t current = objectData->firstProperty;
  while (current != 0 && validOffset(current, detail::BlockKind::Property)) {
    const detail::PropertyPayload *property = propertyPayload(current);
    const StringView propertyKey = stringView(property->key);
    if (propertyKey.size == keyLength && propertyKey.data != nullptr &&
        std::memcmp(propertyKey.data, key, keyLength) == 0) {
      return static_cast<int>(current);
    }
    current = property->next;
  }
  return 0;
}

inline bool Runtime::objectSet(Value object, const char *key, size_t keyLength,
                               Value value) {
  if (detail::tagOf(object) != detail::Tag::Object || key == nullptr) {
    setError(ErrorCode{10}, nullptr, 0);
    return false;
  }
  const int existing = findProperty(object, key, keyLength);
  if (existing != 0) {
    propertyPayload(static_cast<uint32_t>(existing))->value = value;
    return true;
  }
  Value keyValue = makeString(key, keyLength);
  if (isUndefined(keyValue) && error_.message[0] != '\0') {
    return false;
  }
  const uint32_t propertyOffset = allocateBlock(
      detail::BlockKind::Property, sizeof(detail::PropertyPayload));
  if (propertyOffset == 0) {
    return false;
  }
  const uint32_t objectOffset =
      static_cast<uint32_t>(detail::payloadOf(object));
  detail::ObjectPayload *objectData =
      static_cast<detail::ObjectPayload *>(payload(objectOffset));
  detail::PropertyPayload *property = propertyPayload(propertyOffset);
  property->next = objectData->firstProperty;
  property->key = keyValue;
  property->value = value;
  objectData->firstProperty = propertyOffset;
  return true;
}

inline Value Runtime::objectGet(Value object, const char *key,
                                size_t keyLength) const {
  const int found = findProperty(object, key, keyLength);
  return found != 0
             ? propertyPayload(static_cast<uint32_t>(found))->value
             : undefined();
}

inline size_t Runtime::objectPropertyCount(Value object) const {
  if (detail::tagOf(object) != detail::Tag::Object) {
    return 0;
  }
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(object));
  if (!validOffset(offset, detail::BlockKind::Object)) {
    return 0;
  }
  size_t count = 0;
  uint32_t property = static_cast<const detail::ObjectPayload *>(payload(offset))
                          ->firstProperty;
  while (property != 0 && validOffset(property, detail::BlockKind::Property)) {
    count++;
    property = propertyPayload(property)->next;
  }
  return count;
}

inline bool Runtime::objectPropertyAt(Value object, size_t index, Value *key,
                                      Value *value) const {
  const size_t count = objectPropertyCount(object);
  if (index >= count || key == nullptr || value == nullptr) {
    return false;
  }
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(object));
  uint32_t property = static_cast<const detail::ObjectPayload *>(payload(offset))
                          ->firstProperty;
  // Properties are linked newest-first; expose JavaScript insertion order.
  size_t skip = count - index - 1U;
  while (skip-- > 0 && property != 0) {
    property = propertyPayload(property)->next;
  }
  if (property == 0 || !validOffset(property, detail::BlockKind::Property)) {
    return false;
  }
  const detail::PropertyPayload *item = propertyPayload(property);
  *key = item->key;
  *value = item->value;
  return true;
}

inline int Runtime::findBinding(NameRef name) const {
  auto matches = [name](const detail::Binding &binding) {
    return binding.name != nullptr && binding.nameLength == name.length &&
           std::memcmp(binding.name, name.data, name.length) == 0;
  };
  for (size_t i = bindingCount_; i > frameBase_; i--) {
    if (matches(bindings_[i - 1U])) {
      return static_cast<int>(i - 1U);
    }
  }
  const size_t globalLimit = frameBase_ == 0 ? frameBase_ : globalBindingCount_;
  for (size_t i = globalLimit; i > 0; i--) {
    if (matches(bindings_[i - 1U])) {
      return static_cast<int>(i - 1U);
    }
  }
  return -1;
}

inline bool Runtime::defineBinding(NameRef name, Value value, bool constant) {
  const uint16_t visibleStart = frameBase_;
  for (size_t i = bindingCount_; i > visibleStart; i--) {
    const detail::Binding &binding = bindings_[i - 1U];
    if (binding.scopeDepth == scopeDepth_ && binding.nameLength == name.length &&
        std::memcmp(binding.name, name.data, name.length) == 0) {
      setError(ErrorCode{11}, nullptr, 0);
      return false;
    }
  }
  if (bindingCount_ >= options_.maxBindings) {
    setError(ErrorCode{12}, nullptr, 0);
    return false;
  }
  detail::Binding &binding = bindings_[bindingCount_++];
  binding.name = name.data;
  binding.nameLength = name.length;
  binding.nameOwner = name.owner;
  binding.value = value;
  binding.constant = constant ? 1U : 0U;
  binding.scopeDepth = static_cast<uint8_t>(scopeDepth_ > 255U ? 255U
                                                               : scopeDepth_);
  if (frameBase_ == 0 && scopeDepth_ == 0) {
    globalBindingCount_ = bindingCount_;
  }
  return true;
}

inline bool Runtime::assignBinding(size_t index, Value value) {
  if (index >= bindingCount_) {
    setError(ErrorCode{13}, nullptr, 0);
    return false;
  }
  if (bindings_[index].constant != 0) {
    setError(ErrorCode{14}, nullptr, 0);
    return false;
  }
  bindings_[index].value = value;
  return true;
}

inline uint16_t Runtime::enterScope() {
  scopeDepth_++;
  return bindingCount_;
}

inline void Runtime::leaveScope(uint16_t marker) {
  if (marker >= frameBase_ && marker <= bindingCount_) {
    bindingCount_ = marker;
  }
  if (scopeDepth_ > 0) {
    scopeDepth_--;
  }
}

inline Value Runtime::builtinGlobal(NameRef name) const {
  auto equals = [name](const char *text) {
    const size_t length = std::strlen(text);
    return length == name.length &&
           std::memcmp(name.data, text, name.length) == 0;
  };
  if (equals("undefined"))
    return undefined();
  if (equals("NaN"))
    return detail::boxed(detail::Tag::NumberNaN);
  if (equals("Infinity"))
    return number(std::numeric_limits<double>::infinity());
  if (equals("Set"))
    return detail::boxed(detail::Tag::BuiltinFunction,
                         static_cast<uint16_t>(
                             detail::BuiltinFunction::SetConstructor));
  if (equals("parseInt"))
    return detail::boxed(detail::Tag::BuiltinFunction,
                         static_cast<uint16_t>(detail::BuiltinFunction::ParseInt));
  if (equals("parseFloat"))
    return detail::boxed(
        detail::Tag::BuiltinFunction,
        static_cast<uint16_t>(detail::BuiltinFunction::ParseFloat));
  if (equals("isNaN"))
    return detail::boxed(detail::Tag::BuiltinFunction,
                         static_cast<uint16_t>(detail::BuiltinFunction::IsNaN));
  if (equals("Math"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Math));
  if (equals("JSON"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Json));
  if (equals("Date"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Date));
  if (equals("Array"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Array));
  if (equals("Object"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Object));
  if (equals("String"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::String));
  if (equals("Number"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Number));
  if (equals("Boolean"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Boolean));
  if (equals("Promise"))
    return detail::boxed(detail::Tag::BuiltinObject,
                         static_cast<uint8_t>(detail::BuiltinObject::Promise));
  return undefined();
}

inline Value Runtime::get(const char *name) const {
  if (name == nullptr) {
    return undefined();
  }
  NameRef ref{name, static_cast<uint16_t>(std::strlen(name)), 0};
  const int binding = findBinding(ref);
  return binding >= 0 ? bindings_[binding].value : builtinGlobal(ref);
}

inline bool Runtime::set(const char *path, Value value, bool constant) {
  if (path == nullptr || path[0] == '\0') {
    setError(ErrorCode{15}, nullptr, 0);
    return false;
  }
  const char *dot = std::strchr(path, '.');
  const size_t firstLength = dot != nullptr ? static_cast<size_t>(dot - path)
                                            : std::strlen(path);
  if (firstLength == 0 || firstLength > UINT16_MAX) {
    setError(ErrorCode{16}, nullptr, 0);
    return false;
  }
  NameRef first{path, static_cast<uint16_t>(firstLength), 0};
  int binding = findBinding(first);
  if (dot == nullptr) {
    if (binding >= 0) {
      if (bindings_[binding].constant != 0) {
        setError(ErrorCode{17}, nullptr, 0);
        return false;
      }
      bindings_[binding].value = value;
      bindings_[binding].constant = constant ? 1U : 0U;
      return true;
    }
    Value nameValue = makeString(path, firstLength);
    if (error_.message[0] != '\0') {
      return false;
    }
    const StringView storedName = stringView(nameValue);
    return defineBinding(
        NameRef{storedName.data, static_cast<uint16_t>(storedName.size),
                static_cast<uint32_t>(detail::payloadOf(nameValue))},
        value, constant);
  }

  Value current;
  if (binding < 0) {
    current = makeObject();
    if (error_.message[0] != '\0') {
      return false;
    }
    Value nameValue = makeString(path, firstLength);
    if (error_.message[0] != '\0') {
      return false;
    }
    const StringView storedName = stringView(nameValue);
    if (!defineBinding(
            NameRef{storedName.data, static_cast<uint16_t>(storedName.size),
                    static_cast<uint32_t>(detail::payloadOf(nameValue))},
            current, true)) {
      return false;
    }
  } else {
    current = bindings_[binding].value;
  }

  const char *segment = dot + 1;
  while (true) {
    const char *nextDot = std::strchr(segment, '.');
    const size_t segmentLength =
        nextDot != nullptr ? static_cast<size_t>(nextDot - segment)
                           : std::strlen(segment);
    if (segmentLength == 0) {
      setError(ErrorCode{16}, nullptr, 0);
      return false;
    }
    if (nextDot == nullptr) {
      return objectSet(current, segment, segmentLength, value);
    }
    Value next = objectGet(current, segment, segmentLength);
    if (isUndefined(next)) {
      next = makeObject();
      if (!objectSet(current, segment, segmentLength, next)) {
        return false;
      }
    }
    if (detail::tagOf(next) != detail::Tag::Object) {
      setError(ErrorCode{18}, nullptr, 0);
      return false;
    }
    current = next;
    segment = nextDot + 1;
  }
}

inline bool Runtime::bind(const char *path, NativeFunction function,
                          void *userData) {
  if (function == nullptr || nativeCount_ >= options_.maxNatives) {
    setError(ErrorCode{19}, nullptr, 0);
    return false;
  }
  const uint16_t index = nativeCount_++;
  natives_[index].callback.function = function;
  natives_[index].userData = userData;
  if (!set(path, detail::boxed(detail::Tag::Native, index), true)) {
    nativeCount_--;
    return false;
  }
  return true;
}

inline bool Runtime::bindMethod(const char *path,
                                NativeMethodFunction function,
                                void *userData) {
  if (function == nullptr || nativeCount_ >= options_.maxNatives) {
    setError(ErrorCode{19}, nullptr, 0);
    return false;
  }
  const uint16_t index = nativeCount_++;
  natives_[index].callback.method = function;
  natives_[index].userData = userData;
  if (!set(path, detail::boxed(detail::Tag::NativeMethod, index), true)) {
    nativeCount_--;
    return false;
  }
  return true;
}

inline Value Runtime::builtinProperty(detail::BuiltinObject object,
                                      const char *key,
                                      size_t keyLength) const {
  auto is = [key, keyLength](const char *name) {
    const size_t length = std::strlen(name);
    return length == keyLength && std::memcmp(key, name, length) == 0;
  };
  using B = detail::BuiltinFunction;
  switch (object) {
  case detail::BuiltinObject::Math: {
    if (is("PI"))
      return number(3.14159265358979323846);
    if (is("E"))
      return number(2.71828182845904523536);
    static constexpr detail::BuiltinProperty<7> properties[] = {
        detail::builtinProperty<7>("abs", B::MathAbs),
        detail::builtinProperty<7>("floor", B::MathFloor),
        detail::builtinProperty<7>("ceil", B::MathCeil),
        detail::builtinProperty<7>("round", B::MathRound),
        detail::builtinProperty<7>("min", B::MathMin),
        detail::builtinProperty<7>("max", B::MathMax),
        detail::builtinProperty<7>("pow", B::MathPow),
        detail::builtinProperty<7>("sqrt", B::MathSqrt),
        detail::builtinProperty<7>("trunc", B::MathTrunc),
        detail::builtinProperty<7>("random", B::MathRandom),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Json: {
    static constexpr detail::BuiltinProperty<10> properties[] = {
        detail::builtinProperty<10>("parse", B::JsonParse),
        detail::builtinProperty<10>("stringify", B::JsonStringify),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Date: {
    static constexpr detail::BuiltinProperty<12> properties[] = {
        detail::builtinProperty<12>("now", B::DateNow),
        detail::builtinProperty<12>("parse", B::DateParse),
        detail::builtinProperty<12>("UTC", B::DateUtc),
        detail::builtinProperty<12>("toISOString", B::DateToIsoString),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Array: {
    static constexpr detail::BuiltinProperty<8> properties[] = {
        detail::builtinProperty<8>("isArray", B::ArrayIsArray),
        detail::builtinProperty<8>("from", B::ArrayFrom),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Object: {
    static constexpr detail::BuiltinProperty<12> properties[] = {
        detail::builtinProperty<12>("keys", B::ObjectKeys),
        detail::builtinProperty<12>("values", B::ObjectValues),
        detail::builtinProperty<12>("entries", B::ObjectEntries),
        detail::builtinProperty<12>("assign", B::ObjectAssign),
        detail::builtinProperty<12>("hasOwn", B::ObjectHasOwn),
        detail::builtinProperty<12>("fromEntries", B::ObjectFromEntries),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::String: {
    static constexpr detail::BuiltinProperty<13> properties[] = {
        detail::builtinProperty<13>("fromCharCode", B::StringFromCharCode),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Number: {
    if (is("MAX_SAFE_INTEGER"))
      return number(9007199254740991.0);
    if (is("MIN_SAFE_INTEGER"))
      return number(-9007199254740991.0);
    if (is("EPSILON"))
      return number(2.22044604925031308085e-16);
    static constexpr detail::BuiltinProperty<11> properties[] = {
        detail::builtinProperty<11>("isFinite", B::NumberIsFinite),
        detail::builtinProperty<11>("isNaN", B::NumberIsNaN),
        detail::builtinProperty<11>("isInteger", B::NumberIsInteger),
        detail::builtinProperty<11>("parseInt", B::ParseInt),
        detail::builtinProperty<11>("parseFloat", B::ParseFloat),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Promise: {
    static constexpr detail::BuiltinProperty<8> properties[] = {
        detail::builtinProperty<8>("resolve", B::PromiseResolve),
        detail::builtinProperty<8>("reject", B::PromiseReject),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::BuiltinObject::Boolean:
    break;
  }
  return undefined();
}

inline Value Runtime::getProperty(Value receiver, const char *key,
                                  size_t keyLength) {
  auto is = [key, keyLength](const char *name) {
    const size_t length = std::strlen(name);
    return length == keyLength && std::memcmp(key, name, length) == 0;
  };
  using B = detail::BuiltinFunction;
  switch (detail::tagOf(receiver)) {
  case detail::Tag::Object:
    return objectGet(receiver, key, keyLength);
  case detail::Tag::BuiltinObject:
    return builtinProperty(
        static_cast<detail::BuiltinObject>(detail::payloadOf(receiver)), key,
        keyLength);
  case detail::Tag::Array: {
    if (is("length"))
      return number(static_cast<double>(arrayLength(receiver)));
    static constexpr detail::BuiltinProperty<12> properties[] = {
        detail::builtinProperty<12>("push", B::ArrayPush),
        detail::builtinProperty<12>("pop", B::ArrayPop),
        detail::builtinProperty<12>("shift", B::ArrayShift),
        detail::builtinProperty<12>("unshift", B::ArrayUnshift),
        detail::builtinProperty<12>("slice", B::ArraySlice),
        detail::builtinProperty<12>("indexOf", B::ArrayIndexOf),
        detail::builtinProperty<12>("includes", B::ArrayIncludes),
        detail::builtinProperty<12>("join", B::ArrayJoin),
        detail::builtinProperty<12>("concat", B::ArrayConcat),
        detail::builtinProperty<12>("forEach", B::ArrayForEach),
        detail::builtinProperty<12>("map", B::ArrayMap),
        detail::builtinProperty<12>("filter", B::ArrayFilter),
        detail::builtinProperty<12>("reduce", B::ArrayReduce),
        detail::builtinProperty<12>("find", B::ArrayFind),
        detail::builtinProperty<12>("some", B::ArraySome),
        detail::builtinProperty<12>("every", B::ArrayEvery),
        detail::builtinProperty<12>("reverse", B::ArrayReverse),
        detail::builtinProperty<12>("sort", B::ArraySort),
        detail::builtinProperty<12>("splice", B::ArraySplice),
        detail::builtinProperty<12>("at", B::ArrayAt),
        detail::builtinProperty<12>("entries", B::ArrayEntries),
        detail::builtinProperty<12>("keys", B::ArrayKeys),
        detail::builtinProperty<12>("values", B::ArrayValues),
        detail::builtinProperty<12>("findIndex", B::ArrayFindIndex),
        detail::builtinProperty<12>("lastIndexOf", B::ArrayLastIndexOf),
        detail::builtinProperty<12>("fill", B::ArrayFill),
        detail::builtinProperty<12>("flat", B::ArrayFlat),
        detail::builtinProperty<12>("flatMap", B::ArrayFlatMap),
        detail::builtinProperty<12>("toString", B::ArrayJoin),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::Tag::Set: {
    if (is("size"))
      return number(static_cast<double>(arrayLength(receiver)));
    static constexpr detail::BuiltinProperty<8> properties[] = {
        detail::builtinProperty<8>("add", B::SetAdd),
        detail::builtinProperty<8>("has", B::SetHas),
        detail::builtinProperty<8>("delete", B::SetDelete),
        detail::builtinProperty<8>("clear", B::SetClear),
        detail::builtinProperty<8>("values", B::SetValues),
        detail::builtinProperty<8>("keys", B::SetValues),
        detail::builtinProperty<8>("entries", B::SetEntries),
        detail::builtinProperty<8>("forEach", B::SetForEach),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::Tag::String: {
    if (is("length"))
      return number(static_cast<double>(stringView(receiver).size));
    static constexpr detail::BuiltinProperty<12> properties[] = {
        detail::builtinProperty<12>("charAt", B::StringCharAt),
        detail::builtinProperty<12>("slice", B::StringSlice),
        detail::builtinProperty<12>("substring", B::StringSubstring),
        detail::builtinProperty<12>("includes", B::StringIncludes),
        detail::builtinProperty<12>("indexOf", B::StringIndexOf),
        detail::builtinProperty<12>("startsWith", B::StringStartsWith),
        detail::builtinProperty<12>("endsWith", B::StringEndsWith),
        detail::builtinProperty<12>("trim", B::StringTrim),
        detail::builtinProperty<12>("toUpperCase", B::StringUpper),
        detail::builtinProperty<12>("toLowerCase", B::StringLower),
        detail::builtinProperty<12>("split", B::StringSplit),
        detail::builtinProperty<12>("at", B::StringAt),
        detail::builtinProperty<12>("replace", B::StringReplace),
        detail::builtinProperty<12>("replaceAll", B::StringReplaceAll),
        detail::builtinProperty<12>("repeat", B::StringRepeat),
        detail::builtinProperty<12>("padStart", B::StringPadStart),
        detail::builtinProperty<12>("padEnd", B::StringPadEnd),
        detail::builtinProperty<12>("trimStart", B::StringTrimStart),
        detail::builtinProperty<12>("trimEnd", B::StringTrimEnd),
        detail::builtinProperty<12>("lastIndexOf", B::StringLastIndexOf),
        detail::builtinProperty<12>("concat", B::StringConcat),
        detail::builtinProperty<12>("charCodeAt", B::StringCharCodeAt),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::Tag::NumberNaN: {
    static constexpr detail::BuiltinProperty<9> properties[] = {
        detail::builtinProperty<9>("toString", B::NumberToString),
        detail::builtinProperty<9>("toFixed", B::NumberToFixed),
        detail::builtinProperty<9>("valueOf", B::NumberValueOf),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::Tag::Promise: {
    static constexpr detail::BuiltinProperty<8> properties[] = {
        detail::builtinProperty<8>("then", B::PromiseThen),
        detail::builtinProperty<8>("catch", B::PromiseCatch),
        detail::builtinProperty<8>("finally", B::PromiseFinally),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  case detail::Tag::Date: {
    static constexpr detail::BuiltinProperty<19> properties[] = {
        detail::builtinProperty<19>("getTime", B::DateGetTime),
        detail::builtinProperty<19>("valueOf", B::DateGetTime),
        detail::builtinProperty<19>("toISOString", B::DateInstanceToIsoString),
        detail::builtinProperty<19>("toJSON", B::DateInstanceToIsoString),
        detail::builtinProperty<19>("getFullYear", B::DateGetFullYear),
        detail::builtinProperty<19>("getUTCFullYear", B::DateGetFullYear),
        detail::builtinProperty<19>("getMonth", B::DateGetMonth),
        detail::builtinProperty<19>("getUTCMonth", B::DateGetMonth),
        detail::builtinProperty<19>("getDate", B::DateGetDate),
        detail::builtinProperty<19>("getUTCDate", B::DateGetDate),
        detail::builtinProperty<19>("getDay", B::DateGetDay),
        detail::builtinProperty<19>("getUTCDay", B::DateGetDay),
        detail::builtinProperty<19>("getHours", B::DateGetHours),
        detail::builtinProperty<19>("getUTCHours", B::DateGetHours),
        detail::builtinProperty<19>("getMinutes", B::DateGetMinutes),
        detail::builtinProperty<19>("getUTCMinutes", B::DateGetMinutes),
        detail::builtinProperty<19>("getSeconds", B::DateGetSeconds),
        detail::builtinProperty<19>("getUTCSeconds", B::DateGetSeconds),
        detail::builtinProperty<19>("getMilliseconds", B::DateGetMilliseconds),
        detail::builtinProperty<19>("getUTCMilliseconds",
                                    B::DateGetMilliseconds),
    };
    return detail::findBuiltinProperty(properties, key, keyLength);
  }
  default:
    break;
  }
  return undefined();
}

inline bool Runtime::setProperty(Value receiver, const char *key,
                                 size_t keyLength, Value value) {
  if (detail::tagOf(receiver) == detail::Tag::Object) {
    return objectSet(receiver, key, keyLength, value);
  }
  setError(ErrorCode{20}, nullptr, 0);
  return false;
}

inline bool Runtime::getIndex(Value receiver, Value key, Value *result,
                              NameRef *name, int64_t *index, bool *numeric) {
  if (result == nullptr || name == nullptr || index == nullptr ||
      numeric == nullptr) {
    return false;
  }
  *numeric = false;
  *index = 0;
  *name = NameRef{};
  if (isNumber(key)) {
    const double raw = toNumber(key);
    const double maximumIndex =
        sizeof(size_t) < sizeof(int64_t) ? static_cast<double>(SIZE_MAX)
                                         : static_cast<double>(INT64_MAX);
    if (std::isfinite(raw) && raw >= 0 && std::floor(raw) == raw &&
        raw <= maximumIndex) {
      *numeric = true;
      *index = static_cast<int64_t>(raw);
    }
  }
  if (*numeric && (detail::tagOf(receiver) == detail::Tag::Array ||
                   detail::tagOf(receiver) == detail::Tag::Set ||
                   detail::tagOf(receiver) == detail::Tag::String)) {
    if (detail::tagOf(receiver) == detail::Tag::String) {
      const StringView text = stringView(receiver);
      *result = static_cast<size_t>(*index) < text.size
                    ? makeString(text.data + *index, 1)
                    : undefined();
    } else {
      *result = arrayAt(receiver, static_cast<size_t>(*index));
    }
    return true;
  }
  Value keyText = toStringValue(key);
  const StringView text = stringView(keyText);
  if (text.size > UINT16_MAX) {
    setError(ErrorCode{21}, nullptr, 0);
    return false;
  }
  *name = NameRef{text.data, static_cast<uint16_t>(text.size),
                  static_cast<uint32_t>(detail::payloadOf(keyText))};
  *result = getProperty(receiver, text.data, text.size);
  return error_.message[0] == '\0';
}

inline bool Runtime::setIndex(Value receiver, NameRef name, int64_t index,
                              bool numeric, Value value) {
  if (numeric && detail::tagOf(receiver) == detail::Tag::Array) {
    return index >= 0 && arraySet(receiver, static_cast<size_t>(index), value);
  }
  if (numeric && detail::tagOf(receiver) == detail::Tag::Set) {
    setError(ErrorCode{22}, nullptr, 0);
    return false;
  }
  if (name.data == nullptr) {
    setError(ErrorCode{23}, nullptr, 0);
    return false;
  }
  return setProperty(receiver, name.data, name.length, value);
}

inline Value Runtime::makeFunction(
    uint32_t source, uint32_t bodyStart, uint32_t bodyEnd,
    const detail::ParameterSpan *parameters, size_t parameterCount,
    bool expressionBody, bool asyncBody, detail::ParameterSpan name) {
  if (parameterCount > UINT16_MAX || bodyEnd < bodyStart ||
      !validOffset(source, detail::BlockKind::Source)) {
    return fail(ErrorCode{24});
  }
  const uint32_t offset = allocateBlock(
      detail::BlockKind::Function,
      sizeof(detail::FunctionPayload) +
          parameterCount * sizeof(detail::ParameterSpan));
  if (offset == 0) {
    return undefined();
  }
  auto *function =
      static_cast<detail::FunctionPayload *>(payload(offset));
  function->source = source;
  function->bodyStart = bodyStart;
  function->bodyEnd = bodyEnd;
  function->parameterCount = static_cast<uint16_t>(parameterCount);
  function->expressionBody = expressionBody ? 1U : 0U;
  function->asyncBody = asyncBody ? 1U : 0U;
  function->name = name;
  if (parameterCount > 0) {
    std::memcpy(reinterpret_cast<uint8_t *>(function) + sizeof(*function),
                parameters,
                parameterCount * sizeof(detail::ParameterSpan));
  }
  return detail::boxed(detail::Tag::Function, offset);
}

inline bool Runtime::strictEqual(Value left, Value right) const {
  const ValueType leftType = typeOf(left);
  const ValueType rightType = typeOf(right);
  if (leftType != rightType) {
    return false;
  }
  if (leftType == ValueType::Number) {
    const double a = detail::numberOf(left);
    const double b = detail::numberOf(right);
    return !std::isnan(a) && !std::isnan(b) && a == b;
  }
  if (leftType == ValueType::String) {
    const StringView a = stringView(left);
    const StringView b = stringView(right);
    return a.size == b.size &&
           (a.size == 0 || std::memcmp(a.data, b.data, a.size) == 0);
  }
  return left.bits == right.bits;
}

inline int Runtime::compare(Value left, Value right) const {
  if (isString(left) && isString(right)) {
    const StringView a = stringView(left);
    const StringView b = stringView(right);
    const size_t common = a.size < b.size ? a.size : b.size;
    const int compared = common > 0 ? std::memcmp(a.data, b.data, common) : 0;
    if (compared != 0) {
      return compared < 0 ? -1 : 1;
    }
    return a.size == b.size ? 0 : (a.size < b.size ? -1 : 1);
  }
  const double a = toNumber(left);
  const double b = toNumber(right);
  if (std::isnan(a) || std::isnan(b)) {
    return 0;
  }
  return a == b ? 0 : (a < b ? -1 : 1);
}

inline Value Runtime::concatenate(Value left, Value right) {
  Value a = toStringValue(left);
  Value b = toStringValue(right);
  if (error_.message[0] != '\0') {
    return undefined();
  }
  const StringView leftText = stringView(a);
  const StringView rightText = stringView(b);
  char *joined = nullptr;
  Value result = makeStringBuffer(leftText.size + rightText.size, &joined);
  if (joined != nullptr) {
    std::memcpy(joined, leftText.data, leftText.size);
    std::memcpy(joined + leftText.size, rightText.data, rightText.size);
  }
  return result;
}

inline size_t Runtime::valueReferenceCount(Value target, size_t limit) const {
  size_t count = 0;
  auto found = [&count, limit, target](Value value) {
    if (value.bits == target.bits && count < limit)
      count++;
  };
  for (size_t i = 0; i < bindingCount_ && count < limit; i++)
    found(bindings_[i].value);
  for (size_t i = 0; i < temporaryRootCount_ && count < limit; i++)
    found(temporaryRoots_[i]);
  for (size_t i = 0; i < options_.maxHostRoots && count < limit; i++) {
    if (hostRootUsed_[i] != 0)
      found(hostRoots_[i]);
  }
  found(lastResult_);

  for (uint32_t offset = 8; offset < heapUsed_ && count < limit;
       offset += header(offset)->size) {
    const detail::BlockHeader *block = header(offset);
    switch (block->kind) {
    case detail::BlockKind::Property: {
      const detail::PropertyPayload *property = propertyPayload(offset);
      found(property->key);
      found(property->value);
      break;
    }
    case detail::BlockKind::Values: {
      const detail::ValuesPayload *values = valuesPayload(offset);
      const Value *items = valuesData(values);
      for (size_t i = 0; i < values->length && count < limit; i++)
        found(items[i]);
      break;
    }
    case detail::BlockKind::Promise: {
      const auto *promise = static_cast<const detail::PromisePayload *>(
          payload(offset));
      found(promise->result);
      found(promise->onFulfilled);
      found(promise->onRejected);
      found(promise->child);
      break;
    }
    default:
      break;
    }
  }
  return count;
}

inline bool Runtime::appendStringAssignment(Value left, Value right,
                                            Value *result) {
  if (result == nullptr || !isString(left) ||
      valueReferenceCount(left, 2) != 1) {
    return false;
  }
  Value suffix = toStringValue(right);
  if (error_.message[0] != '\0') {
    *result = undefined();
    return true;
  }
  const StringView leftText = stringView(left);
  const StringView rightText = stringView(suffix);
  if (rightText.size > UINT32_MAX - leftText.size) {
    *result = fail(ErrorCode{6});
    return true;
  }
  const size_t length = leftText.size + rightText.size;
  const uint32_t leftOffset =
      static_cast<uint32_t>(detail::payloadOf(left));
  detail::BlockHeader *leftBlock = header(leftOffset);
  const size_t requiredSize = detail::align8(
      sizeof(detail::BlockHeader) + sizeof(uint32_t) + length + 1U);

  if (requiredSize > leftBlock->size &&
      leftOffset + leftBlock->size == heapUsed_) {
    size_t grownSize = leftBlock->size;
    while (grownSize < requiredSize && grownSize <= UINT32_MAX / 2U)
      grownSize *= 2U;
    if (grownSize < requiredSize || grownSize > heapCapacity_ - leftOffset)
      grownSize = requiredSize;
    if (grownSize <= heapCapacity_ - leftOffset) {
      std::memset(heap_ + heapUsed_, 0,
                  grownSize - static_cast<size_t>(leftBlock->size));
      leftBlock->size = static_cast<uint32_t>(grownSize);
      heapUsed_ = leftOffset + leftBlock->size;
      if (heapUsed_ > peakHeapUsed_)
        peakHeapUsed_ = heapUsed_;
    }
  }

  if (requiredSize <= leftBlock->size) {
    char *characters = reinterpret_cast<char *>(
        static_cast<uint8_t *>(payload(leftOffset)) + sizeof(uint32_t));
    std::memmove(characters + leftText.size, rightText.data, rightText.size);
    const uint32_t storedLength = static_cast<uint32_t>(length);
    std::memcpy(payload(leftOffset), &storedLength, sizeof(storedLength));
    characters[length] = '\0';
    *result = left;
    return true;
  }

  size_t capacity = leftBlock->size - sizeof(detail::BlockHeader) -
                    sizeof(uint32_t) - 1U;
  if (capacity == 0)
    capacity = 8;
  while (capacity < length && capacity <= UINT32_MAX / 2U)
    capacity *= 2U;
  if (capacity < length)
    capacity = length;
  const uint32_t offset = allocateBlock(
      detail::BlockKind::String, sizeof(uint32_t) + capacity + 1U);
  if (offset == 0) {
    *result = undefined();
    return true;
  }
  uint8_t *blockData = static_cast<uint8_t *>(payload(offset));
  const uint32_t storedLength = static_cast<uint32_t>(length);
  std::memcpy(blockData, &storedLength, sizeof(storedLength));
  char *characters =
      reinterpret_cast<char *>(blockData + sizeof(uint32_t));
  std::memcpy(characters, leftText.data, leftText.size);
  std::memcpy(characters + leftText.size, rightText.data, rightText.size);
  characters[length] = '\0';
  *result = detail::boxed(detail::Tag::String, offset);
  return true;
}

inline bool Runtime::pushRoot(Value value) {
  if (temporaryRootCount_ >= options_.maxTemporaryRoots) {
    setError(ErrorCode{25}, nullptr, 0);
    return false;
  }
  temporaryRoots_[temporaryRootCount_++] = value;
  return true;
}

inline void Runtime::popRoots(uint16_t count) {
  temporaryRootCount_ = count <= temporaryRootCount_
                            ? static_cast<uint16_t>(temporaryRootCount_ - count)
                            : 0;
}

inline RootHandle Runtime::retain(Value value) {
  for (uint16_t i = 0; i < options_.maxHostRoots; i++) {
    if (hostRootUsed_[i] == 0) {
      hostRootUsed_[i] = 1;
      hostRoots_[i] = value;
      return i;
    }
  }
  setError(ErrorCode{26}, nullptr, 0);
  return kInvalidRoot;
}

inline void Runtime::release(RootHandle handle) {
  if (handle < options_.maxHostRoots) {
    hostRootUsed_[handle] = 0;
    hostRoots_[handle] = undefined();
  }
}

inline Value Runtime::retained(RootHandle handle) const {
  return handle < options_.maxHostRoots && hostRootUsed_[handle] != 0
             ? hostRoots_[handle]
             : undefined();
}

inline PromiseState Runtime::promiseState(Value promise) const {
  const detail::PromisePayload *data = promisePayload(promise);
  for (uint8_t depth = 0; data != nullptr && data->state == 3 && depth < 16;
       depth++) {
    data = promisePayload(data->result);
  }
  if (data == nullptr || data->state == 0 || data->state == 3) {
    return PromiseState::Pending;
  }
  return data->state == 1 ? PromiseState::Fulfilled : PromiseState::Rejected;
}

inline Value Runtime::promiseResult(Value promise) const {
  const detail::PromisePayload *data = promisePayload(promise);
  for (uint8_t depth = 0; data != nullptr && data->state == 3 && depth < 16;
       depth++) {
    data = promisePayload(data->result);
  }
  return data != nullptr && (data->state == 1 || data->state == 2)
             ? data->result
             : undefined();
}

inline bool Runtime::adoptPromise(Value promise, Value result, bool rejected) {
  detail::PromisePayload *data = promisePayload(promise);
  if (data == nullptr || data->state != 0) {
    return false;
  }
  if (!rejected && detail::tagOf(result) == detail::Tag::Promise) {
    if (result.bits == promise.bits) {
      data->state = 2;
      data->result = makeString("promise cannot resolve itself");
    } else {
      data->state = 3;
      data->result = result;
    }
  } else {
    data->state = rejected ? 2U : 1U;
    data->result = result;
  }
  if (!isUndefined(data->child)) {
    data->queued = 1;
  }
  return error_.message[0] == '\0';
}

inline bool Runtime::settlePromise(Value promise, Value result,
                                   bool rejected) {
  if (running_ && detail::tagOf(promise) != detail::Tag::Promise) {
    return false;
  }
  return adoptPromise(promise, result, rejected);
}

inline Value Runtime::promiseThen(Value promise, Value onFulfilled,
                                  Value onRejected) {
  detail::PromisePayload *data = promisePayload(promise);
  if (data == nullptr) {
    return fail(ErrorCode{27});
  }
  if (!isUndefined(data->child)) {
    return fail(ErrorCode{28});
  }
  if (!isUndefined(onFulfilled) && !isCallable(onFulfilled)) {
    return fail(ErrorCode{29});
  }
  if (!isUndefined(onRejected) && !isCallable(onRejected)) {
    return fail(ErrorCode{30});
  }
  Value child = makePromise();
  if (isUndefined(child)) {
    return child;
  }
  data->onFulfilled = onFulfilled;
  data->onRejected = onRejected;
  data->child = child;
  if (data->state != 0) {
    data->queued = 1;
  }
  return child;
}

inline Value Runtime::awaitValue(Value value) {
  if (detail::tagOf(value) != detail::Tag::Promise) {
    return value;
  }
  for (uint8_t depth = 0; depth < 16; depth++) {
    detail::PromisePayload *data = promisePayload(value);
    if (data == nullptr) {
      return fail(ErrorCode{31});
    }
    if (data->state == 3) {
      value = data->result;
      continue;
    }
    if (data->state == 0 && options_.awaitPending != nullptr) {
      if (!options_.awaitPending(*this, value, options_.awaitUserData)) {
        return fail(ErrorCode{32});
      }
      continue;
    }
    if (data->state == 0) {
      return fail(ErrorCode{33});
    }
    if (data->state == 2) {
      if (isString(data->result)) {
        return fail(stringView(data->result).data);
      }
      return fail(ErrorCode{34});
    }
    return data->result;
  }
  return fail(ErrorCode{35});
}

inline Value Runtime::parseJsonPromise(Value text) {
  Value promise = makePromise();
  if (isUndefined(promise))
    return promise;
  if (!isString(text)) {
    adoptPromise(promise, makeString("JSON input must be a string"), true);
    return promise;
  }
  Value result = undefined();
  if (options_.jsonParse != nullptr) {
    result = options_.jsonParse(*this, stringView(text), options_.jsonUserData);
#if !defined(PINKJS_DISABLE_BUILTIN_JSON_PARSER)
  } else {
    result = jsonParse(stringView(text));
#else
  } else {
    setError(ErrorCode{36}, nullptr, 0);
#endif
  }
  if (error_.message[0] != '\0') {
    char message[sizeof(error_.message)];
    std::snprintf(message, sizeof(message), "%s", error_.message);
    clearError();
    adoptPromise(promise, makeString(message), true);
  } else {
    adoptPromise(promise, result, false);
  }
  return promise;
}

inline RunResult Runtime::drainJobs(size_t maximum) {
  clearError();
  if (!ready_ || running_) {
    setError(ErrorCode{37}, nullptr, 0);
    return {false, undefined()};
  }
  collectGarbage();
  stepsRemaining_ = options_.maxSteps;
  running_ = true;
  size_t completed = 0;
  for (uint32_t offset = 8; offset < heapUsed_ && completed < maximum;) {
    detail::BlockHeader *block = header(offset);
    const uint32_t next = offset + block->size;
    if (block->kind == detail::BlockKind::Promise) {
      Value promise = detail::boxed(detail::Tag::Promise, offset);
      detail::PromisePayload *data = promisePayload(promise);
      if (data != nullptr && data->state == 3) {
        const PromiseState adoptedState = promiseState(data->result);
        if (adoptedState != PromiseState::Pending) {
          data->state = adoptedState == PromiseState::Fulfilled ? 1U : 2U;
          data->result = promiseResult(data->result);
          if (!isUndefined(data->child)) {
            data->queued = 1;
          }
        }
      }
      if (data != nullptr && data->queued != 0 && data->state != 0 &&
          data->state != 3 && !isUndefined(data->child)) {
        const bool rejected = data->state == 2;
        const Value input = data->result;
        const Value handler = rejected ? data->onRejected : data->onFulfilled;
        const Value child = data->child;
        const bool finallyHandler = data->reserved != 0;
        data->queued = 0;
        data->reserved = 0;
        data->onFulfilled = undefined();
        data->onRejected = undefined();
        data->child = undefined();
        if (isCallable(handler)) {
          Value output = finallyHandler
                             ? callValue(handler, undefined(), nullptr, 0)
                             : callValue(handler, undefined(), &input, 1);
          if (error_.message[0] != '\0') {
            char message[sizeof(error_.message)];
            std::snprintf(message, sizeof(message), "%s", error_.message);
            clearError();
            adoptPromise(child, makeString(message), true);
          } else {
            adoptPromise(child, finallyHandler ? input : output,
                         finallyHandler ? rejected : false);
          }
        } else {
          adoptPromise(child, input, rejected);
        }
        completed++;
      }
    }
    offset = next;
  }
  running_ = false;
  collectGarbage();
  return {error_.message[0] == '\0', number(static_cast<double>(completed))};
}

inline void Runtime::markOffset(uint32_t offset, uint32_t &workCount,
                                bool &overflow) {
  if (offset < 8 || offset >= heapUsed_) {
    return;
  }
  detail::BlockHeader *block = header(offset);
  if (block->kind == detail::BlockKind::Free || block->marked != 0) {
    return;
  }
  block->marked = 1;
  if (workCount >= options_.maxGcWork) {
    overflow = true;
    return;
  }
  gcWork_[workCount++] = offset;
}

inline void Runtime::markValue(Value value, uint32_t &workCount,
                               bool &overflow) {
  switch (detail::tagOf(value)) {
  case detail::Tag::String:
  case detail::Tag::Object:
  case detail::Tag::Array:
  case detail::Tag::Set:
  case detail::Tag::Function:
  case detail::Tag::Promise:
  case detail::Tag::Date:
  case detail::Tag::Source:
    markOffset(static_cast<uint32_t>(detail::payloadOf(value)), workCount,
               overflow);
    break;
  case detail::Tag::Resolver:
    markOffset(static_cast<uint32_t>(detail::payloadOf(value) >> 1U),
               workCount, overflow);
    break;
  default:
    break;
  }
}

inline void Runtime::collectGarbage() {
  if (!ready_ || running_) {
    return;
  }
  uint32_t workCount = 0;
  bool overflow = false;
  for (size_t i = 0; i < bindingCount_; i++) {
    if (bindings_[i].nameOwner != 0) {
      markOffset(bindings_[i].nameOwner, workCount, overflow);
    }
    markValue(bindings_[i].value, workCount, overflow);
  }
  for (size_t i = 0; i < temporaryRootCount_; i++) {
    markValue(temporaryRoots_[i], workCount, overflow);
  }
  for (size_t i = 0; i < options_.maxHostRoots; i++) {
    if (hostRootUsed_[i] != 0) {
      markValue(hostRoots_[i], workCount, overflow);
    }
  }
  // Pending host promises and queued reactions stay alive even if script code
  // does not retain the value while a core-0 operation is in flight.
  for (uint32_t offset = 8; offset < heapUsed_;
       offset += header(offset)->size) {
    if (header(offset)->kind == detail::BlockKind::Promise) {
      const auto *promise = static_cast<const detail::PromisePayload *>(
          payload(offset));
      if (promise->state == 0 || promise->state == 3 || promise->queued != 0) {
        markOffset(offset, workCount, overflow);
      }
    }
  }
  markValue(lastResult_, workCount, overflow);

  while (workCount > 0 && !overflow) {
    const uint32_t offset = gcWork_[--workCount];
    detail::BlockHeader *block = header(offset);
    switch (block->kind) {
    case detail::BlockKind::Object: {
      const auto *object = static_cast<const detail::ObjectPayload *>(
          payload(offset));
      markOffset(object->firstProperty, workCount, overflow);
      break;
    }
    case detail::BlockKind::Property: {
      const detail::PropertyPayload *property = propertyPayload(offset);
      markOffset(property->next, workCount, overflow);
      markValue(property->key, workCount, overflow);
      markValue(property->value, workCount, overflow);
      break;
    }
    case detail::BlockKind::Array:
    case detail::BlockKind::Set: {
      const auto *owner = static_cast<const detail::CollectionPayload *>(
          payload(offset));
      markOffset(owner->values, workCount, overflow);
      break;
    }
    case detail::BlockKind::Values: {
      const detail::ValuesPayload *values = valuesPayload(offset);
      const Value *items = valuesData(values);
      for (size_t i = 0; i < values->length; i++) {
        markValue(items[i], workCount, overflow);
      }
      break;
    }
    case detail::BlockKind::Function: {
      const auto *function = static_cast<const detail::FunctionPayload *>(
          payload(offset));
      markOffset(function->source, workCount, overflow);
      break;
    }
    case detail::BlockKind::Promise: {
      const auto *promise = static_cast<const detail::PromisePayload *>(
          payload(offset));
      markValue(promise->result, workCount, overflow);
      markValue(promise->onFulfilled, workCount, overflow);
      markValue(promise->onRejected, workCount, overflow);
      markValue(promise->child, workCount, overflow);
      break;
    }
    default:
      break;
    }
  }

  if (overflow) {
    // A bounded mark stack must fail safe. Retain this cycle and try again after
    // the graph becomes smaller rather than freeing a reachable PSRAM block.
    for (uint32_t offset = 8; offset < heapUsed_;
         offset += header(offset)->size) {
      header(offset)->marked = 0;
    }
    return;
  }

  for (uint32_t offset = 8; offset < heapUsed_;
       offset += header(offset)->size) {
    detail::BlockHeader *block = header(offset);
    if (block->kind != detail::BlockKind::Free &&
        block->kind != detail::BlockKind::ExternalScratch &&
        block->marked == 0) {
      block->kind = detail::BlockKind::Free;
    }
    block->marked = 0;
  }
  coalesceFreeBlocks();
}

inline MemoryStats Runtime::memoryStats() const {
  MemoryStats stats;
  stats.workspaceBytes = workspaceBytes_;
  stats.metadataBytes =
      heap_ != nullptr ? static_cast<size_t>(heap_ - workspace_) : 0;
  stats.heapBytes = heapCapacity_;
  stats.heapUsedBytes = heapUsed_ >= 8 ? heapUsed_ - 8U : 0;
  stats.peakHeapUsedBytes = peakHeapUsed_ >= 8 ? peakHeapUsed_ - 8U : 0;
  stats.bindingCount = bindingCount_;
  size_t freeBytes = heapCapacity_ > heapUsed_ ? heapCapacity_ - heapUsed_ : 0;
  for (uint32_t offset = 8; offset < heapUsed_;
       offset += header(offset)->size) {
    if (header(offset)->kind == detail::BlockKind::Free) {
      freeBytes += header(offset)->size;
    }
  }
  stats.heapFreeBytes = freeBytes;
  return stats;
}

namespace detail {

enum class TokenType : uint8_t {
  End,
  Error,
  Identifier,
  Number,
  String,
  Template,
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  LeftBracket,
  RightBracket,
  Comma,
  Dot,
  Colon,
  Semicolon,
  Question,
  OptionalChain,
  Arrow,
  Let,
  Const,
  Function,
  If,
  Else,
  While,
  Do,
  For,
  Of,
  In,
  Return,
  Break,
  Continue,
  True,
  False,
  Null,
  Undefined,
  Typeof,
  New,
  Async,
  Await,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Power,
  PlusPlus,
  MinusMinus,
  Bang,
  Tilde,
  Assign,
  PlusAssign,
  MinusAssign,
  StarAssign,
  SlashAssign,
  PercentAssign,
  NullishAssign,
  Equal,
  NotEqual,
  StrictEqual,
  StrictNotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  And,
  Or,
  Xor,
  LogicalAnd,
  LogicalOr,
  Nullish,
  ShiftLeft,
  ShiftRight,
  ShiftRightUnsigned,
};

struct Token {
  TokenType type = TokenType::End;
  uint32_t start = 0;
  uint32_t length = 0;
  double number = 0;
  bool newlineBefore = false;
};

class Lexer {
public:
  Lexer(const char *source, uint32_t begin, uint32_t end)
      : source_(source), position_(begin), end_(end) {}

  Token next() {
    Token token;
    skipTrivia(token.newlineBefore);
    token.start = position_;
    if (position_ >= end_) {
      token.type = TokenType::End;
      return token;
    }
    const char c = source_[position_++];
    if (isIdentifierStart(c)) {
      while (position_ < end_ && isIdentifierPart(source_[position_])) {
        position_++;
      }
      token.length = position_ - token.start;
      token.type = keyword(source_ + token.start, token.length);
      return token;
    }
    if ((c >= '0' && c <= '9') ||
        (c == '.' && position_ < end_ && source_[position_] >= '0' &&
         source_[position_] <= '9')) {
      if (c == '0' && position_ < end_ &&
          (source_[position_] == 'x' || source_[position_] == 'X')) {
        position_++;
        uint64_t value = 0;
        while (position_ < end_ && isHex(source_[position_])) {
          value = value * 16U + hexValue(source_[position_++]);
        }
        token.number = static_cast<double>(value);
      } else if (c == '0' && position_ < end_ &&
                 (source_[position_] == 'b' || source_[position_] == 'B')) {
        position_++;
        uint64_t value = 0;
        while (position_ < end_ &&
               (source_[position_] == '0' || source_[position_] == '1')) {
          value = value * 2U + static_cast<uint64_t>(source_[position_++] - '0');
        }
        token.number = static_cast<double>(value);
      } else {
        while (position_ < end_ && source_[position_] >= '0' &&
               source_[position_] <= '9') {
          position_++;
        }
        if (position_ < end_ && source_[position_] == '.') {
          position_++;
          while (position_ < end_ && source_[position_] >= '0' &&
                 source_[position_] <= '9') {
            position_++;
          }
        }
        if (position_ < end_ &&
            (source_[position_] == 'e' || source_[position_] == 'E')) {
          const uint32_t exponent = position_++;
          if (position_ < end_ &&
              (source_[position_] == '+' || source_[position_] == '-')) {
            position_++;
          }
          const uint32_t digits = position_;
          while (position_ < end_ && source_[position_] >= '0' &&
                 source_[position_] <= '9') {
            position_++;
          }
          if (digits == position_) {
            position_ = exponent;
          }
        }
        token.number = std::strtod(source_ + token.start, nullptr);
      }
      token.type = TokenType::Number;
      token.length = position_ - token.start;
      return token;
    }
    if (c == '\'' || c == '"') {
      const char quote = c;
      bool escaped = false;
      while (position_ < end_) {
        const char current = source_[position_++];
        if (!escaped && current == quote) {
          token.type = TokenType::String;
          token.length = position_ - token.start;
          return token;
        }
        if (!escaped && (current == '\n' || current == '\r')) {
          break;
        }
        if (!escaped && current == '\\') {
          escaped = true;
        } else {
          escaped = false;
        }
      }
      token.type = TokenType::Error;
      token.length = position_ - token.start;
      return token;
    }
    if (c == '`') {
      bool escaped = false;
      while (position_ < end_) {
        const char current = source_[position_++];
        if (!escaped && current == '`') {
          token.type = TokenType::Template;
          token.length = position_ - token.start;
          return token;
        }
        if (!escaped && current == '\\') {
          escaped = true;
        } else {
          escaped = false;
        }
      }
      token.type = TokenType::Error;
      token.length = position_ - token.start;
      return token;
    }

    auto match = [this](char expected) {
      if (position_ < end_ && source_[position_] == expected) {
        position_++;
        return true;
      }
      return false;
    };
    switch (c) {
    case '(':
      token.type = TokenType::LeftParen;
      break;
    case ')':
      token.type = TokenType::RightParen;
      break;
    case '{':
      token.type = TokenType::LeftBrace;
      break;
    case '}':
      token.type = TokenType::RightBrace;
      break;
    case '[':
      token.type = TokenType::LeftBracket;
      break;
    case ']':
      token.type = TokenType::RightBracket;
      break;
    case ',':
      token.type = TokenType::Comma;
      break;
    case '.':
      token.type = TokenType::Dot;
      break;
    case ':':
      token.type = TokenType::Colon;
      break;
    case ';':
      token.type = TokenType::Semicolon;
      break;
    case '?':
      if (match('?')) {
        token.type = match('=') ? TokenType::NullishAssign
                                : TokenType::Nullish;
      } else {
        token.type = match('.') ? TokenType::OptionalChain
                                : TokenType::Question;
      }
      break;
    case '+':
      token.type = match('+') ? TokenType::PlusPlus
                              : (match('=') ? TokenType::PlusAssign
                                            : TokenType::Plus);
      break;
    case '-':
      token.type = match('-') ? TokenType::MinusMinus
                              : (match('=') ? TokenType::MinusAssign
                                            : TokenType::Minus);
      break;
    case '*':
      token.type = match('*') ? TokenType::Power
                              : (match('=') ? TokenType::StarAssign
                                            : TokenType::Star);
      break;
    case '/':
      token.type = match('=') ? TokenType::SlashAssign : TokenType::Slash;
      break;
    case '%':
      token.type = match('=') ? TokenType::PercentAssign : TokenType::Percent;
      break;
    case '!':
      if (match('=')) {
        token.type = match('=') ? TokenType::StrictNotEqual
                                : TokenType::NotEqual;
      } else {
        token.type = TokenType::Bang;
      }
      break;
    case '~':
      token.type = TokenType::Tilde;
      break;
    case '=':
      if (match('>')) {
        token.type = TokenType::Arrow;
      } else if (match('=')) {
        token.type = match('=') ? TokenType::StrictEqual : TokenType::Equal;
      } else {
        token.type = TokenType::Assign;
      }
      break;
    case '<':
      token.type = match('<') ? TokenType::ShiftLeft
                              : (match('=') ? TokenType::LessEqual
                                            : TokenType::Less);
      break;
    case '>':
      if (match('>')) {
        token.type = match('>') ? TokenType::ShiftRightUnsigned
                                : TokenType::ShiftRight;
      } else {
        token.type = match('=') ? TokenType::GreaterEqual : TokenType::Greater;
      }
      break;
    case '&':
      token.type = match('&') ? TokenType::LogicalAnd : TokenType::And;
      break;
    case '|':
      token.type = match('|') ? TokenType::LogicalOr : TokenType::Or;
      break;
    case '^':
      token.type = TokenType::Xor;
      break;
    default:
      token.type = TokenType::Error;
      break;
    }
    token.length = position_ - token.start;
    return token;
  }

private:
  const char *source_ = nullptr;
  uint32_t position_ = 0;
  uint32_t end_ = 0;

  static bool isIdentifierStart(char c) {
    return c == '_' || c == '$' || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
  }
  static bool isIdentifierPart(char c) {
    return isIdentifierStart(c) || (c >= '0' && c <= '9');
  }
  static bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  }
  static uint8_t hexValue(char c) {
    if (c >= '0' && c <= '9')
      return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f')
      return static_cast<uint8_t>(c - 'a' + 10);
    return static_cast<uint8_t>(c - 'A' + 10);
  }
  static bool same(const char *text, uint32_t length, const char *word) {
    return std::strlen(word) == length && std::memcmp(text, word, length) == 0;
  }
  static TokenType keyword(const char *text, uint32_t length) {
    if (same(text, length, "let"))
      return TokenType::Let;
    if (same(text, length, "const"))
      return TokenType::Const;
    if (same(text, length, "function"))
      return TokenType::Function;
    if (same(text, length, "if"))
      return TokenType::If;
    if (same(text, length, "else"))
      return TokenType::Else;
    if (same(text, length, "while"))
      return TokenType::While;
    if (same(text, length, "do"))
      return TokenType::Do;
    if (same(text, length, "for"))
      return TokenType::For;
    if (same(text, length, "of"))
      return TokenType::Of;
    if (same(text, length, "in"))
      return TokenType::In;
    if (same(text, length, "return"))
      return TokenType::Return;
    if (same(text, length, "break"))
      return TokenType::Break;
    if (same(text, length, "continue"))
      return TokenType::Continue;
    if (same(text, length, "true"))
      return TokenType::True;
    if (same(text, length, "false"))
      return TokenType::False;
    if (same(text, length, "null"))
      return TokenType::Null;
    if (same(text, length, "undefined"))
      return TokenType::Undefined;
    if (same(text, length, "typeof"))
      return TokenType::Typeof;
    if (same(text, length, "new"))
      return TokenType::New;
    if (same(text, length, "async"))
      return TokenType::Async;
    if (same(text, length, "await"))
      return TokenType::Await;
    return TokenType::Identifier;
  }

  void skipTrivia(bool &newline) {
    while (position_ < end_) {
      const char c = source_[position_];
      if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
        position_++;
      } else if (c == '\r' || c == '\n') {
        newline = true;
        if (c == '\r' && position_ + 1U < end_ &&
            source_[position_ + 1U] == '\n') {
          position_++;
        }
        position_++;
      } else if (c == '/' && position_ + 1U < end_ &&
                 source_[position_ + 1U] == '/') {
        position_ += 2U;
        while (position_ < end_ && source_[position_] != '\n' &&
               source_[position_] != '\r') {
          position_++;
        }
      } else if (c == '/' && position_ + 1U < end_ &&
                 source_[position_ + 1U] == '*') {
        position_ += 2U;
        while (position_ + 1U < end_ &&
               !(source_[position_] == '*' &&
                 source_[position_ + 1U] == '/')) {
          if (source_[position_] == '\n') {
            newline = true;
          }
          position_++;
        }
        if (position_ + 1U < end_) {
          position_ += 2U;
        }
      } else {
        break;
      }
    }
  }
};

} // namespace detail

class Parser {
public:
  enum class FlowKind : uint8_t { Normal, Return, Break, Continue };
  struct Flow {
    FlowKind kind = FlowKind::Normal;
    Value value = {};
  };

  Parser(Runtime &runtime, uint32_t sourceOffset, uint32_t begin,
         uint32_t end, bool functionBody = false, bool loopBody = false,
         bool asyncBody = false)
      : runtime_(runtime), sourceOffset_(sourceOffset),
        source_(runtime.sourceData(sourceOffset)), lexer_(source_, begin, end),
        end_(end), functionBody_(functionBody), loopBody_(loopBody),
        asyncBody_(asyncBody) {
    token_ = lexer_.next();
  }

  Flow program(bool execute = true);
  Flow statement(bool execute);
  Value expression(bool execute = true) { return assignment(execute).value; }

private:
  class NestingGuard {
  public:
    explicit NestingGuard(Parser &parser);
    ~NestingGuard();
    explicit operator bool() const { return entered_; }

  private:
    Parser &parser_;
    bool entered_ = false;
  };

  enum class ReferenceKind : uint8_t { None, Binding, Property };
  struct Reference {
    Value receiver = {};
    int64_t index = 0;
    Runtime::NameRef name = {};
    uint16_t binding = 0;
    ReferenceKind kind = ReferenceKind::None;
    bool numeric = false;
  };
  struct Expression {
    Value value = {};
    Reference reference = {};
  };
  struct Span {
    uint32_t begin = 0;
    uint32_t end = 0;
  };
  enum class BinaryPrecedence : uint8_t {
    None,
    LogicalOr,
    Nullish,
    LogicalAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseAnd,
    Equality,
    Relational,
    Shift,
    Additive,
    Multiplicative,
    Power,
  };

  Runtime &runtime_;
  uint32_t sourceOffset_ = 0;
  const char *source_ = nullptr;
  detail::Lexer lexer_;
  detail::Token token_ = {};
  uint32_t end_ = 0;
  bool functionBody_ = false;
  bool loopBody_ = false;
  bool asyncBody_ = false;

  bool advance();
  bool accept(detail::TokenType type);
  bool expect(detail::TokenType type, ErrorCode code);
  void syntax(ErrorCode code);
  detail::TokenType peekType() const;
  bool identifierToken(detail::TokenType type) const;
  bool startsStatement(detail::TokenType type) const;
  void terminator();
  Flow block(bool execute, bool createScope = true);
  Flow declaration(bool execute, bool constant, bool consumeEnd = true);
  Flow functionDeclaration(bool execute, bool asyncBody);
  Flow ifStatement(bool execute);
  Flow whileStatement(bool execute);
  Flow doWhileStatement(bool execute);
  Flow forStatement(bool execute);
  Flow returnStatement(bool execute);
  Span captureStatement();
  Flow executeStatementSpan(Span span, bool loopBody = false);
  Value executeExpressionSpan(Span span);
  bool iterableAt(Value iterable, size_t index, Value *value) const;
  size_t iterableLength(Value iterable) const;

  Expression assignment(bool execute);
  Expression conditional(bool execute);
  Expression binary(
      bool execute,
      BinaryPrecedence minimumPrecedence = BinaryPrecedence::LogicalOr);
  Expression unary(bool execute);
  Expression postfix(bool execute);
  Expression primary(bool execute);
  Expression arrowFunction(bool execute,
                           const detail::ParameterSpan *parameters,
                           size_t parameterCount);
  Expression traditionalFunction(bool execute);
  Expression traditionalFunction(bool execute, bool asyncBody,
                                   detail::ParameterSpan *parsedName);
  Expression arrayLiteral(bool execute);
  Expression objectLiteral(bool execute);
  Value stringLiteral(const detail::Token &token);
  Value templateLiteral(const detail::Token &token, bool execute);
  Value applyBinary(detail::TokenType operation, Value left, Value right,
                    bool execute);
  bool store(const Reference &reference, Value value);
  Value currentValue(const Expression &expression) const {
    return expression.value;
  }
};

inline bool Parser::advance() {
  if (!runtime_.step(source_, token_.start)) {
    return false;
  }
  token_ = lexer_.next();
  if (token_.type == detail::TokenType::Error) {
    syntax(ErrorCode{38});
    return false;
  }
  return true;
}

inline bool Parser::accept(detail::TokenType type) {
  if (token_.type != type) {
    return false;
  }
  advance();
  return true;
}

inline bool Parser::expect(detail::TokenType type, ErrorCode code) {
  if (!accept(type)) {
    syntax(code);
    return false;
  }
  return true;
}

inline void Parser::syntax(ErrorCode code) {
  runtime_.setError(code, source_, token_.start);
}

inline detail::TokenType Parser::peekType() const {
  detail::Lexer copy = lexer_;
  return copy.next().type;
}

inline bool Parser::identifierToken(detail::TokenType type) const {
  return type == detail::TokenType::Identifier || type == detail::TokenType::Of ||
         type == detail::TokenType::In;
}

inline bool Parser::startsStatement(detail::TokenType type) const {
  using T = detail::TokenType;
  return type == T::Let || type == T::Const || type == T::If ||
         type == T::While || type == T::Do || type == T::For ||
         type == T::Return || type == T::Break || type == T::Continue ||
         type == T::LeftBrace || type == T::Function || type == T::Async;
}

inline void Parser::terminator() {
  if (accept(detail::TokenType::Semicolon)) {
    return;
  }
  if (token_.type == detail::TokenType::End ||
      token_.type == detail::TokenType::RightBrace || token_.newlineBefore ||
      startsStatement(token_.type)) {
    return;
  }
  syntax(ErrorCode{39});
}

inline Parser::NestingGuard::NestingGuard(Parser &parser) : parser_(parser) {
  if (parser_.runtime_.parseDepth_ >= parser_.runtime_.options_.maxParseDepth) {
    parser_.syntax(ErrorCode{40});
    return;
  }
  parser_.runtime_.parseDepth_++;
  entered_ = true;
}

inline Parser::NestingGuard::~NestingGuard() {
  if (entered_ && parser_.runtime_.parseDepth_ > 0) {
    parser_.runtime_.parseDepth_--;
  }
}

inline Value Parser::stringLiteral(const detail::Token &token) {
  if (token.length < 2) {
    syntax(ErrorCode{41});
    return runtime_.undefined();
  }
  const size_t maximum = token.length - 2U;
  char *output = nullptr;
  Value value = runtime_.makeStringBuffer(maximum, &output);
  if (output == nullptr) {
    return runtime_.undefined();
  }
  size_t written = 0;
  const uint32_t end = token.start + token.length - 1U;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (uint32_t i = token.start + 1U; i < end; i++) {
    unsigned char c = static_cast<unsigned char>(source_[i]);
    if (c != '\\') {
      output[written++] = static_cast<char>(c);
      continue;
    }
    if (++i >= end) {
      syntax(ErrorCode{42});
      return runtime_.undefined();
    }
    c = static_cast<unsigned char>(source_[i]);
    switch (c) {
    case 'n':
      output[written++] = '\n';
      break;
    case 'r':
      output[written++] = '\r';
      break;
    case 't':
      output[written++] = '\t';
      break;
    case 'b':
      output[written++] = '\b';
      break;
    case 'f':
      output[written++] = '\f';
      break;
    case '\\':
    case '\'':
    case '"':
      output[written++] = static_cast<char>(c);
      break;
    case 'x': {
      if (i + 2U >= end || hex(source_[i + 1U]) < 0 ||
          hex(source_[i + 2U]) < 0) {
        syntax(ErrorCode{43});
        return runtime_.undefined();
      }
      output[written++] = static_cast<char>(hex(source_[i + 1U]) * 16 +
                                            hex(source_[i + 2U]));
      i += 2U;
      break;
    }
    case 'u': {
      if (i + 4U >= end) {
        syntax(ErrorCode{44});
        return runtime_.undefined();
      }
      uint32_t codepoint = 0;
      for (uint8_t n = 1; n <= 4; n++) {
        const int digit = hex(source_[i + n]);
        if (digit < 0) {
          syntax(ErrorCode{44});
          return runtime_.undefined();
        }
        codepoint = codepoint * 16U + static_cast<uint32_t>(digit);
      }
      i += 4U;
      if (codepoint <= 0x7fU) {
        output[written++] = static_cast<char>(codepoint);
      } else if (codepoint <= 0x7ffU) {
        output[written++] = static_cast<char>(0xc0U | (codepoint >> 6U));
        output[written++] = static_cast<char>(0x80U | (codepoint & 0x3fU));
      } else {
        output[written++] = static_cast<char>(0xe0U | (codepoint >> 12U));
        output[written++] =
            static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[written++] = static_cast<char>(0x80U | (codepoint & 0x3fU));
      }
      break;
    }
    default:
      output[written++] = static_cast<char>(c);
      break;
    }
  }
  runtime_.setTextLength(value, written);
  return value;
}

inline Value Parser::templateLiteral(const detail::Token &token, bool execute) {
  if (token.length < 2) {
    syntax(ErrorCode{45});
    return runtime_.undefined();
  }
  struct Part {
    uint32_t begin = 0;
    uint32_t end = 0;
    Value expression = {};
  } parts[16];
  size_t partCount = 0;
  const uint32_t contentEnd = token.start + token.length - 1U;
  uint32_t segmentBegin = token.start + 1U;
  uint32_t scan = segmentBegin;
  while (scan < contentEnd && runtime_.error_.message[0] == '\0') {
    if (source_[scan] == '\\') {
      scan += scan + 1U < contentEnd ? 2U : 1U;
      continue;
    }
    if (source_[scan] != '$' || scan + 1U >= contentEnd ||
        source_[scan + 1U] != '{') {
      scan++;
      continue;
    }
    if (partCount >= sizeof(parts) / sizeof(parts[0])) {
      syntax(ErrorCode{46});
      break;
    }
    const uint32_t expressionBegin = scan + 2U;
    uint32_t expressionEnd = expressionBegin;
    uint16_t braces = 1;
    char quote = '\0';
    bool escaped = false;
    for (; expressionEnd < contentEnd; expressionEnd++) {
      const char c = source_[expressionEnd];
      if (quote != '\0') {
        if (!escaped && c == quote)
          quote = '\0';
        if (!escaped && c == '\\')
          escaped = true;
        else
          escaped = false;
        continue;
      }
      if (c == '\'' || c == '"') {
        quote = c;
      } else if (c == '{') {
        braces++;
      } else if (c == '}' && --braces == 0) {
        break;
      }
    }
    if (expressionEnd >= contentEnd || braces != 0) {
      syntax(ErrorCode{47});
      break;
    }
    Parser nested(runtime_, sourceOffset_, expressionBegin, expressionEnd,
                  functionBody_, loopBody_, asyncBody_);
    Value expression = nested.expression(execute);
    if (nested.token_.type != detail::TokenType::End &&
        runtime_.error_.message[0] == '\0') {
      nested.syntax(ErrorCode{48});
    }
    parts[partCount++] = {segmentBegin, scan,
                          execute ? runtime_.toStringValue(expression)
                                  : runtime_.undefined()};
    scan = expressionEnd + 1U;
    segmentBegin = scan;
  }
  if (runtime_.error_.message[0] != '\0' || !execute)
    return runtime_.undefined();

  auto decodedSize = [this](uint32_t begin, uint32_t end) {
    size_t length = 0;
    for (uint32_t i = begin; i < end; i++, length++) {
      if (source_[i] == '\\' && i + 1U < end)
        i++;
    }
    return length;
  };
  size_t total = decodedSize(segmentBegin, contentEnd);
  for (size_t i = 0; i < partCount; i++) {
    total += decodedSize(parts[i].begin, parts[i].end);
    total += runtime_.stringView(parts[i].expression).size;
  }
  char *output = nullptr;
  Value result = runtime_.makeStringBuffer(total, &output);
  if (output == nullptr)
    return runtime_.undefined();
  size_t written = 0;
  auto decode = [this, output, &written](uint32_t begin, uint32_t end) {
    for (uint32_t i = begin; i < end; i++) {
      char c = source_[i];
      if (c == '\\' && i + 1U < end) {
        c = source_[++i];
        if (c == 'n')
          c = '\n';
        else if (c == 'r')
          c = '\r';
        else if (c == 't')
          c = '\t';
      }
      output[written++] = c;
    }
  };
  for (size_t i = 0; i < partCount; i++) {
    decode(parts[i].begin, parts[i].end);
    const StringView expression = runtime_.stringView(parts[i].expression);
    std::memcpy(output + written, expression.data, expression.size);
    written += expression.size;
  }
  decode(segmentBegin, contentEnd);
  return result;
}

inline Parser::Expression Parser::arrowFunction(
    bool execute, const detail::ParameterSpan *parameters,
    size_t parameterCount) {
  NestingGuard nesting(*this);
  if (!nesting) {
    return {runtime_.undefined()};
  }
  uint32_t bodyStart = token_.start;
  uint32_t bodyEnd = bodyStart;
  bool expressionBody = token_.type != detail::TokenType::LeftBrace;
  if (!expressionBody) {
    advance();
    bodyStart = token_.start;
    while (token_.type != detail::TokenType::RightBrace &&
           token_.type != detail::TokenType::End &&
           runtime_.error_.message[0] == '\0') {
      statement(false);
    }
    bodyEnd = token_.start;
    expect(detail::TokenType::RightBrace, ErrorCode{49});
  } else {
    bodyStart = token_.start;
    assignment(false);
    bodyEnd = token_.start;
  }
  Expression result;
  result.value = execute && runtime_.error_.message[0] == '\0'
                     ? runtime_.makeFunction(sourceOffset_, bodyStart, bodyEnd,
                                             parameters, parameterCount,
                                             expressionBody)
                     : runtime_.undefined();
  return result;
}

inline Parser::Expression Parser::traditionalFunction(bool execute) {
  return traditionalFunction(execute, false, nullptr);
}

inline Parser::Expression
Parser::traditionalFunction(bool execute, bool asyncBody,
                            detail::ParameterSpan *parsedName) {
  NestingGuard nesting(*this);
  if (!nesting) {
    return {runtime_.undefined()};
  }
  advance(); // function
  detail::ParameterSpan name = {};
  if (identifierToken(token_.type) &&
      peekType() == detail::TokenType::LeftParen) {
    name = {token_.start, static_cast<uint16_t>(token_.length), 0};
    advance();
  }
  if (parsedName != nullptr)
    *parsedName = name;
  if (!expect(detail::TokenType::LeftParen, ErrorCode{50})) {
    return {runtime_.undefined()};
  }
  detail::ParameterSpan parameters[16];
  size_t parameterCount = 0;
  if (token_.type != detail::TokenType::RightParen) {
    do {
      if (!identifierToken(token_.type)) {
        syntax(ErrorCode{51});
        break;
      }
      if (parameterCount >= sizeof(parameters) / sizeof(parameters[0])) {
        syntax(ErrorCode{52});
        break;
      }
      parameters[parameterCount++] = {token_.start,
                                      static_cast<uint16_t>(token_.length), 0};
      advance();
    } while (accept(detail::TokenType::Comma));
  }
  expect(detail::TokenType::RightParen, ErrorCode{53});
  if (token_.type != detail::TokenType::LeftBrace) {
    syntax(ErrorCode{54});
    return {runtime_.undefined()};
  }
  advance();
  const uint32_t bodyStart = token_.start;
  const bool outerAsyncBody = asyncBody_;
  asyncBody_ = asyncBody;
  while (token_.type != detail::TokenType::RightBrace &&
         token_.type != detail::TokenType::End &&
         runtime_.error_.message[0] == '\0') {
    statement(false);
  }
  const uint32_t bodyEnd = token_.start;
  expect(detail::TokenType::RightBrace, ErrorCode{49});
  asyncBody_ = outerAsyncBody;
  return {execute && runtime_.error_.message[0] == '\0'
              ? runtime_.makeFunction(sourceOffset_, bodyStart, bodyEnd,
                                      parameters, parameterCount, false,
                                      asyncBody, name)
              : runtime_.undefined()};
}

inline Parser::Expression Parser::arrayLiteral(bool execute) {
  NestingGuard nesting(*this);
  if (!nesting) {
    return {runtime_.undefined()};
  }
  advance(); // [
  Value array = execute ? runtime_.makeArray() : runtime_.undefined();
  while (token_.type != detail::TokenType::RightBracket &&
         token_.type != detail::TokenType::End &&
         runtime_.error_.message[0] == '\0') {
    if (accept(detail::TokenType::Comma)) {
      if (execute) {
        runtime_.arrayPush(array, runtime_.undefined());
      }
      continue;
    }
    Expression element = assignment(execute);
    if (execute) {
      runtime_.arrayPush(array, element.value);
    }
    if (!accept(detail::TokenType::Comma)) {
      break;
    }
  }
  expect(detail::TokenType::RightBracket, ErrorCode{55});
  return {array};
}

inline Parser::Expression Parser::objectLiteral(bool execute) {
  NestingGuard nesting(*this);
  if (!nesting) {
    return {runtime_.undefined()};
  }
  advance(); // {
  Value object = execute ? runtime_.makeObject() : runtime_.undefined();
  while (token_.type != detail::TokenType::RightBrace &&
         token_.type != detail::TokenType::End &&
         runtime_.error_.message[0] == '\0') {
    const detail::Token keyToken = token_;
    const char *key = nullptr;
    size_t keyLength = 0;
    Value allocatedKey = runtime_.undefined();
    bool shorthand = false;
    if (identifierToken(token_.type)) {
      key = source_ + token_.start;
      keyLength = token_.length;
      advance();
      shorthand = token_.type != detail::TokenType::Colon;
    } else if (token_.type == detail::TokenType::String) {
      allocatedKey = execute ? stringLiteral(token_) : runtime_.undefined();
      if (execute) {
        const StringView view = runtime_.stringView(allocatedKey);
        key = view.data;
        keyLength = view.size;
      }
      advance();
    } else if (token_.type == detail::TokenType::Number) {
      char numericKey[32];
      const int count =
          std::snprintf(numericKey, sizeof(numericKey), "%.15g", token_.number);
      allocatedKey = execute
                         ? runtime_.makeString(numericKey,
                                               count > 0 ? size_t(count) : 0)
                         : runtime_.undefined();
      if (execute) {
        const StringView view = runtime_.stringView(allocatedKey);
        key = view.data;
        keyLength = view.size;
      }
      advance();
    } else {
      syntax(ErrorCode{56});
      break;
    }

    Value propertyValue = runtime_.undefined();
    if (shorthand) {
      Runtime::NameRef name{source_ + keyToken.start,
                            static_cast<uint16_t>(keyToken.length),
                            sourceOffset_};
      const int binding = runtime_.findBinding(name);
      if (execute && binding < 0) {
        syntax(ErrorCode{57});
      } else if (execute) {
        propertyValue = runtime_.bindings_[binding].value;
      }
    } else {
      expect(detail::TokenType::Colon, ErrorCode{58});
      propertyValue = assignment(execute).value;
    }
    if (execute && runtime_.error_.message[0] == '\0') {
      runtime_.objectSet(object, key, keyLength, propertyValue);
    }
    if (!accept(detail::TokenType::Comma)) {
      break;
    }
  }
  expect(detail::TokenType::RightBrace, ErrorCode{49});
  return {object};
}

inline Parser::Expression Parser::primary(bool execute) {
  using T = detail::TokenType;
  Expression result;
  result.value = runtime_.undefined();
  if (token_.type == T::Number) {
    result.value = execute ? runtime_.number(token_.number) : runtime_.undefined();
    advance();
    return result;
  }
  if (token_.type == T::String) {
    const detail::Token stringToken = token_;
    result.value = execute ? stringLiteral(stringToken) : runtime_.undefined();
    advance();
    return result;
  }
  if (token_.type == T::Template) {
    const detail::Token templateToken = token_;
    result.value = templateLiteral(templateToken, execute);
    advance();
    return result;
  }
  if (token_.type == T::True || token_.type == T::False) {
    result.value = execute ? runtime_.boolean(token_.type == T::True)
                           : runtime_.undefined();
    advance();
    return result;
  }
  if (token_.type == T::Null) {
    result.value = execute ? runtime_.null() : runtime_.undefined();
    advance();
    return result;
  }
  if (token_.type == T::Undefined) {
    advance();
    return result;
  }
  if (token_.type == T::LeftBracket) {
    return arrayLiteral(execute);
  }
  if (token_.type == T::LeftBrace) {
    return objectLiteral(execute);
  }
  if (token_.type == T::Function) {
    return traditionalFunction(execute);
  }
  if (token_.type == T::Async) {
    advance();
    if (token_.type != T::Function) {
      syntax(ErrorCode{59});
      return result;
    }
    return traditionalFunction(execute, true, nullptr);
  }
  if (identifierToken(token_.type)) {
    const detail::Token nameToken = token_;
    if (peekType() == T::Arrow) {
      advance();
      expect(T::Arrow, ErrorCode{60});
      const detail::ParameterSpan parameter{
          nameToken.start, static_cast<uint16_t>(nameToken.length), 0};
      return arrowFunction(execute, &parameter, 1);
    }
    Runtime::NameRef name{source_ + nameToken.start,
                          static_cast<uint16_t>(nameToken.length),
                          sourceOffset_};
    const int binding = runtime_.findBinding(name);
    if (execute) {
      if (binding >= 0) {
        result.value = runtime_.bindings_[binding].value;
        result.reference.kind = ReferenceKind::Binding;
        result.reference.binding = static_cast<uint16_t>(binding);
      } else {
        result.value = runtime_.builtinGlobal(name);
        if (runtime_.isUndefined(result.value)) {
          syntax(ErrorCode{61});
        }
      }
    }
    advance();
    return result;
  }
  if (token_.type == T::LeftParen) {
    detail::Lexer scan = lexer_;
    detail::Token scanned = scan.next();
    bool arrow = scanned.type == T::RightParen;
    if (!arrow) {
      arrow = scanned.type == T::Identifier || scanned.type == T::Of;
      while (arrow) {
        scanned = scan.next();
        if (scanned.type == T::RightParen) {
          break;
        }
        if (scanned.type != T::Comma) {
          arrow = false;
          break;
        }
        scanned = scan.next();
        if (!identifierToken(scanned.type)) {
          arrow = false;
          break;
        }
      }
    }
    if (arrow) {
      arrow = scan.next().type == T::Arrow;
    }
    if (arrow) {
      advance(); // (
      detail::ParameterSpan parameters[16];
      size_t count = 0;
      if (token_.type != T::RightParen) {
        do {
          if (!identifierToken(token_.type) ||
              count >= sizeof(parameters) / sizeof(parameters[0])) {
            syntax(ErrorCode{62});
            break;
          }
          parameters[count++] = {token_.start,
                                 static_cast<uint16_t>(token_.length), 0};
          advance();
        } while (accept(T::Comma));
      }
      expect(T::RightParen, ErrorCode{53});
      expect(T::Arrow, ErrorCode{60});
      return arrowFunction(execute, parameters, count);
    }
    NestingGuard nesting(*this);
    if (!nesting) {
      return result;
    }
    advance();
    result = assignment(execute);
    expect(T::RightParen, ErrorCode{53});
    result.reference = Reference{};
    return result;
  }
  syntax(ErrorCode{63});
  return result;
}

inline Parser::Expression Parser::postfix(bool execute) {
  using T = detail::TokenType;
  Expression result = primary(execute);
  bool optionalShortCircuit = false;
  while (runtime_.error_.message[0] == '\0') {
    if (accept(T::OptionalChain)) {
      const ValueType type = runtime_.typeOf(result.value);
      optionalShortCircuit = optionalShortCircuit ||
                             (execute && (type == ValueType::Null ||
                                          type == ValueType::Undefined));
      const bool run = execute && !optionalShortCircuit;
      if (identifierToken(token_.type)) {
        const detail::Token key = token_;
        advance();
        const Value receiver = result.value;
        result.value = run ? runtime_.getProperty(receiver, source_ + key.start,
                                                  key.length)
                           : runtime_.undefined();
        result.reference.kind = ReferenceKind::Property;
        result.reference.receiver = receiver;
        result.reference.name = {source_ + key.start,
                                 static_cast<uint16_t>(key.length),
                                 sourceOffset_};
        result.reference.numeric = false;
        continue;
      }
      if (accept(T::LeftBracket)) {
        Expression keyExpression = assignment(run);
        expect(T::RightBracket, ErrorCode{55});
        const Value receiver = result.value;
        Value propertyValue = runtime_.undefined();
        Runtime::NameRef name;
        int64_t index = 0;
        bool numeric = false;
        if (run) {
          runtime_.getIndex(receiver, keyExpression.value, &propertyValue,
                            &name, &index, &numeric);
        }
        result.value = propertyValue;
        result.reference.kind = ReferenceKind::Property;
        result.reference.receiver = receiver;
        result.reference.name = name;
        result.reference.index = index;
        result.reference.numeric = numeric;
        continue;
      }
      if (accept(T::LeftParen)) {
        Value arguments[24];
        size_t count = 0;
        if (token_.type != T::RightParen) {
          do {
            Expression argument = assignment(run);
            if (run && count < sizeof(arguments) / sizeof(arguments[0]))
              arguments[count] = argument.value;
            count++;
          } while (accept(T::Comma));
        }
        expect(T::RightParen, ErrorCode{53});
        if (count > runtime_.options_.maxCallArguments ||
            count > sizeof(arguments) / sizeof(arguments[0])) {
          syntax(ErrorCode{64});
        } else if (run) {
          result.value = runtime_.callValue(
              result.value,
              result.reference.kind == ReferenceKind::Property
                  ? result.reference.receiver
                  : runtime_.undefined(),
              arguments, count);
        } else {
          result.value = runtime_.undefined();
        }
        result.reference = Reference{};
        continue;
      }
      syntax(ErrorCode{65});
      return result;
    }
    if (accept(T::Dot)) {
      if (!identifierToken(token_.type)) {
        syntax(ErrorCode{66});
        return result;
      }
      const detail::Token key = token_;
      advance();
      const Value receiver = result.value;
      result.value = execute && !optionalShortCircuit
                         ? runtime_.getProperty(receiver, source_ + key.start,
                                                key.length)
                         : runtime_.undefined();
      result.reference.kind = ReferenceKind::Property;
      result.reference.receiver = receiver;
      result.reference.name = {
          source_ + key.start, static_cast<uint16_t>(key.length), sourceOffset_};
      result.reference.numeric = false;
      continue;
    }
    if (accept(T::LeftBracket)) {
      const bool run = execute && !optionalShortCircuit;
      Expression keyExpression = assignment(run);
      expect(T::RightBracket, ErrorCode{55});
      const Value receiver = result.value;
      Value propertyValue = runtime_.undefined();
      Runtime::NameRef name;
      int64_t index = 0;
      bool numeric = false;
      if (run) {
        runtime_.getIndex(receiver, keyExpression.value, &propertyValue, &name,
                          &index, &numeric);
      }
      result.value = propertyValue;
      result.reference.kind = ReferenceKind::Property;
      result.reference.receiver = receiver;
      result.reference.name = name;
      result.reference.index = index;
      result.reference.numeric = numeric;
      continue;
    }
    if (accept(T::LeftParen)) {
      Value arguments[24];
      size_t count = 0;
      if (token_.type != T::RightParen) {
        do {
          const bool run = execute && !optionalShortCircuit;
          Expression argument = assignment(run);
          if (run) {
            if (count >= runtime_.options_.maxCallArguments ||
                count >= sizeof(arguments) / sizeof(arguments[0])) {
              syntax(ErrorCode{64});
              break;
            }
            arguments[count] = argument.value;
          }
          count++;
        } while (accept(T::Comma));
      }
      expect(T::RightParen, ErrorCode{53});
      if (execute && !optionalShortCircuit &&
          runtime_.error_.message[0] == '\0') {
        result.value = runtime_.callValue(
            result.value,
            result.reference.kind == ReferenceKind::Property
                ? result.reference.receiver
                : runtime_.undefined(),
            arguments, count);
      } else {
        result.value = runtime_.undefined();
      }
      result.reference = Reference{};
      continue;
    }
    if (token_.type == T::PlusPlus || token_.type == T::MinusMinus) {
      const T operation = token_.type;
      advance();
      if (execute && !optionalShortCircuit) {
        const Value old = result.value;
        const double next = runtime_.toNumber(old) +
                            (operation == T::PlusPlus ? 1.0 : -1.0);
        store(result.reference, runtime_.number(next));
        result.value = old;
        result.reference = Reference{};
      }
      continue;
    }
    break;
  }
  return result;
}

inline Parser::Expression Parser::unary(bool execute) {
  using T = detail::TokenType;
  const T operation = token_.type;
  if (operation == T::New) {
    // Constructors are value factories in PinkJS; `new` is a compatibility
    // alias and deliberately does not add prototypes or `this` allocation.
    advance();
    return unary(execute);
  }
  if (operation == T::Await) {
    if (!asyncBody_) {
      syntax(ErrorCode{67});
      return {runtime_.undefined()};
    }
    advance();
    Expression value = unary(execute);
    value.value = execute ? runtime_.awaitValue(value.value)
                          : runtime_.undefined();
    value.reference = Reference{};
    return value;
  }
  if (operation == T::Bang || operation == T::Tilde || operation == T::Plus ||
      operation == T::Minus || operation == T::Typeof ||
      operation == T::PlusPlus || operation == T::MinusMinus) {
    NestingGuard nesting(*this);
    if (!nesting) {
      return {runtime_.undefined()};
    }
    advance();
    Expression operand = unary(execute);
    if (!execute) {
      return {runtime_.undefined()};
    }
    if (operation == T::PlusPlus || operation == T::MinusMinus) {
      const double next = runtime_.toNumber(operand.value) +
                          (operation == T::PlusPlus ? 1.0 : -1.0);
      operand.value = runtime_.number(next);
      store(operand.reference, operand.value);
      operand.reference = Reference{};
      return operand;
    }
    if (operation == T::Bang)
      return {runtime_.boolean(!runtime_.isTruthy(operand.value))};
    if (operation == T::Tilde)
      return {runtime_.number(
          static_cast<double>(~detail::toInt32(runtime_.toNumber(
              operand.value))))};
    if (operation == T::Plus)
      return {runtime_.number(runtime_.toNumber(operand.value))};
    if (operation == T::Minus)
      return {runtime_.number(-runtime_.toNumber(operand.value))};
    const char *typeName = "undefined";
    switch (runtime_.typeOf(operand.value)) {
    case ValueType::Boolean:
      typeName = "boolean";
      break;
    case ValueType::Number:
      typeName = "number";
      break;
    case ValueType::String:
      typeName = "string";
      break;
    case ValueType::Function:
      typeName = "function";
      break;
    case ValueType::Null:
    case ValueType::Object:
    case ValueType::Array:
    case ValueType::Set:
    case ValueType::Promise:
      typeName = "object";
      break;
    default:
      break;
    }
    return {runtime_.makeString(typeName)};
  }
  return postfix(execute);
}

inline Parser::Expression
Parser::binary(bool execute, BinaryPrecedence minimumPrecedence) {
  using T = detail::TokenType;
  const auto precedence = [](T operation) -> BinaryPrecedence {
    switch (operation) {
    case T::LogicalOr:
      return BinaryPrecedence::LogicalOr;
    case T::Nullish:
      return BinaryPrecedence::Nullish;
    case T::LogicalAnd:
      return BinaryPrecedence::LogicalAnd;
    case T::Or:
      return BinaryPrecedence::BitwiseOr;
    case T::Xor:
      return BinaryPrecedence::BitwiseXor;
    case T::And:
      return BinaryPrecedence::BitwiseAnd;
    case T::Equal:
    case T::NotEqual:
    case T::StrictEqual:
    case T::StrictNotEqual:
      return BinaryPrecedence::Equality;
    case T::Less:
    case T::LessEqual:
    case T::Greater:
    case T::GreaterEqual:
      return BinaryPrecedence::Relational;
    case T::ShiftLeft:
    case T::ShiftRight:
    case T::ShiftRightUnsigned:
      return BinaryPrecedence::Shift;
    case T::Plus:
    case T::Minus:
      return BinaryPrecedence::Additive;
    case T::Star:
    case T::Slash:
    case T::Percent:
      return BinaryPrecedence::Multiplicative;
    case T::Power:
      return BinaryPrecedence::Power;
    default:
      return BinaryPrecedence::None;
    }
  };

  Expression result = unary(execute);
  while (runtime_.error_.message[0] == '\0') {
    const T operation = token_.type;
    const BinaryPrecedence operationPrecedence = precedence(operation);
    if (operationPrecedence < minimumPrecedence) {
      break;
    }
    advance();
    NestingGuard nesting(*this);
    if (!nesting) {
      return {runtime_.undefined()};
    }

    bool runRight = execute;
    if (operation == T::LogicalAnd) {
      runRight = execute && runtime_.isTruthy(result.value);
    } else if (operation == T::LogicalOr) {
      runRight = execute && !runtime_.isTruthy(result.value);
    } else if (operation == T::Nullish) {
      const ValueType type = runtime_.typeOf(result.value);
      runRight = execute &&
                 (type == ValueType::Null || type == ValueType::Undefined);
    }

    const BinaryPrecedence rightPrecedence =
        operation == T::Power
            ? operationPrecedence
            : static_cast<BinaryPrecedence>(
                  static_cast<uint8_t>(operationPrecedence) + 1U);
    const Expression right = binary(runRight, rightPrecedence);
    if (operation == T::LogicalAnd || operation == T::LogicalOr ||
        operation == T::Nullish) {
      if (runRight) {
        result.value = right.value;
      }
    } else {
      result.value =
          applyBinary(operation, result.value, right.value, execute);
    }
    result.reference = Reference{};
  }
  return result;
}

inline Parser::Expression Parser::conditional(bool execute) {
  Expression condition = binary(execute);
  if (!accept(detail::TokenType::Question)) {
    return condition;
  }
  NestingGuard nesting(*this);
  if (!nesting) {
    return {runtime_.undefined()};
  }
  const bool truth = execute && runtime_.isTruthy(condition.value);
  Expression yes = assignment(truth);
  expect(detail::TokenType::Colon, ErrorCode{58});
  Expression no = assignment(execute && !truth);
  Expression result = truth ? yes : no;
  result.reference = Reference{};
  return result;
}

inline bool Parser::store(const Reference &reference, Value value) {
  if (reference.kind == ReferenceKind::Binding) {
    return runtime_.assignBinding(reference.binding, value);
  }
  if (reference.kind == ReferenceKind::Property) {
    return runtime_.setIndex(reference.receiver, reference.name,
                             reference.index, reference.numeric, value);
  }
  syntax(ErrorCode{68});
  return false;
}

inline Parser::Expression Parser::assignment(bool execute) {
  using T = detail::TokenType;
  Expression left = conditional(execute);
  const T operation = token_.type;
  if (operation != T::Assign && operation != T::PlusAssign &&
      operation != T::MinusAssign && operation != T::StarAssign &&
      operation != T::SlashAssign && operation != T::PercentAssign &&
      operation != T::NullishAssign) {
    return left;
  }
  NestingGuard nesting(*this);
  if (!nesting) {
    return {runtime_.undefined()};
  }
  advance();
  const ValueType leftType = runtime_.typeOf(left.value);
  const bool nullishAssignment = operation == T::NullishAssign;
  const bool assignRight = execute &&
                           (!nullishAssignment || leftType == ValueType::Null ||
                            leftType == ValueType::Undefined);
  Expression right = assignment(assignRight);
  if (!execute) {
    return {runtime_.undefined()};
  }
  if (nullishAssignment && !assignRight) {
    left.reference = Reference{};
    return left;
  }
  Value value = right.value;
  if (operation != T::Assign && operation != T::NullishAssign) {
    T binary = T::Plus;
    if (operation == T::MinusAssign)
      binary = T::Minus;
    else if (operation == T::StarAssign)
      binary = T::Star;
    else if (operation == T::SlashAssign)
      binary = T::Slash;
    else if (operation == T::PercentAssign)
      binary = T::Percent;
    const bool nestedDelimiter =
        token_.type == T::RightParen || token_.type == T::RightBracket ||
        token_.type == T::RightBrace || token_.type == T::Comma ||
        token_.type == T::Colon;
    const bool standaloneAssignment = token_.type == T::Semicolon ||
                                      token_.type == T::End ||
                                      (token_.newlineBefore && !nestedDelimiter);
    if (operation != T::PlusAssign || !standaloneAssignment ||
        !runtime_.appendStringAssignment(left.value, right.value, &value)) {
      value = applyBinary(binary, left.value, right.value, true);
    }
  }
  store(left.reference, value);
  return {value};
}

inline Value Parser::applyBinary(detail::TokenType operation, Value left,
                                 Value right, bool execute) {
  using T = detail::TokenType;
  if (!execute) {
    return runtime_.undefined();
  }
  if (operation == T::Plus &&
      (runtime_.isString(left) || runtime_.isString(right))) {
    return runtime_.concatenate(left, right);
  }
  if (operation == T::StrictEqual || operation == T::StrictNotEqual) {
    const bool equal = runtime_.strictEqual(left, right);
    return runtime_.boolean(operation == T::StrictEqual ? equal : !equal);
  }
  if (operation == T::Equal || operation == T::NotEqual) {
    bool equal = runtime_.strictEqual(left, right);
    const ValueType a = runtime_.typeOf(left);
    const ValueType b = runtime_.typeOf(right);
    if (!equal && ((a == ValueType::Null && b == ValueType::Undefined) ||
                   (a == ValueType::Undefined && b == ValueType::Null))) {
      equal = true;
    } else if (!equal && a != b) {
      const double x = runtime_.toNumber(left);
      const double y = runtime_.toNumber(right);
      equal = !std::isnan(x) && !std::isnan(y) && x == y;
    }
    return runtime_.boolean(operation == T::Equal ? equal : !equal);
  }
  if (operation == T::Less || operation == T::LessEqual ||
      operation == T::Greater || operation == T::GreaterEqual) {
    const int compared = runtime_.compare(left, right);
    bool answer = operation == T::Less      ? compared < 0
                  : operation == T::LessEqual  ? compared <= 0
                  : operation == T::Greater    ? compared > 0
                                               : compared >= 0;
    return runtime_.boolean(answer);
  }
  const double a = runtime_.toNumber(left);
  const double b = runtime_.toNumber(right);
  switch (operation) {
  case T::Plus:
    return runtime_.number(a + b);
  case T::Minus:
    return runtime_.number(a - b);
  case T::Star:
    return runtime_.number(a * b);
  case T::Slash:
    return runtime_.number(a / b);
  case T::Percent:
    return runtime_.number(std::fmod(a, b));
  case T::Power:
    return runtime_.number(std::pow(a, b));
  case T::And:
    return runtime_.number(
        static_cast<double>(detail::toInt32(a) & detail::toInt32(b)));
  case T::Or:
    return runtime_.number(
        static_cast<double>(detail::toInt32(a) | detail::toInt32(b)));
  case T::Xor:
    return runtime_.number(
        static_cast<double>(detail::toInt32(a) ^ detail::toInt32(b)));
  case T::ShiftLeft:
    return runtime_.number(static_cast<double>(detail::toInt32(
        static_cast<double>(detail::toUint32(a) <<
                            (detail::toUint32(b) & 31U)))));
  case T::ShiftRight:
    return runtime_.number(static_cast<double>(
        detail::toInt32(a) >> (detail::toUint32(b) & 31U)));
  case T::ShiftRightUnsigned:
    return runtime_.number(static_cast<double>(
        detail::toUint32(a) >> (detail::toUint32(b) & 31U)));
  default:
    syntax(ErrorCode{69});
    return runtime_.undefined();
  }
}

inline Parser::Flow Parser::program(bool execute) {
  Flow result{FlowKind::Normal, runtime_.undefined()};
  while (token_.type != detail::TokenType::End &&
         runtime_.error_.message[0] == '\0') {
    if (accept(detail::TokenType::Semicolon)) {
      continue;
    }
    Flow next = statement(execute);
    if (execute) {
      result.value = next.value;
    }
    if (next.kind != FlowKind::Normal) {
      return next;
    }
  }
  return result;
}

inline Parser::Flow Parser::statement(bool execute) {
  using T = detail::TokenType;
  NestingGuard nesting(*this);
  if (!nesting) {
    return {FlowKind::Normal, runtime_.undefined()};
  }
  if (token_.type == T::Semicolon) {
    advance();
    return {FlowKind::Normal, runtime_.undefined()};
  }
  switch (token_.type) {
  case T::LeftBrace:
    return block(execute);
  case T::Let:
    return declaration(execute, false);
  case T::Const:
    return declaration(execute, true);
  case T::Function:
    return functionDeclaration(execute, false);
  case T::Async:
    if (peekType() == T::Function)
      return functionDeclaration(execute, true);
    {
      Expression value = assignment(execute);
      terminator();
      return {FlowKind::Normal, value.value};
    }
  case T::If:
    return ifStatement(execute);
  case T::While:
    return whileStatement(execute);
  case T::Do:
    return doWhileStatement(execute);
  case T::For:
    return forStatement(execute);
  case T::Return:
    return returnStatement(execute);
  case T::Break:
    advance();
    terminator();
    if (execute && !loopBody_) {
      syntax(ErrorCode{70});
      return {FlowKind::Normal, runtime_.undefined()};
    }
    return {execute ? FlowKind::Break : FlowKind::Normal,
            runtime_.undefined()};
  case T::Continue:
    advance();
    terminator();
    if (execute && !loopBody_) {
      syntax(ErrorCode{71});
      return {FlowKind::Normal, runtime_.undefined()};
    }
    return {execute ? FlowKind::Continue : FlowKind::Normal,
            runtime_.undefined()};
  default: {
    Expression value = assignment(execute);
    terminator();
    return {FlowKind::Normal, value.value};
  }
  }
}

inline Parser::Flow Parser::block(bool execute, bool createScope) {
  expect(detail::TokenType::LeftBrace, ErrorCode{72});
  const uint16_t marker =
      execute && createScope ? runtime_.enterScope() : runtime_.bindingCount_;
  Flow result{FlowKind::Normal, runtime_.undefined()};
  while (token_.type != detail::TokenType::RightBrace &&
         token_.type != detail::TokenType::End &&
         runtime_.error_.message[0] == '\0') {
    const bool run = execute && result.kind == FlowKind::Normal;
    Flow next = statement(run);
    if (run) {
      result = next;
    }
  }
  expect(detail::TokenType::RightBrace, ErrorCode{49});
  if (execute && createScope) {
    runtime_.leaveScope(marker);
  }
  return result;
}

inline Parser::Flow Parser::declaration(bool execute, bool constant,
                                        bool consumeEnd) {
  advance(); // let / const
  do {
    if (!identifierToken(token_.type)) {
      syntax(ErrorCode{73});
      return {FlowKind::Normal, runtime_.undefined()};
    }
    const detail::Token nameToken = token_;
    advance();
    Value value = runtime_.undefined();
    const bool initialized = accept(detail::TokenType::Assign);
    if (initialized) {
      value = assignment(execute).value;
    } else if (constant) {
      syntax(ErrorCode{74});
      return {FlowKind::Normal, runtime_.undefined()};
    }
    if (execute && runtime_.error_.message[0] == '\0') {
      runtime_.defineBinding(
          Runtime::NameRef{source_ + nameToken.start,
                           static_cast<uint16_t>(nameToken.length),
                           sourceOffset_},
          value, constant);
    }
  } while (accept(detail::TokenType::Comma));
  if (consumeEnd) {
    terminator();
  }
  return {FlowKind::Normal, runtime_.undefined()};
}

inline Parser::Flow Parser::functionDeclaration(bool execute,
                                                 bool asyncBody) {
  if (asyncBody) {
    advance(); // async
  }
  if (token_.type != detail::TokenType::Function) {
    syntax(ErrorCode{75});
    return {FlowKind::Normal, runtime_.undefined()};
  }
  detail::ParameterSpan name;
  Expression function = traditionalFunction(execute, asyncBody, &name);
  if (name.length == 0) {
    syntax(ErrorCode{76});
    return {FlowKind::Normal, runtime_.undefined()};
  }
  if (execute && runtime_.error_.message[0] == '\0') {
    runtime_.defineBinding(
        Runtime::NameRef{source_ + name.start, name.length, sourceOffset_},
        function.value, true);
  }
  accept(detail::TokenType::Semicolon);
  return {FlowKind::Normal, function.value};
}

inline Parser::Flow Parser::ifStatement(bool execute) {
  advance();
  expect(detail::TokenType::LeftParen, ErrorCode{50});
  Expression condition = assignment(execute);
  expect(detail::TokenType::RightParen, ErrorCode{53});
  const bool truth = execute && runtime_.isTruthy(condition.value);
  Flow chosen = statement(truth);
  if (accept(detail::TokenType::Else)) {
    Flow other = statement(execute && !truth);
    if (execute && !truth) {
      chosen = other;
    }
  }
  return execute ? chosen
                 : Flow{FlowKind::Normal, runtime_.undefined()};
}

inline Parser::Span Parser::captureStatement() {
  const uint32_t begin = token_.start;
  statement(false);
  return {begin, token_.start};
}

inline Parser::Flow Parser::executeStatementSpan(Span span, bool loopBody) {
  Parser nested(runtime_, sourceOffset_, span.begin, span.end, functionBody_,
                loopBody, asyncBody_);
  return nested.statement(true);
}

inline Value Parser::executeExpressionSpan(Span span) {
  if (span.begin >= span.end) {
    return runtime_.boolean(true);
  }
  Parser nested(runtime_, sourceOffset_, span.begin, span.end, functionBody_,
                loopBody_, asyncBody_);
  Value value = nested.expression(true);
  if (nested.token_.type != detail::TokenType::End &&
      runtime_.error_.message[0] == '\0') {
    nested.syntax(ErrorCode{77});
  }
  return value;
}

inline Parser::Flow Parser::whileStatement(bool execute) {
  advance();
  expect(detail::TokenType::LeftParen, ErrorCode{50});
  const uint32_t conditionBegin = token_.start;
  assignment(false);
  const Span condition{conditionBegin, token_.start};
  expect(detail::TokenType::RightParen, ErrorCode{53});
  const Span body = captureStatement();
  if (!execute || runtime_.error_.message[0] != '\0') {
    return {FlowKind::Normal, runtime_.undefined()};
  }
  while (runtime_.isTruthy(executeExpressionSpan(condition)) &&
         runtime_.error_.message[0] == '\0') {
    if (!runtime_.step(source_, body.begin)) {
      break;
    }
    Flow flow = executeStatementSpan(body, true);
    if (flow.kind == FlowKind::Return) {
      return flow;
    }
    if (flow.kind == FlowKind::Break) {
      break;
    }
  }
  return {FlowKind::Normal, runtime_.undefined()};
}

inline Parser::Flow Parser::doWhileStatement(bool execute) {
  advance();
  const Span body = captureStatement();
  if (!expect(detail::TokenType::While, ErrorCode{78}) ||
      !expect(detail::TokenType::LeftParen, ErrorCode{50})) {
    return {FlowKind::Normal, runtime_.undefined()};
  }
  const uint32_t conditionBegin = token_.start;
  assignment(false);
  const Span condition{conditionBegin, token_.start};
  expect(detail::TokenType::RightParen, ErrorCode{53});
  if (token_.type == detail::TokenType::Semicolon) {
    advance();
  }
  if (!execute || runtime_.error_.message[0] != '\0') {
    return {FlowKind::Normal, runtime_.undefined()};
  }
  do {
    if (!runtime_.step(source_, body.begin)) {
      break;
    }
    Flow flow = executeStatementSpan(body, true);
    if (flow.kind == FlowKind::Return) {
      return flow;
    }
    if (flow.kind == FlowKind::Break) {
      break;
    }
  } while (runtime_.isTruthy(executeExpressionSpan(condition)) &&
           runtime_.error_.message[0] == '\0');
  return {FlowKind::Normal, runtime_.undefined()};
}

inline size_t Parser::iterableLength(Value iterable) const {
  if (detail::tagOf(iterable) == detail::Tag::Array ||
      detail::tagOf(iterable) == detail::Tag::Set) {
    return runtime_.arrayLength(iterable);
  }
  if (detail::tagOf(iterable) == detail::Tag::String) {
    return runtime_.stringView(iterable).size;
  }
  return 0;
}

inline bool Parser::iterableAt(Value iterable, size_t index, Value *value) const {
  if (value == nullptr) {
    return false;
  }
  if (detail::tagOf(iterable) == detail::Tag::Array ||
      detail::tagOf(iterable) == detail::Tag::Set) {
    if (index >= runtime_.arrayLength(iterable)) {
      return false;
    }
    *value = runtime_.arrayAt(iterable, index);
    return true;
  }
  if (detail::tagOf(iterable) == detail::Tag::String) {
    const StringView text = runtime_.stringView(iterable);
    if (index >= text.size) {
      return false;
    }
    *value = const_cast<Runtime &>(runtime_).makeString(text.data + index, 1);
    return true;
  }
  return false;
}

inline Parser::Flow Parser::forStatement(bool execute) {
  using T = detail::TokenType;
  advance();
  expect(T::LeftParen, ErrorCode{50});
  const uint16_t scopeMarker =
      execute ? runtime_.enterScope() : runtime_.bindingCount_;
  bool declarationInit = token_.type == T::Let || token_.type == T::Const;
  bool declarationConst = token_.type == T::Const;
  detail::Token firstName;
  bool hasFirstName = false;

  if (declarationInit) {
    advance();
    if (!identifierToken(token_.type)) {
      syntax(ErrorCode{79});
    } else {
      firstName = token_;
      hasFirstName = true;
      advance();
    }
    if (token_.type == T::Of || token_.type == T::In) {
      const bool enumerateKeys = token_.type == T::In;
      advance();
      Value iterable = assignment(execute).value;
      expect(T::RightParen, ErrorCode{53});
      const Span body = captureStatement();
      int binding = -1;
      if (execute && hasFirstName && runtime_.error_.message[0] == '\0') {
        runtime_.defineBinding(
            Runtime::NameRef{source_ + firstName.start,
                             static_cast<uint16_t>(firstName.length),
                             sourceOffset_},
            runtime_.undefined(), declarationConst);
        binding = runtime_.findBinding(
            Runtime::NameRef{source_ + firstName.start,
                             static_cast<uint16_t>(firstName.length),
                             sourceOffset_});
      }
      Flow result{FlowKind::Normal, runtime_.undefined()};
      if (execute && binding >= 0) {
        size_t length = iterableLength(iterable);
        if (enumerateKeys) {
          if (detail::tagOf(iterable) == detail::Tag::Object)
            length = runtime_.objectPropertyCount(iterable);
          else if (detail::tagOf(iterable) == detail::Tag::Array ||
                   detail::tagOf(iterable) == detail::Tag::String)
            length = detail::tagOf(iterable) == detail::Tag::Array
                         ? runtime_.arrayLength(iterable)
                         : runtime_.stringView(iterable).size;
          else
            length = 0;
        }
        for (size_t i = 0; i < length &&
                           runtime_.error_.message[0] == '\0';
             i++) {
          Value item;
          if (enumerateKeys) {
            if (detail::tagOf(iterable) == detail::Tag::Object) {
              Value ignored;
              if (!runtime_.objectPropertyAt(iterable, i, &item, &ignored))
                break;
            } else {
              char key[24];
              const int count =
                  std::snprintf(key, sizeof(key), "%lu",
                                static_cast<unsigned long>(i));
              item = runtime_.makeString(
                  key, count > 0 ? static_cast<size_t>(count) : 0);
            }
          } else if (!iterableAt(iterable, i, &item)) {
            break;
          }
          runtime_.bindings_[binding].value = item;
          Flow flow = executeStatementSpan(body, true);
          if (flow.kind == FlowKind::Return) {
            result = flow;
            break;
          }
          if (flow.kind == FlowKind::Break) {
            break;
          }
        }
      }
      if (execute) {
        runtime_.leaveScope(scopeMarker);
      }
      return result;
    }

    if (hasFirstName) {
      Value value = runtime_.undefined();
      const bool initialized = accept(T::Assign);
      if (initialized) {
        value = assignment(execute).value;
      } else if (declarationConst) {
        syntax(ErrorCode{74});
      }
      if (execute && runtime_.error_.message[0] == '\0') {
        runtime_.defineBinding(
            Runtime::NameRef{source_ + firstName.start,
                             static_cast<uint16_t>(firstName.length),
                             sourceOffset_},
            value, declarationConst);
      }
      while (accept(T::Comma)) {
        if (!identifierToken(token_.type)) {
          syntax(ErrorCode{73});
          break;
        }
        const detail::Token name = token_;
        advance();
        Value nextValue = runtime_.undefined();
        const bool nextInitialized = accept(T::Assign);
        if (nextInitialized) {
          nextValue = assignment(execute).value;
        } else if (declarationConst) {
          syntax(ErrorCode{74});
        }
        if (execute && runtime_.error_.message[0] == '\0') {
          runtime_.defineBinding(
              Runtime::NameRef{source_ + name.start,
                               static_cast<uint16_t>(name.length),
                               sourceOffset_},
              nextValue, declarationConst);
        }
      }
    }
  } else if (identifierToken(token_.type) &&
             (peekType() == T::Of || peekType() == T::In)) {
    const detail::Token name = token_;
    advance();
    const bool enumerateKeys = token_.type == T::In;
    advance();
    Value iterable = assignment(execute).value;
    expect(T::RightParen, ErrorCode{53});
    const Span body = captureStatement();
    Flow result{FlowKind::Normal, runtime_.undefined()};
    const Runtime::NameRef nameRef{source_ + name.start,
                                   static_cast<uint16_t>(name.length),
                                   sourceOffset_};
    const int binding = execute ? runtime_.findBinding(nameRef) : -1;
    if (execute && binding < 0)
      syntax(ErrorCode{80});
    size_t length = iterableLength(iterable);
    if (enumerateKeys) {
      if (detail::tagOf(iterable) == detail::Tag::Object)
        length = runtime_.objectPropertyCount(iterable);
      else if (detail::tagOf(iterable) == detail::Tag::Array)
        length = runtime_.arrayLength(iterable);
      else if (detail::tagOf(iterable) == detail::Tag::String)
        length = runtime_.stringView(iterable).size;
      else
        length = 0;
    }
    for (size_t i = 0; execute && binding >= 0 && i < length &&
                       runtime_.error_.message[0] == '\0';
         i++) {
      Value item;
      if (enumerateKeys) {
        if (detail::tagOf(iterable) == detail::Tag::Object) {
          Value ignored;
          if (!runtime_.objectPropertyAt(iterable, i, &item, &ignored))
            break;
        } else {
          char key[24];
          const int count = std::snprintf(key, sizeof(key), "%lu",
                                          static_cast<unsigned long>(i));
          item = runtime_.makeString(
              key, count > 0 ? static_cast<size_t>(count) : 0);
        }
      } else if (!iterableAt(iterable, i, &item)) {
        break;
      }
      if (!runtime_.assignBinding(static_cast<size_t>(binding), item))
        break;
      Flow flow = executeStatementSpan(body, true);
      if (flow.kind == FlowKind::Return) {
        result = flow;
        break;
      }
      if (flow.kind == FlowKind::Break)
        break;
    }
    if (execute)
      runtime_.leaveScope(scopeMarker);
    return result;
  } else if (token_.type != T::Semicolon) {
    assignment(execute);
  }
  expect(T::Semicolon, ErrorCode{81});

  const uint32_t conditionBegin = token_.start;
  if (token_.type != T::Semicolon) {
    assignment(false);
  }
  const Span condition{conditionBegin, token_.start};
  expect(T::Semicolon, ErrorCode{81});

  const uint32_t incrementBegin = token_.start;
  if (token_.type != T::RightParen) {
    assignment(false);
  }
  const Span increment{incrementBegin, token_.start};
  expect(T::RightParen, ErrorCode{53});
  const Span body = captureStatement();

  Flow result{FlowKind::Normal, runtime_.undefined()};
  if (execute && runtime_.error_.message[0] == '\0') {
    while ((condition.begin == condition.end ||
            runtime_.isTruthy(executeExpressionSpan(condition))) &&
           runtime_.error_.message[0] == '\0') {
      if (!runtime_.step(source_, body.begin)) {
        break;
      }
      Flow flow = executeStatementSpan(body, true);
      if (flow.kind == FlowKind::Return) {
        result = flow;
        break;
      }
      if (flow.kind == FlowKind::Break) {
        break;
      }
      if (increment.begin != increment.end) {
        executeExpressionSpan(increment);
      }
    }
  }
  if (execute) {
    runtime_.leaveScope(scopeMarker);
  }
  return result;
}

inline Parser::Flow Parser::returnStatement(bool execute) {
  advance();
  if (execute && !functionBody_) {
    syntax(ErrorCode{82});
    return {FlowKind::Normal, runtime_.undefined()};
  }
  Value value = runtime_.undefined();
  if (token_.type != detail::TokenType::Semicolon &&
      token_.type != detail::TokenType::RightBrace &&
      token_.type != detail::TokenType::End && !token_.newlineBefore) {
    value = assignment(execute).value;
  }
  terminator();
  return {execute ? FlowKind::Return : FlowKind::Normal, value};
}

inline RunResult Runtime::eval(const char *source, size_t length) {
  clearError();
  if (!ready_ || source == nullptr || length > UINT32_MAX) {
    setError(!ready_ ? ErrorCode{217} : ErrorCode{218}, nullptr, 0);
    return {false, undefined()};
  }
  if (running_) {
    setError(ErrorCode{83}, nullptr, 0);
    return {false, undefined()};
  }
  collectGarbage();
  Value sourceValue = makeTextBlock(detail::BlockKind::Source,
                                    detail::Tag::Source, source, length);
  const uint32_t sourceBlock = sourceOffset(sourceValue);
  if (sourceBlock == 0 || !pushRoot(sourceValue)) {
    return {false, undefined()};
  }
  stepsRemaining_ = options_.maxSteps;
  running_ = true;
  Parser parser(*this, sourceBlock, 0, static_cast<uint32_t>(length));
  Parser::Flow flow = parser.program(true);
  if (flow.kind != Parser::FlowKind::Normal && error_.message[0] == '\0') {
    setError(ErrorCode{84}, sourceData(sourceBlock), 0);
  }
  lastResult_ = flow.value;
  running_ = false;
  popRoots(1);
  collectGarbage();
  return {error_.message[0] == '\0',
          error_.message[0] == '\0' ? lastResult_ : undefined()};
}

inline RunResult Runtime::call(const char *name, const Value *args,
                               size_t argc) {
  bool called = false;
  RunResult result = callIfFunction(name, args, argc, &called);
  if (result.ok && !called) {
    setError(ErrorCode{85}, nullptr, 0);
    result.ok = false;
    result.value = undefined();
  }
  return result;
}

inline RunResult Runtime::call(Value function, const Value *args,
                               size_t argc) {
  clearError();
  if (!ready_ || running_ || !isCallable(function) ||
      (args == nullptr && argc != 0) || argc > options_.maxCallArguments) {
    setError(ErrorCode{86}, nullptr, 0);
    return {false, undefined()};
  }
  const uint16_t rootStart = temporaryRootCount_;
  if (!pushRoot(function)) {
    return {false, undefined()};
  }
  for (size_t i = 0; i < argc; i++) {
    if (!pushRoot(args[i])) {
      popRoots(static_cast<uint16_t>(temporaryRootCount_ - rootStart));
      return {false, undefined()};
    }
  }
  collectGarbage();
  stepsRemaining_ = options_.maxSteps;
  running_ = true;
  Value result = callValue(function, undefined(), args, argc);
  lastResult_ = result;
  running_ = false;
  popRoots(static_cast<uint16_t>(temporaryRootCount_ - rootStart));
  collectGarbage();
  return {error_.message[0] == '\0',
          error_.message[0] == '\0' ? result : undefined()};
}

inline RunResult Runtime::callIfFunction(const char *name, const Value *args,
                                         size_t argc, bool *called) {
  clearError();
  if (called != nullptr) {
    *called = false;
  }
  if (!ready_ || running_ || name == nullptr ||
      (args == nullptr && argc != 0) || argc > options_.maxCallArguments) {
    setError(ErrorCode{86}, nullptr, 0);
    return {false, undefined()};
  }
  const uint16_t rootStart = temporaryRootCount_;
  for (size_t i = 0; i < argc; i++) {
    if (!pushRoot(args[i])) {
      popRoots(static_cast<uint16_t>(temporaryRootCount_ - rootStart));
      return {false, undefined()};
    }
  }
  collectGarbage();
  Value function = get(name);
  if (!isCallable(function)) {
    popRoots(static_cast<uint16_t>(temporaryRootCount_ - rootStart));
    return {true, undefined()};
  }
  if (called != nullptr) {
    *called = true;
  }
  stepsRemaining_ = options_.maxSteps;
  running_ = true;
  Value result = callValue(function, undefined(), args, argc);
  lastResult_ = result;
  running_ = false;
  popRoots(static_cast<uint16_t>(temporaryRootCount_ - rootStart));
  collectGarbage();
  return {error_.message[0] == '\0',
          error_.message[0] == '\0' ? result : undefined()};
}

inline Value Runtime::callValue(Value function, Value receiver,
                                const Value *args, size_t argc) {
  if (argc > options_.maxCallArguments) {
    return fail(ErrorCode{64});
  }
  const detail::Tag tag = detail::tagOf(function);
  if (tag == detail::Tag::Native) {
    const size_t index = static_cast<size_t>(detail::payloadOf(function));
    if (index >= nativeCount_ || natives_[index].callback.function == nullptr) {
      return fail(ErrorCode{87});
    }
    return natives_[index].callback.function(*this, args, argc,
                                             natives_[index].userData);
  }
  if (tag == detail::Tag::NativeMethod) {
    const size_t index = static_cast<size_t>(detail::payloadOf(function));
    if (index >= nativeCount_ || natives_[index].callback.method == nullptr) {
      return fail(ErrorCode{88});
    }
    return natives_[index].callback.method(*this, receiver, args, argc,
                                           natives_[index].userData);
  }
  if (tag == detail::Tag::Resolver) {
    const uint64_t encoded = detail::payloadOf(function);
    const Value promise =
        detail::boxed(detail::Tag::Promise, static_cast<uint32_t>(encoded >> 1U));
    settlePromise(promise, argc > 0 ? args[0] : undefined(),
                  (encoded & 1U) != 0);
    return undefined();
  }
  if (tag == detail::Tag::BuiltinFunction) {
    return callBuiltin(
        static_cast<detail::BuiltinFunction>(detail::payloadOf(function)),
        receiver, args, argc);
  }
  if (tag == detail::Tag::BuiltinObject) {
    const detail::BuiltinObject constructor =
        static_cast<detail::BuiltinObject>(detail::payloadOf(function));
    switch (constructor) {
    case detail::BuiltinObject::Array: {
      if (argc == 1 && isNumber(args[0])) {
        const double requested = toNumber(args[0]);
        if (!std::isfinite(requested) || requested < 0 ||
            requested > UINT32_MAX || std::floor(requested) != requested) {
          return fail(ErrorCode{89});
        }
        Value array = makeArray(static_cast<size_t>(requested));
        if (requested > 0) {
          arraySet(array, static_cast<size_t>(requested) - 1U, undefined());
        }
        return array;
      }
      Value array = makeArray(argc);
      for (size_t i = 0; i < argc; i++) {
        arrayPush(array, args[i]);
      }
      return array;
    }
    case detail::BuiltinObject::Object:
      if (argc > 0 && (typeOf(args[0]) == ValueType::Object ||
                       typeOf(args[0]) == ValueType::Array ||
                       typeOf(args[0]) == ValueType::Set ||
                       typeOf(args[0]) == ValueType::Promise)) {
        return args[0];
      }
      return makeObject();
    case detail::BuiltinObject::String:
      return argc > 0 ? toStringValue(args[0]) : makeString("");
    case detail::BuiltinObject::Number:
      return number(argc > 0 ? toNumber(args[0]) : 0.0);
    case detail::BuiltinObject::Boolean:
      return boolean(argc > 0 && isTruthy(args[0]));
    case detail::BuiltinObject::Date: {
      double milliseconds = static_cast<double>(nowMilliseconds());
      if (argc == 1) {
        if (isString(args[0])) {
          Value parsed = callBuiltin(detail::BuiltinFunction::DateParse,
                                     undefined(), args, 1);
          milliseconds = toNumber(parsed);
        } else {
          milliseconds = toNumber(args[0]);
        }
      } else if (argc > 1) {
        milliseconds = toNumber(callBuiltin(detail::BuiltinFunction::DateUtc,
                                             undefined(), args, argc));
      }
      return makeDate(milliseconds);
    }
    case detail::BuiltinObject::Promise: {
      if (argc != 1 || !isCallable(args[0])) {
        return fail(ErrorCode{90});
      }
      Value promise = makePromise();
      if (isUndefined(promise)) {
        return promise;
      }
      const uint64_t offset = detail::payloadOf(promise);
      Value resolverArgs[2] = {
          detail::boxed(detail::Tag::Resolver, offset << 1U),
          detail::boxed(detail::Tag::Resolver, (offset << 1U) | 1U)};
      callValue(args[0], undefined(), resolverArgs, 2);
      if (error_.message[0] != '\0') {
        char message[sizeof(error_.message)];
        std::snprintf(message, sizeof(message), "%s", error_.message);
        clearError();
        settlePromise(promise, makeString(message), true);
      }
      return promise;
    }
    case detail::BuiltinObject::Math:
    case detail::BuiltinObject::Json:
      return fail(ErrorCode{91});
    }
  }
  if (tag != detail::Tag::Function) {
    return fail(ErrorCode{92});
  }
  if (callDepth_ >= options_.maxCallDepth) {
    return fail(ErrorCode{93});
  }
  const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(function));
  if (!validOffset(offset, detail::BlockKind::Function)) {
    return fail(ErrorCode{24});
  }
  const auto *functionData =
      static_cast<const detail::FunctionPayload *>(payload(offset));
  const detail::ParameterSpan *parameters =
      reinterpret_cast<const detail::ParameterSpan *>(functionData + 1);
  const char *source = sourceData(functionData->source);
  if (source == nullptr || functionData->bodyEnd < functionData->bodyStart ||
      functionData->bodyEnd > sourceLength(functionData->source)) {
    return fail(ErrorCode{94});
  }

  const uint16_t savedBindingCount = bindingCount_;
  const uint16_t savedFrameBase = frameBase_;
  const uint16_t savedScopeDepth = scopeDepth_;
  Value asyncPromise = undefined();
  if (functionData->asyncBody != 0) {
    asyncPromise = makePromise();
    if (isUndefined(asyncPromise)) {
      return asyncPromise;
    }
  }
  frameBase_ = bindingCount_;
  scopeDepth_ = 0;
  callDepth_++;
  if (functionData->name.length > 0 &&
      functionData->name.start + functionData->name.length <=
          sourceLength(functionData->source)) {
    defineBinding(NameRef{source + functionData->name.start,
                          functionData->name.length, functionData->source},
                  function, true);
  }
  for (size_t i = 0; i < functionData->parameterCount; i++) {
    const detail::ParameterSpan &parameter = parameters[i];
    if (parameter.start + parameter.length > sourceLength(functionData->source)) {
      setError(ErrorCode{95}, nullptr, 0);
      break;
    }
    defineBinding(NameRef{source + parameter.start, parameter.length,
                          functionData->source},
                  i < argc ? args[i] : undefined(), false);
  }

  Value result = undefined();
  if (error_.message[0] == '\0') {
    Parser parser(*this, functionData->source, functionData->bodyStart,
                  functionData->bodyEnd, true, false,
                  functionData->asyncBody != 0);
    if (functionData->expressionBody != 0) {
      result = parser.expression(true);
    } else {
      Parser::Flow flow = parser.program(true);
      if (flow.kind == Parser::FlowKind::Return) {
        result = flow.value;
      }
    }
  }
  bindingCount_ = savedBindingCount;
  frameBase_ = savedFrameBase;
  scopeDepth_ = savedScopeDepth;
  callDepth_--;
  if (functionData->asyncBody != 0) {
    if (error_.message[0] != '\0') {
      char message[sizeof(error_.message)];
      std::snprintf(message, sizeof(message), "%s", error_.message);
      clearError();
      adoptPromise(asyncPromise, makeString(message), true);
    } else {
      adoptPromise(asyncPromise, result, false);
    }
    return asyncPromise;
  }
  return result;
}

inline Value Runtime::callBuiltin(detail::BuiltinFunction function,
                                  Value receiver, const Value *args,
                                  size_t argc) {
  using B = detail::BuiltinFunction;
  auto argument = [this, args, argc](size_t index) {
    return index < argc ? args[index] : undefined();
  };
  auto requireReceiver = [this, receiver](detail::Tag tag, ErrorCode code) {
    if (detail::tagOf(receiver) != tag) {
      setError(code, nullptr, 0);
      return false;
    }
    return true;
  };
  auto normalizedIndex = [](double raw, size_t length, bool end) {
    if (std::isnan(raw))
      return size_t(0);
    if (!std::isfinite(raw))
      return raw < 0 ? size_t(0) : length;
    if (raw <= static_cast<double>(INT64_MIN))
      return size_t(0);
    if (raw >= static_cast<double>(INT64_MAX))
      return length;
    int64_t index = static_cast<int64_t>(raw);
    if (index < 0)
      index += static_cast<int64_t>(length);
    if (index < 0)
      index = 0;
    if (index > static_cast<int64_t>(length))
      index = static_cast<int64_t>(length);
    (void)end;
    return static_cast<size_t>(index);
  };

  switch (function) {
  case B::SetConstructor: {
    Value set = makeSet();
    const Value source = argument(0);
    if (detail::tagOf(source) == detail::Tag::Array ||
        detail::tagOf(source) == detail::Tag::Set) {
      const size_t length = arrayLength(source);
      for (size_t i = 0; i < length; i++) {
        collectionPush(set, arrayAt(source, i), true);
      }
    } else if (isString(source)) {
      const StringView text = stringView(source);
      for (size_t i = 0; i < text.size; i++)
        collectionPush(set, makeString(text.data + i, 1), true);
    } else if (!isUndefined(source)) {
      return fail(ErrorCode{96});
    }
    return set;
  }
  case B::ParseInt: {
    Value textValue = toStringValue(argument(0));
    const StringView text = stringView(textValue);
    int base = argc > 1 ? static_cast<int>(toNumber(argument(1))) : 10;
    if (base == 0)
      base = 10;
    char *end = nullptr;
    const long parsed = std::strtol(text.data, &end, base);
    return end == text.data ? detail::boxed(detail::Tag::NumberNaN)
                            : number(static_cast<double>(parsed));
  }
  case B::ParseFloat: {
    Value textValue = toStringValue(argument(0));
    const StringView text = stringView(textValue);
    char *end = nullptr;
    const double parsed = std::strtod(text.data, &end);
    return end == text.data ? detail::boxed(detail::Tag::NumberNaN)
                            : number(parsed);
  }
  case B::IsNaN:
    return boolean(std::isnan(toNumber(argument(0))));
  case B::MathAbs:
    return number(std::fabs(toNumber(argument(0))));
  case B::MathFloor:
    return number(std::floor(toNumber(argument(0))));
  case B::MathCeil:
    return number(std::ceil(toNumber(argument(0))));
  case B::MathRound:
    return number(std::round(toNumber(argument(0))));
  case B::MathTrunc:
    return number(std::trunc(toNumber(argument(0))));
  case B::MathSqrt:
    return number(std::sqrt(toNumber(argument(0))));
  case B::MathPow:
    return number(std::pow(toNumber(argument(0)), toNumber(argument(1))));
  case B::MathMin:
  case B::MathMax: {
    if (argc == 0) {
      return number(function == B::MathMin
                        ? std::numeric_limits<double>::infinity()
                        : -std::numeric_limits<double>::infinity());
    }
    double result = toNumber(args[0]);
    for (size_t i = 1; i < argc; i++) {
      const double next = toNumber(args[i]);
      result = function == B::MathMin ? std::fmin(result, next)
                                      : std::fmax(result, next);
    }
    return number(result);
  }
  case B::MathRandom:
    randomState_ ^= randomState_ << 13U;
    randomState_ ^= randomState_ >> 17U;
    randomState_ ^= randomState_ << 5U;
    return number(static_cast<double>(randomState_) /
                  static_cast<double>(UINT32_MAX));
  case B::JsonParse:
    if (!isString(argument(0)))
      return fail(ErrorCode{97});
    if (options_.jsonParse != nullptr) {
      return options_.jsonParse(*this, stringView(argument(0)),
                                options_.jsonUserData);
    }
#if defined(PINKJS_DISABLE_BUILTIN_JSON_PARSER)
    return fail(ErrorCode{36});
#else
    return jsonParse(stringView(argument(0)));
#endif
  case B::JsonStringify:
    return jsonStringify(argument(0));
  case B::DateNow:
    return number(static_cast<double>(nowMilliseconds()));
  case B::DateUtc: {
    if (argc < 2) {
      return detail::boxed(detail::Tag::NumberNaN);
    }
    for (size_t i = 0; i < argc && i < 7; i++) {
      const double field = toNumber(args[i]);
      if (!std::isfinite(field) || std::fabs(field) > 1000000.0) {
        return detail::boxed(detail::Tag::NumberNaN);
      }
    }
    const int year = static_cast<int>(toNumber(argument(0)));
    const int monthZero = static_cast<int>(toNumber(argument(1)));
    const int day = argc > 2 ? static_cast<int>(toNumber(argument(2))) : 1;
    const int hour = argc > 3 ? static_cast<int>(toNumber(argument(3))) : 0;
    const int minute = argc > 4 ? static_cast<int>(toNumber(argument(4))) : 0;
    const int second = argc > 5 ? static_cast<int>(toNumber(argument(5))) : 0;
    const int millisecond =
        argc > 6 ? static_cast<int>(toNumber(argument(6))) : 0;
    int adjustedYear = year + monthZero / 12;
    int adjustedMonth = monthZero % 12;
    if (adjustedMonth < 0) {
      adjustedMonth += 12;
      adjustedYear--;
    }
    const int64_t days =
        daysFromCivil(adjustedYear, static_cast<unsigned>(adjustedMonth + 1), 1) +
        day - 1;
    return number(static_cast<double>(days * INT64_C(86400000) +
                                      int64_t(hour) * 3600000 +
                                      int64_t(minute) * 60000 +
                                      int64_t(second) * 1000 + millisecond));
  }
  case B::DateParse: {
    if (!isString(argument(0)))
      return detail::boxed(detail::Tag::NumberNaN);
    const StringView text = stringView(argument(0));
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0,
        milliseconds = 0;
    const int fields = std::sscanf(text.data, "%d-%d-%dT%d:%d:%d.%d", &year,
                                   &month, &day, &hour, &minute, &second,
                                   &milliseconds);
    if (fields < 3) {
      return detail::boxed(detail::Tag::NumberNaN);
    }
    const int64_t total = daysFromCivil(year, static_cast<unsigned>(month),
                                        static_cast<unsigned>(day)) *
                              INT64_C(86400000) +
                          int64_t(hour) * 3600000 + int64_t(minute) * 60000 +
                          int64_t(second) * 1000 + milliseconds;
    return number(static_cast<double>(total));
  }
  case B::DateToIsoString:
    return isoDate(toNumber(argument(0)));
  case B::DateCall:
    return isoDate(static_cast<double>(nowMilliseconds()));
  case B::ArrayIsArray:
    return boolean(detail::tagOf(argument(0)) == detail::Tag::Array);
  case B::ArrayFrom: {
    const Value source = argument(0);
    Value result = makeArray();
    if (detail::tagOf(source) == detail::Tag::Array ||
        detail::tagOf(source) == detail::Tag::Set) {
      for (size_t i = 0; i < arrayLength(source); i++)
        arrayPush(result, arrayAt(source, i));
    } else if (isString(source)) {
      const StringView text = stringView(source);
      for (size_t i = 0; i < text.size; i++)
        arrayPush(result, makeString(text.data + i, 1));
    } else {
      return fail(ErrorCode{98});
    }
    return result;
  }
  case B::ObjectKeys:
  case B::ObjectValues:
  case B::ObjectEntries: {
    const Value object = argument(0);
    if (detail::tagOf(object) != detail::Tag::Object)
      return fail(ErrorCode{10});
    Value result = makeArray();
    const size_t count = objectPropertyCount(object);
    for (size_t i = 0; i < count; i++) {
      Value key;
      Value item;
      objectPropertyAt(object, i, &key, &item);
      if (function == B::ObjectEntries) {
        Value entry = makeArray(2);
        arrayPush(entry, key);
        arrayPush(entry, item);
        arrayPush(result, entry);
      } else {
        arrayPush(result, function == B::ObjectKeys ? key : item);
      }
    }
    return result;
  }
  case B::ObjectAssign: {
    Value target = argument(0);
    if (detail::tagOf(target) != detail::Tag::Object) {
      return fail(ErrorCode{99});
    }
    for (size_t sourceIndex = 1; sourceIndex < argc; sourceIndex++) {
      const Value source = args[sourceIndex];
      if (detail::tagOf(source) != detail::Tag::Object) {
        continue;
      }
      const size_t count = objectPropertyCount(source);
      for (size_t i = 0; i < count; i++) {
        Value key;
        Value item;
        objectPropertyAt(source, i, &key, &item);
        const StringView text = stringView(key);
        objectSet(target, text.data, text.size, item);
      }
    }
    return target;
  }
  case B::ObjectHasOwn: {
    const Value object = argument(0);
    if (detail::tagOf(object) != detail::Tag::Object) {
      return boolean(false);
    }
    Value key = toStringValue(argument(1));
    const StringView text = stringView(key);
    return boolean(findProperty(object, text.data, text.size) != 0);
  }
  case B::ObjectFromEntries: {
    const Value entries = argument(0);
    if (detail::tagOf(entries) != detail::Tag::Array &&
        detail::tagOf(entries) != detail::Tag::Set) {
      return fail(ErrorCode{100});
    }
    Value result = makeObject();
    for (size_t i = 0; i < arrayLength(entries); i++) {
      const Value entry = arrayAt(entries, i);
      if (detail::tagOf(entry) != detail::Tag::Array ||
          arrayLength(entry) < 2) {
        return fail(ErrorCode{101});
      }
      Value key = toStringValue(arrayAt(entry, 0));
      const StringView text = stringView(key);
      objectSet(result, text.data, text.size, arrayAt(entry, 1));
    }
    return result;
  }
  case B::ArrayPush: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{102}))
      return undefined();
    for (size_t i = 0; i < argc; i++)
      arrayPush(receiver, args[i]);
    return number(static_cast<double>(arrayLength(receiver)));
  }
  case B::ArrayPop: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{103}))
      return undefined();
    Value removed = undefined();
    const size_t length = arrayLength(receiver);
    if (length > 0)
      collectionRemoveAt(receiver, length - 1U, &removed);
    return removed;
  }
  case B::ArrayShift: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{104}))
      return undefined();
    Value removed = undefined();
    collectionRemoveAt(receiver, 0, &removed);
    return removed;
  }
  case B::ArrayUnshift: {
    if (!requireReceiver(detail::Tag::Array,
                         ErrorCode{105}))
      return undefined();
    const size_t oldLength = arrayLength(receiver);
    if (!reserveCollection(receiver, oldLength + argc))
      return undefined();
    detail::CollectionPayload *owner = collection(receiver);
    detail::ValuesPayload *values = valuesPayload(owner->values);
    Value *items = valuesData(values);
    std::memmove(items + argc, items, oldLength * sizeof(Value));
    for (size_t i = 0; i < argc; i++)
      items[i] = args[i];
    values->length = static_cast<uint32_t>(oldLength + argc);
    return number(static_cast<double>(values->length));
  }
  case B::ArraySlice: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{106}))
      return undefined();
    const size_t length = arrayLength(receiver);
    size_t begin = argc > 0 ? normalizedIndex(toNumber(args[0]), length, false) : 0;
    size_t end = argc > 1 ? normalizedIndex(toNumber(args[1]), length, true) : length;
    if (end < begin)
      end = begin;
    Value result = makeArray(end - begin);
    for (size_t i = begin; i < end; i++)
      arrayPush(result, arrayAt(receiver, i));
    return result;
  }
  case B::ArrayIndexOf:
  case B::ArrayIncludes:
  case B::SetHas: {
    const bool isSetMethod = function == B::SetHas;
    if (!requireReceiver(isSetMethod ? detail::Tag::Set : detail::Tag::Array,
                         ErrorCode{107}))
      return undefined();
    int found = -1;
    for (size_t i = 0; i < arrayLength(receiver); i++) {
      if (strictEqual(arrayAt(receiver, i), argument(0))) {
        found = static_cast<int>(i);
        break;
      }
    }
    return function == B::ArrayIndexOf ? number(found) : boolean(found >= 0);
  }
  case B::ArrayJoin: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{108}))
      return undefined();
    Value separatorValue = argc > 0 ? toStringValue(args[0]) : makeString(",");
    const StringView separator = stringView(separatorValue);
    size_t total = 0;
    for (size_t i = 0; i < arrayLength(receiver); i++) {
      if (i > 0)
        total += separator.size;
      total += format(arrayAt(receiver, i), nullptr, 0);
    }
    char *out = nullptr;
    Value result = makeStringBuffer(total, &out);
    size_t at = 0;
    for (size_t i = 0; out != nullptr && i < arrayLength(receiver); i++) {
      if (i > 0) {
        std::memcpy(out + at, separator.data, separator.size);
        at += separator.size;
      }
      at += format(arrayAt(receiver, i), out + at, total - at + 1U);
    }
    return result;
  }
  case B::ArrayConcat: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{109}))
      return undefined();
    Value result = makeArray(arrayLength(receiver));
    for (size_t i = 0; i < arrayLength(receiver); i++)
      arrayPush(result, arrayAt(receiver, i));
    for (size_t i = 0; i < argc; i++) {
      if (detail::tagOf(args[i]) == detail::Tag::Array) {
        for (size_t n = 0; n < arrayLength(args[i]); n++)
          arrayPush(result, arrayAt(args[i], n));
      } else {
        arrayPush(result, args[i]);
      }
    }
    return result;
  }
  case B::ArrayForEach:
  case B::ArrayMap:
  case B::ArrayFilter:
  case B::ArrayFind:
  case B::ArraySome:
  case B::ArrayEvery:
  case B::SetForEach: {
    const bool setMethod = function == B::SetForEach;
    if (!requireReceiver(setMethod ? detail::Tag::Set : detail::Tag::Array,
                         ErrorCode{110}) ||
        !isCallable(argument(0)))
      return fail(ErrorCode{111});
    Value output = function == B::ArrayMap || function == B::ArrayFilter
                       ? makeArray(arrayLength(receiver))
                       : undefined();
    const size_t length = arrayLength(receiver);
    for (size_t i = 0; i < length && error_.message[0] == '\0'; i++) {
      const Value item = arrayAt(receiver, i);
      Value callbackArgs[3] = {item, number(static_cast<double>(i)), receiver};
      Value callbackResult = callValue(argument(0), undefined(), callbackArgs, 3);
      if (function == B::ArrayMap)
        arrayPush(output, callbackResult);
      else if (function == B::ArrayFilter && isTruthy(callbackResult))
        arrayPush(output, item);
      else if (function == B::ArrayFind && isTruthy(callbackResult))
        return item;
      else if (function == B::ArraySome && isTruthy(callbackResult))
        return boolean(true);
      else if (function == B::ArrayEvery && !isTruthy(callbackResult))
        return boolean(false);
    }
    if (function == B::ArrayMap || function == B::ArrayFilter)
      return output;
    if (function == B::ArraySome)
      return boolean(false);
    if (function == B::ArrayEvery)
      return boolean(true);
    return undefined();
  }
  case B::ArrayReduce: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{112}) ||
        !isCallable(argument(0)))
      return fail(ErrorCode{111});
    const size_t length = arrayLength(receiver);
    if (length == 0 && argc < 2)
      return fail(ErrorCode{113});
    size_t index = 0;
    Value accumulator = argc > 1 ? args[1] : arrayAt(receiver, index++);
    for (; index < length && error_.message[0] == '\0'; index++) {
      Value callbackArgs[4] = {accumulator, arrayAt(receiver, index),
                               number(static_cast<double>(index)), receiver};
      accumulator = callValue(argument(0), undefined(), callbackArgs, 4);
    }
    return accumulator;
  }
  case B::ArrayReverse: {
    if (!requireReceiver(detail::Tag::Array,
                         ErrorCode{114}))
      return undefined();
    detail::ValuesPayload *values = valuesPayload(collection(receiver)->values);
    Value *items = valuesData(values);
    for (size_t i = 0; i < values->length / 2U; i++) {
      Value temporary = items[i];
      items[i] = items[values->length - 1U - i];
      items[values->length - 1U - i] = temporary;
    }
    return receiver;
  }
  case B::ArraySort: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{115}))
      return undefined();
    const Value comparator = argument(0);
    if (!isUndefined(comparator) && !isCallable(comparator))
      return fail(ErrorCode{116});
    detail::ValuesPayload *values = valuesPayload(collection(receiver)->values);
    Value *items = valuesData(values);
    Value keys = undefined();
    if (isUndefined(comparator)) {
      keys = makeArray(values->length);
      for (size_t i = 0; i < values->length; i++)
        arrayPush(keys, toStringValue(items[i]));
    }
    for (size_t i = 1; i < values->length && error_.message[0] == '\0'; i++) {
      const Value item = items[i];
      const Value key = isUndefined(comparator) ? arrayAt(keys, i) : undefined();
      size_t at = i;
      while (at > 0) {
        int order = 0;
        if (isCallable(comparator)) {
          Value compareArgs[2] = {items[at - 1U], item};
          order = toNumber(callValue(comparator, undefined(), compareArgs, 2)) > 0
                      ? 1
                      : 0;
        } else {
          order = compare(arrayAt(keys, at - 1U), key) > 0 ? 1 : 0;
        }
        if (order == 0 || error_.message[0] != '\0')
          break;
        items[at] = items[at - 1U];
        if (!isUndefined(comparator)) {
          // Nothing else to move when a callback computes ordering.
        } else {
          arraySet(keys, at, arrayAt(keys, at - 1U));
        }
        at--;
      }
      items[at] = item;
      if (isUndefined(comparator))
        arraySet(keys, at, key);
    }
    return receiver;
  }
  case B::ArraySplice: {
    if (!requireReceiver(detail::Tag::Array,
                         ErrorCode{117}))
      return undefined();
    const size_t length = arrayLength(receiver);
    const size_t start =
        argc > 0 ? normalizedIndex(toNumber(args[0]), length, false) : 0;
    size_t removeCount = length - start;
    if (argc > 1) {
      const double raw = toNumber(args[1]);
      removeCount = !std::isfinite(raw) || raw >= static_cast<double>(SIZE_MAX)
                        ? (raw > 0 ? length - start : 0)
                        : static_cast<size_t>(raw <= 0 ? 0 : std::floor(raw));
      if (removeCount > length - start)
        removeCount = length - start;
    }
    const size_t insertCount = argc > 2 ? argc - 2U : 0;
    Value removed = makeArray(removeCount);
    for (size_t i = 0; i < removeCount; i++)
      arrayPush(removed, arrayAt(receiver, start + i));
    const size_t nextLength = length - removeCount + insertCount;
    if (!reserveCollection(receiver, nextLength))
      return undefined();
    detail::ValuesPayload *values = valuesPayload(collection(receiver)->values);
    Value *items = valuesData(values);
    const size_t tail = length - start - removeCount;
    if (tail > 0 && insertCount != removeCount) {
      std::memmove(items + start + insertCount, items + start + removeCount,
                   tail * sizeof(Value));
    }
    for (size_t i = 0; i < insertCount; i++)
      items[start + i] = args[i + 2U];
    values->length = static_cast<uint32_t>(nextLength);
    return removed;
  }
  case B::ArrayAt: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{118}))
      return undefined();
    const size_t length = arrayLength(receiver);
    const double raw = toNumber(argument(0));
    if (!std::isfinite(raw) || raw < static_cast<double>(INT64_MIN) ||
        raw > static_cast<double>(INT64_MAX))
      return undefined();
    int64_t index = static_cast<int64_t>(std::trunc(raw));
    if (index < 0)
      index += static_cast<int64_t>(length);
    return index >= 0 && static_cast<size_t>(index) < length
               ? arrayAt(receiver, static_cast<size_t>(index))
               : undefined();
  }
  case B::ArrayEntries:
  case B::ArrayKeys:
  case B::ArrayValues: {
    if (!requireReceiver(detail::Tag::Array,
                         ErrorCode{119}))
      return undefined();
    Value result = makeArray(arrayLength(receiver));
    for (size_t i = 0; i < arrayLength(receiver); i++) {
      if (function == B::ArrayEntries) {
        Value entry = makeArray(2);
        arrayPush(entry, number(static_cast<double>(i)));
        arrayPush(entry, arrayAt(receiver, i));
        arrayPush(result, entry);
      } else {
        arrayPush(result, function == B::ArrayKeys
                              ? number(static_cast<double>(i))
                              : arrayAt(receiver, i));
      }
    }
    return result;
  }
  case B::ArrayFindIndex: {
    if (!requireReceiver(detail::Tag::Array,
                         ErrorCode{120}) ||
        !isCallable(argument(0)))
      return fail(ErrorCode{111});
    for (size_t i = 0; i < arrayLength(receiver); i++) {
      Value callbackArgs[3] = {arrayAt(receiver, i),
                               number(static_cast<double>(i)), receiver};
      if (isTruthy(callValue(argument(0), undefined(), callbackArgs, 3)))
        return number(static_cast<double>(i));
      if (error_.message[0] != '\0')
        break;
    }
    return number(-1);
  }
  case B::ArrayLastIndexOf: {
    if (!requireReceiver(detail::Tag::Array,
                         ErrorCode{121}))
      return undefined();
    const size_t length = arrayLength(receiver);
    int64_t start = length == 0 ? -1 : static_cast<int64_t>(length - 1U);
    if (argc > 1) {
      const double raw = toNumber(args[1]);
      if (std::isnan(raw))
        start = 0;
      else if (std::isfinite(raw) &&
               raw > static_cast<double>(INT64_MIN) &&
               raw < static_cast<double>(INT64_MAX)) {
        start = static_cast<int64_t>(std::trunc(raw));
        if (start < 0)
          start += static_cast<int64_t>(length);
        if (start >= static_cast<int64_t>(length))
          start = static_cast<int64_t>(length) - 1;
      }
    }
    for (int64_t i = start; i >= 0; i--)
      if (strictEqual(arrayAt(receiver, static_cast<size_t>(i)), argument(0)))
        return number(static_cast<double>(i));
    return number(-1);
  }
  case B::ArrayFill: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{122}))
      return undefined();
    const size_t length = arrayLength(receiver);
    const size_t begin =
        argc > 1 ? normalizedIndex(toNumber(args[1]), length, false) : 0;
    const size_t end =
        argc > 2 ? normalizedIndex(toNumber(args[2]), length, true) : length;
    for (size_t i = begin; i < end; i++)
      arraySet(receiver, i, argument(0));
    return receiver;
  }
  case B::ArrayFlat:
  case B::ArrayFlatMap: {
    if (!requireReceiver(detail::Tag::Array, ErrorCode{123}))
      return undefined();
    const bool mapFirst = function == B::ArrayFlatMap;
    if (mapFirst && !isCallable(argument(0)))
      return fail(ErrorCode{124});
    const double rawDepth = argc > 0 ? toNumber(args[0]) : 1;
    int depth = mapFirst || !std::isfinite(rawDepth)
                    ? 1
                    : (rawDepth <= 0 ? 0 : (rawDepth >= 8 ? 8
                                                           : static_cast<int>(rawDepth)));
    if (depth < 0)
      depth = 0;
    if (depth > 8)
      depth = 8;
    Value source = receiver;
    if (mapFirst) {
      source = makeArray(arrayLength(receiver));
      for (size_t i = 0; i < arrayLength(receiver); i++) {
        Value callbackArgs[3] = {arrayAt(receiver, i),
                                 number(static_cast<double>(i)), receiver};
        arrayPush(source,
                  callValue(argument(0), undefined(), callbackArgs, 3));
      }
    }
    Value result = makeArray();
    struct Frame {
      Value value;
      size_t index;
      int depth;
    } stack[16];
    size_t stackSize = 1;
    stack[0] = {source, 0, depth};
    while (stackSize > 0 && error_.message[0] == '\0') {
      Frame &frame = stack[stackSize - 1U];
      if (frame.index >= arrayLength(frame.value)) {
        stackSize--;
        continue;
      }
      const Value item = arrayAt(frame.value, frame.index++);
      if (frame.depth > 0 && detail::tagOf(item) == detail::Tag::Array) {
        if (stackSize >= sizeof(stack) / sizeof(stack[0]))
          return fail(ErrorCode{125});
        stack[stackSize++] = {item, 0, frame.depth - 1};
      } else {
        arrayPush(result, item);
      }
    }
    return result;
  }
  case B::SetAdd:
    if (!requireReceiver(detail::Tag::Set, ErrorCode{126}))
      return undefined();
    collectionPush(receiver, argument(0), true);
    return receiver;
  case B::SetDelete: {
    if (!requireReceiver(detail::Tag::Set, ErrorCode{127}))
      return undefined();
    for (size_t i = 0; i < arrayLength(receiver); i++) {
      if (strictEqual(arrayAt(receiver, i), argument(0))) {
        collectionRemoveAt(receiver, i, nullptr);
        return boolean(true);
      }
    }
    return boolean(false);
  }
  case B::SetClear: {
    if (!requireReceiver(detail::Tag::Set, ErrorCode{128}))
      return undefined();
    detail::ValuesPayload *values = valuesPayload(collection(receiver)->values);
    values->length = 0;
    return undefined();
  }
  case B::SetValues: {
    if (!requireReceiver(detail::Tag::Set, ErrorCode{129}))
      return undefined();
    Value result = makeArray(arrayLength(receiver));
    for (size_t i = 0; i < arrayLength(receiver); i++)
      arrayPush(result, arrayAt(receiver, i));
    return result;
  }
  case B::SetEntries: {
    if (!requireReceiver(detail::Tag::Set, ErrorCode{130}))
      return undefined();
    Value result = makeArray(arrayLength(receiver));
    for (size_t i = 0; i < arrayLength(receiver); i++) {
      Value entry = makeArray(2);
      const Value item = arrayAt(receiver, i);
      arrayPush(entry, item);
      arrayPush(entry, item);
      arrayPush(result, entry);
    }
    return result;
  }
  case B::PromiseResolve: {
    const Value value = argument(0);
    if (detail::tagOf(value) == detail::Tag::Promise)
      return value;
    Value promise = makePromise();
    adoptPromise(promise, value, false);
    return promise;
  }
  case B::PromiseReject: {
    Value promise = makePromise();
    adoptPromise(promise, argument(0), true);
    return promise;
  }
  case B::PromiseThen:
    return promiseThen(receiver, argument(0), argument(1));
  case B::PromiseCatch:
    return promiseThen(receiver, undefined(), argument(0));
  case B::PromiseFinally: {
    if (!isCallable(argument(0)))
      return fail(ErrorCode{131});
    Value child = promiseThen(receiver, argument(0), argument(0));
    detail::PromisePayload *data = promisePayload(receiver);
    if (data != nullptr)
      data->reserved = 1;
    return child;
  }
  case B::NumberIsFinite:
    return boolean(isNumber(argument(0)) &&
                   std::isfinite(toNumber(argument(0))));
  case B::NumberIsNaN:
    return boolean(isNumber(argument(0)) &&
                   std::isnan(toNumber(argument(0))));
  case B::NumberIsInteger: {
    const double value = toNumber(argument(0));
    return boolean(isNumber(argument(0)) && std::isfinite(value) &&
                   std::floor(value) == value);
  }
  case B::NumberValueOf:
    return number(toNumber(receiver));
  case B::NumberToFixed: {
    const double rawDigits = argc > 0 ? toNumber(args[0]) : 0;
    if (!std::isfinite(rawDigits) || rawDigits < 0 || rawDigits > 12)
      return fail(ErrorCode{132});
    char text[96];
    const int length = std::snprintf(text, sizeof(text), "%.*f",
                                     static_cast<int>(rawDigits),
                                     toNumber(receiver));
    return makeString(text, length > 0 ? static_cast<size_t>(length) : 0);
  }
  case B::NumberToString: {
    const double value = toNumber(receiver);
    const double rawRadix = argc > 0 ? toNumber(args[0]) : 10;
    if (!std::isfinite(rawRadix))
      return fail(ErrorCode{133});
    if (rawRadix < 2 || rawRadix > 36)
      return fail(ErrorCode{133});
    const int radix = static_cast<int>(rawRadix);
    if (radix == 10 || !std::isfinite(value) || std::floor(value) != value)
      return toStringValue(receiver);
    if (radix < 2 || radix > 36 || std::fabs(value) > 9007199254740991.0)
      return fail(ErrorCode{133});
    static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char reverse[72];
    size_t length = 0;
    double remaining = std::fabs(value);
    do {
      const double quotient = std::floor(remaining / radix);
      const int digit = static_cast<int>(remaining - quotient * radix);
      reverse[length++] = digits[digit];
      remaining = quotient;
    } while (remaining > 0 && length < sizeof(reverse) - 1U);
    char output[73];
    size_t at = 0;
    if (value < 0)
      output[at++] = '-';
    while (length > 0)
      output[at++] = reverse[--length];
    return makeString(output, at);
  }
  case B::StringFromCharCode: {
    char *output = nullptr;
    Value result = makeStringBuffer(argc * 3U, &output);
    size_t written = 0;
    for (size_t i = 0; output != nullptr && i < argc; i++) {
      const uint32_t code = detail::toUint32(toNumber(args[i])) & 0xffffU;
      if (code <= 0x7fU) {
        output[written++] = static_cast<char>(code);
      } else if (code <= 0x7ffU) {
        output[written++] = static_cast<char>(0xc0U | (code >> 6U));
        output[written++] = static_cast<char>(0x80U | (code & 0x3fU));
      } else {
        output[written++] = static_cast<char>(0xe0U | (code >> 12U));
        output[written++] =
            static_cast<char>(0x80U | ((code >> 6U) & 0x3fU));
        output[written++] = static_cast<char>(0x80U | (code & 0x3fU));
      }
    }
    setTextLength(result, written);
    return result;
  }
  case B::DateGetTime:
  case B::DateInstanceToIsoString:
  case B::DateGetFullYear:
  case B::DateGetMonth:
  case B::DateGetDate:
  case B::DateGetDay:
  case B::DateGetHours:
  case B::DateGetMinutes:
  case B::DateGetSeconds:
  case B::DateGetMilliseconds: {
    if (!requireReceiver(detail::Tag::Date, ErrorCode{134}))
      return undefined();
    const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(receiver));
    const double raw = static_cast<const detail::DatePayload *>(payload(offset))
                           ->milliseconds;
    if (function == B::DateGetTime)
      return number(raw);
    if (function == B::DateInstanceToIsoString)
      return isoDate(raw);
    if (!std::isfinite(raw))
      return detail::boxed(detail::Tag::NumberNaN);
    int64_t total = static_cast<int64_t>(std::floor(raw));
    int64_t days = total / INT64_C(86400000);
    int64_t dayTime = total % INT64_C(86400000);
    if (dayTime < 0) {
      dayTime += INT64_C(86400000);
      days--;
    }
    int year = 0;
    unsigned month = 0, day = 0;
    civilFromDays(days, year, month, day);
    if (function == B::DateGetFullYear)
      return number(year);
    if (function == B::DateGetMonth)
      return number(month - 1U);
    if (function == B::DateGetDate)
      return number(day);
    if (function == B::DateGetDay) {
      int weekday = static_cast<int>((days + 4) % 7);
      if (weekday < 0)
        weekday += 7;
      return number(weekday);
    }
    if (function == B::DateGetHours)
      return number(dayTime / INT64_C(3600000));
    dayTime %= INT64_C(3600000);
    if (function == B::DateGetMinutes)
      return number(dayTime / INT64_C(60000));
    dayTime %= INT64_C(60000);
    if (function == B::DateGetSeconds)
      return number(dayTime / 1000);
    return number(dayTime % 1000);
  }
  default:
    break;
  }

  if (function >= B::StringCharAt && function <= B::StringCharCodeAt) {
    if (!requireReceiver(detail::Tag::String, ErrorCode{135}))
      return undefined();
    const StringView text = stringView(receiver);
    if (function == B::StringCharAt || function == B::StringAt ||
        function == B::StringCharCodeAt) {
      const double rawIndex = toNumber(argument(0));
      if (!std::isfinite(rawIndex) ||
          rawIndex < static_cast<double>(INT64_MIN) ||
          rawIndex > static_cast<double>(INT64_MAX)) {
        return function == B::StringCharCodeAt
                   ? detail::boxed(detail::Tag::NumberNaN)
                   : (function == B::StringAt ? undefined() : makeString(""));
      }
      int64_t index = static_cast<int64_t>(std::trunc(rawIndex));
      if (function == B::StringAt && index < 0)
        index += static_cast<int64_t>(text.size);
      if (index < 0 || static_cast<size_t>(index) >= text.size) {
        return function == B::StringCharCodeAt
                   ? detail::boxed(detail::Tag::NumberNaN)
                   : (function == B::StringAt ? undefined() : makeString(""));
      }
      return function == B::StringCharCodeAt
                 ? number(static_cast<unsigned char>(text.data[index]))
                 : makeString(text.data + index, 1);
    }
    if (function == B::StringSlice || function == B::StringSubstring) {
      size_t begin = argc > 0 ? normalizedIndex(toNumber(args[0]), text.size, false)
                              : 0;
      size_t end = argc > 1 ? normalizedIndex(toNumber(args[1]), text.size, true)
                            : text.size;
      if (function == B::StringSubstring && begin > end) {
        const size_t swap = begin;
        begin = end;
        end = swap;
      } else if (end < begin) {
        end = begin;
      }
      return makeString(text.data + begin, end - begin);
    }
    if (function == B::StringTrim || function == B::StringTrimStart ||
        function == B::StringTrimEnd) {
      size_t begin = 0, end = text.size;
      if (function != B::StringTrimEnd) {
        while (begin < end &&
               static_cast<unsigned char>(text.data[begin]) <= ' ')
          begin++;
      }
      if (function != B::StringTrimStart) {
        while (end > begin &&
               static_cast<unsigned char>(text.data[end - 1U]) <= ' ')
          end--;
      }
      return makeString(text.data + begin, end - begin);
    }
    if (function == B::StringUpper || function == B::StringLower) {
      char *out = nullptr;
      Value result = makeStringBuffer(text.size, &out);
      for (size_t i = 0; out != nullptr && i < text.size; i++) {
        char c = text.data[i];
        if (function == B::StringUpper && c >= 'a' && c <= 'z')
          c = static_cast<char>(c - 'a' + 'A');
        if (function == B::StringLower && c >= 'A' && c <= 'Z')
          c = static_cast<char>(c - 'A' + 'a');
        out[i] = c;
      }
      return result;
    }
    if (function == B::StringConcat) {
      Value result = receiver;
      for (size_t i = 0; i < argc && error_.message[0] == '\0'; i++)
        result = concatenate(result, args[i]);
      return result;
    }
    if (function == B::StringRepeat) {
      const double raw = toNumber(argument(0));
      if (!std::isfinite(raw) || raw < 0 || raw > UINT32_MAX)
        return fail(ErrorCode{136});
      const size_t count = static_cast<size_t>(std::floor(raw));
      if (text.size != 0 && count > UINT32_MAX / text.size)
        return fail(ErrorCode{137});
      char *output = nullptr;
      Value result = makeStringBuffer(text.size * count, &output);
      for (size_t i = 0; output != nullptr && i < count; i++)
        std::memcpy(output + i * text.size, text.data, text.size);
      return result;
    }
    if (function == B::StringPadStart || function == B::StringPadEnd) {
      const double raw = toNumber(argument(0));
      if (!std::isfinite(raw) || raw <= static_cast<double>(text.size))
        return receiver;
      if (raw > UINT32_MAX)
        return fail(ErrorCode{138});
      const size_t target = static_cast<size_t>(std::floor(raw));
      Value fillValue = argc > 1 ? toStringValue(args[1]) : makeString(" ");
      const StringView fill = stringView(fillValue);
      if (fill.size == 0)
        return receiver;
      char *output = nullptr;
      Value result = makeStringBuffer(target, &output);
      const size_t padding = target - text.size;
      const size_t textAt = function == B::StringPadStart ? padding : 0;
      for (size_t i = 0; output != nullptr && i < padding; i++) {
        const size_t at = function == B::StringPadStart ? i : text.size + i;
        output[at] = fill.data[i % fill.size];
      }
      if (output != nullptr)
        std::memcpy(output + textAt, text.data, text.size);
      return result;
    }
    Value needleValue = toStringValue(argument(0));
    const StringView needle = stringView(needleValue);
    if (function == B::StringStartsWith) {
      return boolean(needle.size <= text.size &&
                     std::memcmp(text.data, needle.data, needle.size) == 0);
    }
    if (function == B::StringEndsWith) {
      return boolean(needle.size <= text.size &&
                     std::memcmp(text.data + text.size - needle.size,
                                 needle.data, needle.size) == 0);
    }
    if (function == B::StringIndexOf || function == B::StringIncludes ||
        function == B::StringLastIndexOf) {
      int found = needle.size == 0 ? 0 : -1;
      for (size_t i = 0; needle.size > 0 && i + needle.size <= text.size; i++) {
        if (std::memcmp(text.data + i, needle.data, needle.size) == 0) {
          found = static_cast<int>(i);
          if (function != B::StringLastIndexOf)
            break;
        }
      }
      return function == B::StringIncludes ? boolean(found >= 0)
                                            : number(found);
    }
    if (function == B::StringReplace || function == B::StringReplaceAll) {
      Value replacementValue = toStringValue(argument(1));
      const StringView replacement = stringView(replacementValue);
      size_t matches = 0;
      if (needle.size == 0) {
        matches = function == B::StringReplace ? 1 : text.size + 1U;
      } else {
        for (size_t i = 0; i + needle.size <= text.size;) {
          if (std::memcmp(text.data + i, needle.data, needle.size) == 0) {
            matches++;
            i += needle.size;
            if (function == B::StringReplace)
              break;
          } else {
            i++;
          }
        }
      }
      if (matches == 0)
        return receiver;
      const size_t removed = matches * needle.size;
      if (replacement.size > 0 &&
          matches > (UINT32_MAX - (text.size - removed)) / replacement.size)
        return fail(ErrorCode{139});
      const size_t outputSize = text.size - removed + matches * replacement.size;
      char *output = nullptr;
      Value result = makeStringBuffer(outputSize, &output);
      size_t inputAt = 0, outputAt = 0, replaced = 0;
      while (output != nullptr && inputAt <= text.size && replaced < matches) {
        const bool match = needle.size == 0 ||
                           (inputAt + needle.size <= text.size &&
                            std::memcmp(text.data + inputAt, needle.data,
                                        needle.size) == 0);
        if (match) {
          std::memcpy(output + outputAt, replacement.data, replacement.size);
          outputAt += replacement.size;
          inputAt += needle.size;
          replaced++;
          if (function == B::StringReplace)
            break;
          if (needle.size == 0 && inputAt < text.size)
            output[outputAt++] = text.data[inputAt++];
        } else {
          output[outputAt++] = text.data[inputAt++];
        }
      }
      if (output != nullptr && inputAt < text.size) {
        std::memcpy(output + outputAt, text.data + inputAt,
                    text.size - inputAt);
      }
      return result;
    }
    if (function == B::StringSplit) {
      Value result = makeArray();
      if (argc == 0) {
        arrayPush(result, receiver);
        return result;
      }
      if (needle.size == 0) {
        for (size_t i = 0; i < text.size; i++)
          arrayPush(result, makeString(text.data + i, 1));
        return result;
      }
      size_t begin = 0;
      for (size_t i = 0; i + needle.size <= text.size;) {
        if (std::memcmp(text.data + i, needle.data, needle.size) == 0) {
          arrayPush(result, makeString(text.data + begin, i - begin));
          i += needle.size;
          begin = i;
        } else {
          i++;
        }
      }
      arrayPush(result, makeString(text.data + begin, text.size - begin));
      return result;
    }
  }
  return fail(ErrorCode{140});
}

#if !defined(PINKJS_DISABLE_BUILTIN_JSON_PARSER)
class JsonReader {
public:
  JsonReader(Runtime &runtime, StringView input)
      : runtime_(runtime), text_(input.data), length_(input.size) {}

  Value parse() {
    skip();
    Value result = value(0);
    skip();
    if (position_ != length_ && runtime_.error().message[0] == '\0') {
      runtime_.fail(ErrorCode{141});
    }
    return result;
  }

private:
  Runtime &runtime_;
  const char *text_ = nullptr;
  size_t length_ = 0;
  size_t position_ = 0;

  void skip() {
    while (position_ < length_ &&
           (text_[position_] == ' ' || text_[position_] == '\t' ||
            text_[position_] == '\r' || text_[position_] == '\n')) {
      position_++;
    }
  }

  bool consume(char expected) {
    skip();
    if (position_ < length_ && text_[position_] == expected) {
      position_++;
      return true;
    }
    runtime_.fail(ErrorCode{142});
    return false;
  }

  bool literal(const char *word) {
    const size_t size = std::strlen(word);
    if (position_ + size <= length_ &&
        std::memcmp(text_ + position_, word, size) == 0) {
      position_ += size;
      return true;
    }
    runtime_.fail(ErrorCode{143});
    return false;
  }

  static int hex(char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  }

  Value string() {
    skip();
    if (position_ >= length_ || text_[position_] != '"') {
      return runtime_.fail(ErrorCode{144});
    }
    const size_t start = ++position_;
    size_t scan = position_;
    bool escaped = false;
    while (scan < length_) {
      const char c = text_[scan++];
      if (!escaped && c == '"')
        break;
      if (!escaped && static_cast<unsigned char>(c) < 0x20U)
        return runtime_.fail(ErrorCode{145});
      if (!escaped && c == '\\')
        escaped = true;
      else
        escaped = false;
    }
    if (scan > length_ || scan == start || text_[scan - 1U] != '"') {
      return runtime_.fail(ErrorCode{146});
    }
    char *output = nullptr;
    Value result = runtime_.makeStringBuffer(scan - start - 1U, &output);
    if (output == nullptr) {
      return runtime_.undefined();
    }
    size_t written = 0;
    while (position_ + 1U < scan) {
      char c = text_[position_++];
      if (c != '\\') {
        output[written++] = c;
        continue;
      }
      if (position_ + 1U >= scan)
        return runtime_.fail(ErrorCode{147});
      c = text_[position_++];
      switch (c) {
      case '"':
      case '\\':
      case '/':
        output[written++] = c;
        break;
      case 'b':
        output[written++] = '\b';
        break;
      case 'f':
        output[written++] = '\f';
        break;
      case 'n':
        output[written++] = '\n';
        break;
      case 'r':
        output[written++] = '\r';
        break;
      case 't':
        output[written++] = '\t';
        break;
      case 'u': {
        if (position_ + 4U >= scan)
          return runtime_.fail(ErrorCode{148});
        uint32_t codepoint = 0;
        for (uint8_t i = 0; i < 4; i++) {
          const int digit = hex(text_[position_++]);
          if (digit < 0)
            return runtime_.fail(ErrorCode{148});
          codepoint = codepoint * 16U + static_cast<uint32_t>(digit);
        }
        if (codepoint <= 0x7fU) {
          output[written++] = static_cast<char>(codepoint);
        } else if (codepoint <= 0x7ffU) {
          output[written++] = static_cast<char>(0xc0U | (codepoint >> 6U));
          output[written++] =
              static_cast<char>(0x80U | (codepoint & 0x3fU));
        } else {
          output[written++] = static_cast<char>(0xe0U | (codepoint >> 12U));
          output[written++] =
              static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
          output[written++] =
              static_cast<char>(0x80U | (codepoint & 0x3fU));
        }
        break;
      }
      default:
        return runtime_.fail(ErrorCode{147});
      }
    }
    position_ = scan;
    runtime_.setTextLength(result, written);
    return result;
  }

  Value value(size_t depth) {
    if (depth > 20)
      return runtime_.fail(ErrorCode{149});
    skip();
    if (position_ >= length_)
      return runtime_.fail(ErrorCode{150});
    const char c = text_[position_];
    if (c == '"')
      return string();
    if (c == '{') {
      position_++;
      Value object = runtime_.makeObject();
      skip();
      if (position_ < length_ && text_[position_] == '}') {
        position_++;
        return object;
      }
      while (runtime_.error().message[0] == '\0') {
        Value key = string();
        if (!consume(':'))
          break;
        Value item = value(depth + 1U);
        const StringView keyText = runtime_.stringView(key);
        runtime_.objectSet(object, keyText.data, keyText.size, item);
        skip();
        if (position_ < length_ && text_[position_] == '}') {
          position_++;
          break;
        }
        if (!consume(','))
          break;
      }
      return object;
    }
    if (c == '[') {
      position_++;
      Value array = runtime_.makeArray();
      skip();
      if (position_ < length_ && text_[position_] == ']') {
        position_++;
        return array;
      }
      while (runtime_.error().message[0] == '\0') {
        runtime_.arrayPush(array, value(depth + 1U));
        skip();
        if (position_ < length_ && text_[position_] == ']') {
          position_++;
          break;
        }
        if (!consume(','))
          break;
      }
      return array;
    }
    if (c == 't') {
      return literal("true") ? runtime_.boolean(true) : runtime_.undefined();
    }
    if (c == 'f') {
      return literal("false") ? runtime_.boolean(false) : runtime_.undefined();
    }
    if (c == 'n') {
      return literal("null") ? runtime_.null() : runtime_.undefined();
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      char *end = nullptr;
      const double parsed = std::strtod(text_ + position_, &end);
      if (end == text_ + position_ ||
          static_cast<size_t>(end - text_) > length_) {
        return runtime_.fail(ErrorCode{151});
      }
      position_ = static_cast<size_t>(end - text_);
      return runtime_.number(parsed);
    }
    return runtime_.fail(ErrorCode{152});
  }
};

inline Value Runtime::jsonParse(StringView json) {
  JsonReader reader(*this, json);
  return reader.parse();
}
#endif

inline size_t Runtime::jsonMeasure(Value value, uint32_t *parents,
                                   size_t depth, bool &ok) {
  if (!ok || depth >= 16) {
    ok = false;
    return 0;
  }
  const ValueType type = typeOf(value);
  if (detail::tagOf(value) == detail::Tag::Date) {
    const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(value));
    if (!validOffset(offset, detail::BlockKind::Date)) {
      ok = false;
      return 0;
    }
    Value iso = const_cast<Runtime *>(this)->isoDate(
        static_cast<const detail::DatePayload *>(payload(offset))->milliseconds);
    if (error_.message[0] != '\0') {
      ok = false;
      return 0;
    }
    return jsonMeasure(iso, parents, depth, ok);
  }
  if (type == ValueType::Promise)
    return 2;
  if (type == ValueType::String) {
    size_t length = 2;
    const StringView text = stringView(value);
    for (size_t i = 0; i < text.size; i++) {
      const unsigned char c = static_cast<unsigned char>(text.data[i]);
      length += c == '"' || c == '\\' || c == '\b' || c == '\f' ||
                        c == '\n' || c == '\r' || c == '\t'
                    ? 2
                    : (c < 0x20U ? 6 : 1);
    }
    return length;
  }
  if (type == ValueType::Number) {
    const double numeric = toNumber(value);
    return std::isfinite(numeric) ? format(value, nullptr, 0) : 4;
  }
  if (type == ValueType::Boolean)
    return detail::payloadOf(value) != 0 ? 4 : 5;
  if (type == ValueType::Null || type == ValueType::Undefined ||
      type == ValueType::Function)
    return 4;
  if (type == ValueType::Array || type == ValueType::Set) {
    const uint32_t identity = static_cast<uint32_t>(detail::payloadOf(value));
    for (size_t i = 0; i < depth; i++) {
      if (parents[i] == identity) {
        ok = false;
        return 0;
      }
    }
    parents[depth] = identity;
    size_t length = 2;
    for (size_t i = 0; i < arrayLength(value); i++) {
      if (i > 0)
        length++;
      length += jsonMeasure(arrayAt(value, i), parents, depth + 1U, ok);
    }
    return length;
  }
  if (detail::tagOf(value) == detail::Tag::BuiltinObject)
    return 2;
  const uint32_t identity = static_cast<uint32_t>(detail::payloadOf(value));
  for (size_t i = 0; i < depth; i++) {
    if (parents[i] == identity) {
      ok = false;
      return 0;
    }
  }
  parents[depth] = identity;
  size_t length = 2;
  const size_t count = objectPropertyCount(value);
  for (size_t i = 0; i < count; i++) {
    Value key;
    Value item;
    objectPropertyAt(value, i, &key, &item);
    if (i > 0)
      length++;
    length += jsonMeasure(key, parents, depth + 1U, ok) + 1U;
    length += jsonMeasure(item, parents, depth + 1U, ok);
  }
  return length;
}

inline char *Runtime::jsonWrite(Value value, char *out, uint32_t *parents,
                                size_t depth, bool &ok) {
  auto writeText = [&out](const char *text, size_t length) {
    std::memcpy(out, text, length);
    out += length;
  };
  const ValueType type = typeOf(value);
  if (detail::tagOf(value) == detail::Tag::Date) {
    const uint32_t offset = static_cast<uint32_t>(detail::payloadOf(value));
    Value iso = isoDate(
        static_cast<const detail::DatePayload *>(payload(offset))->milliseconds);
    return jsonWrite(iso, out, parents, depth, ok);
  }
  if (type == ValueType::Promise) {
    writeText("{}", 2);
    return out;
  }
  if (type == ValueType::String) {
    *out++ = '"';
    const char hex[] = "0123456789abcdef";
    const StringView text = stringView(value);
    for (size_t i = 0; i < text.size; i++) {
      const unsigned char c = static_cast<unsigned char>(text.data[i]);
      switch (c) {
      case '"':
        writeText("\\\"", 2);
        break;
      case '\\':
        writeText("\\\\", 2);
        break;
      case '\b':
        writeText("\\b", 2);
        break;
      case '\f':
        writeText("\\f", 2);
        break;
      case '\n':
        writeText("\\n", 2);
        break;
      case '\r':
        writeText("\\r", 2);
        break;
      case '\t':
        writeText("\\t", 2);
        break;
      default:
        if (c < 0x20U) {
          *out++ = '\\';
          *out++ = 'u';
          *out++ = '0';
          *out++ = '0';
          *out++ = hex[c >> 4U];
          *out++ = hex[c & 15U];
        } else {
          *out++ = static_cast<char>(c);
        }
        break;
      }
    }
    *out++ = '"';
    return out;
  }
  if (type == ValueType::Number) {
    const double numeric = toNumber(value);
    if (!std::isfinite(numeric)) {
      writeText("null", 4);
    } else {
      char numberText[40];
      const size_t length = format(value, numberText, sizeof(numberText));
      writeText(numberText, length);
    }
    return out;
  }
  if (type == ValueType::Boolean) {
    if (detail::payloadOf(value) != 0)
      writeText("true", 4);
    else
      writeText("false", 5);
    return out;
  }
  if (type == ValueType::Null || type == ValueType::Undefined ||
      type == ValueType::Function) {
    writeText("null", 4);
    return out;
  }
  if (type == ValueType::Array || type == ValueType::Set) {
    parents[depth] = static_cast<uint32_t>(detail::payloadOf(value));
    *out++ = '[';
    for (size_t i = 0; i < arrayLength(value); i++) {
      if (i > 0)
        *out++ = ',';
      out = jsonWrite(arrayAt(value, i), out, parents, depth + 1U, ok);
    }
    *out++ = ']';
    return out;
  }
  *out++ = '{';
  if (detail::tagOf(value) == detail::Tag::Object) {
    const uint32_t identity = static_cast<uint32_t>(detail::payloadOf(value));
    parents[depth] = identity;
    const size_t count = objectPropertyCount(value);
    for (size_t i = 0; i < count; i++) {
      Value key;
      Value item;
      objectPropertyAt(value, i, &key, &item);
      if (i > 0)
        *out++ = ',';
      out = jsonWrite(key, out, parents, depth + 1U, ok);
      *out++ = ':';
      out = jsonWrite(item, out, parents, depth + 1U, ok);
    }
  }
  *out++ = '}';
  return out;
}

inline Value Runtime::jsonStringify(Value value) {
  uint32_t parents[16] = {};
  bool ok = true;
  const size_t length = jsonMeasure(value, parents, 0, ok);
  if (!ok)
    return fail(ErrorCode{153});
  char *output = nullptr;
  Value result = makeStringBuffer(length, &output);
  if (output == nullptr)
    return undefined();
  std::memset(parents, 0, sizeof(parents));
  char *end = jsonWrite(value, output, parents, 0, ok);
  if (!ok || static_cast<size_t>(end - output) != length)
    return fail(ErrorCode{154});
  return result;
}

inline int64_t Runtime::nowMilliseconds() const {
  return options_.nowMilliseconds != nullptr
             ? options_.nowMilliseconds(options_.nowUserData)
             : 0;
}

inline int64_t Runtime::daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2U;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned monthPrime =
      static_cast<unsigned>(static_cast<int>(month) + (month > 2U ? -3 : 9));
  const unsigned dayOfYear =
      (153U * monthPrime + 2U) / 5U + day - 1U;
  const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U -
                            yearOfEra / 100U + dayOfYear;
  return static_cast<int64_t>(era) * 146097 +
         static_cast<int64_t>(dayOfEra) - 719468;
}

inline void Runtime::civilFromDays(int64_t days, int &year, unsigned &month,
                                   unsigned &day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra =
      (dayOfEra - dayOfEra / 1460U + dayOfEra / 36524U -
       dayOfEra / 146096U) /
      365U;
  year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const unsigned dayOfYear =
      dayOfEra - (365U * yearOfEra + yearOfEra / 4U - yearOfEra / 100U);
  const unsigned monthPrime = (5U * dayOfYear + 2U) / 153U;
  day = dayOfYear - (153U * monthPrime + 2U) / 5U + 1U;
  month = static_cast<unsigned>(static_cast<int>(monthPrime) +
                                (monthPrime < 10U ? 3 : -9));
  year += month <= 2U;
}

inline Value Runtime::isoDate(double milliseconds) {
  if (!std::isfinite(milliseconds) ||
      std::fabs(milliseconds) > 8640000000000000.0)
    return fail(ErrorCode{155});
  int64_t total = static_cast<int64_t>(milliseconds);
  int64_t days = total / INT64_C(86400000);
  int64_t dayMilliseconds = total % INT64_C(86400000);
  if (dayMilliseconds < 0) {
    dayMilliseconds += INT64_C(86400000);
    days--;
  }
  int year = 0;
  unsigned month = 0, day = 0;
  civilFromDays(days, year, month, day);
  const int hour = static_cast<int>(dayMilliseconds / 3600000);
  const int minute = static_cast<int>((dayMilliseconds / 60000) % 60);
  const int second = static_cast<int>((dayMilliseconds / 1000) % 60);
  const int ms = static_cast<int>(dayMilliseconds % 1000);
  char text[40];
  const int length = year >= 0 && year <= 9999
                         ? std::snprintf(
                               text, sizeof(text),
                               "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", year,
                               month, day, hour, minute, second, ms)
                         : std::snprintf(
                               text, sizeof(text),
                               "%+07d-%02u-%02uT%02d:%02d:%02d.%03dZ", year,
                               month, day, hour, minute, second, ms);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(text))
    return fail(ErrorCode{156});
  return makeString(text, static_cast<size_t>(length));
}

} // namespace pinkjs

#endif
