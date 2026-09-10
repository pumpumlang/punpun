fn source() -> Option<int> { return Option::Some(1); }
fn user() -> Result<int, str> { let v = source()?; return Result::Ok(v); }
launch { say(1); }
