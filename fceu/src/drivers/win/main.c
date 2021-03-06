/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "common.h"

// I like hacks.
#define uint8 __UNO492032
#include <winsock.h>
#include <ddraw.h>
#undef LPCWAVEFORMATEX
#include <dsound.h>
#include <dinput.h>
#include <dir.h>
#include <commctrl.h>
#include <shlobj.h>     // For directories configuration dialog.
#undef uint8

#include "input.h"
#include "netplay.h"
#include "joystick.h"
#include "keyboard.h"
#include "cheat.h"
#include "debug.h"
#include "../common/args.h"

#include "memwatch.h"
#include "basicbot.h"
#include "../../fceulua.h"

#define VNSCLIP  ((eoptions&EO_CLIPSIDES)?8:0)
#define VNSWID   ((eoptions&EO_CLIPSIDES)?240:256)

uint8 *xbsave=NULL;
int eoptions=EO_BGRUN | EO_FORCEISCALE;

void ResetVideo(void);
void ShowCursorAbs(int w);
void HideFWindow(int h);
void FixWXY(int pref);
int SetMainWindowStuff(void);
int GetClientAbsRect(LPRECT lpRect);
void UpdateFCEUWindow(void);

const char* MovieToLoad;

HWND hAppWnd=0;
HINSTANCE fceu_hInstance;

HRESULT  ddrval;

FCEUGI *GI=0;

// cheats, misc, nonvol, states, snaps, ..., base
static char *DOvers[10]={0,0,0,0,0,0,0,0,0,0};
static char *defaultds[9]={"cheats","movie","sav","fcs","snaps","","memwatch","basicbot","dump"};

static char TempArray[2048];
static char BaseDirectory[2048];

void SetDirs(void)
{
 int x;
 static int jlist[10]=
  {FCEUIOD_CHEATS,FCEUIOD_MISC,FCEUIOD_NV,FCEUIOD_STATE,FCEUIOD_SNAPS, FCEUMKF_FDSROM, FCEUIOD_MEMW, FCEUIOD_BBOT, FCEUIOD_DUMP, FCEUIOD__COUNT};

 FCEUI_SetSnapName(eoptions&EO_SNAPNAME);

 for(x=0;x<9;x++)
 {
  FCEUI_SetDirOverride(jlist[x], DOvers[x]);  
 }
 if(DOvers[9])
  FCEUI_SetBaseDirectory(DOvers[9]);
 else
  FCEUI_SetBaseDirectory(BaseDirectory);
}
/* Remove empty, unused directories. */
void RemoveDirs(void)
{
 int x;

 for(x=0;x<9;x++)
  if(!DOvers[x] && defaultds[x] != "")
  {
   sprintf(TempArray,"%s\\%s",DOvers[9]?DOvers[9]:BaseDirectory,defaultds[x]);
   RemoveDirectory(TempArray);
  }
}

void CreateDirs(void)
{
 int x;

 for(x=0;x<9;x++)
  if(!DOvers[x] && defaultds[x] != "")
  {
   sprintf(TempArray,"%s\\%s",DOvers[9]?DOvers[9]:BaseDirectory,defaultds[x]);
   CreateDirectory(TempArray,0);
  }
}

static char *gfsdir=0;
void GetBaseDirectory(void)
{
 int x;
 BaseDirectory[0]=0;
 GetModuleFileName(0,(LPTSTR)BaseDirectory,2047);

 for(x=strlen(BaseDirectory);x>=0;x--)
 {
  if(BaseDirectory[x]=='\\' || BaseDirectory[x]=='/')
   {BaseDirectory[x]=0;break;}
 }
}

static int exiting=0;
static volatile int moocow = 0;
int BlockingCheck(void)
{
  MSG msg;
  moocow = 1;
  while( PeekMessage( &msg, 0, 0, 0, PM_NOREMOVE ) ) {
     if( GetMessage( &msg, 0,  0, 0)>0 )
     {
     TranslateMessage(&msg);
     DispatchMessage(&msg);
     }
   }
 moocow = 0;
 if(exiting) return(0);
 return(1);
}

