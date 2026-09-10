#include "ppc/codegen/c_backend.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "ppc/sema/builtins.hpp"

namespace ppc {

const char *backend_name(BackendKind kind) {
    switch (kind) {
        case BackendKind::C: return "c";
        case BackendKind::Native: return "native";
        case BackendKind::Bytecode: return "bytecode";
    }
    return "c";
}

bool parse_backend(const std::string &name, BackendKind &out) {
    if (name == "c" || name == "cc") { out = BackendKind::C; return true; }
    if (name == "native" || name == "x86-64" || name == "asm") {
        out = BackendKind::Native;
        return true;
    }
    if (name == "bytecode" || name == "vm") { out = BackendKind::Bytecode; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Naming
// ---------------------------------------------------------------------------

std::string CBackend::sanitize(const std::string &name) {
    std::string result;
    result.reserve(name.size() + 4);
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_') {
            result += c;
        } else {
            // `$` separates a specialization's type arguments in the mangled
            // name and is not a C identifier character.
            result += '_';
        }
    }
    if (result.empty()) result = "anon";
    if (result[0] >= '0' && result[0] <= '9') result.insert(result.begin(), '_');
    return result;
}

std::string CBackend::quote(const std::string &text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\t': result += "\\t"; break;
            case '\r': result += "\\r"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\%03o", c);
                    result += buffer;
                } else {
                    result += static_cast<char>(c);
                }
        }
    }
    result += "\"";
    return result;
}

std::string CBackend::function_name(std::size_t index) const {
    // The index guarantees uniqueness even when two specializations sanitize to
    // the same identifier; the readable part is kept for debuggable output.
    const MirFunction *fn = program_->functions[index];
    if (fn->is_extern_native) return fn->native_symbol;
    return "ppf" + std::to_string(index) + "_" + sanitize(fn->name);
}

std::string CBackend::aggregate_name(const Type *type) {
    auto it = aggregate_names_.find(type);
    if (it != aggregate_names_.end()) return it->second;
    const std::string name =
        "ppt" + std::to_string(aggregate_names_.size()) + "_" + sanitize(types_.mangle(type));
    aggregate_names_.emplace(type, name);
    return name;
}

const char *CBackend::access(const Type *type) {
    if (!type) return ".";
    // Objects are handles and references are pointers; both are dereferenced.
    if (type->kind == TypeKind::Object || type->is_pointer_like()) return "->";
    return ".";
}

const Type *CBackend::reg_type(const MirFunction &fn, Reg id) const {
    if (id == kNoReg || id >= fn.reg_types.size()) return nullptr;
    return fn.reg_types[id];
}

std::string CBackend::type_name(const Type *type) {
    if (!type) return "int64_t";
    switch (type->kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int: return "int64_t";
        case TypeKind::Float: return "double";
        case TypeKind::Str: return "const char *";
        case TypeKind::Nums: return "pp_numbers *";
        case TypeKind::Slice: return "pp_i64_slice *";
        // Elements are 8-byte slots; the element type lives in the compiler, not
        // in the runtime, so every List<T> is the same C type.
        case TypeKind::List: return "pp_list *";
        case TypeKind::Map: return "pp_map *";
        case TypeKind::Bytes: return "pp_bytes *";
        case TypeKind::Task: return "pp_task *";
        case TypeKind::Error: return "int64_t";
        case TypeKind::Param:
            // A type parameter should have been substituted before codegen.
            return "int64_t";
        case TypeKind::Reference:
        case TypeKind::MutRef:
        case TypeKind::RawPointer: {
            const std::string inner = type_name(type->element);
            // `const char *` already ends in a pointer, so avoid `* *` spacing
            // oddities by always appending with a space.
            return inner + " *";
        }
        case TypeKind::Struct: return aggregate_name(type);
        // An object has identity, so the value is always the handle.
        case TypeKind::Object: return aggregate_name(type) + " *";
        case TypeKind::Enum: return aggregate_name(type);
    }
    return "int64_t";
}

// ---------------------------------------------------------------------------
// Aggregate discovery and emission
// ---------------------------------------------------------------------------

