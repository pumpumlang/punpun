fn same(left: String, right: String) -> bool {
    return len(left) == len(right) && contains(left, right);
}

fn cat3(first: String, second: String, third: String) -> String {
    return concat(concat(first, second), third);
}

fn cat4(first: String, second: String, third: String, fourth: String) -> String {
    return concat(cat3(first, second, third), fourth);
}

fn cursor_new() -> nums {
    let value: nums = numbers();
    push(value, 0);
    return value;
}

fn cursor_get(cursor: nums) -> i64 {
    return at(cursor, 0);
}

fn cursor_set(cursor: nums, value: i64) -> void {
    put(cursor, 0, value);
}

fn self_char_at(source: String, index: i64) -> String {
    return slice(source, index, index + 1);
}

fn is_space(value: String) -> bool {
    return len(value) == 1 && contains(" \t\r\n", value);
}

fn is_digit(value: String) -> bool {
    return len(value) == 1 && contains("0123456789", value);
}

fn is_alpha(value: String) -> bool {
    return len(value) == 1 && contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_", value);
}

fn is_ident_continue(value: String) -> bool {
    return is_alpha(value) || is_digit(value);
}

fn skip_trivia(source: String, cursor: nums) -> void {
    let mut position: i64 = cursor_get(cursor);
    let length: i64 = len(source);
    let mut scanning: bool = true;
    while scanning && position < length {
        let current: String = self_char_at(source, position);
        if is_space(current) {
            position = position + 1;
        } else {
            if same(current, "/") && position + 1 < length && same(self_char_at(source, position + 1), "/") {
                position = position + 2;
                while position < length && !same(self_char_at(source, position), "\n") {
                    position = position + 1;
                }
            } else {
                scanning = false;
            }
        }
    }
    cursor_set(cursor, position);
}

fn next_token(source: String, cursor: nums) -> String {
    skip_trivia(source, cursor);
    let start: i64 = cursor_get(cursor);
    let length: i64 = len(source);
    if start >= length {
        return "<eof>";
    }
    let first: String = self_char_at(source, start);
    let mut position: i64 = start;
    if is_alpha(first) {
        position = position + 1;
        while position < length && is_ident_continue(self_char_at(source, position)) {
            position = position + 1;
        }
        cursor_set(cursor, position);
        return slice(source, start, position);
    }
    if is_digit(first) {
        position = position + 1;
        while position < length && is_digit(self_char_at(source, position)) {
            position = position + 1;
        }
        cursor_set(cursor, position);
        return slice(source, start, position);
    }
    if same(first, "\"") {
        position = position + 1;
        let mut closed: bool = false;
        while position < length && !closed {
            let current: String = self_char_at(source, position);
            if same(current, "\\") {
                position = position + 2;
            } else {
                position = position + 1;
                if same(current, "\"") {
                    closed = true;
                }
            }
        }
        if !closed {
            panic("selfhost lexer: unterminated string");
        }
        cursor_set(cursor, position);
        return slice(source, start, position);
    }
    if start + 1 < length {
        let pair: String = slice(source, start, start + 2);
        if same(pair, "->") || same(pair, "==") || same(pair, "!=") || same(pair, "<=") || same(pair, ">=") || same(pair, "&&") || same(pair, "||") {
            cursor_set(cursor, start + 2);
            return pair;
        }
    }
    cursor_set(cursor, start + 1);
    return first;
}

fn peek_token(source: String, cursor: nums) -> String {
    let copy: nums = cursor_new();
    cursor_set(copy, cursor_get(cursor));
    return next_token(source, copy);
}

fn expect_token(source: String, cursor: nums, expected: String) -> void {
    let actual: String = next_token(source, cursor);
    if !same(actual, expected) {
        panic(cat4("selfhost parser: expected '", expected, "', found '", concat(actual, "'")));
    }
}

fn is_identifier(value: String) -> bool {
    if len(value) == 0 || !is_alpha(self_char_at(value, 0)) {
        return false;
    }
    let mut index: i64 = 1;
    while index < len(value) {
        if !is_ident_continue(self_char_at(value, index)) {
            return false;
        }
        index = index + 1;
    }
    return true;
}

