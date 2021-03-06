#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

#ifdef __linux
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif



#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "types.h"
#include "general.h"
#include "memory.h"
#include "fceu.h"
#include "video.h"
#include "state.h"
#include "movie.h"
#include "driver.h"
#include "cheat.h"
#ifdef WIN32
#include "drivers/win/common.h"
#endif
#include "fceulua.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

static lua_State *LUA;

// Are we running any code right now?
static char *luaScriptName = NULL;

// Are we running any code right now?
static int luaRunning = FALSE;

// True at the frame boundary, false otherwise.
static int frameBoundary = FALSE;


// The execution speed we're running at.
static enum {SPEED_NORMAL, SPEED_NOTHROTTLE, SPEED_TURBO, SPEED_MAXIMUM} speedmode = SPEED_NORMAL;

// Rerecord count skip mode
static int skipRerecords = FALSE;

// Used by the registry to find our functions
static const char *frameAdvanceThread = "FCEU.FrameAdvance";
static const char *memoryWatchTable = "FCEU.Memory";
static const char *memoryValueTable = "FCEU.MemValues";
static const char *guiCallbackTable = "FCEU.GUI";

// True if there's a thread waiting to run after a run of frame-advance.
static int frameAdvanceWaiting = FALSE;

// We save our pause status in the case of a natural death.
static int wasPaused = FALSE;

// Transparency strength. 0=opaque, 4=so transparent it's invisible
// TODO: intermediate values would be nice...
static int transparency;

// Our joypads.
static uint8 lua_joypads[4];
static uint8 lua_joypads_used;


static enum { GUI_USED_SINCE_LAST_DISPLAY, GUI_USED_SINCE_LAST_FRAME, GUI_CLEAR } gui_used = GUI_CLEAR;
static uint8 *gui_data = NULL;
static int gui_saw_current_palette = FALSE;

// See drawing.h for comments about FCEU's palette. We interpret zero as transparent.
enum
{
	GUI_COLOUR_CLEAR, GUI_COLOUR_WHITE,
	GUI_COLOUR_BLACK, GUI_COLOUR_GREY,
	GUI_COLOUR_RED,   GUI_COLOUR_GREEN,
	GUI_COLOUR_BLUE
};

// Protects Lua calls from going nuts.
// We set this to a big number like 1000 and decrement it
// over time. The script gets knifed once this reaches zero.
static int numTries;


// Look in fceu.h for macros named like JOY_UP to determine the order.
static const char *button_mappings[] = {
	"A", "B", "select", "start", "up", "down", "left", "right"
};


/**
 * Resets emulator speed / pause states after script exit.
 */
static void FCEU_LuaOnStop() {
	luaRunning = FALSE;
	lua_joypads_used = 0;
	gui_used = GUI_CLEAR;
	if (wasPaused && !FCEUI_EmulationPaused())
		FCEUI_ToggleEmulationPause();
	FCEUD_SetEmulationSpeed(EMUSPEED_NORMAL);
	FCEUD_UpdateLuaMenus();
}


/**
 * Asks Lua if it wants control of the emulator's speed.
 * Returns 0 if no, 1 if yes. If yes, caller should also
 * consult FCEU_LuaFrameSkip().
 */
int FCEU_LuaSpeed() {
	if (!LUA || !luaRunning)
		return 0;

	//printf("%d\n", speedmode);

	switch (speedmode) {
	case SPEED_NOTHROTTLE:
	case SPEED_TURBO:
	case SPEED_MAXIMUM:
		return 1;
	case SPEED_NORMAL:
	default:
		return 0;
	}
}

/**
 * Asks Lua if it wants control whether this frame is skipped.
 * Returns 0 if no, 1 if frame should be skipped, -1 if it should not be.
 */
int FCEU_LuaFrameSkip() {
	if (!LUA || !luaRunning)
		return 0;

	switch (speedmode) {
	case SPEED_NORMAL:
		return 0;
	case SPEED_NOTHROTTLE:
		return -1;
	case SPEED_TURBO:
		return 0;
	case SPEED_MAXIMUM:
		return 1;
	}
}

/**
 * When code determines that a write has occurred
 * (not necessarily worth informing Lua), call this.
 *
 */
void FCEU_LuaWriteInform() {
	if (!LUA || !luaRunning) return;
	// Nuke the stack, just in case.
	lua_settop(LUA,0);

	lua_getfield(LUA, LUA_REGISTRYINDEX, memoryWatchTable);
	lua_pushnil(LUA);
	while (lua_next(LUA, 1) != 0)
	{
		unsigned int addr = luaL_checkinteger(LUA, 2);
		lua_Integer value;
		lua_getfield(LUA, LUA_REGISTRYINDEX, memoryValueTable);
		lua_pushvalue(LUA, 2);
		lua_gettable(LUA, 4);
		value = luaL_checkinteger(LUA, 5);
		if (FCEU_CheatGetByte(addr) != value)
		{
			// Value changed; update & invoke the Lua callback
			lua_pushinteger(LUA, addr);
			lua_pushinteger(LUA, FCEU_CheatGetByte(addr));
			lua_settable(LUA, 4);
			lua_pop(LUA, 2);

			numTries = 1000;
			int res = lua_pcall(LUA, 0, 0, 0);
			if (res) {
				const char *err = lua_tostring(LUA, -1);
				
#ifdef WIN32
				StopSound();
				MessageBox(hAppWnd, err, "Lua Engine", MB_OK);
#else
				fprintf(stderr, "Lua error: %s\n", err);
#endif
			}
		}
		lua_settop(LUA, 2);
	}
	lua_settop(LUA, 0);
}

///////////////////////////



