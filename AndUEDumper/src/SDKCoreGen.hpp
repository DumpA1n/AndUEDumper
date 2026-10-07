#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace sdkcoregen
{

struct FNameLayout
{
    int32_t Size = 8;
    int32_t ComparisonIndex = 0;
    int32_t DisplayIndex = 0;
    int32_t Number = 4;
    bool CasePreserving = false;
    bool OutlineNumber = false;
};

// Drives the emitted FUObjectItem / FUObjectArray. Values come from the probed
// UE_Offsets so the SDK object array matches the dumped engine version:
//   ItemStride          = FUObjectItem.Size  (0x10 on 4.11-4.20, 0x18 on 4.22+)
//   ObjectOffset        = FUObjectItem.Object (0 except UE5.7+)
//   NumElementsPerChunk = 0 => flat (FFixedUObjectArray, <=4.20), else chunked (>=4.21)
struct UObjectArrayLayout
{
    int32_t ItemStride = 0x18;
    int32_t ObjectOffset = 0;
    int32_t NumElementsPerChunk = 0x10000;
    // FUObjectArray / TUObjectArray field offsets, so the emitted struct OVERLAYS the
    // live (possibly reordered) game memory directly — no host-side synthetic remap.
    // -1 = use the canonical UE offset (output unchanged for non-reordered games).
    int32_t ObjObjectsOffset = -1;   // ObjObjects within FUObjectArray      (canon 0x10)
    int32_t ObjectsOffset = -1;      // Objects within TUObjectArray         (canon 0x0)
    int32_t MaxElementsOffset = -1;  // MaxElements   (canon: fixed 0x8 / chunked 0x10)
    int32_t NumElementsOffset = -1;  // NumElements   (canon: fixed 0xC / chunked 0x14)
    int32_t MaxChunksOffset = -1;    // MaxChunks within TUObjectArray       (canon 0x18)
    int32_t NumChunksOffset = -1;    // NumChunks within TUObjectArray       (canon 0x1C)
};

// Drives the emitted FTextImpl::FTextData, the ITextData that FText::TextData
// points at. Each ITextData implementation keeps its display string in a different
// member, so when GetDisplayStringIndex names the vtable slot of the virtual
// ITextData::GetDisplayString, FText calls it. Otherwise FText reads TextSource,
// the source string that the base text history embeds at a fixed offset.
struct FTextLayout
{
    int32_t TextSource = 0x28;
    int32_t GetDisplayStringIndex = -1;
};

// Sizes this engine reports for the types Basic.h / UnrealContainers.h hand-write, keyed
// by the name the package generator declares members with.
//
// Basic.h declares those types in C++; the dumped reflection states how wide the engine
// actually made them. The two must agree, or every generated member declared after one of
// them lands at an offset the dump never claimed. Each value is emitted as a static_assert
// over the corresponding C++ type (GenCoreTypeGuards), so a disagreement fails the build.
//
// Values are read off the generated member tables (ObserveCoreTypeSizes): a member
// declared with one of these types records the width this engine gave it. 0 means the dump
// declared no member of that type, and no assert is emitted for it.
struct CoreTypeSizes
{
    int32_t FSoftObjectPath = 0;
    int32_t TSoftObjectPtr = 0;
    int32_t TSoftClassPtr = 0;
    int32_t TLazyObjectPtr = 0;
    int32_t TWeakObjectPtr = 0;
    int32_t TArray = 0;
    int32_t TMap = 0;
    int32_t TSet = 0;
    int32_t TScriptInterface = 0;
    int32_t TFieldPath = 0;
    int32_t FString = 0;
    int32_t FText = 0;
    int32_t FName = 0;
    int32_t FDelegate = 0;
    int32_t FMulticastInlineDelegate = 0;
    int32_t FMulticastSparseDelegate = 0;
};

// UnrealContainers.h hand-writes FString as TArray<char16_t>: a 64-bit Data pointer plus
// NumElements and MaxElements. GenCoreTypeGuards asserts it against the dumped
// StrProperty element size.
constexpr int32_t kFStringSize = 0x10;

namespace detail
{
inline std::string hx(unsigned long v)
{
    char b[20];
    std::snprintf(b, sizeof b, "0x%lX", v);
    return b;
}
inline int32_t alignUp(int32_t v, int32_t a) { return (v + a - 1) & ~(a - 1); }
}  // namespace detail

// Per-game config macros spliced at the top of Basic.h (@@SDK_GEN_CONFIG@@). Real
// UE macro names so the macro-guarded core types resolve to this engine build 1:1.
inline std::string GenSDKConfigDefines(const FNameLayout& L)
{
    std::string s;
    s += "// Engine layout selectors (probed). Drive the macro-guarded core types below.\n";
    s += std::string("#define WITH_CASE_PRESERVING_NAME ") + (L.CasePreserving ? "1" : "0") + "\n";
    s += std::string("#define UE_FNAME_OUTLINE_NUMBER   ") + (L.OutlineNumber ? "1" : "0") + "\n";
    return s;
}

// FName emitted 1:1 with UE source: #if-guarded fields + getters keyed on the
// macros above, no padding (real FName is always tightly packed at 0/4/8).
//
// UE reorganized FName at 5.1: `Number` moved from after `DisplayIndex` (UE<=5.0)
// to before it (UE>=5.1) and became gated by UE_FNAME_OUTLINE_NUMBER. The order is
// only observable in memory when both fields exist (CasePreserving && !OutlineNumber);
// the probed offsets disambiguate. OutlineNumber is UE5-only so it forces the new
// order; everything else keeps the UE4 form (unconditional Number) — byte-identical
// for the non-CP case and matching UE4.25-27 mobile targets verbatim.
inline std::string GenFName(const FNameLayout& L)
{
    using detail::hx;

    auto field = [](const char* type, const char* name) {
        char line[160];
        std::snprintf(line, sizeof line, "\t%-44s %s;\n", type, name);
        return std::string(line);
    };
    const std::string cmpField     = field("int32", "ComparisonIndex");
    const std::string displayField = field("int32", "DisplayIndex");
    const std::string numberField  = field("uint32", "Number");

    const bool ue5Order =
        L.OutlineNumber || (L.CasePreserving && !L.OutlineNumber && L.Number < L.DisplayIndex);

    std::string s = "class FName final\n{\npublic:\n";
    s += "\tstatic inline std::function<std::string(uint32)> s_NameResolver;\n\n";

    s += cmpField;
    if (ue5Order)
    {
        s += "#if !UE_FNAME_OUTLINE_NUMBER\n" + numberField + "#endif\n";
        s += "#if WITH_CASE_PRESERVING_NAME\n" + displayField + "#endif\n";
    }
    else
    {
        s += "#if WITH_CASE_PRESERVING_NAME\n" + displayField + "#endif\n";
        s += numberField;
    }

    s += "\npublic:\n";
    s += "#if WITH_CASE_PRESERVING_NAME\n";
    s += "\tint32 GetDisplayIndex() const { return DisplayIndex; }\n";
    s += "#else\n";
    s += "\tint32 GetDisplayIndex() const { return ComparisonIndex; }\n";
    s += "#endif\n";
    s += "\tint32 GetComparisonIndex() const { return ComparisonIndex; }\n";
    s += "#if UE_FNAME_OUTLINE_NUMBER\n";
    s += "\tint32 GetNumber() const { return 0; }\n";
    s += "#else\n";
    s += "\tint32 GetNumber() const { return Number; }\n";
    s += "#endif\n";
    s += R"BODY(
	static std::string GetPlainANSIString(const FName* Name)
	{
		if (s_NameResolver)
			return s_NameResolver(Name->GetDisplayIndex());
		return {};
	}

	std::string GetRawString() const
	{
		return GetPlainANSIString(this);
	}

	std::string ToString() const
	{
		std::string OutputString = GetRawString();
		size_t pos = OutputString.rfind('/');
		if (pos == std::string::npos)
			return OutputString;
		return OutputString.substr(pos + 1);
	}

)BODY";
    s += "\tbool operator==(const FName& Other) const\n";
    s += "\t{\n";
    s += "\t\treturn ComparisonIndex == Other.ComparisonIndex\n";
    s += "#if !UE_FNAME_OUTLINE_NUMBER\n";
    s += "\t\t\t&& Number == Other.Number\n";
    s += "#endif\n";
    s += "\t\t;\n";
    s += "\t}\n";
    s += "\tbool operator!=(const FName& Other) const { return !(*this == Other); }\n";
    s += "};\n";

    s += "static_assert(alignof(FName) == 0x4, \"Wrong alignment on FName\");\n";
    s += "static_assert(sizeof(FName) == " + hx(L.Size) + ", \"Wrong size on FName\");\n";
    s += "static_assert(offsetof(FName, ComparisonIndex) == " + hx(L.ComparisonIndex) + ", \"Member 'FName::ComparisonIndex' has a wrong offset!\");\n";
    s += "#if WITH_CASE_PRESERVING_NAME\n";
    s += "static_assert(offsetof(FName, DisplayIndex) == " + hx(L.DisplayIndex) + ", \"Member 'FName::DisplayIndex' has a wrong offset!\");\n";
    s += "#endif\n";
    // UE4 form keeps Number unconditional; UE5 form gates it on the outline macro.
    if (ue5Order)
        s += "#if !UE_FNAME_OUTLINE_NUMBER\n";
    s += "static_assert(offsetof(FName, Number) == " + hx(L.Number) + ", \"Member 'FName::Number' has a wrong offset!\");\n";
    if (ue5Order)
        s += "#endif\n";
    return s;
}

