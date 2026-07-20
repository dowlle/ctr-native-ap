#ifndef PLATFORM_H
#define PLATFORM_H

struct PlatformMempackArena
{
	void *base;
	void *start;
	void *endOfMemory;
	int size;
	int backingSize;
};

void Platform_Init(const char *title, int width, int height);
void Platform_Shutdown(void);
void Platform_InitScratchpad(void);
const struct PlatformMempackArena *Platform_InitMempackArena(void);
const struct PlatformMempackArena *Platform_GetMempackArena(void);
void Platform_BeginFrame(void);
int Platform_BeginScene(void);
void Platform_EndScene(void);
void Platform_EndFrame(void);
void Platform_PresentVRAMDisplay(void);
void Platform_PinVRAMDisplayFrames(int frameCount);
void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount);
int Platform_GetVBlankCount(void);
void Platform_WaitUntilVBlank(int targetVBlank);
void Platform_PollHostEvents(void);
int Platform_PollInput(void);

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);

// In-game text entry (host keyboard). While a session is active the platform
// event loop routes typing into the caller's buffer instead of the game: printable
// ASCII appends, Backspace deletes, Enter commits, Escape cancels -- and those keys
// are consumed so gameplay / menu input never sees them. Used by the in-game
// Archipelago connection manager (game/230/MM_ConfigMenu.c). buf holds the current
// value on entry (editing continues from it) and is null-terminated throughout.
void NativeText_Begin(char *buf, int cap);
void NativeText_End(void);
int NativeText_Active(void); // 1 while capturing
int NativeText_Result(void); // 0 = active/none, 1 = committed (Enter), 2 = cancelled (Escape)
void NativeText_Resolve(int result); // resolve from outside the keyboard (1 commit, 2 cancel)
#endif

#endif
