bring std.system

launch:
    say "PUNPUN_IDE_DOCTOR_V1"
    say system_platform()
    say system_current_dir()
    say system_env_or("PATH", "")
    say system_env_or("PPC_RUNTIME", "")
    say system_env_or("PPC_STDLIB", "")
    say system_env_or("CC", "")
    say system_env_or("CXX", "")
done
