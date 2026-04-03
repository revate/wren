// Standalone test for Revate Wren visibility (public/private-by-default).
// Compile & run:
//   cc -g -I src/include -o test_visibility test_visibility.c build/libwren.a -lm
//   ./test_visibility

#include <stdio.h>
#include <string.h>
#include "wren.h"

static int testsPassed = 0;
static int testsFailed = 0;
static char lastError[1024] = {0};

static void writeFn(WrenVM* vm, const char* text)
{
    printf("%s", text);
}

static void errorFn(WrenVM* vm, WrenErrorType type,
                    const char* module, int line, const char* message)
{
    if (type == WREN_ERROR_RUNTIME)
    {
        snprintf(lastError, sizeof(lastError), "%s", message);
        printf("[runtime error] %s\n", message);
    }
    else if (type == WREN_ERROR_COMPILE)
    {
        printf("[compile error] %s:%d: %s\n", module, line, message);
    }
    else if (type == WREN_ERROR_STACK_TRACE)
    {
        printf("  at %s:%d in %s\n", module, line, message);
    }
}

static void check(const char* label, int condition)
{
    if (condition)
    {
        testsPassed++;
        printf("  PASS: %s\n", label);
    }
    else
    {
        testsFailed++;
        printf("  FAIL: %s\n", label);
    }
}

