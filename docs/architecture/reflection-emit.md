# 反射元数据统一 emit 架构

## 1. 问题陈述

dumper 当前把"反射元数据类型"劈成了两个**不一致**的世界：

| 类型族 | 来源 | 布局怎么决定 |
|---|---|---|
| `UObject` 派生（UObject / UField / UStruct / UEnum / UFunction / UClass） | GUObjectArray walker → `_sdkProcessed` → `augment()` | `UE_Offsets.UObject.*` 等，**运行时**探到的 |
| `FField` 派生（FField / FProperty / FBoolProperty / ...） | preamble 硬编码 in `kUECoreBasicH` | UE 5.x 默认 layout，**编译时**写死 |

直接后果：

- prober 探到的 `FField.ClassPrivate / Next / NamePrivate / FlagsPrivate`、`FProperty.ArrayDim / ElementSize / PropertyFlags / Offset_Internal / Size` 在运行时被 `UEWrappers` 正确使用（property walker、`GetName()`、`GetSize()` 等），但是 dump 出来的 `Basic.h` 里的 `class FField` / `class FProperty` / `class FBoolProperty` 等**仍然是默认 layout**。
- 消费 SDK 的下游代码用 `offsetof(FField, Next)` 拿到的是 `0x18`（默认值），跟运行时探到的实际偏移（可能是 `0x20` 也可能是别的）**不一致**。
- 像 [DeltaForce FBoolProperty 派生类多 8 字节 leading metadata][1] 这种异常布局，walker 在 [`FindSubFPropertyBaseOffset`][2] 里临时浮动决定，但浮动结果**不写回任何地方**，dump 出的 SDK 不知道这事。

[1]: ../../../../docs 之外的 dfm_fboolproperty_layout 记录
[2]: ../../AndUEDumper/src/UE/UEWrappers.cpp `UE_FProperty::FindSubFPropertyBaseOffset`

本质：`UObject` 派生和 `FField` 派生**同构**——都是反射元数据，都依赖 `UE_Offsets` 描述布局，都该走同一条 dumper emit pipeline。"UObject 派生动态 / FField 派生静态" 是历史遗留，不是设计。

## 2. 目标架构

把 augment pipeline 推广成"统一的 reflection metadata emitter"，两阶段：

```
Phase A (synthesize): 不在 GUObjectArray 里的 reflection 类型 →
                      基于 UE_Offsets 合成 UE_UPackage::Struct 条目 →
                      注入 CoreUObject package._sdkProcessed

Phase B (augment):    现有的 fieldsFor / augment 逻辑统一作用于
                      所有 reflection 类型 (合成的 + walker 产出的)
```

副产品：

- preamble 缩到只剩**真·跨版本稳定**的类型（FName / FString / TArray / TMap / FFieldVariant / 各种 enum）
- 把 walker 里的"运行时浮动偏移"（`FindSubFPropertyBaseOffset`、`FEnumProperty` 双布局）**固化进 `UE_Offsets`**，dump 时能拿到
- DeltaForce 派生类 leading metadata 的怪布局可以通过新增探测项 + `UE_Offsets` 字段**干净表达**，不用在 walker 里打补丁
- AndUEProber 的 `Phase5_ProbeFField*` / `Phase5_ProbeFProperty*` 产物**真正端到端用上**，SDK consumer 静态 `offsetof` 与运行时一致

## 3. 反射元数据全集映射表

下表给出**所有需要由 dumper 合成 / augment 的反射类型**，外加每个类型字段的来源（`UE_Offsets` 字段 vs 子类常量 vs 新增探测项）。

### 3.1 已存在于 UE_Offsets 的字段

| 类型 | 字段 | 当前 UE_Offsets 路径 | 大小 |
|---|---|---|---|
| FField | ClassPrivate | `FField.ClassPrivate` | 8 |
| FField | Next | `FField.Next` | 8 |
| FField | NamePrivate | `FField.NamePrivate` | `FName.Size` |
| FField | FlagsPrivate | `FField.FlagsPrivate` | 4 |
| FProperty | ArrayDim | `FProperty.ArrayDim` | 4 |
| FProperty | ElementSize | `FProperty.ElementSize` | 4 |
| FProperty | PropertyFlags | `FProperty.PropertyFlags` | 8 |
| FProperty | Offset_Internal | `FProperty.Offset_Internal` | 4 |
| (sizeof FProperty) | — | `FProperty.Size` | — |

