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

// FName members emitted at their real offsets; gaps padded. Method bodies are
// layout-independent and kept byte-identical to the old static block.
inline std::string GenFName(const FNameLayout& L)
{
    using detail::hx;

    struct Member { int32_t off; const char* type; const char* name; };
    std::vector<Member> mem{{L.ComparisonIndex, "int32", "ComparisonIndex"}};
    if (L.CasePreserving) mem.push_back({L.DisplayIndex, "int32", "DisplayIndex"});
    if (!L.OutlineNumber) mem.push_back({L.Number, "uint32", "Number"});
    std::sort(mem.begin(), mem.end(), [](const Member& a, const Member& b) { return a.off < b.off; });

    std::string s = "class FName final\n{\npublic:\n";
    s += "\tstatic inline std::function<std::string(uint32)> s_NameResolver;\n\n";

    int32_t run = 0;
    for (const auto& m : mem)
    {
        if (m.off > run)
            s += "\tuint8                                         _pad_" + hx(run) + "[" + hx(m.off - run) + "];\n";
        char line[160];
        std::snprintf(line, sizeof line, "\t%-44s %s;\n", m.type, m.name);
        s += line;
        run = m.off + static_cast<int32_t>(sizeof(int32_t));
    }

    s += "\npublic:\n";
    s += L.CasePreserving ? "\tint32 GetDisplayIndex() const { return DisplayIndex; }\n"
                          : "\tint32 GetDisplayIndex() const { return ComparisonIndex; }\n";
    s += R"BODY(
	static std::string GetPlainANSIString(const FName* Name)
	{
		if (s_NameResolver)
			return s_NameResolver(Name->ComparisonIndex);
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
    s += L.OutlineNumber
             ? "\tbool operator==(const FName& Other) const { return ComparisonIndex == Other.ComparisonIndex; }\n"
             : "\tbool operator==(const FName& Other) const { return ComparisonIndex == Other.ComparisonIndex && Number == Other.Number; }\n";
    s += "\tbool operator!=(const FName& Other) const { return !(*this == Other); }\n";
    s += "};\n";

    s += "static_assert(alignof(FName) == 0x4, \"Wrong alignment on FName\");\n";
    s += "static_assert(sizeof(FName) == " + hx(L.Size) + ", \"Wrong size on FName\");\n";
    s += "static_assert(offsetof(FName, ComparisonIndex) == " + hx(L.ComparisonIndex) + ", \"Member 'FName::ComparisonIndex' has a wrong offset!\");\n";
    if (L.CasePreserving)
        s += "static_assert(offsetof(FName, DisplayIndex) == " + hx(L.DisplayIndex) + ", \"Member 'FName::DisplayIndex' has a wrong offset!\");\n";
    if (!L.OutlineNumber)
        s += "static_assert(offsetof(FName, Number) == " + hx(L.Number) + ", \"Member 'FName::Number' has a wrong offset!\");\n";
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

    std::string s;

    // check/checkf/etc. come from "UEAssert.h" (included by Basic.h ahead of this block).
    s += item;
    s += "static_assert(sizeof(FUObjectItem) == " + hx(stride) + ", \"FUObjectItem stride mismatch vs dumped size — re-dump SDK\");\n";
    s += "static_assert(offsetof(FUObjectItem, Object) == " + hx(objOff) + ", \"FUObjectItem::Object has a wrong offset!\");\n\n";

    s += R"OARR(class FFixedUObjectArray
{
public:
	FUObjectItem* Objects;
	int32 MaxElements;
	int32 NumElements;

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

	FUObjectItem** Objects;
	FUObjectItem* PreAllocatedObjects;
	int32 MaxElements;
	int32 NumElements;
	int32 MaxChunks;
	int32 NumChunks;

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

private:
	TUObjectArray* ObjObjects = nullptr;

public:
	inline void InitManually(void* GObjectsAddressParameter)
	{
		ObjObjects = reinterpret_cast<TUObjectArray*>(GObjectsAddressParameter);
	}

	inline int32 GetObjectArrayNum() const
	{
		return ObjObjects ? ObjObjects->Num() : 0;
	}

	int32 ObjectToIndex(const class UObject* Object) const;

	inline FUObjectItem* IndexToObject(int32 Index)
	{
		if (!ObjObjects || !ObjObjects->IsValidIndex(Index))
			return nullptr;
		return ObjObjects->GetObjectPtr(Index);
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

extern FUObjectArray GUObjectArray;
)OARR";

    return s;
}

// Replace the @@SDK_GEN_*@@ placeholders in kUECoreBasicH with generated code.
inline std::string SpliceLayoutCoreTypes(std::string content, const FNameLayout& L, const UObjectArrayLayout& UA)
{
    struct Sub { const char* tok; std::string code; };
    const Sub subs[] = {
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
