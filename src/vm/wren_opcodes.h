// This defines the bytecode instructions used by the VM. It does so by invoking
// an OPCODE() macro which is expected to be defined at the point that this is
// included. (See: http://en.wikipedia.org/wiki/X_Macro for more.)
//
// The first argument is the name of the opcode. The second is its "stack
// effect" -- the amount that the op code changes the size of the stack. A
// stack effect of 1 means it pushes a value and the stack grows one larger.
// -2 means it pops two values, etc.
//
// Note that the order of instructions here affects the order of the dispatch
// table in the VM's interpreter loop. That in turn affects caching which
// affects overall performance. Take care to run benchmarks if you change the
// order here.

// Load the constant at index [arg].
OPCODE(CONSTANT, 1)

// Push null onto the stack.
OPCODE(NULL, 1)

// Push false onto the stack.
OPCODE(FALSE, 1)

// Push true onto the stack.
OPCODE(TRUE, 1)

// Pushes the value in the given local slot.
OPCODE(LOAD_LOCAL_0, 1)
OPCODE(LOAD_LOCAL_1, 1)
OPCODE(LOAD_LOCAL_2, 1)
OPCODE(LOAD_LOCAL_3, 1)
OPCODE(LOAD_LOCAL_4, 1)
OPCODE(LOAD_LOCAL_5, 1)
OPCODE(LOAD_LOCAL_6, 1)
OPCODE(LOAD_LOCAL_7, 1)
OPCODE(LOAD_LOCAL_8, 1)

// Note: The compiler assumes the following _STORE instructions always
// immediately follow their corresponding _LOAD ones.

// Pushes the value in local slot [arg].
OPCODE(LOAD_LOCAL, 1)

// Stores the top of stack in local slot [arg]. Does not pop it.
OPCODE(STORE_LOCAL, 0)

// Pushes the value in upvalue [arg].
OPCODE(LOAD_UPVALUE, 1)

// Stores the top of stack in upvalue [arg]. Does not pop it.
OPCODE(STORE_UPVALUE, 0)

// Pushes the value of the top-level variable in slot [arg].
OPCODE(LOAD_MODULE_VAR, 1)

// Stores the top of stack in top-level variable slot [arg]. Does not pop it.
OPCODE(STORE_MODULE_VAR, 0)

// Pushes the value of the field in slot [arg] of the receiver of the current
// function. This is used for regular field accesses on "this" directly in
// methods. This instruction is faster than the more general CODE_LOAD_FIELD
// instruction.
OPCODE(LOAD_FIELD_THIS, 1)

// Stores the top of the stack in field slot [arg] in the receiver of the
// current value. Does not pop the value. This instruction is faster than the
// more general CODE_LOAD_FIELD instruction.
OPCODE(STORE_FIELD_THIS, 0)

// Pops an instance and pushes the value of the field in slot [arg] of it.
OPCODE(LOAD_FIELD, 0)

// Pops an instance and stores the subsequent top of stack in field slot
// [arg] in it. Does not pop the value.
OPCODE(STORE_FIELD, -1)

// Pop and discard the top of stack.
OPCODE(POP, -1)

// Invoke the method with symbol [arg]. The number indicates the number of
// arguments (not including the receiver).
OPCODE(CALL_0, 0)
OPCODE(CALL_1, -1)
OPCODE(CALL_2, -2)
OPCODE(CALL_3, -3)
OPCODE(CALL_4, -4)
OPCODE(CALL_5, -5)
OPCODE(CALL_6, -6)
OPCODE(CALL_7, -7)
OPCODE(CALL_8, -8)
OPCODE(CALL_9, -9)
OPCODE(CALL_10, -10)
OPCODE(CALL_11, -11)
OPCODE(CALL_12, -12)
OPCODE(CALL_13, -13)
OPCODE(CALL_14, -14)
OPCODE(CALL_15, -15)
OPCODE(CALL_16, -16)