### 3.2 需要新增到 UE_Offsets 的字段

以下都是当前 walker 里"运行时浮动决定但不写回"的偏移，搬进 `UE_Offsets` 之后可以由 Phase A synthesize 直接读到。

| 新增字段 | 含义 | 默认值 | 当前怎么决定 |
|---|---|---|---|
| `FProperty.SubPropertyBase` | FProperty 子类的额外字段相对 FField+FProperty 末尾的起点 | `FProperty.Size`（标准）/`FProperty.Size + 8`（DFM 派生类） | [`FindSubFPropertyBaseOffset`][2] 试探两次 |
| `FEnumProperty.UnderlyingType` | `FEnumProperty::UnderlyingType` 相对 `object` 的偏移 | `FProperty.Size` | [`UE_FEnumProperty::GetUnderlayingProperty`][3] 试探 |
| `FEnumProperty.Enum` | `FEnumProperty::Enum` 相对 `object` 的偏移 | `FProperty.Size + 8` | [`UE_FEnumProperty::GetEnum`][3] 试探 |
| `FFieldClass.Name` | offset 0（FFieldClass 起手就是 FName） | 0 | 保留扩展位 |
| `FFieldClass.Id` | | 8 | 保留扩展位 |
| `FFieldClass.CastFlags` | | 0x10 | 保留扩展位 |
| `FFieldClass.ClassFlags` | | 0x18 | 保留扩展位 |
| `FFieldClass.SuperClass` | | 0x20 | 保留扩展位 |
| `FFieldClass.Size` | sizeof(FFieldClass) | 0x28 | 保留扩展位 |

[3]: ../../AndUEDumper/src/UE/UEWrappers.cpp `UE_FEnumProperty::GetUnderlayingProperty` / `UE_FEnumProperty::GetEnum`

### 3.3 FProperty 子类合成表（完整 13 个）

> 所有子类 `Inherited` 取直接父类的 `Size`，自身段从 `SubPropertyBase` 起算（FBoolProperty 特殊：从 `FProperty.Size` 起算的 uint8 四元组）。

| 子类 | 父类 | 自身字段 | 字段类型 | 字段大小 | 字段相对偏移（相对自身段起点） |
|---|---|---|---|---|---|
| **FStructProperty** | FProperty | Struct | `struct UStruct*` | 8 | `+0` |
| **FObjectPropertyBase** | FProperty | PropertyClass | `struct UClass*` | 8 | `+0` |
| **FClassProperty** | FObjectPropertyBase | MetaClass | `struct UClass*` | 8 | `+0`（注：起点已经是 FObjectPropertyBase 末尾 = SubPropertyBase + 8） |
| **FSoftClassProperty** | FClassProperty | — | — | — | 无新字段，Size = FClassProperty.Size |
| **FArrayProperty** | FProperty | Inner | `struct FProperty*` | 8 | `+0` |
| **FByteProperty** | FProperty | Enum | `struct UEnum*` | 8 | `+0` |
| **FBoolProperty** | FProperty | FieldSize, ByteOffset, ByteMask, FieldMask | `uint8` × 4 | 1 × 4 | `FProperty.Size + 0/1/2/3`（**不**用 SubPropertyBase，FBool 跨版本稳定） |
| **FEnumProperty** | FProperty | UnderlyingType, Enum | `struct FProperty*`, `struct UEnum*` | 8, 8 | `FEnumProperty.UnderlyingType`, `FEnumProperty.Enum`（双布局，新探测项） |
| **FSetProperty** | FProperty | ElementProp | `struct FProperty*` | 8 | `+0` |
| **FMapProperty** | FProperty | KeyProp, ValueProp | `struct FProperty*` × 2 | 8, 8 | `+0`, `+8` |
| **FInterfaceProperty** | FProperty | InterfaceClass | `struct UClass*` | 8 | `+0` |
| **FFieldPathProperty** | FProperty | PropertyName | `FName` | `FName.Size` | `FProperty.Size + 0`（FFieldPath 自身在 PropertyName 之后，但 dumper 这里只需要拿到 PropertyName） |