void CBackend::collect_aggregates(const MirProgram &program) {
    // Any aggregate that appears as a local, a register, a parameter, or a
    // result has to be declared. Fields pull in more, which `emit_aggregate`
    // handles recursively.
    auto note = [&](const Type *type) {
        if (!type) return;
        while (type->is_pointer_like() || type->kind == TypeKind::Slice ||
               type->kind == TypeKind::Task) {
            type = type->element;
            if (!type) return;
        }
        if (!type->is_aggregate()) return;
        if (std::find(aggregates_.begin(), aggregates_.end(), type) != aggregates_.end()) return;
        aggregates_.push_back(type);
    };

    for (const MirFunction *fn : program.functions) {
        note(fn->result);
        for (const MirLocal &local : fn->locals) note(local.type);
        for (const Type *type : fn->reg_types) note(type);
    }
}

void CBackend::emit_aggregate(const Type *type, std::vector<const Type *> &pending) {
    if (!type || !type->is_aggregate()) return;

    const int state = emit_state_[type];
    if (state == 2) return;
    if (state == 1) {
        // A cycle through value-embedded fields would need infinite storage.
        // The checker rejects that, so reaching here means a compiler bug.
        diagnostics_
            .error(Code::BackendInternal,
                   "type " + types_.describe(type) + " contains itself by value")
            .note("this is a compiler bug; please report it");
        return;
    }
    emit_state_[type] = 1;

    // Emit dependencies first. Only value-embedded members create an ordering
    // constraint; a pointer to an incomplete type is fine in C.
    auto require = [&](const Type *member) {
        if (!member) return;
        if (member->kind == TypeKind::Struct || member->kind == TypeKind::Enum) {
            emit_aggregate(member, pending);
        }
    };

    if (type->kind == TypeKind::Enum) {
        const EnumInfo &info = types_.enum_at(type->decl);
        for (const VariantInfo &variant : info.variants) {
            for (const Type *payload : variant.payload) require(payload);
        }
    } else {
        const StructInfo &info = types_.struct_at(type->decl);
        for (const FieldInfo &field : info.fields) require(field.type);
    }

    const std::string name = aggregate_name(type);

    if (type->kind == TypeKind::Enum) {
        const EnumInfo &info = types_.enum_at(type->decl);
        out_ << "/* " << types_.describe(type) << " */\n";
        out_ << "struct " << name << " {\n";
        out_ << "    int64_t tag;\n";

        // Variants with a payload share one union; the tag selects the member.
        bool any_payload = false;
        for (const VariantInfo &variant : info.variants) {
            if (!variant.payload.empty()) { any_payload = true; break; }
        }
        if (any_payload) {
            out_ << "    union {\n";
            for (std::size_t i = 0; i < info.variants.size(); ++i) {
                const VariantInfo &variant = info.variants[i];
                if (variant.payload.empty()) continue;
                out_ << "        struct {";
                for (std::size_t f = 0; f < variant.payload.size(); ++f) {
                    out_ << " " << type_name(variant.payload[f]) << " f" << f << ";";
                }
                out_ << " } v" << i << ";  /* " << types_.interner().text(variant.name)
                     << " */\n";
            }
            out_ << "    } as;\n";
        }
        out_ << "};\n\n";
    } else {
        const StructInfo &info = types_.struct_at(type->decl);
        out_ << "/* " << types_.describe(type)
             << (type->kind == TypeKind::Object ? "  (identity object)" : "  (value struct)")
             << " */\n";
        out_ << "struct " << name << " {\n";
        if (info.fields.empty()) {
            // C forbids an empty struct; a placeholder keeps sizeof sane.
            out_ << "    char pp_empty;\n";
        }
        for (std::size_t i = 0; i < info.fields.size(); ++i) {
            out_ << "    " << type_name(info.fields[i].type) << " f" << i << ";  /* "
                 << types_.interner().text(info.fields[i].name) << " */\n";
        }
        out_ << "};\n\n";
    }

    emit_state_[type] = 2;
}

void CBackend::emit_aggregates() {
    if (aggregates_.empty()) return;

    out_ << "/* ---- type declarations ---- */\n\n";
    // Forward-declare every tag first so pointers between aggregates resolve
    // regardless of the order the bodies end up in.
    for (const Type *type : aggregates_) {
        const std::string name = aggregate_name(type);
        out_ << "typedef struct " << name << " " << name << ";\n";
    }
    out_ << "\n";

    std::vector<const Type *> pending;
    for (const Type *type : aggregates_) emit_aggregate(type, pending);
}

}  // namespace ppc
