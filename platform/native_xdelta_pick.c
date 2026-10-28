/* See include/platform/native_xdelta_pick.h. Included by main.c next to the
   disc picker, so it shares that unit's SDL headers. On Steam Deck Game Mode,
   where a desktop dialog may not show, the Track Manager also lists
   assets/tracks/import/. */
#include <platform/native_xdelta_pick.h>
#include <stdint.h>

static SDL_Mutex *s_xdeltaPickMutex;
static int s_xdeltaPickState = -1;
static char s_xdeltaPickPath[4096];

static unsigned s_xdeltaPickGeneration;

static void SDLCALL XdeltaPick_Callback(void *userdata, const char *const *filelist, int filter)
{
	(void)filter;
	SDL_LockMutex(s_xdeltaPickMutex);
	if ((unsigned)(uintptr_t)userdata != s_xdeltaPickGeneration || s_xdeltaPickState != 0)
	{
		SDL_UnlockMutex(s_xdeltaPickMutex);
		return; /* An abandoned dialog. */
	}
	if (filelist != NULL && filelist[0] != NULL && strlen(filelist[0]) < sizeof s_xdeltaPickPath)
	{
		strcpy(s_xdeltaPickPath, filelist[0]);
		s_xdeltaPickState = 1;
	}
	else
		s_xdeltaPickState = 2;
	SDL_UnlockMutex(s_xdeltaPickMutex);
}

int XdeltaPick_Start(void)
{
	static const SDL_DialogFileFilter filters[] = {
	    {"xdelta patches", "xdelta;vcdiff"},
	};
	if (s_xdeltaPickMutex == NULL && (s_xdeltaPickMutex = SDL_CreateMutex()) == NULL)
		return 0;
	SDL_LockMutex(s_xdeltaPickMutex);
	if (s_xdeltaPickState == 0)
	{
		SDL_UnlockMutex(s_xdeltaPickMutex);
		return 0;
	}
	s_xdeltaPickState = 0;
	s_xdeltaPickPath[0] = 0;
	s_xdeltaPickGeneration++;
	{
		void *generation = (void *)(uintptr_t)s_xdeltaPickGeneration;
		SDL_UnlockMutex(s_xdeltaPickMutex);
		SDL_ShowOpenFileDialog(XdeltaPick_Callback, generation, NULL, filters, 1, NULL, false);
	}
	return 1;
}

int XdeltaPick_Poll(char *path, size_t size)
{
	int state;
	if (s_xdeltaPickMutex == NULL)
		return -1;
	SDL_LockMutex(s_xdeltaPickMutex);
	state = s_xdeltaPickState;
	if (state == 1 && path != NULL && size > strlen(s_xdeltaPickPath))
		strcpy(path, s_xdeltaPickPath);
	if (state == 1 || state == 2)
		s_xdeltaPickState = -1;
	SDL_UnlockMutex(s_xdeltaPickMutex);
	return state;
}

void XdeltaPick_Abandon(void)
{
	if (s_xdeltaPickMutex == NULL)
		return;
	SDL_LockMutex(s_xdeltaPickMutex);
	if (s_xdeltaPickState == 0)
	{
		s_xdeltaPickGeneration++;
		s_xdeltaPickState = -1;
	}
	SDL_UnlockMutex(s_xdeltaPickMutex);
}
