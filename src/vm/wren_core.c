#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "wren_common.h"
#include "wren_core.h"
#include "wren_math.h"
#include "wren_primitive.h"
#include "wren_value.h"
#include "wren_vm.h"

#include "wren_core.wren.inc"

DEF_PRIMITIVE(bool_not)
{
  RETURN_BOOL(!AS_BOOL(args[0]));
}

DEF_PRIMITIVE(bool_toString)
{
  if (AS_BOOL(args[0]))
  {
    RETURN_VAL(CONST_STRING(vm, "true"));
  }
  else
  {
    RETURN_VAL(CONST_STRING(vm, "false"));
  }
}

DEF_PRIMITIVE(class_name)
{
  RETURN_OBJ(AS_CLASS(args[0])->name);
}

DEF_PRIMITIVE(class_supertype)
{
  ObjClass* classObj = AS_CLASS(args[0]);

  // Object has no superclass.
  if (classObj->superclass == NULL) RETURN_NULL;

  RETURN_OBJ(classObj->superclass);
}

DEF_PRIMITIVE(class_toString)
{
  RETURN_OBJ(AS_CLASS(args[0])->name);
}

DEF_PRIMITIVE(class_attributes)
{
  RETURN_VAL(AS_CLASS(args[0])->attributes);
}

// REVATE EXTENSION (§7a): expose the isAttachment flag and the
// attachmentTargets[] name array as introspection primitives so tests
// (and 7b's attach() machinery) can identify attachment classes.
DEF_PRIMITIVE(class_isAttachment)
{
  RETURN_BOOL(AS_CLASS(args[0])->isAttachment);
}

DEF_PRIMITIVE(class_attachmentTargets)
{
  ObjClass* classObj = AS_CLASS(args[0]);
  ObjList* list = wrenNewList(vm, 0);
  wrenPushRoot(vm, (Obj*)list);
  for (int i = 0; i < classObj->numAttachmentTargets; i++)
  {
    wrenValueBufferWrite(vm, &list->elements,
                         OBJ_VAL(classObj->attachmentTargets[i]));
  }
  wrenPopRoot(vm);
  RETURN_OBJ(list);
}

DEF_PRIMITIVE(fiber_new)
{
  if (!validateFn(vm, args[1], "Argument")) return false;

  ObjClosure* closure = AS_CLOSURE(args[1]);
  if (closure->fn->arity > 1)
  {
    RETURN_ERROR("Function cannot take more than one parameter.");
  }
  
  RETURN_OBJ(wrenNewFiber(vm, closure));
}

DEF_PRIMITIVE(fiber_abort)
{
  vm->fiber->error = args[1];

  // If the error is explicitly null, it's not really an abort.
  return IS_NULL(args[1]);
}

// Transfer execution to [fiber] coming from the current fiber whose stack has
// [args].
//
// [isCall] is true if [fiber] is being called and not transferred.
//
// [hasValue] is true if a value in [args] is being passed to the new fiber.
// Otherwise, `null` is implicitly being passed.
static bool runFiber(WrenVM* vm, ObjFiber* fiber, Value* args, bool isCall,
                     bool hasValue, const char* verb)
{

  if (wrenHasError(fiber))
  {
    RETURN_ERROR_FMT("Cannot $ an aborted fiber.", verb);
  }

  if (isCall)
  {
    // You can't call a called fiber, but you can transfer directly to it,
    // which is why this check is gated on `isCall`. This way, after resuming a
    // suspended fiber, it will run and then return to the fiber that called it
    // and so on.
    if (fiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

    if (fiber->state == FIBER_ROOT) RETURN_ERROR("Cannot call root fiber.");
    
    // Remember who ran it.
    fiber->caller = vm->fiber;
  }

  if (fiber->numFrames == 0)
  {
    RETURN_ERROR_FMT("Cannot $ a finished fiber.", verb);
  }

  // When the calling fiber resumes, we'll store the result of the call in its
  // stack. If the call has two arguments (the fiber and the value), we only
  // need one slot for the result, so discard the other slot now.
  if (hasValue) vm->fiber->stackTop--;

  if (fiber->numFrames == 1 &&
      fiber->frames[0].ip == fiber->frames[0].closure->fn->code.data)
  {
    // The fiber is being started for the first time. If its function takes a
    // parameter, bind an argument to it.
    if (fiber->frames[0].closure->fn->arity == 1)
    {
      fiber->stackTop[0] = hasValue ? args[1] : NULL_VAL;
      fiber->stackTop++;
    }
  }
  else
  {
    // The fiber is being resumed, make yield() or transfer() return the result.
    fiber->stackTop[-1] = hasValue ? args[1] : NULL_VAL;
  }

  vm->fiber = fiber;
  return false;
}

DEF_PRIMITIVE(fiber_call)
{
  return runFiber(vm, AS_FIBER(args[0]), args, true, false, "call");
}

DEF_PRIMITIVE(fiber_call1)
{
  return runFiber(vm, AS_FIBER(args[0]), args, true, true, "call");
}

DEF_PRIMITIVE(fiber_current)
{
  RETURN_OBJ(vm->fiber);
}

DEF_PRIMITIVE(fiber_error)
{
  RETURN_VAL(AS_FIBER(args[0])->error);
}

DEF_PRIMITIVE(fiber_isDone)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);
  RETURN_BOOL(runFiber->numFrames == 0 || wrenHasError(runFiber));
}

DEF_PRIMITIVE(fiber_suspend)
{
  // Switching to a null fiber tells the interpreter to stop and exit.
  vm->fiber = NULL;
  vm->apiStack = NULL;
  return false;
}

DEF_PRIMITIVE(fiber_transfer)
{
  return runFiber(vm, AS_FIBER(args[0]), args, false, false, "transfer to");
}

DEF_PRIMITIVE(fiber_transfer1)
{
  return runFiber(vm, AS_FIBER(args[0]), args, false, true, "transfer to");
}

DEF_PRIMITIVE(fiber_transferError)
{
  runFiber(vm, AS_FIBER(args[0]), args, false, true, "transfer to");
  vm->fiber->error = args[1];
  return false;
}

DEF_PRIMITIVE(fiber_try)
{
  runFiber(vm, AS_FIBER(args[0]), args, true, false, "try");
  
  // If we're switching to a valid fiber to try, remember that we're trying it.
  if (!wrenHasError(vm->fiber)) vm->fiber->state = FIBER_TRY;
  return false;
}

DEF_PRIMITIVE(fiber_try1)
{
  runFiber(vm, AS_FIBER(args[0]), args, true, true, "try");
  
  // If we're switching to a valid fiber to try, remember that we're trying it.
  if (!wrenHasError(vm->fiber)) vm->fiber->state = FIBER_TRY;
  return false;
}

DEF_PRIMITIVE(fiber_yield)
{
  ObjFiber* current = vm->fiber;
  vm->fiber = current->caller;

  // Unhook this fiber from the one that called it.
  current->caller = NULL;
  current->state = FIBER_OTHER;

  if (vm->fiber != NULL)
  {
    // Make the caller's run method return null.
    vm->fiber->stackTop[-1] = NULL_VAL;
  }

  return false;
}

DEF_PRIMITIVE(fiber_yield1)
{
  ObjFiber* current = vm->fiber;
  vm->fiber = current->caller;

  // Unhook this fiber from the one that called it.
  current->caller = NULL;
  current->state = FIBER_OTHER;

  if (vm->fiber != NULL)
  {
    // Make the caller's run method return the argument passed to yield.
    vm->fiber->stackTop[-1] = args[1];

    // When the yielding fiber resumes, we'll store the result of the yield
    // call in its stack. Since Fiber.yield(value) has two arguments (the Fiber
    // class and the value) and we only need one slot for the result, discard
    // the other slot now.
    current->stackTop--;
  }

  return false;
}

DEF_PRIMITIVE(fn_new)
{
  if (!validateFn(vm, args[1], "Argument")) return false;

  // The block argument is already a function, so just return it.
  RETURN_VAL(args[1]);
}

DEF_PRIMITIVE(fn_arity)
{
  RETURN_NUM(AS_CLOSURE(args[0])->fn->arity);
}

static void call_fn(WrenVM* vm, Value* args, int numArgs)
{
  // +1 to include the function itself.
  wrenCallFunction(vm, vm->fiber, AS_CLOSURE(args[0]), numArgs + 1);
}