### 3.4 FField / FFieldClass 本身

| 类型 | 字段 | 来源 | 注 |
|---|---|---|---|
| **FFieldVariant** | Container, bIsUObject | preamble 保留 | `{void*, bool}`，跨版本稳定 |
| **FField** | VTable, Owner, Next, ClassPrivate, NamePrivate, FlagsPrivate | `UE_Offsets.FField.*` + VTable 强 emit `@0`、Owner 强 emit `@8`（sizeof FFieldVariant 后） | 注意源码声明顺序 ≠ 内存顺序，augment 必须按 offset 排序后插 padding |
| **FFieldClass** | Name, Id, CastFlags, ClassFlags, SuperClass | `UE_Offsets.FFieldClass.*`（新增）| layout 跨版本稳定，但留扩展位 |

## 4. Synthesize 阶段 API 设计

### 4.1 入口函数

```cpp
// Dumper.cpp 新增（在 augment() 之前调用）
void UEDumper::SynthesizeReflectionTypes(std::vector<UE_UPackage>& sdkProcessed);
```

调用时机：[Dumper.cpp `dropDups` 块][5] 之后、[Dumper.cpp `augment` 块][4] 之前。放在 dropDups 之后是因为 walker 不会产出 FField/FProperty（它们不是 UObject），dropDups 把保守 drop 表里的 FFieldPath/FFieldClass/FProperty 干掉之后再合成不会误伤；放在 augment 之前是因为 augment 要 fill 合成的空 Struct 的 Members。

[4]: ../../AndUEDumper/src/Dumper.cpp augment block (`fieldsFor` / `augment`)
[5]: ../../AndUEDumper/src/Dumper.cpp dedup block (`kPreambleProvided` / `dropDups`)

### 4.2 内部行为

1. 在 `sdkProcessed` 中定位 CoreUObject package（按 `PackageName == "CoreUObject"`），如果不存在则跳过（保守，因为 dumper 一般保证有 CoreUObject）。
2. 对下面这 16 个 cppName 中**尚未存在于 CoreUObject.Structures 的**，合成一个 `UE_UPackage::Struct` 条目并 push 进 `Structures`：

```
FFieldVariant  (保留 preamble 不合成——见 §6)
FField
FFieldClass
FProperty
FStructProperty FObjectPropertyBase FClassProperty FSoftClassProperty
FArrayProperty FByteProperty FBoolProperty FEnumProperty
FSetProperty FMapProperty FInterfaceProperty FFieldPathProperty
```

3. 合成的 Struct 条目长这样：

```cpp
UE_UPackage::Struct s;
s.Name         = cppNameOnly;               // 同 CppNameOnly，FField 不带 U/A 前缀
s.FullName     = "ScriptStruct CoreUObject." + cppNameOnly;  // 仿 dumper 风格
s.CppNameOnly  = cppNameOnly;
s.SuperCppName = parentCppName;             // "" for FField / FFieldClass，否则填父类
s.CppName      = parentCppName.empty()
                   ? "struct " + cppNameOnly
                   : "struct " + cppNameOnly + " : " + parentCppName;
s.Inherited    = SizeOf(parentCppName);     // 父类 Size
s.Size         = SizeOf(cppNameOnly);       // 见 §3
s.Members      = {};                        // 留空让 augment 填
// FullDeps / ForwardDeps 由 augment 时 ExtractTypeDeps 自动填
```

4. **不**为 FFieldVariant 合成（preamble 提供）。

### 4.3 `SizeOf` 计算规则

每个类型的 Size 在 synthesize 时算出来（augment 不再算）：

