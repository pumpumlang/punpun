# DNS resolution helpers. family is 0 (any), 4 (IPv4), or 6 (IPv6).

fn dns_resolve(host: str) -> List<str> {
    return net_resolve(host, 0);
}

fn dns_resolve_v4(host: str) -> List<str> {
    return net_resolve(host, 4);
}

fn dns_resolve_v6(host: str) -> List<str> {
    return net_resolve(host, 6);
}

async fn dns_resolve_async(host: str) -> List<str> {
    return dns_resolve(host);
}
