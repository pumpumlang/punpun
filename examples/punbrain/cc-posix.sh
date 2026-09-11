#!/bin/sh
# PunBrain-local PPC host compiler shim. Keeps POSIX feature exposure local to
# this example instead of changing PunPun's global C backend flags.
exec cc -D_POSIX_C_SOURCE=200809L "$@"