// Invoke a superclass method with symbol [arg]. The number indicates the
// number of arguments (not including the receiver).
OPCODE(SUPER_0, 0)
OPCODE(SUPER_1, -1)
OPCODE(SUPER_2, -2)
OPCODE(SUPER_3, -3)
OPCODE(SUPER_4, -4)
OPCODE(SUPER_5, -5)
OPCODE(SUPER_6, -6)
OPCODE(SUPER_7, -7)
OPCODE(SUPER_8, -8)
OPCODE(SUPER_9, -9)
OPCODE(SUPER_10, -10)
OPCODE(SUPER_11, -11)
OPCODE(SUPER_12, -12)
OPCODE(SUPER_13, -13)
OPCODE(SUPER_14, -14)
OPCODE(SUPER_15, -15)
OPCODE(SUPER_16, -16)

// Jump the instruction pointer [arg] forward.
OPCODE(JUMP, 0)

// Jump the instruction pointer [arg] backward.
OPCODE(LOOP, 0)

// Pop and if not truthy then jump the instruction pointer [arg] forward.
OPCODE(JUMP_IF, -1)

// If the top of the stack is false, jump [arg] forward. Otherwise, pop and
// continue.
OPCODE(AND, -1)

// If the top of the stack is non-false, jump [arg] forward. Otherwise, pop
// and continue.
OPCODE(OR, -1)

// Close the upvalue for the local on the top of the stack, then pop it.
OPCODE(CLOSE_UPVALUE, -1)

// Exit from the current function and return the value on the top of the
// stack.
OPCODE(RETURN, 0)

// Creates a closure for the function stored at [arg] in the constant table.
//
// Following the function argument is a number of arguments, two for each
// upvalue. The first is true if the variable being captured is a local (as
// opposed to an upvalue), and the second is the index of the local or
// upvalue being captured.
//
// Pushes the created closure.
OPCODE(CLOSURE, 1)

// Creates a new instance of a class.
//
// Assumes the class object is in slot zero, and replaces it with the new
// uninitialized instance of that class. This opcode is only emitted by the
// compiler-generated constructor metaclass methods.
OPCODE(CONSTRUCT, 0)

// Creates a new instance of a foreign class.
//
// Assumes the class object is in slot zero, and replaces it with the new
// uninitialized instance of that class. This opcode is only emitted by the
// compiler-generated constructor metaclass methods.
OPCODE(FOREIGN_CONSTRUCT, 0)

// Creates a class. Top of stack is the superclass. Below that is a string for
// the name of the class. Byte [arg] is the number of fields in the class.
OPCODE(CLASS, -1)

// Ends a class. 
// Atm the stack contains the class and the ClassAttributes (or null).
OPCODE(END_CLASS, -2)

// Creates a foreign class. Top of stack is the superclass. Below that is a
// string for the name of the class.
OPCODE(FOREIGN_CLASS, -1)

// Define a method for symbol [arg]. The class receiving the method is popped
// off the stack, then the function defining the body is popped.
//
// If a foreign method is being defined, the "function" will be a string
// identifying the foreign method. Otherwise, it will be a function or
// closure.
OPCODE(METHOD_INSTANCE, -2)

// Define a method for symbol [arg]. The class whose metaclass will receive
// the method is popped off the stack, then the function defining the body is
// popped.
//
// If a foreign method is being defined, the "function" will be a string
// identifying the foreign method. Otherwise, it will be a function or
// closure.
OPCODE(METHOD_STATIC, -2)

// REVATE EXTENSION: Public-visibility variants of METHOD_INSTANCE/METHOD_STATIC.
// Same semantics but set Method.isPublic = true on the bound method.
OPCODE(METHOD_INSTANCE_PUBLIC, -2)
OPCODE(METHOD_STATIC_PUBLIC, -2)

// REVATE EXTENSION: Creates a mixin class.  Same as CLASS but sets isMixin = true.
// Byte [arg] is the number of fields in the mixin.
OPCODE(MIXIN, -1)

// REVATE EXTENSION: Binds a mixin to a class.  Pops the mixin, then the class.
OPCODE(BIND_MIXIN, -2)