#define DEF_FN_CALL(numArgs)                                                   \
    DEF_PRIMITIVE(fn_call##numArgs)                                            \
    {                                                                          \
      call_fn(vm, args, numArgs);                                              \
      return false;                                                            \
    }

DEF_FN_CALL(0)
DEF_FN_CALL(1)
DEF_FN_CALL(2)
DEF_FN_CALL(3)
DEF_FN_CALL(4)
DEF_FN_CALL(5)
DEF_FN_CALL(6)
DEF_FN_CALL(7)
DEF_FN_CALL(8)
DEF_FN_CALL(9)
DEF_FN_CALL(10)
DEF_FN_CALL(11)
DEF_FN_CALL(12)
DEF_FN_CALL(13)
DEF_FN_CALL(14)
DEF_FN_CALL(15)
DEF_FN_CALL(16)

DEF_PRIMITIVE(fn_toString)
{
  RETURN_VAL(CONST_STRING(vm, "<fn>"));
}

// Creates a new list of size args[1], with all elements initialized to args[2].
DEF_PRIMITIVE(list_filled)
{
  if (!validateInt(vm, args[1], "Size")) return false;  
  if (AS_NUM(args[1]) < 0) RETURN_ERROR("Size cannot be negative.");
  
  uint32_t size = (uint32_t)AS_NUM(args[1]);
  ObjList* list = wrenNewList(vm, size);
  
  for (uint32_t i = 0; i < size; i++)
  {
    list->elements.data[i] = args[2];
  }
  
  RETURN_OBJ(list);
}

DEF_PRIMITIVE(list_new)
{
  RETURN_OBJ(wrenNewList(vm, 0));
}

DEF_PRIMITIVE(list_add)
{
  wrenValueBufferWrite(vm, &AS_LIST(args[0])->elements, args[1]);
  RETURN_VAL(args[1]);
}

// Adds an element to the list and then returns the list itself. This is called
// by the compiler when compiling list literals instead of using add() to
// minimize stack churn.
DEF_PRIMITIVE(list_addCore)
{
  wrenValueBufferWrite(vm, &AS_LIST(args[0])->elements, args[1]);
  
  // Return the list.
  RETURN_VAL(args[0]);
}

DEF_PRIMITIVE(list_clear)
{
  wrenValueBufferClear(vm, &AS_LIST(args[0])->elements);
  RETURN_NULL;
}

DEF_PRIMITIVE(list_count)
{
  RETURN_NUM(AS_LIST(args[0])->elements.count);
}

DEF_PRIMITIVE(list_insert)
{
  ObjList* list = AS_LIST(args[0]);

  // count + 1 here so you can "insert" at the very end.
  uint32_t index = validateIndex(vm, args[1], list->elements.count + 1,
                                 "Index");
  if (index == UINT32_MAX) return false;

  wrenListInsert(vm, list, args[2], index);
  RETURN_VAL(args[2]);
}

DEF_PRIMITIVE(list_iterate)
{
  ObjList* list = AS_LIST(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (list->elements.count == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args[1], "Iterator")) return false;

  // Stop if we're out of bounds.
  double index = AS_NUM(args[1]);
  if (index < 0 || index >= list->elements.count - 1) RETURN_FALSE;

  // Otherwise, move to the next index.
  RETURN_NUM(index + 1);
}

DEF_PRIMITIVE(list_iteratorValue)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t index = validateIndex(vm, args[1], list->elements.count, "Iterator");
  if (index == UINT32_MAX) return false;

  RETURN_VAL(list->elements.data[index]);
}

DEF_PRIMITIVE(list_removeAt)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t index = validateIndex(vm, args[1], list->elements.count, "Index");
  if (index == UINT32_MAX) return false;

  RETURN_VAL(wrenListRemoveAt(vm, list, index));
}

DEF_PRIMITIVE(list_removeValue) {
  ObjList* list = AS_LIST(args[0]);
  int index = wrenListIndexOf(vm, list, args[1]);
  if(index == -1) RETURN_NULL;
  RETURN_VAL(wrenListRemoveAt(vm, list, index));
}

DEF_PRIMITIVE(list_indexOf)
{
  ObjList* list = AS_LIST(args[0]);
  RETURN_NUM(wrenListIndexOf(vm, list, args[1]));
}

DEF_PRIMITIVE(list_swap)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t indexA = validateIndex(vm, args[1], list->elements.count, "Index 0");
  if (indexA == UINT32_MAX) return false;
  uint32_t indexB = validateIndex(vm, args[2], list->elements.count, "Index 1");
  if (indexB == UINT32_MAX) return false;

  Value a = list->elements.data[indexA];
  list->elements.data[indexA] = list->elements.data[indexB];
  list->elements.data[indexB] = a;

  RETURN_NULL;
}

DEF_PRIMITIVE(list_subscript)
{
  ObjList* list = AS_LIST(args[0]);

  if (IS_NUM(args[1]))
  {
    uint32_t index = validateIndex(vm, args[1], list->elements.count,
                                   "Subscript");
    if (index == UINT32_MAX) return false;

    RETURN_VAL(list->elements.data[index]);
  }

  if (!IS_RANGE(args[1]))
  {
    RETURN_ERROR("Subscript must be a number or a range.");
  }

  int step;
  uint32_t count = list->elements.count;
  uint32_t start = calculateRange(vm, AS_RANGE(args[1]), &count, &step);
  if (start == UINT32_MAX) return false;

  ObjList* result = wrenNewList(vm, count);
  for (uint32_t i = 0; i < count; i++)
  {
    result->elements.data[i] = list->elements.data[start + i * step];
  }

  RETURN_OBJ(result);
}

DEF_PRIMITIVE(list_subscriptSetter)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t index = validateIndex(vm, args[1], list->elements.count,
                                 "Subscript");
  if (index == UINT32_MAX) return false;

  list->elements.data[index] = args[2];
  RETURN_VAL(args[2]);
}

DEF_PRIMITIVE(map_new)
{
  RETURN_OBJ(wrenNewMap(vm));
}

DEF_PRIMITIVE(map_subscript)
{
  if (!validateKey(vm, args[1])) return false;

  ObjMap* map = AS_MAP(args[0]);
  Value value = wrenMapGet(map, args[1]);
  if (IS_UNDEFINED(value)) RETURN_NULL;

  RETURN_VAL(value);
}

DEF_PRIMITIVE(map_subscriptSetter)
{
  if (!validateKey(vm, args[1])) return false;

  wrenMapSet(vm, AS_MAP(args[0]), args[1], args[2]);
  RETURN_VAL(args[2]);
}

// Adds an entry to the map and then returns the map itself. This is called by
// the compiler when compiling map literals instead of using [_]=(_) to
// minimize stack churn.
DEF_PRIMITIVE(map_addCore)
{
  if (!validateKey(vm, args[1])) return false;
  
  wrenMapSet(vm, AS_MAP(args[0]), args[1], args[2]);
  
  // Return the map itself.
  RETURN_VAL(args[0]);
}

DEF_PRIMITIVE(map_clear)
{
  wrenMapClear(vm, AS_MAP(args[0]));
  RETURN_NULL;
}

DEF_PRIMITIVE(map_containsKey)
{
  if (!validateKey(vm, args[1])) return false;

  RETURN_BOOL(!IS_UNDEFINED(wrenMapGet(AS_MAP(args[0]), args[1])));
}

DEF_PRIMITIVE(map_count)
{
  RETURN_NUM(AS_MAP(args[0])->count);
}

DEF_PRIMITIVE(map_iterate)
{
  ObjMap* map = AS_MAP(args[0]);

  if (map->count == 0) RETURN_FALSE;

  // If we're starting the iteration, start at the first used entry.
  uint32_t index = 0;

  // Otherwise, start one past the last entry we stopped at.
  if (!IS_NULL(args[1]))
  {
    if (!validateInt(vm, args[1], "Iterator")) return false;

    if (AS_NUM(args[1]) < 0) RETURN_FALSE;
    index = (uint32_t)AS_NUM(args[1]);

    if (index >= map->capacity) RETURN_FALSE;

    // Advance the iterator.
    index++;
  }

  // Find a used entry, if any.
  for (; index < map->capacity; index++)
  {
    if (!IS_UNDEFINED(map->entries[index].key)) RETURN_NUM(index);
  }

  // If we get here, walked all of the entries.
  RETURN_FALSE;
}

DEF_PRIMITIVE(map_remove)
{
  if (!validateKey(vm, args[1])) return false;

  RETURN_VAL(wrenMapRemoveKey(vm, AS_MAP(args[0]), args[1]));
}

DEF_PRIMITIVE(map_keyIteratorValue)
{
  ObjMap* map = AS_MAP(args[0]);
  uint32_t index = validateIndex(vm, args[1], map->capacity, "Iterator");
  if (index == UINT32_MAX) return false;

  MapEntry* entry = &map->entries[index];
  if (IS_UNDEFINED(entry->key))
  {
    RETURN_ERROR("Invalid map iterator.");
  }

  RETURN_VAL(entry->key);
}

DEF_PRIMITIVE(map_valueIteratorValue)
{
  ObjMap* map = AS_MAP(args[0]);
  uint32_t index = validateIndex(vm, args[1], map->capacity, "Iterator");
  if (index == UINT32_MAX) return false;

  MapEntry* entry = &map->entries[index];
  if (IS_UNDEFINED(entry->key))
  {
    RETURN_ERROR("Invalid map iterator.");
  }

  RETURN_VAL(entry->value);
}

DEF_PRIMITIVE(null_not)
{
  RETURN_VAL(TRUE_VAL);
}

DEF_PRIMITIVE(null_toString)
{
  RETURN_VAL(CONST_STRING(vm, "null"));
}

DEF_PRIMITIVE(num_fromString)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[1]);

  // Corner case: Can't parse an empty string.
  if (string->length == 0) RETURN_NULL;

  errno = 0;
  char* end;
  double number = strtod(string->value, &end);

  // Skip past any trailing whitespace.
  while (*end != '\0' && isspace((unsigned char)*end)) end++;

  if (errno == ERANGE) RETURN_ERROR("Number literal is too large.");

  // We must have consumed the entire string. Otherwise, it contains non-number
  // characters and we can't parse it.
  if (end < string->value + string->length) RETURN_NULL;

  RETURN_NUM(number);
}

