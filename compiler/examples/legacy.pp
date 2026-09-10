# The same program in the 0.4 migration dialect. ppc accepts both, and they
# lower to the same AST.
craft double(value as int) gives int:
    give value * 2
done

launch:
    say "hello from ppc"
    say double(21)
done
