craft fibonacci(n as int) gives int:
    when n <= 1:
        give n
    done
    give fibonacci(n - 1) + fibonacci(n - 2)
done

craft sum_to(limit as int) gives int:
    keep current <- 1
    keep total as int <- 0
    whilst current <= limit:
        total <- total + current
        current <- current + 1
    done
    give total
done