// FScriptDelegate: FWeakObjectPtr(8) + FName. Both 4-aligned, no padding, so
// size is exactly 8 + FNameSize.
inline std::string GenFScriptDelegate(const FNameLayout& L)
{
    using detail::hx;
    std::string s;
    s += "struct FScriptDelegate\n{\npublic:\n";
    s += "\tFWeakObjectPtr                                Object;\n";
    s += "\tFName                                         FunctionName;\n";
    s += "};\n";
    s += "static_assert(alignof(FScriptDelegate) == 0x4, \"Wrong alignment on FScriptDelegate\");\n";
    s += "static_assert(sizeof(FScriptDelegate) == " + hx(0x8 + L.Size) + ", \"Wrong size on FScriptDelegate\");\n";
    s += "static_assert(offsetof(FScriptDelegate, Object) == 0x0, \"Member 'FScriptDelegate::Object' has a wrong offset!\");\n";
    s += "static_assert(offsetof(FScriptDelegate, FunctionName) == 0x8, \"Member 'FScriptDelegate::FunctionName' has a wrong offset!\");\n";
    return s;
}

// FSoftObjectPath: { FName AssetPathName; <pad>; FString SubPathString; }.
//
// The two members are laid out back to back, SubPathString at the first 8-aligned offset
// past FName, so the stock 8-byte FName leaves no padding and makes the struct 0x18; a
// 12-byte case-preserving FName pushes SubPathString to 0x10 and the struct to 0x20.
//
// This size reaches far beyond the struct itself. FSoftObjectPtr embeds it as
// TPersistentObjectPtr's ObjectID, so every TSoftObjectPtr and TSoftClassPtr member the
// package generator emits is sized from it, and an over-wide path shifts every member
// declared after such a pointer — once per occurrence and per nesting level. The
// generated headers only document their offsets in comments, so the assert below is what
// makes that detectable: reflectedSize is the dumped Size of CoreUObject.SoftObjectPath,
// and asserting the C++ struct against it turns any disagreement into a build error
// instead of silently relocated members. reflectedSize of 0 (no such ScriptStruct in the
// dump) falls back to pinning the derived layout.
inline std::string GenFSoftObjectPath(const FNameLayout& L, int32_t reflectedSize)
{
    using detail::hx;
    const int32_t subOff = detail::alignUp(L.Size, 8);
    const int32_t pad = subOff - L.Size;
    const int32_t derived = subOff + kFStringSize;
    const int32_t assertSize = reflectedSize > 0 ? reflectedSize : derived;

    std::string s;
    s += "namespace FakeSoftObjectPtr\n{\n";
    s += "struct FSoftObjectPath\n{\npublic:\n";
    s += "\tclass FName                                   AssetPathName;\n";
    if (pad > 0)
        s += "\tuint8                                         Pad_" + hx(L.Size) + "[" + hx(pad) + "];\n";
    s += "\tclass FString                                 SubPathString;\n";
    s += "};\n";
    s += "static_assert(alignof(FSoftObjectPath) == 0x8, \"Wrong alignment on FSoftObjectPath\");\n";
    s += "static_assert(sizeof(FSoftObjectPath) == " + hx(assertSize) +
         ", \"FSoftObjectPath size mismatch vs dumped CoreUObject.SoftObjectPath — "
         "every TSoftObjectPtr member would shift; re-dump SDK\");\n";
    s += "static_assert(offsetof(FSoftObjectPath, AssetPathName) == 0x0, \"Member 'FSoftObjectPath::AssetPathName' has a wrong offset!\");\n";
    s += "static_assert(offsetof(FSoftObjectPath, SubPathString) == " + hx(subOff) + ", \"Member 'FSoftObjectPath::SubPathString' has a wrong offset!\");\n";
    s += "}\n";
    return s;
}

