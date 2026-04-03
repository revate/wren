// Standalone proof that pre-constructor field-default overrides work correctly.
//
// Compile & run:
//   cc -g -I src/include -o test_pre_constructor_overrides test_pre_constructor_overrides.c build/libwren.a -lm
//   ./test_pre_constructor_overrides
//
// What this proves:
//   1. Field defaults are set via CODE_FIELD_DEFAULT and applied by wrenNewInstance().
//   2. The host can patch classObj->fieldDefaults[] BEFORE calling new(),
//      and the constructor body sees the overridden values.
//   3. The constructor body can still explicitly overwrite an overridden value.
//   4. After restoring the original defaults, new instances get the originals.
//   5. Mixins fields work the same way (appended after class fields).

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "wren.h"

static int testsPassed = 0;
static int testsFailed = 0;
static char printBuffer[4096] = {0};
static int printLen = 0;

static void writeFn(WrenVM* vm, const char* text)
{
    int len = (int)strlen(text);
    if (printLen + len < (int)sizeof(printBuffer) - 1)
    {
        memcpy(printBuffer + printLen, text, len);
        printLen += len;
        printBuffer[printLen] = '\0';
    }
    printf("%s", text);
}

static void errorFn(WrenVM* vm, WrenErrorType type,
                    const char* module, int line, const char* message)
{
    if (type == WREN_ERROR_COMPILE)
        printf("[compile error] %s:%d: %s\n", module, line, message);
    else if (type == WREN_ERROR_RUNTIME)
        printf("[runtime error] %s\n", message);
    else if (type == WREN_ERROR_STACK_TRACE)
        printf("  at %s:%d in %s\n", module, line, message);
}

