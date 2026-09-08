// Host and environment queries. These do not mutate process state.

fn system_platform() -> String {
    return platform();
}

fn system_current_dir() -> String {
    return current_dir();
}

fn system_env_has(name: String) -> bool {
    return env_has(name);
}

fn system_env_or(name: String, fallback: String) -> String {
    return env_or(name, fallback);
}
