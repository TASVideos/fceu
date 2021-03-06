#ifndef _INPUT_H_
#define _INPUT_H_

typedef struct {
        uint8 FP_FASTAPASS(1) (*Read)(int w);
	void FP_FASTAPASS(1) (*Write)(uint8 v);
        void FP_FASTAPASS(1) (*Strobe)(int w);
	void FP_FASTAPASS(3) (*Update)(int w, void *data, int arg);
	void FP_FASTAPASS(3) (*SLHook)(int w, uint8 *bg, uint8 *spr, uint32 linets, int final);
	void FP_FASTAPASS(3) (*Draw)(int w, uint8 *buf, int arg);
} INPUTC;

typedef struct {
	uint8 FP_FASTAPASS(2) (*Read)(int w, uint8 ret);
	void FP_FASTAPASS(1) (*Write)(uint8 v);
	void (*Strobe)(void);
        void FP_FASTAPASS(2) (*Update)(void *data, int arg);
        void FP_FASTAPASS(3) (*SLHook)(uint8 *bg, uint8 *spr, uint32 linets, int final);
        void FP_FASTAPASS(2) (*Draw)(uint8 *buf, int arg);
} INPUTCFC;

void FCEU_DrawInput(uint8 *buf);
void FCEU_UpdateInput(void);
int FCEU_BotMode(void);
void FCEU_SetBotMode(int x);
void InitializeInput(void);
void FCEU_UpdateBot(void);
extern void (*PStrobe[2])(void);
extern void (*InputScanlineHook)(uint8 *bg, uint8 *spr, uint32 linets, int final);

void FCEU_DoSimpleCommand(int cmd);

enum EMUCMD
{
	EMUCMD_POWER=0,
	EMUCMD_RESET,
	EMUCMD_PAUSE,
	EMUCMD_FRAME_ADVANCE,
	EMUCMD_SCREENSHOT,
	EMUCMD_HIDE_MENU_TOGGLE,

	EMUCMD_SPEED_SLOWEST,
	EMUCMD_SPEED_SLOWER,
	EMUCMD_SPEED_NORMAL,
	EMUCMD_SPEED_FASTER,
	EMUCMD_SPEED_FASTEST,
	EMUCMD_SPEED_TURBO,

