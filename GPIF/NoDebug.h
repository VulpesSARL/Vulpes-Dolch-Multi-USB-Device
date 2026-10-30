#ifndef NODEBUG_H
#define NODEBUG_H

/* Release builds: no debug output. The Host and Client makefiles force this into every source file (-include) for CYCONFOPT=fx3_release unless FIRMWARE_DEBUG=1.
   Code that only exists for the messages can be wrapped in #ifndef FIRMWARE_NO_DEBUG ... #endif, the way one would use #if DEBUG.

   cyu3system.h is included first so the prototype of CyU3PDebugPrint is already read when the macro is defined (a function-like macro
   defined earlier would also swallow the prototype). Its include guard keeps the sources' own #include from reading it again.

   The arguments are NOT evaluated: a call with a side effect must not sit inside a CyU3PDebugPrint argument list. */
#include "cyu3types.h"
#include "cyu3system.h"

#define FIRMWARE_NO_DEBUG                   1
#define CyU3PDebugPrint(Priority, ...)      ((void)0)

#endif
