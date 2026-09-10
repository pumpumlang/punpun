struct Point { x: int, y: int }

launch {
    // Explicit element type.
    let names = list<str>();
    list_push(names, "alpha");
    list_push(names, "beta");
    say(list_size(names));
    say(list_at(names, 1));

    // Element type from the annotation.
    let numbers2: List<int> = list();
    for i in 0..5 { list_push(numbers2, i * i); }
    say(list_size(numbers2));
    say(list_at(numbers2, 4));
    list_put(numbers2, 0, 99);
    say(list_at(numbers2, 0));
    say(list_pop(numbers2));

    // A list of aggregates: the thing nums could never do.
    let points = list<Point>();
    list_push(points, Point(3, 4));
    list_push(points, Point(10, 20));
    say(list_size(points));
    say(list_at(points, 1).x + list_at(points, 1).y);

    // Floats must round-trip their bits, not their value.
    let reals = list<float>();
    list_push(reals, 2.5);
    list_push(reals, 0.125);
    say(list_at(reals, 0) + list_at(reals, 1));
}