// FCEU.speedmode(string mode)
//
//   Takes control of the emulation speed
//   of the system. Normal is normal speed (60fps, 50 for PAL),
//   nothrottle disables speed control but renders every frame,
//   turbo renders only a few frames in order to speed up emulation,
//   maximum renders no frames
//   TODO: better enforcement, done in the same way as basicbot...
static int fceu_speedmode(lua_State *L) {
	const char *mode = luaL_checkstring(L,1);
	
	if (strcasecmp(mode, "normal")==0) {
		speedmode = SPEED_NORMAL;
	} else if (strcasecmp(mode, "nothrottle")==0) {
		speedmode = SPEED_NOTHROTTLE;
	} else if (strcasecmp(mode, "turbo")==0) {
		speedmode = SPEED_TURBO;
	} else if (strcasecmp(mode, "maximum")==0) {
		speedmode = SPEED_MAXIMUM;
	} else
		luaL_error(L, "Invalid mode %s to FCEU.speedmode",mode);
	
	//printf("new speed mode:  %d\n", speedmode);
        if (speedmode == SPEED_NORMAL) FCEUD_SetEmulationSpeed(EMUSPEED_NORMAL);
        else FCEUD_SetEmulationSpeed(EMUSPEED_FASTEST);

	return 0;

}


// FCEU.frameadvance()
//
//  Executes a frame advance. Occurs by yielding the coroutine, then re-running
//  when we break out.
static int fceu_frameadvance(lua_State *L) {
	// We're going to sleep for a frame-advance. Take notes.

	if (frameAdvanceWaiting) 
		return luaL_error(L, "can't call FCEU.frameadvance() from here");

	frameAdvanceWaiting = TRUE;

	// Now we can yield to the main 
	return lua_yield(L, 0);


	// It's actually rather disappointing...
}


// FCEU.pause()
//
//  Pauses the emulator, function "waits" until the user unpauses.
//  This function MAY be called from a non-frame boundary, but the frame
//  finishes executing anwyays. In this case, the function returns immediately.
static int fceu_pause(lua_State *L) {
	
	if (!FCEUI_EmulationPaused())
		FCEUI_ToggleEmulationPause();
	speedmode = SPEED_NORMAL;

	// Return control if we're midway through a frame. We can't pause here.
	if (frameAdvanceWaiting) {
		return 0;
	}

	// If it's on a frame boundary, we also yield.	
	frameAdvanceWaiting = TRUE;
	return lua_yield(L, 0);
	
}



// FCEU.message(string msg)
//
//  Displays the given message on the screen.
static int fceu_message(lua_State *L) {

	const char *msg = luaL_checkstring(L,1);
	FCEU_DispMessage("%s", msg);
	
	return 0;

}


static int memory_readbyte(lua_State *L) {   lua_pushinteger(L, FCEU_CheatGetByte(luaL_checkinteger(L,1))); return 1; }
static int memory_writebyte(lua_State *L) {   FCEU_CheatSetByte(luaL_checkinteger(L,1), luaL_checkinteger(L,2)); return 0; }


// Not for the signed versions though
static int memory_readbytesigned(lua_State *L) {
	signed char c = (signed char) FCEU_CheatGetByte(luaL_checkinteger(L,1));
	lua_pushinteger(L, c);
	return 1;
}

// memory.register(int address, function func)
//
//  Calls the given function when the indicated memory address is
//  written to. No args are given to the function. The write has already
//  occurred, so the new address is readable.
static int memory_register(lua_State *L) {

	// Check args
	unsigned int addr = luaL_checkinteger(L, 1);
	if (lua_type(L,2) != LUA_TNIL && lua_type(L,2) != LUA_TFUNCTION)
		luaL_error(L, "function or nil expected in arg 2 to memory.register");
	
	
	// Check the address range
	if (addr > 0xffff)
		luaL_error(L, "arg 1 should be between 0x0000 and 0x0fff");

	// Commit it to the registery
	lua_getfield(L, LUA_REGISTRYINDEX, memoryWatchTable);
	lua_pushvalue(L,1);
	lua_pushvalue(L,2);
	lua_settable(L, -3);
	lua_getfield(L, LUA_REGISTRYINDEX, memoryValueTable);
	lua_pushvalue(L,1);
	if (lua_isnil(L,2)) lua_pushnil(L);
	else lua_pushinteger(L, FCEU_CheatGetByte(addr));
	lua_settable(L, -3);
	
	return 0;
}


// table joypad.read(int which = 1)
//
//  Reads the joypads as inputted by the user.
//  This is really the only way to get input to the system.
//  TODO: Don't read in *everything*...
static int joypad_read(lua_State *L) {

	// Reads the joypads as inputted by the user
	int which = luaL_checkinteger(L,1);
	
	if (which < 1 || which > 4) {
		luaL_error(L,"Invalid input port (valid range 1-2, specified %d)", which);
	}
	
	// Use the OS-specific code to do the reading.
	extern void FCEUD_UpdateInput(void);
	FCEUD_UpdateInput();
	extern SFORMAT FCEUCTRL_STATEINFO[];
	uint8 buttons = ((uint8 *) FCEUCTRL_STATEINFO[1].v)[which - 1];
	
	lua_newtable(L);
	
	int i;
	for (i = 0; i < 8; i++) {
		if (buttons & (1<<i)) {
			lua_pushinteger(L,1);
			lua_setfield(L, -2, button_mappings[i]);
		}
	}
	
	return 1;
}


// joypad.write(int which, table buttons)
//
//   Sets the given buttons to be pressed during the next
//   frame advance. The table should have the right 
//   keys (no pun intended) set.
static int joypad_set(lua_State *L) {

	// Which joypad we're tampering with
	int which = luaL_checkinteger(L,1);
	if (which < 1 || which > 4) {
		luaL_error(L,"Invalid output port (valid range 1-4, specified %d)", which);

	}

	// And the table of buttons.
	luaL_checktype(L,2,LUA_TTABLE);

	// Set up for taking control of the indicated controller
	lua_joypads_used |= 1 << (which-1);
	lua_joypads[which-1] = 0;

	int i;
	for (i=0; i < 8; i++) {
		lua_getfield(L, 2, button_mappings[i]);
		if (!lua_isnil(L,-1))
			lua_joypads[which-1] |= 1 << i;
		lua_pop(L,1);
	}
	
	return 0;
}




// Helper function to convert a savestate object to the filename it represents.
static char *savestateobj2filename(lua_State *L, int offset) {
	
	// First we get the metatable of the indicated object
	int result = lua_getmetatable(L, offset);

	if (!result)
		luaL_error(L, "object not a savestate object");
	
	// Also check that the type entry is set
	lua_getfield(L, -1, "__metatable");
	if (strcmp(lua_tostring(L,-1), "FCEU Savestate") != 0)
		luaL_error(L, "object not a savestate object");
	lua_pop(L,1);
	
	// Now, get the field we want
	lua_getfield(L, -1, "filename");
	
	// Return it
	return (char *) lua_tostring(L, -1);
}