fn indent(level: i64) -> String {
    let mut result: String = "";
    let mut index: i64 = 0;
    while index < level {
        result = concat(result, "    ");
        index = index + 1;
    }
    return result;
}

fn map_type_name(name: String) -> String {
    if same(name, "String") || same(name, "text") || same(name, "str") {
        return "const char *";
    }
    if same(name, "i64") || same(name, "int") {
        return "int64_t";
    }
    if same(name, "bool") {
        return "bool";
    }
    if same(name, "void") {
        return "void";
    }
    if same(name, "nums") {
        return "pp_numbers *";
    }
    panic(concat("selfhost parser: unsupported type ", name));
    return "void";
}

fn parse_type(source: String, cursor: nums) -> String {
    return map_type_name(next_token(source, cursor));
}

fn map_call_name(name: String) -> String {
    if same(name, "len") { return "pp_len"; }
    if same(name, "concat") { return "pp_concat"; }
    if same(name, "slice") { return "pp_slice"; }
    if same(name, "contains") { return "pp_contains"; }
    if same(name, "read_text") { return "pp_read_text"; }
    if same(name, "write_text") { return "pp_write_text"; }
    if same(name, "file_exists") { return "pp_file_exists"; }
    if same(name, "arg_count") { return "pp_arg_count"; }
    if same(name, "arg") { return "pp_arg"; }
    if same(name, "numbers") { return "pp_numbers_new"; }
    if same(name, "push") { return "pp_push"; }
    if same(name, "at") { return "pp_at"; }
    if same(name, "put") { return "pp_put"; }
    if same(name, "size") { return "pp_size"; }
    if same(name, "text") { return "pp_text_int"; }
    if same(name, "println") || same(name, "say") { return "pp_println_str"; }
    if same(name, "panic") { return "pp_panic"; }
    return concat("pp_self_", name);
}

fn binary_expression(left: String, operation: String, right: String) -> String {
    let mut mapped: String = operation;
    if same(operation, "and") { mapped = "&&"; }
    if same(operation, "or") { mapped = "||"; }
    return cat4("(", left, concat(" ", mapped), concat(" ", concat(right, ")")));
}

fn parse_primary(source: String, cursor: nums) -> String {
    let token: String = next_token(source, cursor);
    if same(token, "(") {
        let nested: String = parse_expression(source, cursor);
        expect_token(source, cursor, ")");
        return cat3("(", nested, ")");
    }
    if same(token, "true") || same(token, "false") {
        return token;
    }
    if len(token) > 0 && same(self_char_at(token, 0), "\"") {
        return token;
    }
    if is_digit(self_char_at(token, 0)) {
        return token;
    }
    if is_identifier(token) {
        if same(peek_token(source, cursor), "(") {
            expect_token(source, cursor, "(");
            let mut arguments: String = "";
            let mut first_argument: bool = true;
            while !same(peek_token(source, cursor), ")") {
                if !first_argument {
                    expect_token(source, cursor, ",");
                    arguments = concat(arguments, ", ");
                }
                arguments = concat(arguments, parse_expression(source, cursor));
                first_argument = false;
            }
            expect_token(source, cursor, ")");
            return cat4(map_call_name(token), "(", arguments, ")");
        }
        return token;
    }
    panic(concat("selfhost parser: unexpected expression token ", token));
    return "0";
}

fn parse_unary(source: String, cursor: nums) -> String {
    let token: String = peek_token(source, cursor);
    if same(token, "!") || same(token, "not") || same(token, "-") {
        next_token(source, cursor);
        let mut mapped: String = token;
        if same(token, "not") { mapped = "!"; }
        return cat3("(", concat(mapped, parse_unary(source, cursor)), ")");
    }
    return parse_primary(source, cursor);
}

fn parse_multiply(source: String, cursor: nums) -> String {
    let mut left: String = parse_unary(source, cursor);
    let mut operation: String = peek_token(source, cursor);
    while same(operation, "*") || same(operation, "/") || same(operation, "%") {
        next_token(source, cursor);
        left = binary_expression(left, operation, parse_unary(source, cursor));
        operation = peek_token(source, cursor);
    }
    return left;
}

fn parse_add(source: String, cursor: nums) -> String {
    let mut left: String = parse_multiply(source, cursor);
    let mut operation: String = peek_token(source, cursor);
    while same(operation, "+") || same(operation, "-") {
        next_token(source, cursor);
        left = binary_expression(left, operation, parse_multiply(source, cursor));
        operation = peek_token(source, cursor);
    }
    return left;
}

