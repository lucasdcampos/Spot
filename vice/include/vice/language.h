#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace Vice {

// Header-only language vocabulary shared by the lexer and tools, including builds without the runtime.
enum class Keyword {
    Func,
    Var,
    Const,
    Return,
    If,
    Else,
    While,
    For,
    In,
    Break,
    Continue,
    Override,
    Null,
    True,
    False,
    Not,
    And,
    Or,
    Import,
    As,
    Class,
    Extends,
    Self,
    Super,
    Export,
    Is,
    Yield,
    Enum,
    Match
};
struct KeywordDefinition {
    std::string_view name;
    Keyword kind;
};
inline constexpr std::array Keywords{KeywordDefinition{"while", Keyword::While},
                                     KeywordDefinition{"for", Keyword::For},
                                     KeywordDefinition{"in", Keyword::In},
                                     KeywordDefinition{"break", Keyword::Break},
                                     KeywordDefinition{"continue", Keyword::Continue},
                                     KeywordDefinition{"override", Keyword::Override},
                                     KeywordDefinition{"func", Keyword::Func},
                                     KeywordDefinition{"var", Keyword::Var},
                                     KeywordDefinition{"const", Keyword::Const},
                                     KeywordDefinition{"return", Keyword::Return},
                                     KeywordDefinition{"if", Keyword::If},
                                     KeywordDefinition{"else", Keyword::Else},
                                     KeywordDefinition{"null", Keyword::Null},
                                     KeywordDefinition{"true", Keyword::True},
                                     KeywordDefinition{"false", Keyword::False},
                                     KeywordDefinition{"not", Keyword::Not},
                                     KeywordDefinition{"and", Keyword::And},
                                     KeywordDefinition{"or", Keyword::Or},
                                     KeywordDefinition{"import", Keyword::Import},
                                     KeywordDefinition{"as", Keyword::As},
                                     KeywordDefinition{"class", Keyword::Class},
                                     KeywordDefinition{"extends", Keyword::Extends},
                                     KeywordDefinition{"self", Keyword::Self},
                                     KeywordDefinition{"super", Keyword::Super},
                                     KeywordDefinition{"export", Keyword::Export},
                                     KeywordDefinition{"is", Keyword::Is},
                                     KeywordDefinition{"yield", Keyword::Yield},
                                     KeywordDefinition{"enum", Keyword::Enum},
                                     KeywordDefinition{"match", Keyword::Match}};
inline constexpr std::array<std::string_view, 4> ValueConstructors{"Vector2", "Vector3", "Color", "range"};
inline constexpr std::array<std::string_view, 4> StandardLibraries{"math", "string", "core", "random"};

constexpr std::optional<Keyword> FindKeyword(std::string_view name) {
    for (const auto& keyword : Keywords) {
        if (keyword.name == name)
            return keyword.kind;
    }
    return std::nullopt;
}
constexpr bool IsValueConstructor(std::string_view name) {
    for (const auto constructor : ValueConstructors) {
        if (constructor == name)
            return true;
    }
    return false;
}

} // namespace Vice
