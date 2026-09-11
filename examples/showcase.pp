bring std::math;
bring std::stats;
bring std::text;

struct Sensor {
    name: String,
    location: String,
}

struct Report {
    sensor: Sensor,
    readings: nums,
    budget_ms: i64,
}

launch {
    assert(arg_count() <= 1, "usage: showcase [notes-file]");
    let mut note = "Built-in sample; no file was read.";
    if arg_count() == 1 {
        note = text_trim(read_text(arg(0)));
    }

    let original = Report(Sensor("relay", "lab"), [18, 24, 17, 61, 22, 35], 30);
    let mut report = original;
    report.sensor.name = concat(report.sensor.name, " / evening");
    report.budget_ms = clamp(40, 10, 100);
    assert(original.sensor.name == "relay", "nested records must copy by value");
    assert(original.budget_ms == 30, "record fields must copy by value");

    // Copying a record copies its nums handle, not the list's elements.
    let shared = report.readings;
    push(shared, 28);
    shared[0] = 19;
    assert(size(original.readings) == 7, "list handles must share mutations");
    assert(original.readings[0] == 19, "indexed writes must reach shared handles");

    say(text_repeat("=", 44));
    say("PUNPUN / SENSOR REPORT");
    say(report.sensor.name + " @ " + report.sensor.location);
    say("Note: " + slice(note, 0, minimum(len(note), 120)));
    say("Samples: " + text(size(report.readings)));
    say("Budget: " + text(report.budget_ms) + " ms");
    say("Range: " + text(stats_min(shared)) + ".." + text(stats_max(shared)) + " ms");
    print("Mean (ms): ");
    say(stats_mean(shared));

    let ordered = stats_sorted(shared);
    say("Sorted samples (ms):");
    for i in 0..size(ordered) {
        say(ordered[i]);
    }

    let mut over_budget = 0;
    for i in 0..size(shared) {
        if shared[i] <= report.budget_ms {
            continue;
        }
        over_budget = over_budget + 1;
        say("Slow sample #" + text(i) + ": " + text(shared[i]) + " ms");
    }
    if over_budget == 0 {
        say("All samples are within budget.");
    } else {
        say(text(over_budget) + " sample(s) exceed the budget.");
    }
    say(text_repeat("=", 44));
}