/* Some timing-related variables (now ignored). */
static int maxconbskip = 32;             /* Maximum consecutive blit skips. */
static int ffbskip = 32;              /* Blit skips per blit when FF-ing */

static int moviereadonly=1;

static int fullscreen=0;
static int soundflush=0;
static int genie=0;
static int palyo=0;
static int status_icon=1;
static int windowedfailed;
static double saspectw=1, saspecth=1;
static double winsizemulx=1, winsizemuly=1;
static int winwidth,winheight;
static int ismaximized = 0;

static volatile int nofocus=0;
static volatile int userpause=0;

#define SO_FORCE8BIT  1
#define SO_SECONDARY  2
#define SO_GFOCUS     4
#define SO_D16VOL     8
#define SO_MUTEFA     16
#define SO_OLDUP      32

#define GOO_DISABLESS   1       /* Disable screen saver when game is loaded. */
#define GOO_CONFIRMEXIT 2       /* Confirmation before exiting. */
#define GOO_POWERRESET  4       /* Confirm on power/reset. */

static uint32 goptions = GOO_DISABLESS;

static int soundrate=44100;
static int soundbuftime=50;
/*static*/ int soundoptions=SO_SECONDARY|SO_GFOCUS;
static int soundvolume=100;
static int soundquality=0;
extern int autoHoldKey, autoHoldClearKey;
extern int frame_display, input_display;

static unsigned int srendline,erendline;
static unsigned int srendlinen=8;
static unsigned int erendlinen=231;
static unsigned int srendlinep=0;
static unsigned int erendlinep=239;


static unsigned int totallines;

static void FixFL(void)
{
 FCEUI_GetCurrentVidSystem(&srendline,&erendline);
 totallines=erendline-srendline+1;
}

static void UpdateRendBounds(void)
{ 
 FCEUI_SetRenderedLines(srendlinen,erendlinen,srendlinep,erendlinep);
 FixFL(); 
}

static uint8 cpalette[192];
static int vmod = 0;
int soundo=1;
static int ntsccol=0,ntsctint,ntschue;

void FCEUD_PrintError(char *s)
{
 AddLogText(s,1);
 if(fullscreen) ShowCursorAbs(1);
 MessageBox(0,s,"FCE Ultra Error",MB_ICONERROR|MB_OK|MB_SETFOREGROUND|MB_TOPMOST);
 if(fullscreen)ShowCursorAbs(0);
}

void ShowAboutBox(void)
{
 sprintf(TempArray,"FCE Ultra "FCEU_VERSION"\n\nhttp://tasvideos.org/forum/t/1020\n\n"__TIME__"\n"__DATE__"\n""gcc "__VERSION__);
 MessageBox(hAppWnd,TempArray,"About FCE Ultra",MB_OK);
}


void DoFCEUExit(void)
{
 /* Wolfenstein 3D had cute exit messages. */
 char *emsg[4]={"Are you sure you want to leave?  I'll become lonely!",
                "If you exit, I'll... EAT YOUR MOUSE.",
                "You can never really exit, you know.",
                "E-x-i-t?"
               };

 if(exiting)    /* Eh, oops.  I'll need to try to fix this later. */
  return;

 StopSound();
 if(goptions & GOO_CONFIRMEXIT)
  if(IDYES != MessageBox(hAppWnd,emsg[rand()&3],"Exit FCE Ultra?",MB_ICONQUESTION|MB_YESNO))
   return;

 FCEUI_StopMovie();
 FCEUD_AviStop();

 exiting=1;
 if(GI)
 {
  FCEUI_CloseGame();
  GI=0;
 }
}

void DoPriority(void)
{
 if(eoptions&EO_HIGHPRIO)
 {
  if(!SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST))
  {
   AddLogText("Error setting thread priority to THREAD_PRIORITY_HIGHEST.",1);
  }
 }
 else
  if(!SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL))
  {
   AddLogText("Error setting thread priority to THREAD_PRIORITY_NORMAL.",1);
  }
}

