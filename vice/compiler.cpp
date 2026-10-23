#include "bytecode.h"
#include "syntax.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>

namespace Vice::Detail {
namespace {

Operation Operator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Plus:
    case TokenKind::PlusEqual:
        return Operation::Add;
    case TokenKind::Minus:
    case TokenKind::MinusEqual:
        return Operation::Subtract;
    case TokenKind::Star:
    case TokenKind::StarEqual:
        return Operation::Multiply;
    case TokenKind::Slash:
    case TokenKind::SlashEqual:
        return Operation::Divide;
    case TokenKind::Percent:
    case TokenKind::PercentEqual:
        return Operation::Modulo;
    case TokenKind::EqualEqual:
        return Operation::Equal;
    case TokenKind::BangEqual:
        return Operation::NotEqual;
    case TokenKind::Less:
        return Operation::Less;
    case TokenKind::LessEqual:
        return Operation::LessEqual;
    case TokenKind::Greater:
        return Operation::Greater;
    case TokenKind::GreaterEqual:
        return Operation::GreaterEqual;
    default:
        throw std::logic_error("Invalid compiler operator");
    }
}

class Compiler {
public:
    explicit Compiler(std::string_view sourceName, Compiler* parent = nullptr)
        : m_sourceName(sourceName), m_parent(parent) {
        m_scopes.emplace_back();
    }

    CompiledFunction Compile(const Function& function) {
        CompiledFunction compiled{function.name, function.isOverride, function.parameters.size(),
                                  FunctionBody(function)};
        compiled.upvalues = std::move(m_upvalues);
        return compiled;
    }

    Bytecode FunctionBody(const Function& function) {
        for (const auto& parameter : function.parameters)
            AddLocal(parameter.text, false);
        Body(function.body);
        Constant({}, function.name);
        Emit(Opcode::Return, function.name, 0, 0, -1);
        return std::move(m_code);
    }