// Helper function for garbage collection.
static int savestate_gc(lua_State *L) {
	// The object we're collecting is on top of the stack
	lua_getmetatable(L,1);
	
	// Get the filename
	const char *filename;
	lua_getfield(L, -1, "filename");
	filename = lua_tostring(L,-1);

	// Delete the file
	remove(filename);
	
	// We exit, and the garbage collector takes care of the rest.
	return 0;
}

// object savestate.create(int which = nil)
//
//  Creates an object used for savestates.
//  The object can be associated with a player-accessible savestate
//  ("which" between 1 and 10) or not (which == nil).
static int savestate_create(lua_State *L) {
	int which = -1;
	if (lua_gettop(L) >= 1) {
		which = luaL_checkinteger(L, 1);
		if (which < 1 || which > 10) {
			luaL_error(L, "invalid player's savestate %d", which);
		}
	}
	

	char *filename;

	if (which > 0) {
		// Find an appropriate filename. This is OS specific, unfortunately.
		// So I turned the filename selection code into my bitch. :)
		// Numbers are 0 through 9 though.
		filename = FCEU_MakeFName(FCEUMKF_STATE, which - 1, 0);
	}
	else {
		filename = tempnam(NULL, "snlua");
	}
	
	// Our "object". We don't care about the type, we just need the memory and GC services.
	lua_newuserdata(L,1);
	
	// The metatable we use, protected from Lua and contains garbage collection info and stuff.
	lua_newtable(L);
	
	// First, we must protect it
	lua_pushstring(L, "FCEU Savestate");
	lua_setfield(L, -2, "__metatable");
	
	
	// Now we need to save the file itself.
	lua_pushstring(L, filename);
	lua_setfield(L, -2, "filename");
	
	// If it's an anonymous savestate, we must delete the file from disk should it be gargage collected
	if (which < 0) {
		lua_pushcfunction(L, savestate_gc);
		lua_setfield(L, -2, "__gc");
		
	}
	
	// Set the metatable
	lua_setmetatable(L, -2);

	// The filename was allocated using malloc. Do something about that.
	free(filename);
	
	// Awesome. Return the object
	return 1;
	
}


// savestate.save(object state)
//
//   Saves a state to the given object.
static int savestate_save(lua_State *L) {

	char *filename = savestateobj2filename(L,1);

//	printf("saving %s\n", filename);

	// Save states are very expensive. They take time.
	numTries--;

	FCEUI_SaveState(filename);
	return 0;

}

// savestate.load(object state)
//
//   Loads the given state
static int savestate_load(lua_State *L) {

	char *filename = savestateobj2filename(L,1);

	numTries--;

//	printf("loading %s\n", filename);
	FCEUI_LoadState(filename);
	return 0;

}


// int movie.framecount()
//
//   Gets the frame counter for the movie, or nil if no movie running.
int movie_framecount(lua_State *L) {
	if (!FCEUMOV_IsPlaying() && !FCEUMOV_IsRecording()) {
		lua_pushnil(L);
		return 1;
	}
	
	lua_pushinteger(L, FCEUMOV_GetFrame());
	return 1;
}

// string movie.mode()
//
//   "record", "playback" or nil
int movie_mode(lua_State *L) {
	if (FCEUMOV_IsRecording())
		lua_pushstring(L, "record");
	else if (FCEUMOV_IsPlaying())
		lua_pushstring(L, "playback");
	else
		lua_pushnil(L);
	return 1;
}


static int movie_rerecordcounting(lua_State *L) {
	if (lua_gettop(L) == 0)
		luaL_error(L, "no parameters specified");

	skipRerecords = lua_toboolean(L,1);
	return 0;
}

// movie.stop()
//
//   Stops movie playback/recording. Bombs out if movie is not running.
static int movie_stop(lua_State *L) {
	if (!FCEUMOV_IsRecording() && !FCEUMOV_IsPlaying())
		luaL_error(L, "no movie");
	
	FCEUI_StopMovie();
	return 0;

}




// Common code by the gui library: make sure the screen array is ready
static void gui_prepare() {
	if (!gui_data)
		gui_data = (uint8 *) FCEU_malloc(256 * 256 + 8);
	if (gui_used != GUI_USED_SINCE_LAST_DISPLAY)
		memset(gui_data,GUI_COLOUR_CLEAR,256*256);
	gui_used = GUI_USED_SINCE_LAST_DISPLAY;
}


// Helper for a simple hex parser
static int hex2int(lua_State *L, char c) {
	if (c >= '0' && c <= '9')
		return c-'0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return luaL_error(L, "invalid hex in colour");
}

/**
 * Returns an index approximating an RGB colour.
 * TODO: This is easily improvable in terms of speed and probably
 * quality of matches. (gd overlay & transparency code call it a lot.)
 * With effort we could also cheat and map indices 0x08 .. 0x3F
 * ourselves.
 */
static uint8 gui_colour_rgb(uint8 r, uint8 g, uint8 b) {
	static uint8 index_lookup[1 << (3+3+3)];
        int k;
	if (!gui_saw_current_palette)
	{
		memset(index_lookup, GUI_COLOUR_CLEAR, sizeof(index_lookup));
		gui_saw_current_palette = TRUE;
	}

	k = ((r & 0xE0) << 1) | ((g & 0xE0) >> 2) | ((b & 0xE0) >> 5);
	uint16 test, best = GUI_COLOUR_CLEAR;
	uint32 best_score = 0xffffffffu, test_score;
	if (index_lookup[k] != GUI_COLOUR_CLEAR) return index_lookup[k];
	for (test = 0; test < 0xff; test++)
	{
		uint8 tr, tg, tb;
		if (test == GUI_COLOUR_CLEAR) continue;
		FCEUD_GetPalette(test, &tr, &tg, &tb);
		test_score = abs(r - tr) *  66 +
		             abs(g - tg) * 129 +
		             abs(b - tb) *  25;
		if (test_score < best_score) best_score = test_score, best = test;
	}
	index_lookup[k] = best;
	return best;
}