fn parse_compare(source: String, cursor: nums) -> String {
    let mut left: String = parse_add(source, cursor);
    let mut operation: String = peek_token(source, cursor);
    while same(operation, "<") || same(operation, "<=") || same(operation, ">") || same(operation, ">=") {
        next_token(source, cursor);
        left = binary_expression(left, operation, parse_add(source, cursor));
        operation = peek_token(source, cursor);
    }
    return left;
}

fn parse_equality(source: String, cursor: nums) -> String {
    let mut left: String = parse_compare(source, cursor);
    let mut operation: String = peek_token(source, cursor);
    while same(operation, "==") || same(operation, "!=") {
        next_token(source, cursor);
        left = binary_expression(left, operation, parse_compare(source, cursor));
        operation = peek_token(source, cursor);
    }
    return left;
}

fn parse_and(source: String, cursor: nums) -> String {
    let mut left: String = parse_equality(source, cursor);
    let mut operation: String = peek_token(source, cursor);
    while same(operation, "&&") || same(operation, "and") {
        next_token(source, cursor);
        left = binary_expression(left, operation, parse_equality(source, cursor));
        operation = peek_token(source, cursor);
    }
    return left;
}

fn parse_expression(source: String, cursor: nums) -> String {
    let mut left: String = parse_and(source, cursor);
    let mut operation: String = peek_token(source, cursor);
    while same(operation, "||") || same(operation, "or") {
        next_token(source, cursor);
        left = binary_expression(left, operation, parse_and(source, cursor));
        operation = peek_token(source, cursor);
    }
    return left;
}

fn parse_if_statement(source: String, cursor: nums, level: i64) -> String {
    expect_token(source, cursor, "if");
    let condition: String = parse_expression(source, cursor);
    let body: String = parse_block(source, cursor, level);
    let mut result: String = cat4(indent(level), "if (", condition, concat(") ", body));
    if same(peek_token(source, cursor), "else") {
        next_token(source, cursor);
        result = concat(result, concat(" else ", parse_block(source, cursor, level)));
    }
    return concat(result, "\n");
}

fn parse_statement(source: String, cursor: nums, level: i64) -> String {
    let token: String = peek_token(source, cursor);
    if same(token, "let") {
        next_token(source, cursor);
        if same(peek_token(source, cursor), "mut") {
            next_token(source, cursor);
        }
        let name: String = next_token(source, cursor);
        expect_token(source, cursor, ":");
        let kind: String = parse_type(source, cursor);
        expect_token(source, cursor, "=");
        let value: String = parse_expression(source, cursor);
        expect_token(source, cursor, ";");
        return cat4(indent(level), kind, concat(" ", name), concat(" = ", concat(value, ";\n")));
    }
    if same(token, "if") {
        return parse_if_statement(source, cursor, level);
    }
    if same(token, "while") {
        next_token(source, cursor);
        let condition: String = parse_expression(source, cursor);
        let body: String = parse_block(source, cursor, level);
        return cat4(indent(level), "while (", condition, concat(") ", concat(body, "\n")));
    }
    if same(token, "return") {
        next_token(source, cursor);
        if same(peek_token(source, cursor), ";") {
            next_token(source, cursor);
            return concat(indent(level), "return;\n");
        }
        let value: String = parse_expression(source, cursor);
        expect_token(source, cursor, ";");
        return cat4(indent(level), "return ", value, ";\n");
    }
    if same(token, "break") || same(token, "continue") {
        next_token(source, cursor);
        expect_token(source, cursor, ";");
        return cat3(indent(level), token, ";\n");
    }
    let saved: i64 = cursor_get(cursor);
    let name: String = next_token(source, cursor);
    if is_identifier(name) && same(peek_token(source, cursor), "=") {
        next_token(source, cursor);
        let value: String = parse_expression(source, cursor);
        expect_token(source, cursor, ";");
        return cat4(indent(level), name, " = ", concat(value, ";\n"));
    }
    cursor_set(cursor, saved);
    let expression: String = parse_expression(source, cursor);
    expect_token(source, cursor, ";");
    return cat3(indent(level), expression, ";\n");
}

