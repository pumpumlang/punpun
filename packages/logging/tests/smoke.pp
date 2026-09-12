import src.main
launch {let log=log_named("smoke",log_error_level());assert(log.enabled(log_error_level()),"level");log_info("package");say("logging-ok");}