    Bytecode Initializer(const Expression& expression) {
        ExpressionCode(expression);
        Emit(Opcode::Return, expression.token, 0, 0, -1);
        return std::move(m_code);
    }

private:
    std::uint32_t Index(std::size_t value) const {
        if (value > std::numeric_limits<std::uint32_t>::max())
            throw Error(m_sourceName, {}, "Bytecode size limit exceeded");
        return static_cast<std::uint32_t>(value);
    }
    std::size_t Emit(Opcode opcode, const Token& token, std::uint32_t operand = 0, std::uint32_t auxiliary = 0,
                     int effect = 0) {
        const auto offset = m_code.instructions.size();
        m_code.instructions.push_back({opcode, operand, auxiliary});
        m_code.locations.push_back(token.location);
        m_stack += effect;
        assert(m_stack >= 0);
        m_code.maxStack = std::max(m_code.maxStack, static_cast<std::size_t>(m_stack));
        return offset;
    }
    std::uint32_t Name(const std::string& name) {
        const auto [entry, inserted] = m_names.try_emplace(name, Index(m_code.names.size()));
        if (inserted)
            m_code.names.push_back(name);
        return entry->second;
    }
    void Constant(Value value, const Token& token) {
        const auto index = Index(m_code.constants.size());
        m_code.constants.push_back(std::move(value));
        Emit(Opcode::Constant, token, index, 0, 1);
    }
    std::optional<std::uint32_t> FindLocal(const std::string& name) const {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
            if (const auto found = scope->find(name); found != scope->end())
                return found->second;
        }
        return std::nullopt;
    }
    std::optional<std::uint32_t> FindUpvalue(const std::string& name) {
        if (!m_parent)
            return std::nullopt;
        UpvalueInfo info;
        if (const auto slot = m_parent->FindLocal(name))
            info = {true, *slot, m_parent->m_code.locals[*slot].constant};
        else if (const auto upvalue = m_parent->FindUpvalue(name))
            info = {false, *upvalue, m_parent->m_upvalues[*upvalue].constant};
        else
            return std::nullopt;
        for (std::size_t i = 0; i < m_upvalues.size(); ++i) {
            if (m_upvalues[i].local == info.local && m_upvalues[i].index == info.index)
                return Index(i);
        }
        m_upvalues.push_back(info);
        return Index(m_upvalues.size() - 1);
    }
    void Closure(const Expression& expression) {
        Compiler child(m_sourceName, this);
        const auto index = Index(m_code.functions.size());
        m_code.functions.push_back(std::make_shared<CompiledFunction>(child.Compile(*expression.function)));
        Emit(Opcode::MakeClosure, expression.token, index, 0, 1);
    }
    std::uint32_t AddLocal(const std::string& name, bool constant) {
        const auto index = Index(m_code.locals.size());
        m_code.locals.push_back({name, constant});
        m_scopes.back().emplace(name, index);
        return index;
    }
    void Patch(std::size_t jump) { m_code.instructions[jump].operand = Index(m_code.instructions.size()); }

    void ExpressionCode(const Expression& expression) {
        const auto& token = expression.token;
        switch (expression.kind) {
        case Expression::Kind::Array:
        case Expression::Kind::Map: {
            for (const auto& child : expression.children)
                ExpressionCode(*child);
            const auto count = Index(expression.children.size());
            Emit(expression.kind == Expression::Kind::Array ? Opcode::MakeArray : Opcode::MakeMap, token, count, 0,
                 1 - static_cast<int>(count));
            break;
        }
        case Expression::Kind::Index:
            ExpressionCode(*expression.children[0]);
            ExpressionCode(*expression.children[1]);
            Emit(Opcode::GetIndex, token, 0, 0, -1);
            break;
        case Expression::Kind::Literal:
            Constant(expression.value, token);
            break;
        case Expression::Kind::Function:
            Closure(expression);
            break;
        case Expression::Kind::Interpolation: {
            for (const auto& child : expression.children)
                ExpressionCode(*child);
            const auto count = Index(expression.children.size());
            Emit(Opcode::Concatenate, token, count, 0, 1 - static_cast<int>(count));
            break;
        }
        case Expression::Kind::Name:
            if (const auto slot = FindLocal(token.text))
                Emit(Opcode::LoadLocal, token, *slot, 0, 1);
            else if (const auto upvalue = FindUpvalue(token.text))
                Emit(Opcode::LoadUpvalue, token, *upvalue, 0, 1);
            else
                Emit(Opcode::LoadName, token, Name(token.text), 0, 1);
            break;
        case Expression::Kind::Member:
            ExpressionCode(*expression.children[0]);
            Emit(Opcode::GetMember, token, Name(token.text));
            break;
        case Expression::Kind::Invoke:
            for (const auto& child : expression.children)
                ExpressionCode(*child);
            Emit(Opcode::CallValue, token, 0, Index(expression.children.size() - 1),
                 1 - static_cast<int>(expression.children.size()));
            break;
        case Expression::Kind::Call:
        case Expression::Kind::MethodCall: {
            const bool method = expression.kind == Expression::Kind::MethodCall;
            if (!method && !FindLocal(token.text)) {
                if (const auto upvalue = FindUpvalue(token.text)) {
                    Emit(Opcode::LoadUpvalue, token, *upvalue, 0, 1);
                    for (const auto& child : expression.children)
                        ExpressionCode(*child);
                    Emit(Opcode::CallValue, token, 0, Index(expression.children.size()),
                         -static_cast<int>(expression.children.size()));
                    break;
                }
            }
            for (const auto& child : expression.children)
                ExpressionCode(*child);
            const auto count = Index(expression.children.size() - (method ? 1 : 0));
            const auto slot = method ? std::nullopt : FindLocal(token.text);
            Emit(method ? Opcode::CallMember
                 : slot ? Opcode::CallLocal
                        : Opcode::Call,
                 token, slot ? *slot : Name(token.text), count, 1 - static_cast<int>(expression.children.size()));
            break;
        }
        case Expression::Kind::Unary:
            ExpressionCode(*expression.children[0]);
            Emit(Opcode::Unary, token, token.kind == TokenKind::Bang ? 0 : token.kind == TokenKind::Minus ? 1 : 2);
            break;
        case Expression::Kind::Binary:
            ExpressionCode(*expression.children[0]);
            if (token.kind == TokenKind::Is) {
                // `value is Vector3` checks a value type; otherwise the right side evaluates to a class.
                const auto& type = *expression.children[1];
                if (type.kind == Expression::Kind::Name && IsConstructor(type.token.text) &&
                    type.token.text != "range") {
                    const std::uint32_t kind = type.token.text == "Vector2" ? 1 : type.token.text == "Vector3" ? 2 : 3;
                    Emit(Opcode::IsType, token, kind);
                } else {
                    ExpressionCode(type);
                    Emit(Opcode::IsInstance, token, 0, 0, -1);
                }
            } else if (token.kind == TokenKind::And || token.kind == TokenKind::Or) {
                const auto jump = Emit(token.kind == TokenKind::And ? Opcode::JumpFalse : Opcode::JumpTrue, token);
                Emit(Opcode::Pop, token, 0, 0, -1);
                ExpressionCode(*expression.children[1]);
                Emit(Opcode::Boolean, token);
                Patch(jump);
            } else {
                ExpressionCode(*expression.children[1]);
                Emit(Opcode::Binary, token, static_cast<std::uint32_t>(Operator(token.kind)), 0, -1);
            }
            break;
        }
    }

    void Target(const Expression& expression, bool read) {
        const auto& token = expression.token;
        if (expression.kind == Expression::Kind::Name) {
            if (const auto slot = FindLocal(token.text))
                Emit(Opcode::LocalTarget, token, *slot);
            else if (const auto upvalue = FindUpvalue(token.text))
                Emit(Opcode::UpvalueTarget, token, *upvalue);
            else
                Emit(Opcode::NameTarget, token, Name(token.text), read ? 1 : 0);
        } else if (expression.kind == Expression::Kind::Member) {
            Target(*expression.children[0], true);
            Emit(Opcode::MemberTarget, token, Name(token.text), read ? 1 : 0);
        } else if (expression.kind == Expression::Kind::Index) {
            Target(*expression.children[0], true);
            ExpressionCode(*expression.children[1]);
            Emit(Opcode::IndexTarget, token, 0, read ? 1 : 0, -1);
        } else {
            ExpressionCode(expression);
            Emit(Opcode::TemporaryTarget, token, 0, 0, -1);
        }
    }

    void Scoped(const std::vector<Statement>& statements) {
        m_scopes.emplace_back();
        Body(statements);
        for (const auto& [name, slot] : m_scopes.back()) {
            (void)name;
            Emit(Opcode::ClearLocal, {}, slot);
        }
        m_scopes.pop_back();
    }
    void ClearScopes(std::size_t depth, const Token& token) {
        for (std::size_t i = m_scopes.size(); i > depth; --i) {
            for (const auto& [name, slot] : m_scopes[i - 1]) {
                (void)name;
                Emit(Opcode::ClearLocal, token, slot);
            }
        }
    }
    void Loop(const Statement& statement) {
        const auto& token = statement.token;
        const bool iteration = statement.kind == Statement::Kind::For;
        std::uint32_t iterator = 0;
        std::vector<std::uint32_t> variables;
        if (iteration) {
            m_scopes.emplace_back();
            ExpressionCode(*statement.expression);
            Emit(Opcode::IteratorBegin, token);
            iterator = AddLocal("$iterator", true);
            Emit(Opcode::DeclareLocal, token, iterator, 0, -1);
            for (const auto& variable : statement.variables)
                variables.push_back(AddLocal(variable.text, false));
        }
        const auto begin = Index(m_code.instructions.size());
        if (iteration)
            Emit(Opcode::IteratorNext, token, iterator, Index(variables.size()),
                 static_cast<int>(variables.size()) + 1);
        else
            ExpressionCode(*statement.expression);
        const auto end = Emit(Opcode::JumpFalse, token);
        Emit(Opcode::Pop, token, 0, 0, -1);
        for (auto slot = variables.rbegin(); slot != variables.rend(); ++slot)
            Emit(Opcode::DeclareLocal, token, *slot, 0, -1);
        m_loops.push_back({begin, m_scopes.size(), {}});
        Scoped(statement.body);
        Emit(Opcode::Jump, token, begin);
        Patch(end);
        // Both the condition and yielded values remain on the exhausted edge.
        for (std::size_t i = 0; i <= variables.size(); ++i)
            Emit(Opcode::Pop, token);
        for (const auto jump : m_loops.back().breaks)
            Patch(jump);
        m_loops.pop_back();
        if (iteration) {
            ClearScopes(m_scopes.size() - 1, token);
            m_scopes.pop_back();
        }
    }
    void Body(const std::vector<Statement>& statements) {
        for (const auto& statement : statements) {
            const auto& token = statement.token;
            switch (statement.kind) {
            case Statement::Kind::While:
            case Statement::Kind::For:
                Loop(statement);
                break;
            case Statement::Kind::Break:
            case Statement::Kind::Continue:
                if (m_loops.empty())
                    throw Error(m_sourceName, token.location, "Loop control requires an enclosing loop");
                ClearScopes(m_loops.back().scopeDepth, token);
                if (statement.kind == Statement::Kind::Break)
                    m_loops.back().breaks.push_back(Emit(Opcode::Jump, token));
                else
                    Emit(Opcode::Jump, token, m_loops.back().begin);
                break;
            case Statement::Kind::Variable: {
                if (statement.expression)
                    ExpressionCode(*statement.expression);
                else
                    Constant({}, token);
                if (m_scopes.back().contains(token.text))
                    Emit(Opcode::DuplicateDeclaration, token, Name(token.text), 0, -1);
                else
                    Emit(Opcode::DeclareLocal, token, AddLocal(token.text, statement.constant), 0, -1);
                break;
            }
            case Statement::Kind::Function: {
                if (m_scopes.back().contains(token.text)) {
                    Constant({}, token);
                    Emit(Opcode::DuplicateDeclaration, token, Name(token.text), 0, -1);
                    break;
                }
                Constant({}, token);
                const auto slot = AddLocal(token.text, false);
                Emit(Opcode::DeclareLocal, token, slot, 0, -1);
                Emit(Opcode::LocalTarget, token, slot);
                Closure(*statement.expression);
                Emit(Opcode::StoreTarget, token, 0, 0, -1);
                break;
            }
            case Statement::Kind::Assignment:
                Target(*statement.target, token.kind != TokenKind::Equal);
                if (token.kind != TokenKind::Equal)
                    Emit(Opcode::LoadTarget, statement.target->token, 0, 0, 1);
                ExpressionCode(*statement.expression);
                if (token.kind != TokenKind::Equal)
                    Emit(Opcode::Binary, token, static_cast<std::uint32_t>(Operator(token.kind)), 0, -1);
                Emit(Opcode::StoreTarget, token, 0, 0, -1);
                break;
            case Statement::Kind::Expression:
                ExpressionCode(*statement.expression);
                Emit(Opcode::Pop, token, 0, 0, -1);
                break;
            case Statement::Kind::Yield:
                if (statement.expression)
                    ExpressionCode(*statement.expression);
                else
                    Constant({}, token);
                Emit(Opcode::Yield, token, 0, 0, -1);
                break;
            case Statement::Kind::Return:
                if (statement.expression)
                    ExpressionCode(*statement.expression);
                else
                    Constant({}, token);
                Emit(Opcode::Return, token, 0, 0, -1);
                break;
            case Statement::Kind::Block:
                Scoped(statement.body);
                break;
            case Statement::Kind::Match: {
                // The subject is evaluated once into a hidden local; cases test it in order and the first match runs.
                m_scopes.emplace_back();
                ExpressionCode(*statement.expression);
                const auto subject = AddLocal("$match", true);
                Emit(Opcode::DeclareLocal, token, subject, 0, -1);
                std::vector<std::size_t> ends;
                for (const auto& matchCase : statement.cases) {
                    std::vector<std::size_t> hits;
                    for (std::size_t i = 0; i < matchCase.patterns.size(); ++i) {
                        const auto& pattern = *matchCase.patterns[i];
                        Emit(Opcode::LoadLocal, pattern.token, subject, 0, 1);
                        if (matchCase.typeChecks[i]) {
                            if (pattern.kind == Expression::Kind::Name && IsConstructor(pattern.token.text) &&
                                pattern.token.text != "range") {
                                const std::uint32_t kind = pattern.token.text == "Vector2"   ? 1
                                                           : pattern.token.text == "Vector3" ? 2
                                                                                             : 3;
                                Emit(Opcode::IsType, pattern.token, kind);
                            } else {
                                ExpressionCode(pattern);
                                Emit(Opcode::IsInstance, pattern.token, 0, 0, -1);
                            }
                        } else {
                            ExpressionCode(pattern);
                            Emit(Opcode::Binary, pattern.token, static_cast<std::uint32_t>(Operation::Equal), 0, -1);
                        }
                        hits.push_back(Emit(Opcode::JumpTrue, pattern.token));
                        Emit(Opcode::Pop, pattern.token, 0, 0, -1);
                    }
                    const auto next = Emit(Opcode::Jump, matchCase.token);
                    for (const auto hit : hits)
                        Patch(hit);
                    // Jumps arrive with the true condition still on the stack.
                    Emit(Opcode::Pop, matchCase.token);
                    Scoped(matchCase.body);
                    ends.push_back(Emit(Opcode::Jump, matchCase.token));
                    Patch(next);
                }
                Scoped(statement.elseBody);
                for (const auto end : ends)
                    Patch(end);
                ClearScopes(m_scopes.size() - 1, token);
                m_scopes.pop_back();
                break;
            }
            case Statement::Kind::If: {
                ExpressionCode(*statement.expression);
                const auto branch = Emit(Opcode::JumpFalse, token);
                Emit(Opcode::Pop, token, 0, 0, -1);
                Scoped(statement.body);
                const auto end = Emit(Opcode::Jump, token);
                Patch(branch);
                // The false edge still owns the condition value.
                Emit(Opcode::Pop, token);
                Scoped(statement.elseBody);
                Patch(end);
                break;
            }
            }
        }
    }

    std::string m_sourceName;
    Compiler* m_parent = nullptr;
    std::vector<UpvalueInfo> m_upvalues;
    Bytecode m_code;
    std::vector<std::unordered_map<std::string, std::uint32_t>> m_scopes;
    std::unordered_map<std::string, std::uint32_t> m_names;
    std::ptrdiff_t m_stack = 0;
    struct LoopContext {
        std::uint32_t begin;
        std::size_t scopeDepth;
        std::vector<std::size_t> breaks;
    };
    std::vector<LoopContext> m_loops;
};

} // namespace

