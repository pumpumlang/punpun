#include "ppc/sema/builtins.hpp"

#include <unordered_map>

namespace ppc {

namespace {
using B = BuiltinType;
}

const std::vector<BuiltinSpec> &builtin_table() {
    // Order is load-bearing: the index into this vector is the BuiltinId stored
    // in the HIR and read by every backend. Append only.
    static const std::vector<BuiltinSpec> table = {
        // -- console -------------------------------------------------------
        {"print", {B::AnyScalar}, B::Void, "", "Print a scalar without a newline."},
        {"println", {B::AnyScalar}, B::Void, "", "Print a scalar followed by a newline."},
        {"say", {B::AnyScalar}, B::Void, "", "Print a scalar followed by a newline."},
        {"read_line", {}, B::Str, "pp_read_line", "Read one line from standard input."},

        // -- text ----------------------------------------------------------
        {"len", {B::Str}, B::Int, "pp_len", "Byte length of a string."},
        {"concat", {B::Str, B::Str}, B::Str, "pp_concat", "Concatenate two strings."},
        {"slice", {B::Str, B::Int, B::Int}, B::Str, "pp_slice", "Copy string bytes in [start, end)."},
        {"contains", {B::Str, B::Str}, B::Bool, "pp_contains", "Case-sensitive substring test."},
        {"text", {B::Int}, B::Str, "pp_text_int", "Convert an int to str."},
        {"parse_int", {B::Str}, B::Int, "pp_parse_int", "Parse a decimal string as int."},
        {"utf8_valid", {B::Str}, B::Bool, "pp_utf8_valid", "Validate UTF-8 text."},
        {"utf8_len", {B::Str}, B::Int, "pp_utf8_len", "Count scalar values in UTF-8 text."},

        // -- numbers -------------------------------------------------------
        {"abs", {B::Int}, B::Int, "pp_abs_i64", "Checked absolute value."},
        {"decimal", {B::Int}, B::Float, "pp_decimal", "Convert an int to float."},
        {"whole", {B::Float}, B::Int, "pp_whole", "Truncate a float toward zero."},

        // -- nums list -----------------------------------------------------
        {"numbers", {}, B::Nums, "pp_numbers_new", "Create an empty nums list."},
        {"push", {B::Nums, B::Int}, B::Void, "pp_push", "Append an integer to nums."},
        {"at", {B::Nums, B::Int}, B::Int, "pp_at", "Read a checked nums element."},
        {"put", {B::Nums, B::Int, B::Int}, B::Void, "pp_put", "Replace a checked nums element."},
        {"size", {B::Nums}, B::Int, "pp_size", "Number of elements in nums."},
        {"pop", {B::Nums}, B::Int, "pp_pop", "Remove and return the last element."},
        {"sort", {B::Nums}, B::Void, "pp_sort", "Sort nums ascending in place."},

        // -- List<T>: the general growable sequence -------------------------
        // `nums` remains the int-specific list for source compatibility; these
        // are the general form and work for any element type.
        {"list", {}, B::NewList, "pp_list_new",
         "Create an empty List<T>; T comes from context or a type argument."},
        {"list_push", {B::AnyList, B::ListElement}, B::Void, "pp_list_push",
         "Append a value to a List<T>."},
        {"list_at", {B::AnyList, B::Int}, B::ListElement, "pp_list_at",
         "Read a checked List<T> element."},
        {"list_put", {B::AnyList, B::Int, B::ListElement}, B::Void, "pp_list_put",
         "Replace a checked List<T> element."},
        {"list_size", {B::AnyList}, B::Int, "pp_list_size",
         "Number of elements in a List<T>."},
        {"list_pop", {B::AnyList}, B::ListElement, "pp_list_pop",
         "Remove and return the last element of a List<T>."},
        {"list_clear", {B::AnyList}, B::Void, "pp_list_clear",
         "Remove every element from a List<T>."},


        // -- Map<V>: string-keyed hash map ---------------------------------
        {"map", {}, B::NewMap, "pp_map_new",
         "Create an empty Map<V>; V comes from context or a type argument."},
        {"map_put", {B::AnyMap, B::Str, B::MapValue}, B::Void, "pp_map_put",
         "Insert or replace a value under a key."},
        {"map_get", {B::AnyMap, B::Str}, B::MapValue, "pp_map_get",
         "Read a value; panics when the key is absent."},
        {"map_get_or", {B::AnyMap, B::Str, B::MapValue}, B::MapValue, "pp_map_get_or",
         "Read a value, or a fallback when the key is absent."},
        {"map_has", {B::AnyMap, B::Str}, B::Bool, "pp_map_has", "Test for a key."},
        {"map_remove", {B::AnyMap, B::Str}, B::Bool, "pp_map_remove",
         "Remove a key; false when it was not present."},
        {"map_size", {B::AnyMap}, B::Int, "pp_map_size", "Number of entries."},
        {"map_keys", {B::AnyMap}, B::ListOfStr, "pp_map_keys",
         "Every key, in unspecified order."},
        {"map_clear", {B::AnyMap}, B::Void, "pp_map_clear", "Remove every entry."},

        // -- bytes: mutable binary buffer ----------------------------------
        {"bytes", {}, B::Bytes, "pp_bytes_new", "Create an empty byte buffer."},
        {"bytes_from_text", {B::Str}, B::Bytes, "pp_bytes_from_text",
         "UTF-8 bytes of a string."},
        {"bytes_to_text", {B::Bytes}, B::Str, "pp_bytes_to_text",
         "Interpret bytes as text; panics on an embedded NUL."},
        {"bytes_push", {B::Bytes, B::Int}, B::Void, "pp_bytes_push", "Append a byte (0..255)."},
        {"bytes_at", {B::Bytes, B::Int}, B::Int, "pp_bytes_at", "Read a checked byte."},
        {"bytes_put", {B::Bytes, B::Int, B::Int}, B::Void, "pp_bytes_put", "Replace a checked byte."},
        {"bytes_len", {B::Bytes}, B::Int, "pp_bytes_len", "Number of bytes."},
        {"bytes_slice", {B::Bytes, B::Int, B::Int}, B::Bytes, "pp_bytes_slice",
         "Copy bytes in [start, end)."},
        {"bytes_concat", {B::Bytes, B::Bytes}, B::Bytes, "pp_bytes_concat", "Join two buffers."},

        // -- text ----------------------------------------------------------
        {"char_at", {B::Str, B::Int}, B::Int, "pp_char_at", "Byte at an index, 0..255."},
        {"char_str", {B::Int}, B::Str, "pp_char_str", "One-byte string from a byte value."},
        {"index_of", {B::Str, B::Str, B::Int}, B::Int, "pp_index_of",
         "First occurrence at or after a byte offset, or -1."},
        {"last_index_of", {B::Str, B::Str}, B::Int, "pp_last_index_of",
         "Last occurrence, or -1."},
        {"starts_with", {B::Str, B::Str}, B::Bool, "pp_starts_with", "Prefix test."},
        {"ends_with", {B::Str, B::Str}, B::Bool, "pp_ends_with", "Suffix test."},
        {"to_upper", {B::Str}, B::Str, "pp_to_upper", "ASCII uppercase."},
        {"to_lower", {B::Str}, B::Str, "pp_to_lower", "ASCII lowercase."},
        {"trim", {B::Str}, B::Str, "pp_trim", "Remove leading and trailing whitespace."},
        {"replace", {B::Str, B::Str, B::Str}, B::Str, "pp_replace", "Replace every occurrence."},
        {"repeat", {B::Str, B::Int}, B::Str, "pp_repeat", "Concatenate a string with itself."},
        {"split", {B::Str, B::Str}, B::ListOfStr, "pp_split", "Split on a separator."},
        {"join", {B::ListOfStr, B::Str}, B::Str, "pp_join", "Join with a separator."},
        {"text_float", {B::Float}, B::Str, "pp_text_float", "Convert a float to str."},
        {"parse_float", {B::Str}, B::Float, "pp_parse_float", "Parse a string as float."},
        {"pad_left", {B::Str, B::Int, B::Str}, B::Str, "pp_pad_left", "Pad to a width on the left."},
        {"pad_right", {B::Str, B::Int, B::Str}, B::Str, "pp_pad_right", "Pad to a width on the right."},

        // -- math ------------------------------------------------------------
        {"sqrt", {B::Float}, B::Float, "pp_sqrt", "Square root; panics on a negative input."},
        {"pow", {B::Float, B::Float}, B::Float, "pp_pow", "Raise to a power."},
        {"exp", {B::Float}, B::Float, "pp_exp", "e raised to a power."},
        {"log", {B::Float}, B::Float, "pp_log", "Natural logarithm."},
        {"log2", {B::Float}, B::Float, "pp_log2", "Base-2 logarithm."},
        {"log10", {B::Float}, B::Float, "pp_log10", "Base-10 logarithm."},
        {"sin", {B::Float}, B::Float, "pp_sin", "Sine, in radians."},
        {"cos", {B::Float}, B::Float, "pp_cos", "Cosine, in radians."},
        {"tan", {B::Float}, B::Float, "pp_tan", "Tangent, in radians."},
        {"asin", {B::Float}, B::Float, "pp_asin", "Arc sine."},
        {"acos", {B::Float}, B::Float, "pp_acos", "Arc cosine."},
        {"atan", {B::Float}, B::Float, "pp_atan", "Arc tangent."},
        {"atan2", {B::Float, B::Float}, B::Float, "pp_atan2", "Arc tangent of y/x."},
        {"floor", {B::Float}, B::Float, "pp_floor", "Round toward negative infinity."},
        {"ceil", {B::Float}, B::Float, "pp_ceil", "Round toward positive infinity."},
        {"round", {B::Float}, B::Float, "pp_round", "Round to nearest, halves away from zero."},
        {"fabs", {B::Float}, B::Float, "pp_fabs", "Absolute value of a float."},
        {"fmod", {B::Float, B::Float}, B::Float, "pp_fmod", "Float remainder."},
        {"hypot", {B::Float, B::Float}, B::Float, "pp_hypot", "Euclidean distance."},
        {"is_nan", {B::Float}, B::Bool, "pp_is_nan", "Test for NaN."},
        {"is_infinite", {B::Float}, B::Bool, "pp_is_infinite", "Test for an infinity."},

        // -- pseudorandom ----------------------------------------------------
        {"random_seed", {B::Int}, B::Void, "pp_random_seed", "Seed the generator."},
        {"random_int", {B::Int, B::Int}, B::Int, "pp_random_int",
         "Uniform integer in an inclusive range."},
        {"random_float", {}, B::Float, "pp_random_float", "Uniform float in [0, 1)."},
        {"random_bytes", {B::Int}, B::Bytes, "pp_random_bytes",
         "Cryptographically secure bytes from the operating system."},

        // -- filesystem ------------------------------------------------------
        {"list_dir", {B::Str}, B::ListOfStr, "pp_list_dir", "Entry names in a directory."},
        {"is_dir", {B::Str}, B::Bool, "pp_is_dir", "Test whether a path is a directory."},
        {"file_size", {B::Str}, B::Int, "pp_file_size_of", "Size in bytes, or -1."},
        {"read_bytes", {B::Str}, B::Bytes, "pp_read_bytes", "Read a file as binary."},
        {"write_bytes", {B::Str, B::Bytes}, B::Void, "pp_write_bytes", "Write binary to a file."},
        {"append_text", {B::Str, B::Str}, B::Void, "pp_append_text", "Append text to a file."},
        {"remove_dir", {B::Str}, B::Bool, "pp_remove_dir", "Remove an empty directory."},
        {"make_dirs", {B::Str}, B::Bool, "pp_make_dirs", "Create a directory and its parents."},
        {"path_parent", {B::Str}, B::Str, "pp_path_parent", "Directory portion of a path."},
        {"path_name", {B::Str}, B::Str, "pp_path_name", "Final component of a path."},
        {"path_extension", {B::Str}, B::Str, "pp_path_extension", "Extension without the dot."},

        // -- time --------------------------------------------------------------
        {"now_ms", {}, B::Int, "pp_now_ms", "Milliseconds since the Unix epoch."},
        {"year_of", {B::Int}, B::Int, "pp_year_of", "Local year of a timestamp."},
        {"month_of", {B::Int}, B::Int, "pp_month_of", "Local month, 1..12."},
        {"day_of", {B::Int}, B::Int, "pp_day_of", "Local day of month, 1..31."},
        {"hour_of", {B::Int}, B::Int, "pp_hour_of", "Local hour, 0..23."},
        {"minute_of", {B::Int}, B::Int, "pp_minute_of", "Local minute, 0..59."},
        {"second_of", {B::Int}, B::Int, "pp_second_of", "Local second, 0..59."},
        {"weekday_of", {B::Int}, B::Int, "pp_weekday_of", "Day of week, 0 = Sunday."},

        // -- process -----------------------------------------------------------
        {"exit", {B::Int}, B::Void, "pp_exit", "Exit with a status, running cleanup."},
        {"run_command", {B::Str}, B::Int, "pp_run_command", "Run a shell command."},

        // -- checked slices ------------------------------------------------
        {"view", {B::Nums, B::Int, B::Int}, B::IntSlice, "pp_numbers_view",
         "Create a checked borrowed Slice<int> over nums."},
        {"slice_len", {B::IntSlice}, B::Int, "pp_slice_len_i64", "Length of a Slice<int>."},
        {"slice_get", {B::IntSlice, B::Int}, B::Int, "pp_slice_at_i64",
         "Read a checked Slice<int> element."},

        // -- process and environment ---------------------------------------
        {"arg_count", {}, B::Int, "pp_arg_count", "Number of program arguments."},
        {"arg", {B::Int}, B::Str, "pp_arg", "Program argument by index."},
        {"env_has", {B::Str}, B::Bool, "pp_env_has", "Test for an environment variable."},
        {"env_or", {B::Str, B::Str}, B::Str, "pp_env_or", "Environment value or fallback."},
        {"platform", {}, B::Str, "pp_platform", "Current platform name."},
        {"current_dir", {}, B::Str, "pp_current_dir", "Current working directory."},

        // -- filesystem ----------------------------------------------------
        {"read_text", {B::Str}, B::Str, "pp_read_text", "Read an entire text file."},
        {"write_text", {B::Str, B::Str}, B::Void, "pp_write_text", "Write an entire text file."},
        {"file_exists", {B::Str}, B::Bool, "pp_file_exists", "Test whether a regular file exists."},
        {"make_dir", {B::Str}, B::Bool, "pp_make_dir", "Create one directory."},
        {"remove_file", {B::Str}, B::Bool, "pp_remove_file", "Remove a regular file."},
        {"rename_file", {B::Str, B::Str}, B::Bool, "pp_rename_file", "Rename or move a file."},
        {"path_join", {B::Str, B::Str}, B::Str, "pp_path_join", "Join two path segments."},

        // -- time and control ----------------------------------------------
        {"clock_ms", {}, B::Int, "pp_clock_ms", "Monotonic milliseconds."},
        {"sleep_ms", {B::Int}, B::Void, "pp_sleep_ms", "Sleep for a nonnegative duration."},
        {"panic", {B::Str}, B::Void, "pp_panic", "Abort with a PunPun panic message."},
        {"assert", {B::Bool, B::Str}, B::Void, "pp_assert", "Panic when a condition is false."},

        // -- ownership (expanded inline by the backends) ---------------------
        {"move", {B::SameAsArgument}, B::SameAsArgument, "",
         "Transfer an owning value and invalidate the source binding."},
        {"drop", {B::SameAsArgument}, B::Void, "",
         "Destroy a value immediately and invalidate the source binding."},

        // -- async ---------------------------------------------------------
        {"cancel", {B::AnyTask}, B::Void, "pp_task_cancel", "Request cooperative cancellation."},
        {"task_done", {B::AnyTask}, B::Bool, "pp_task_is_done", "Test whether a task finished."},
        {"cancelled", {}, B::Bool, "pp_task_cancelled",
         "Test cancellation inside the current task."},

        // -- structured concurrency (1.0) ----------------------------------
        // A group is one explicit lifetime boundary around a set of tasks. It
        // does not change a task's result type; it gives the caller a single
        // place to wait, cancel, and clean up. The handle is an int rather than
        // a pointer so it can cross the FFI boundary unchanged.
        {"task_group", {}, B::Int, "pp_task_group_new",
         "Create a structured task group and return its opaque handle."},
        {"task_group_add", {B::Int, B::AnyTask}, B::Void, "pp_task_group_add",
         "Attach a task to a structured task group."},
        {"task_group_cancel", {B::Int}, B::Void, "pp_task_group_cancel",
         "Request cooperative cancellation of every task in a group."},
        {"task_group_wait", {B::Int}, B::Void, "pp_task_group_wait",
         "Wait for every task in a group to finish."},
        {"task_group_wait_for", {B::Int, B::Int}, B::Bool, "pp_task_group_wait_for",
         "Wait up to a timeout for a group; false means it is still running."},
        {"task_group_done", {B::Int}, B::Bool, "pp_task_group_is_done",
         "Test whether every task in a group has completed."},
        {"task_group_pending", {B::Int}, B::Int, "pp_task_group_pending",
         "Count the unfinished tasks in a group."},
        {"task_group_close", {B::Int}, B::Void, "pp_task_group_close",
         "Wait for a group and release it."},

        // -- secure HTTPS (1.3) -----------------------------------------
        // These are runtime-backed so C, native, and bytecode programs share
        // one verified-TLS implementation and one observable error model.
        {"https_available", {}, B::Bool, "pp_https_available",
         "Test whether the system libcurl HTTPS runtime is available."},
        {"https_request", {B::Str, B::Str, B::Str, B::Str, B::Int, B::Bool},
         B::Str, "pp_https_request",
         "Perform a verified HTTPS request and return its response body."},
        {"https_status", {}, B::Int, "pp_https_status",
         "Status code from this thread's most recent HTTPS request."},
        {"https_error", {}, B::Str, "pp_https_error",
         "Error from this thread's most recent HTTPS request, or an empty string."},

        // -- sockets / DNS ---------------------------------------------
        // Appended after the stable 1.3 surface. Handles are runtime-owned
        // integers rather than raw descriptors, so Windows and POSIX share the
        // same language ABI.
        {"net_available", {}, B::Bool, "pp_net_available",
         "Test whether the platform socket runtime initialized successfully."},
        {"net_resolve", {B::Str, B::Int}, B::ListOfStr, "pp_net_resolve",
         "Resolve a host; family is 0 for any, 4 for IPv4, or 6 for IPv6."},
        {"net_tcp_connect", {B::Str, B::Int, B::Int}, B::Int, "pp_net_tcp_connect",
         "Open a timeout-aware nonblocking TCP connection."},
        {"net_tcp_listen", {B::Str, B::Int, B::Int}, B::Int, "pp_net_tcp_listen",
         "Bind and listen for TCP connections; port 0 chooses an ephemeral port."},
        {"net_tcp_accept", {B::Int, B::Int}, B::Int, "pp_net_tcp_accept",
         "Accept one TCP connection, waiting up to a timeout."},
        {"net_udp_bind", {B::Str, B::Int}, B::Int, "pp_net_udp_bind",
         "Bind a UDP socket; port 0 chooses an ephemeral port."},
        {"net_socket_close", {B::Int}, B::Bool, "pp_net_socket_close",
         "Close a runtime socket handle. Closing an already closed handle succeeds."},
        {"net_socket_shutdown", {B::Int, B::Int}, B::Bool, "pp_net_socket_shutdown",
         "Shutdown receive (0), send (1), or both (2) directions."},
        {"net_socket_wait_readable", {B::Int, B::Int}, B::Bool, "pp_net_socket_wait_readable",
         "Wait until a socket is readable or a timeout/cancellation occurs."},
        {"net_socket_wait_writable", {B::Int, B::Int}, B::Bool, "pp_net_socket_wait_writable",
         "Wait until a socket is writable or a timeout/cancellation occurs."},
        {"net_socket_set_nodelay", {B::Int, B::Bool}, B::Bool, "pp_net_socket_set_nodelay",
         "Enable or disable TCP_NODELAY."},
        {"net_socket_send", {B::Int, B::Bytes, B::Int}, B::Int, "pp_net_socket_send",
         "Send an entire byte buffer on TCP unless timeout, error, or cancellation stops it."},
        {"net_socket_recv", {B::Int, B::Int, B::Int}, B::Bytes, "pp_net_socket_recv",
         "Receive up to a byte limit from TCP."},
        {"net_udp_send_to", {B::Int, B::Str, B::Int, B::Bytes, B::Int}, B::Int,
         "pp_net_udp_send_to", "Send one UDP datagram."},
        {"net_udp_recv_from", {B::Int, B::Int, B::Int}, B::Bytes, "pp_net_udp_recv_from",
         "Receive one UDP datagram and record its source host/port."},
        {"net_socket_local_port", {B::Int}, B::Int, "pp_net_socket_local_port",
         "Return a socket's bound local port."},
        {"net_socket_peer_host", {B::Int}, B::Str, "pp_net_socket_peer_host",
         "Return a connected socket's numeric peer address."},
        {"net_socket_peer_port", {B::Int}, B::Int, "pp_net_socket_peer_port",
         "Return a connected socket's peer port."},
        {"net_timed_out", {}, B::Bool, "pp_net_timed_out",
         "Whether this thread's most recent network operation timed out."},
        {"net_eof", {}, B::Bool, "pp_net_eof",
         "Whether this thread's most recent TCP receive observed EOF."},
        {"net_error", {}, B::Str, "pp_net_error",
         "Error from this thread's most recent network operation, or empty string."},
        {"net_last_host", {}, B::Str, "pp_net_last_host",
         "Numeric source host recorded by the most recent UDP receive."},
        {"net_last_port", {}, B::Int, "pp_net_last_port",
         "Source port recorded by the most recent UDP receive."},
        {"https_headers_raw", {}, B::Str, "pp_https_headers_raw",
         "Raw final response headers from this thread's most recent HTTPS request."},
        {"https_body_bytes", {}, B::Bytes, "pp_https_body_bytes",
         "Binary body from this thread's most recent successful HTTPS request."},
        {"https_request_bytes", {B::Str, B::Str, B::Bytes, B::Str, B::Int, B::Bool},
         B::Bytes, "pp_https_request_bytes",
         "Perform verified HTTPS with a binary request body and return binary response bytes."},

        // -- native GUI foundation (1.3) -------------------------------
        {"gui_available", {}, B::Bool, "pp_gui_available",
         "Test whether a supported native display is available."},
        {"gui_headless", {}, B::Bool, "pp_gui_headless",
         "Whether the retained GUI model is running without a native display."},
        {"gui_message", {B::Str, B::Str}, B::Bool, "pp_gui_message",
         "Show a native message window; false when no GUI backend is available."},
        {"gui_window_create", {B::Str, B::Int, B::Int}, B::Int, "pp_gui_window_create",
         "Create a retained native window and return its handle."},
        {"gui_window_show", {B::Int, B::Bool}, B::Bool, "pp_gui_window_show",
         "Show or hide a GUI window."},
        {"gui_window_close", {B::Int}, B::Bool, "pp_gui_window_close",
         "Close a GUI window."},
        {"gui_window_open", {B::Int}, B::Bool, "pp_gui_window_open",
         "Test whether a GUI window is still open."},
        {"gui_window_set_title", {B::Int, B::Str}, B::Bool, "pp_gui_window_set_title",
         "Change a GUI window title."},
        {"gui_window_width", {B::Int}, B::Int, "pp_gui_window_width",
         "Return the current GUI window width."},
        {"gui_window_height", {B::Int}, B::Int, "pp_gui_window_height",
         "Return the current GUI window height."},
        {"gui_widget_create", {B::Int, B::Int, B::Str}, B::Int, "pp_gui_widget_create",
         "Create a retained widget in a window."},
        {"gui_widget_destroy", {B::Int}, B::Bool, "pp_gui_widget_destroy",
         "Destroy a retained widget."},
        {"gui_widget_set_bounds", {B::Int, B::Int, B::Int, B::Int, B::Int}, B::Bool, "pp_gui_widget_set_bounds",
         "Set a widget's x, y, width and height."},
        {"gui_widget_x", {B::Int}, B::Int, "pp_gui_widget_x", "Return a widget's x coordinate."},
        {"gui_widget_y", {B::Int}, B::Int, "pp_gui_widget_y", "Return a widget's y coordinate."},
        {"gui_widget_width", {B::Int}, B::Int, "pp_gui_widget_width", "Return a widget's width."},
        {"gui_widget_height", {B::Int}, B::Int, "pp_gui_widget_height", "Return a widget's height."},
        {"gui_widget_set_text", {B::Int, B::Str}, B::Bool, "pp_gui_widget_set_text",
         "Change a widget's text."},
        {"gui_widget_text", {B::Int}, B::Str, "pp_gui_widget_text",
         "Read a widget's text."},
        {"gui_widget_set_value", {B::Int, B::Int}, B::Bool, "pp_gui_widget_set_value",
         "Set a checkbox, slider or progress value."},
        {"gui_widget_value", {B::Int}, B::Int, "pp_gui_widget_value",
         "Read a checkbox, slider or progress value."},
        {"gui_widget_set_range", {B::Int, B::Int, B::Int}, B::Bool, "pp_gui_widget_set_range",
         "Set a slider or progress range."},
        {"gui_widget_set_visible", {B::Int, B::Bool}, B::Bool, "pp_gui_widget_set_visible",
         "Show or hide one widget."},
        {"gui_widget_set_enabled", {B::Int, B::Bool}, B::Bool, "pp_gui_widget_set_enabled",
         "Enable or disable one widget."},
        {"gui_redraw", {B::Int}, B::Bool, "pp_gui_redraw",
         "Request a retained GUI window redraw."},
        {"gui_poll", {B::Int, B::Int}, B::Int, "pp_gui_poll",
         "Poll one GUI window for the next event."},
        {"gui_post_event", {B::Int, B::Int, B::Int, B::Int, B::Str, B::Int, B::Int}, B::Bool, "pp_gui_post_event",
         "Post an application-defined GUI event to a window."},
        {"gui_event_window", {}, B::Int, "pp_gui_event_window",
         "Window associated with the last GUI event."},
        {"gui_event_widget", {}, B::Int, "pp_gui_event_widget",
         "Widget associated with the last GUI event."},
        {"gui_event_key", {}, B::Int, "pp_gui_event_key",
         "Key/button code associated with the last GUI event."},
        {"gui_event_x", {}, B::Int, "pp_gui_event_x",
         "X coordinate associated with the last GUI event."},
        {"gui_event_y", {}, B::Int, "pp_gui_event_y",
         "Y coordinate associated with the last GUI event."},
        {"gui_event_text", {}, B::Str, "pp_gui_event_text",
         "Text payload associated with the last GUI event."},
        {"gui_canvas_clear", {B::Int, B::Int}, B::Bool, "pp_gui_canvas_clear",
         "Clear a canvas widget to an RGB color."},
        {"gui_canvas_rect", {B::Int, B::Int, B::Int, B::Int, B::Int, B::Int, B::Bool}, B::Bool, "pp_gui_canvas_rect",
         "Draw a rectangle into a canvas widget."},
        {"gui_canvas_line", {B::Int, B::Int, B::Int, B::Int, B::Int, B::Int}, B::Bool, "pp_gui_canvas_line",
         "Draw a line into a canvas widget."},
        {"gui_canvas_text", {B::Int, B::Int, B::Int, B::Str, B::Int}, B::Bool, "pp_gui_canvas_text",
         "Draw text into a canvas widget."},

        // -- broader standard-library host primitives -------------------
        {"env_set", {B::Str, B::Str}, B::Bool, "pp_env_set",
         "Set or replace an environment variable in this process."},
        {"hostname", {}, B::Str, "pp_hostname", "Current machine host name."},
        {"cpu_count", {}, B::Int, "pp_cpu_count", "Online logical CPU count."},
        {"process_capture", {B::Str}, B::Str, "pp_process_capture",
         "Run a shell command and capture standard output."},
        {"process_status", {}, B::Int, "pp_process_status",
         "Exit status from this thread's most recent process_capture call."},
    };
    return table;
}

BuiltinId find_builtin(const std::string &name) {
    static const std::unordered_map<std::string, BuiltinId> index = [] {
        std::unordered_map<std::string, BuiltinId> map;
        const auto &table = builtin_table();
        for (std::size_t i = 0; i < table.size(); ++i) {
            map.emplace(table[i].name, static_cast<BuiltinId>(i));
        }
        return map;
    }();
    auto it = index.find(name);
    return it == index.end() ? kNotBuiltin : it->second;
}

const Type *builtin_type_to_type(BuiltinType kind, TypeContext &types) {
    switch (kind) {
        case BuiltinType::Void: return types.void_type();
        case BuiltinType::Bool: return types.bool_type();
        case BuiltinType::Int: return types.int_type();
        case BuiltinType::Float: return types.float_type();
        case BuiltinType::Str: return types.str_type();
        case BuiltinType::Nums: return types.nums_type();
        case BuiltinType::Bytes: return types.bytes_type();
        case BuiltinType::ListOfStr: return types.list(types.str_type());
        case BuiltinType::IntSlice: return types.slice(types.int_type());
        case BuiltinType::AnyScalar:
        case BuiltinType::SameAsArgument:
        case BuiltinType::AnyTask:
        case BuiltinType::AnyList:
        case BuiltinType::ListElement:
        case BuiltinType::NewList:
        case BuiltinType::AnyMap:
        case BuiltinType::MapValue:
        case BuiltinType::NewMap:
            return nullptr;  // resolved per call site by the checker
    }
    return nullptr;
}

std::string builtin_signature(const BuiltinSpec &spec) {
    auto name_of = [](BuiltinType kind) -> const char * {
        switch (kind) {
            case BuiltinType::Void: return "void";
            case BuiltinType::Bool: return "bool";
            case BuiltinType::Int: return "int";
            case BuiltinType::Float: return "float";
            case BuiltinType::Str: return "str";
            case BuiltinType::Nums: return "nums";
            case BuiltinType::IntSlice: return "Slice<int>";
            case BuiltinType::AnyScalar: return "scalar";
            case BuiltinType::SameAsArgument: return "T";
            case BuiltinType::AnyTask: return "task<T>";
            case BuiltinType::AnyList: return "List<T>";
            case BuiltinType::ListElement: return "T";
            case BuiltinType::NewList: return "List<T>";
            case BuiltinType::AnyMap: return "Map<V>";
            case BuiltinType::MapValue: return "V";
            case BuiltinType::NewMap: return "Map<V>";
            case BuiltinType::ListOfStr: return "List<str>";
            case BuiltinType::Bytes: return "bytes";
        }
        return "?";
    };
    std::string result = std::string(spec.name) + "(";
    for (std::size_t i = 0; i < spec.params.size(); ++i) {
        if (i) result += ", ";
        result += name_of(spec.params[i]);
    }
    result += ") -> ";
    result += name_of(spec.result);
    return result;
}

}  // namespace ppc
