// Arithmetic-heavy with checked operations in the inner loop.
launch {
    let size = 60;
    let a = numbers();
    let b = numbers();
    for i in 0..size * size { push(a, i % 7); push(b, i % 5); }
    let mut checksum = 0;
    for row in 0..size {
        for col in 0..size {
            let mut sum = 0;
            for k in 0..size {
                sum = sum + a[row * size + k] * b[k * size + col];
            }
            checksum = checksum + sum;
        }
    }
    say(checksum);
}