// Defines a primitive on Num that calls infix [op] and returns [type].
#define DEF_NUM_CONSTANT(name, value)                                          \
    DEF_PRIMITIVE(num_##name)                                                  \
    {                                                                          \
      RETURN_NUM(value);                                                       \
    }

DEF_NUM_CONSTANT(infinity, INFINITY)
DEF_NUM_CONSTANT(nan,      WREN_DOUBLE_NAN)
DEF_NUM_CONSTANT(pi,       3.14159265358979323846264338327950288)
DEF_NUM_CONSTANT(tau,      6.28318530717958647692528676655900577)

DEF_NUM_CONSTANT(largest,  DBL_MAX)
DEF_NUM_CONSTANT(smallest, DBL_MIN)

DEF_NUM_CONSTANT(maxSafeInteger, 9007199254740991.0)
DEF_NUM_CONSTANT(minSafeInteger, -9007199254740991.0)

// Defines a primitive on Num that calls infix [op] and returns [type].
#define DEF_NUM_INFIX(name, op, type)                                          \
    DEF_PRIMITIVE(num_##name)                                                  \
    {                                                                          \
      if (!validateNum(vm, args[1], "Right operand")) return false;            \
      RETURN_##type(AS_NUM(args[0]) op AS_NUM(args[1]));                       \
    }

DEF_NUM_INFIX(minus,    -,  NUM)
DEF_NUM_INFIX(plus,     +,  NUM)
DEF_NUM_INFIX(multiply, *,  NUM)
DEF_NUM_INFIX(divide,   /,  NUM)
DEF_NUM_INFIX(lt,       <,  BOOL)
DEF_NUM_INFIX(gt,       >,  BOOL)
DEF_NUM_INFIX(lte,      <=, BOOL)
DEF_NUM_INFIX(gte,      >=, BOOL)

// Defines a primitive on Num that call infix bitwise [op].
#define DEF_NUM_BITWISE(name, op)                                              \
    DEF_PRIMITIVE(num_bitwise##name)                                           \
    {                                                                          \
      if (!validateNum(vm, args[1], "Right operand")) return false;            \
      uint32_t left = (uint32_t)AS_NUM(args[0]);                               \
      uint32_t right = (uint32_t)AS_NUM(args[1]);                              \
      RETURN_NUM(left op right);                                               \
    }

DEF_NUM_BITWISE(And,        &)
DEF_NUM_BITWISE(Or,         |)
DEF_NUM_BITWISE(Xor,        ^)
DEF_NUM_BITWISE(LeftShift,  <<)
DEF_NUM_BITWISE(RightShift, >>)

// Defines a primitive method on Num that returns the result of [fn].
#define DEF_NUM_FN(name, fn)                                                   \
    DEF_PRIMITIVE(num_##name)                                                  \
    {                                                                          \
      RETURN_NUM(fn(AS_NUM(args[0])));                                         \
    }

DEF_NUM_FN(abs,     fabs)
DEF_NUM_FN(acos,    acos)
DEF_NUM_FN(asin,    asin)
DEF_NUM_FN(atan,    atan)
DEF_NUM_FN(cbrt,    cbrt)
DEF_NUM_FN(ceil,    ceil)
DEF_NUM_FN(cos,     cos)
DEF_NUM_FN(floor,   floor)
DEF_NUM_FN(negate,  -)
DEF_NUM_FN(round,   round)
DEF_NUM_FN(sin,     sin)
DEF_NUM_FN(sqrt,    sqrt)
DEF_NUM_FN(tan,     tan)
DEF_NUM_FN(log,     log)
DEF_NUM_FN(log2,    log2)
DEF_NUM_FN(exp,     exp)

DEF_PRIMITIVE(num_mod)
{
  if (!validateNum(vm, args[1], "Right operand")) return false;
  RETURN_NUM(fmod(AS_NUM(args[0]), AS_NUM(args[1])));
}

DEF_PRIMITIVE(num_eqeq)
{
  if (!IS_NUM(args[1])) RETURN_FALSE;
  RETURN_BOOL(AS_NUM(args[0]) == AS_NUM(args[1]));
}

DEF_PRIMITIVE(num_bangeq)
{
  if (!IS_NUM(args[1])) RETURN_TRUE;
  RETURN_BOOL(AS_NUM(args[0]) != AS_NUM(args[1]));
}

DEF_PRIMITIVE(num_bitwiseNot)
{
  // Bitwise operators always work on 32-bit unsigned ints.
  RETURN_NUM(~(uint32_t)AS_NUM(args[0]));
}

DEF_PRIMITIVE(num_dotDot)
{
  if (!validateNum(vm, args[1], "Right hand side of range")) return false;

  double from = AS_NUM(args[0]);
  double to = AS_NUM(args[1]);
  RETURN_VAL(wrenNewRange(vm, from, to, true));
}

DEF_PRIMITIVE(num_dotDotDot)
{
  if (!validateNum(vm, args[1], "Right hand side of range")) return false;

  double from = AS_NUM(args[0]);
  double to = AS_NUM(args[1]);
  RETURN_VAL(wrenNewRange(vm, from, to, false));
}

DEF_PRIMITIVE(num_atan2)
{
  if (!validateNum(vm, args[1], "x value")) return false;

  RETURN_NUM(atan2(AS_NUM(args[0]), AS_NUM(args[1])));
}

DEF_PRIMITIVE(num_min)
{
  if (!validateNum(vm, args[1], "Other value")) return false;

  double value = AS_NUM(args[0]);
  double other = AS_NUM(args[1]);
  RETURN_NUM(value <= other ? value : other);
}

DEF_PRIMITIVE(num_max)
{
  if (!validateNum(vm, args[1], "Other value")) return false;

  double value = AS_NUM(args[0]);
  double other = AS_NUM(args[1]);
  RETURN_NUM(value > other ? value : other);
}

DEF_PRIMITIVE(num_clamp)
{
  if (!validateNum(vm, args[1], "Min value")) return false;
  if (!validateNum(vm, args[2], "Max value")) return false;

  double value = AS_NUM(args[0]);
  double min = AS_NUM(args[1]);
  double max = AS_NUM(args[2]);
  double result = (value < min) ? min : ((value > max) ? max : value);
  RETURN_NUM(result);
}

DEF_PRIMITIVE(num_pow)
{
  if (!validateNum(vm, args[1], "Power value")) return false;

  RETURN_NUM(pow(AS_NUM(args[0]), AS_NUM(args[1])));
}

DEF_PRIMITIVE(num_fraction)
{
  double unused;
  RETURN_NUM(modf(AS_NUM(args[0]) , &unused));
}

DEF_PRIMITIVE(num_isInfinity)
{
  RETURN_BOOL(isinf(AS_NUM(args[0])));
}

DEF_PRIMITIVE(num_isInteger)
{
  double value = AS_NUM(args[0]);
  if (isnan(value) || isinf(value)) RETURN_FALSE;
  RETURN_BOOL(trunc(value) == value);
}

DEF_PRIMITIVE(num_isNan)
{
  RETURN_BOOL(isnan(AS_NUM(args[0])));
}

DEF_PRIMITIVE(num_sign)
{
  double value = AS_NUM(args[0]);
  if (value > 0)
  {
    RETURN_NUM(1);
  }
  else if (value < 0)
  {
    RETURN_NUM(-1);
  }
  else
  {
    RETURN_NUM(0);
  }
}

DEF_PRIMITIVE(num_toString)
{
  RETURN_VAL(wrenNumToString(vm, AS_NUM(args[0])));
}

DEF_PRIMITIVE(num_truncate)
{
  double integer;
  modf(AS_NUM(args[0]) , &integer);
  RETURN_NUM(integer);
}

DEF_PRIMITIVE(object_same)
{
  RETURN_BOOL(wrenValuesEqual(args[1], args[2]));
}

DEF_PRIMITIVE(object_not)
{
  RETURN_VAL(FALSE_VAL);
}

DEF_PRIMITIVE(object_eqeq)
{
  RETURN_BOOL(wrenValuesEqual(args[0], args[1]));
}

DEF_PRIMITIVE(object_bangeq)
{
  RETURN_BOOL(!wrenValuesEqual(args[0], args[1]));
}

DEF_PRIMITIVE(object_is)
{
  if (!IS_CLASS(args[1]))
  {
    RETURN_ERROR("Right operand must be a class.");
  }

  ObjClass *classObj = wrenGetClass(vm, args[0]);
  ObjClass *baseClassObj = AS_CLASS(args[1]);

  // Walk the superclass chain looking for the class.
  do
  {
    if (baseClassObj == classObj) RETURN_BOOL(true);

    classObj = classObj->superclass;
  }
  while (classObj != NULL);

  RETURN_BOOL(false);
}

DEF_PRIMITIVE(object_toString)
{
  Obj* obj = AS_OBJ(args[0]);
  Value name = OBJ_VAL(obj->classObj->name);
  RETURN_VAL(wrenStringFormat(vm, "instance of @", name));
}

DEF_PRIMITIVE(object_type)
{
  RETURN_OBJ(wrenGetClass(vm, args[0]));
}

// REVATE EXTENSION: hasMixin(_) — checks if the receiver's class (or any
// ancestor) was bound with the given mixin.
DEF_PRIMITIVE(object_hasMixin)
{
  if (!IS_CLASS(args[1]))
  {
    RETURN_ERROR("Argument must be a class (mixin).");
  }

  ObjClass* target = AS_CLASS(args[1]);
  ObjClass* classObj = wrenGetClass(vm, args[0]);

  // Walk the class hierarchy.
  while (classObj != NULL)
  {
    for (int i = 0; i < classObj->numMixins; i++)
    {
      if (classObj->mixins[i] == target) RETURN_BOOL(true);
    }
    classObj = classObj->superclass;
  }

  RETURN_BOOL(false);
}

// ───────────────────────────────────────────────────────────────────────
// REVATE EXTENSION (§7b): runtime attach / detach machinery for the
// `attachment X targets Y { ... }` declaration shipped in §7a.
//
// All of the user-facing operations — `host.attach(att)`,
// `host.detach(klass)`, `host.detach(att)`, `host.detachAll(klass)`,
// `host.hasAttachment(klass)`, `host.attachments(klass)`,
// `host.attachments`, and the self-detach sugar `att.detach()` from
// inside an attachment method — are primitives on `Object`.  Putting
// them on Object means any class instance (including foreign-class
// proxies and attachment instances themselves) can host attachments
// without each having to opt in.
//
// Per-instance state lives in `ObjInstance.attachments` (a Value,
// lazy-allocated to OBJ_VAL(ObjList*) on first attach).  The attachment
// instance's own slot-0 field stores its current host (the implicit
// `host` field synthesised by §7a's attachmentDefinition).  Together
// these two slots plus the per-class attachmentTargets[] name array
// form the complete runtime state.
// ───────────────────────────────────────────────────────────────────────

// Returns true if `classObj` (or any class in its superclass chain) is
// named `name`, OR has a mixin in its `mixins[]` array named `name`.
//
// 7b's `targets` matching is by NAME, not by ObjClass identity: a
// targets clause like `targets Health` matches any class that mixes in
// a class called "Health", regardless of module of origin.  This is
// what makes Loot-targets-Health discoverable from Goblin without
// Goblin or Loot ever importing each other.
static bool classOrMixinNameMatches(ObjClass* classObj, ObjString* name)
{
  while (classObj != NULL)
  {
    // Direct class-name match.
    if (classObj->name != NULL &&
        classObj->name->length == name->length &&
        memcmp(classObj->name->value, name->value,
               (size_t)name->length) == 0)
    {
      return true;
    }

    // Mixin-name match: `targets Health` should accept any class that
    // mixes in Health.
    for (int i = 0; i < classObj->numMixins; i++)
    {
      ObjClass* m = classObj->mixins[i];
      if (m != NULL && m->name != NULL &&
          m->name->length == name->length &&
          memcmp(m->name->value, name->value,
                 (size_t)name->length) == 0)
      {
        return true;
      }
    }

    classObj = classObj->superclass;
  }
  return false;
}

// Returns true if `att`'s `attachmentTargets[]` accepts `hostClass`.
// Wildcard `"*"` matches any host class.
static bool attachmentAccepts(ObjClass* attachmentClass, ObjClass* hostClass)
{
  for (int i = 0; i < attachmentClass->numAttachmentTargets; i++)
  {
    ObjString* target = attachmentClass->attachmentTargets[i];
    if (target->length == 1 && target->value[0] == '*') return true;
    if (classOrMixinNameMatches(hostClass, target)) return true;
  }
  return false;
}

// Look up the symbol for a bracketed VM-internal trampoline (e.g.
// `<onAttached>(_)`) and pull the matching method off `classObj`.
// Returns NULL if not present.  Every class produced by
// `attachmentDefinition` carries the two §7b trampolines, but this
// guard keeps the dispatch path correct when a non-attachment somehow
// ends up here.
static ObjClosure* attachmentTrampoline(WrenVM* vm, ObjClass* classObj,
                                        const char* signature, int sigLen)
{
  int symbol = wrenSymbolTableFind(&vm->methodNames, signature, (size_t)sigLen);
  if (symbol < 0) return NULL;
  if (symbol >= classObj->methods.count) return NULL;
  Method* m = &classObj->methods.data[symbol];
  if (m->type != METHOD_BLOCK) return NULL;
  return (ObjClosure*)m->as.closure;
}

// Common pre-dispatch validation for host.attach(att).
// Returns true on success (caller proceeds with bookkeeping + trampoline
// invocation).  On failure, sets fiber->error and returns false.
static bool validateAttachArgs(WrenVM* vm, Value hostVal, Value attVal,
                               ObjClass** outHostClass, ObjInstance** outAtt,
                               ObjClass** outAttClass)
{
  // (1) `att` must be an instance.  Instance vs Foreign: only ObjInstance
  // has the `host` field synthesised at slot 0, so foreign objects
  // cannot stand in for attachments.
  if (!IS_INSTANCE(attVal))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "attach(_) argument must be an attachment instance.");
    return false;
  }

  ObjInstance* att      = AS_INSTANCE(attVal);
  ObjClass*    attClass = att->obj.classObj;

  // (2) Its class must be marked as an attachment.
  if (!attClass->isAttachment)
  {
    vm->fiber->error = wrenStringFormat(vm,
        "attach(_) argument's class '@' is not an attachment "
        "(declared with `attachment ... targets ...`).",
        OBJ_VAL(attClass->name));
    return false;
  }

  // (3) The attachment must not currently be attached to anything.
  //     The implicit `host` field sits at slot 0; non-null means bound.
  if (attClass->numFields >= 1 && !IS_NULL(att->fields[0]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "Attachment '@' is already attached to a host. Detach it first.",
        OBJ_VAL(attClass->name));
    return false;
  }

  // (4) The host's class (or its mixin chain) must satisfy the
  //     attachment's `targets` clause.
  ObjClass* hostClass = wrenGetClass(vm, hostVal);
  if (!attachmentAccepts(attClass, hostClass))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "Attachment '@' does not target host class '@'.",
        OBJ_VAL(attClass->name), OBJ_VAL(hostClass->name));
    return false;
  }

  *outHostClass = hostClass;
  *outAtt       = att;
  *outAttClass  = attClass;
  return true;
}

