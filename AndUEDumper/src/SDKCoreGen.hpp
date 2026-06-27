#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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
inline std::string GenFSoftObjectPath(const FNameLayout& L)
{
    using detail::hx;
    const int32_t subOff = detail::alignUp(L.Size + 4, 8);
    const int32_t pad = subOff - L.Size;
    const int32_t total = subOff + 0x10;

    std::string s;
    s += "namespace FakeSoftObjectPtr\n{\n";
    s += "struct FSoftObjectPath\n{\npublic:\n";
    s += "\tclass FName                                   AssetPathName;\n";
    s += "\tuint8                                         Pad_C[" + hx(pad) + "];\n";
    s += "\tclass FString                                 SubPathString;\n";
    s += "};\n";
    s += "static_assert(alignof(FSoftObjectPath) == 0x8, \"Wrong alignment on FSoftObjectPath\");\n";
    s += "static_assert(sizeof(FSoftObjectPath) == " + hx(total) + ", \"Wrong size on FSoftObjectPath\");\n";
    s += "static_assert(offsetof(FSoftObjectPath, AssetPathName) == 0x0, \"Member 'FSoftObjectPath::AssetPathName' has a wrong offset!\");\n";
    s += "static_assert(offsetof(FSoftObjectPath, SubPathString) == " + hx(subOff) + ", \"Member 'FSoftObjectPath::SubPathString' has a wrong offset!\");\n";
    s += "}\n";
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

// Replace the @@SDK_GEN_*@@ placeholders in kUECoreBasicH with generated code.
inline std::string SpliceLayoutCoreTypes(std::string content, const FNameLayout& L, const UObjectArrayLayout& UA)
{
    struct Sub { const char* tok; std::string code; };
    const Sub subs[] = {
        {"// @@SDK_GEN_CONFIG@@", GenSDKConfigDefines(L)},
        {"// @@SDK_GEN_UOBJECTARRAY@@", GenUObjectArray(UA)},
        {"// @@SDK_GEN_FNAME@@", GenFName(L)},
        {"// @@SDK_GEN_FSCRIPTDELEGATE@@", GenFScriptDelegate(L)},
        {"// @@SDK_GEN_FSOFTOBJECTPATH@@", GenFSoftObjectPath(L)},
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