void FCEU_LuaUpdatePalette()
{
    gui_saw_current_palette = FALSE;
}

/**
 * Converts an integer or a string on the stack at the given
 * offset to a native colour. Several encodings are supported.
 * The user may pass their own palette index, a simple colour name,
 * or an HTML-style "#09abcd" colour, which is approximated.
 */
static uint16 gui_getcolour(lua_State *L, int offset) {
	switch (lua_type(L,offset)) {
	case LUA_TSTRING:
		{
			const char *str = lua_tostring(L,offset);
			if (strcmp(str,"red")==0) {
				return GUI_COLOUR_RED;
			} else if (strcmp(str, "green")==0) {
				return GUI_COLOUR_GREEN;
			} else if (strcmp(str, "blue")==0) {
				return GUI_COLOUR_BLUE;
			} else if (strcmp(str, "black")==0) {
				return GUI_COLOUR_BLACK;
			} else if (strcmp(str, "white")==0) {
				return GUI_COLOUR_WHITE;
			} else if (strcmp(str, "clear")==0) {
				return GUI_COLOUR_CLEAR;
			} else if (str[0] == '#' && strlen(str) == 7) {
				int red, green, blue;
				red = (hex2int(L, str[1]) << 4) | hex2int(L, str[2]);
				green = (hex2int(L, str[3]) << 4) | hex2int(L, str[4]);
				blue = (hex2int(L, str[5]) << 4) | hex2int(L, str[6]);

				return gui_colour_rgb(red, green, blue);
			} else
				return luaL_error(L, "unknown colour %s", str);

		}
	case LUA_TNUMBER:
		return (uint8) lua_tointeger(L,offset);
	default:
		return luaL_error(L, "invalid colour");
	}

}

// I'm going to use this a lot in here
#define swap(T, one, two) { \
	T temp = one; \
	one = two;    \
	two = temp;   \
}

// gui.drawpixel(x,y,colour)
static int gui_drawpixel(lua_State *L) {

	int x = luaL_checkinteger(L, 1);
	int y = luaL_checkinteger(L,2);
	y += FSettings.FirstSLine;

	uint8 colour = gui_getcolour(L,3);

	if (x < 0 || x >= 256 || y < FSettings.FirstSLine || y > FSettings.LastSLine)
		luaL_error(L,"bad coordinates");

	gui_prepare();

	gui_data[y*256 + x] = colour;

	return 0;
}

// gui.drawline(x1,y1,x2,y2,type colour)
static int gui_drawline(lua_State *L) {

	int x1,y1,x2,y2;
	uint8 colour;
	x1 = luaL_checkinteger(L,1);
	y1 = luaL_checkinteger(L,2);
	x2 = luaL_checkinteger(L,3);
	y2 = luaL_checkinteger(L,4);
	y1 += FSettings.FirstSLine;
	y2 += FSettings.FirstSLine;
	colour = gui_getcolour(L,5);

	if (x1 < 0 || x1 >= 256 || y1 < FSettings.FirstSLine || y1 > FSettings.LastSLine)
		luaL_error(L,"bad coordinates");

	if (x2 < 0 || x2 >= 256 || y2 < FSettings.FirstSLine || y2 > FSettings.LastSLine)
		luaL_error(L,"bad coordinates");

	gui_prepare();


	// Horizontal line?
	if (y1 == y2) {
		if (x1 > x2) 
			swap(int, x1, x2);
		int i;
		for (i=x1; i <= x2; i++)
			gui_data[y1*256+i] = colour;
	} else if (x1 == x2) { // Vertical line?
		if (y1 > y2)
			swap(int, y1, y2);
		int i;
		for (i=y1; i < y2; i++)
			gui_data[i*256+x1] = colour;
	} else {
		// Some very real slope. We want to increase along the x value, so we swap for that.
		if (x1 > x2) {
			swap(int, x1, x2);
			swap(int, y1, y2);
		}


		double slope = ((double)y2-(double)y1) / ((double)x2-(double)x1);
		int myX = x1, myY = y1;
		double accum = 0;

		while (myX <= x2) {
			// Draw the current pixel
			gui_data[myY*256 + myX] = colour;

			// If it's above 1, we knock 1 off it and go up 1 pixel
			if (accum >= 1.0) {
				myY += 1;
				accum -= 1.0;
			} else if (accum <= -1.0) {
				myY -= 1;
				accum += 1.0;
			} else {
				myX += 1;
				accum += slope; // Step up

			}
		}


	}

	return 0;
}

// gui.drawbox(x1, y1, x2, y2, colour)
static int gui_drawbox(lua_State *L) {

	int x1,y1,x2,y2;
	uint8 colour;
	int i;

	x1 = luaL_checkinteger(L,1);
	y1 = luaL_checkinteger(L,2);
	x2 = luaL_checkinteger(L,3);
	y2 = luaL_checkinteger(L,4);
	y1 += FSettings.FirstSLine;
	y2 += FSettings.FirstSLine;
	colour = gui_getcolour(L,5);

	if (x1 < 0 || x1 >= 256 || y1 < FSettings.FirstSLine || y1 > FSettings.LastSLine)
		luaL_error(L,"bad coordinates");

	if (x2 < 0 || x2 >= 256 || y2 < FSettings.FirstSLine || y2 > FSettings.LastSLine)
		luaL_error(L,"bad coordinates");


	gui_prepare();

	// For simplicity, we mandate that x1,y1 be the upper-left corner
	if (x1 > x2)
		swap(int, x1, x2);
	if (y1 > y2)
		swap(int, y1, y2);

	// top surface
	for (i=x1; i <= x2; i++)
		gui_data[y1*256 + i] = colour;

	// bottom surface
	for (i=x1; i <= x2; i++)
		gui_data[y2*256 + i] = colour;

	// left surface
	for (i=y1; i <= y2; i++)
		gui_data[i*256+x1] = colour;

	// right surface
	for (i=y1; i <= y2; i++)
		gui_data[i*256+x2] = colour;


	return 0;
}