// Append `att` to the host instance's per-instance attachment list,
// lazily allocating the list on first use.
//
// Lazy-allocation strategy keeps the common "this instance never
// attaches anything" case zero-cost: ObjInstance.attachments stays
// NULL_VAL forever and no ObjList is created.
static void appendToAttachmentList(WrenVM* vm, ObjInstance* host, Value attVal)
{
  if (IS_NULL(host->attachments))
  {
    // Pin the about-to-be-appended value as a GC root in case the
    // ObjList allocation triggers a collection.
    wrenPushRoot(vm, AS_OBJ(attVal));
    ObjList* list = wrenNewList(vm, 0);
    host->attachments = OBJ_VAL(list);
    wrenPopRoot(vm);
  }
  ObjList* list = AS_LIST(host->attachments);
  wrenValueBufferWrite(vm, &list->elements, attVal);
}

// host.attach(att) — see comments at the top of the §7b block.
DEF_PRIMITIVE(object_attach)
{
  ObjClass*    hostClass;
  ObjInstance* att;
  ObjClass*    attClass;
  if (!validateAttachArgs(vm, args[0], args[1],
                          &hostClass, &att, &attClass))
  {
    return false;
  }

  // §7b: attach() is only legal on ObjInstance hosts.  A foreign-class
  // host would have no `attachments` slot to store the list in (the
  // ObjForeign struct uses its tail bytes for foreign data, not Wren
  // Values).
  if (!IS_INSTANCE(args[0]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "attach(_) receiver must be a class instance, not a foreign object.");
    return false;
  }
  ObjInstance* host = AS_INSTANCE(args[0]);

  // Write the host into the attachment's slot-0 `host` field.  The
  // synthesised setter would do the same thing but the direct write
  // skips an unnecessary frame.
  att->fields[0] = args[0];

  // Append to the host's per-instance list (lazy-allocated).
  appendToAttachmentList(vm, host, args[1]);

  // Dispatch the `<onAttached>(_)` trampoline on the attachment.  The
  // trampoline calls user-visible `onAttached(host)` and returns
  // `this` so the outer primitive's caller sees `att` as the value of
  // `host.attach(att)`.
  ObjClosure* trampoline =
      attachmentTrampoline(vm, attClass, "<onAttached>(_)", 15);

  // Every attachment class is synthesised with this trampoline in
  // attachmentDefinition; missing it would indicate a bytecode-build
  // bug, so abort loudly rather than silently returning att.
  if (trampoline == NULL)
  {
    vm->fiber->error = wrenStringFormat(vm,
        "Internal: attachment '@' has no <onAttached>(_) trampoline.",
        OBJ_VAL(attClass->name));
    return false;
  }

  // Re-layout the outer primitive's stack as the trampoline's call
  // frame so the trampoline's CODE_RETURN drops its result (`att`)
  // directly into the outer caller's expected result slot:
  //
  //   Outer stack before:  [host, att]              (stackTop = +2)
  //   Inner frame wants:   [att,  host]             (stackStart = args[0])
  //
  //   args[0] := att          — outer's return slot now points at att
  //   args[1] := host         — overwrite the dropped att with host
  //
  // After inner CODE_RETURN, stackStart[0] = trampoline's returned
  // `this` (== att); stackTop = stackStart + 1.  That leaves the
  // outer caller with exactly one slot containing `att`, which is the
  // documented chained-result shape of `host.attach(att)`.
  args[0] = args[1];      // args[0] = att
  args[1] = OBJ_VAL(host); // args[1] = host

  // Push the trampoline frame.  numArgs = 2 (this + host).  This sets
  // the inner frame's stackStart to args[0], so the trampoline's
  // implicit `this` is att and its single argument is host.
  wrenCallFunction(vm, vm->fiber, trampoline, 2);
  return false;
}

// Walk a host's attachment list looking for the most-recently-attached
// instance of `klass`.  Returns the array index in `list->elements`,
// or -1 if no match.  LIFO order — the design doc explicitly mandates
// this for both `detach(Klass)` (single) and `detachAll(Klass)`.
static int findLastAttachmentOfClass(ObjList* list, ObjClass* klass)
{
  for (int i = list->elements.count - 1; i >= 0; i--)
  {
    Value v = list->elements.data[i];
    if (!IS_INSTANCE(v)) continue;
    if (AS_INSTANCE(v)->obj.classObj == klass) return i;
  }
  return -1;
}