std::shared_ptr<const ProgramData> ParseProgram(std::string_view source, std::string_view sourceName) {
    const auto parsed = ParseSyntax(source, sourceName);
    auto program = std::make_shared<ProgramData>();
    program->sourceName = parsed->sourceName;
    for (const auto& import : parsed->imports)
        program->imports.push_back({import.name, import.request});
    const auto expression = [&](const std::unique_ptr<Expression>& node) -> std::shared_ptr<const Bytecode> {
        return node ? std::make_shared<Bytecode>(Compiler(sourceName).Initializer(*node)) : nullptr;
    };
    const auto variable = [&](const Statement& node) {
        return CompiledVariable{node.token, node.constant, expression(node.expression), node.exported,
                                node.exportDefault};
    };
    const auto function = [&](const Function& node) { return Compiler(sourceName).Compile(node); };
    for (const auto& node : parsed->globals)
        program->globals.push_back(variable(node));
    for (const auto& node : parsed->functions) {
        program->functionIndices.emplace(node.name.text, program->functions.size());
        program->functions.push_back(function(node));
    }
    for (const auto& node : parsed->enums) {
        CompiledEnum declaration{node.name, {}};
        for (const auto& member : node.members)
            declaration.members.push_back(member.text);
        program->enums.push_back(std::move(declaration));
    }
    for (const auto& node : parsed->classes) {
        CompiledClass type;
        type.name = node.name;
        type.base = expression(node.base);
        for (const auto& field : node.fields)
            type.fields.push_back(variable(field));
        for (const auto& method : node.methods)
            type.methods.push_back(function(method));
        program->classes.push_back(std::move(type));
    }
    return program;
}

} // namespace Vice::Detail
