craft double(value as int) gives int:
    give value * 2
done

launch:
    pin answer <- double(21)
    say answer
done