// Layout guards for the types Basic.h and UnrealContainers.h hand-write, emitted at the
// end of Basic.h where all of them are in scope.
//
// Each assert pins one C++ type against the width the dumped engine reported for it. The
// generated package headers place members at dumped offsets but declare them with these
// C++ types, so the two descriptions of a type have to agree: when they do not, the
// compiler lays out every following member at an offset the dump never claimed, and
// typed member access reads unrelated bytes. These asserts are the only thing standing
// between that and a silently wrong SDK, so a new engine whose containers differ fails
// to build rather than producing drifted offsets.
//
// A size the dump did not report is skipped — asserting a type against a figure this run
// never observed would fail for reasons unrelated to the engine.
inline std::string GenCoreTypeGuards(const CoreTypeSizes& S)
{
    using detail::hx;

    // Instantiated with int32 / UObject / FProperty because only the wrapper's own fields
    // contribute to its size; those stay incomplete here and the templates hold no member
    // of the argument type. `dumped` names the generated member type the size was read
    // from, so a failure points at the declarations that disagree.
    struct Guard { const char* cppType; int32_t size; const char* dumped; };
    const Guard guards[] = {
        {"FakeSoftObjectPtr::FSoftObjectPath", S.FSoftObjectPath, "FSoftObjectPath"},
        {"FSoftObjectPtr",                     S.TSoftObjectPtr,  "TSoftObjectPtr"},
        {"TSoftObjectPtr<class UObject>",      S.TSoftObjectPtr,  "TSoftObjectPtr"},
        {"TSoftClassPtr<class UObject>",       S.TSoftClassPtr,   "TSoftClassPtr"},
        {"TLazyObjectPtr<class UObject>",      S.TLazyObjectPtr,  "TLazyObjectPtr"},
        {"FWeakObjectPtr",                     S.TWeakObjectPtr,  "TWeakObjectPtr"},
        {"TWeakObjectPtr<class UObject>",      S.TWeakObjectPtr,  "TWeakObjectPtr"},
        {"TArray<int32>",                      S.TArray,          "TArray"},
        {"TMap<int32, int32>",                 S.TMap,            "TMap"},
        {"TSet<int32>",                        S.TSet,            "TSet"},
        {"FScriptInterface",                   S.TScriptInterface, "TScriptInterface"},
        {"TScriptInterface<class UObject>",    S.TScriptInterface, "TScriptInterface"},
        {"FFieldPath",                         S.TFieldPath,      "TFieldPath"},
        {"TFieldPath<class FProperty>",        S.TFieldPath,      "TFieldPath"},
        {"FString",                            S.FString,         "FString"},
        {"FText",                              S.FText,           "FText"},
        {"FName",                              S.FName,           "FName"},
        {"FDelegate",                          S.FDelegate,       "FDelegate"},
        {"FScriptDelegate",                    S.FDelegate,       "FDelegate"},
        {"FMulticastInlineDelegate",           S.FMulticastInlineDelegate, "FMulticastInlineDelegate"},
        {"FMulticastSparseDelegate",           S.FMulticastSparseDelegate, "FMulticastSparseDelegate"},
    };

    std::string s;
    s += "// Hand-written core types checked against the sizes this dump reported for them.\n";
    s += "// A failure here means the SDK describes a type more narrowly or widely than the\n";
    s += "// engine does, which relocates every generated member placed after one of them.\n";
    for (const auto& g : guards)
    {
        if (g.size <= 0)
            continue;
        s += std::string("static_assert(sizeof(") + g.cppType + ") == " + hx(g.size) + ", \"" + g.cppType +
             " size mismatch vs the dumped " + g.dumped + " — re-dump SDK\");\n";
    }

    // TPersistentObjectPtr places ObjectID past TagAtLastTest at the ObjectID type's own
    // alignment, so for the 8-aligned FSoftObjectPath it lands at 0x10 and the soft pointer
    // is 0x10 plus the path. Pinning the offset reports a layout change where it happens
    // rather than only as a size mismatch further out.
    s += "static_assert(offsetof(FSoftObjectPtr, ObjectID) == " +
         hx(detail::alignUp(0xC, 0x8)) + ", \"FSoftObjectPtr::ObjectID has a wrong offset!\");\n";

    return s;
}

inline std::string GenFText(const FTextLayout& L)
{
    using detail::hx;
    const bool virtualDisplay = L.GetDisplayStringIndex >= 0;
    std::string s;
    s += "namespace FTextImpl\n{\n";
    s += "class FTextData final\n{\npublic:\n";
    if (virtualDisplay)
    {
        s += "\tvoid**                                        VTable;\n\n";
        s += "public:\n";
        s += "\tconst class FString& GetDisplayString() const\n\t{\n";
        s += "\t\tusing GetDisplayStringFn = const class FString& (*)(const FTextData*);\n";
        s += "\t\treturn reinterpret_cast<GetDisplayStringFn>(VTable[" + std::to_string(L.GetDisplayStringIndex) + "])(this);\n\t}\n";
        s += "};\n";
    }
    else
    {
        if (L.TextSource > 0)
            s += "\tuint8                                         Pad_0[" + hx(L.TextSource) + "];\n";
        s += "\tclass FString                                 TextSource;\n";
        s += "};\n";
        s += "static_assert(offsetof(FTextData, TextSource) == " + hx(L.TextSource) + ", \"Member 'FTextData::TextSource' has a wrong offset!\");\n";
    }
    s += "}\n\n";

    s += "class FText final\n{\npublic:\n";
    s += "\tclass FTextImpl::FTextData*                   TextData;\n";
    s += "\tuint8                                         Pad_8[0x10];\n\n";
    s += "public:\n";
    s += "\tconst class FString& GetStringRef() const\n\t{\n";
    s += "\t\tstatic const class FString Empty{};\n";
    s += std::string("\t\treturn TextData ? TextData->") + (virtualDisplay ? "GetDisplayString()" : "TextSource") + " : Empty;\n\t}\n";
    s += "\tstd::string ToString() const\n\t{\n";
    s += "\t\treturn TextData ? GetStringRef().ToString() : std::string();\n\t}\n";
    s += "\tbool IsValid() const\n\t{\n\t\treturn TextData != nullptr;\n\t}\n";
    s += "};\n";
    s += "static_assert(sizeof(FText) == 0x18, \"Wrong size on FText\");\n";
    return s;
}

