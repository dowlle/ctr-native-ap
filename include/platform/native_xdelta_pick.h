#ifndef NATIVE_XDELTA_PICK_H
#define NATIVE_XDELTA_PICK_H
/* The "Import track files" file dialog (SDL3, asynchronous) and window drops.
   Start shows the system dialog; Poll returns -1 idle, 0 open, 1 picked
   (path copied), 2 cancelled or no dialog available. Main thread only. */
#include <stddef.h>
int XdeltaPick_Start(void);
int XdeltaPick_Poll(char *path, size_t size);
/* Stop waiting for an open dialog (it may never answer in Game Mode). A late
   answer is dropped and Start works again. */
void XdeltaPick_Abandon(void);
/* A file dropped on the client window (SDL_EVENT_DROP_FILE, main thread).
   The newest drop wins. A name that does not end in .xdelta or .vcdiff is
   remembered as refused, so the import screen can say why nothing happened. */
void XdeltaPick_Drop(const char *path);
/* 1: a patch was dropped (path copied, drop consumed); 2: a file that is not
   a patch was dropped (consumed); 0: nothing waiting. */
int XdeltaPick_TakeDrop(char *path, size_t size);
#endif
