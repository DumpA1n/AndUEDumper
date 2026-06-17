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
    s += "\tstatic inline std::function<std::string(int32_t)> s_NameResolver;\n\n";

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

	const char* ToCString() const
	{
		return ToString().c_str();
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

// Replace the @@SDK_GEN_*@@ placeholders in kUECoreBasicH with generated code.
inline std::string SpliceLayoutCoreTypes(std::string content, const FNameLayout& L)
{
    struct Sub { const char* tok; std::string code; };
    const Sub subs[] = {
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