// Canonical UE GUObjectArray emitted per dumped version
inline std::string GenUObjectArray(const UObjectArrayLayout& L)
{
    using detail::hx;
    const int32_t stride  = L.ItemStride > 0 ? L.ItemStride : 0x18;
    const int32_t objOff  = L.ObjectOffset;
    const bool    chunked = L.NumElementsPerChunk > 0;
    const int32_t nepc    = chunked ? L.NumElementsPerChunk : (64 * 1024);

    // FUObjectItem differs by UE version (derived from probed stride / Object offset).
    std::string item = "struct FUObjectItem\n{\n";
    if (objOff == 8)  // UE5.7+: int64 FlagsAndRefCount @0, Object @8
    {
        item += "\tint64 FlagsAndRefCount;\n";
        item += "\tclass UObject* Object;\n";
        item += "\tint32 SerialNumber;\n";
        item += "\tint32 ClusterRootIndex;\n";
    }
    else if (objOff == 0 && stride == 0x10)  // UE4.11-4.21: flags+cluster fused
    {
        item += "\tclass UObject* Object;\n";
        item += "\tint32 ClusterAndFlags;\n";
        item += "\tint32 SerialNumber;\n";
    }
    else if (objOff == 0 && stride == 0x18)  // UE4.22-5.6
    {
        item += "\t// Pointer to the allocated object\n";
        item += "\tclass UObject* Object;\n";
        item += "\t// Internal flags\n";
        item += "\tint32 Flags;\n";
        item += "\t// UObject Owner Cluster Index\n";
        item += "\tint32 ClusterRootIndex;\n";
        item += "\t// Weak Object Pointer Serial number associated with the object\n";
        item += "\tint32 SerialNumber;\n";
    }
    else  // generic: pad to the probed stride
    {
        if (objOff > 0)
            item += "\tuint8 _pad_0[" + hx(objOff) + "];\n";
        item += "\tclass UObject* Object;\n";
        const int32_t tail = stride - objOff - static_cast<int32_t>(sizeof(void*));
        if (tail > 0)
            item += "\tuint8 _pad_obj[" + hx(tail) + "];\n";
    }
    item += "};\n";

    const std::string nepcStr = (nepc == 64 * 1024) ? "64 * 1024" : std::to_string(nepc);
    const std::string arrType = chunked ? "FChunkedFixedUObjectArray" : "FFixedUObjectArray";

    // Emit data members at explicit offsets, padding gaps so the struct overlays the
    // live (possibly reordered) game array 1:1. Specs may be out of order.
    struct GF { const char* type; const char* name; int32_t off; int32_t size; };
    auto emitFields = [](std::vector<GF> fs) {
        std::sort(fs.begin(), fs.end(), [](const GF& a, const GF& b) { return a.off < b.off; });
        std::string r;
        int32_t cursor = 0, padIdx = 0;
        for (const auto& f : fs)
        {
            if (f.off > cursor)
            {
                r += "\tuint8 _pad_" + std::to_string(padIdx++) + "[" + detail::hx(f.off - cursor) + "];\n";
                cursor = f.off;
            }
            r += std::string("\t") + f.type + " " + f.name + ";\n";
            cursor = f.off + f.size;
        }
        return r;
    };
    auto eff = [](int32_t v, int32_t canon) { return v >= 0 ? v : canon; };

    // Active array (TUObjectArray alias) takes probed offsets; sibling stays canonical.
    const int32_t objObjectsOff = eff(L.ObjObjectsOffset, 0x10);
    const int32_t aObjects   = eff(L.ObjectsOffset, 0x0);
    const int32_t aMaxElem   = chunked ? eff(L.MaxElementsOffset, 0x10) : eff(L.MaxElementsOffset, 0x8);
    const int32_t aNumElem   = chunked ? eff(L.NumElementsOffset, 0x14) : eff(L.NumElementsOffset, 0xC);
    const int32_t aMaxChunks = eff(L.MaxChunksOffset, 0x18);
    const int32_t aNumChunks = eff(L.NumChunksOffset, 0x1C);

    const std::string fixedFields = chunked
        ? emitFields({{"FUObjectItem*", "Objects", 0x0, 8}, {"int32", "MaxElements", 0x8, 4}, {"int32", "NumElements", 0xC, 4}})
        : emitFields({{"FUObjectItem*", "Objects", aObjects, 8}, {"int32", "MaxElements", aMaxElem, 4}, {"int32", "NumElements", aNumElem, 4}});

    const std::string chunkedFields = chunked
        ? emitFields({{"FUObjectItem**", "Objects", aObjects, 8}, {"int32", "MaxElements", aMaxElem, 4}, {"int32", "NumElements", aNumElem, 4}, {"int32", "MaxChunks", aMaxChunks, 4}, {"int32", "NumChunks", aNumChunks, 4}})
        : emitFields({{"FUObjectItem**", "Objects", 0x0, 8}, {"int32", "MaxElements", 0x10, 4}, {"int32", "NumElements", 0x14, 4}, {"int32", "MaxChunks", 0x18, 4}, {"int32", "NumChunks", 0x1C, 4}});

    std::string s;

    // check/checkf/etc. come from "UEAssert.h" (included by Basic.h ahead of this block).
    s += item;
    s += "static_assert(sizeof(FUObjectItem) == " + hx(stride) + ", \"FUObjectItem stride mismatch vs dumped size — re-dump SDK\");\n";
    s += "static_assert(offsetof(FUObjectItem, Object) == " + hx(objOff) + ", \"FUObjectItem::Object has a wrong offset!\");\n\n";

    s += "class FFixedUObjectArray\n{\npublic:\n";
    s += fixedFields;
    s += R"OARR(
	inline int32 Num() const
	{
		return NumElements;
	}

	inline int32 Capacity() const
	{
		return MaxElements;
	}

	inline bool IsValidIndex(int32 Index) const
	{
		return Index < Num() && Index >= 0;
	}

	inline FUObjectItem const* GetObjectPtr(int32 Index) const
	{
		check(Index >= 0 && Index < NumElements);
		return &Objects[Index];
	}

	inline FUObjectItem* GetObjectPtr(int32 Index)
	{
		check(Index >= 0 && Index < NumElements);
		return &Objects[Index];
	}

	inline FUObjectItem const& operator[](int32 Index) const
	{
		FUObjectItem const* ItemPtr = GetObjectPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}

	inline FUObjectItem& operator[](int32 Index)
	{
		FUObjectItem* ItemPtr = GetObjectPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}
};