// gui.gdscreenshot()
//
//  Returns a screen shot as a string in gd's v1 file format.
//  This allows us to make screen shots available without gd installed locally.
//  Users can also just grab pixels via substring selection.
//
//  I think...  Does lua support grabbing byte values from a string?
//  Well, either way, just install gd and do what you like with it.
//  It really is easier that way.
static int gui_gdscreenshot(lua_State *L) {

	// Eat the stack
	lua_settop(L,0);
	
	// This is QUITE nasty...
	
	const int width=256, height=1+FSettings.LastSLine-FSettings.FirstSLine;
	
	// Stack allocation
	unsigned char *buffer = (unsigned char*)alloca(2+2+2+1+4 + (width*height*4));
	unsigned char *pixels = (buffer + 2+2+2+1+4);

	// Truecolour image
	buffer[0] = 255;
	buffer[1] = 254;
	
	// Width
	buffer[2] = width >> 8;
	buffer[3] = width & 0xFF;
	
	// height
	buffer[4] = height >> 8;
	buffer[5] = height & 0xFF;
	
	// Make it truecolour... AGAIN?
	buffer[6] = 1;
	
	// No transparency
	buffer[7] = buffer[8] = buffer[9] = buffer[10] = 255;
	
	// Now we can actually save the image data
	int i = 0;
	int x,y;
	for (y=FSettings.FirstSLine; y <= FSettings.LastSLine; y++) {
		for (x=0; x < width; x++) {
			uint8 index = XBuf[y*256 + x];

			// Write A,R,G,B (alpha=0 for us):
			pixels[i] = 0;
			FCEUD_GetPalette(index, &pixels[i+1],&pixels[i+2], &pixels[i+3]); 
			i += 4;
		}
	}
	
	// Ugh, ugh, ugh. Don't call this more than once a frame, for god's sake!
	
	lua_pushlstring(L, (char*)buffer, 2+2+2+1+4 + (width*height*4));
	
	// Buffers allocated with alloca are freed by the function's exit, since they're on the stack.
	
	return 1;
}


// gui.transparency(int strength)
//
//  0 = solid, 
static int gui_transparency(lua_State *L) {
	int trans = luaL_checkinteger(L,1);
	if (trans < 0 || trans > 4) {
		luaL_error(L, "transparency out of range");
	}
	
	transparency = trans;
	return 0;
}


// gui.text(int x, int y, string msg)
//
//  Displays the given text on the screen, using the same font and techniques as the
//  main HUD.
static int gui_text(lua_State *L) {
	const char *msg;
	int x, y;

	x = luaL_checkinteger(L,1);
	y = luaL_checkinteger(L,2);
	msg = luaL_checkstring(L,3);
	y += FSettings.FirstSLine;

	if (x < 0 || x >= 256 || y < FSettings.FirstSLine || y > FSettings.LastSLine)
		luaL_error(L,"bad coordinates");

	gui_prepare();

	DrawTextTransWH(gui_data+y*256+x, 256, (uint8 *)msg, 0x20+0x80, 256 - x, 1 + FSettings.LastSLine - y);

	return 0;

}


// gui.gdoverlay(string str)
//
//  Overlays the given image on the screen.
static int gui_gdoverlay(lua_State *L) {

	int baseX, baseY;
	int width, height;
	size_t size;

	baseX = luaL_checkinteger(L,1);
	baseY = luaL_checkinteger(L,2);
	const uint8 *data = (const uint8*) luaL_checklstring(L, 3, &size);
	
	if (size < 11)
		luaL_error(L, "bad image data");
	
	if (data[0] != 255 || data[1] != 254)
		luaL_error(L, "bad image data or not truecolour");
		
	width = data[2] << 8 | data[3];
	height = data[4] << 8 | data[5];
	
	if (!data[6])
		luaL_error(L, "bad image data or not truecolour");
	
	// Don't care about transparent colour
	if (size < (11+ width*height*4))
		luaL_error(L, "bad image data");
	
	const uint8* pixels = data + 11;
	
	// Run image

	gui_prepare();

	// These coordinates are relative to the image itself.
	int x,y;
	
	// These coordinates are relative to the screen
	int sx, sy;
	
	if (baseY < 0) {
		// Above the top of the screen
		sy = 0;
		y = -baseY;
	} else {
		// It starts on the screen itself
		sy = baseY;
		y = 0; 
	}	
	
	for (sy += FSettings.FirstSLine; y < height && sy <= FSettings.LastSLine; y++, sy++) {
	
		if (baseX < 0) {
			x = -baseX;
			sx = 0;
		} else {
			x = 0;
			sx = baseX;
		}

		for (; x < width && sx < 256; x++, sx++) {
			if (pixels[4 * (y*height+x)] == 127)
				continue;

			uint8 r = pixels[4 * (y*width+x)+1];
			uint8 g = pixels[4 * (y*width+x)+2];
			uint8 b = pixels[4 * (y*width+x)+3];
			gui_data[256*(sy)+sx] = gui_colour_rgb(r, g, b);
		}
	
	}

	return 0;
}


// function gui.register(function f)
//
//  This function will be called just before a graphical update.
//  More complicated, but doesn't suffer any frame delays.
//  Nil will be accepted in place of a function to erase
//  a previously registered function, and the previous function
//  (if any) is returned, or nil if none.
static int gui_register(lua_State *L) {

	// We'll do this straight up.


	// First set up the stack.
	lua_settop(L,1);
	
	// Verify the validity of the entry
	if (!lua_isnil(L,1))
		luaL_checktype(L, 1, LUA_TFUNCTION);

	// Get the old value
	lua_getfield(L, LUA_REGISTRYINDEX, guiCallbackTable);
	
	// Save the new value
	lua_pushvalue(L,1);
	lua_setfield(L, LUA_REGISTRYINDEX, guiCallbackTable);
	
	// The old value is on top of the stack. Return it.
	return 1;

}

