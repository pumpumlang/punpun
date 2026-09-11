bring std.math
bring std.stats
bring std.text

shape Sensor:
    name as str
    location as str
done

shape Report:
    sensor as Sensor
    readings as nums
    budget_ms as int
done

launch:
    assert(arg_count() <= 1, "usage: showcase [notes-file]")
    keep note <- "Built-in sample; no file was read."
    when arg_count() == 1:
        note <- text_trim(read_text(arg(0)))
    done

    pin original <- Report(Sensor("relay", "lab"), [18, 24, 17, 61, 22, 35], 30)
    keep report <- original
    report.sensor.name <- concat(report.sensor.name, " / evening")
    report.budget_ms <- clamp(40, 10, 100)
    assert(original.sensor.name == "relay", "nested records must copy by value")
    assert(original.budget_ms == 30, "record fields must copy by value")

    // Copying a record copies its nums handle, not the list's elements.
    pin shared <- report.readings
    push(shared, 28)
    shared[0] <- 19
    assert(size(original.readings) == 7, "list handles must share mutations")
    assert(original.readings[0] == 19, "indexed writes must reach shared handles")

    say text_repeat("=", 44)
    say "PUNPUN / SENSOR REPORT"
    say report.sensor.name + " @ " + report.sensor.location
    say "Note: " + slice(note, 0, minimum(len(note), 120))
    say "Samples: " + text(size(report.readings))
    say "Budget: " + text(report.budget_ms) + " ms"
    say "Range: " + text(stats_min(shared)) + ".." + text(stats_max(shared)) + " ms"
    print("Mean (ms): ")
    say stats_mean(shared)

    pin ordered <- stats_sorted(shared)
    say "Sorted samples (ms):"
    each i from 0 until size(ordered):
        say ordered[i]
    done

    keep over_budget <- 0
    each i from 0 until size(shared):
        when shared[i] <= report.budget_ms:
            next
        done
        over_budget <- over_budget + 1
        say "Slow sample #" + text(i) + ": " + text(shared[i]) + " ms"
    done
    when over_budget == 0:
        say "All samples are within budget."
    otherwise:
        say text(over_budget) + " sample(s) exceed the budget."
    done
    say text_repeat("=", 44)
done
