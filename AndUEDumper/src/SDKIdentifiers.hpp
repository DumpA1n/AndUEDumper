#pragma once

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>

namespace SDKIdentifiers
{
inline std::string Sanitize(std::string_view input)
{
    std::string output;
    output.reserve(input.size() + 1);
    for (const char character : input)
    {
        const auto value = static_cast<unsigned char>(character);
        output += (std::isalnum(value) || character == '_') ? character : '_';
    }
    if (output.empty())
        output = "Unnamed";
    if (std::isdigit(static_cast<unsigned char>(output.front())))
        output.insert(output.begin(), '_');

    static constexpr std::array reserved{
        std::string_view{"alignas"}, std::string_view{"alignof"}, std::string_view{"and"},
        std::string_view{"and_eq"}, std::string_view{"asm"}, std::string_view{"auto"},
        std::string_view{"bitand"}, std::string_view{"bitor"}, std::string_view{"bool"},
        std::string_view{"break"}, std::string_view{"case"}, std::string_view{"catch"},
        std::string_view{"char"}, std::string_view{"char8_t"}, std::string_view{"char16_t"},
        std::string_view{"char32_t"}, std::string_view{"class"}, std::string_view{"compl"},
        std::string_view{"concept"}, std::string_view{"const"}, std::string_view{"consteval"},
        std::string_view{"constexpr"}, std::string_view{"constinit"}, std::string_view{"const_cast"},
        std::string_view{"continue"}, std::string_view{"co_await"}, std::string_view{"co_return"},
        std::string_view{"co_yield"}, std::string_view{"decltype"}, std::string_view{"default"},
        std::string_view{"delete"}, std::string_view{"do"}, std::string_view{"double"},
        std::string_view{"dynamic_cast"}, std::string_view{"else"}, std::string_view{"enum"},
        std::string_view{"explicit"}, std::string_view{"export"}, std::string_view{"extern"},
        std::string_view{"false"}, std::string_view{"float"}, std::string_view{"for"},
        std::string_view{"friend"}, std::string_view{"goto"}, std::string_view{"if"},
        std::string_view{"inline"}, std::string_view{"int"}, std::string_view{"long"},
        std::string_view{"mutable"}, std::string_view{"namespace"}, std::string_view{"new"},
        std::string_view{"noexcept"}, std::string_view{"not"}, std::string_view{"not_eq"},
        std::string_view{"nullptr"}, std::string_view{"operator"}, std::string_view{"or"},
        std::string_view{"or_eq"}, std::string_view{"private"}, std::string_view{"protected"},
        std::string_view{"public"}, std::string_view{"register"}, std::string_view{"reinterpret_cast"},
        std::string_view{"requires"}, std::string_view{"return"}, std::string_view{"short"},
        std::string_view{"signed"}, std::string_view{"sizeof"}, std::string_view{"static"},
        std::string_view{"static_assert"}, std::string_view{"static_cast"}, std::string_view{"struct"},
        std::string_view{"switch"}, std::string_view{"template"}, std::string_view{"this"},
        std::string_view{"thread_local"}, std::string_view{"throw"}, std::string_view{"true"},
        std::string_view{"try"}, std::string_view{"typedef"}, std::string_view{"typeid"},
        std::string_view{"typename"}, std::string_view{"union"}, std::string_view{"unsigned"},
        std::string_view{"using"}, std::string_view{"virtual"}, std::string_view{"void"},
        std::string_view{"volatile"}, std::string_view{"wchar_t"}, std::string_view{"while"},
        std::string_view{"xor"}, std::string_view{"xor_eq"},
        std::string_view{"Parms"}, std::string_view{"Func"}, std::string_view{"NULL"},
    };
    for (const auto candidate : reserved)
    {
        if (output == candidate)
        {
            output.push_back('_');
            break;
        }
    }
    return output;
}

inline std::string SanitizeType(std::string_view input)
{
    std::string output;
    output.reserve(input.size());
    for (const char character : input)
    {
        const auto value = static_cast<unsigned char>(character);
        const bool keep = std::isalnum(value) || character == '_'
                       || character == ' ' || character == '\t'
                       || character == '<' || character == '>' || character == ','
                       || character == '*' || character == '&' || character == ':'
                       || character == '[' || character == ']';
        output += keep ? character : '_';
    }
    return output;
}

inline bool IsUsableType(std::string_view type)
{
    int angleDepth = 0;
    int bracketDepth = 0;
    bool hasIdentifier = false;
    for (std::size_t index = 0; index < type.size();)
    {
        const auto value = static_cast<unsigned char>(type[index]);
        if (std::isdigit(value))
            return false;
        if (std::isalpha(value) || type[index] == '_')
        {
            const auto start = index++;
            while (index < type.size())
            {
                const auto next = static_cast<unsigned char>(type[index]);
                if (!std::isalnum(next) && type[index] != '_')
                    break;
                ++index;
            }
            const auto token = type.substr(start, index - start);
            hasIdentifier = true;
            if (token == "struct" || token == "class" || token == "enum")
            {
                while (index < type.size() && std::isspace(static_cast<unsigned char>(type[index])))
                    ++index;
                if (token == "enum" && type.substr(index, 5) == "class")
                {
                    index += 5;
                    while (index < type.size() && std::isspace(static_cast<unsigned char>(type[index])))
                        ++index;
                }
                if (index >= type.size()
                    || (!std::isalpha(static_cast<unsigned char>(type[index])) && type[index] != '_'))
                    return false;
            }
            continue;
        }
        if (type[index] == '<')
        {
            std::size_t next = index + 1;
            while (next < type.size() && std::isspace(static_cast<unsigned char>(type[next])))
                ++next;
            if (next >= type.size() || type[next] == '>')
                return false;
            ++angleDepth;
        }
        else if (type[index] == '>' && --angleDepth < 0)
        {
            return false;
        }
        else if (type[index] == '[')
        {
            ++bracketDepth;
        }
        else if (type[index] == ']' && --bracketDepth < 0)
        {
            return false;
        }
        ++index;
    }
    return hasIdentifier && angleDepth == 0 && bracketDepth == 0;
}

inline std::string MakeUnique(std::string identifier, std::unordered_set<std::string> &used)
{
    if (used.insert(identifier).second)
        return identifier;
    const std::string base = identifier;
    for (std::size_t suffix = 2;; ++suffix)
    {
        identifier = base + "_" + std::to_string(suffix);
        if (used.insert(identifier).second)
            return identifier;
    }
}
} // namespace SDKIdentifiers