// REVATE EXTENSION: Stores a field default on a class.
// Stack: ..., class, value → ..., class
// Byte [arg] is the field index (class-local, adjusted by superclass offset at
// runtime).  Pops the value but leaves the class.
OPCODE(FIELD_DEFAULT, -1)

// REVATE EXTENSION (§6b): Invoke a method on a specific mixin's namespace
// from inside a class method body — the runtime side of
// `my.MixinName.method(args)`.
//
// Operands: (mixinNameConst : short, methodSymbol : short)
//   mixinNameConst   — constant-pool index of an ObjString naming the
//                      mixin (e.g. "Health"); compared by name against
//                      classObj->mixins[].name at the call site.
//   methodSymbol     — VM-wide method-name symbol (same space as
//                      CODE_CALL_<n>).
//
// Stack: receiver + N args on top.  The receiver (this) is read from
// the bottom of the args window, exactly like CODE_CALL_<n>; the call
// runs the mixin's offset-adjusted clone — kept in
// classObj->mixinMethods[i] — so `my.M.foo()` reaches M's original
// body even when the host class shadows the slot.
//
// Stack effect mirrors CODE_CALL_<n>: -N (the args are consumed,
// receiver slot becomes the return value).
//
// Runtime errors (all WREN_ERROR_COMPILE for diagnostic consistency):
//   - mixin name is not in `this`'s class's `with` list
//     (compile-time guards make this unreachable from valid code;
//     here as defense in depth)
//   - the mixin does not declare the named method
OPCODE(INVOKE_MIXIN_METHOD_0,   0)
OPCODE(INVOKE_MIXIN_METHOD_1,  -1)
OPCODE(INVOKE_MIXIN_METHOD_2,  -2)
OPCODE(INVOKE_MIXIN_METHOD_3,  -3)
OPCODE(INVOKE_MIXIN_METHOD_4,  -4)
OPCODE(INVOKE_MIXIN_METHOD_5,  -5)
OPCODE(INVOKE_MIXIN_METHOD_6,  -6)
OPCODE(INVOKE_MIXIN_METHOD_7,  -7)
OPCODE(INVOKE_MIXIN_METHOD_8,  -8)
OPCODE(INVOKE_MIXIN_METHOD_9,  -9)
OPCODE(INVOKE_MIXIN_METHOD_10, -10)
OPCODE(INVOKE_MIXIN_METHOD_11, -11)
OPCODE(INVOKE_MIXIN_METHOD_12, -12)
OPCODE(INVOKE_MIXIN_METHOD_13, -13)
OPCODE(INVOKE_MIXIN_METHOD_14, -14)
OPCODE(INVOKE_MIXIN_METHOD_15, -15)
OPCODE(INVOKE_MIXIN_METHOD_16, -16)

// REVATE EXTENSION (§6b): load a mixin-namespaced field from `this`.
//
// Operands: (mixinNameConst : short, fieldNameConst : short)
// Stack effect: +1 (pushes the loaded value).
//
// The VM finds the named mixin's slot in
// `wrenGetClassInline(this)->mixins[]`, looks up the field name in
// that mixin's own `fields` symbol table, adds the host's
// `mixinFieldOffsets[slot]`, and emits a regular field load against
// `this` at the resulting offset.
OPCODE(LOAD_MIXIN_FIELD_THIS,  1)

// REVATE EXTENSION (§6b): store the top-of-stack into a mixin-namespaced
// field on `this`.  Does NOT pop the value (same convention as
// CODE_STORE_FIELD_THIS).
//
// Operands: (mixinNameConst : short, fieldNameConst : short)
// Stack effect: 0.
OPCODE(STORE_MIXIN_FIELD_THIS, 0)