class FChunkedFixedUObjectArray
{
public:
	enum
	{
		NumElementsPerChunk = )OARR";
    s += nepcStr;
    s += R"OARR(,
	};

)OARR";
    s += chunkedFields;
    s += R"OARR(
	inline int32 Num() const
	{
		return NumElements;
	}

	inline int32 Capacity() const
	{
		return MaxElements;
	}

	inline bool IsValidIndex(int32 Index) const
	{
		return Index < Num() && Index >= 0;
	}

	inline FUObjectItem const* GetObjectPtr(int32 Index) const
	{
		const int32 ChunkIndex = Index / NumElementsPerChunk;
		const int32 WithinChunkIndex = Index % NumElementsPerChunk;
		checkf(IsValidIndex(Index), TEXT("IsValidIndex(%d)"), Index);
		checkf(ChunkIndex < NumChunks, TEXT("ChunkIndex (%d) < NumChunks (%d)"), ChunkIndex, NumChunks);
		checkf(Index < MaxElements, TEXT("Index (%d) < MaxElements (%d)"), Index, MaxElements);
		FUObjectItem* Chunk = Objects[ChunkIndex];
		check(Chunk);
		return Chunk + WithinChunkIndex;
	}

	inline FUObjectItem* GetObjectPtr(int32 Index)
	{
		const int32 ChunkIndex = Index / NumElementsPerChunk;
		const int32 WithinChunkIndex = Index % NumElementsPerChunk;
		checkf(IsValidIndex(Index), TEXT("IsValidIndex(%d)"), Index);
		checkf(ChunkIndex < NumChunks, TEXT("ChunkIndex (%d) < NumChunks (%d)"), ChunkIndex, NumChunks);
		checkf(Index < MaxElements, TEXT("Index (%d) < MaxElements (%d)"), Index, MaxElements);
		FUObjectItem* Chunk = Objects[ChunkIndex];
		check(Chunk);
		return Chunk + WithinChunkIndex;
	}

	inline FUObjectItem const& operator[](int32 Index) const
	{
		FUObjectItem const* ItemPtr = GetObjectPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}

	inline FUObjectItem& operator[](int32 Index)
	{
		FUObjectItem* ItemPtr = GetObjectPtr(Index);
		check(ItemPtr);
		return *ItemPtr;
	}
};

class FUObjectArray
{
public:
	typedef )OARR";
    s += arrType;
    s += R"OARR( TUObjectArray;

)OARR";
    if (objObjectsOff > 0)
        s += "\tuint8 ObjObjectsPadding[" + hx(objObjectsOff) + "];\n";
    s += R"OARR(	TUObjectArray ObjObjects;

public:
	inline int32 GetObjectArrayNum() const
	{
		return ObjObjects.Num();
	}

	int32 ObjectToIndex(const class UObject* Object) const;

	inline FUObjectItem* IndexToObject(int32 Index)
	{
		if (!ObjObjects.IsValidIndex(Index))
			return nullptr;
		return ObjObjects.GetObjectPtr(Index);
	}

	// Convenience iterator (not UE source). Callback returns true to stop early.
	inline void ForEachObject(const std::function<bool(class UObject*)>& Callback)
	{
		if (!Callback) return;
		const int32 N = GetObjectArrayNum();
		for (int32 i = 0; i < N; ++i)
		{
			FUObjectItem* Item = IndexToObject(i);
			class UObject* Object = Item ? Item->Object : nullptr;
			if (!Object) continue;
			if (Callback(Object)) return;
		}
	}
};

)OARR";
    // Overlay guards: the emitted struct must map 1:1 onto live memory.
    s += "static_assert(offsetof(FUObjectArray, ObjObjects) == " + hx(objObjectsOff) + ", \"ObjObjects overlay offset mismatch — re-dump SDK\");\n";
    s += "static_assert(offsetof(" + arrType + ", Objects) == " + hx(aObjects) + ", \"TUObjectArray::Objects overlay offset mismatch — re-dump SDK\");\n";
    s += "static_assert(offsetof(" + arrType + ", NumElements) == " + hx(aNumElem) + ", \"TUObjectArray::NumElements overlay offset mismatch — re-dump SDK\");\n";
    s += "static_assert(offsetof(" + arrType + ", MaxElements) == " + hx(aMaxElem) + ", \"TUObjectArray::MaxElements overlay offset mismatch — re-dump SDK\");\n";
    if (chunked)
    {
        s += "static_assert(offsetof(" + arrType + ", NumChunks) == " + hx(aNumChunks) + ", \"TUObjectArray::NumChunks overlay offset mismatch — re-dump SDK\");\n";
        s += "static_assert(offsetof(" + arrType + ", MaxChunks) == " + hx(aMaxChunks) + ", \"TUObjectArray::MaxChunks overlay offset mismatch — re-dump SDK\");\n";
    }
    s += "\nextern FUObjectArray* GUObjectArray;\n";

    return s;
}

// ── Base tail padding ───────────────────────────────────────────────────────────────
//
// Under the Itanium C++ ABI a derived class lays out its own members from the base's data
// size (dsize, sizeof without tail padding) rather than from its sizeof, provided the base
// is not POD in the C++03 sense the ABI fixes. Unreal's native types rarely are — a vtable,
// a constructor or a base class is enough — so the engine routinely puts a derived
// struct's first members below the base's dumped Size, and GenerateStruct lays them out
// from there. The base's declaration matches that only when:
//
//   * it is not C++03-POD. A struct with a base class never is. A root struct derives from
//     the empty TTailPaddingReusable<Self> (Basic.h), which leaves it trivial, trivially
//     copyable and standard-layout. A user-provided destructor would also work, but it
//     makes the base and every struct built on it non-trivially-copyable, which changes
//     how they are passed and copied; clang 18+ treats a defaulted one as still POD
//     outside Apple targets.
//   * its declared members end at or before the lowest offset any derived struct uses, so
//     the generated trailing padding is shortened or dropped.
//   * its sizeof still equals the dumped Size. With the explicit trailing padding gone,
//     __attribute__((aligned(N))) restores it. Unlike alignas, the attribute may request
//     less than the natural alignment, which is then kept.
//
// A base left with no members is an empty class instead: the empty-base optimization puts
// derived members at offset 0, which needs no marker and no attribute.
//
// Every derived struct's Inherited then becomes the offset its own layout starts at, the
// base's declared data size, with explicit padding up to its first member. A base whose
// declaration cannot end early enough — one of its own members, other than padding, lies at
// or past the offset a derived struct needs, or no alignment yields its Size — is left as it
// was, so its derived structs keep a member below Inherited and AppendLayoutGuards skips them.
struct TailPaddingStats
{
    size_t ReusedBases = 0;      // bases whose tail padding a derived struct now occupies
    size_t MarkedRoots = 0;      // of those, root structs given TTailPaddingReusable
    size_t EmptiedBases = 0;     // of those, root structs reduced to an empty class
    size_t UnresolvedBases = 0;  // bases whose declaration cannot end early enough
};

