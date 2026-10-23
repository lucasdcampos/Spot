#include "runtime.h"
#include "syntax.h"
#include <vice/language.h>

#include <charconv>
#include <optional>
#include <unordered_set>
#include <utility>

namespace Vice::Detail {
namespace {

class Parser {
public:
    Parser(std::string_view source, std::string_view sourceName)
        : m_tokens(Tokenize(source, sourceName)), m_program(std::make_shared<ParsedProgram>()) {
        m_program->sourceName = sourceName;
    }

    std::shared_ptr<const ParsedProgram> Run() {
        std::unordered_set<std::string> names;
        Separators();
        while (!Is(TokenKind::End)) {
            if (Match(TokenKind::Import)) {
                Import import;
                if (!Is(TokenKind::Identifier) && !Is(TokenKind::String))
                    Fail(Current(), "Expected a library name or quoted module path");
                import.name = m_tokens[m_offset++];
                import.request = import.name.text;
                if (import.request.empty() || import.request.find_first_of("\r\n") != std::string::npos ||
                    import.request.find('\0') != std::string::npos)
                    Fail(import.name, "Invalid module path");
                if (Match(TokenKind::As)) {
                    import.name = Expect(TokenKind::Identifier, "Expected an import alias");
                } else {
                    const auto separator = import.name.text.find_last_of("/\\");
                    if (separator != std::string::npos)
                        import.name.text.erase(0, separator + 1);
                    const auto extension = import.name.text.find_last_of('.');
                    if (extension != std::string::npos)
                        import.name.text.erase(extension);
                    const auto validStart = [](char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
                    };
                    if (import.name.text.empty() || !validStart(import.name.text.front()) ||
                        FindKeyword(import.name.text))
                        Fail(import.name, "This module path requires an explicit alias");
                    for (char c : import.name.text) {
                        if (!validStart(c) && !(c >= '0' && c <= '9'))
                            Fail(import.name, "This module path requires an explicit alias");
                    }
                }
                if (IsConstructor(import.name.text))
                    Fail(import.name, "Name is reserved for a value constructor");
                Declare(names, import.name);
                m_program->imports.push_back(std::move(import));
            } else if (Match(TokenKind::Func)) {
                auto function = ParseFunction();
                if (IsConstructor(function.name.text))
                    Fail(function.name, "Name is reserved for a value constructor");
                Declare(names, function.name);
                m_program->functions.push_back(std::move(function));
            } else if (Match(TokenKind::Class)) {
                Class type;
                type.name = Expect(TokenKind::Identifier, "Expected class name");
                if (IsConstructor(type.name.text))
                    Fail(type.name, "Name is reserved for a value constructor");
                Declare(names, type.name);
                if (Match(TokenKind::Colon) || Match(TokenKind::Extends)) {
                    type.base = Primary();
                    if (type.base->kind != Expression::Kind::Name)
                        Fail(type.name, "Expected base class name");
                    std::size_t members = 0;
                    while (Match(TokenKind::Dot)) {
                        if (++members > 128)
                            Fail(Current(), "Base class expression is too long");
                        auto member = std::make_unique<Expression>();
                        member->kind = Expression::Kind::Member;
                        member->token = Expect(TokenKind::Identifier, "Expected base class name");
                        member->children.push_back(std::move(type.base));
                        type.base = std::move(member);
                    }
                }
                Newlines();
                Expect(TokenKind::LeftBrace, "Expected '{' after class name");
                Separators();
                std::unordered_set<std::string> members;
                while (!Is(TokenKind::RightBrace)) {
                    const bool isExport = Match(TokenKind::Export);
                    const bool isOverride = !isExport && Match(TokenKind::Override);
                    if (isOverride || isExport)
                        Newlines();
                    if (isExport && !Is(TokenKind::Var))
                        Fail(Current(), "Only var fields can be exported");
                    if (Match(TokenKind::Func)) {
                        auto method = ParseFunction();
                        method.isOverride = isOverride;
                        if (isOverride && method.name.text == "init")
                            Fail(method.name, "Constructors cannot use override");
                        Declare(members, method.name);
                        type.methods.push_back(std::move(method));
                    } else if (!isOverride && (Is(TokenKind::Var) || Is(TokenKind::Const))) {
                        auto field = Variable();
                        if (field.token.text == "init")
                            Fail(field.token, "The init member must be a method");
                        if (isExport) {
                            const auto value = field.expression ? ConstantValue(*field.expression) : std::nullopt;
                            if (!value)
                                Fail(field.token, "An exported field needs a constant boolean, number, string, "
                                                  "Vector2, Vector3 or Color initializer");
                            field.exported = true;
                            field.exportDefault = *value;
                        }
                        Declare(members, field.token);
                        type.fields.push_back(std::move(field));
                    } else {
                        Fail(Current(), "Expected a field or method declaration");
                    }
                    EndStatement();
                }
                Expect(TokenKind::RightBrace, "Expected '}'");
                m_program->classes.push_back(std::move(type));
            } else if (Match(TokenKind::Enum)) {
                EnumDeclaration declaration;
                declaration.name = Expect(TokenKind::Identifier, "Expected enum name");
                if (IsConstructor(declaration.name.text))
                    Fail(declaration.name, "Name is reserved for a value constructor");
                Declare(names, declaration.name);
                Newlines();
                Expect(TokenKind::LeftBrace, "Expected '{' after enum name");
                Separators();
                std::unordered_set<std::string> members;
                while (!Is(TokenKind::RightBrace)) {
                    auto member = Expect(TokenKind::Identifier, "Expected an enum member name");
                    if (member.text == "values")
                        Fail(member, "'values' is reserved for the list of an enum's members");
                    Declare(members, member);
                    declaration.members.push_back(std::move(member));
                    Match(TokenKind::Comma);
                    Separators();
                }
                Expect(TokenKind::RightBrace, "Expected '}' after enum members");
                if (declaration.members.empty())
                    Fail(declaration.name, "An enum needs at least one member");
                m_program->enums.push_back(std::move(declaration));
            } else if (Is(TokenKind::Var) || Is(TokenKind::Const)) {
                Statement declaration = Variable();
                if (IsConstructor(declaration.token.text))
                    Fail(declaration.token, "Name is reserved for a value constructor");
                Declare(names, declaration.token);
                m_program->globals.push_back(std::move(declaration));
            } else {
                Fail(Current(), "Only import, var, const, func and class declarations are allowed at file scope");
            }
            EndStatement();
        }
        return m_program;
    }

private:
    // The value of a literal initializer, as tools show it before the script runs.
    static std::optional<Value> ConstantValue(const Expression& expression) {
        const auto number = [](const Expression& node) -> std::optional<double> {
            const Expression* literal = &node;
            double sign = 1;
            if (node.kind == Expression::Kind::Unary && node.token.kind == TokenKind::Minus) {
                literal = node.children[0].get();
                sign = -1;
            }
            const auto* value =
                literal->kind == Expression::Kind::Literal ? std::get_if<double>(&literal->value) : nullptr;
            return value ? std::optional<double>(sign * *value) : std::nullopt;
        };
        if (expression.kind == Expression::Kind::Literal) {
            if (std::holds_alternative<bool>(expression.value) || std::holds_alternative<double>(expression.value) ||
                std::holds_alternative<std::string>(expression.value))
                return expression.value;
            return std::nullopt;
        }
        if (const auto value = number(expression))
            return *value;
        if (expression.kind != Expression::Kind::Call || !IsConstructor(expression.token.text) ||
            expression.token.text == "range")
            return std::nullopt;
        std::vector<Value> arguments;
        for (const auto& child : expression.children) {
            const auto value = number(*child);
            if (!value)
                return std::nullopt;
            arguments.push_back(*value);
        }
        try {
            return Construct(expression.token.text, arguments);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    Function ParseFunction() {
        Function function;
        function.name = Expect(TokenKind::Identifier, "Expected function name");
        ParseSignatureAndBody(function);
        return function;
    }
    void ParseSignatureAndBody(Function& function) {
        Expect(TokenKind::LeftParen, "Expected '(' after function name");
        Newlines();
        std::unordered_set<std::string> parameters;
        if (!Is(TokenKind::RightParen)) {
            do {
                Newlines();
                Token parameter = Expect(TokenKind::Identifier, "Expected parameter name");
                Declare(parameters, parameter);
                function.parameters.push_back(std::move(parameter));
                Newlines();
            } while (Match(TokenKind::Comma));
        }
        Expect(TokenKind::RightParen, "Expected ')' after parameters");
        Newlines();
        function.body = Block();
    }
    struct DepthScope {
        explicit DepthScope(Parser& parser) : parser(parser) {
            if (++parser.m_depth > 64) {
                parser.Fail(parser.Current(), "Syntax nesting limit exceeded");
            }
        }
        ~DepthScope() { --parser.m_depth; }
        Parser& parser;
    };

    const Token& Current() const { return m_tokens[m_offset]; }
    bool Is(TokenKind kind) const { return Current().kind == kind; }
    bool Match(TokenKind kind) {
        if (!Is(kind))
            return false;
        ++m_offset;
        return true;
    }
    Token Expect(TokenKind kind, std::string_view message) {
        if (!Is(kind))
            Fail(Current(), message);
        return m_tokens[m_offset++];
    }
    [[noreturn]] void Fail(const Token& token, std::string_view message) const {
        throw Error(m_program->sourceName, token.location, message);
    }
    void Declare(std::unordered_set<std::string>& names, const Token& token) {
        if (!names.insert(token.text).second) {
            Fail(token, "Duplicate declaration '" + token.text + "'");
        }
    }
    void Newlines() {
        while (Match(TokenKind::Newline)) {
        }
    }
    void Separators() {
        while (Match(TokenKind::Newline) || Match(TokenKind::Semicolon)) {
        }
    }
    void EndStatement() {
        if (!Is(TokenKind::End) && !Is(TokenKind::RightBrace) && !Is(TokenKind::Newline) && !Is(TokenKind::Semicolon)) {
            Fail(Current(), "Expected a newline or ';' after statement");
        }
        Separators();
    }
    std::vector<Statement> Block() {
        const DepthScope depth(*this);
        Expect(TokenKind::LeftBrace, "Expected '{'");
        Separators();
        std::vector<Statement> body;
        while (!Is(TokenKind::RightBrace)) {
            if (Is(TokenKind::End))
                Fail(Current(), "Expected '}'");
            body.push_back(ParseStatement());
            EndStatement();
        }
        Expect(TokenKind::RightBrace, "Expected '}'");
        return body;
    }
    Statement Variable() {
        const bool constant = Match(TokenKind::Const);
        if (!constant)
            Expect(TokenKind::Var, "Expected 'var'");
        Statement statement;
        statement.kind = Statement::Kind::Variable;
        statement.constant = constant;
        statement.token = Expect(TokenKind::Identifier, "Expected variable name");
        if (Match(TokenKind::Equal)) {
            statement.expression = ParseExpression();
        } else if (constant) {
            Fail(statement.token, "A const declaration requires an initializer");
        }
        return statement;
    }
    Statement ParseStatement() {
        const DepthScope depth(*this);
        if (Is(TokenKind::Var) || Is(TokenKind::Const))
            return Variable();
        Statement statement;
        statement.token = Current();
        if (Is(TokenKind::LeftBrace)) {
            statement.kind = Statement::Kind::Block;
            statement.body = Block();
        } else if (Match(TokenKind::While)) {
            statement.kind = Statement::Kind::While;
            statement.expression = ParseExpression();
            Newlines();
            statement.body = Block();
        } else if (Match(TokenKind::For)) {
            statement.kind = Statement::Kind::For;
            statement.variables.push_back(Expect(TokenKind::Identifier, "Expected loop variable"));
            if (Match(TokenKind::Comma)) {
                statement.variables.push_back(Expect(TokenKind::Identifier, "Expected second loop variable"));
                if (statement.variables[0].text == statement.variables[1].text)
                    Fail(statement.variables[1], "Duplicate loop variable");
            }
            Expect(TokenKind::In, "Expected 'in' after loop variables");
            statement.expression = ParseExpression();
            Newlines();
            statement.body = Block();
        } else if (Is(TokenKind::Func) && m_tokens[m_offset + 1].kind == TokenKind::Identifier) {
            // A local function binds its name before its body, so it can call itself.
            ++m_offset;
            auto function = std::make_shared<Function>(ParseFunction());
            if (IsConstructor(function->name.text))
                Fail(function->name, "Name is reserved for a value constructor");
            statement.kind = Statement::Kind::Function;
            statement.token = function->name;
            statement.expression = std::make_unique<Expression>();
            statement.expression->kind = Expression::Kind::Function;
            statement.expression->token = function->name;
            statement.expression->function = std::move(function);
        } else if (Match(TokenKind::Break)) {
            statement.kind = Statement::Kind::Break;
        } else if (Match(TokenKind::Continue)) {
            statement.kind = Statement::Kind::Continue;
        } else if (Match(TokenKind::If)) {
            statement.kind = Statement::Kind::If;
            statement.expression = ParseExpression();
            Newlines();
            statement.body = Block();
            const auto afterBlock = m_offset;
            Newlines();
            if (Match(TokenKind::Else)) {
                Newlines();
                if (Is(TokenKind::If))
                    statement.elseBody.push_back(ParseStatement());
                else
                    statement.elseBody = Block();
            } else {
                m_offset = afterBlock;
            }
        } else if (Match(TokenKind::Match)) {
            statement.kind = Statement::Kind::Match;
            statement.expression = ParseExpression();
            Newlines();
            Expect(TokenKind::LeftBrace, "Expected '{' after the matched value");
            Separators();
            while (!Is(TokenKind::RightBrace)) {
                if (Is(TokenKind::End))
                    Fail(Current(), "Expected '}'");
                if (statement.hasElse)
                    Fail(Current(), "The else case must be the last one");
                if (Match(TokenKind::Else)) {
                    Newlines();
                    statement.elseBody = Block();
                    statement.hasElse = true;
                    Separators();
                    continue;
                }
                MatchCase matchCase;
                matchCase.token = Current();
                do {
                    Newlines();
                    const bool typeCheck = Match(TokenKind::Is);
                    matchCase.patterns.push_back(ParseExpression());
                    matchCase.typeChecks.push_back(typeCheck);
                    if (matchCase.patterns.size() > 64)
                        Fail(matchCase.token, "A case has too many patterns");
                } while (Match(TokenKind::Comma));
                Newlines();
                matchCase.body = Block();
                statement.cases.push_back(std::move(matchCase));
                Separators();
            }
            Expect(TokenKind::RightBrace, "Expected '}' after match cases");
        } else if (Match(TokenKind::Yield)) {
            statement.kind = Statement::Kind::Yield;
            if (!Is(TokenKind::Newline) && !Is(TokenKind::Semicolon) && !Is(TokenKind::RightBrace) &&
                !Is(TokenKind::End))
                statement.expression = ParseExpression();
        } else if (Match(TokenKind::Return)) {
            statement.kind = Statement::Kind::Return;
            if (!Is(TokenKind::Newline) && !Is(TokenKind::Semicolon) && !Is(TokenKind::RightBrace) &&
                !Is(TokenKind::End)) {
                statement.expression = ParseExpression();
            }
        } else {
            statement.kind = Statement::Kind::Expression;
            statement.expression = ParseExpression();
            if (Is(TokenKind::Equal) || Is(TokenKind::PlusEqual) || Is(TokenKind::MinusEqual) ||
                Is(TokenKind::StarEqual) || Is(TokenKind::SlashEqual) || Is(TokenKind::PercentEqual)) {
                statement.kind = Statement::Kind::Assignment;
                statement.token = m_tokens[m_offset++];
                if (statement.expression->kind != Expression::Kind::Name &&
                    statement.expression->kind != Expression::Kind::Member &&
                    statement.expression->kind != Expression::Kind::Index)
                    Fail(statement.token, "Expected an assignable variable or property");
                statement.target = std::move(statement.expression);
                statement.expression = ParseExpression();
            }
        }
        return statement;
    }
    static int Precedence(TokenKind kind) {
        if (kind == TokenKind::Or)
            return 1;
        if (kind == TokenKind::And)
            return 2;
        if (kind == TokenKind::EqualEqual || kind == TokenKind::BangEqual)
            return 3;
        if (kind == TokenKind::Less || kind == TokenKind::LessEqual || kind == TokenKind::Greater ||
            kind == TokenKind::GreaterEqual || kind == TokenKind::Is)
            return 4;
        if (kind == TokenKind::Plus || kind == TokenKind::Minus)
            return 5;
        if (kind == TokenKind::Star || kind == TokenKind::Slash || kind == TokenKind::Percent)
            return 6;
        return 0;
    }
    std::unique_ptr<Expression> ParseExpression(int minimum = 1) {
        const DepthScope depth(*this);
        auto expression = Unary();
        std::size_t operators = 0;
        while (Precedence(Current().kind) >= minimum) {
            if (++operators > 128)
                Fail(Current(), "Expression is too long");
            auto binary = std::make_unique<Expression>();
            binary->kind = Expression::Kind::Binary;
            binary->token = m_tokens[m_offset++];
            binary->children.push_back(std::move(expression));
            // An operator at the end of a line continues the expression on the next one.
            Newlines();
            binary->children.push_back(ParseExpression(Precedence(binary->token.kind) + 1));
            expression = std::move(binary);
        }
        return expression;
    }
    std::unique_ptr<Expression> Unary() {
        const DepthScope depth(*this);
        if (Is(TokenKind::Minus) || Is(TokenKind::Plus) || Is(TokenKind::Bang)) {
            auto unary = std::make_unique<Expression>();
            unary->kind = Expression::Kind::Unary;
            unary->token = m_tokens[m_offset++];
            unary->children.push_back(Unary());
            return unary;
        }
        return Postfix();
    }
    std::unique_ptr<Expression> Postfix() {
        auto expression = Primary();
        std::size_t count = 0;
        while (Is(TokenKind::Dot) || Is(TokenKind::LeftParen) || Is(TokenKind::LeftBracket)) {
            if (++count > 128)
                Fail(Current(), "Expression is too long");
            if (Match(TokenKind::Dot)) {
                auto member = std::make_unique<Expression>();
                member->kind = Expression::Kind::Member;
                member->token = Expect(TokenKind::Identifier, "Expected property or method name after '.'");
                member->children.push_back(std::move(expression));
                expression = std::move(member);
            } else if (Is(TokenKind::LeftBracket)) {
                auto indexed = std::make_unique<Expression>();
                indexed->kind = Expression::Kind::Index;
                indexed->token = m_tokens[m_offset++];
                indexed->children.push_back(std::move(expression));
                Newlines();
                indexed->children.push_back(ParseExpression());
                Newlines();
                Expect(TokenKind::RightBracket, "Expected ']' after index");
                expression = std::move(indexed);
            } else {
                Expect(TokenKind::LeftParen, "Expected '('");
                if (expression->kind == Expression::Kind::Name)
                    expression->kind = Expression::Kind::Call;
                else if (expression->kind == Expression::Kind::Member)
                    expression->kind = Expression::Kind::MethodCall;
                else {
                    auto call = std::make_unique<Expression>();
                    call->kind = Expression::Kind::Invoke;
                    call->token = expression->token;
                    call->children.push_back(std::move(expression));
                    expression = std::move(call);
                }
                Newlines();
                if (!Is(TokenKind::RightParen)) {
                    do {
                        Newlines();
                        expression->children.push_back(ParseExpression());
                        Newlines();
                    } while (Match(TokenKind::Comma));
                }
                Expect(TokenKind::RightParen, "Expected ')' after arguments");
            }
        }
        return expression;
    }
    std::unique_ptr<Expression> Primary() {
        if (Match(TokenKind::LeftParen)) {
            Newlines();
            auto expression = ParseExpression();
            Newlines();
            Expect(TokenKind::RightParen, "Expected ')'");
            return expression;
        }
        auto expression = std::make_unique<Expression>();
        expression->token = Current();
        if (Is(TokenKind::TemplateStart)) {
            // "a {x} b" alternates literal parts and expressions, starting and ending with a literal.
            expression->kind = Expression::Kind::Interpolation;
            const auto literal = [&] {
                auto part = std::make_unique<Expression>();
                part->token = m_tokens[m_offset++];
                part->value = part->token.text;
                expression->children.push_back(std::move(part));
            };
            literal();
            for (;;) {
                if (expression->children.size() > 256)
                    Fail(expression->token, "String has too many interpolations");
                expression->children.push_back(ParseExpression());
                if (Is(TokenKind::TemplateMiddle)) {
                    literal();
                    continue;
                }
                if (!Is(TokenKind::TemplateEnd))
                    Fail(Current(), "Expected '}' after interpolated expression");
                literal();
                return expression;
            }
        }
        if (Match(TokenKind::Func)) {
            const DepthScope depth(*this);
            auto function = std::make_shared<Function>();
            function->name = expression->token;
            function->name.text = "<anonymous>";
            ParseSignatureAndBody(*function);
            expression->kind = Expression::Kind::Function;
            expression->function = std::move(function);
            return expression;
        }
        if (Is(TokenKind::LeftBracket) || Is(TokenKind::LeftBrace)) {
            const bool map = Match(TokenKind::LeftBrace);
            if (!map)
                Expect(TokenKind::LeftBracket, "Expected '['");
            expression->kind = map ? Expression::Kind::Map : Expression::Kind::Array;
            const auto end = map ? TokenKind::RightBrace : TokenKind::RightBracket;
            Newlines();
            while (!Is(end)) {
                expression->children.push_back(ParseExpression());
                Newlines();
                if (map) {
                    Expect(TokenKind::Colon, "Expected ':' between map key and value");
                    Newlines();
                    expression->children.push_back(ParseExpression());
                    Newlines();
                }
                if (!Match(TokenKind::Comma))
                    break;
                Newlines();
            }
            Expect(end, "Expected closing collection delimiter");
            return expression;
        }
        switch (Current().kind) {
        case TokenKind::Number: {
            const std::string& text = Current().text;
            double value = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
                Fail(Current(), "Invalid number");
            }
            expression->value = value;
            break;
        }
        case TokenKind::String:
            expression->value = Current().text;
            break;
        case TokenKind::True:
            expression->value = true;
            break;
        case TokenKind::False:
            expression->value = false;
            break;
        case TokenKind::Null:
            break;
        case TokenKind::Self:
        case TokenKind::Super:
        case TokenKind::Identifier:
            expression->kind = Expression::Kind::Name;
            break;
        default:
            Fail(Current(), "Expected expression");
        }
        ++m_offset;
        return expression;
    }

    std::vector<Token> m_tokens;
    std::size_t m_offset = 0;
    std::size_t m_depth = 0;
    std::shared_ptr<ParsedProgram> m_program;
};

} // namespace

std::shared_ptr<const ParsedProgram> ParseSyntax(std::string_view source, std::string_view sourceName) {
    return Parser(source, sourceName).Run();
}

} // namespace Vice::Detail
