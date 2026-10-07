#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SDKMathNames
{
namespace Detail
{
inline bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        const auto left = static_cast<unsigned char>(lhs[i]);
        const auto right = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(left) != std::tolower(right))
            return false;
    }
    return true;
}

inline bool IsSupportedScalar(std::string_view type)
{
    return type == "float" || type == "double";
}
} // namespace Detail

template <typename Members>
bool Canonicalize(std::string_view cppName, Members &members)
{
    struct Specification
    {
        std::string_view Type;
        std::array<std::string_view, 4> Names;
        std::size_t Count;
    };

    static constexpr std::array specifications{
        Specification{"FVector", {"X", "Y", "Z", ""}, 3},
        Specification{"FVector2D", {"X", "Y", "", ""}, 2},
        Specification{"FVector4", {"X", "Y", "Z", "W"}, 4},
        Specification{"FRotator", {"Pitch", "Yaw", "Roll", ""}, 3},
        Specification{"FLinearColor", {"R", "G", "B", "A"}, 4},
    };

    const Specification *specification = nullptr;
    for (const auto &candidate : specifications)
    {
        if (candidate.Type == cppName)
        {
            specification = &candidate;
            break;
        }
    }
    if (!specification)
        return false;

    std::vector<std::pair<std::size_t, std::string_view>> matches;
    matches.reserve(specification->Count);
    for (std::size_t nameIndex = 0; nameIndex < specification->Count; ++nameIndex)
    {
        const auto canonicalName = specification->Names[nameIndex];
        std::size_t matchedIndex = members.size();
        for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex)
        {
            const auto &member = members[memberIndex];
            if (!Detail::IsSupportedScalar(member.Type)
                || !Detail::EqualsIgnoreCase(member.Name, canonicalName))
                continue;
            if (matchedIndex != members.size())
                return false;
            matchedIndex = memberIndex;
        }
        if (matchedIndex == members.size())
            return false;
        matches.emplace_back(matchedIndex, canonicalName);
    }

    for (const auto &[index, canonicalName] : matches)
        members[index].Name = canonicalName;
    return true;
}
} // namespace SDKMathNames