// Same but matching a specific attachment instance by identity.
static int findAttachmentInstance(ObjList* list, ObjInstance* att)
{
  for (int i = list->elements.count - 1; i >= 0; i--)
  {
    Value v = list->elements.data[i];
    if (IS_INSTANCE(v) && AS_INSTANCE(v) == att) return i;
  }
  return -1;
}

// Remove the i-th element from `list->elements` in O(n).  Used by the
// detach machinery.  We keep the relative order of remaining elements
// so introspection's "in attachment order" guarantee is preserved.
static void removeListElementAt(ObjList* list, int index)
{
  for (int j = index; j < list->elements.count - 1; j++)
  {
    list->elements.data[j] = list->elements.data[j + 1];
  }
  list->elements.count--;
}

// Common detach finisher: invokes the `<onDetached>(_)` trampoline on
// `att` with the saved host, clears att's host slot, leaves att's
// position in `args[0]` so the primitive's caller sees the returned
// instance.  Caller must have already removed att from the host's list.
//
// Returns false from the primitive (control passes to the trampoline
// via the same stack-relayout trick described in `object_attach`).
static bool dispatchOnDetached(WrenVM* vm, Value* args,
                               ObjInstance* att, Value hostVal)
{
  ObjClass* attClass = att->obj.classObj;
  ObjClosure* trampoline =
      attachmentTrampoline(vm, attClass, "<onDetached>(_)", 15);

  // Clear the host slot AFTER the trampoline reads it would be ideal,
  // but the trampoline's user-overridable onDetached(host) takes host
  // as its parameter — it does not read the slot.  Clearing here means
  // `this.host` inside `onDetached` returns null, which the design
  // explicitly accepts ("After onDetached returns, this.host becomes
  // invalid").  Keeping the slot live throughout would require a
  // second post-trampoline primitive entry, which the current
  // primitive ABI cannot express.
  if (attClass->numFields >= 1)
  {
    att->fields[0] = NULL_VAL;
  }

  if (trampoline == NULL)
  {
    // No trampoline — return att directly (shouldn't happen for
    // attachment-declared classes but stays safe for non-attachments).
    args[0] = OBJ_VAL(att);
    return true;
  }

  // Re-layout outer stack as the trampoline's call frame.
  //   Outer:  [<irrelevant>, <irrelevant>]
  //   Inner:  [att, host]                  (numArgs = 2)
  args[0] = OBJ_VAL(att);
  args[1] = hostVal;

  wrenCallFunction(vm, vm->fiber, trampoline, 2);
  return false;
}

// host.detach(_) — the argument is either an attachment class or an
// attachment instance.  Class form: removes the most-recently-attached
// instance of that class (LIFO).  Instance form: removes that specific
// instance.  Returns the removed instance, or null if no match (class
// form) / a runtime error (instance form, not on this host).
DEF_PRIMITIVE(object_detach)
{
  if (!IS_INSTANCE(args[0]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "detach(_) receiver must be a class instance.");
    return false;
  }
  ObjInstance* host = AS_INSTANCE(args[0]);

  // Host has no list → nothing to detach.
  if (IS_NULL(host->attachments))
  {
    // For the class form, return null (no match isn't an error).
    // For the instance form, this IS an error (the caller passed a
    // specific instance that must be on this host).
    if (IS_CLASS(args[1]))
    {
      args[0] = NULL_VAL;
      return true;
    }
    if (IS_INSTANCE(args[1]))
    {
      vm->fiber->error = wrenStringFormat(vm,
          "Attachment is not attached to this host.");
      return false;
    }
    vm->fiber->error = wrenStringFormat(vm,
        "detach(_) argument must be an attachment class or instance.");
    return false;
  }

  ObjList* list = AS_LIST(host->attachments);
  int idx = -1;

  if (IS_CLASS(args[1]))
  {
    ObjClass* klass = AS_CLASS(args[1]);
    idx = findLastAttachmentOfClass(list, klass);
    if (idx < 0)
    {
      // No matching attachment — return null (not an error per design).
      args[0] = NULL_VAL;
      return true;
    }
  }
  else if (IS_INSTANCE(args[1]))
  {
    ObjInstance* att = AS_INSTANCE(args[1]);
    idx = findAttachmentInstance(list, att);
    if (idx < 0)
    {
      vm->fiber->error = wrenStringFormat(vm,
          "Attachment '@' is not attached to this host.",
          OBJ_VAL(att->obj.classObj->name));
      return false;
    }
  }
  else
  {
    vm->fiber->error = wrenStringFormat(vm,
        "detach(_) argument must be an attachment class or instance.");
    return false;
  }

  ObjInstance* att = AS_INSTANCE(list->elements.data[idx]);
  Value hostVal    = OBJ_VAL(host);

  // Remove from the list BEFORE the trampoline runs.  The design
  // wants `onDetached(host)` to fire with the host still readable as
  // a parameter; list membership is irrelevant to that.  Removing
  // first also prevents the trampoline (which could re-entrantly
  // call `host.attach(...)` or `host.detachAll(...)`) from seeing
  // a half-detached intermediate state.
  removeListElementAt(list, idx);

  return dispatchOnDetached(vm, args, att, hostVal);
}

// host.detachAll(_) — argument is an attachment class.  Removes every
// matching attachment in LIFO order and returns the number of
// attachments removed.
//
// A C primitive owns exactly one frame transition per invocation, so
// detachAll cannot fire the user-overridable `onDetached(host)` hook
// per-attachment from this primitive.  §7b spec calls this out
// explicitly: `detachAll(Klass)` clears the host slot of every
// matching attachment and removes them from the list, but does NOT
// invoke `onDetached(host)` on any of them.  Callers who need
// per-attachment lifecycle hooks should loop `host.detach(Klass)`
// instead — that path drives the trampoline once per call.
DEF_PRIMITIVE(object_detachAll)
{
  if (!IS_INSTANCE(args[0]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "detachAll(_) receiver must be a class instance.");
    return false;
  }
  if (!IS_CLASS(args[1]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "detachAll(_) argument must be an attachment class.");
    return false;
  }

  ObjInstance* host  = AS_INSTANCE(args[0]);
  ObjClass*    klass = AS_CLASS(args[1]);

  if (IS_NULL(host->attachments))
  {
    RETURN_NUM(0);
  }

  ObjList* list = AS_LIST(host->attachments);

  // Pass 1: count matches and clear the host slot on every match so
  // their `this.host` reads null on (potentially-foreign-driven) tick
  // closures that survive the detach.  This part runs entirely in C.
  int count = 0;
  for (int i = list->elements.count - 1; i >= 0; i--)
  {
    Value v = list->elements.data[i];
    if (!IS_INSTANCE(v)) continue;
    ObjInstance* att = AS_INSTANCE(v);
    if (att->obj.classObj != klass) continue;
    if (att->obj.classObj->numFields >= 1)
    {
      att->fields[0] = NULL_VAL;
    }
    count++;
  }

  // Pass 2: remove matched entries from the list in LIFO order.  Note
  // that the user-overridable `onDetached(host)` hook is NOT invoked
  // by detachAll — this is the documented trade-off for the
  // single-frame primitive ABI.  Callers who need lifecycle hooks
  // fired per-attachment should loop `host.detach(Klass)` instead.
  for (int i = list->elements.count - 1; i >= 0; i--)
  {
    Value v = list->elements.data[i];
    if (IS_INSTANCE(v) && AS_INSTANCE(v)->obj.classObj == klass)
    {
      removeListElementAt(list, i);
    }
  }

  RETURN_NUM(count);
}

// host.hasAttachment(klass) — true if any attached instance has class
// `klass` (exact match, no superclass / mixin walk).
DEF_PRIMITIVE(object_hasAttachment)
{
  if (!IS_CLASS(args[1]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "hasAttachment(_) argument must be an attachment class.");
    return false;
  }
  if (!IS_INSTANCE(args[0])) RETURN_BOOL(false);

  ObjInstance* host  = AS_INSTANCE(args[0]);
  ObjClass*    klass = AS_CLASS(args[1]);

  if (IS_NULL(host->attachments)) RETURN_BOOL(false);
  ObjList* list = AS_LIST(host->attachments);
  for (int i = 0; i < list->elements.count; i++)
  {
    Value v = list->elements.data[i];
    if (IS_INSTANCE(v) && AS_INSTANCE(v)->obj.classObj == klass)
    {
      RETURN_BOOL(true);
    }
  }
  RETURN_BOOL(false);
}

// host.attachments(klass) — list of every attached instance of class
// `klass`, in attachment order (oldest first).  Empty list if none.
DEF_PRIMITIVE(object_attachmentsOfClass)
{
  if (!IS_CLASS(args[1]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "attachments(_) argument must be an attachment class.");
    return false;
  }

  ObjList* out = wrenNewList(vm, 0);

  if (!IS_INSTANCE(args[0]) || IS_NULL(AS_INSTANCE(args[0])->attachments))
  {
    RETURN_OBJ(out);
  }

  // Pin the freshly-allocated output list against the buffer-write
  // allocations below.
  wrenPushRoot(vm, (Obj*)out);

  ObjInstance* host  = AS_INSTANCE(args[0]);
  ObjClass*    klass = AS_CLASS(args[1]);
  ObjList*     list  = AS_LIST(host->attachments);

  for (int i = 0; i < list->elements.count; i++)
  {
    Value v = list->elements.data[i];
    if (IS_INSTANCE(v) && AS_INSTANCE(v)->obj.classObj == klass)
    {
      wrenValueBufferWrite(vm, &out->elements, v);
    }
  }

  wrenPopRoot(vm);
  RETURN_OBJ(out);
}

