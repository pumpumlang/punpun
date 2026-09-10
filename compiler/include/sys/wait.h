#ifndef PPC_COMPAT_SYS_WAIT_H
#define PPC_COMPAT_SYS_WAIT_H

/*
 * MinGW does not provide POSIX <sys/wait.h>. PPC's Windows process handling
 * lives in support/host.cpp and uses _spawnvp, so no waitpid declarations are
 * needed on that target. Keep the existing include harmless on Windows while
 * forwarding to the real system header everywhere else.
 */
#if !defined(_WIN32) && !defined(_WIN64)
#  include_next <sys/wait.h>
#endif

#endif