int main(void)
{
    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.writeFn = writeFn;
    config.errorFn = errorFn;

    WrenVM* vm = wrenNewVM(&config);

    printf("=== Wren Visibility Test ===\n\n");

    // ── Test 1: Public methods work, private methods blocked from outside ──
    printf("--- Basic public/private ---\n");

    const char* classSource =
        "class Greeter {\n"
        "  construct new(name) {\n"
        "    _name = name\n"
        "  }\n"
        "\n"
        "  // Private by default\n"
        "  secret() { \"whisper from %(_name)\" }\n"
        "\n"
        "  // Explicitly public\n"
        "  public greet() { \"hello from %(_name)\" }\n"
        "\n"
        "  // Public method that calls private method internally\n"
        "  public callSecret() { secret() }\n"
        "\n"
        "  // Public getter\n"
        "  public name { _name }\n"
        "}\n"
        "\n"
        "// Store in a module variable so we can access from later calls\n"
        "var G = Greeter.new(\"Alice\")\n";

    WrenInterpretResult result = wrenInterpret(vm, "test", classSource);
    check("class definition compiles", result == WREN_RESULT_SUCCESS);

    // Test public method
    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "System.print(G.greet())\n");
    check("public method callable from outside", result == WREN_RESULT_SUCCESS);

    // Test public getter
    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "System.print(G.name)\n");
    check("public getter callable from outside", result == WREN_RESULT_SUCCESS);

    // Test private-from-inside via public wrapper
    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "System.print(G.callSecret())\n");
    check("private from inside (via public wrapper)", result == WREN_RESULT_SUCCESS);

    // Test private method blocked from outside
    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "G.secret()\n");
    check("private method blocked from outside", result == WREN_RESULT_RUNTIME_ERROR);
    check("error mentions 'private'", strstr(lastError, "private") != NULL);

    // ── Test 2: Constructors always public ──
    printf("\n--- Constructors ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "var g2 = Greeter.new(\"Bob\")\n"
        "System.print(g2.greet())\n");
    check("constructor always public", result == WREN_RESULT_SUCCESS);

    // ── Test 3: Inheritance ──
    printf("\n--- Inheritance ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "class FriendlyGreeter is Greeter {\n"
        "  construct new(name) {\n"
        "    super(name)\n"
        "  }\n"
        "  public friendlyGreet() { \"friendly: \" + secret() }\n"
        "}\n"
        "var fg = FriendlyGreeter.new(\"Carol\")\n"
        "System.print(fg.friendlyGreet())\n");
    check("subclass can call inherited private internally", result == WREN_RESULT_SUCCESS);

    // Inherited public from outside
    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "System.print(fg.greet())\n");
    check("inherited public works from outside", result == WREN_RESULT_SUCCESS);

    // Inherited private blocked from outside
    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "fg.secret()\n");
    check("inherited private blocked from outside", result == WREN_RESULT_RUNTIME_ERROR);
    check("inherited private error mentions 'private'", strstr(lastError, "private") != NULL);

    // ── Test 4: Built-in methods still work ──
    printf("\n--- Built-in methods ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "System.print(G.toString)\n");
    check("built-in toString accessible", result == WREN_RESULT_SUCCESS);

    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "System.print(G.type)\n");
    check("built-in type accessible", result == WREN_RESULT_SUCCESS);

    // ── Test 5: Fiber.try captures visibility error ──
    printf("\n--- Fiber.try error capture ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "var fiber = Fiber.new {\n"
        "  G.secret()\n"
        "}\n"
        "var err = fiber.try()\n"
        "System.print(\"caught: \" + err)\n");
    check("Fiber.try captures private error", result == WREN_RESULT_SUCCESS);

    // ── Test 6: Top-level functions not affected ──
    printf("\n--- Top-level (no visibility restriction) ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "test",
        "var topLevel = Fn.new { \"I'm free\" }\n"
        "System.print(topLevel.call())\n");
    check("top-level Fn not restricted", result == WREN_RESULT_SUCCESS);

    // ══════════════════════════════════════════════════════════════
    // Phase 2b: var field syntax (no underscore)
    // ══════════════════════════════════════════════════════════════

    // ── Test 7: var field declaration + private access ──
    printf("\n--- var field syntax ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "vartest",
        "class Counter {\n"
        "  var count\n"
        "  var label\n"
        "\n"
        "  construct new(n, l) {\n"
        "    count = n\n"
        "    label = l\n"
        "  }\n"
        "\n"
        "  // Private method that uses fields\n"
        "  format_() { label + \": \" + count.toString }\n"
        "\n"
        "  // Public method\n"
        "  public display() { format_() }\n"
        "}\n"
        "\n"
        "var C = Counter.new(42, \"hits\")\n");
    check("var field class definition compiles", result == WREN_RESULT_SUCCESS);

    // ── Test 8: Private field getter blocked from outside ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "vartest",
        "C.count\n");
    check("private var getter blocked from outside",
          result == WREN_RESULT_RUNTIME_ERROR);
    check("private var error mentions 'private'",
          strstr(lastError, "private") != NULL);

    // ── Test 9: Private field setter blocked from outside ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "vartest",
        "C.count = 99\n");
    check("private var setter blocked from outside",
          result == WREN_RESULT_RUNTIME_ERROR);

    // ── Test 10: Public method using private fields works ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "vartest",
        "System.print(C.display())\n");
    check("public method using private fields", result == WREN_RESULT_SUCCESS);

    // ── Test 11: public var field declaration ──
    printf("\n--- public var field syntax ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "pubvartest",
        "class Player {\n"
        "  public var name\n"
        "  public var score\n"
        "  var secretId\n"
        "\n"
        "  construct new(n, s, id) {\n"
        "    name = n\n"
        "    score = s\n"
        "    secretId = id\n"
        "  }\n"
        "\n"
        "  public info() { name + \" (\" + score.toString + \")\" }\n"
        "}\n"
        "\n"
        "var P = Player.new(\"Alice\", 100, 42)\n");
    check("public var class definition compiles", result == WREN_RESULT_SUCCESS);

    // ── Test 12: Public var getter works from outside ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "pubvartest",
        "System.print(P.name)\n");
    check("public var getter accessible", result == WREN_RESULT_SUCCESS);

    // ── Test 13: Public var setter works from outside ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "pubvartest",
        "P.score = 200\n"
        "System.print(P.score)\n");
    check("public var setter accessible", result == WREN_RESULT_SUCCESS);

    // ── Test 14: Private var still blocked ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "pubvartest",
        "P.secretId\n");
    check("private var still blocked in mixed class",
          result == WREN_RESULT_RUNTIME_ERROR);

    // ── Test 15: public method using vars works ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "pubvartest",
        "System.print(P.info())\n");
    check("public method with public vars", result == WREN_RESULT_SUCCESS);

    // ── Test 16: Default values ──
    printf("\n--- var with default values ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "deftest",
        "class Config {\n"
        "  public var volume = 75\n"
        "  public var name = \"unnamed\"\n"
        "  public var enabled = true\n"
        "  var internal = 0\n"
        "\n"
        "  construct new() {}\n"
        "}\n"
        "\n"
        "var cfg = Config.new()\n"
        "System.print(cfg.volume)\n"
        "System.print(cfg.name)\n"
        "System.print(cfg.enabled)\n");
    check("default values applied in constructor", result == WREN_RESULT_SUCCESS);

    // ── Test 17: Default value overridden by constructor ──
    lastError[0] = 0;
    result = wrenInterpret(vm, "deftest2",
        "class Item {\n"
        "  public var value = 10\n"
        "  public var rarity = \"common\"\n"
        "\n"
        "  construct new() {}\n"
        "  construct withValue(v) { value = v }\n"
        "  construct full(v, r) {\n"
        "    value = v\n"
        "    rarity = r\n"
        "  }\n"
        "}\n"
        "\n"
        "var a = Item.new()\n"
        "var b = Item.withValue(99)\n"
        "var c = Item.full(50, \"rare\")\n"
        "System.print(a.value)\n"       // 10
        "System.print(b.value)\n"       // 99
        "System.print(b.rarity)\n"      // common
        "System.print(c.rarity)\n");    // rare
    check("default with override", result == WREN_RESULT_SUCCESS);

    // ── Test 18: Old _underscore syntax still works ──
    printf("\n--- backwards compatibility ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "compat",
        "class OldStyle {\n"
        "  construct new() { _x = 99 }\n"
        "  public getX() { _x }\n"
        "}\n"
        "var o = OldStyle.new()\n"
        "System.print(o.getX())\n");
    check("old _underscore syntax still works", result == WREN_RESULT_SUCCESS);

    // ── Test 19: defaultPublic VM flag ──
    printf("\n--- defaultPublic VM flag ---\n");

    // With defaultPublic = true, methods without 'public' keyword are public
    wrenSetDefaultPublic(vm, true);
    lastError[0] = 0;
    result = wrenInterpret(vm, "defpub",
        "class Widget {\n"
        "  construct new() { _val = 42 }\n"
        "  getValue() { _val }\n"       // no 'public' keyword
        "  setValue(v) { _val = v }\n"
        "}\n");
    check("defaultPublic class compiles", result == WREN_RESULT_SUCCESS);
    wrenSetDefaultPublic(vm, false);  // reset

    // Call from outside — should work because compiled under defaultPublic
    result = wrenInterpret(vm, "defpub_call",
        "import \"defpub\" for Widget\n"
        "var w = Widget.new()\n"
        "System.print(w.getValue())\n"   // 42
        "w.setValue(100)\n"
        "System.print(w.getValue())\n"); // 100
    check("defaultPublic methods callable from outside", result == WREN_RESULT_SUCCESS);

    // After resetting, new classes should be private-by-default again
    lastError[0] = 0;
    result = wrenInterpret(vm, "defpub_off",
        "class Locked {\n"
        "  construct new() { _x = 7 }\n"
        "  secret() { _x }\n"           // private again
        "}\n");
    check("after reset, class compiles", result == WREN_RESULT_SUCCESS);

    lastError[0] = 0;
    result = wrenInterpret(vm, "defpub_off_call",
        "import \"defpub_off\" for Locked\n"
        "var l = Locked.new()\n"
        "l.secret()\n");
    check("after reset, methods are private again",
          result == WREN_RESULT_RUNTIME_ERROR &&
          strstr(lastError, "private") != NULL);

    // ── Test 23: Top-level enum ──
    printf("\n--- enum syntax ---\n");

    lastError[0] = 0;
    result = wrenInterpret(vm, "enum_test",
        "enum Rarity {\n"
        "  Common,\n"
        "  Uncommon,\n"
        "  Rare,\n"
        "  Legendary\n"
        "}\n"
        "System.print(Rarity.Common)\n"     // 0
        "System.print(Rarity.Uncommon)\n"   // 1
        "System.print(Rarity.Rare)\n"       // 2
        "System.print(Rarity.Legendary)\n"  // 3
    );
    check("top-level enum compiles and returns ordinals",
          result == WREN_RESULT_SUCCESS);

    // Test 24: enum comparison
    lastError[0] = 0;
    result = wrenInterpret(vm, "enum_cmp",
        "import \"enum_test\" for Rarity\n"
        "var r = Rarity.Rare\n"
        "System.print(r == Rarity.Rare)\n"           // true
        "System.print(r == Rarity.Common)\n"         // false
        "System.print(r == 2)\n"                     // true (int comparison)
    );
    check("enum comparison works", result == WREN_RESULT_SUCCESS);

    // Test 25: enum importable from another module
    lastError[0] = 0;
    result = wrenInterpret(vm, "enum_import",
        "import \"enum_test\" for Rarity\n"
        "class Item {\n"
        "  construct new(r) { _rarity = r }\n"
        "  public rarity { _rarity }\n"
        "  public isRare() {\n"
        "    return _rarity == Rarity.Rare || _rarity == Rarity.Legendary\n"
        "  }\n"
        "}\n"
        "var a = Item.new(Rarity.Common)\n"
        "var b = Item.new(Rarity.Legendary)\n"
        "System.print(a.isRare())\n"    // false
        "System.print(b.isRare())\n"    // true
    );
    check("enum usable in another class via import",
          result == WREN_RESULT_SUCCESS);

    // Test 26: enum inside a class body
    lastError[0] = 0;
    result = wrenInterpret(vm, "enum_nested",
        "class Weapon {\n"
        "  enum DamageType {\n"
        "    Physical,\n"
        "    Fire,\n"
        "    Ice,\n"
        "    Electric\n"
        "  }\n"
        "  construct new(t) { _type = t }\n"
        "  public damageType { _type }\n"
        "}\n"
        "var w = Weapon.new(DamageType.Fire)\n"
        "System.print(w.damageType)\n"            // 1
        "System.print(DamageType.Electric)\n"     // 3
    );
    check("enum inside class body works",
          result == WREN_RESULT_SUCCESS);

    // Test 27: enum always public (no 'public' keyword needed)
    lastError[0] = 0;
    result = wrenInterpret(vm, "enum_pub",
        "enum Color {\n"
        "  Red,\n"
        "  Green,\n"
        "  Blue\n"
        "}\n");
    check("enum class compiles", result == WREN_RESULT_SUCCESS);

    result = wrenInterpret(vm, "enum_pub_use",
        "import \"enum_pub\" for Color\n"
        "System.print(Color.Red)\n"    // 0
        "System.print(Color.Blue)\n"   // 2
    );
    check("enum members accessible from outside module",
          result == WREN_RESULT_SUCCESS);

    // ── Phase 4: Typed Properties + @ Meta Tags ──
    printf("\n--- typed property syntax ---\n");

    // Test 28: typed property compiles and getter/setter work
    lastError[0] = 0;
    result = wrenInterpret(vm, "typed_prop",
        "class Item {\n"
        "  public Num    value     = 10\n"
        "  public String label     = \"sword\"\n"
        "  public Bool   equipped  = false\n"
        "  construct new() {}\n"
        "}\n"
        "var i = Item.new()\n"
        "System.print(i.value)\n"     // 10
        "System.print(i.label)\n"     // sword
        "System.print(i.equipped)\n"  // false
        "i.value = 99\n"
        "i.label = \"axe\"\n"
        "i.equipped = true\n"
        "System.print(i.value)\n"     // 99
        "System.print(i.label)\n"     // axe
        "System.print(i.equipped)\n"  // true
    );
    check("typed properties compile with getter/setter",
          result == WREN_RESULT_SUCCESS);

    // Test 29: typed property metadata in class attributes
    lastError[0] = 0;
    result = wrenInterpret(vm, "typed_meta",
        "import \"typed_prop\" for Item\n"
        "var attrs = Item.attributes\n"
        "if (attrs == null) {\n"
        "  System.print(\"ERR: no attributes\")\n"
        "} else {\n"
        "  var props = attrs.self[\"__properties\"]\n"
        "  if (props == null) {\n"
        "    System.print(\"ERR: no __properties\")\n"
        "  } else {\n"
        "    System.print(props[\"value\"])\n"    // [Num]
        "    System.print(props[\"label\"])\n"    // [String]
        "    System.print(props[\"equipped\"])\n" // [Bool]
        "  }\n"
        "}\n"
    );
    check("typed property metadata in attributes",
          result == WREN_RESULT_SUCCESS);

    // Test 30: @ meta tag syntax (on a class)
    lastError[0] = 0;
    result = wrenInterpret(vm, "at_tag",
        "@editor(category = \"Gameplay\", tooltip = \"A coin\")\n"
        "class Trinket {\n"
        "  public Num worth = 5\n"
        "  construct new() {}\n"
        "}\n"
        "var t = Trinket.new()\n"
        "System.print(t.worth)\n"    // 5
        "var attrs = Trinket.attributes\n"
        "if (attrs != null && attrs.self != null) {\n"
        "  var editor = attrs.self[\"editor\"]\n"
        "  if (editor != null) {\n"
        "    System.print(editor[\"category\"])\n"  // [Gameplay]
        "    System.print(editor[\"tooltip\"])\n"   // [A coin]
        "  } else {\n"
        "    System.print(\"ERR: no editor group\")\n"
        "  }\n"
        "} else {\n"
        "  System.print(\"ERR: no attrs\")\n"
        "}\n"
    );
    check("@ meta tag stores runtime attributes",
          result == WREN_RESULT_SUCCESS);

    // Test 31: @ tag on a method
    lastError[0] = 0;
    result = wrenInterpret(vm, "at_method",
        "class Gadget {\n"
        "  construct new() {}\n"
        "  @editor(tooltip = \"Activates the gadget\")\n"
        "  public activate() {\n"
        "    System.print(\"activated\")\n"
        "  }\n"
        "}\n"
        "var g = Gadget.new()\n"
        "g.activate()\n"    // activated
    );
    check("@ meta tag on method compiles and method works",
          result == WREN_RESULT_SUCCESS);

    // Test 32: mixed typed properties + enum + regular methods
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixed_all",
        "class Chest {\n"
        "  enum LockType {\n"
        "    None,\n"
        "    Padlock,\n"
        "    Magic\n"
        "  }\n"
        "  public Num    gold    = 0\n"
        "  public String name    = \"chest\"\n"
        "  var locked\n"
        "  construct new() {\n"
        "    locked = true\n"
        "  }\n"
        "  public open() {\n"
        "    locked = false\n"
        "    System.print(\"%(name) opened, %(gold) gold!\")\n"
        "  }\n"
        "}\n"
        "var c = Chest.new()\n"
        "c.gold = 100\n"
        "c.name = \"Royal Chest\"\n"
        "c.open()\n"          // Royal Chest opened, 100 gold!
        "System.print(LockType.Magic)\n"  // 2
    );
    check("mixed: typed props + enum + methods all work",
          result == WREN_RESULT_SUCCESS);

    // ══════════════════════════════════════════════════════════════════════
    // ── Mixin tests ──
    // ══════════════════════════════════════════════════════════════════════
    printf("\n--- mixin syntax ---\n");

    // Test: basic mixin definition
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_basic",
        "mixin Greetable {\n"
        "  public greet() {\n"
        "    System.print(\"hello from mixin\")\n"
        "  }\n"
        "}\n"
    );
    check("basic mixin definition compiles",
          result == WREN_RESULT_SUCCESS);

    // Test: mixin with fields
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_fields",
        "mixin Health {\n"
        "  var hp\n"
        "  construct new() {\n"
        "    hp = 100\n"
        "  }\n"
        "  public takeDamage(n) {\n"
        "    hp = hp - n\n"
        "  }\n"
        "  public getHp() { hp }\n"
        "}\n"
    );
    check("mixin with fields compiles",
          result == WREN_RESULT_SUCCESS);

    // Test: class with single mixin
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_single_with",
        "mixin Printable {\n"
        "  public describe() {\n"
        "    System.print(\"I am printable\")\n"
        "  }\n"
        "}\n"
        "class Item with Printable {\n"
        "  construct new() {}\n"
        "}\n"
        "var i = Item.new()\n"
        "i.describe()\n"   // I am printable
    );
    check("class with single mixin works",
          result == WREN_RESULT_SUCCESS);

    // Test: class with superclass + mixin
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_super_with",
        "mixin Audible {\n"
        "  public sound() {\n"
        "    System.print(\"*sound*\")\n"
        "  }\n"
        "}\n"
        "class Base {\n"
        "  construct new() {}\n"
        "  public baseMethod() {\n"
        "    System.print(\"from base\")\n"
        "  }\n"
        "}\n"
        "class Child is Base with Audible {\n"
        "  construct new() { super() }\n"
        "}\n"
        "var c = Child.new()\n"
        "c.baseMethod()\n"   // from base
        "c.sound()\n"        // *sound*
    );
    check("class with superclass + mixin works",
          result == WREN_RESULT_SUCCESS);

    // Test: multiple mixins
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_multi",
        "mixin Flyable {\n"
        "  public fly() { System.print(\"flying\") }\n"
        "}\n"
        "mixin Swimmable {\n"
        "  public swim() { System.print(\"swimming\") }\n"
        "}\n"
        "class Duck with Flyable, Swimmable {\n"
        "  construct new() {}\n"
        "}\n"
        "var d = Duck.new()\n"
        "d.fly()\n"     // flying
        "d.swim()\n"    // swimming
    );
    check("class with multiple mixins works",
          result == WREN_RESULT_SUCCESS);

    // Test: class method overrides mixin method
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_override",
        "mixin Describable {\n"
        "  public describe() { System.print(\"default describe\") }\n"
        "}\n"
        "class Potion with Describable {\n"
        "  construct new() {}\n"
        "  public describe() { System.print(\"red potion\") }\n"
        "}\n"
        "Potion.new().describe()\n"   // red potion
    );
    check("class method overrides mixin method",
          result == WREN_RESULT_SUCCESS);

    // Test: mixin with fields + field offset
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_field_offset",
        "mixin Stamina {\n"
        "  var energy = 50\n"
        "  public getEnergy() { energy }\n"
        "  public tire(n) { energy = energy - n }\n"
        "}\n"
        "class Runner with Stamina {\n"
        "  var speed = 10\n"
        "  construct new() {}\n"
        "  public getSpeed() { speed }\n"
        "}\n"
        "var r = Runner.new()\n"
        "System.print(r.getSpeed())\n"   // 10
        "System.print(r.getEnergy())\n"  // 50
        "r.tire(20)\n"
        "System.print(r.getEnergy())\n"  // 30
    );
    check("mixin fields offset correctly in host class",
          result == WREN_RESULT_SUCCESS);

    // Test: mixin public visibility
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_visibility",
        "mixin Visible {\n"
        "  public show() { System.print(\"visible\") }\n"
        "  secret() { System.print(\"hidden\") }\n"
        "}\n"
        "class Widget with Visible {\n"
        "  construct new() {}\n"
        "}\n"
        "Widget.new().show()\n"   // visible
    );
    check("mixin public method callable from outside",
          result == WREN_RESULT_SUCCESS);

    // Test: mixin private method blocked
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_priv_block",
        "mixin Secretive {\n"
        "  secret() { System.print(\"hidden\") }\n"
        "}\n"
        "class Box with Secretive {\n"
        "  construct new() {}\n"
        "}\n"
        "Box.new().secret()\n"
    );
    check("mixin private method blocked from outside",
          result == WREN_RESULT_RUNTIME_ERROR);
    check("mixin private error mentions 'private'",
          strstr(lastError, "private") != NULL);

    // Test: hasMixin() introspection
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_hasmixin",
        "mixin Healable {\n"
        "  public heal() { System.print(\"healed\") }\n"
        "}\n"
        "mixin Damageable {\n"
        "  public hurt() { System.print(\"ouch\") }\n"
        "}\n"
        "class Hero with Healable {\n"
        "  construct new() {}\n"
        "}\n"
        "var h = Hero.new()\n"
        "System.print(h.hasMixin(Healable))\n"    // true
        "System.print(h.hasMixin(Damageable))\n"  // false
    );
    check("hasMixin() returns correct results",
          result == WREN_RESULT_SUCCESS);

    // Test: hasMixin through inheritance
    lastError[0] = 0;
    result = wrenInterpret(vm, "mixin_hasmixin_inherit",
        "mixin Trackable {\n"
        "  public track() { System.print(\"tracked\") }\n"
        "}\n"
        "class Vehicle with Trackable {\n"
        "  construct new() {}\n"
        "}\n"
        "class Car is Vehicle {\n"
        "  construct new() { super() }\n"
        "}\n"
        "var c = Car.new()\n"
        "System.print(c.hasMixin(Trackable))\n"  // true
    );
    check("hasMixin() works through inheritance",
          result == WREN_RESULT_SUCCESS);

    // ── Summary ──
    printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);

    wrenFreeVM(vm);
    return testsFailed > 0 ? 1 : 0;
}
