#include "ppc/codegen/vm.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ppc/sema/builtins.hpp"

extern "C" {
#include "ppcrt.h"
}

namespace ppc {

namespace {

/// A boxed aggregate: a heap block of slots. Structs, objects, and enums all
/// use this shape, with an enum's tag living in slot 0.
Slot *allocate_block(u32 count) {
    // Routed through the runtime allocator so blocks are freed by the same
    // cleanup sweep as everything else, and a `drop` behaves identically to the
    // native backends.
    void *memory = pp_object_alloc(static_cast<int64_t>(sizeof(Slot) * (count ? count : 1)));
    return static_cast<Slot *>(memory);
}

}  // namespace

namespace {

/// What a worker thread needs to run one task: the shared program, which
/// function to run, and the already-evaluated arguments.
struct VmTaskContext {
    static constexpr u32 kMaxArguments = 16;
    const BytecodeProgram *program = nullptr;
    u32 function = 0;
    u32 argument_count = 0;
    Slot arguments[kMaxArguments];
};

uintptr_t vm_task_entry(void *raw) {
    const VmTaskContext *context = static_cast<const VmTaskContext *>(raw);
    // A fresh interpreter per task: the stack and depth counter are per-thread
    // state, while the program itself is immutable and shared.
    Vm vm;
    const Slot result = vm.run_task(*context->program, context->function, context->arguments,
                                    context->argument_count);
    // Every Slot member is 8 bytes and overlays the same storage, so reading
    // the integer member carries a double's or a pointer's bits unchanged.
    return static_cast<uintptr_t>(result.integer);
}

}  // namespace

Slot Vm::run_task(const BytecodeProgram &program, u32 function, const Slot *arguments,
                  u32 argument_count) {
    program_ = &program;
    stack_.assign(1024, Slot{});
    stack_top_ = 0;
    depth_ = 0;
    return invoke(function, arguments, argument_count);
}

void *Vm::copy_struct(void *source, u32 decl) {
    if (decl >= program_->layouts.size()) return source;
    const StructLayout &layout = program_->layouts[decl];

    Slot *block = allocate_block(layout.slot_count);
    if (source) {
        std::memcpy(block, source, sizeof(Slot) * layout.slot_count);
    }
    // Nested value fields are boxed too, so a shallow copy would leave the two
    // structs sharing them. Recursion terminates because the checker rejects a
    // type that contains itself by value.
    for (const auto &nested : layout.nested) {
        Slot &field = block[nested.first];
        if (field.pointer) field.pointer = copy_struct(field.pointer, nested.second);
    }
    return block;
}

void Vm::fail(const std::string &message) {
    // Routed through the runtime so a VM failure reports exactly like a native
    // panic, including the exit status.
    pp_panic(message.c_str());
    std::abort();  // unreachable; pp_panic does not return
}

Slot Vm::call_builtin(u32 builtin, const Slot *arguments, u32 argument_count,
                      u32 formatter) {
    const std::vector<BuiltinSpec> &table = builtin_table();
    Slot result;
    if (builtin >= table.size()) fail("bytecode names an unknown builtin");

    const BuiltinSpec &spec = table[builtin];
    const std::string name = spec.name;

    auto integer = [&](u32 i) { return i < argument_count ? arguments[i].integer : 0; };
    auto text = [&](u32 i) { return i < argument_count ? arguments[i].text : ""; };
    auto pointer = [&](u32 i) { return i < argument_count ? arguments[i].pointer : nullptr; };
    auto real = [&](u32 i) { return i < argument_count ? arguments[i].real : 0.0; };

    // Console builtins are polymorphic in the source language; the compiler
    // recorded which formatter to use by way of the argument's static type,
    // which is encoded here by dispatching on the spec's parameter kind.
    if (name == "print" || name == "println" || name == "say") {
        const bool newline = (name != "print");
        const Slot value = argument_count ? arguments[0] : Slot{};
        switch (formatter) {
            case kFormatInt:
                newline ? pp_println_int(value.integer) : pp_print_int(value.integer);
                break;
            case kFormatFloat:
                newline ? pp_println_float(value.real) : pp_print_float(value.real);
                break;
            case kFormatBool:
                newline ? pp_println_bool(value.integer != 0) : pp_print_bool(value.integer != 0);
                break;
            default:
                newline ? pp_println_str(value.text) : pp_print_str(value.text);
                break;
        }
        return result;
    }

    if (name == "move") {
        return argument_count ? arguments[0] : result;
    }
    if (name == "drop") {
        return result;
    }

    // The remaining builtins map one-to-one onto runtime entry points. The
    // switch is on the symbol name so this table cannot drift from the one the
    // C backend uses.
    const std::string symbol = spec.symbol ? spec.symbol : "";

    if (symbol == "pp_numbers_new") { result.pointer = pp_numbers_new(); return result; }
    if (symbol == "pp_push") { pp_push((pp_numbers *)pointer(0), integer(1)); return result; }
    if (symbol == "pp_at") { result.integer = pp_at((pp_numbers *)pointer(0), integer(1)); return result; }
    if (symbol == "pp_put") { pp_put((pp_numbers *)pointer(0), integer(1), integer(2)); return result; }
    if (symbol == "pp_size") { result.integer = pp_size((pp_numbers *)pointer(0)); return result; }
    if (symbol == "pp_pop") { result.integer = pp_pop((pp_numbers *)pointer(0)); return result; }
    if (symbol == "pp_sort") { pp_sort((pp_numbers *)pointer(0)); return result; }

    if (symbol == "pp_list_new") { result.pointer = pp_list_new(); return result; }
    if (symbol == "pp_list_push") {
        // Arguments arrive as raw slots already; a Slot and a list element are
        // both 8 bytes overlaying the same storage.
        pp_list_push((pp_list *)pointer(0), argument_count > 1 ? arguments[1].integer : 0);
        return result;
    }
    if (symbol == "pp_list_at") {
        result.integer = pp_list_at((pp_list *)pointer(0), integer(1));
        return result;
    }
    if (symbol == "pp_list_put") {
        pp_list_put((pp_list *)pointer(0), integer(1),
                    argument_count > 2 ? arguments[2].integer : 0);
        return result;
    }
    if (symbol == "pp_list_size") { result.integer = pp_list_size((pp_list *)pointer(0)); return result; }
    if (symbol == "pp_list_pop") { result.integer = pp_list_pop((pp_list *)pointer(0)); return result; }
    if (symbol == "pp_list_clear") { pp_list_clear((pp_list *)pointer(0)); return result; }


    // --- Map<V> -------------------------------------------------------
    if (symbol == "pp_map_new") { result.pointer = pp_map_new(); return result; }
    if (symbol == "pp_map_put") {
        pp_map_put((pp_map *)pointer(0), text(1),
                   argument_count > 2 ? arguments[2].integer : 0);
        return result;
    }
    if (symbol == "pp_map_get") { result.integer = pp_map_get((pp_map *)pointer(0), text(1)); return result; }
    if (symbol == "pp_map_get_or") {
        result.integer = pp_map_get_or((pp_map *)pointer(0), text(1),
                                       argument_count > 2 ? arguments[2].integer : 0);
        return result;
    }
    if (symbol == "pp_map_has") { result.integer = pp_map_has((pp_map *)pointer(0), text(1)); return result; }
    if (symbol == "pp_map_remove") { result.integer = pp_map_remove((pp_map *)pointer(0), text(1)); return result; }
    if (symbol == "pp_map_size") { result.integer = pp_map_size((pp_map *)pointer(0)); return result; }
    if (symbol == "pp_map_keys") { result.pointer = pp_map_keys((pp_map *)pointer(0)); return result; }
    if (symbol == "pp_map_clear") { pp_map_clear((pp_map *)pointer(0)); return result; }

    // --- bytes --------------------------------------------------------
    if (symbol == "pp_bytes_new") { result.pointer = pp_bytes_new(); return result; }
    if (symbol == "pp_bytes_from_text") { result.pointer = pp_bytes_from_text(text(0)); return result; }
    if (symbol == "pp_bytes_to_text") { result.text = pp_bytes_to_text((pp_bytes *)pointer(0)); return result; }
    if (symbol == "pp_bytes_push") { pp_bytes_push((pp_bytes *)pointer(0), integer(1)); return result; }
    if (symbol == "pp_bytes_at") { result.integer = pp_bytes_at((pp_bytes *)pointer(0), integer(1)); return result; }
    if (symbol == "pp_bytes_put") { pp_bytes_put((pp_bytes *)pointer(0), integer(1), integer(2)); return result; }
    if (symbol == "pp_bytes_len") { result.integer = pp_bytes_len((pp_bytes *)pointer(0)); return result; }
    if (symbol == "pp_bytes_slice") { result.pointer = pp_bytes_slice((pp_bytes *)pointer(0), integer(1), integer(2)); return result; }
    if (symbol == "pp_bytes_concat") { result.pointer = pp_bytes_concat((pp_bytes *)pointer(0), (pp_bytes *)pointer(1)); return result; }

    // --- text ---------------------------------------------------------
    if (symbol == "pp_char_at") { result.integer = pp_char_at(text(0), integer(1)); return result; }
    if (symbol == "pp_char_str") { result.text = pp_char_str(integer(0)); return result; }
    if (symbol == "pp_index_of") { result.integer = pp_index_of(text(0), text(1), integer(2)); return result; }
    if (symbol == "pp_last_index_of") { result.integer = pp_last_index_of(text(0), text(1)); return result; }
    if (symbol == "pp_starts_with") { result.integer = pp_starts_with(text(0), text(1)); return result; }
    if (symbol == "pp_ends_with") { result.integer = pp_ends_with(text(0), text(1)); return result; }
    if (symbol == "pp_to_upper") { result.text = pp_to_upper(text(0)); return result; }
    if (symbol == "pp_to_lower") { result.text = pp_to_lower(text(0)); return result; }
    if (symbol == "pp_trim") { result.text = pp_trim(text(0)); return result; }
    if (symbol == "pp_replace") { result.text = pp_replace(text(0), text(1), text(2)); return result; }
    if (symbol == "pp_repeat") { result.text = pp_repeat(text(0), integer(1)); return result; }
    if (symbol == "pp_split") { result.pointer = pp_split(text(0), text(1)); return result; }
    if (symbol == "pp_join") { result.text = pp_join((pp_list *)pointer(0), text(1)); return result; }
    if (symbol == "pp_text_float") { result.text = pp_text_float(real(0)); return result; }
    if (symbol == "pp_parse_float") { result.real = pp_parse_float(text(0)); return result; }
    if (symbol == "pp_pad_left") { result.text = pp_pad_left(text(0), integer(1), text(2)); return result; }
    if (symbol == "pp_pad_right") { result.text = pp_pad_right(text(0), integer(1), text(2)); return result; }

    // --- math ---------------------------------------------------------
    if (symbol == "pp_sqrt") { result.real = pp_sqrt(real(0)); return result; }
    if (symbol == "pp_pow") { result.real = pp_pow(real(0), real(1)); return result; }
    if (symbol == "pp_exp") { result.real = pp_exp(real(0)); return result; }
    if (symbol == "pp_log") { result.real = pp_log(real(0)); return result; }
    if (symbol == "pp_log2") { result.real = pp_log2(real(0)); return result; }
    if (symbol == "pp_log10") { result.real = pp_log10(real(0)); return result; }
    if (symbol == "pp_sin") { result.real = pp_sin(real(0)); return result; }
    if (symbol == "pp_cos") { result.real = pp_cos(real(0)); return result; }
    if (symbol == "pp_tan") { result.real = pp_tan(real(0)); return result; }
    if (symbol == "pp_asin") { result.real = pp_asin(real(0)); return result; }
    if (symbol == "pp_acos") { result.real = pp_acos(real(0)); return result; }
    if (symbol == "pp_atan") { result.real = pp_atan(real(0)); return result; }
    if (symbol == "pp_atan2") { result.real = pp_atan2(real(0), real(1)); return result; }
    if (symbol == "pp_floor") { result.real = pp_floor(real(0)); return result; }
    if (symbol == "pp_ceil") { result.real = pp_ceil(real(0)); return result; }
    if (symbol == "pp_round") { result.real = pp_round(real(0)); return result; }
    if (symbol == "pp_fabs") { result.real = pp_fabs(real(0)); return result; }
    if (symbol == "pp_fmod") { result.real = pp_fmod(real(0), real(1)); return result; }
    if (symbol == "pp_hypot") { result.real = pp_hypot(real(0), real(1)); return result; }
    if (symbol == "pp_is_nan") { result.integer = pp_is_nan(real(0)); return result; }
    if (symbol == "pp_is_infinite") { result.integer = pp_is_infinite(real(0)); return result; }

    // --- random -------------------------------------------------------
    if (symbol == "pp_random_seed") { pp_random_seed(integer(0)); return result; }
    if (symbol == "pp_random_int") { result.integer = pp_random_int(integer(0), integer(1)); return result; }
    if (symbol == "pp_random_float") { result.real = pp_random_float(); return result; }
    if (symbol == "pp_random_bytes") { result.pointer = pp_random_bytes(integer(0)); return result; }

    // --- filesystem ---------------------------------------------------
    if (symbol == "pp_list_dir") { result.pointer = pp_list_dir(text(0)); return result; }
    if (symbol == "pp_is_dir") { result.integer = pp_is_dir(text(0)); return result; }
    if (symbol == "pp_file_size_of") { result.integer = pp_file_size_of(text(0)); return result; }
    if (symbol == "pp_read_bytes") { result.pointer = pp_read_bytes(text(0)); return result; }
    if (symbol == "pp_write_bytes") { pp_write_bytes(text(0), (pp_bytes *)pointer(1)); return result; }
    if (symbol == "pp_append_text") { pp_append_text(text(0), text(1)); return result; }
    if (symbol == "pp_remove_dir") { result.integer = pp_remove_dir(text(0)); return result; }
    if (symbol == "pp_make_dirs") { result.integer = pp_make_dirs(text(0)); return result; }
    if (symbol == "pp_path_parent") { result.text = pp_path_parent(text(0)); return result; }
    if (symbol == "pp_path_name") { result.text = pp_path_name(text(0)); return result; }
    if (symbol == "pp_path_extension") { result.text = pp_path_extension(text(0)); return result; }

    // --- time ---------------------------------------------------------
    if (symbol == "pp_now_ms") { result.integer = pp_now_ms(); return result; }
    if (symbol == "pp_year_of") { result.integer = pp_year_of(integer(0)); return result; }
    if (symbol == "pp_month_of") { result.integer = pp_month_of(integer(0)); return result; }
    if (symbol == "pp_day_of") { result.integer = pp_day_of(integer(0)); return result; }
    if (symbol == "pp_hour_of") { result.integer = pp_hour_of(integer(0)); return result; }
    if (symbol == "pp_minute_of") { result.integer = pp_minute_of(integer(0)); return result; }
    if (symbol == "pp_second_of") { result.integer = pp_second_of(integer(0)); return result; }
    if (symbol == "pp_weekday_of") { result.integer = pp_weekday_of(integer(0)); return result; }

    // --- process ------------------------------------------------------
    if (symbol == "pp_exit") { pp_exit(integer(0)); return result; }
    if (symbol == "pp_run_command") { result.integer = pp_run_command(text(0)); return result; }
    if (symbol == "pp_process_capture") { result.text = pp_process_capture(text(0)); return result; }
    if (symbol == "pp_process_status") { result.integer = pp_process_status(); return result; }

    if (symbol == "pp_numbers_view") {
        result.pointer = pp_numbers_view((pp_numbers *)pointer(0), integer(1), integer(2));
        return result;
    }
    if (symbol == "pp_slice_len_i64") { result.integer = pp_slice_len_i64((pp_i64_slice *)pointer(0)); return result; }
    if (symbol == "pp_slice_at_i64") { result.integer = pp_slice_at_i64((pp_i64_slice *)pointer(0), integer(1)); return result; }

    if (symbol == "pp_len") { result.integer = pp_len(text(0)); return result; }
    if (symbol == "pp_concat") { result.text = pp_concat(text(0), text(1)); return result; }
    if (symbol == "pp_slice") { result.text = pp_slice(text(0), integer(1), integer(2)); return result; }
    if (symbol == "pp_contains") { result.integer = pp_contains(text(0), text(1)); return result; }
    if (symbol == "pp_text_int") { result.text = pp_text_int(integer(0)); return result; }
    if (symbol == "pp_parse_int") { result.integer = pp_parse_int(text(0)); return result; }
    if (symbol == "pp_utf8_valid") { result.integer = pp_utf8_valid(text(0)); return result; }
    if (symbol == "pp_utf8_len") { result.integer = pp_utf8_len(text(0)); return result; }

    if (symbol == "pp_abs_i64") { result.integer = pp_abs_i64(integer(0)); return result; }
    if (symbol == "pp_decimal") { result.real = pp_decimal(integer(0)); return result; }
    if (symbol == "pp_whole") {
        double value = argument_count ? arguments[0].real : 0.0;
        result.integer = pp_whole(value);
        return result;
    }

    if (symbol == "pp_read_line") { result.text = pp_read_line(); return result; }
    if (symbol == "pp_arg_count") { result.integer = pp_arg_count(); return result; }
    if (symbol == "pp_arg") { result.text = pp_arg(integer(0)); return result; }
    if (symbol == "pp_env_has") { result.integer = pp_env_has(text(0)); return result; }
    if (symbol == "pp_env_or") { result.text = pp_env_or(text(0), text(1)); return result; }
    if (symbol == "pp_env_set") { result.integer = pp_env_set(text(0), text(1)); return result; }
    if (symbol == "pp_hostname") { result.text = pp_hostname(); return result; }
    if (symbol == "pp_cpu_count") { result.integer = pp_cpu_count(); return result; }
    if (symbol == "pp_platform") { result.text = pp_platform(); return result; }
    if (symbol == "pp_current_dir") { result.text = pp_current_dir(); return result; }

    if (symbol == "pp_read_text") { result.text = pp_read_text(text(0)); return result; }
    if (symbol == "pp_write_text") { pp_write_text(text(0), text(1)); return result; }
    if (symbol == "pp_file_exists") { result.integer = pp_file_exists(text(0)); return result; }
    if (symbol == "pp_make_dir") { result.integer = pp_make_dir(text(0)); return result; }
    if (symbol == "pp_remove_file") { result.integer = pp_remove_file(text(0)); return result; }
    if (symbol == "pp_rename_file") { result.integer = pp_rename_file(text(0), text(1)); return result; }
    if (symbol == "pp_path_join") { result.text = pp_path_join(text(0), text(1)); return result; }

    if (symbol == "pp_clock_ms") { result.integer = pp_clock_ms(); return result; }
    if (symbol == "pp_sleep_ms") { pp_sleep_ms(integer(0)); return result; }
    if (symbol == "pp_panic") { pp_panic(text(0)); return result; }
    if (symbol == "pp_assert") { pp_assert(integer(0) != 0, text(1)); return result; }

    // Tasks run inline in this backend, so a task handle already holds its
    // result and cancellation has nothing left to interrupt. Group operations
    // still go to the real runtime, so handles, validity checks, and panic
    // messages behave exactly as they do natively; the waits simply find
    // everything already finished.
    if (symbol == "pp_task_cancel") { pp_task_cancel((pp_task *)pointer(0)); return result; }
    if (symbol == "pp_task_is_done") {
        result.integer = pp_task_is_done((pp_task *)pointer(0));
        return result;
    }
    if (symbol == "pp_task_cancelled") { result.integer = pp_task_cancelled(); return result; }

    if (symbol == "pp_task_group_new") { result.integer = pp_task_group_new(); return result; }
    if (symbol == "pp_task_group_add") {
        pp_task_group_add(integer(0), (pp_task *)pointer(1));
        return result;
    }
    if (symbol == "pp_task_group_cancel") { pp_task_group_cancel(integer(0)); return result; }
    if (symbol == "pp_task_group_wait") { pp_task_group_wait(integer(0)); return result; }
    if (symbol == "pp_task_group_wait_for") {
        result.integer = pp_task_group_wait_for(integer(0), integer(1));
        return result;
    }
    if (symbol == "pp_task_group_is_done") {
        result.integer = pp_task_group_is_done(integer(0));
        return result;
    }
    if (symbol == "pp_task_group_pending") {
        result.integer = pp_task_group_pending(integer(0));
        return result;
    }
    if (symbol == "pp_task_group_close") { pp_task_group_close(integer(0)); return result; }

    // HTTPS and GUI use the same runtime entry points as native code. Keeping
    // them here makes backend equivalence testable instead of treating the VM
    // as a second implementation.
    if (symbol == "pp_https_available") { result.integer = pp_https_available(); return result; }
    if (symbol == "pp_https_request") {
        result.text = pp_https_request(text(0), text(1), text(2), text(3), integer(4),
                                       integer(5) != 0);
        return result;
    }
    if (symbol == "pp_https_status") { result.integer = pp_https_status(); return result; }
    if (symbol == "pp_https_error") { result.text = pp_https_error(); return result; }

    if (symbol == "pp_net_available") { result.integer = pp_net_available(); return result; }
    if (symbol == "pp_net_resolve") {
        result.pointer = pp_net_resolve(text(0), integer(1));
        return result;
    }
    if (symbol == "pp_net_tcp_connect") {
        result.integer = pp_net_tcp_connect(text(0), integer(1), integer(2)); return result;
    }
    if (symbol == "pp_net_tcp_listen") {
        result.integer = pp_net_tcp_listen(text(0), integer(1), integer(2)); return result;
    }
    if (symbol == "pp_net_tcp_accept") {
        result.integer = pp_net_tcp_accept(integer(0), integer(1)); return result;
    }
    if (symbol == "pp_net_udp_bind") {
        result.integer = pp_net_udp_bind(text(0), integer(1)); return result;
    }
    if (symbol == "pp_net_socket_close") {
        result.integer = pp_net_socket_close(integer(0)); return result;
    }
    if (symbol == "pp_net_socket_shutdown") {
        result.integer = pp_net_socket_shutdown(integer(0), integer(1)); return result;
    }
    if (symbol == "pp_net_socket_wait_readable") {
        result.integer = pp_net_socket_wait_readable(integer(0), integer(1)); return result;
    }
    if (symbol == "pp_net_socket_wait_writable") {
        result.integer = pp_net_socket_wait_writable(integer(0), integer(1)); return result;
    }
    if (symbol == "pp_net_socket_set_nodelay") {
        result.integer = pp_net_socket_set_nodelay(integer(0), integer(1) != 0); return result;
    }
    if (symbol == "pp_net_socket_send") {
        result.integer = pp_net_socket_send(integer(0), (pp_bytes *)pointer(1), integer(2));
        return result;
    }
    if (symbol == "pp_net_socket_recv") {
        result.pointer = pp_net_socket_recv(integer(0), integer(1), integer(2)); return result;
    }
    if (symbol == "pp_net_udp_send_to") {
        result.integer = pp_net_udp_send_to(integer(0), text(1), integer(2),
                                            (pp_bytes *)pointer(3), integer(4));
        return result;
    }
    if (symbol == "pp_net_udp_recv_from") {
        result.pointer = pp_net_udp_recv_from(integer(0), integer(1), integer(2)); return result;
    }
    if (symbol == "pp_net_socket_local_port") {
        result.integer = pp_net_socket_local_port(integer(0)); return result;
    }
    if (symbol == "pp_net_socket_peer_host") {
        result.text = pp_net_socket_peer_host(integer(0)); return result;
    }
    if (symbol == "pp_net_socket_peer_port") {
        result.integer = pp_net_socket_peer_port(integer(0)); return result;
    }
    if (symbol == "pp_net_timed_out") { result.integer = pp_net_timed_out(); return result; }
    if (symbol == "pp_net_eof") { result.integer = pp_net_eof(); return result; }
    if (symbol == "pp_net_error") { result.text = pp_net_error(); return result; }
    if (symbol == "pp_net_last_host") { result.text = pp_net_last_host(); return result; }
    if (symbol == "pp_net_last_port") { result.integer = pp_net_last_port(); return result; }
    if (symbol == "pp_https_headers_raw") { result.text = pp_https_headers_raw(); return result; }
    if (symbol == "pp_https_body_bytes") { result.pointer = pp_https_body_bytes(); return result; }
    if (symbol == "pp_https_request_bytes") {
        result.pointer = pp_https_request_bytes(text(0), text(1), (pp_bytes *)pointer(2), text(3),
                                                integer(4), integer(5) != 0);
        return result;
    }

    if (symbol == "pp_gui_available") { result.integer = pp_gui_available(); return result; }
    if (symbol == "pp_gui_headless") { result.integer = pp_gui_headless(); return result; }
    if (symbol == "pp_gui_message") { result.integer = pp_gui_message(text(0), text(1)); return result; }
    if (symbol == "pp_gui_window_create") { result.integer = pp_gui_window_create(text(0), integer(1), integer(2)); return result; }
    if (symbol == "pp_gui_window_show") { result.integer = pp_gui_window_show(integer(0), integer(1) != 0); return result; }
    if (symbol == "pp_gui_window_close") { result.integer = pp_gui_window_close(integer(0)); return result; }
    if (symbol == "pp_gui_window_open") { result.integer = pp_gui_window_open(integer(0)); return result; }
    if (symbol == "pp_gui_window_set_title") { result.integer = pp_gui_window_set_title(integer(0), text(1)); return result; }
    if (symbol == "pp_gui_window_width") { result.integer = pp_gui_window_width(integer(0)); return result; }
    if (symbol == "pp_gui_window_height") { result.integer = pp_gui_window_height(integer(0)); return result; }
    if (symbol == "pp_gui_widget_create") { result.integer = pp_gui_widget_create(integer(0), integer(1), text(2)); return result; }
    if (symbol == "pp_gui_widget_destroy") { result.integer = pp_gui_widget_destroy(integer(0)); return result; }
    if (symbol == "pp_gui_widget_set_bounds") { result.integer = pp_gui_widget_set_bounds(integer(0), integer(1), integer(2), integer(3), integer(4)); return result; }
    if (symbol == "pp_gui_widget_x") { result.integer = pp_gui_widget_x(integer(0)); return result; }
    if (symbol == "pp_gui_widget_y") { result.integer = pp_gui_widget_y(integer(0)); return result; }
    if (symbol == "pp_gui_widget_width") { result.integer = pp_gui_widget_width(integer(0)); return result; }
    if (symbol == "pp_gui_widget_height") { result.integer = pp_gui_widget_height(integer(0)); return result; }
    if (symbol == "pp_gui_widget_set_text") { result.integer = pp_gui_widget_set_text(integer(0), text(1)); return result; }
    if (symbol == "pp_gui_widget_text") { result.text = pp_gui_widget_text(integer(0)); return result; }
    if (symbol == "pp_gui_widget_set_value") { result.integer = pp_gui_widget_set_value(integer(0), integer(1)); return result; }
    if (symbol == "pp_gui_widget_value") { result.integer = pp_gui_widget_value(integer(0)); return result; }
    if (symbol == "pp_gui_widget_set_range") { result.integer = pp_gui_widget_set_range(integer(0), integer(1), integer(2)); return result; }
    if (symbol == "pp_gui_widget_set_visible") { result.integer = pp_gui_widget_set_visible(integer(0), integer(1) != 0); return result; }
    if (symbol == "pp_gui_widget_set_enabled") { result.integer = pp_gui_widget_set_enabled(integer(0), integer(1) != 0); return result; }
    if (symbol == "pp_gui_redraw") { result.integer = pp_gui_redraw(integer(0)); return result; }
    if (symbol == "pp_gui_poll") { result.integer = pp_gui_poll(integer(0), integer(1)); return result; }
    if (symbol == "pp_gui_post_event") { result.integer = pp_gui_post_event(integer(0), integer(1), integer(2), integer(3), text(4), integer(5), integer(6)); return result; }
    if (symbol == "pp_gui_event_window") { result.integer = pp_gui_event_window(); return result; }
    if (symbol == "pp_gui_event_widget") { result.integer = pp_gui_event_widget(); return result; }
    if (symbol == "pp_gui_event_key") { result.integer = pp_gui_event_key(); return result; }
    if (symbol == "pp_gui_event_x") { result.integer = pp_gui_event_x(); return result; }
    if (symbol == "pp_gui_event_y") { result.integer = pp_gui_event_y(); return result; }
    if (symbol == "pp_gui_event_text") { result.text = pp_gui_event_text(); return result; }
    if (symbol == "pp_gui_canvas_clear") { result.integer = pp_gui_canvas_clear(integer(0), integer(1)); return result; }
    if (symbol == "pp_gui_canvas_rect") { result.integer = pp_gui_canvas_rect(integer(0), integer(1), integer(2), integer(3), integer(4), integer(5), integer(6) != 0); return result; }
    if (symbol == "pp_gui_canvas_line") { result.integer = pp_gui_canvas_line(integer(0), integer(1), integer(2), integer(3), integer(4), integer(5)); return result; }
    if (symbol == "pp_gui_canvas_text") { result.integer = pp_gui_canvas_text(integer(0), integer(1), integer(2), text(3), integer(4)); return result; }

    fail("the bytecode VM does not implement builtin '" + name + "'");
}

Slot Vm::invoke(u32 function_index, const Slot *arguments, u32 argument_count, i64 closure_raw) {
    if (function_index >= program_->functions.size()) fail("call to an unknown function");
    const BytecodeFunction &fn = program_->functions[function_index];

    if (++depth_ > kMaxDepth) {
        fail("call stack depth exceeded " + std::to_string(kMaxDepth) +
             " frames (infinite recursion?)");
    }

    // Carve a frame out of the shared stack.
    const std::size_t base = stack_top_;
    if (base + fn.frame_size > stack_.size()) {
        stack_.resize((base + fn.frame_size) * 2 + 64);
    }
    stack_top_ = base + fn.frame_size;
    Slot *frame = stack_.data() + base;
    for (u32 i = 0; i < fn.frame_size; ++i) frame[i] = Slot{};

    // A value-struct local starts as a zeroed block rather than a null handle,
    // so reading a field of a declared-but-unassigned struct behaves like C's
    // `= {0}` instead of dereferencing null.
    for (const auto &entry : fn.struct_locals) {
        if (entry.first >= fn.frame_size) continue;
        frame[entry.first].pointer =
            allocate_block(entry.second < program_->layouts.size()
                               ? program_->layouts[entry.second].slot_count
                               : 1);
    }

    // Parameters occupy the first local slots, which is why locals come first
    // in the frame layout. They are assigned after the struct-local setup so an
    // incoming argument replaces the placeholder block rather than the reverse.
    for (u32 i = 0; i < argument_count && i < fn.local_count; ++i) frame[i] = arguments[i];

    Slot result;
    const Instr *code = program_->code.data();
    const u32 limit = fn.entry + fn.instruction_count;
    u32 pc = fn.entry;

    // Scratch buffer for call arguments, reused across calls in this frame.
    std::vector<Slot> call_arguments;

    while (pc < limit) {
        const Instr &in = code[pc];

        switch (in.op) {
            case Op::Halt:
                fail("control ran off the end of function '" + fn.name + "'");
                break;

            case Op::ConstInt: frame[in.dest].integer = in.imm; ++pc; break;
            case Op::ConstBool: frame[in.dest].integer = in.imm; ++pc; break;
            case Op::ConstFloat: frame[in.dest].real = in.fimm; ++pc; break;
            case Op::ConstStr:
                frame[in.dest].text =
                    program_->strings[static_cast<std::size_t>(in.imm)].c_str();
                ++pc;
                break;
            case Op::Move: frame[in.dest] = frame[in.a]; ++pc; break;
            case Op::CopyStruct:
                frame[in.dest].pointer =
                    copy_struct(frame[in.a].pointer, static_cast<u32>(in.imm));
                ++pc;
                break;
            case Op::LoadLocal: frame[in.dest] = frame[in.a]; ++pc; break;
            case Op::StoreLocal: frame[in.dest] = frame[in.a]; ++pc; break;
            case Op::LocalAddr: frame[in.dest].pointer = &frame[in.a]; ++pc; break;
            case Op::MakeClosure: {
                pp_closure closure = in.arg_count == 0
                                         ? pp_closure_named(in.imm)
                                         : pp_closure_new(in.imm, in.arg_count);
                for (u32 i = 0; i < in.arg_count; ++i) {
                    const u32 slot = program_->arguments[in.arg_offset + i];
                    pp_closure_set(closure, i, frame[slot].integer);
                }
                frame[in.dest].integer = static_cast<i64>(closure);
                ++pc;
                break;
            }
            case Op::LoadCapture: {
                const pp_closure closure = static_cast<pp_closure>(closure_raw);
                frame[in.dest].integer = pp_closure_get(closure, in.imm);
                ++pc;
                break;
            }
            case Op::StoreCapture: {
                const pp_closure closure = static_cast<pp_closure>(closure_raw);
                pp_closure_set(closure, in.imm, frame[in.a].integer);
                ++pc;
                break;
            }

            // Checked arithmetic delegates to the runtime so overflow behaves
            // and reports identically across every backend.
            case Op::AddInt: frame[in.dest].integer = pp_add_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::SubInt: frame[in.dest].integer = pp_sub_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::MulInt: frame[in.dest].integer = pp_mul_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::DivInt: frame[in.dest].integer = pp_div_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::ModInt: frame[in.dest].integer = pp_mod_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::NegInt: frame[in.dest].integer = pp_neg_i64(frame[in.a].integer); ++pc; break;

            case Op::UncheckedAddInt: frame[in.dest].integer = frame[in.a].integer + frame[in.b].integer; ++pc; break;
            case Op::UncheckedSubInt: frame[in.dest].integer = frame[in.a].integer - frame[in.b].integer; ++pc; break;
            case Op::UncheckedMulInt: frame[in.dest].integer = frame[in.a].integer * frame[in.b].integer; ++pc; break;

            case Op::AddFloat: frame[in.dest].real = frame[in.a].real + frame[in.b].real; ++pc; break;
            case Op::SubFloat: frame[in.dest].real = frame[in.a].real - frame[in.b].real; ++pc; break;
            case Op::MulFloat: frame[in.dest].real = frame[in.a].real * frame[in.b].real; ++pc; break;
            case Op::DivFloat: frame[in.dest].real = frame[in.a].real / frame[in.b].real; ++pc; break;
            case Op::NegFloat: frame[in.dest].real = -frame[in.a].real; ++pc; break;

            case Op::BitAnd: frame[in.dest].integer = frame[in.a].integer & frame[in.b].integer; ++pc; break;
            case Op::BitOr: frame[in.dest].integer = frame[in.a].integer | frame[in.b].integer; ++pc; break;
            case Op::BitXor: frame[in.dest].integer = frame[in.a].integer ^ frame[in.b].integer; ++pc; break;
            case Op::ShiftLeft: frame[in.dest].integer = pp_shl_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::ShiftRight: frame[in.dest].integer = pp_shr_i64(frame[in.a].integer, frame[in.b].integer); ++pc; break;
            case Op::BitNot: frame[in.dest].integer = ~frame[in.a].integer; ++pc; break;

            case Op::EqInt: frame[in.dest].integer = frame[in.a].integer == frame[in.b].integer; ++pc; break;
            case Op::NeInt: frame[in.dest].integer = frame[in.a].integer != frame[in.b].integer; ++pc; break;
            case Op::LtInt: frame[in.dest].integer = frame[in.a].integer < frame[in.b].integer; ++pc; break;
            case Op::LeInt: frame[in.dest].integer = frame[in.a].integer <= frame[in.b].integer; ++pc; break;
            case Op::GtInt: frame[in.dest].integer = frame[in.a].integer > frame[in.b].integer; ++pc; break;
            case Op::GeInt: frame[in.dest].integer = frame[in.a].integer >= frame[in.b].integer; ++pc; break;

            case Op::EqFloat: frame[in.dest].integer = frame[in.a].real == frame[in.b].real; ++pc; break;
            case Op::NeFloat: frame[in.dest].integer = frame[in.a].real != frame[in.b].real; ++pc; break;
            case Op::LtFloat: frame[in.dest].integer = frame[in.a].real < frame[in.b].real; ++pc; break;
            case Op::LeFloat: frame[in.dest].integer = frame[in.a].real <= frame[in.b].real; ++pc; break;
            case Op::GtFloat: frame[in.dest].integer = frame[in.a].real > frame[in.b].real; ++pc; break;
            case Op::GeFloat: frame[in.dest].integer = frame[in.a].real >= frame[in.b].real; ++pc; break;

            case Op::EqStr: frame[in.dest].integer = pp_str_eq(frame[in.a].text, frame[in.b].text); ++pc; break;
            case Op::NeStr: frame[in.dest].integer = !pp_str_eq(frame[in.a].text, frame[in.b].text); ++pc; break;
            case Op::LtStr: frame[in.dest].integer = pp_str_cmp(frame[in.a].text, frame[in.b].text) < 0; ++pc; break;
            case Op::LeStr: frame[in.dest].integer = pp_str_cmp(frame[in.a].text, frame[in.b].text) <= 0; ++pc; break;
            case Op::GtStr: frame[in.dest].integer = pp_str_cmp(frame[in.a].text, frame[in.b].text) > 0; ++pc; break;
            case Op::GeStr: frame[in.dest].integer = pp_str_cmp(frame[in.a].text, frame[in.b].text) >= 0; ++pc; break;

            case Op::NotBool: frame[in.dest].integer = !frame[in.a].integer; ++pc; break;
            case Op::ConcatStr: frame[in.dest].text = pp_concat(frame[in.a].text, frame[in.b].text); ++pc; break;

            case Op::Jump: pc = static_cast<u32>(in.imm); break;
            case Op::BranchTrue:
                pc = frame[in.a].integer ? static_cast<u32>(in.imm) : pc + 1;
                break;

            case Op::Call: {
                call_arguments.clear();
                call_arguments.reserve(in.arg_count);
                for (u32 i = 0; i < in.arg_count; ++i) {
                    call_arguments.push_back(frame[program_->arguments[in.arg_offset + i]]);
                }
                const Slot value = invoke(static_cast<u32>(in.imm), call_arguments.data(),
                                          static_cast<u32>(call_arguments.size()));
                // The frame pointer can move when the shared stack grows during
                // the call, so it is refreshed rather than cached.
                frame = stack_.data() + base;
                if (in.dest != 0xFFFFFFFFu) frame[in.dest] = value;
                ++pc;
                break;
            }
            case Op::CallIndirect: {
                call_arguments.clear();
                call_arguments.reserve(in.arg_count);
                for (u32 i = 0; i < in.arg_count; ++i) {
                    call_arguments.push_back(frame[program_->arguments[in.arg_offset + i]]);
                }
                const pp_closure closure = static_cast<pp_closure>(frame[in.a].integer);
                const u32 callee = static_cast<u32>(pp_closure_target(closure));
                const Slot value =
                    invoke(callee, call_arguments.data(),
                           static_cast<u32>(call_arguments.size()), static_cast<i64>(closure));
                frame = stack_.data() + base;
                if (in.dest != 0xFFFFFFFFu) frame[in.dest] = value;
                ++pc;
                break;
            }
            case Op::Spawn: {
                // A task runs on a real worker thread with its own interpreter,
                // so cancellation and task groups behave the same here as under
                // the C backend. Running it inline would be simpler, but then a
                // cancellation request would always arrive after the work had
                // already finished, which is observably different.
                VmTaskContext context;
                context.program = program_;
                context.function = static_cast<u32>(in.imm);
                context.argument_count = in.arg_count;
                if (in.arg_count > VmTaskContext::kMaxArguments) {
                    fail("a spawned task takes at most " +
                         std::to_string(VmTaskContext::kMaxArguments) + " arguments");
                }
                for (u32 i = 0; i < in.arg_count; ++i) {
                    context.arguments[i] = frame[program_->arguments[in.arg_offset + i]];
                }
                // pp_task_spawn copies the context before the worker starts, so
                // passing a frame-local struct is safe.
                pp_task *task = pp_task_spawn(vm_task_entry, &context, (int64_t)sizeof(context));
                frame = stack_.data() + base;
                if (in.dest != 0xFFFFFFFFu) frame[in.dest].pointer = task;
                ++pc;
                break;
            }
            case Op::CallBuiltin: {
                call_arguments.clear();
                call_arguments.reserve(in.arg_count);
                for (u32 i = 0; i < in.arg_count; ++i) {
                    call_arguments.push_back(frame[program_->arguments[in.arg_offset + i]]);
                }
                const Slot value =
                    call_builtin(static_cast<u32>(in.imm), call_arguments.data(),
                                 static_cast<u32>(call_arguments.size()), in.b);
                frame = stack_.data() + base;
                if (in.dest != 0xFFFFFFFFu) frame[in.dest] = value;
                ++pc;
                break;
            }
            case Op::Await: {
                // Joins the worker and takes its result bits. Slot's members
                // all overlay the same 8 bytes, so storing through the integer
                // member round-trips a double or a pointer faithfully.
                const uintptr_t bits =
                    pp_task_await_bits(static_cast<pp_task *>(frame[in.a].pointer));
                if (in.dest != 0xFFFFFFFFu) frame[in.dest].integer = static_cast<i64>(bits);
                ++pc;
                break;
            }

            case Op::Return:
                result = frame[in.a];
                stack_top_ = base;
                --depth_;
                return result;
            case Op::ReturnVoid:
                stack_top_ = base;
                --depth_;
                return result;

            case Op::MakeStruct: {
                Slot *block = allocate_block(static_cast<u32>(in.imm));
                for (u32 i = 0; i < in.arg_count; ++i) {
                    block[i] = frame[program_->arguments[in.arg_offset + i]];
                }
                frame[in.dest].pointer = block;
                ++pc;
                break;
            }
            case Op::GetField:
                frame[in.dest] = static_cast<Slot *>(frame[in.a].pointer)[in.imm];
                ++pc;
                break;
            case Op::SetField:
                static_cast<Slot *>(frame[in.a].pointer)[in.imm] = frame[in.b];
                ++pc;
                break;

            case Op::MakeEnum: {
                // Slot 0 holds the tag; the payload follows.
                Slot *block = allocate_block(1 + in.arg_count);
                block[0].integer = in.imm;
                for (u32 i = 0; i < in.arg_count; ++i) {
                    block[1 + i] = frame[program_->arguments[in.arg_offset + i]];
                }
                frame[in.dest].pointer = block;
                ++pc;
                break;
            }
            case Op::EnumTag:
                frame[in.dest].integer = static_cast<Slot *>(frame[in.a].pointer)[0].integer;
                ++pc;
                break;
            case Op::EnumPayload:
                frame[in.dest] = static_cast<Slot *>(frame[in.a].pointer)[1 + in.imm];
                ++pc;
                break;

            case Op::MakeList: {
                pp_numbers *list = pp_numbers_new();
                for (u32 i = 0; i < in.arg_count; ++i) {
                    pp_push(list, frame[program_->arguments[in.arg_offset + i]].integer);
                }
                frame[in.dest].pointer = list;
                ++pc;
                break;
            }
            case Op::GetIndex:
                frame[in.dest].integer =
                    pp_at(static_cast<pp_numbers *>(frame[in.a].pointer), frame[in.b].integer);
                ++pc;
                break;
            case Op::GetSliceIndex:
                frame[in.dest].integer = pp_slice_at_i64(
                    static_cast<pp_i64_slice *>(frame[in.a].pointer), frame[in.b].integer);
                ++pc;
                break;
            case Op::SetIndex:
                pp_put(static_cast<pp_numbers *>(frame[in.a].pointer), frame[in.b].integer,
                       frame[in.c].integer);
                ++pc;
                break;

            case Op::Deref:
                frame[in.dest] = *static_cast<Slot *>(frame[in.a].pointer);
                ++pc;
                break;
            case Op::StoreDeref:
                *static_cast<Slot *>(frame[in.a].pointer) = frame[in.b];
                ++pc;
                break;
        }
    }

    stack_top_ = base;
    --depth_;
    return result;
}

int Vm::run(const BytecodeProgram &program, int argc, char **argv) {
    program_ = &program;
    stack_.assign(4096, Slot{});
    stack_top_ = 0;
    depth_ = 0;

    if (program.entry >= program.functions.size()) {
        error_ = "the bytecode image has no entry point";
        return 1;
    }

    pp_runtime_init(argc, argv);
    invoke(program.entry, nullptr, 0);
    pp_runtime_cleanup();
    return 0;
}

}  // namespace ppc
