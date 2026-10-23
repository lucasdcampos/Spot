#include "syntax.h"

#include <vice/language.h>

#include <cctype>
#include <utility>

namespace Vice::Detail {
namespace {

class Lexer {
public:
    Lexer(std::string_view source, std::string_view sourceName, SourceLocation location = {})
        : m_source(source), m_sourceName(sourceName), m_location(location) {}

    std::vector<Token> Run() {
        while (m_offset < m_source.size()) {
            const SourceLocation location = m_location;
            const char c = Advance();
            switch (c) {
            case ' ':
            case '\t':
            case '\r':
                break;
            case '\n':
                Add(TokenKind::Newline, "", location);
                break;
            case '(':
                Add(TokenKind::LeftParen, "(", location);
                break;
            case ')':
                Add(TokenKind::RightParen, ")", location);
                break;
            case '{':
                Add(TokenKind::LeftBrace, "{", location);
                break;
            case '}':
                Add(TokenKind::RightBrace, "}", location);
                break;
            case '[':
                Add(TokenKind::LeftBracket, "[", location);
                break;
            case ']':
                Add(TokenKind::RightBracket, "]", location);
                break;
            case ',':
                Add(TokenKind::Comma, ",", location);
                break;
            case ':':
                Add(TokenKind::Colon, ":", location);
                break;
            case ';':
                Add(TokenKind::Semicolon, ";", location);
                break;
            case '=':
                Operator('=', TokenKind::EqualEqual, TokenKind::Equal, location);
                break;
            case '+':
                Operator('=', TokenKind::PlusEqual, TokenKind::Plus, location);
                break;
            case '-':
                Operator('=', TokenKind::MinusEqual, TokenKind::Minus, location);
                break;
            case '*':
                Operator('=', TokenKind::StarEqual, TokenKind::Star, location);
                break;
            case '%':
                Operator('=', TokenKind::PercentEqual, TokenKind::Percent, location);
                break;
            case '.':
                Add(TokenKind::Dot, ".", location);
                break;
            case '!':
                Operator('=', TokenKind::BangEqual, TokenKind::Bang, location);
                break;
            case '<':
                Operator('=', TokenKind::LessEqual, TokenKind::Less, location);
                break;
            case '>':
                Operator('=', TokenKind::GreaterEqual, TokenKind::Greater, location);
                break;
            case '&':
            case '|':
                if (Peek() != c)
                    throw Error(m_sourceName, location, "Expected a doubled logical operator");
                Advance();
                Add(c == '&' ? TokenKind::And : TokenKind::Or, std::string(2, c), location);
                break;
            case '/':
                if (Peek() == '/') {
                    while (m_offset < m_source.size() && Peek() != '\n') {
                        Advance();
                    }
                } else {
                    Operator('=', TokenKind::SlashEqual, TokenKind::Slash, location);
                }
                break;
            case '"':
                String(location);
                break;
            default:
                if (IsDigit(c)) {
                    Number(location);
                } else if (IsNameStart(c)) {
                    Identifier(location);
                } else {
                    throw Error(m_sourceName, location, "Unexpected character");
                }
                break;
            }
        }
        Add(TokenKind::End, "", m_location);
        return std::move(m_tokens);
    }

private:
    void Operator(char next, TokenKind paired, TokenKind single, SourceLocation location) {
        std::string text(1, m_source[m_offset - 1]);
        const bool matched = Peek() == next;
        if (matched)
            text += Advance();
        Add(matched ? paired : single, std::move(text), location);
    }
    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }
    static bool IsNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    char Peek(std::size_t ahead = 0) const {
        return m_offset + ahead < m_source.size() ? m_source[m_offset + ahead] : '\0';
    }
    char Advance() {
        const char c = m_source[m_offset++];
        if (c == '\n') {
            ++m_location.line;
            m_location.column = 1;
        } else {
            ++m_location.column;
        }
        return c;
    }
    void Add(TokenKind kind, std::string text, SourceLocation location) {
        m_tokens.push_back({kind, std::move(text), location});
    }
    // Lexes `{expression}` inside a string, after its opening brace, appending the expression's tokens.
    void Interpolation(SourceLocation stringLocation) {
        const std::size_t begin = m_offset;
        const SourceLocation location = m_location;
        std::size_t depth = 0;
        for (;;) {
            if (m_offset == m_source.size() || Peek() == '\n')
                throw Error(m_sourceName, stringLocation, "Unterminated string interpolation");
            const char c = Peek();
            if (c == '"') {
                // A string inside the expression, with its own escapes and interpolations.
                Advance();
                while (Peek() != '"') {
                    if (m_offset == m_source.size() || Peek() == '\n')
                        throw Error(m_sourceName, stringLocation, "Unterminated string interpolation");
                    if (Advance() == '\\' && m_offset < m_source.size())
                        Advance();
                }
                Advance();
                continue;
            }
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                if (depth == 0)
                    break;
                --depth;
            }
            Advance();
        }
        const auto source = m_source.substr(begin, m_offset - begin);
        Advance();
        if (source.find_first_not_of(" \t") == std::string_view::npos)
            throw Error(m_sourceName, location, "Expected an expression in string interpolation");
        auto tokens = Lexer(source, m_sourceName, location).Run();
        tokens.pop_back();
        for (auto& token : tokens)
            m_tokens.push_back(std::move(token));
    }
    void String(SourceLocation location) {
        std::string text;
        bool interpolated = false;
        while (m_offset < m_source.size() && Peek() != '"') {
            char c = Advance();
            if (c == '\n') {
                throw Error(m_sourceName, location, "Unterminated string");
            }
            if (c == '{') {
                Add(interpolated ? TokenKind::TemplateMiddle : TokenKind::TemplateStart, std::move(text), location);
                text.clear();
                interpolated = true;
                Interpolation(location);
                continue;
            }
            if (c == '\\') {
                if (m_offset == m_source.size()) {
                    throw Error(m_sourceName, location, "Unterminated string escape");
                }
                c = Advance();
                switch (c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                case '\\':
                case '"':
                case '{':
                case '}':
                    break;
                default:
                    throw Error(m_sourceName, m_location, "Unknown string escape");
                }
            }
            text.push_back(c);
        }
        if (m_offset == m_source.size()) {
            throw Error(m_sourceName, location, "Unterminated string");
        }
        Advance();
        Add(interpolated ? TokenKind::TemplateEnd : TokenKind::String, std::move(text), location);
    }
    void Number(SourceLocation location) {
        const std::size_t begin = m_offset - 1;
        while (IsDigit(Peek())) {
            Advance();
        }
        if (Peek() == '.' && IsDigit(Peek(1))) {
            Advance();
            while (IsDigit(Peek())) {
                Advance();
            }
        }
        if (Peek() == 'e' || Peek() == 'E') {
            Advance();
            if (Peek() == '+' || Peek() == '-') {
                Advance();
            }
            if (!IsDigit(Peek())) {
                throw Error(m_sourceName, location, "Expected exponent digits");
            }
            while (IsDigit(Peek())) {
                Advance();
            }
        }
        Add(TokenKind::Number, std::string(m_source.substr(begin, m_offset - begin)), location);
    }
    void Identifier(SourceLocation location) {
        const std::size_t begin = m_offset - 1;
        while (IsNameStart(Peek()) || IsDigit(Peek())) {
            Advance();
        }
        const std::string text(m_source.substr(begin, m_offset - begin));
        TokenKind kind = TokenKind::Identifier;
        if (const auto keyword = FindKeyword(text)) {
            switch (*keyword) {
            case Keyword::While:
                kind = TokenKind::While;
                break;
            case Keyword::For:
                kind = TokenKind::For;
                break;
            case Keyword::In:
                kind = TokenKind::In;
                break;
            case Keyword::Break:
                kind = TokenKind::Break;
                break;
            case Keyword::Continue:
                kind = TokenKind::Continue;
                break;
            case Keyword::Export:
                kind = TokenKind::Export;
                break;
            case Keyword::Is:
                kind = TokenKind::Is;
                break;
            case Keyword::Yield:
                kind = TokenKind::Yield;
                break;
            case Keyword::Enum:
                kind = TokenKind::Enum;
                break;
            case Keyword::Match:
                kind = TokenKind::Match;
                break;
            case Keyword::Override:
                kind = TokenKind::Override;
                break;
            case Keyword::Import:
                kind = TokenKind::Import;
                break;
            case Keyword::As:
                kind = TokenKind::As;
                break;
            case Keyword::Class:
                kind = TokenKind::Class;
                break;
            case Keyword::Extends:
                kind = TokenKind::Extends;
                break;
            case Keyword::Self:
                kind = TokenKind::Self;
                break;
            case Keyword::Super:
                kind = TokenKind::Super;
                break;
            case Keyword::Func:
                kind = TokenKind::Func;
                break;
            case Keyword::Var:
                kind = TokenKind::Var;
                break;
            case Keyword::Const:
                kind = TokenKind::Const;
                break;
            case Keyword::Return:
                kind = TokenKind::Return;
                break;
            case Keyword::If:
                kind = TokenKind::If;
                break;
            case Keyword::Else:
                kind = TokenKind::Else;
                break;
            case Keyword::Null:
                kind = TokenKind::Null;
                break;
            case Keyword::True:
                kind = TokenKind::True;
                break;
            case Keyword::False:
                kind = TokenKind::False;
                break;
            case Keyword::Not:
                kind = TokenKind::Bang;
                break;
            case Keyword::And:
                kind = TokenKind::And;
                break;
            case Keyword::Or:
                kind = TokenKind::Or;
                break;
            }
        }
        Add(kind, text, location);
    }

    std::string_view m_source;
    std::string_view m_sourceName;
    std::size_t m_offset = 0;
    SourceLocation m_location;
    std::vector<Token> m_tokens;
};

} // namespace

std::vector<Token> Tokenize(std::string_view source, std::string_view sourceName) {
    return Lexer(source, sourceName).Run();
}

} // namespace Vice::Detail