// string gui.popup(string message, [string type = "ok"])
//
//  Popup dialog!
int gui_popup(lua_State *L) {
	const char *message = luaL_checkstring(L, 1);
	const char *type = luaL_optstring(L, 2, "ok");
	
#ifdef __WIN32__
	int t;
	if (strcmp(type, "ok") == 0)
		t = MB_OK;
	else if (strcmp(type, "yesno") == 0)
		t = MB_YESNO;
	else if (strcmp(type, "yesnocancel") == 0)
		t = MB_YESNOCANCEL;
	else
		return luaL_error(L, "invalid popup type \"%s\"", type);

        StopSound();
	int result = MessageBox(hAppWnd, message, "Lua Script Pop-up", t);
	
	lua_settop(L,1);

	if (t != MB_OK) {
		if (result == IDYES)
			lua_pushstring(L, "yes");
		else if (result == IDNO)
			lua_pushstring(L, "no");
		else if (result == IDCANCEL)
			lua_pushstring(L, "cancel");
		else
			luaL_error(L, "win32 unrecognized return value %d", result);
		return 1;
	}

	// else, we don't care.
	return 0;
#else

	char *t;
#ifdef __linux

	int pid; // appease compiler

	// Before doing any work, verify the correctness of the parameters.
	if (strcmp(type, "ok") == 0)
		t = "OK:100";
	else if (strcmp(type, "yesno") == 0)
		t = "Yes:100,No:101";
	else if (strcmp(type, "yesnocancel") == 0)
		t = "Yes:100,No:101,Cancel:102";
	else
		return luaL_error(L, "invalid popup type \"%s\"", type);

	// Can we find a copy of xmessage? Search the path.
	
	char *path = strdup(getenv("PATH"));

	char *current = path;
	
	char *colon;

	int found = 0;

	while (current) {
		colon = strchr(current, ':');
		
		// Clip off the colon.
		*colon++ = 0;
		
		int len = strlen(current);
		char *filename = (char*)malloc(len + 12); // always give excess
		snprintf(filename, len+12, "%s/xmessage", current);
		
		if (access(filename, X_OK) == 0) {
			free(filename);
			found = 1;
			break;
		}
		
		// Failed, move on.
		current = colon;
		free(filename);
		
	}

	free(path);

	// We've found it?
	if (!found)
		goto use_console;

	pid = fork();
	if (pid == 0) {// I'm the virgin sacrifice
	
		// I'm gonna be dead in a matter of microseconds anyways, so wasted memory doesn't matter to me.
		// Go ahead and abuse strdup.
		char * parameters[] = {"xmessage", "-buttons", t, strdup(message), NULL};

		execvp("xmessage", parameters);
		
		// Aw shitty
		perror("exec xmessage");
		exit(1);
	}
	else if (pid < 0) // something went wrong!!! Oh hell... use the console
		goto use_console;
	else {
		// We're the parent. Watch for the child.
		int r;
		int res = waitpid(pid, &r, 0);
		if (res < 0) // wtf?
			goto use_console;
		
		// The return value gets copmlicated...
		if (!WIFEXITED(r)) {
			luaL_error(L, "don't screw with my xmessage process!");
		}
		r = WEXITSTATUS(r);
		
		// We assume it's worked.
		if (r == 0)
		{
			return 0; // no parameters for an OK
		}
		if (r == 100) {
			lua_pushstring(L, "yes");
			return 1;
		}
		if (r == 101) {
			lua_pushstring(L, "no");
			return 1;
		}
		if (r == 102) {
			lua_pushstring(L, "cancel");
			return 1;
		}
		
		// Wtf?
		return luaL_error(L, "popup failed due to unknown results involving xmessage (%d)", r);
	}

use_console:
#endif

	// All else has failed

	if (strcmp(type, "ok") == 0)
		t = "";
	else if (strcmp(type, "yesno") == 0)
		t = "yn";
	else if (strcmp(type, "yesnocancel") == 0)
		t = "ync";
	else
		return luaL_error(L, "invalid popup type \"%s\"", type);

	fprintf(stderr, "Lua Message: %s\n", message);

	while (TRUE) {
		char buffer[64];

		// We don't want parameters
		if (!t[0]) {
			fprintf(stderr, "[Press Enter]");
			fgets(buffer, sizeof(buffer), stdin);
			// We're done
			return 0;

		}
		fprintf(stderr, "(%s): ", t);
		fgets(buffer, sizeof(buffer), stdin);
		
		// Check if the option is in the list
		if (strchr(t, tolower(buffer[0]))) {
			switch (tolower(buffer[0])) {
			case 'y':
				lua_pushstring(L, "yes");
				return 1;
			case 'n':
				lua_pushstring(L, "no");
				return 1;
			case 'c':
				lua_pushstring(L, "cancel");
				return 1;
			default:
				luaL_error(L, "internal logic error in console based prompts for gui.popup");
			
			}
		}
		
		// We fell through, so we assume the user answered wrong and prompt again.
	
	}

	// Nothing here, since the only way out is in the loop.
#endif

}


// int AND(int one, int two, ..., int n)
//
//  Since Lua doesn't provide binary, I provide this function.
//  Does a full binary AND on all parameters and returns the result.
static int base_AND(lua_State *L) {
	int count = lua_gettop(L);
	
	lua_Integer result = ~((lua_Integer)0);
	int i;
	for (i=1; i <= count; i++)
		result &= luaL_checkinteger(L,i);
	lua_settop(L,0);
	lua_pushinteger(L, result);
	return 1;
}


// int OR(int one, int two, ..., int n)
//
//   ..and similarly for a binary OR
static int base_OR(lua_State *L) {
	int count = lua_gettop(L);
	
	lua_Integer result = 0;
	int i;
	for (i=1; i <= count; i++)
		result |= luaL_checkinteger(L,i);
	lua_settop(L,0);
	lua_pushinteger(L, result);
	return 1;
}


// int XOR(int one, int two, ..., int n)
//
//   ..and similarly for a binary XOR
static int base_XOR(lua_State *L) {
	int count = lua_gettop(L);
	
	lua_Integer result = 0;
	int i;
	for (i=1; i <= count; i++)
		result ^= luaL_checkinteger(L,i);
	lua_settop(L,0);
	lua_pushinteger(L, result);
	return 1;
}


// int BIT(int one, int two, ..., int n)
//
//   Returns a number with the specified bit(s) set.
static int base_BIT(lua_State *L) {
	int count = lua_gettop(L);
	
	lua_Integer result = 0;
	int i;
	for (i=1; i <= count; i++)
		result |= (lua_Integer)1 << luaL_checkinteger(L,i);
	lua_settop(L,0);
	lua_pushinteger(L, result);
	return 1;
}



