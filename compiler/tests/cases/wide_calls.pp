// Wide direct and indirect calls exercise System V stack argument passing.
fn isum(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) -> int {
    return a + b + c + d + e + f + g + h;
}

fn fsum(a: float, b: float, c: float, d: float, e: float,
        f: float, g: float, h: float, i: float, j: float) -> float {
    return a + b + c + d + e + f + g + h + i + j;
}

fn mixed(a: int, b: float, c: int, d: float, e: int, f: float,
         g: int, h: float, i: int, j: float, k: int, l: float,
         m: int, n: float, o: int, p: float, q: int, r: float) -> int {
    return a + c + e + g + i + k + m + o + q;
}

launch {
    say(isum(1, 2, 3, 4, 5, 6, 7, 8));
    say(fsum(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0));
    say(mixed(1, 1.0, 2, 2.0, 3, 3.0, 4, 4.0, 5, 5.0,
              6, 6.0, 7, 7.0, 8, 8.0, 9, 9.0));

    let indirect: fn(int, int, int, int, int, int, int, int) -> int = isum;
    say(indirect(8, 7, 6, 5, 4, 3, 2, 1));
}