static int changerecursive=0;

#include "throttle.c"

#include "sound.c"
#include "video.c"
#include "window.c"
#include "config.c"
#include "args.c"

int DriverInitialize(void)
{
  if(soundo)
   soundo=InitSound();

  SetVideoMode(fullscreen);
  InitInputStuff();             /* Initialize DInput interfaces. */
  return 1;
}

static void DriverKill(void)
{ 
 sprintf(TempArray,"%s/fceu98.cfg",BaseDirectory);
 SaveConfig(TempArray);
 DestroyInput();
 ResetVideo();
 if(soundo) TrashSound();
 CloseWave();
 ByebyeWindow();
}

void FCEUD_Update(uint8 *XBuf, int32 *Buffer, int Count);

void ApplyDefaultCommandMapping(void);

int main(int argc,char *argv[])
{
	char *t;

	if(timeBeginPeriod(1)!=TIMERR_NOERROR)
	{
	AddLogText("Error setting timer granularity to 1ms.",1);
	}

	InitCommonControls();

	if(!FCEUI_Initialize())
		goto doexito;
	ApplyDefaultCommandMapping();

	srand(GetTickCount());        // rand() is used for some GUI sillyness.

	fceu_hInstance=GetModuleHandle(0);

	GetBaseDirectory();

	sprintf(TempArray,"%s\\fceu98.cfg",BaseDirectory);
	LoadConfig(TempArray);

	t=ParseArgies(argc,argv);
	/* Bleh, need to find a better place for this. */
	{
        palyo&=1;
        FCEUI_SetVidSystem(palyo);
        genie&=1;
        FCEUI_SetGameGenie(genie);
        fullscreen&=1;
        soundo&=1;
        FCEUI_SetSoundVolume(soundvolume);
		FCEUI_SetSoundQuality(soundquality);
	}
	ParseGIInput(NULL);      /* Since a game doesn't have to be
                     loaded before the GUI can be used, make
                     sure the temporary input type variables
                     are set.
                  */
	CreateDirs();
	SetDirs();

	DoVideoConfigFix();
	DoTimingConfigFix();

	if(eoptions&EO_CPALETTE)
		FCEUI_SetPaletteArray(cpalette);

	if(!t) fullscreen=0;

	CreateMainWindow();

	if(!InitDInput())
		goto doexito;

	if(!DriverInitialize())
		goto doexito;
 
	InitSpeedThrottle();
	UpdateMenu();

	FCEUAVI_ShowMovieAVIMessage(EnableStopMessageInAvi);

	if(t)
		ALoad(t);
	else if(eoptions&EO_FOAFTERSTART)
		LoadNewGamey(hAppWnd, 0);

 if(MovieToLoad)
 {
  FCEUI_LoadMovie(MovieToLoad, 1, 0);
  MovieToLoad = NULL;
 }

doloopy:
	UpdateFCEUWindow();  
	if(GI)
	{
		while(GI)
		{
	        uint8 *gfx=0;
			int32 *sound=0;
			int32 ssize=0;
			FCEUI_Emulate(&gfx, &sound, &ssize, 0);
			xbsave = gfx;
			FCEUD_Update(gfx, sound, ssize);
			static int stopCount=0;
			if(FCEUI_EmulationPaused() & 1)
			{
				stopCount++;
				if(stopCount > 8)
				{
					StopSound();
					stopCount = 0;
				}
				//if in bot mode, don't idle.  eat the CPU up :)
				if(!FCEU_BotMode() && !FCEU_LuaSpeed())
				{
					int notAlternateThrottle = !(soundoptions&SO_OLDUP) && soundo && ((NoWaiting&1)?(256*16):fps_scale) >= 64;
					if(notAlternateThrottle)
						Sleep(5); // HACK to fix 100% CPU usage that happens sporadically when paused in background - also, this affects the repeat rate of frame advance and its sound quality
					else
						Sleep(1); // lesser, so frame advance repeat doesn't take too long with the alternate throttling
				}
				else if(stopCount == 1)
				{
					Sleep(0);
				}
			}
			else
			{
				stopCount=0;
			}
		}
		xbsave = NULL;
		RedrawWindow(hAppWnd,0,0,RDW_ERASE|RDW_INVALIDATE);
		StopSound();
	}
	Sleep(50);
	if(!exiting)
		goto doloopy;

	doexito:
	DriverKill();
	timeEndPeriod(1);
	FCEUI_Kill();
	return(0);
}