// The function called periodically to ensure Lua doesn't run amok.
static void FCEU_LuaHookFunction(lua_State *L, lua_Debug *dbg) {
	
	if (numTries-- == 0) {

		int kill = 0;

#ifdef WIN32
		// Uh oh
                StopSound();
		int ret = MessageBox(hAppWnd, "The Lua script running has been running a long time. It may have gone crazy. Kill it?\n\n(No = don't check anymore either)", "Lua Script Gone Nuts?", MB_YESNO);
		
		if (ret == IDYES) {
			kill = 1;
		}

#else
		fprintf(stderr, "The Lua script running has been running a long time.\nIt may have gone crazy. Kill it? (I won't ask again if you say No)\n");
		char buffer[64];
		while (TRUE) {
			fprintf(stderr, "(y/n): ");
			fgets(buffer, sizeof(buffer), stdin);
			if (buffer[0] == 'y' || buffer[0] == 'Y') {
				kill = 1;
				break;
			}
			
			if (buffer[0] == 'n' || buffer[0] == 'N')
				break;
		}
#endif

		if (kill) {
			luaL_error(L, "Killed by user request.");
			FCEU_LuaOnStop();
		}

		// else, kill the debug hook.
		lua_sethook(L, NULL, 0, 0);
	}
	

}


static const struct luaL_reg fceulib [] = {

	{"speedmode", fceu_speedmode},
	{"frameadvance", fceu_frameadvance},
	{"pause", fceu_pause},

	{"message", fceu_message},
	{NULL,NULL}
};

static const struct luaL_reg memorylib [] = {

	{"readbyte", memory_readbyte},
	{"readbytesigned", memory_readbytesigned},
	{"writebyte", memory_writebyte},

	{"register", memory_register},

	{NULL,NULL}
};

static const struct luaL_reg joypadlib[] = {
	{"read", joypad_read},
	{"set", joypad_set},

	{NULL,NULL}
};

static const struct luaL_reg savestatelib[] = {
	{"create", savestate_create},
	{"save", savestate_save},
	{"load", savestate_load},

	{NULL,NULL}
};

static const struct luaL_reg movielib[] = {

	{"framecount", movie_framecount},
	{"mode", movie_mode},
	{"rerecordcounting", movie_rerecordcounting},
	{"stop", movie_stop},
//	{"record", movie_record},
//	{"playback", movie_playback},

	{NULL,NULL}

};


static const struct luaL_reg guilib[] = {
	
	{"drawpixel", gui_drawpixel},
	{"drawline", gui_drawline},
	{"drawbox", gui_drawbox},
	{"text", gui_text},

	{"gdscreenshot", gui_gdscreenshot},
	{"gdoverlay", gui_gdoverlay},
	{"transparency", gui_transparency},

	{"register", gui_register},
	
	{"popup", gui_popup},
	{NULL,NULL}

};


void FCEU_LuaFrameBoundary() {

//	printf("Lua Frame\n");

	// HA!
	if (!LUA || !luaRunning)
		return;

	// Our function needs calling
	lua_settop(LUA,0);
	lua_getfield(LUA, LUA_REGISTRYINDEX, frameAdvanceThread);
	lua_State *thread = lua_tothread(LUA,1);	

	// Lua calling C must know that we're busy inside a frame boundary
	frameBoundary = TRUE;
	frameAdvanceWaiting = FALSE;

	numTries = 1000;
	int result = lua_resume(thread, 0);
	
	if (result == LUA_YIELD) {
		// Okay, we're fine with that.
	} else if (result != 0) {
		// Done execution by bad causes
		FCEU_LuaOnStop();
		lua_pushnil(LUA);
		lua_setfield(LUA, LUA_REGISTRYINDEX, frameAdvanceThread);
		
		// Error?
#ifdef WIN32
                StopSound();
		MessageBox( hAppWnd, lua_tostring(thread,-1), "Lua run error", MB_OK | MB_ICONSTOP);
#else
		fprintf(stderr, "Lua thread bombed out: %s\n", lua_tostring(thread,-1));
#endif

	} else {
		FCEU_LuaOnStop();
		FCEU_DispMessage("Script died of natural causes.\n");
	}

	// Past here, the nes actually runs, so any Lua code is called mid-frame. We must
	// not do anything too stupid, so let ourselves know.
	frameBoundary = FALSE;

	if (!frameAdvanceWaiting) {
		FCEU_LuaOnStop();
	}

}

/**
 * Loads and runs the given Lua script.
 * The emulator MUST be paused for this function to be
 * called. Otherwise, all frame boundary assumptions go out the window.
 *
 * Returns true on success, false on failure.
 */
int FCEU_LoadLuaCode(const char *filename) {
	if (filename != luaScriptName)
	{
		if (luaScriptName) free(luaScriptName);
		luaScriptName = strdup(filename);
	}

	if (!LUA) {
		LUA = lua_open();
		luaL_openlibs(LUA);

		luaL_register(LUA, "FCEU", fceulib);
		luaL_register(LUA, "memory", memorylib);
		luaL_register(LUA, "joypad", joypadlib);
		luaL_register(LUA, "savestate", savestatelib);
		luaL_register(LUA, "movie", movielib);
		luaL_register(LUA, "gui", guilib);

		lua_pushcfunction(LUA, base_AND);
		lua_setfield(LUA, LUA_GLOBALSINDEX, "AND");
		lua_pushcfunction(LUA, base_OR);
		lua_setfield(LUA, LUA_GLOBALSINDEX, "OR");
		lua_pushcfunction(LUA, base_XOR);
		lua_setfield(LUA, LUA_GLOBALSINDEX, "XOR");
		lua_pushcfunction(LUA, base_BIT);
		lua_setfield(LUA, LUA_GLOBALSINDEX, "BIT");

		
		lua_newtable(LUA);
		lua_setfield(LUA, LUA_REGISTRYINDEX, memoryWatchTable);
		lua_newtable(LUA);
		lua_setfield(LUA, LUA_REGISTRYINDEX, memoryValueTable);
	}

	// We make our thread NOW because we want it at the bottom of the stack.
	// If all goes wrong, we let the garbage collector remove it.
	lua_State *thread = lua_newthread(LUA);
	
	// Load the data	
	int result = luaL_loadfile(LUA,filename);

	if (result) {
#ifdef WIN32
		// Doing this here caused nasty problems; reverting to MessageBox-from-dialog behavior.
                StopSound();
		MessageBox(NULL, lua_tostring(LUA,-1), "Lua load error", MB_OK | MB_ICONSTOP);
#else
		fprintf(stderr, "Failed to compile file: %s\n", lua_tostring(LUA,-1));
#endif

		// Wipe the stack. Our thread
		lua_settop(LUA,0);
		return 0; // Oh shit.
	}

	
	// Get our function into it
	lua_xmove(LUA, thread, 1);
	
	// Save the thread to the registry. This is why I make the thread FIRST.
	lua_setfield(LUA, LUA_REGISTRYINDEX, frameAdvanceThread);
	

	// Initialize settings
	luaRunning = TRUE;
	skipRerecords = FALSE;

	wasPaused = FCEUI_EmulationPaused();
	if (wasPaused) FCEUI_ToggleEmulationPause();

	// And run it right now. :)
	//FCEU_LuaFrameBoundary();

	// Set up our protection hook to be executed once every 10,000 bytecode instructions.
	lua_sethook(thread, FCEU_LuaHookFunction, LUA_MASKCOUNT, 10000);

	// We're done.
	return 1;
}

