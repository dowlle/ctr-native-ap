#ifndef NATIVE_XDELTA_PICK_H
#define NATIVE_XDELTA_PICK_H
/* The Track Manager's "Import track files" file dialog (SDL3, asynchronous).
   Start shows the system dialog; Poll returns -1 idle, 0 open, 1 picked
   (path copied), 2 cancelled or no dialog available. Main thread only. */
#include <stddef.h>
int XdeltaPick_Start(void);
int XdeltaPick_Poll(char *path, size_t size);
#endif
