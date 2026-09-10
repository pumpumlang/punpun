// CSV reading and writing, following RFC 4180.
//
// Fields containing a comma, a quote, or a newline are quoted, and a quote
// inside a quoted field is doubled. Parsing handles all three cases, because
// data exported from a spreadsheet routinely contains them.

fn csv_escape(field: str) -> str {
    // Wrapped in parentheses so the expression may span lines: a newline
    // outside brackets ends a statement in PunPun.
    let needs_quotes = (contains(field, ",") or contains(field, "\"")
                        or contains(field, "\n") or contains(field, "\r"));
    if !needs_quotes { return field; }
    return "\"" + replace(field, "\"", "\"\"") + "\"";
}

fn csv_write_row(fields: List<str>) -> str {
    let escaped = list<str>();
    for i in 0..list_size(fields) {
        list_push(escaped, csv_escape(list_at(fields, i)));
    }
    return join(escaped, ",");
}

/// Splits one CSV line into fields. A quoted field may contain commas; a
/// doubled quote inside one is a literal quote.
fn csv_parse_row(line: str) -> List<str> {
    let fields = list<str>();
    let mut current = "";
    let mut inside_quotes = false;
    let length = len(line);
    let mut i = 0;

    while i < length {
        let code = char_at(line, i);
        if inside_quotes {
            if code == 34 {
                // A doubled quote is an escaped quote; a single one closes the
                // field.
                if i + 1 < length and char_at(line, i + 1) == 34 {
                    current = current + "\"";
                    i = i + 1;
                } else {
                    inside_quotes = false;
                }
            } else {
                current = current + char_str(code);
            }
        } else {
            if code == 34 {
                inside_quotes = true;
            } else if code == 44 {
                list_push(fields, current);
                current = "";
            } else {
                current = current + char_str(code);
            }
        }
        i = i + 1;
    }
    list_push(fields, current);
    return fields;
}

fn csv_write(rows: List<str>) -> str { return join(rows, "\n"); }

fn csv_field_count(line: str) -> int { return list_size(csv_parse_row(line)); }