// host.attachments — no-arg getter — returns every attachment in
// attachment order (oldest first), regardless of class.  Returns an
// empty list when no attachments are present (consistent with the
// per-class form).
DEF_PRIMITIVE(object_attachmentsAll)
{
  ObjList* out = wrenNewList(vm, 0);

  if (!IS_INSTANCE(args[0]) || IS_NULL(AS_INSTANCE(args[0])->attachments))
  {
    RETURN_OBJ(out);
  }

  wrenPushRoot(vm, (Obj*)out);

  ObjList* list = AS_LIST(AS_INSTANCE(args[0])->attachments);
  for (int i = 0; i < list->elements.count; i++)
  {
    wrenValueBufferWrite(vm, &out->elements, list->elements.data[i]);
  }

  wrenPopRoot(vm);
  RETURN_OBJ(out);
}

// att.detach() — zero-arg self-detach sugar.  Only valid on
// attachment instances whose `host` slot is currently non-null.
// Reaches the host through `att.fields[0]`, then performs the
// equivalent of `host.detach(att)`.
DEF_PRIMITIVE(object_detachSelf)
{
  if (!IS_INSTANCE(args[0]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "detach() (zero-arg) is only valid on attachment instances.");
    return false;
  }
  ObjInstance* att = AS_INSTANCE(args[0]);
  ObjClass* attClass = att->obj.classObj;

  if (!attClass->isAttachment)
  {
    vm->fiber->error = wrenStringFormat(vm,
        "detach() (zero-arg) is only valid on attachment instances; "
        "'@' is not an attachment.",
        OBJ_VAL(attClass->name));
    return false;
  }

  if (attClass->numFields < 1 || IS_NULL(att->fields[0]))
  {
    vm->fiber->error = wrenStringFormat(vm,
        "Attachment '@' is not attached to any host.",
        OBJ_VAL(attClass->name));
    return false;
  }

  Value hostVal = att->fields[0];

  // The host slot is set, but defensively guard against a host that
  // somehow lost its list (shouldn't happen, but graceful failure
  // beats segfault).
  if (!IS_INSTANCE(hostVal) || IS_NULL(AS_INSTANCE(hostVal)->attachments))
  {
    // Treat as already-detached state: clear the slot, return att.
    att->fields[0] = NULL_VAL;
    args[0] = OBJ_VAL(att);
    return true;
  }

  ObjInstance* host = AS_INSTANCE(hostVal);
  ObjList*     list = AS_LIST(host->attachments);

  int idx = findAttachmentInstance(list, att);
  if (idx < 0)
  {
    // Slot says host X but X's list doesn't contain us.  Recover.
    att->fields[0] = NULL_VAL;
    args[0] = OBJ_VAL(att);
    return true;
  }

  removeListElementAt(list, idx);
  return dispatchOnDetached(vm, args, att, hostVal);
}

DEF_PRIMITIVE(range_from)
{
  RETURN_NUM(AS_RANGE(args[0])->from);
}

DEF_PRIMITIVE(range_to)
{
  RETURN_NUM(AS_RANGE(args[0])->to);
}

DEF_PRIMITIVE(range_min)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(fmin(range->from, range->to));
}

DEF_PRIMITIVE(range_max)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(fmax(range->from, range->to));
}

DEF_PRIMITIVE(range_isInclusive)
{
  RETURN_BOOL(AS_RANGE(args[0])->isInclusive);
}

DEF_PRIMITIVE(range_iterate)
{
  ObjRange* range = AS_RANGE(args[0]);

  // Special case: empty range.
  if (range->from == range->to && !range->isInclusive) RETURN_FALSE;

  // Start the iteration.
  if (IS_NULL(args[1])) RETURN_NUM(range->from);

  if (!validateNum(vm, args[1], "Iterator")) return false;

  double iterator = AS_NUM(args[1]);

  // Iterate towards [to] from [from].
  if (range->from < range->to)
  {
    iterator++;
    if (iterator > range->to) RETURN_FALSE;
  }
  else
  {
    iterator--;
    if (iterator < range->to) RETURN_FALSE;
  }

  if (!range->isInclusive && iterator == range->to) RETURN_FALSE;

  RETURN_NUM(iterator);
}

DEF_PRIMITIVE(range_iteratorValue)
{
  // Assume the iterator is a number so that is the value of the range.
  RETURN_VAL(args[1]);
}

DEF_PRIMITIVE(range_toString)
{
  ObjRange* range = AS_RANGE(args[0]);

  Value from = wrenNumToString(vm, range->from);
  wrenPushRoot(vm, AS_OBJ(from));

  Value to = wrenNumToString(vm, range->to);
  wrenPushRoot(vm, AS_OBJ(to));

  Value result = wrenStringFormat(vm, "@$@", from,
                                  range->isInclusive ? ".." : "...", to);

  wrenPopRoot(vm);
  wrenPopRoot(vm);
  RETURN_VAL(result);
}

DEF_PRIMITIVE(string_fromCodePoint)
{
  if (!validateInt(vm, args[1], "Code point")) return false;

  int codePoint = (int)AS_NUM(args[1]);
  if (codePoint < 0)
  {
    RETURN_ERROR("Code point cannot be negative.");
  }
  else if (codePoint > 0x10ffff)
  {
    RETURN_ERROR("Code point cannot be greater than 0x10ffff.");
  }

  RETURN_VAL(wrenStringFromCodePoint(vm, codePoint));
}

DEF_PRIMITIVE(string_fromByte)
{
  if (!validateInt(vm, args[1], "Byte")) return false;
  int byte = (int) AS_NUM(args[1]);
  if (byte < 0)
  {
    RETURN_ERROR("Byte cannot be negative.");
  }
  else if (byte > 0xff)
  {
    RETURN_ERROR("Byte cannot be greater than 0xff.");
  }
  RETURN_VAL(wrenStringFromByte(vm, (uint8_t) byte));
}

DEF_PRIMITIVE(string_byteAt)
{
  ObjString* string = AS_STRING(args[0]);

  uint32_t index = validateIndex(vm, args[1], string->length, "Index");
  if (index == UINT32_MAX) return false;

  RETURN_NUM((uint8_t)string->value[index]);
}

DEF_PRIMITIVE(string_byteCount)
{
  RETURN_NUM(AS_STRING(args[0])->length);
}

DEF_PRIMITIVE(string_codePointAt)
{
  ObjString* string = AS_STRING(args[0]);

  uint32_t index = validateIndex(vm, args[1], string->length, "Index");
  if (index == UINT32_MAX) return false;

  // If we are in the middle of a UTF-8 sequence, indicate that.
  const uint8_t* bytes = (uint8_t*)string->value;
  if ((bytes[index] & 0xc0) == 0x80) RETURN_NUM(-1);

  // Decode the UTF-8 sequence.
  RETURN_NUM(wrenUtf8Decode((uint8_t*)string->value + index,
                            string->length - index));
}

DEF_PRIMITIVE(string_contains)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  RETURN_BOOL(wrenStringFind(string, search, 0) != UINT32_MAX);
}

DEF_PRIMITIVE(string_endsWith)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Edge case: If the search string is longer then return false right away.
  if (search->length > string->length) RETURN_FALSE;

  RETURN_BOOL(memcmp(string->value + string->length - search->length,
                     search->value, search->length) == 0);
}

DEF_PRIMITIVE(string_indexOf1)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  uint32_t index = wrenStringFind(string, search, 0);
  RETURN_NUM(index == UINT32_MAX ? -1 : (int)index);
}

DEF_PRIMITIVE(string_indexOf2)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);
  uint32_t start = validateIndex(vm, args[2], string->length, "Start");
  if (start == UINT32_MAX) return false;
  
  uint32_t index = wrenStringFind(string, search, start);
  RETURN_NUM(index == UINT32_MAX ? -1 : (int)index);
}

DEF_PRIMITIVE(string_iterate)
{
  ObjString* string = AS_STRING(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (string->length == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args[1], "Iterator")) return false;

  if (AS_NUM(args[1]) < 0) RETURN_FALSE;
  uint32_t index = (uint32_t)AS_NUM(args[1]);

  // Advance to the beginning of the next UTF-8 sequence.
  do
  {
    index++;
    if (index >= string->length) RETURN_FALSE;
  } while ((string->value[index] & 0xc0) == 0x80);

  RETURN_NUM(index);
}

DEF_PRIMITIVE(string_iterateByte)
{
  ObjString* string = AS_STRING(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (string->length == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args[1], "Iterator")) return false;

  if (AS_NUM(args[1]) < 0) RETURN_FALSE;
  uint32_t index = (uint32_t)AS_NUM(args[1]);

  // Advance to the next byte.
  index++;
  if (index >= string->length) RETURN_FALSE;

  RETURN_NUM(index);
}

DEF_PRIMITIVE(string_iteratorValue)
{
  ObjString* string = AS_STRING(args[0]);
  uint32_t index = validateIndex(vm, args[1], string->length, "Iterator");
  if (index == UINT32_MAX) return false;

  RETURN_VAL(wrenStringCodePointAt(vm, string, index));
}

