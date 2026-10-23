#pragma once

#include <vice/vice.h>

namespace Vice::Detail {

enum class TokenKind {
    End,
    Newline,
    Identifier,
    Number,
    String,
    // Interpolated strings: TemplateStart, then each expression's tokens separated by TemplateMiddle, then
    // TemplateEnd, each carrying the literal text around the expressions.
    TemplateStart,
    TemplateMiddle,
    TemplateEnd,
    Func,
    Class,
    Extends,
    Self,
    Super,
    Var,
    Const,
    Import,
    As,
    Return,
    If,
    Else,
    While,
    For,
    In,
    Break,
    Continue,
    Override,
    Export,
    Is,
    Yield,
    Enum,
    Match,
    Null,
    True,
    False,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Semicolon,
    Colon,
    Equal,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Dot,
    Bang,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourceLocation location;
};

} // namespace Vice::Detail
