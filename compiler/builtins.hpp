#ifndef PUNPUN_BUILTINS_HPP
#define PUNPUN_BUILTINS_HPP

#include <string>
#include <vector>

#include "frontend.hpp"

struct BuiltinSpec {
    std::string name;
    std::vector<Type> parameters;
    Type result;
    std::string runtime_symbol;
    std::string documentation;
};

inline const std::vector<BuiltinSpec> &punpun_builtins() {
    static const std::vector<BuiltinSpec> builtins = {
        {"print", {Type::Infer}, Type::Void, "", "Print a scalar without a newline."},
        {"println", {Type::Infer}, Type::Void, "", "Print a scalar followed by a newline."},
        {"len", {Type::Str}, Type::Int, "pp_len", "Return the byte length of a string."},
        {"abs", {Type::Int}, Type::Int, "pp_abs_i64", "Checked absolute value of an integer."},
        {"clock_ms", {}, Type::Int, "pp_clock_ms", "Return monotonic milliseconds."},
        {"panic", {Type::Str}, Type::Void, "pp_panic", "Abort with a PunPun panic message."},
        {"numbers", {}, Type::Nums, "pp_numbers_new", "Create an empty nums list."},
        {"push", {Type::Nums, Type::Int}, Type::Void, "pp_push", "Append an integer to nums."},
        {"at", {Type::Nums, Type::Int}, Type::Int, "pp_at", "Read a checked nums element."},
        {"put", {Type::Nums, Type::Int, Type::Int}, Type::Void, "pp_put", "Replace a checked nums element."},
        {"size", {Type::Nums}, Type::Int, "pp_size", "Return nums length."},
        {"view", {Type::Nums, Type::Int, Type::Int}, Type{"Slice<int>"}, "pp_numbers_view", "Create a checked borrowed Slice<int> over nums."},
        {"slice_len", {Type{"Slice<int>"}}, Type::Int, "pp_slice_len_i64", "Return the number of elements in a Slice<int>."},
        {"slice_get", {Type{"Slice<int>"}, Type::Int}, Type::Int, "pp_slice_at_i64", "Read a checked element from a Slice<int>."},
        {"pop", {Type::Nums}, Type::Int, "pp_pop", "Remove and return the last nums element."},
        {"sort", {Type::Nums}, Type::Void, "pp_sort", "Sort nums ascending in place."},
        {"concat", {Type::Str, Type::Str}, Type::Str, "pp_concat", "Concatenate strings."},
        {"slice", {Type::Str, Type::Int, Type::Int}, Type::Str, "pp_slice", "Copy string bytes in [start, end)."},
        {"contains", {Type::Str, Type::Str}, Type::Bool, "pp_contains", "Case-sensitive substring test."},
        {"read_text", {Type::Str}, Type::Str, "pp_read_text", "Read an entire text file."},
        {"write_text", {Type::Str, Type::Str}, Type::Void, "pp_write_text", "Write an entire text file."},
        {"text", {Type::Int}, Type::Str, "pp_text_int", "Convert int to str."},
        {"parse_int", {Type::Str}, Type::Int, "pp_parse_int", "Parse a decimal string as int."},
        {"decimal", {Type::Int}, Type::Float, "pp_decimal", "Convert int to float."},
        {"whole", {Type::Float}, Type::Int, "pp_whole", "Truncate float to int."},
        {"assert", {Type::Bool, Type::Str}, Type::Void, "pp_assert", "Panic when a condition is no."},
        {"arg_count", {}, Type::Int, "pp_arg_count", "Return the number of program arguments."},
        {"arg", {Type::Int}, Type::Str, "pp_arg", "Return a program argument by index."},
        {"file_exists", {Type::Str}, Type::Bool, "pp_file_exists", "Test whether a regular file exists."},
        {"current_dir", {}, Type::Str, "pp_current_dir", "Return the current working directory."},
        {"env_has", {Type::Str}, Type::Bool, "pp_env_has", "Test whether an environment variable exists."},
        {"env_or", {Type::Str, Type::Str}, Type::Str, "pp_env_or", "Return an environment value or fallback."},
        {"platform", {}, Type::Str, "pp_platform", "Return the current platform name."},
        {"read_line", {}, Type::Str, "pp_read_line", "Read one line from standard input."},
        {"sleep_ms", {Type::Int}, Type::Void, "pp_sleep_ms", "Sleep for a nonnegative number of milliseconds."},
        {"move", {Type::Infer}, Type::Infer, "", "Transfer an owning value and invalidate the source binding."},
        {"drop", {Type::Infer}, Type::Void, "", "Destroy an object immediately and invalidate the source binding."},
        {"cancel", {Type::Infer}, Type::Void, "pp_task_cancel", "Request cooperative cancellation of a task."},
        {"task_done", {Type::Infer}, Type::Bool, "pp_task_is_done", "Test whether a task has completed."},
        {"cancelled", {}, Type::Bool, "pp_task_cancelled", "Test cancellation from inside the current async task."},
        {"task_group", {}, Type::Int, "pp_task_group_new", "Create a structured task group and return its opaque handle."},
        {"task_group_add", {Type::Int, Type::Infer}, Type::Void, "pp_task_group_add", "Attach a Task to a structured task group."},
        {"task_group_cancel", {Type::Int}, Type::Void, "pp_task_group_cancel", "Request cooperative cancellation of every task in a group."},
        {"task_group_wait", {Type::Int}, Type::Void, "pp_task_group_wait", "Wait for every task in a group."},
        {"task_group_wait_for", {Type::Int, Type::Int}, Type::Bool, "pp_task_group_wait_for", "Wait up to a timeout for a group to finish."},
        {"task_group_done", {Type::Int}, Type::Bool, "pp_task_group_is_done", "Test whether every task in a group has completed."},
        {"task_group_pending", {Type::Int}, Type::Int, "pp_task_group_pending", "Count unfinished tasks in a group."},
        {"task_group_close", {Type::Int}, Type::Void, "pp_task_group_close", "Wait for and close a task group."},
        {"utf8_valid", {Type::Str}, Type::Bool, "pp_utf8_valid", "Validate UTF-8 text."},
        {"utf8_len", {Type::Str}, Type::Int, "pp_utf8_len", "Count Unicode scalar values in validated UTF-8 text."},
        {"make_dir", {Type::Str}, Type::Bool, "pp_make_dir", "Create one directory, returning false when it already exists or fails."},
        {"remove_file", {Type::Str}, Type::Bool, "pp_remove_file", "Remove a regular file."},
        {"rename_file", {Type::Str, Type::Str}, Type::Bool, "pp_rename_file", "Rename or move a file."},
        {"path_join", {Type::Str, Type::Str}, Type::Str, "pp_path_join", "Join two path segments using the platform separator."},
    };
    return builtins;
}

inline std::string builtin_signature(const BuiltinSpec &builtin) {
    std::string value = builtin.name + "(";
    for (std::size_t i = 0; i < builtin.parameters.size(); ++i) {
        if (i) value += ", ";
        value += type_name(builtin.parameters[i]);
    }
    value += ") -> " + type_name(builtin.result);
    return value;
}

#endif