static void resetPrint(void)
{
    printBuffer[0] = '\0';
    printLen = 0;
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

// Helper: check that printBuffer contains a substring
static int printed(const char* needle)
{
    return strstr(printBuffer, needle) != NULL;
}

// Helper: get a Num from a slot
static double getNum(WrenVM* vm, int slot)
{
    return wrenGetSlotDouble(vm, slot);
}

int main(void)
{
    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.writeFn = writeFn;
    config.errorFn = errorFn;

    WrenVM* vm = wrenNewVM(&config);
    wrenSetDefaultPublic(vm, true);  // class-script style

    printf("=== Pre-Constructor Override Proof Test ===\n\n");

    // ─────────────────────────────────────────────────────────────
    // Define a class with typed field defaults (like Enemy.rvobj)
    // ─────────────────────────────────────────────────────────────
    printf("--- Define Enemy class ---\n");

    const char* enemySource =
        "class Enemy {\n"
        "  // Typed fields with defaults (Revate extension) — need 'public' keyword\n"
        "  public Num maxHp = 100\n"
        "  public Num hp = 100\n"
        "  public Num damage = 10\n"
        "  public Bool isBoss = false\n"
        "  public Num lootValue = 25\n"
        "\n"
        "  construct new(name) {\n"
        "    _name = name\n"
        "    // Constructor body: prints values it SEES\n"
        "    System.print(\"Enemy '%(name)' hp=%(hp) dmg=%(damage) boss=%(isBoss) loot=%(lootValue)\")\n"
        "  }\n"
        "\n"
        "  public name { _name }\n"
        "  // Getters for verification after construction\n"
        "  public getHp { hp }\n"
        "  public getDamage { damage }\n"
        "  public getIsBoss { isBoss }\n"
        "  public getLootValue { lootValue }\n"
        "}\n";

    WrenInterpretResult result = wrenInterpret(vm, "test", enemySource);
    check("Enemy class compiles", result == WREN_RESULT_SUCCESS);

    // ─────────────────────────────────────────────────────────────
    // TEST 1: Default instantiation (no overrides)
    // ─────────────────────────────────────────────────────────────
    printf("\n--- Test 1: Default instantiation (no overrides) ---\n");

    resetPrint();
    result = wrenInterpret(vm, "test",
        "var goblin = Enemy.new(\"Goblin\")\n"
        "System.print(\"POST hp=%(goblin.getHp) dmg=%(goblin.getDamage) boss=%(goblin.getIsBoss) loot=%(goblin.getLootValue)\")\n");
    check("default instantiation succeeds", result == WREN_RESULT_SUCCESS);
    check("constructor sees hp=100", printed("hp=100"));
    check("constructor sees dmg=10", printed("dmg=10"));
    check("constructor sees boss=false", printed("boss=false"));
    check("constructor sees loot=25", printed("loot=25"));
    check("post-construction values correct", printed("POST hp=100 dmg=10 boss=false loot=25"));

    // ─────────────────────────────────────────────────────────────
    // TEST 2: Pre-constructor overrides
    //   Simulates what instantiateClassWithOverrides() does:
    //   1. Save original field defaults
    //   2. Patch classObj->fieldDefaults with override values
    //   3. Call new() — constructor sees overrides
    //   4. Restore original defaults
    // ─────────────────────────────────────────────────────────────
    printf("\n--- Test 2: Pre-constructor overrides (the main proof) ---\n");

    // Get class handle
    wrenEnsureSlots(vm, 4);
    wrenGetVariable(vm, "test", "Enemy", 0);
    WrenHandle* enemyClass = wrenGetSlotHandle(vm, 0);

    int fieldCount = wrenGetClassFieldCount(vm, 0);
    printf("  Enemy field count: %d\n", fieldCount);
    check("Enemy has 5 typed fields + 1 underscore field = 6 total fields", fieldCount == 6);

    // Field layout (class own fields, offset from 0):
    //   0: maxHp = 100
    //   1: hp = 100
    //   2: damage = 10
    //   3: isBoss = false
    //   4: lootValue = 25
    //   5: _name (set by constructor, default null)

    // Step 1: Read and save original defaults
    printf("  Reading original defaults...\n");
    wrenEnsureSlots(vm, 4);
    wrenSetSlotHandle(vm, 0, enemyClass);

    wrenGetFieldDefault(vm, 0, 0, 1); // maxHp
    double origMaxHp = wrenGetSlotDouble(vm, 1);
    check("original maxHp default = 100", fabs(origMaxHp - 100.0) < 0.001);

    wrenGetFieldDefault(vm, 0, 1, 1); // hp
    double origHp = wrenGetSlotDouble(vm, 1);
    check("original hp default = 100", fabs(origHp - 100.0) < 0.001);

    wrenGetFieldDefault(vm, 0, 2, 1); // damage
    double origDmg = wrenGetSlotDouble(vm, 1);
    check("original damage default = 10", fabs(origDmg - 10.0) < 0.001);

    wrenGetFieldDefault(vm, 0, 3, 1); // isBoss
    bool origBoss = wrenGetSlotBool(vm, 1);
    check("original isBoss default = false", origBoss == false);

    wrenGetFieldDefault(vm, 0, 4, 1); // lootValue
    double origLoot = wrenGetSlotDouble(vm, 1);
    check("original lootValue default = 25", fabs(origLoot - 25.0) < 0.001);

    // Step 2: Patch field defaults with "DragonBoss" overrides
    printf("  Patching field defaults for DragonBoss...\n");
    wrenEnsureSlots(vm, 4);
    wrenSetSlotHandle(vm, 0, enemyClass);

    wrenSetSlotDouble(vm, 1, 1000.0);
    wrenSetFieldDefault(vm, 0, 0, 1); // maxHp = 1000

    wrenSetSlotDouble(vm, 1, 1000.0);
    wrenSetFieldDefault(vm, 0, 1, 1); // hp = 1000

    wrenSetSlotDouble(vm, 1, 75.0);
    wrenSetFieldDefault(vm, 0, 2, 1); // damage = 75

    wrenSetSlotBool(vm, 1, true);
    wrenSetFieldDefault(vm, 0, 3, 1); // isBoss = true

    wrenSetSlotDouble(vm, 1, 500.0);
    wrenSetFieldDefault(vm, 0, 4, 1); // lootValue = 500

    // Step 3: Instantiate — constructor should see the overridden values
    resetPrint();
    result = wrenInterpret(vm, "test",
        "var dragon = Enemy.new(\"DragonBoss\")\n"
        "System.print(\"POST hp=%(dragon.getHp) dmg=%(dragon.getDamage) boss=%(dragon.getIsBoss) loot=%(dragon.getLootValue)\")\n");
    check("override instantiation succeeds", result == WREN_RESULT_SUCCESS);
    check("constructor sees hp=1000 (overridden!)", printed("hp=1000"));
    check("constructor sees dmg=75 (overridden!)", printed("dmg=75"));
    check("constructor sees boss=true (overridden!)", printed("boss=true"));
    check("constructor sees loot=500 (overridden!)", printed("loot=500"));
    check("post-construction values correct", printed("POST hp=1000 dmg=75 boss=true loot=500"));

    // Step 4: Restore original defaults
    printf("  Restoring original defaults...\n");
    wrenEnsureSlots(vm, 4);
    wrenSetSlotHandle(vm, 0, enemyClass);

    wrenSetSlotDouble(vm, 1, origMaxHp);
    wrenSetFieldDefault(vm, 0, 0, 1);

    wrenSetSlotDouble(vm, 1, origHp);
    wrenSetFieldDefault(vm, 0, 1, 1);

    wrenSetSlotDouble(vm, 1, origDmg);
    wrenSetFieldDefault(vm, 0, 2, 1);

    wrenSetSlotBool(vm, 1, origBoss);
    wrenSetFieldDefault(vm, 0, 3, 1);

    wrenSetSlotDouble(vm, 1, origLoot);
    wrenSetFieldDefault(vm, 0, 4, 1);

    // ─────────────────────────────────────────────────────────────
    // TEST 3: After restoring, new instances get original defaults
    // ─────────────────────────────────────────────────────────────
    printf("\n--- Test 3: Post-restore instantiation (originals back) ---\n");

    resetPrint();
    result = wrenInterpret(vm, "test",
        "var goblin2 = Enemy.new(\"Goblin2\")\n"
        "System.print(\"POST hp=%(goblin2.getHp) dmg=%(goblin2.getDamage)\")\n");
    check("post-restore instantiation succeeds", result == WREN_RESULT_SUCCESS);
    check("restored: constructor sees hp=100", printed("hp=100"));
    check("restored: constructor sees dmg=10", printed("dmg=10"));
    check("restored: post-construction hp=100", printed("POST hp=100"));

    // ─────────────────────────────────────────────────────────────
    // TEST 4: Constructor can OVERWRITE an overridden value
    //   Proves the constructor body still has full control.
    // ─────────────────────────────────────────────────────────────
    printf("\n--- Test 4: Constructor overwrites an overridden value ---\n");

    const char* overwriteSource =
        "class Warrior {\n"
        "  public Num hp = 50\n"
        "  public Num damage = 5\n"
        "  public Num armor = 0\n"
        "\n"
        "  construct new(name) {\n"
        "    _name = name\n"
        "    // Constructor explicitly sets armor to 999 regardless of override\n"
        "    armor = 999\n"
        "    System.print(\"Warrior '%(name)' hp=%(hp) dmg=%(damage) armor=%(armor)\")\n"
        "  }\n"
        "\n"
        "  public name { _name }\n"
        "  public getHp { hp }\n"
        "  public getDamage { damage }\n"
        "  public getArmor { armor }\n"
        "}\n";

    result = wrenInterpret(vm, "test", overwriteSource);
    check("Warrior class compiles", result == WREN_RESULT_SUCCESS);

    // Get Warrior class and patch defaults
    wrenEnsureSlots(vm, 4);
    wrenGetVariable(vm, "test", "Warrior", 0);
    WrenHandle* warriorClass = wrenGetSlotHandle(vm, 0);

    int wFieldCount = wrenGetClassFieldCount(vm, 0);
    printf("  Warrior field count: %d\n", wFieldCount);
    check("Warrior has 3 typed fields + 1 underscore = 4 total", wFieldCount == 4);

    // Save originals
    wrenGetFieldDefault(vm, 0, 0, 1); // hp
    double wOrigHp = wrenGetSlotDouble(vm, 1);
    wrenGetFieldDefault(vm, 0, 1, 1); // damage
    double wOrigDmg = wrenGetSlotDouble(vm, 1);
    wrenGetFieldDefault(vm, 0, 2, 1); // armor
    double wOrigArmor = wrenGetSlotDouble(vm, 1);

    // Patch: hp=200, damage=50, armor=100
    wrenEnsureSlots(vm, 4);
    wrenSetSlotHandle(vm, 0, warriorClass);

    wrenSetSlotDouble(vm, 1, 200.0);
    wrenSetFieldDefault(vm, 0, 0, 1); // hp = 200

    wrenSetSlotDouble(vm, 1, 50.0);
    wrenSetFieldDefault(vm, 0, 1, 1); // damage = 50

    wrenSetSlotDouble(vm, 1, 100.0);
    wrenSetFieldDefault(vm, 0, 2, 1); // armor = 100 (will be overwritten by ctor!)

    // Instantiate
    resetPrint();
    result = wrenInterpret(vm, "test",
        "var tank = Warrior.new(\"Tank\")\n"
        "System.print(\"POST hp=%(tank.getHp) dmg=%(tank.getDamage) armor=%(tank.getArmor)\")\n");
    check("overwrite instantiation succeeds", result == WREN_RESULT_SUCCESS);

    // hp and damage should be the overrides (200, 50)
    check("constructor sees hp=200 (override held)", printed("hp=200"));
    check("constructor sees dmg=50 (override held)", printed("dmg=50"));

    // armor should be 999 — constructor explicitly overwrote the 100 override
    check("constructor overwrites armor to 999 (ctor wins!)", printed("armor=999"));
    check("post-construction values correct", printed("POST hp=200 dmg=50 armor=999"));

    // Restore Warrior defaults
    wrenEnsureSlots(vm, 4);
    wrenSetSlotHandle(vm, 0, warriorClass);
    wrenSetSlotDouble(vm, 1, wOrigHp);
    wrenSetFieldDefault(vm, 0, 0, 1);
    wrenSetSlotDouble(vm, 1, wOrigDmg);
    wrenSetFieldDefault(vm, 0, 1, 1);
    wrenSetSlotDouble(vm, 1, wOrigArmor);
    wrenSetFieldDefault(vm, 0, 2, 1);

    // ─────────────────────────────────────────────────────────────
    // TEST 5: Mixin fields are also overrideable
    // ─────────────────────────────────────────────────────────────
    printf("\n--- Test 5: Mixin field overrides ---\n");

    const char* mixinSource =
        "mixin Health {\n"
        "  public Num maxHp = 100\n"
        "  public Num hp = 100\n"
        "}\n"
        "\n"
        "mixin Lootable {\n"
        "  public Num lootValue = 25\n"
        "}\n"
        "\n"
        "class Monster is Object with Health, Lootable {\n"
        "  public Num damage = 10\n"
        "  public Bool isBoss = false\n"
        "\n"
        "  construct new(name) {\n"
        "    _name = name\n"
        "    System.print(\"Monster '%(name)' maxHp=%(maxHp) hp=%(hp) dmg=%(damage) boss=%(isBoss) loot=%(lootValue)\")\n"
        "  }\n"
        "\n"
        "  public name { _name }\n"
        "  public getMaxHp { maxHp }\n"
        "  public getHp { hp }\n"
        "  public getDamage { damage }\n"
        "  public getIsBoss { isBoss }\n"
        "  public getLootValue { lootValue }\n"
        "}\n";

    result = wrenInterpret(vm, "test", mixinSource);
    check("Monster with mixins compiles", result == WREN_RESULT_SUCCESS);

    // Get Monster class
    wrenEnsureSlots(vm, 4);
    wrenGetVariable(vm, "test", "Monster", 0);
    WrenHandle* monsterClass = wrenGetSlotHandle(vm, 0);

    int mFieldCount = wrenGetClassFieldCount(vm, 0);
    printf("  Monster field count: %d\n", mFieldCount);
    // Class fields: damage(0), isBoss(1), _name(2)
    // Mixin Health fields: maxHp(3), hp(4)
    // Mixin Lootable fields: lootValue(5)
    check("Monster has 6 total fields (3 class + 2 Health + 1 Lootable)", mFieldCount == 6);

    // Read defaults to find our field layout
    printf("  Probing field layout...\n");
    for (int i = 0; i < mFieldCount; i++)
    {
        wrenEnsureSlots(vm, 4);
        wrenSetSlotHandle(vm, 0, monsterClass);
        wrenGetFieldDefault(vm, 0, i, 1);
        WrenType t = wrenGetSlotType(vm, 1);
        if (t == WREN_TYPE_NUM)
            printf("    field[%d] = %.1f (Num)\n", i, wrenGetSlotDouble(vm, 1));
        else if (t == WREN_TYPE_BOOL)
            printf("    field[%d] = %s (Bool)\n", i, wrenGetSlotBool(vm, 1) ? "true" : "false");
        else if (t == WREN_TYPE_NULL)
            printf("    field[%d] = null\n", i);
        else
            printf("    field[%d] = <type %d>\n", i, t);
    }

    // Default instantiation first
    resetPrint();
    result = wrenInterpret(vm, "test",
        "var slime = Monster.new(\"Slime\")\n");
    check("default Monster instantiation works", result == WREN_RESULT_SUCCESS);
    check("mixin: default maxHp=100", printed("maxHp=100"));
    check("mixin: default dmg=10", printed("dmg=10"));
    check("mixin: default loot=25", printed("loot=25"));

    // Now patch: class fields first, then mixin fields
    // We need to figure out the actual layout — let's read the defaults
    // Patch damage (field 0) = 75, isBoss (field 1) = true
    // Patch maxHp (field 3) = 500, hp (field 4) = 500
    // Patch lootValue (field 5) = 250
    printf("  Patching for BossMonster...\n");

    // Save all originals
    double mOrigDefaults[6];
    bool mOrigBool[6];
    WrenType mOrigTypes[6];
    for (int i = 0; i < mFieldCount; i++)
    {
        wrenEnsureSlots(vm, 4);
        wrenSetSlotHandle(vm, 0, monsterClass);
        wrenGetFieldDefault(vm, 0, i, 1);
        mOrigTypes[i] = wrenGetSlotType(vm, 1);
        if (mOrigTypes[i] == WREN_TYPE_NUM)
            mOrigDefaults[i] = wrenGetSlotDouble(vm, 1);
        else if (mOrigTypes[i] == WREN_TYPE_BOOL)
            mOrigBool[i] = wrenGetSlotBool(vm, 1);
    }

    // Patch overrides
    wrenEnsureSlots(vm, 4);
    wrenSetSlotHandle(vm, 0, monsterClass);

    wrenSetSlotDouble(vm, 1, 75.0);
    wrenSetFieldDefault(vm, 0, 0, 1); // damage = 75

    wrenSetSlotBool(vm, 1, true);
    wrenSetFieldDefault(vm, 0, 1, 1); // isBoss = true

    wrenSetSlotDouble(vm, 1, 500.0);
    wrenSetFieldDefault(vm, 0, 3, 1); // maxHp = 500

    wrenSetSlotDouble(vm, 1, 500.0);
    wrenSetFieldDefault(vm, 0, 4, 1); // hp = 500

    wrenSetSlotDouble(vm, 1, 250.0);
    wrenSetFieldDefault(vm, 0, 5, 1); // lootValue = 250

    // Instantiate with overrides
    resetPrint();
    result = wrenInterpret(vm, "test",
        "var boss = Monster.new(\"BossMonster\")\n"
        "System.print(\"POST maxHp=%(boss.getMaxHp) hp=%(boss.getHp) dmg=%(boss.getDamage) boss=%(boss.getIsBoss) loot=%(boss.getLootValue)\")\n");
    check("override Monster instantiation works", result == WREN_RESULT_SUCCESS);
    check("mixin override: constructor sees maxHp=500", printed("maxHp=500"));
    check("mixin override: constructor sees hp=500", printed("hp=500"));
    check("mixin override: constructor sees dmg=75", printed("dmg=75"));
    check("mixin override: constructor sees boss=true", printed("boss=true"));
    check("mixin override: constructor sees loot=250", printed("loot=250"));
    check("mixin override: POST values correct", printed("POST maxHp=500 hp=500 dmg=75 boss=true loot=250"));

    // Restore all originals
    for (int i = 0; i < mFieldCount; i++)
    {
        wrenEnsureSlots(vm, 4);
        wrenSetSlotHandle(vm, 0, monsterClass);
        if (mOrigTypes[i] == WREN_TYPE_NUM)
        {
            wrenSetSlotDouble(vm, 1, mOrigDefaults[i]);
            wrenSetFieldDefault(vm, 0, i, 1);
        }
        else if (mOrigTypes[i] == WREN_TYPE_BOOL)
        {
            wrenSetSlotBool(vm, 1, mOrigBool[i]);
            wrenSetFieldDefault(vm, 0, i, 1);
        }
        else if (mOrigTypes[i] == WREN_TYPE_NULL)
        {
            wrenSetSlotNull(vm, 1);
            wrenSetFieldDefault(vm, 0, i, 1);
        }
    }

    // Verify restored
    resetPrint();
    result = wrenInterpret(vm, "test",
        "var slime2 = Monster.new(\"Slime2\")\n");
    check("post-restore Monster works", result == WREN_RESULT_SUCCESS);
    check("mixin: restored maxHp=100", printed("maxHp=100"));
    check("mixin: restored dmg=10", printed("dmg=10"));
    check("mixin: restored loot=25", printed("loot=25"));

    // ─────────────────────────────────────────────────────────────
    // TEST 6: Direct instance field read/write (wrenGetInstanceField / wrenSetInstanceField)
    // ─────────────────────────────────────────────────────────────
    printf("\n--- Test 6: Direct instance field access ---\n");

    // Get the dragon instance from earlier
    wrenEnsureSlots(vm, 4);
    wrenGetVariable(vm, "test", "dragon", 0);

    // dragon's fields were: maxHp=1000, hp=1000, damage=75, isBoss=true, lootValue=500
    wrenGetInstanceField(vm, 0, 0, 1); // maxHp
    check("instance field read: dragon maxHp=1000", fabs(wrenGetSlotDouble(vm, 1) - 1000.0) < 0.001);

    wrenGetInstanceField(vm, 0, 2, 1); // damage
    check("instance field read: dragon damage=75", fabs(wrenGetSlotDouble(vm, 1) - 75.0) < 0.001);

    wrenGetInstanceField(vm, 0, 3, 1); // isBoss
    check("instance field read: dragon isBoss=true", wrenGetSlotBool(vm, 1) == true);

    // Directly write a field on the instance
    wrenEnsureSlots(vm, 4);
    wrenGetVariable(vm, "test", "dragon", 0);
    wrenSetSlotDouble(vm, 1, 42.0);
    wrenSetInstanceField(vm, 0, 2, 1); // damage = 42

    // Verify the write took effect in Wren
    resetPrint();
    result = wrenInterpret(vm, "test",
        "System.print(\"dragon dmg after C write: %(dragon.getDamage)\")\n");
    check("C-side instance field write works", printed("dragon dmg after C write: 42"));

    // ─────────────────────────────────────────────────────────────
    // Clean up
    // ─────────────────────────────────────────────────────────────
    wrenReleaseHandle(vm, enemyClass);
    wrenReleaseHandle(vm, warriorClass);
    wrenReleaseHandle(vm, monsterClass);
    wrenFreeVM(vm);

    // ─────────────────────────────────────────────────────────────
    // Summary
    // ─────────────────────────────────────────────────────────────
    printf("\n========================================\n");
    printf("  %d passed, %d failed, %d total\n",
           testsPassed, testsFailed, testsPassed + testsFailed);
    printf("========================================\n");

    if (testsFailed > 0)
    {
        printf("\n*** FAILURES DETECTED ***\n");
        return 1;
    }

    printf("\n  ALL TESTS PASSED — Pre-constructor overrides PROVEN.\n");
    printf("  Key results:\n");
    printf("    - Field defaults applied by wrenNewInstance (no compiler preamble)\n");
    printf("    - Host patches fieldDefaults[] → constructor body sees overrides\n");
    printf("    - Constructor can still explicitly overwrite override values\n");
    printf("    - After restoring defaults, new instances get originals\n");
    printf("    - Mixin fields (appended after class fields) also overrideable\n");
    printf("    - Direct instance field read/write works from C host\n");
    return 0;
}
