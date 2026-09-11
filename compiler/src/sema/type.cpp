#include "ppc/sema/type.hpp"

#include <algorithm>

namespace ppc {

namespace {

/// Structural key for interning. It must distinguish every observable
/// difference between two types and nothing else.
std::string key_for(const Type &type, const TypeContext &context) {
    switch (type.kind) {
        case TypeKind::Error: return "!";
        case TypeKind::Void: return "v";
        case TypeKind::Bool: return "b";
        case TypeKind::Int: return "i";
        case TypeKind::Float: return "f";
        case TypeKind::Str: return "s";
        case TypeKind::Nums: return "n";
        case TypeKind::Reference: return "&" + context.mangle(type.element);
        case TypeKind::MutRef: return "&m" + context.mangle(type.element);
        case TypeKind::RawPointer: return "*" + context.mangle(type.element);
        case TypeKind::Slice: return "[" + context.mangle(type.element);
        case TypeKind::List: return "l" + context.mangle(type.element);
        case TypeKind::Map: return "m" + context.mangle(type.element);
        case TypeKind::Bytes: return "y";
        case TypeKind::Task: return "@" + context.mangle(type.element);
        case TypeKind::Function: {
            std::string result = "f(";
            for (const Type *parameter : type.arguments) result += context.mangle(parameter) + ",";
            return result + ")" + context.mangle(type.element);
        }
        case TypeKind::Param: return "P" + context.interner().text(type.name);
        case TypeKind::Struct:
        case TypeKind::Object:
        case TypeKind::Enum: {
            std::string result;
            result += (type.kind == TypeKind::Struct)   ? "S"
                      : (type.kind == TypeKind::Object) ? "O"
                                                        : "E";
            result += context.interner().text(type.name);
            if (!type.arguments.empty()) {
                result += "<";
                for (const Type *argument : type.arguments) {
                    result += context.mangle(argument);
                    result += ",";
                }
                result += ">";
            }
            return result;
        }
    }
    return "?";
}

}  // namespace

TypeContext::TypeContext(Interner &interner) : interner_(interner) {
    auto primitive = [&](TypeKind kind) {
        Type candidate;
        candidate.kind = kind;
        return intern(std::move(candidate));
    };
    error_ = primitive(TypeKind::Error);
    void_ = primitive(TypeKind::Void);
    bool_ = primitive(TypeKind::Bool);
    int_ = primitive(TypeKind::Int);
    float_ = primitive(TypeKind::Float);
    str_ = primitive(TypeKind::Str);
    nums_ = primitive(TypeKind::Nums);
    bytes_ = primitive(TypeKind::Bytes);
}

const Type *TypeContext::intern(Type &&candidate) {
    const std::string key = key_for(candidate, *this);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    storage_.push_back(std::make_unique<Type>(std::move(candidate)));
    const Type *result = storage_.back().get();
    cache_.emplace(key, result);
    return result;
}

const Type *TypeContext::reference(const Type *element, bool mutable_ref) {
    if (!element) return error_;
    Type candidate;
    candidate.kind = mutable_ref ? TypeKind::MutRef : TypeKind::Reference;
    candidate.element = element;
    return intern(std::move(candidate));
}

const Type *TypeContext::raw_pointer(const Type *element) {
    if (!element) return error_;
    Type candidate;
    candidate.kind = TypeKind::RawPointer;
    candidate.element = element;
    return intern(std::move(candidate));
}

const Type *TypeContext::slice(const Type *element) {
    if (!element) return error_;
    Type candidate;
    candidate.kind = TypeKind::Slice;
    candidate.element = element;
    return intern(std::move(candidate));
}

const Type *TypeContext::list(const Type *element) {
    if (!element) return error_;
    Type candidate;
    candidate.kind = TypeKind::List;
    candidate.element = element;
    return intern(std::move(candidate));
}

const Type *TypeContext::map(const Type *value) {
    if (!value) return error_;
    Type candidate;
    candidate.kind = TypeKind::Map;
    candidate.element = value;
    return intern(std::move(candidate));
}

const Type *TypeContext::task(const Type *result) {
    if (!result) return error_;
    Type candidate;
    candidate.kind = TypeKind::Task;
    candidate.element = result;
    return intern(std::move(candidate));
}

const Type *TypeContext::function(std::vector<const Type *> parameters, const Type *result) {
    if (!result) return error_;
    for (const Type *parameter : parameters) {
        if (!parameter) return error_;
    }
    Type candidate;
    candidate.kind = TypeKind::Function;
    candidate.arguments = std::move(parameters);
    candidate.element = result;
    return intern(std::move(candidate));
}

const Type *TypeContext::param(Symbol name) {
    Type candidate;
    candidate.kind = TypeKind::Param;
    candidate.name = name;
    return intern(std::move(candidate));
}

const Type *TypeContext::named(TypeKind kind, Symbol name, std::vector<const Type *> arguments,
                               u32 decl) {
    Type candidate;
    candidate.kind = kind;
    candidate.name = name;
    candidate.arguments = std::move(arguments);
    candidate.decl = decl;
    return intern(std::move(candidate));
}

std::string TypeContext::describe(const Type *type) const {
    if (!type) return "<null>";
    switch (type->kind) {
        case TypeKind::Error: return "<error>";
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Str: return "str";
        case TypeKind::Nums: return "nums";
        case TypeKind::Reference: return "&" + describe(type->element);
        case TypeKind::MutRef: return "&mut " + describe(type->element);
        case TypeKind::RawPointer: return "*" + describe(type->element);
        case TypeKind::Slice: return "Slice<" + describe(type->element) + ">";
        case TypeKind::List: return "List<" + describe(type->element) + ">";
        case TypeKind::Map: return "Map<" + describe(type->element) + ">";
        case TypeKind::Bytes: return "bytes";
        case TypeKind::Task: return "task<" + describe(type->element) + ">";
        case TypeKind::Function: {
            std::string result = "fn(";
            for (std::size_t i = 0; i < type->arguments.size(); ++i) {
                if (i) result += ", ";
                result += describe(type->arguments[i]);
            }
            result += ")";
            if (type->element && type->element->kind != TypeKind::Void)
                result += " -> " + describe(type->element);
            return result;
        }
        case TypeKind::Param: return interner_.text(type->name);
        case TypeKind::Struct:
        case TypeKind::Object:
        case TypeKind::Enum: {
            std::string result = interner_.text(type->name);
            if (!type->arguments.empty()) {
                result += "<";
                for (std::size_t i = 0; i < type->arguments.size(); ++i) {
                    if (i) result += ", ";
                    result += describe(type->arguments[i]);
                }
                result += ">";
            }
            return result;
        }
    }
    return "?";
}

std::string TypeContext::mangle(const Type *type) const {
    if (!type) return "err";
    switch (type->kind) {
        case TypeKind::Error: return "err";
        case TypeKind::Void: return "v";
        case TypeKind::Bool: return "b";
        case TypeKind::Int: return "i";
        case TypeKind::Float: return "f";
        case TypeKind::Str: return "s";
        case TypeKind::Nums: return "n";
        case TypeKind::Reference: return "R" + mangle(type->element);
        case TypeKind::MutRef: return "M" + mangle(type->element);
        case TypeKind::RawPointer: return "P" + mangle(type->element);
        case TypeKind::Slice: return "L" + mangle(type->element);
        case TypeKind::List: return "Q" + mangle(type->element);
        case TypeKind::Map: return "H" + mangle(type->element);
        case TypeKind::Bytes: return "y";
        case TypeKind::Task: return "T" + mangle(type->element);
        case TypeKind::Function: {
            std::string result = "F";
            for (const Type *parameter : type->arguments) result += mangle(parameter) + "_";
            return result + "R" + mangle(type->element);
        }
        case TypeKind::Param: return "G" + interner_.text(type->name);
        case TypeKind::Struct:
        case TypeKind::Object:
        case TypeKind::Enum: {
            std::string result = interner_.text(type->name);
            for (const Type *argument : type->arguments) {
                result += "$";
                result += mangle(argument);
            }
            return result;
        }
    }
    return "err";
}

bool TypeContext::assignable(const Type *from, const Type *to) const {
    if (!from || !to) return true;               // an earlier error already fired
    if (from->is_error() || to->is_error()) return true;
    if (from == to) return true;

    // A mutable borrow may be used where a shared borrow is expected; the
    // reverse is not allowed. Nothing else weakens reference strength.
    if (from->kind == TypeKind::MutRef && to->kind == TypeKind::Reference) {
        return from->element == to->element;
    }
    return false;
}

bool TypeContext::is_copy(const Type *type) const {
    std::vector<const Type *> visiting;
    return is_copy_impl(type, visiting);
}

bool TypeContext::is_copy_impl(const Type *type, std::vector<const Type *> &visiting) const {
    if (!type) return true;
    // A type already on the walk contributes no new members to examine.
    if (std::find(visiting.begin(), visiting.end(), type) != visiting.end()) return true;
    visiting.push_back(type);
    struct Pop {
        std::vector<const Type *> &stack;
        ~Pop() { stack.pop_back(); }
    } pop{visiting};


    switch (type->kind) {
        case TypeKind::Bool:
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Str:
        case TypeKind::Reference:
        case TypeKind::RawPointer:
        case TypeKind::Slice:
        case TypeKind::Error:
        case TypeKind::Void:
            return true;
        // `nums` stays a shared handle for source compatibility: assignment and
        // parameter passing alias the same list rather than copying it.
        case TypeKind::Nums:
        // A List is a shared owning handle, exactly like nums. Assignment
        // aliases the same sequence rather than duplicating it.
        case TypeKind::List:
        // A Map and a bytes buffer are shared owning handles, like nums and
        // List: assignment aliases rather than duplicating.
        case TypeKind::Map:
        case TypeKind::Bytes:
        case TypeKind::Function:
            return true;
        case TypeKind::MutRef:
            // An exclusive borrow cannot be duplicated without breaking the
            // "one mutable borrow at a time" rule.
            return false;
        case TypeKind::Object:
        case TypeKind::Task:
            return false;
        case TypeKind::Param:
            // Conservative: only a `T: Copy` bound makes a parameter copyable,
            // and the checker records that separately.
            return false;
        case TypeKind::Struct: {
            if (type->decl >= structs_.size()) return true;
            for (const FieldInfo &field : structs_[type->decl]->fields) {
                if (!is_copy_impl(field.type, visiting)) return false;
            }
            return true;
        }
        case TypeKind::Enum: {
            if (type->decl >= enums_.size()) return true;
            for (const VariantInfo &variant : enums_[type->decl]->variants) {
                for (const Type *payload : variant.payload) {
                    if (!is_copy_impl(payload, visiting)) return false;
                }
            }
            return true;
        }
    }
    return true;
}

bool TypeContext::needs_drop(const Type *type) const {
    std::vector<const Type *> visiting;
    return needs_drop_impl(type, visiting);
}

bool TypeContext::needs_drop_impl(const Type *type, std::vector<const Type *> &visiting) const {
    if (!type) return false;
    // A type already on the walk contributes no new members to examine.
    if (std::find(visiting.begin(), visiting.end(), type) != visiting.end()) return false;
    visiting.push_back(type);
    struct Pop {
        std::vector<const Type *> &stack;
        ~Pop() { stack.pop_back(); }
    } pop{visiting};


    // Identity objects get deterministic lexical destruction; everything else in
    // 0.6 is either a shared handle owned by the runtime or a plain value.
    if (type->kind == TypeKind::Object) return true;
    if (type->kind == TypeKind::Struct && type->decl < structs_.size()) {
        for (const FieldInfo &field : structs_[type->decl]->fields) {
            if (needs_drop_impl(field.type, visiting)) return true;
        }
    }
    return false;
}

u32 TypeContext::add_struct(StructInfo info) {
    structs_.push_back(std::make_unique<StructInfo>(std::move(info)));
    return static_cast<u32>(structs_.size() - 1);
}

u32 TypeContext::add_enum(EnumInfo info) {
    enums_.push_back(std::make_unique<EnumInfo>(std::move(info)));
    return static_cast<u32>(enums_.size() - 1);
}

u32 TypeContext::add_contract(ContractInfo info) {
    contracts_.push_back(std::make_unique<ContractInfo>(std::move(info)));
    return static_cast<u32>(contracts_.size() - 1);
}

}  // namespace ppc