```
SizeOf(FFieldClass)         = UE_Offsets.FFieldClass.Size                    (默认 0x28)
SizeOf(FField)              = align(UE_Offsets.FField.FlagsPrivate + 4, 8)
SizeOf(FProperty)           = UE_Offsets.FProperty.Size
SizeOf(FStructProperty)     = SubPropertyBase + 8
SizeOf(FObjectPropertyBase) = SubPropertyBase + 8
SizeOf(FClassProperty)      = SizeOf(FObjectPropertyBase) + 8
SizeOf(FSoftClassProperty)  = SizeOf(FClassProperty)
SizeOf(FArrayProperty)      = SubPropertyBase + 8
SizeOf(FByteProperty)       = SubPropertyBase + 8
SizeOf(FBoolProperty)       = align(FProperty.Size + 4, 8)
SizeOf(FEnumProperty)       = max(FEnumProperty.UnderlyingType, FEnumProperty.Enum) + 8
SizeOf(FSetProperty)        = SubPropertyBase + 8
SizeOf(FMapProperty)        = SubPropertyBase + 16
SizeOf(FInterfaceProperty)  = SubPropertyBase + 8
SizeOf(FFieldPathProperty)  = FProperty.Size + FName.Size
```

其中 `SubPropertyBase = UE_Offsets.FProperty.SubPropertyBase`。

### 4.4 Inherited 一般规则与 FProperty 例外

合成时 `s.Inherited = SizeOf(parent)`（§3.3 默认规则）。但 **FProperty 的 Inherited 需要 clamp 到探到的 `ArrayDim`**：

```
Inherited(FProperty) = min( SizeOf(FField), UE_Offsets.FProperty.ArrayDim )
                     = min( align8(FlagsPrivate + 4), ArrayDim )
```

原因：UE 4.25+ 标准 build 下，编译器把 `FProperty.ArrayDim`（int32，4 字节）塞进 FField 末尾 `FlagsPrivate@0x30+4..0x37` 的 4 字节对齐 pad（合法 C++ 派生类 trailing-padding reuse），所以 prober 实测 `ArrayDim @ 0x34`。但 `SizeOf(FField) = align8(0x34) = 0x38`，augment 把 offset < 0x38 的字段全 erase（[Dumper.cpp augment 块][4]），ArrayDim 直接消失。

DFM-style alt layout 把 ArrayDim 对齐到 0x38，min 是 no-op，行为不变。所以这条 clamp 是单向修正：标准 layout 修好，DFM 不受影响。

> 注：这只调 `Inherited(FProperty)`，**不**改 `SizeOf(FField)` 本身——FField 作为独立结构 emit 时仍然 0x38（C++ 自动按最大成员 8 字节对齐），只是 FProperty 看父类的 "可被复用范围" 比 SizeOf 小 4 字节。

## 5. fieldsFor 扩展

[Dumper.cpp:480 `fieldsFor`][6] 在现有 UObject 派生分支之后追加 reflection 派生分支：

[6]: ../../AndUEDumper/src/Dumper.cpp `fieldsFor` lambda

