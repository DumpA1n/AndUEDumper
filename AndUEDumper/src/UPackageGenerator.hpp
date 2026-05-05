#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Utils/BufferFmt.hpp"
#include "Utils/ProgressUtils.hpp"

#include "UE/UEWrappers.hpp"

class UE_UPackage
{
public:
    struct Member
    {
        std::string Type;
        std::string Name;
        std::string extra;  // extra comment
        uint32_t Offset = 0;
        uint32_t Size = 0;
    };
    struct Param
    {
        std::string Type;     // sanitized C++ type, e.g. "FVector"
        std::string Name;     // sanitized identifier (keyword-safe)
        uint64_t    Flags = 0; // CPF_* flags from prop->GetPropertyFlags()
        int32_t     ArrayDim = 1;
    };
    struct Function
    {
        std::string Name;
        std::string FullName;
        std::string CppName;        // "[static ]<ReturnType> <FuncName>"
        std::string Params;         // "(Type Name, Type& OutName, ...)"  payload only
        std::string ReturnType;     // "void" / "FVector" / ...
        std::string OwnerCppName;   // owner struct C++ name (qualifier in out-of-line bodies)
        std::string OwnerUEName;    // owner struct UE FName (passed to UClass::GetFunction)
        std::vector<Param> ParamsList;
        bool        IsStatic = false; // FUNC_Static
        uint32_t EFlags = 0;
        std::string Flags;
        int8_t NumParams = 0;
        int16_t ParamSize = 0;
        uintptr_t Func = 0;
    };
    struct Struct
    {
        std::string Name;
        std::string FullName;
        std::string CppName;        // "struct FFoo : FBar" or "struct UBaz"
        std::string CppNameOnly;    // just "FFoo" / "UBaz"
        std::string SuperCppName;   // just "FBar" or empty
        uint32_t Inherited = 0;
        uint32_t Size = 0;
        std::vector<Member> Members;
        std::vector<Function> Functions;
        // other dumped types needed as complete type (inheritance, value members)
        std::set<std::string> FullDeps;
        // other dumped types needing only fwd-decl (ptr/wrapper refs)
        std::set<std::string> ForwardDeps;
        // extra C++ appended after Members/Functions (e.g. AIOCore method decls)
        std::string ExtraDecls;
        // extra C++ prepended before Members (e.g. DEFINE_UE_CLASS_HELPERS, GObjects)
        std::string PrefixDecls;
    };
    struct Enum
    {
        std::string FullName;
        std::string CppName;       // "enum class EFoo : uint8_t"
        std::string CppNameOnly;   // just "EFoo"
        std::string UnderlyingType;// "uint8_t" / "uint16_t" ...
        std::vector<std::pair<std::string, uint64_t>> Members;
    };

private:
    std::pair<uint8_t *const, std::vector<UE_UObject>> *Package;

public:
    std::string PackageName;                      // package object name
    std::vector<Struct> Classes;
    std::vector<Struct> Structures;
    std::vector<Enum> Enums;

private:
    static void GenerateFunction(const UE_UFunction &fn, Function *out);
    static void GenerateStruct(const UE_UStruct &object, std::vector<Struct> &arr);
    static void GenerateEnum(const UE_UEnum &object, std::vector<Enum> &arr);

    static void GenerateBitPadding(std::vector<Member> &members, uint32_t offset, uint8_t bitOffset, uint8_t size);
    static void GeneratePadding(std::vector<Member> &members, uint32_t offset, uint32_t size);
    static void FillPadding(const UE_UStruct &object, std::vector<Member> &members, uint32_t &offset, uint8_t &bitOffset, uint32_t end);

public:
    UE_UPackage(std::pair<uint8_t *const, std::vector<UE_UObject>> &package) : Package(&package) {};
    inline UE_UObject GetObject() const { return UE_UObject(Package->first); }
    void Process();

    // split type-string idents into full-deps vs fwd-deps
    static void ExtractTypeDeps(const std::string &typeStr,
                                std::set<std::string> &fullDeps,
                                std::set<std::string> &fwdDeps);

    static void AppendStructsToBuffer(std::vector<Struct> &arr, class BufferFmt *bufFmt);
    static void AppendEnumsToBuffer(std::vector<Enum> &arr, class BufferFmt *bufFmt);
};