DEF_PRIMITIVE(string_startsWith)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Edge case: If the search string is longer then return false right away.
  if (search->length > string->length) RETURN_FALSE;

  RETURN_BOOL(memcmp(string->value, search->value, search->length) == 0);
}

DEF_PRIMITIVE(string_plus)
{
  if (!validateString(vm, args[1], "Right operand")) return false;
  RETURN_VAL(wrenStringFormat(vm, "@@", args[0], args[1]));
}

DEF_PRIMITIVE(string_subscript)
{
  ObjString* string = AS_STRING(args[0]);

  if (IS_NUM(args[1]))
  {
    int index = validateIndex(vm, args[1], string->length, "Subscript");
    if (index == -1) return false;

    RETURN_VAL(wrenStringCodePointAt(vm, string, index));
  }

  if (!IS_RANGE(args[1]))
  {
    RETURN_ERROR("Subscript must be a number or a range.");
  }

  int step;
  uint32_t count = string->length;
  int start = calculateRange(vm, AS_RANGE(args[1]), &count, &step);
  if (start == -1) return false;

  RETURN_VAL(wrenNewStringFromRange(vm, string, start, count, step));
}

DEF_PRIMITIVE(string_toString)
{
  RETURN_VAL(args[0]);
}

DEF_PRIMITIVE(system_clock)
{
  RETURN_NUM((double)clock() / CLOCKS_PER_SEC);
}

DEF_PRIMITIVE(system_gc)
{
  wrenCollectGarbage(vm);
  RETURN_NULL;
}

DEF_PRIMITIVE(system_writeString)
{
  if (vm->config.writeFn != NULL)
  {
    vm->config.writeFn(vm, AS_CSTRING(args[1]));
  }

  RETURN_VAL(args[1]);
}

// Creates either the Object or Class class in the core module with [name].
static ObjClass* defineClass(WrenVM* vm, ObjModule* module, const char* name)
{
  ObjString* nameString = AS_STRING(wrenNewString(vm, name));
  wrenPushRoot(vm, (Obj*)nameString);

  ObjClass* classObj = wrenNewSingleClass(vm, 0, nameString);

  wrenDefineVariable(vm, module, name, nameString->length, OBJ_VAL(classObj), NULL);

  wrenPopRoot(vm);
  return classObj;
}