/**
 * Equivalent to repeating the last FCEU_LoadLuaCode() call.
 */
void FCEU_ReloadLuaCode()
{
	if (!luaScriptName)
		FCEU_DispMessage("There's no script to reload.");
	else
		FCEU_LoadLuaCode(luaScriptName);
}


/**
 * Terminates a running Lua script by killing the whole Lua engine.
 *
 * Always safe to call, except from within a lua call itself (duh).
 *
 */
void FCEU_LuaStop() {

	// Kill it.
	if (LUA) {
		lua_close(LUA); // this invokes our garbage collectors for us
		LUA = NULL;
		FCEU_LuaOnStop();
	}

}

/**
 * Returns true if there is a Lua script running.
 *
 */
int FCEU_LuaRunning() {
	return LUA && luaRunning;
}


/**
 * Returns true if Lua would like to steal the given joypad control.
 */
int FCEU_LuaUsingJoypad(int which) {
	return lua_joypads_used & (1 << which);
}

/**
 * Reads the buttons Lua is feeding for the given joypad, in the same
 * format as the OS-specific code.
 *
 * This function must not be called more than once per frame. Ideally exactly once
 * per frame (if FCEU_LuaUsingJoypad says it's safe to do so)
 */
uint8 FCEU_LuaReadJoypad(int which) {
	lua_joypads_used &= !(1 << which);
	return lua_joypads[which];
}

/**
 * If this function returns true, the movie code should NOT increment
 * the rerecord count for a load-state.
 *
 * This function will not return true if a script is not running.
 */
int FCEU_LuaRerecordCountSkip() {
	return LUA && luaRunning && skipRerecords;
}


/**
 * Given an 8-bit screen with the indicated resolution,
 * draw the current GUI onto it.
 *
 * Currently we only support 256x* resolutions.
 */
void FCEU_LuaGui(uint8 *XBuf) {

	if (!LUA || !luaRunning)
		return;

	// First, check if we're being called by anybody
	lua_getfield(LUA, LUA_REGISTRYINDEX, guiCallbackTable);
	
	if (lua_isfunction(LUA, -1)) {
		// We call it now
		numTries = 1000;
		int ret = lua_pcall(LUA, 0, 0, 0);
		if (ret != 0) {
			int kill = 0;
#ifdef WIN32
			char message[1024];
			int ret;
			StopSound();
			sprintf(message, "Lua error in gui.register function:\n%s\nFunction has been unregistered. Kill script?", lua_tostring(LUA, -1));
			ret = MessageBox(hAppWnd, message, "Lua Error in GUI function", MB_YESNO);

			if (ret == IDYES) {
				kill = 1;
			}
#else
			fprintf(stderr, "Lua error in gui.register function: %sFunction has been unregistered. Kill script?\n", lua_tostring(LUA, -1));
			// XXX Do this more intelligently than by using cut&paste
			char buffer[64];
			while (TRUE) {
				fprintf(stderr, "(y/n): ");
				fgets(buffer, sizeof(buffer), stdin);
				if (buffer[0] == 'y' || buffer[0] == 'Y') {
					kill = 1;
					break;
				}
				
				if (buffer[0] == 'n' || buffer[0] == 'N')
					break;
			}
#endif

			// This is grounds for trashing the function, or worse
			if (kill) {
				FCEU_LuaStop();
				return;
			}
			else
			{
				lua_pushnil(LUA);
				lua_setfield(LUA, LUA_REGISTRYINDEX, guiCallbackTable);
			}
		
		}
	}

	// And wreak the stack
	lua_settop(LUA, 0);

	if (gui_used == GUI_CLEAR)
		return;

	gui_used = GUI_USED_SINCE_LAST_FRAME;

	int x,y;

	if (transparency == 4) // wtf?
		return;
	
	// direct copy
	if (transparency == 0) {
		for (y = FSettings.FirstSLine; y <= FSettings.LastSLine && y < 256; y++) {
			for (x=0; x < 256; x++) {
				if (gui_data[y*256+x] != GUI_COLOUR_CLEAR)
					XBuf[y*256 + x] =  gui_data[y*256+x];
			}
		}
	} else {
		for (y = FSettings.FirstSLine; y <= FSettings.LastSLine && y < 256; y++) {
			for (x=0; x < 256; x++) {
				if (gui_data[y*256+x] != GUI_COLOUR_CLEAR) {
					uint8 rg, gg, bg,  rx, gx, bx,  r, g, b;
					FCEUD_GetPalette(gui_data[y*256+x], &rg, &gg, &bg);
					FCEUD_GetPalette(    XBuf[y*256+x], &rx, &gx, &bx);
					r = (rg * (4 - transparency) + rx * transparency) / 4;
					g = (gg * (4 - transparency) + gx * transparency) / 4;
					b = (bg * (4 - transparency) + bx * transparency) / 4;
					XBuf[y*256+x] = gui_colour_rgb(r, g, b);
				}
			}
		}
	}

	return;
}
