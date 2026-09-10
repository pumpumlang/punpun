craft classify(n as int) gives str:
    when n < 0:
        give "negative"
    otherwise:
        when n == 0:
            give "zero"
        done
        give "positive"
    done
done

launch:
    say classify(-5)
    say classify(0)
    say classify(9)
    keep total <- 0
    each i from 1 until 6:
        total <- total + i
    done
    say total
    keep n <- 0
    whilst n < 100:
        n <- n + 7
        when n > 50:
            leave
        done
    done
    say n
done