// `structs` must cover every struct in the dump, across all packages, because a base
// routinely lives in another package than the structs derived from it. Runs before any
// header is emitted: it rewrites members, Inherited and CppName.
template <typename StructT>
TailPaddingStats PackBaseTailPadding(const std::vector<StructT*>& structs)
{
    using detail::hx;
    using MemberT = typename decltype(StructT::Members)::value_type;
    constexpr size_t kNone = static_cast<size_t>(-1);
    constexpr uint32_t kNoMembers = static_cast<uint32_t>(-1);
    TailPaddingStats stats;
    const size_t n = structs.size();

    std::unordered_map<std::string, size_t> byName;
    byName.reserve(n * 2);
    for (size_t i = 0; i < n; ++i)
        byName.emplace(structs[i]->CppNameOnly, i);

    std::vector<size_t> base(n, kNone);
    std::vector<std::vector<size_t>> derived(n);
    for (size_t i = 0; i < n; ++i)
    {
        auto it = byName.find(structs[i]->SuperCppName);
        if (it == byName.end() || it->second == i)
            continue;
        base[i] = it->second;
        derived[it->second].push_back(i);
    }

    // Deepest first, so every struct is visited after all structs derived from it. The
    // depth bound keeps a cyclic base chain in a corrupt dump from looping.
    std::vector<size_t> depth(n, 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t b = base[i]; b != kNone && depth[i] <= n; b = base[b])
            ++depth[i];
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return depth[a] > depth[b]; });

    auto isPad = [](const MemberT& m) { return m.Type == "uint8_t" && m.Name.rfind("Pad_0x", 0) == 0; };
    auto makePad = [](uint32_t offset, uint32_t size)
    {
        MemberT pad;
        pad.Type = "uint8_t";
        pad.Name = "Pad_" + hx(offset) + "[" + hx(size) + "]";
        pad.Offset = offset;
        pad.Size = size;
        return pad;
    };
    auto lowest = [&](const StructT& s)
    {
        uint32_t low = kNoMembers;
        for (const auto& m : s.Members)
            low = std::min(low, m.Offset);
        return low;
    };
    auto dataEnd = [](const std::vector<MemberT>& members)
    {
        uint32_t end = 0;
        for (const auto& m : members)
            end = std::max(end, m.Offset + m.Size);
        return end;
    };

    // Bottom-up: end each base's declaration at the lowest offset a derived struct starts
    // at. start[i] is that offset for struct i itself — its lowest member, or, for a struct
    // without members, the limit it passes on from its own derived structs.
    std::vector<uint32_t> start(n);
    std::vector<uint32_t> align(n, 0);
    std::vector<bool> marked(n, false);
    for (size_t i : order)
    {
        StructT& s = *structs[i];
        uint32_t limit = s.Size;
        for (size_t d : derived[i])
            limit = std::min(limit, start[d]);

        if (limit < s.Size)
        {
            std::vector<MemberT> kept;
            bool fits = true;
            for (const auto& m : s.Members)
            {
                if (m.Offset + m.Size <= limit)
                    kept.push_back(m);
                else if (!isPad(m))
                    fits = false;
                else if (m.Offset < limit)
                    kept.push_back(makePad(m.Offset, limit - m.Offset));
            }

            const bool hasBase = !s.SuperCppName.empty();
            uint32_t alignment = 0;
            if (fits && kept.empty())
            {
                // An empty class has sizeof 1; one with a base has its base's sizeof.
                const uint32_t emptySize = !hasBase ? 1 : base[i] != kNone ? structs[base[i]]->Size : s.Inherited;
                fits = s.Size == emptySize;
            }
            else if (fits)
            {
                const uint32_t end = dataEnd(kept);
                for (alignment = 1; alignment <= s.Size; alignment *= 2)
                    if ((end + alignment - 1) / alignment * alignment == s.Size)
                        break;
                fits = alignment <= s.Size;
            }

            if (fits)
            {
                // A struct with a base and no members only passes the limit on to its base.
                if (!s.Members.empty() || !hasBase)
                    ++stats.ReusedBases;
                s.Members = std::move(kept);
                if (s.Members.empty())
                    stats.EmptiedBases += !hasBase;
                else
                {
                    align[i] = alignment;
                    marked[i] = !hasBase;
                    stats.MarkedRoots += marked[i];
                }
            }
            else
            {
                ++stats.UnresolvedBases;
                limit = s.Size;
            }
        }
        start[i] = s.Members.empty() ? limit : lowest(s);
    }

    // Top-down: start each derived struct's own layout at its base's declared data size.
    // dataSize[i] is where a struct derived from struct i starts placing members.
    std::vector<uint32_t> dataSize(n);
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        const size_t i = *it;
        StructT& s = *structs[i];
        const bool hasBase = !s.SuperCppName.empty();

        if (base[i] != kNone && dataSize[base[i]] < s.Inherited)
        {
            const uint32_t from = dataSize[base[i]];
            const uint32_t own = lowest(s);
            // A member below the base's data stays below Inherited for the guards to report.
            if (own >= from)
            {
                if (own != kNoMembers && own > from)
                    s.Members.insert(s.Members.begin(), makePad(from, own - from));
                s.Inherited = from;
            }
        }

        if (s.Members.empty())
            dataSize[i] = hasBase ? s.Inherited : 0;
        else if (hasBase || marked[i])
            dataSize[i] = dataEnd(s.Members);
        else
            dataSize[i] = s.Size;  // C++03-POD: derived structs start past its sizeof

        const std::string head = "struct ";
        if (align[i] > 1 && s.CppName.compare(0, head.size(), head) == 0)
            s.CppName.insert(head.size(), "__attribute__((aligned(" + std::to_string(align[i]) + "))) ");
        if (marked[i])
            s.CppName += " : TTailPaddingReusable<" + s.CppNameOnly + ">";
    }
    return stats;
}

