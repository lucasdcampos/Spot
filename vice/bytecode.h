#pragma once

#include "runtime.h"
#include "token.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Vice::Detail {

enum class Opcode : std::uint8_t {
    Constant,
    LoadLocal,
    LoadName,
    DeclareLocal,
    ClearLocal,
    DuplicateDeclaration,
    Pop,
    Unary,
    Binary,
    Boolean,
    Jump,
    JumpFalse,
    JumpTrue,
    GetMember,
    Call,
    CallLocal,
    CallMember,
    CallValue,
    MakeClosure,
    LoadUpvalue,
    UpvalueTarget,
    Concatenate,
    IsType,
    IsInstance,
    Yield,
    MakeArray,
    MakeMap,
    GetIndex,
    IndexTarget,
    IteratorBegin,
    IteratorNext,
    NameTarget,
    LocalTarget,
    TemporaryTarget,
    MemberTarget,
    LoadTarget,
    StoreTarget,
    Return
};

// Names, constants and source locations stay outside the dispatch stream.
struct Instruction {
    Opcode opcode;
    std::uint32_t operand = 0;
    std::uint32_t auxiliary = 0;
};

struct Local {
    std::string name;
    bool constant = false;
};

struct CompiledFunction;

struct Bytecode {
    std::vector<Instruction> instructions;
    std::vector<SourceLocation> locations;
    std::vector<Value> constants;
    std::vector<std::string> names;
    std::vector<Local> locals;
    // Anonymous and local functions declared in this code, created by MakeClosure.
    std::vector<std::shared_ptr<const CompiledFunction>> functions;
    std::size_t maxStack = 0;
};

// Where a closure finds a captured variable: a local of the enclosing function or one of its own captures.
struct UpvalueInfo {
    bool local = false;
    std::uint32_t index = 0;
    bool constant = false;
};

struct CompiledFunction {
    Token name;
    bool isOverride = false;
    std::size_t parameterCount = 0;
    Bytecode code;
    std::vector<UpvalueInfo> upvalues;
};

struct CompiledVariable {
    Token token;
    bool constant = false;
    std::shared_ptr<const Bytecode> expression;
    bool exported = false;
    Value exportDefault;
};

struct CompiledClass {
    Token name;
    std::shared_ptr<const Bytecode> base;
    std::vector<CompiledVariable> fields;
    std::vector<CompiledFunction> methods;
};

struct CompiledEnum {
    Token name;
    std::vector<std::string> members;
};

struct CompiledImport {
    Token name;
    std::string request;
};

struct ProgramData {
    std::string sourceName;
    std::vector<CompiledVariable> globals;
    std::vector<CompiledFunction> functions;
    std::vector<CompiledImport> imports;
    std::vector<CompiledClass> classes;
    std::vector<CompiledEnum> enums;
    std::unordered_map<std::string, std::size_t> functionIndices;
};

std::shared_ptr<const ProgramData> ParseProgram(std::string_view source, std::string_view sourceName);

} // namespace Vice::Detail
