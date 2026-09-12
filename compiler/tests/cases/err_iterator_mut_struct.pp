struct MutableStructIter {
    let mut value: int,
    public fn advance(mut self) -> Option<int> {
        self.value = self.value + 1;
        return Option::Some(self.value);
    }
}

launch {
    let values = MutableStructIter(0);
    for value in values {
        say(value);
        break;
    }
}