	EMUCMD_SAVE_SLOT_0,
	EMUCMD_SAVE_SLOT_1,
	EMUCMD_SAVE_SLOT_2,
	EMUCMD_SAVE_SLOT_3,
	EMUCMD_SAVE_SLOT_4,
	EMUCMD_SAVE_SLOT_5,
	EMUCMD_SAVE_SLOT_6,
	EMUCMD_SAVE_SLOT_7,
	EMUCMD_SAVE_SLOT_8,
	EMUCMD_SAVE_SLOT_9,
	EMUCMD_SAVE_SLOT_NEXT,
	EMUCMD_SAVE_SLOT_PREV,
	EMUCMD_SAVE_STATE,
	EMUCMD_SAVE_STATE_AS,
	EMUCMD_SAVE_STATE_SLOT_0,
	EMUCMD_SAVE_STATE_SLOT_1,
	EMUCMD_SAVE_STATE_SLOT_2,
	EMUCMD_SAVE_STATE_SLOT_3,
	EMUCMD_SAVE_STATE_SLOT_4,
	EMUCMD_SAVE_STATE_SLOT_5,
	EMUCMD_SAVE_STATE_SLOT_6,
	EMUCMD_SAVE_STATE_SLOT_7,
	EMUCMD_SAVE_STATE_SLOT_8,
	EMUCMD_SAVE_STATE_SLOT_9,
	EMUCMD_LOAD_STATE,
	EMUCMD_LOAD_STATE_FROM,
	EMUCMD_LOAD_STATE_SLOT_0,
	EMUCMD_LOAD_STATE_SLOT_1,
	EMUCMD_LOAD_STATE_SLOT_2,
	EMUCMD_LOAD_STATE_SLOT_3,
	EMUCMD_LOAD_STATE_SLOT_4,
	EMUCMD_LOAD_STATE_SLOT_5,
	EMUCMD_LOAD_STATE_SLOT_6,
	EMUCMD_LOAD_STATE_SLOT_7,
	EMUCMD_LOAD_STATE_SLOT_8,
	EMUCMD_LOAD_STATE_SLOT_9,

/*	EMUCMD_MOVIE_SLOT_0,
	EMUCMD_MOVIE_SLOT_1,
	EMUCMD_MOVIE_SLOT_2,
	EMUCMD_MOVIE_SLOT_3,
	EMUCMD_MOVIE_SLOT_4,
	EMUCMD_MOVIE_SLOT_5,
	EMUCMD_MOVIE_SLOT_6,
	EMUCMD_MOVIE_SLOT_7,
	EMUCMD_MOVIE_SLOT_8,
	EMUCMD_MOVIE_SLOT_9,
	EMUCMD_MOVIE_SLOT_NEXT,
	EMUCMD_MOVIE_SLOT_PREV,
	EMUCMD_MOVIE_RECORD,*/
	EMUCMD_MOVIE_RECORD_TO,
/*	EMUCMD_MOVIE_RECORD_SLOT_0,
	EMUCMD_MOVIE_RECORD_SLOT_1,
	EMUCMD_MOVIE_RECORD_SLOT_2,
	EMUCMD_MOVIE_RECORD_SLOT_3,
	EMUCMD_MOVIE_RECORD_SLOT_4,
	EMUCMD_MOVIE_RECORD_SLOT_5,
	EMUCMD_MOVIE_RECORD_SLOT_6,
	EMUCMD_MOVIE_RECORD_SLOT_7,
	EMUCMD_MOVIE_RECORD_SLOT_8,
	EMUCMD_MOVIE_RECORD_SLOT_9,
	EMUCMD_MOVIE_REPLAY,*/
	EMUCMD_MOVIE_REPLAY_FROM,
/*	EMUCMD_MOVIE_REPLAY_SLOT_0,
	EMUCMD_MOVIE_REPLAY_SLOT_1,
	EMUCMD_MOVIE_REPLAY_SLOT_2,
	EMUCMD_MOVIE_REPLAY_SLOT_3,
	EMUCMD_MOVIE_REPLAY_SLOT_4,
	EMUCMD_MOVIE_REPLAY_SLOT_5,
	EMUCMD_MOVIE_REPLAY_SLOT_6,
	EMUCMD_MOVIE_REPLAY_SLOT_7,
	EMUCMD_MOVIE_REPLAY_SLOT_8,
	EMUCMD_MOVIE_REPLAY_SLOT_9,*/
	EMUCMD_MOVIE_STOP,
	EMUCMD_MOVIE_PLAY_FROM_BEGINNING,
	EMUCMD_MOVIE_READONLY_TOGGLE,
	EMUCMD_MOVIE_FRAME_DISPLAY_TOGGLE,
	EMUCMD_MOVIE_INPUT_DISPLAY_TOGGLE,
	EMUCMD_MOVIE_ICON_DISPLAY_TOGGLE,

	EMUCMD_SCRIPT_RELOAD,

	EMUCMD_SOUND_TOGGLE,
	EMUCMD_SOUND_VOLUME_UP,
	EMUCMD_SOUND_VOLUME_DOWN,
	EMUCMD_SOUND_VOLUME_NORMAL,

	EMUCMD_AVI_RECORD_AS,
	EMUCMD_AVI_STOP,

	EMUCMD_FDS_EJECT_INSERT,
	EMUCMD_FDS_SIDE_SELECT,

	EMUCMD_VSUNI_COIN,
	EMUCMD_VSUNI_TOGGLE_DIP_0,
	EMUCMD_VSUNI_TOGGLE_DIP_1,
	EMUCMD_VSUNI_TOGGLE_DIP_2,
	EMUCMD_VSUNI_TOGGLE_DIP_3,
	EMUCMD_VSUNI_TOGGLE_DIP_4,
	EMUCMD_VSUNI_TOGGLE_DIP_5,
	EMUCMD_VSUNI_TOGGLE_DIP_6,
	EMUCMD_VSUNI_TOGGLE_DIP_7,
	EMUCMD_VSUNI_TOGGLE_DIP_8,
	EMUCMD_VSUNI_TOGGLE_DIP_9,
	EMUCMD_MISC_REWIND,
	EMUCMD_MISC_SHOWSTATES,

	EMUCMD_MAX
};

enum EMUCMDTYPE
{
	EMUCMDTYPE_MISC=0,
	EMUCMDTYPE_SPEED,
	EMUCMDTYPE_STATE,
	EMUCMDTYPE_MOVIE,
	EMUCMDTYPE_SOUND,
	EMUCMDTYPE_AVI,
	EMUCMDTYPE_FDS,
	EMUCMDTYPE_VSUNI,

	EMUCMDTYPE_MAX
};

extern const char* FCEUI_CommandTypeNames[];

typedef void EMUCMDFN(void);

struct EMUCMDTABLE
{
	int cmd;
	int type;
	EMUCMDFN* fn_on;
	EMUCMDFN* fn_off;
	int state;
	char* name;
};

extern struct EMUCMDTABLE FCEUI_CommandTable[];

#endif //_INPUT_H_

