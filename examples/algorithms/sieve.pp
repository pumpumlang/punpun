craft sieve_count(flags as nums, limit as int) gives int:
    each i from 2 until limit + 1:
        flags[i] <- 1
    done

    keep factor <- 2
    whilst factor <= limit / factor:
        when flags[factor] == 0:
            factor <- factor + 1
            next
        done
        keep multiple <- factor * factor
        whilst multiple <= limit:
            flags[multiple] <- 0
            multiple <- multiple + factor
        done
        factor <- factor + 1
    done

    keep count <- 0
    each i from 2 until limit + 1:
        when flags[i] == 1:
            count <- count + 1
        done
    done
    give count
done

launch:
    assert(arg_count() <= 2, "usage: sieve [limit [rounds]]")
    keep limit <- 200000
    keep rounds <- 5
    when arg_count() >= 1:
        limit <- parse_int(arg(0))
    done
    when arg_count() == 2:
        rounds <- parse_int(arg(1))
    done
    assert(limit >= 2 and limit <= 5000000, "limit must be between 2 and 5000000")
    assert(rounds >= 1 and rounds <= 100, "rounds must be between 1 and 100")

    // Allocate once outside the timer; every round resets and reuses this list.
    pin flags <- numbers()
    each i from 0 until limit + 1:
        push(flags, 0)
    done
    keep count <- 0
    keep checksum <- 0
    pin started <- clock_ms()
    each round from 0 until rounds:
        count <- sieve_count(flags, limit)
        checksum <- checksum + count
    done
    pin elapsed <- clock_ms() - started

    when limit == 200000:
        assert(count == 17984, "unexpected prime count for the default limit")
    done
    say "Primes <= " + text(limit) + ": " + text(count)
    say "Rounds: " + text(rounds) + ", checksum: " + text(checksum)
    say "Sieve/reset/count time (ms): " + text(elapsed)
done