// ── Generated-package layout guards ─────────────────────────────────────────────────
//
// Every generated member records its dumped offset in a `// 0xNN(0xM)` comment, while
// the compiler places the member according to the C++ type it was declared with. Nothing
// checks that the two agree. When they disagree — a declared type wider or narrower than
// the engine's — every member after it moves, typed access reads neighbouring bytes, and
// the call site looks exactly like correct code. AppendLayoutGuards restates the comments
// as static_asserts so that becomes a build error at the point it is introduced.
//
// An assert can only be emitted where the declaration is able to match the dump at all.
// A member whose dumped offset lies below the struct's Inherited boundary makes it unable
// to. PackBaseTailPadding lowers Inherited to the base's declared data size wherever the
// base's declaration can end before the struct's first member, so a member still below it
// lies inside the base's declared data and C++ places the whole struct at a skew from its
// dumped offsets. That is a property of the struct rather than of the member being
// checked, so it propagates to anything built on it.
//
// A struct also inherits the condition from its base and from the type of any
// struct-valued member, because its own offsets are then computed over a type whose C++
// size already differs from the dumped one. Those structs get a comment naming the reason
// instead of asserts, and LayoutGuardStats counts them so the gap is reported rather than
// silently absorbed.
struct LayoutGuardStats
{
    size_t CheckedStructs = 0;
    size_t CheckedMembers = 0;
    size_t SkippedBaseOverlap = 0;      // own member inside the base's declared data
    size_t SkippedPropagated = 0;       // through a base or a struct-valued member

    size_t SkippedStructs() const
    {
        return SkippedBaseOverlap + SkippedPropagated;
    }
};

namespace detail
{
// The outermost type a member is declared with, qualifiers and template arguments
// dropped: "struct TArray<struct FName>" -> "TArray", "struct FString" -> "FString".
// A pointer yields "", since its width is the pointer's and not the pointee type's.
inline std::string OuterTypeName(const std::string& type)
{
    // Only a '*' past every template argument makes the member itself a pointer. One
    // inside the arguments belongs to an argument: a TSoftClassPtr<UObject*> member is a
    // value of the wrapper type, and takes the wrapper's width.
    const size_t close = type.rfind('>');
    if (type.find('*', close == std::string::npos ? 0 : close + 1) != std::string::npos)
        return {};
    std::string t = type.substr(0, type.find('<'));
    for (const char* kw : {"const ", "enum class ", "enum ", "struct ", "class "})
    {
        const size_t n = std::char_traits<char>::length(kw);
        while (t.compare(0, n, kw) == 0)
            t.erase(0, n);
    }
    return t;
}

// As OuterTypeName, but "" for a container or wrapper template as well. Used where the
// member's width has to follow the named type: a template's own width is fixed by Basic.h
// rather than by its arguments, so it tells the caller nothing about the member.
inline std::string BareTypeName(const std::string& type)
{
    return type.find('<') != std::string::npos ? std::string{} : OuterTypeName(type);
}

// Members are emitted with the array extent spliced into the name ("Foo[0x3]") and
// bit-fields with their width ("bFoo : 1").
inline int32_t DeclaredArrayDim(const std::string& name)
{
    const size_t open = name.find('[');
    if (open == std::string::npos)
        return 1;
    const int32_t dim = static_cast<int32_t>(std::strtol(name.c_str() + open + 1, nullptr, 16));
    return dim > 0 ? dim : 1;
}

inline bool IsBitField(const std::string& name) { return name.find(':') != std::string::npos; }

// The plain identifier offsetof takes, without the array extent.
inline std::string AssertIdentifier(const std::string& name)
{
    return name.substr(0, name.find_first_of("[ \t"));
}

inline int32_t IntegerWidth(const std::string& type)
{
    if (type == "uint8_t" || type == "int8_t") return 1;
    if (type == "uint16_t" || type == "int16_t") return 2;
    if (type == "uint32_t" || type == "int32_t") return 4;
    if (type == "uint64_t" || type == "int64_t") return 8;
    return 0;
}
}  // namespace detail

// Read the core-type sizes this engine reports off the generated member tables.
//
// A member declared with one of the hand-written core types records, in its own dumped
// size, how wide this engine made that type. Taking the figure from the same table the
// headers are emitted from means Basic.h's guards cannot disagree with what was written.
//
// Members carrying an array extent are skipped, since their recorded size covers the whole
// extent; so are bit-fields. A type that is not uniformly sized across the dump is left at
// 0 and goes unasserted rather than pinning a width only some members use.
template <typename StructT>
CoreTypeSizes ObserveCoreTypeSizes(const std::vector<StructT*>& structs)
{
    CoreTypeSizes sizes;
    const std::pair<const char*, int32_t CoreTypeSizes::*> slots[] = {
        {"FSoftObjectPath", &CoreTypeSizes::FSoftObjectPath},
        {"TSoftObjectPtr", &CoreTypeSizes::TSoftObjectPtr},
        {"TSoftClassPtr", &CoreTypeSizes::TSoftClassPtr},
        {"TLazyObjectPtr", &CoreTypeSizes::TLazyObjectPtr},
        {"TWeakObjectPtr", &CoreTypeSizes::TWeakObjectPtr},
        {"TArray", &CoreTypeSizes::TArray},
        {"TMap", &CoreTypeSizes::TMap},
        {"TSet", &CoreTypeSizes::TSet},
        {"TScriptInterface", &CoreTypeSizes::TScriptInterface},
        {"TFieldPath", &CoreTypeSizes::TFieldPath},
        {"FString", &CoreTypeSizes::FString},
        {"FText", &CoreTypeSizes::FText},
        {"FName", &CoreTypeSizes::FName},
        {"FDelegate", &CoreTypeSizes::FDelegate},
        {"FMulticastInlineDelegate", &CoreTypeSizes::FMulticastInlineDelegate},
        {"FMulticastSparseDelegate", &CoreTypeSizes::FMulticastSparseDelegate},
    };
    std::unordered_map<std::string, int32_t CoreTypeSizes::*> bySlot;
    for (const auto& slot : slots)
        bySlot.emplace(slot.first, slot.second);

    // -1 marks a type the dump sized inconsistently, so one disagreement cannot be
    // overwritten by a later agreeing member.
    for (const StructT* s : structs)
    {
        for (const auto& m : s->Members)
        {
            if (detail::IsBitField(m.Name) || m.Name.find('[') != std::string::npos)
                continue;
            auto it = bySlot.find(detail::OuterTypeName(m.Type));
            if (it == bySlot.end())
                continue;
            int32_t& slot = sizes.*(it->second);
            if (slot == -1)
                continue;
            if (slot == 0)
                slot = static_cast<int32_t>(m.Size);
            else if (slot != static_cast<int32_t>(m.Size))
                slot = -1;
        }
    }
    return sizes;
}