```cpp
// FField 派生
if (cppName == "FFieldClass")
{
    add(offs.FFieldClass.Name,       fnameSize,       "FName",                 "Name", true);
    add(offs.FFieldClass.Id,         8,               "uint64_t",              "Id");
    add(offs.FFieldClass.CastFlags,  8,               "uint64_t",              "CastFlags");
    add(offs.FFieldClass.ClassFlags, 4,               "EClassFlags",           "ClassFlags");
    add(offs.FFieldClass.SuperClass, 8,               "struct FFieldClass*",   "SuperClass");
}
else if (cppName == "FField")
{
    // VTable + Owner 没探测，按 ABI 固定位置硬 emit（FFieldVariant size 0x10）
    add(0,                              8,            "void**",                "VTable", true);
    add(8,                              16,           "FFieldVariant",         "Owner", true);
    add(offs.FField.ClassPrivate,       8,            "struct FFieldClass*",   "ClassPrivate");
    add(offs.FField.Next,               8,            "struct FField*",        "Next");
    add(offs.FField.NamePrivate,        fnameSize,    "FName",                 "NamePrivate");
    add(offs.FField.FlagsPrivate,       4,            "int32_t",               "FlagsPrivate");
}
else if (cppName == "FProperty")
{
    add(offs.FProperty.ArrayDim,        4,            "int32_t",               "ArrayDim");
    add(offs.FProperty.ElementSize,     4,            "int32_t",               "ElementSize");
    add(offs.FProperty.PropertyFlags,   8,            "uint64_t",              "PropertyFlags");
    add(offs.FProperty.Offset_Internal, 4,            "int32_t",               "Offset_Internal");
}
// FProperty 子类
else if (cppName == "FBoolProperty")
{
    add(offs.FProperty.Size + 0, 1, "uint8_t", "FieldSize", true);
    add(offs.FProperty.Size + 1, 1, "uint8_t", "ByteOffset", true);
    add(offs.FProperty.Size + 2, 1, "uint8_t", "ByteMask", true);
    add(offs.FProperty.Size + 3, 1, "uint8_t", "FieldMask", true);
}
else if (cppName == "FStructProperty")
{
    add(offs.FProperty.SubPropertyBase, 8, "struct UStruct*", "Struct", true);
}
else if (cppName == "FObjectPropertyBase")
{
    add(offs.FProperty.SubPropertyBase, 8, "struct UClass*", "PropertyClass", true);
}
else if (cppName == "FClassProperty")
{
    add(offs.FProperty.SubPropertyBase + 8, 8, "struct UClass*", "MetaClass", true);
}
else if (cppName == "FArrayProperty")
{
    add(offs.FProperty.SubPropertyBase, 8, "struct FProperty*", "Inner", true);
}
else if (cppName == "FByteProperty")
{
    add(offs.FProperty.SubPropertyBase, 8, "struct UEnum*", "Enum", true);
}
else if (cppName == "FEnumProperty")
{
    add(offs.FEnumProperty.UnderlyingType, 8, "struct FProperty*", "UnderlyingType", true);
    add(offs.FEnumProperty.Enum,           8, "struct UEnum*",     "Enum", true);
}
else if (cppName == "FSetProperty")
{
    add(offs.FProperty.SubPropertyBase, 8, "struct FProperty*", "ElementProp", true);
}
else if (cppName == "FMapProperty")
{
    add(offs.FProperty.SubPropertyBase,     8, "struct FProperty*", "KeyProp", true);
    add(offs.FProperty.SubPropertyBase + 8, 8, "struct FProperty*", "ValueProp", true);
}
else if (cppName == "FInterfaceProperty")
{
    add(offs.FProperty.SubPropertyBase, 8, "struct UClass*", "InterfaceClass", true);
}
else if (cppName == "FFieldPathProperty")
{
    add(offs.FProperty.Size, fnameSize, "FName", "PropertyName", true);
}
// FSoftClassProperty 无新字段
```

注意：所有 reflection 派生字段都用 `allowZeroOffset=true`，因为 `FBoolProperty.FieldSize` 在 `FProperty.Size + 0` 是合法的。

## 6. preamble 改造（UECoreEmbed.hpp）

### 6.1 删除的段

从 [UECoreEmbed.hpp:1058 ~ 1180 区间][7] 删除以下 class + static_assert 段：

```
class FField  (1058 ~ 1075)
class FProperty  (1077 ~ 1094)
class FByteProperty  (1097 ~ 1106)
class FBoolProperty  (1108 ~ 1123)
class FObjectPropertyBase  (1125 ~ 1134)
class FClassProperty  (1136 ~ 1144)
class FStructProperty (如有)
class FArrayProperty (如有)
class FMapProperty (如有)
class FSetProperty (如有)
class FInterfaceProperty (如有)
class FFieldPathProperty (如有)
class FEnumProperty (如有)
class FSoftClassProperty (如有)
class FFieldClass  (1027 ~ 1041)
```

[7]: ../../AndUEDumper/src/UECoreEmbed.hpp

### 6.2 保留的段

- `FFieldVariant`（`{void*, bool}` 跨版本稳定）
- `FFieldPath` / `TFieldPath`（容器层，不直接关 reflection layout）
- 所有 `EClassFlags` / `EClassCastFlags` / `EPropertyFlags` 等 enum
- 所有 `FName` / `FString` / `FText` / `TArray` / `TMap` 等基础容器