void wrenInitializeCore(WrenVM* vm)
{
  ObjModule* coreModule = wrenNewModule(vm, NULL);
  wrenPushRoot(vm, (Obj*)coreModule);
  
  // The core module's key is null in the module map.
  wrenMapSet(vm, vm->modules, NULL_VAL, OBJ_VAL(coreModule));
  wrenPopRoot(vm); // coreModule.

  // Define the root Object class. This has to be done a little specially
  // because it has no superclass.
  vm->objectClass = defineClass(vm, coreModule, "Object");
  PRIMITIVE(vm->objectClass, "!", object_not);
  PRIMITIVE(vm->objectClass, "==(_)", object_eqeq);
  PRIMITIVE(vm->objectClass, "!=(_)", object_bangeq);
  PRIMITIVE(vm->objectClass, "is(_)", object_is);
  PRIMITIVE(vm->objectClass, "toString", object_toString);
  PRIMITIVE(vm->objectClass, "type", object_type);
  PRIMITIVE(vm->objectClass, "hasMixin(_)", object_hasMixin);

  // REVATE EXTENSION (§7b): runtime attachment dispatch.  These live
  // on Object so every class instance (regular, mixin-bound, or
  // attachment-target) participates in the attach/detach machinery
  // without per-class opt-in.  The bookkeeping is C-side; lifecycle
  // hooks (`onAttached(_)` / `onDetached(_)`) fire through the
  // bracketed `<onAttached>(_)` / `<onDetached>(_)` trampolines
  // synthesised on every attachment class by §7a's
  // attachmentDefinition.
  PRIMITIVE(vm->objectClass, "attach(_)",        object_attach);
  PRIMITIVE(vm->objectClass, "detach(_)",        object_detach);
  PRIMITIVE(vm->objectClass, "detachAll(_)",     object_detachAll);
  PRIMITIVE(vm->objectClass, "hasAttachment(_)", object_hasAttachment);
  PRIMITIVE(vm->objectClass, "attachments(_)",   object_attachmentsOfClass);
  PRIMITIVE(vm->objectClass, "attachments",      object_attachmentsAll);
  PRIMITIVE(vm->objectClass, "detach()",         object_detachSelf);

  // Now we can define Class, which is a subclass of Object.
  vm->classClass = defineClass(vm, coreModule, "Class");
  wrenBindSuperclass(vm, vm->classClass, vm->objectClass);
  PRIMITIVE(vm->classClass, "name", class_name);
  PRIMITIVE(vm->classClass, "supertype", class_supertype);
  PRIMITIVE(vm->classClass, "toString", class_toString);
  PRIMITIVE(vm->classClass, "attributes", class_attributes);
  // REVATE EXTENSION (§7a): runtime attachment introspection.
  PRIMITIVE(vm->classClass, "isAttachment", class_isAttachment);
  PRIMITIVE(vm->classClass, "attachmentTargets", class_attachmentTargets);

  // Finally, we can define Object's metaclass which is a subclass of Class.
  ObjClass* objectMetaclass = defineClass(vm, coreModule, "Object metaclass");

  // Wire up the metaclass relationships now that all three classes are built.
  vm->objectClass->obj.classObj = objectMetaclass;
  objectMetaclass->obj.classObj = vm->classClass;
  vm->classClass->obj.classObj = vm->classClass;

  // Do this after wiring up the metaclasses so objectMetaclass doesn't get
  // collected.
  wrenBindSuperclass(vm, objectMetaclass, vm->classClass);

  PRIMITIVE(objectMetaclass, "same(_,_)", object_same);

  // The core class diagram ends up looking like this, where single lines point
  // to a class's superclass, and double lines point to its metaclass:
  //
  //        .------------------------------------. .====.
  //        |                  .---------------. | #    #
  //        v                  |               v | v    #
  //   .---------.   .-------------------.   .-------.  #
  //   | Object  |==>| Object metaclass  |==>| Class |=="
  //   '---------'   '-------------------'   '-------'
  //        ^                                 ^ ^ ^ ^
  //        |                  .--------------' # | #
  //        |                  |                # | #
  //   .---------.   .-------------------.      # | # -.
  //   |  Base   |==>|  Base metaclass   |======" | #  |
  //   '---------'   '-------------------'        | #  |
  //        ^                                     | #  |
  //        |                  .------------------' #  | Example classes
  //        |                  |                    #  |
  //   .---------.   .-------------------.          #  |
  //   | Derived |==>| Derived metaclass |=========="  |
  //   '---------'   '-------------------'            -'

  // The rest of the classes can now be defined normally.
  wrenInterpret(vm, NULL, coreModuleSource);

  vm->boolClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Bool"));
  PRIMITIVE(vm->boolClass, "toString", bool_toString);
  PRIMITIVE(vm->boolClass, "!", bool_not);

  vm->fiberClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Fiber"));
  PRIMITIVE(vm->fiberClass->obj.classObj, "new(_)", fiber_new);
  PRIMITIVE(vm->fiberClass->obj.classObj, "abort(_)", fiber_abort);
  PRIMITIVE(vm->fiberClass->obj.classObj, "current", fiber_current);
  PRIMITIVE(vm->fiberClass->obj.classObj, "suspend()", fiber_suspend);
  PRIMITIVE(vm->fiberClass->obj.classObj, "yield()", fiber_yield);
  PRIMITIVE(vm->fiberClass->obj.classObj, "yield(_)", fiber_yield1);
  PRIMITIVE(vm->fiberClass, "call()", fiber_call);
  PRIMITIVE(vm->fiberClass, "call(_)", fiber_call1);
  PRIMITIVE(vm->fiberClass, "error", fiber_error);
  PRIMITIVE(vm->fiberClass, "isDone", fiber_isDone);
  PRIMITIVE(vm->fiberClass, "transfer()", fiber_transfer);
  PRIMITIVE(vm->fiberClass, "transfer(_)", fiber_transfer1);
  PRIMITIVE(vm->fiberClass, "transferError(_)", fiber_transferError);
  PRIMITIVE(vm->fiberClass, "try()", fiber_try);
  PRIMITIVE(vm->fiberClass, "try(_)", fiber_try1);

  vm->fnClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Fn"));
  PRIMITIVE(vm->fnClass->obj.classObj, "new(_)", fn_new);

  PRIMITIVE(vm->fnClass, "arity", fn_arity);

  FUNCTION_CALL(vm->fnClass, "call()", fn_call0);
  FUNCTION_CALL(vm->fnClass, "call(_)", fn_call1);
  FUNCTION_CALL(vm->fnClass, "call(_,_)", fn_call2);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_)", fn_call3);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_)", fn_call4);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_)", fn_call5);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_)", fn_call6);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_)", fn_call7);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_)", fn_call8);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_)", fn_call9);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_)", fn_call10);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_,_)", fn_call11);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_)", fn_call12);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call13);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call14);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call15);
  FUNCTION_CALL(vm->fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call16);
  
  PRIMITIVE(vm->fnClass, "toString", fn_toString);

  vm->nullClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Null"));
  PRIMITIVE(vm->nullClass, "!", null_not);
  PRIMITIVE(vm->nullClass, "toString", null_toString);

  vm->numClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Num"));
  PRIMITIVE(vm->numClass->obj.classObj, "fromString(_)", num_fromString);
  PRIMITIVE(vm->numClass->obj.classObj, "infinity", num_infinity);
  PRIMITIVE(vm->numClass->obj.classObj, "nan", num_nan);
  PRIMITIVE(vm->numClass->obj.classObj, "pi", num_pi);
  PRIMITIVE(vm->numClass->obj.classObj, "tau", num_tau);
  PRIMITIVE(vm->numClass->obj.classObj, "largest", num_largest);
  PRIMITIVE(vm->numClass->obj.classObj, "smallest", num_smallest);
  PRIMITIVE(vm->numClass->obj.classObj, "maxSafeInteger", num_maxSafeInteger);
  PRIMITIVE(vm->numClass->obj.classObj, "minSafeInteger", num_minSafeInteger);
  PRIMITIVE(vm->numClass, "-(_)", num_minus);
  PRIMITIVE(vm->numClass, "+(_)", num_plus);
  PRIMITIVE(vm->numClass, "*(_)", num_multiply);
  PRIMITIVE(vm->numClass, "/(_)", num_divide);
  PRIMITIVE(vm->numClass, "<(_)", num_lt);
  PRIMITIVE(vm->numClass, ">(_)", num_gt);
  PRIMITIVE(vm->numClass, "<=(_)", num_lte);
  PRIMITIVE(vm->numClass, ">=(_)", num_gte);
  PRIMITIVE(vm->numClass, "&(_)", num_bitwiseAnd);
  PRIMITIVE(vm->numClass, "|(_)", num_bitwiseOr);
  PRIMITIVE(vm->numClass, "^(_)", num_bitwiseXor);
  PRIMITIVE(vm->numClass, "<<(_)", num_bitwiseLeftShift);
  PRIMITIVE(vm->numClass, ">>(_)", num_bitwiseRightShift);
  PRIMITIVE(vm->numClass, "abs", num_abs);
  PRIMITIVE(vm->numClass, "acos", num_acos);
  PRIMITIVE(vm->numClass, "asin", num_asin);
  PRIMITIVE(vm->numClass, "atan", num_atan);
  PRIMITIVE(vm->numClass, "cbrt", num_cbrt);
  PRIMITIVE(vm->numClass, "ceil", num_ceil);
  PRIMITIVE(vm->numClass, "cos", num_cos);
  PRIMITIVE(vm->numClass, "floor", num_floor);
  PRIMITIVE(vm->numClass, "-", num_negate);
  PRIMITIVE(vm->numClass, "round", num_round);
  PRIMITIVE(vm->numClass, "min(_)", num_min);
  PRIMITIVE(vm->numClass, "max(_)", num_max);
  PRIMITIVE(vm->numClass, "clamp(_,_)", num_clamp);
  PRIMITIVE(vm->numClass, "sin", num_sin);
  PRIMITIVE(vm->numClass, "sqrt", num_sqrt);
  PRIMITIVE(vm->numClass, "tan", num_tan);
  PRIMITIVE(vm->numClass, "log", num_log);
  PRIMITIVE(vm->numClass, "log2", num_log2);
  PRIMITIVE(vm->numClass, "exp", num_exp);
  PRIMITIVE(vm->numClass, "%(_)", num_mod);
  PRIMITIVE(vm->numClass, "~", num_bitwiseNot);
  PRIMITIVE(vm->numClass, "..(_)", num_dotDot);
  PRIMITIVE(vm->numClass, "...(_)", num_dotDotDot);
  PRIMITIVE(vm->numClass, "atan(_)", num_atan2);
  PRIMITIVE(vm->numClass, "pow(_)", num_pow);
  PRIMITIVE(vm->numClass, "fraction", num_fraction);
  PRIMITIVE(vm->numClass, "isInfinity", num_isInfinity);
  PRIMITIVE(vm->numClass, "isInteger", num_isInteger);
  PRIMITIVE(vm->numClass, "isNan", num_isNan);
  PRIMITIVE(vm->numClass, "sign", num_sign);
  PRIMITIVE(vm->numClass, "toString", num_toString);
  PRIMITIVE(vm->numClass, "truncate", num_truncate);

  // These are defined just so that 0 and -0 are equal, which is specified by
  // IEEE 754 even though they have different bit representations.
  PRIMITIVE(vm->numClass, "==(_)", num_eqeq);
  PRIMITIVE(vm->numClass, "!=(_)", num_bangeq);

  vm->stringClass = AS_CLASS(wrenFindVariable(vm, coreModule, "String"));
  PRIMITIVE(vm->stringClass->obj.classObj, "fromCodePoint(_)", string_fromCodePoint);
  PRIMITIVE(vm->stringClass->obj.classObj, "fromByte(_)", string_fromByte);
  PRIMITIVE(vm->stringClass, "+(_)", string_plus);
  PRIMITIVE(vm->stringClass, "[_]", string_subscript);
  PRIMITIVE(vm->stringClass, "byteAt_(_)", string_byteAt);
  PRIMITIVE(vm->stringClass, "byteCount_", string_byteCount);
  PRIMITIVE(vm->stringClass, "codePointAt_(_)", string_codePointAt);
  PRIMITIVE(vm->stringClass, "contains(_)", string_contains);
  PRIMITIVE(vm->stringClass, "endsWith(_)", string_endsWith);
  PRIMITIVE(vm->stringClass, "indexOf(_)", string_indexOf1);
  PRIMITIVE(vm->stringClass, "indexOf(_,_)", string_indexOf2);
  PRIMITIVE(vm->stringClass, "iterate(_)", string_iterate);
  PRIMITIVE(vm->stringClass, "iterateByte_(_)", string_iterateByte);
  PRIMITIVE(vm->stringClass, "iteratorValue(_)", string_iteratorValue);
  PRIMITIVE(vm->stringClass, "startsWith(_)", string_startsWith);
  PRIMITIVE(vm->stringClass, "toString", string_toString);

  vm->listClass = AS_CLASS(wrenFindVariable(vm, coreModule, "List"));
  PRIMITIVE(vm->listClass->obj.classObj, "filled(_,_)", list_filled);
  PRIMITIVE(vm->listClass->obj.classObj, "new()", list_new);
  PRIMITIVE(vm->listClass, "[_]", list_subscript);
  PRIMITIVE(vm->listClass, "[_]=(_)", list_subscriptSetter);
  PRIMITIVE(vm->listClass, "add(_)", list_add);
  PRIMITIVE(vm->listClass, "addCore_(_)", list_addCore);
  PRIMITIVE(vm->listClass, "clear()", list_clear);
  PRIMITIVE(vm->listClass, "count", list_count);
  PRIMITIVE(vm->listClass, "insert(_,_)", list_insert);
  PRIMITIVE(vm->listClass, "iterate(_)", list_iterate);
  PRIMITIVE(vm->listClass, "iteratorValue(_)", list_iteratorValue);
  PRIMITIVE(vm->listClass, "removeAt(_)", list_removeAt);
  PRIMITIVE(vm->listClass, "remove(_)", list_removeValue);
  PRIMITIVE(vm->listClass, "indexOf(_)", list_indexOf);
  PRIMITIVE(vm->listClass, "swap(_,_)", list_swap);

  vm->mapClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Map"));
  PRIMITIVE(vm->mapClass->obj.classObj, "new()", map_new);
  PRIMITIVE(vm->mapClass, "[_]", map_subscript);
  PRIMITIVE(vm->mapClass, "[_]=(_)", map_subscriptSetter);
  PRIMITIVE(vm->mapClass, "addCore_(_,_)", map_addCore);
  PRIMITIVE(vm->mapClass, "clear()", map_clear);
  PRIMITIVE(vm->mapClass, "containsKey(_)", map_containsKey);
  PRIMITIVE(vm->mapClass, "count", map_count);
  PRIMITIVE(vm->mapClass, "remove(_)", map_remove);
  PRIMITIVE(vm->mapClass, "iterate(_)", map_iterate);
  PRIMITIVE(vm->mapClass, "keyIteratorValue_(_)", map_keyIteratorValue);
  PRIMITIVE(vm->mapClass, "valueIteratorValue_(_)", map_valueIteratorValue);

  vm->rangeClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Range"));
  PRIMITIVE(vm->rangeClass, "from", range_from);
  PRIMITIVE(vm->rangeClass, "to", range_to);
  PRIMITIVE(vm->rangeClass, "min", range_min);
  PRIMITIVE(vm->rangeClass, "max", range_max);
  PRIMITIVE(vm->rangeClass, "isInclusive", range_isInclusive);
  PRIMITIVE(vm->rangeClass, "iterate(_)", range_iterate);
  PRIMITIVE(vm->rangeClass, "iteratorValue(_)", range_iteratorValue);
  PRIMITIVE(vm->rangeClass, "toString", range_toString);

  ObjClass* systemClass = AS_CLASS(wrenFindVariable(vm, coreModule, "System"));
  PRIMITIVE(systemClass->obj.classObj, "clock", system_clock);
  PRIMITIVE(systemClass->obj.classObj, "gc()", system_gc);
  PRIMITIVE(systemClass->obj.classObj, "writeString_(_)", system_writeString);

  // While bootstrapping the core types and running the core module, a number
  // of string objects have been created, many of which were instantiated
  // before stringClass was stored in the VM. Some of them *must* be created
  // first -- the ObjClass for string itself has a reference to the ObjString
  // for its name.
  //
  // These all currently have a NULL classObj pointer, so go back and assign
  // them now that the string class is known.
  for (Obj* obj = vm->first; obj != NULL; obj = obj->next)
  {
    if (obj->type == OBJ_STRING) obj->classObj = vm->stringClass;
  }
}
