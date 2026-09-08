// Convenience algorithms for the built-in nums list type.

fn nums_copy(values: nums) -> nums {
    let result = [];
    for i in 0..size(values) {
        push(result, values[i]);
    }
    return result;
}

fn nums_contains(values: nums, needle: i64) -> bool {
    for i in 0..size(values) {
        if values[i] == needle {
            return true;
        }
    }
    return false;
}

fn nums_index_of(values: nums, needle: i64) -> i64 {
    for i in 0..size(values) {
        if values[i] == needle {
            return i;
        }
    }
    return -1;
}

fn nums_count(values: nums, needle: i64) -> i64 {
    let mut count = 0;
    for i in 0..size(values) {
        if values[i] == needle {
            count = count + 1;
        }
    }
    return count;
}

fn nums_reverse(values: nums) -> nums {
    let result = [];
    let mut i = size(values);
    while i > 0 {
        i = i - 1;
        push(result, values[i]);
    }
    return result;
}

fn nums_equal(left: nums, right: nums) -> bool {
    if size(left) != size(right) {
        return false;
    }
    for i in 0..size(left) {
        if left[i] != right[i] {
            return false;
        }
    }
    return true;
}
