object BrokenIter {
    public fn advance() -> int { return 1; }
}

struct BrokenRange {
    public fn iter() -> BrokenIter { return BrokenIter(); }
}

launch {
    let values = BrokenRange();
    for value in values { say(value); }
}