### 6.3 Dumper.cpp 同步改动

- [Dumper.cpp:430][8] `kPreambleProvided` 删除 `FFieldPath`、`FFieldClass`、`FProperty`：
  ```cpp
  // 改前
  "FScriptInterface", "FFieldPath", "FFieldClass", "FProperty",
  // 改后
  "FScriptInterface",
  ```
  保留 `FFieldPath`（容器，preamble 仍提供）的话单独留下。

[8]: ../../AndUEDumper/src/Dumper.cpp `kPreambleProvided`

## 7. AndUEProber 需要新增的 Phase5 探测项

在 [source/UEProber/UEProber.cpp][9] 内 `Phase5_*` 系列已经有：

- `Phase5_ProbeFFieldNamePrivate`
- `Phase5_ProbeFFieldOwner`
- `Phase5_ProbeFFieldNext`
- `Phase5_ProbeFFieldClassPrivate`
- `Phase5_ProbeFFieldFlagsPrivate`
- `Phase5_ProbeFPropertyArrayDim` / `ElementSize` / `PropertyFlags` / `Offset_Internal`
- `Phase5_ProbeFPropertySize`（其隐患见 memory `probe_fpropsize_strategy.md`）

[9]: ../../../source/UEProber/UEProber.cpp

新增以下探测项（落到 prober 项目，不是 dumper）：

### 7.1 `Phase5_ProbeFPropertySubPropertyBase`

- **锚点**：选一个已知 `UE_FObjectPropertyBase`（例如 BlueprintGeneratedClass 的某个 `UObject*` 属性，或一个已知 USoftObjectProperty），它的 `PropertyClass` 字段必定指向有效 UClass 实例。
- **算法**：在候选偏移 `FProperty.Size` 与 `FProperty.Size + sizeof(void*)` 处分别读 8 字节指针，验证指向的对象 `ClassPrivate` 解出来的 cppName 是 "Class"（说明它是 UClass 实例）。
- **写回**：`UE_Offsets.FProperty.SubPropertyBase`。
- **Bridge 透传**：`DumperBridge.h::ProbedOffsets` 增加 `fpropSubBase`，`DumperBridge.cpp::StartDumpWithProbedOffsets` 追加 `if (offsets.fpropSubBase) probedUEOffsets.FProperty.SubPropertyBase = offsets.fpropSubBase;`。
- **`UEProber.cpp::StartDump`** 追加 `if (HasConfirmed("FProperty::SubPropertyBase")) offsets.fpropSubBase = GetConfirmedOffset("FProperty::SubPropertyBase");`。

### 7.2 `Phase5_ProbeFEnumPropertyLayout`

- **锚点**：选一个已知 `FEnumProperty`（例如某 UObject 上的 `enum class` 字段对应的 FEnumProperty）。
- **算法**：双布局试探：
  - 布局 A：`UnderlyingType @ FProperty.Size + 0`、`Enum @ FProperty.Size + 8`
  - 布局 B：`Enum @ FProperty.Size + 0`、`UnderlyingType @ FProperty.Size + 8`
  - 验证：UnderlyingType 读出来应该是一个有 `FProperty.ElementSize` 字段 ≤ 8 的 FProperty（uint8/uint16/uint32/uint64）；Enum 读出来 `IsA<UE_UEnum>()`。
- **写回**：`UE_Offsets.FEnumProperty.UnderlyingType / .Enum`。
- **Bridge 透传**：同 §7.1 模式。

### 7.3 `Phase5_ProbeFFieldClass`（可选 / 未来扩展）

跨版本默认 layout 稳定，先用默认值，留接口位。

## 8. 实施步骤

每一步独立可验证、可 git commit：

