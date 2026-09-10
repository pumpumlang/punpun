fn greeting(name: String) -> String {
    return concat("hello from self-hosted PunPun, ", name);
}

launch {
    println(greeting("world"));
    return;
}