// REVATE EXTENSION (§6c): Stores a per-class mixin field default on
// the class template.  Runs once, at class-definition time, after
// CODE_BIND_MIXIN has merged the mixin's own defaults into the host.
//
// Stack: ..., class, value → ..., class
//
// Operands: (mixinNameConst : short, fieldNameConst : short)
//   mixinNameConst   — constant-pool index of an ObjString naming a
//                      mixin in the class's `with` list.
//   fieldNameConst   — constant-pool index of an ObjString naming a
//                      field declared on that mixin (public or
//                      private).
//
// The VM walks classObj->mixins[] for the named mixin, resolves the
// per-mixin field slot index by scanning the mixin's synthesized
// getter for `LOAD_FIELD_THIS <slot>` (same machinery as
// LOAD_MIXIN_FIELD_THIS), then writes the popped value into
// classObj->fieldDefaults[mixinFieldOffsets[slot] + fieldIdx].
// Pops the value but leaves the class on the stack so a series of
// CODE_MIXIN_FIELD_DEFAULT emissions can target the same class.
OPCODE(MIXIN_FIELD_DEFAULT, -1)

// REVATE EXTENSION (§7a): Creates an attachment class.  Same shape as
// CLASS but sets isAttachment = true on the resulting ObjClass.  Used by
// `attachment X targets Y { ... }` declarations.  Byte [arg] is the
// number of fields in the attachment (already includes the implicit
// `host` field at slot 0).
//
// Stack: ..., name, superclass → ..., class
OPCODE(ATTACHMENT, -1)

// REVATE EXTENSION (§7a): Appends a single target-class name string to
// the attachment class's `attachmentTargets[]` array.  Emitted once per
// entry in the `targets X, Y, ...` clause, after CODE_ATTACHMENT but
// before any method binding.  The wildcard `targets *` form emits a
// single ATTACHMENT_TARGET whose name string is "*".
//
// Stack: ..., class → ..., class  (leaves the class for chaining)
//
// Operand: nameConst (short) — constant-pool index of the target's
// ObjString name.  Storing the string (not an ObjClass pointer) lets
// the target be declared later in the module; resolution happens at
// attach-time (7b), not at declaration time.
OPCODE(ATTACHMENT_TARGET, 0)

// REVATE EXTENSION (§7d): Marks the attachment class on top of the
// stack as `@unique`.  Sets `classObj->isUnique = true` so host.attach()
// will refuse to install a duplicate of this attachment class on a
// host that already carries one (returns null + stderr warning, leaves
// the existing instance untouched).  Emitted once per attachment whose
// declaration site carries the `@unique` attribute.
//
// Stack: ..., class → ..., class  (leaves the class for chaining)
OPCODE(ATTACHMENT_UNIQUE, 0)

// REVATE EXTENSION (§6a): Validates @override(MixinName) annotations and
// emits compile-time warnings for unannotated shadows of mixin methods.
//
// Stack: ..., class, validationMap → ..., class
//   (validationMap is a Wren Map produced by the compiler; the class is
//    left on the stack so the caller may continue patching it).
//
// The validation map has the shape:
//   {
//     "noOverrideWarnings": Bool,
//     "withMixins": [String, String, ...],   // names in `with` order
//     "classMethods": {
//       "<full sig>": {
//         "static": Bool,
//         "overrides": [String, String, ...] // mixin names from @override
//       },
//       ...
//     }
//   }
//
// The opcode walks classObj->mixins[] and, for each (mixin, method)
// pair, performs the diagnostic logic described in
// docs/specs/wren_language_extensions.md §6.
OPCODE(VALIDATE_OVERRIDES, -1)

// This is executed at the end of the module's body. Pushes NULL onto the stack
// as the "return value" of the import statement and stores the module as the
// most recently imported one.
OPCODE(END_MODULE, 1)

// Import a module whose name is the string stored at [arg] in the constant
// table.
//
// Pushes null onto the stack so that the fiber for the imported module can
// replace that with a dummy value when it returns. (Fibers always return a
// value when resuming a caller.)
OPCODE(IMPORT_MODULE, 1)

// Import a variable from the most recently imported module. The name of the
// variable to import is at [arg] in the constant table. Pushes the loaded
// variable's value.
OPCODE(IMPORT_VARIABLE, 1)

// This pseudo-instruction indicates the end of the bytecode. It should
// always be preceded by a `CODE_RETURN`, so is never actually executed.
OPCODE(END, 0)