void FCEUD_Update(uint8 *XBuf, int32 *Buffer, int Count)
{
	static int skipcount = 0;
	int temp_fps_scale=(NoWaiting&1)?(256*16):fps_scale;
	int maxskip = (temp_fps_scale<=256) ? 0 : temp_fps_scale>>8;

	if (FCEU_LuaFrameSkip() < 0) skipcount = 32767;
	if (FCEU_LuaFrameSkip() > 0) skipcount = -1;

	int ocount = Count;
	// apply frame scaling to Count
	Count = (Count<<8)/temp_fps_scale;

	//Disable sound and throttling for BotMode--we want max speed!
	if(FCEU_BotMode())
	{
		if(XBuf && (skipcount >= 64))
		{
			skipcount = 0;
			FCEUD_BlitScreen(XBuf);
		}
		else
		{
			skipcount++;
		}
		UpdateFCEUWindow();
		FCEUD_UpdateInput();
		return;
	}

	if(!(soundoptions&SO_OLDUP) && soundo && temp_fps_scale >= 64)
	{
		// sounds better with FPS scaling, and is much less code than the other version...

		int32 writeSize = GetWriteSound();
		int32 writeCount = Count;
/*
		// prevents delay when exiting fast-forward
		if((NoWaiting&1) && writeCount>writeSize)
			writeCount=writeSize;
*/

		if(Buffer && (writeCount))
			FCEUD_WriteSoundData(Buffer,temp_fps_scale,MAX(writeSize,writeCount));

		if(XBuf && (skipcount >= maxskip))
		{
			skipcount = 0;
			FCEUD_BlitScreen(XBuf);
		}
		else
			skipcount++;

		UpdateFCEUWindow();
		FCEUD_UpdateInput();
	}
	else
	{
		// I don't understand this code, so I kept it around as an option ("old sound update")
		// in case it's doing something clever and necessary that I can't fathom
		// (oops, also it seems to be important for speeds <25% so it's always used then)

		const int soundScale = !(soundoptions&SO_OLDUP) ? temp_fps_scale : 256;

		if(Count)
		{
			int32 can=GetWriteSound();
			static int uflow=0;
			int32 tmpcan;
			extern int FCEUDnetplay;

			// don't underflow when scaling fps
			if(can >= GetMaxSound() && fps_scale<=256) uflow=1;	// Go into massive underflow mode. 

			if(can > Count) can=Count;
			else uflow=0;

			FCEUD_WriteSoundData(Buffer,soundScale,can);

			tmpcan = GetWriteSound();
			// don't underflow when scaling fps
			if(fps_scale>256 || ((tmpcan < Count*0.90) && !uflow) || (skipcount >= maxskip))
			{
				if(XBuf && (skipcount >= maxskip))
				{
					skipcount = 0;
					FCEUD_BlitScreen(XBuf);
				}
				else
				{
					skipcount++;
					//FCEU_printf("Skipped0");
					//	FCEU_PrintError("Skipped0");
				}
				Buffer+=can;
				Count-=can;
				if(Count)
				{
					if(NoWaiting)
					{
						can=GetWriteSound(); 
						if(Count>can) Count=can;
						FCEUD_WriteSoundData(Buffer,soundScale,Count);
					}
					else
					{
						int cnum=0;
						extern int silencer;
						while(Count>0)
						{
							FCEUD_WriteSoundData(Buffer,soundScale,(Count<ocount) ? Count : ocount);
							if(!(soundoptions&SO_OLDUP))
							{
								cnum++;
								if(cnum>2)
									silencer=1;
							}
							Count -= ocount;
							// prevent long updates from interfering with gui responsiveness:
							UpdateFCEUWindow();
							FCEUD_UpdateInput();
						}
						silencer=0;
					}
				}
			}
			else
			{
				skipcount++;
				//FCEU_printf("Skipped");
#ifdef NETWORK
				if(!NoWaiting && FCEUDnetplay && (uflow || tmpcan >= (Count * 0.90)))
				{
					if(Count > tmpcan) Count=tmpcan;
					while(tmpcan > 0)
					{
						//printf("Overwrite: %d\n", (Count <= tmpcan)?Count : tmpcan);
						FCEUD_WriteSoundData(Buffer,soundScale,(Count <= tmpcan)?Count : tmpcan);
						tmpcan -= Count;
					}
				}
#endif
			}
		}
		else
		{
			/* This complex statement deserves some explanation.
			Make sure this special speed throttling hasn't been disabled by the user
			first. Second, we don't want to throttle the speed if the fast-forward
			button is pressed down(or during certain network play conditions).

			Now, if we're at this point, we'll throttle speed if sound is disabled.
			Otherwise, it gets a bit more complicated.  We'll throttle speed if focus
			to FCE Ultra has been lost and we're writing to the primary sound buffer
			because our sound code won't block.  Blocking does seem to work when
			writing to a secondary buffer, so we won't throttle when a secondary
			buffer is used.
			*/
			int skipthis = 0;

			if(!(eoptions&EO_NOTHROTTLE) || fps_scale != 256)
				if(!NoWaiting)
					if(!soundo || (soundo && nofocus && !(soundoptions&SO_SECONDARY)) || FCEUI_EmulationPaused() )
						skipthis = SpeedThrottle();

			if(XBuf)
			{
				if((!skipthis && !NoWaiting) || (skipcount >= maxskip))
				{
					FCEUD_BlitScreen(XBuf);
					skipcount = 0;
				}
				else
				{
					skipcount++;
				}
			}
		}
		UpdateFCEUWindow();
		FCEUD_UpdateInput();

	} // end of !(old sound code) block

	// Memory Tools windows update
	if (!FCEUI_EmulationPaused() && MemToolsUpdateSpeed == 999)
	{
		UpdateMemWatch();
		UpdateDebugger();
		UpdateCheatList();
	}
	else if(!FCEUI_EmulationPaused() && !(((FCEUD_GetTime()/10000)*10)%MemToolsUpdateSpeed))
	{
		UpdateMemWatch();
		UpdateDebugger();
		UpdateCheatList();
	}
}

