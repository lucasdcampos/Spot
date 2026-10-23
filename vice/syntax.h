#pragma once

#include "token.h"

#include <memory>
#include <string>
#include <vector>

namespace Vice::Detail {

std::vector<Token> Tokenize(std::string_view source, std::string_view sourceName);

struct Function;

struct Expression {
    enum class Kind {
        Literal,
        Name,
        Unary,
        Binary,
        Call,
        Member,
        MethodCall,
        Invoke,
        Array,
        Map,
        Index,
        Function,
        Interpolation
    };
    Kind kind = Kind::Literal;
    Token token;
    Value value;
    std::vector<std::unique_ptr<Expression>> children;
    std::shared_ptr<const Function> function;
};

struct MatchCase;

struct Statement {
    enum class Kind {
        Variable,
        Assignment,
        Expression,
        Return,
        Block,
        If,
        While,
        For,
        Break,
        Continue,
        Function,
        Yield,
        Match
    };
    Kind kind = Kind::Expression;
    Token token;
    bool constant = false;
    // An `export var` field, editable by tools, with its constant initializer's value.
    bool exported = false;
    Value exportDefault;
    std::vector<Token> variables;
    std::unique_ptr<Expression> expression;
    std::unique_ptr<Expression> target;
    std::vector<Statement> body;
    std::vector<Statement> elseBody;
    // `match`: its cases, in order, and whether it has an else case.
    std::vector<MatchCase> cases;
    bool hasElse = false;
};

// One case of a `match`: values compared with ==, or types checked with `is` where typeChecks says so.
struct MatchCase {
    Token token;
    std::vector<std::unique_ptr<Expression>> patterns;
    std::vector<bool> typeChecks;
    std::vector<Statement> body;
};

struct EnumDeclaration {
    Token name;
    std::vector<Token> members;
};

struct Function {
    Token name;
    bool isOverride = false;
    std::vector<Token> parameters;
    std::vector<Statement> body;
};

struct Class {
    Token name;
    std::unique_ptr<Expression> base;
    std::vector<Statement> fields;
    std::vector<Function> methods;
};

struct Import {
    Token name;
    std::string request;
};

struct ParsedProgram {
    std::string sourceName;
    std::vector<Statement> globals;
    std::vector<Function> functions;
    std::vector<Import> imports;
    std::vector<Class> classes;
    std::vector<EnumDeclaration> enums;
};

std::shared_ptr<const ParsedProgram> ParseSyntax(std::string_view source, std::string_view sourceName);

} // namespace Vice::Detail