| Step | Repo | 改动 | 验证 |
|---|---|---|---|
| **S0** | AndUEDumper | 加这份文档 | merge 即可 |
| **S1** | AndUEDumper | `UEOffsets.hpp` 加 `FProperty.SubPropertyBase` + `FEnumProperty` + `FFieldClass` 子结构和默认初始化 | 编译通过 + 默认 dump 结果无变化（默认值就是当前 walker 浮动结果） |
| **S2** | AndUEDumper | `Dumper.cpp` 加 `SynthesizeReflectionTypes`，调用点接好，但 `fieldsFor` 还没扩 → 合成的 Struct 字段为空 | dump 出来 CoreUObject Structures 里有空 FField/FProperty/...（验证合成入口对） |
| **S3** | AndUEDumper | `fieldsFor` 扩 16 个分支（§5） | dump 出来 FField/FProperty/FBoolProperty layout 跟当前 preamble 完全一致（默认 layout 验证） |
| **S4** | AndUEDumper | 删 `kUECoreBasicH` 中 FField/FProperty/子类的硬编码段；调整 `kPreambleProvided` | dump 出来 Basic.h 不再有这些类，但 CoreUObject_structs.hpp 有 → SDK 编译通过 |
| **S5** | AndUEProber | 加 `Phase5_ProbeFPropertySubPropertyBase` + bridge 透传 | DeltaForce 跑一发：probed SubPropertyBase 写回，FBoolProperty 派生子类字段偏移**正确** |
| **S6** | AndUEProber | 加 `Phase5_ProbeFEnumPropertyLayout` + bridge 透传 | 一个有 enum 字段的游戏跑一发：dump 出的 FEnumProperty UnderlyingType/Enum 位置正确 |
| **S7** | AndUEDumper | 删 walker 内 `FindSubFPropertyBaseOffset` 之类的运行时浮动逻辑（改为直接读 UE_Offsets） | 没回归 = 偏移已经全走 UE_Offsets |

S1~S4 在 AndUEDumper fork 完成（独立 PR）；S5~S6 在 AndUEProber；S7 收尾。

## 9. 验收标准

- DeltaForce dump 跑 → `Basic.h`（或新位置的 CoreUObject_structs.hpp）里：
  - `static_assert(offsetof(FField, ClassPrivate) == 0xX)` 中 0xX = prober 探到的实际值
  - `FBoolProperty` 字段偏移落在 `FProperty.Size + 0/1/2/3`，与 DFM 的派生类 leading metadata 兼容
- 把 dump 出的 SDK 给一个独立测试程序 link，运行时取 `&((FField*)0)->Next` 与 walker 走出的字段位置一致
- PUBG / Valorant / Arena Breakout 各跑一遍，dump 不退化（默认值兜底正确）
- 不依赖 `kUECoreBasicH` 里 FField/FProperty 硬编码段（删了之后还能编通过）

## 10. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 删除 preamble FField/FProperty 后老 SDK 消费者代码 break | S4 单独一个 commit，写在 release notes 里；rollback 路径清晰 |
| `SubPropertyBase` 探测在没有 FObjectPropertyBase 实例的小游戏里失败 | 兜底回退到 `FProperty.Size`（即当前 `FindSubFPropertyBaseOffset` 的第一个候选） |
| FEnumProperty 双布局探测在游戏没用 enum 时跳过 | 跳过即用默认值，跟当前行为一致 |
| FFieldClass 跨版本 layout 偶发变化没被探测 | 先保留默认值，留出 `UE_Offsets.FFieldClass.*` 字段位以备扩展 |
| 合成的 Struct 跟 walker 产出的 Struct cppName 冲突 | synthesize 前先扫一遍 `Structures` 跳过已存在的（§4.2 第 2 条） |
| Dumper 用 `offs.FFieldClass.*` 但 profile 没初始化（旧 profile） | profile 不传 → 用 §3.2 表里的默认值 |

## 11. 不在本次范围

- FNamePool / FName 内部 layout 重整（与本次 reflection emit 解耦）
- UObject / UField / UStruct / UFunction / UClass augment 路径本身的重构（已经 work，不动）
- AndUEProber 的 prober 算法本身改进（只新增 §7 三个探测项）
- 把 ProcessEvent / GObjects 这些**全局指针**也走 dump 化（属于 runtime wiring，与 layout 解耦）