// Append the layout guards to every struct's Trailer. `structs` must cover every struct
// in the dump, across all packages, because a base or a member's type routinely lives in
// another one. Returns what was checked and what was not.
template <typename StructT>
LayoutGuardStats AppendLayoutGuards(const std::vector<StructT*>& structs)
{
    using detail::hx;
    LayoutGuardStats stats;

    std::unordered_map<std::string, size_t> byName;
    byName.reserve(structs.size() * 2);
    for (size_t i = 0; i < structs.size(); ++i)
        byName.emplace(structs[i]->CppNameOnly, i);

    // Reasons are resolved in two passes so that propagation cannot depend on the order
    // structs happen to appear in: first the conditions provable from a struct's own
    // record, then a fixpoint over bases and struct-valued members. Growing the unfaithful
    // set monotonically also keeps cycles in the type graph from deciding the outcome.
    enum class Skip
    {
        None,
        BaseOverlap,      // a member of this struct lies inside its base's declared data
        Base,             // the base cannot be checked
        MemberType,       // a struct-valued member's type cannot be checked
        MemberTypeSize,   // a member's size disagrees with its own type's dumped size
    };
    std::vector<Skip> skip(structs.size(), Skip::None);

    for (size_t i = 0; i < structs.size(); ++i)
    {
        const StructT& s = *structs[i];
        for (const auto& m : s.Members)
        {
            if (m.Offset < s.Inherited)
            {
                skip[i] = Skip::BaseOverlap;
                break;
            }
        }
    }

    for (bool changed = true; changed;)
    {
        changed = false;
        for (size_t i = 0; i < structs.size(); ++i)
        {
            if (skip[i] != Skip::None)
                continue;
            const StructT& s = *structs[i];
            Skip bad = Skip::None;

            auto super = byName.find(s.SuperCppName);
            if (super != byName.end() && skip[super->second] != Skip::None)
                bad = Skip::Base;

            for (const auto& m : s.Members)
            {
                if (bad != Skip::None)
                    break;
                if (detail::IsBitField(m.Name))
                    continue;
                auto it = byName.find(detail::BareTypeName(m.Type));
                if (it == byName.end())
                    continue;
                if (skip[it->second] != Skip::None)
                    bad = Skip::MemberType;
                else if (structs[it->second]->Size * detail::DeclaredArrayDim(m.Name) != m.Size)
                    bad = Skip::MemberTypeSize;
            }

            if (bad != Skip::None)
            {
                skip[i] = bad;
                changed = true;
            }
        }
    }

    for (size_t i = 0; i < structs.size(); ++i)
    {
        StructT& s = *structs[i];
        if (skip[i] != Skip::None)
        {
            const char* why = "";
            switch (skip[i])
            {
            case Skip::BaseOverlap:
                ++stats.SkippedBaseOverlap;
                why = "a member lies inside its base's declared data";
                break;
            case Skip::Base:
                ++stats.SkippedPropagated;
                why = "its base cannot be checked";
                break;
            case Skip::MemberType:
                ++stats.SkippedPropagated;
                why = "a member's type cannot be checked";
                break;
            case Skip::MemberTypeSize:
                ++stats.SkippedPropagated;
                why = "a member's size disagrees with its own type's dumped size";
                break;
            case Skip::None:
                break;
            }

            if (!s.Trailer.empty())
                s.Trailer += "\n";
            s.Trailer += "// Layout asserts omitted for " + s.CppNameOnly + ": " + why + ".";
            continue;
        }

        std::string guards;
        // A dumped size of 0 is the dumper's placeholder for a struct it could not size;
        // the emitted empty struct is 1 byte, so there is nothing to assert against.
        // The core types synthesized from UE_Offsets already carry their own size assert.
        if (s.Size > 0 && s.Trailer.find("sizeof(" + s.CppNameOnly + ")") == std::string::npos)
            guards += "static_assert(sizeof(" + s.CppNameOnly + ") == " + hx(s.Size) + ", \"" +
                      s.CppNameOnly + " size mismatch vs dumped size — re-dump SDK\");\n";

        for (const auto& m : s.Members)
        {
            // offsetof is ill-formed on a bit-field, and the dumped offset of one is the
            // byte its mask lives in rather than a member address.
            if (detail::IsBitField(m.Name))
                continue;
            const std::string id = detail::AssertIdentifier(m.Name);
            guards += "static_assert(offsetof(" + s.CppNameOnly + ", " + id + ") == " + hx(m.Offset) +
                      ", \"Member '" + s.CppNameOnly + "::" + id +
                      "' is not at its dumped offset — re-dump SDK\");\n";
            ++stats.CheckedMembers;
        }

        if (guards.empty())
            continue;
        guards.pop_back();  // Trailer is emitted with its own trailing newline
        if (!s.Trailer.empty())
            s.Trailer += "\n";
        s.Trailer += guards;
        ++stats.CheckedStructs;
    }

    return stats;
}

// Replace the @@SDK_GEN_*@@ placeholders in kUECoreBasicH with generated code.
inline std::string SpliceLayoutCoreTypes(std::string content, const FNameLayout& L, const UObjectArrayLayout& UA,
                                         const FTextLayout& T, const CoreTypeSizes& CS)
{
    struct Sub { const char* tok; std::string code; };
    const Sub subs[] = {
        {"// @@SDK_GEN_CONFIG@@", GenSDKConfigDefines(L)},
        {"// @@SDK_GEN_UOBJECTARRAY@@", GenUObjectArray(UA)},
        {"// @@SDK_GEN_FNAME@@", GenFName(L)},
        {"// @@SDK_GEN_FSCRIPTDELEGATE@@", GenFScriptDelegate(L)},
        {"// @@SDK_GEN_FSOFTOBJECTPATH@@", GenFSoftObjectPath(L, CS.FSoftObjectPath)},
        {"// @@SDK_GEN_FTEXT@@", GenFText(T)},
        {"// @@SDK_GEN_CORETYPE_GUARDS@@", GenCoreTypeGuards(CS)},
    };
    for (const auto& sub : subs)
    {
        auto pos = content.find(sub.tok);
        if (pos != std::string::npos)
            content.replace(pos, std::char_traits<char>::length(sub.tok), sub.code);
    }
    return content;
}

}  // namespace sdkcoregen
