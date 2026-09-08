fn cli_count() -> i64 { return arg_count(); }
fn cli_arg(index: i64) -> String { return arg(index); }
fn cli_has_args() -> bool { return arg_count() > 0; }