fn parse_block(source: String, cursor: nums, level: i64) -> String {
    expect_token(source, cursor, "{");
    let mut result: String = "{\n";
    while !same(peek_token(source, cursor), "}") {
        if same(peek_token(source, cursor), "<eof>") {
            panic("selfhost parser: unterminated block");
        }
        result = concat(result, parse_statement(source, cursor, level + 1));
    }
    expect_token(source, cursor, "}");
    return concat(result, concat(indent(level), "}"));
}

fn parse_parameters(source: String, cursor: nums) -> String {
    expect_token(source, cursor, "(");
    let mut result: String = "";
    let mut first: bool = true;
    while !same(peek_token(source, cursor), ")") {
        if !first {
            expect_token(source, cursor, ",");
            result = concat(result, ", ");
        }
        let name: String = next_token(source, cursor);
        expect_token(source, cursor, ":");
        let kind: String = parse_type(source, cursor);
        result = cat4(result, kind, " ", name);
        first = false;
    }
    expect_token(source, cursor, ")");
    return result;
}

fn skip_block(source: String, cursor: nums) -> void {
    expect_token(source, cursor, "{");
    let mut depth: i64 = 1;
    while depth > 0 {
        let token: String = next_token(source, cursor);
        if same(token, "<eof>") {
            panic("selfhost parser: unterminated top-level block");
        }
        if same(token, "{") { depth = depth + 1; }
        if same(token, "}") { depth = depth - 1; }
    }
}

fn parse_function(source: String, cursor: nums, prototype: bool) -> String {
    expect_token(source, cursor, "fn");
    let name: String = next_token(source, cursor);
    let parameters: String = parse_parameters(source, cursor);
    expect_token(source, cursor, "->");
    let result_type: String = parse_type(source, cursor);
    let signature: String = cat4("static ", result_type, concat(" pp_self_", name), cat3("(", parameters, ")"));
    if prototype {
        skip_block(source, cursor);
        return concat(signature, ";\n");
    }
    return concat(signature, concat(" ", concat(parse_block(source, cursor, 0), "\n\n")));
}

fn compile_program(source: String) -> String {
    let first_pass: nums = cursor_new();
    let mut declarations: String = "";
    let mut has_launch: bool = false;
    while !same(peek_token(source, first_pass), "<eof>") {
        let token: String = peek_token(source, first_pass);
        if same(token, "fn") {
            declarations = concat(declarations, parse_function(source, first_pass, true));
        } else {
            if same(token, "launch") {
                next_token(source, first_pass);
                skip_block(source, first_pass);
                if !has_launch {
                    declarations = concat(declarations, "static void pp_self_main(void);\n");
                    has_launch = true;
                }
            } else {
                panic(concat("selfhost parser: unsupported top-level token ", token));
            }
        }
    }
    if !has_launch {
        panic("selfhost parser: program requires launch");
    }
    let second_pass: nums = cursor_new();
    let mut definitions: String = "";
    while !same(peek_token(source, second_pass), "<eof>") {
        let token: String = peek_token(source, second_pass);
        if same(token, "fn") {
            definitions = concat(definitions, parse_function(source, second_pass, false));
        } else {
            if same(token, "launch") {
                next_token(source, second_pass);
                definitions = concat(definitions, concat("static void pp_self_main(void) ", concat(parse_block(source, second_pass, 0), "\n\n")));
            } else {
                panic(concat("selfhost parser: unsupported top-level token ", token));
            }
        }
    }
    let header: String = "#include \"ppcrt.h\"\n#include <stdint.h>\n#include <stdbool.h>\n\n";
    let entry: String = "int main(int argc, char **argv) {\n    pp_runtime_init(argc, argv);\n    pp_self_main();\n    pp_runtime_cleanup();\n    return 0;\n}\n";
    return concat(header, concat(declarations, concat("\n", concat(definitions, entry))));
}

launch {
    if arg_count() != 2 {
        println("usage: ppc-self INPUT.pp OUTPUT.c");
        return;
    }
    let input: String = arg(0);
    let output: String = arg(1);
    let source: String = read_text(input);
    let generated: String = compile_program(source);
    write_text(output, generated);
    println(concat("ppc-self: wrote ", output));
    return;
}