/*
void FCEUD_Update(uint8 *XBuf, int32 *Buffer, int Count)
{
 FCEUD_BlitScreen(XBuf);
 if(Count && soundo)
  FCEUD_WriteSoundData(Buffer,Count);
 FCEUD_UpdateInput();
}
*/

static void FCEUD_MakePathDirs(const char *fname)
{
	char path[MAX_PATH];
	const char* div=fname;
	do {
		const char* fptr=strchr(div,'\\');
		if(!fptr) fptr=strchr(div,'/');
		if(!fptr) break;
		int off=fptr-fname;
		strncpy(path,fname,off);
		path[off]='\0';
		mkdir(path);
		div=fptr+1;
		while(div[0]=='\\'||div[0]=='/') div++;
	} while(1);
}

FILE *FCEUD_UTF8fopen(const char *n, const char *m)
{
  if(strchr(m,'w')||strchr(m,'+'))
	  FCEUD_MakePathDirs(n);

  return(fopen(n,m));
}

int FCEUD_ShowStatusIcon(void)
{
	return status_icon;
}

void FCEUD_ToggleStatusIcon(void)
{
	status_icon=!status_icon;
	UpdateMenu();
}

void FCEUD_ResetStopframe(int opt)
{
 if (EnablePauseAfterPlayback) FCEUMOV_ResetStopframe(opt);
}
