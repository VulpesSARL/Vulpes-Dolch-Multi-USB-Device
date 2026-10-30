/* The guard (watchdog, fatal error reset, USB re-attach) is shared source (..GPIFGuardCore.c); this wrapper compiles it into this project. */
#define GUARD_FATAL_WAIT_FOR_RESET 1
#include "GuardCore.h"
#include "GuardCore.c"
