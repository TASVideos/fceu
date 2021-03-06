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

/****************************************************************/
/*                        FCE Ultra                                */
/*                                                                */
/*        This file contains code to interface to the standard    */
/*        FCE Ultra configuration file saving/loading code.        */
/*                                                                */
/****************************************************************/

static CFGSTRUCT fceuconfig[]={

        ACS(rfiles[0]),
        ACS(rfiles[1]),
        ACS(rfiles[2]),
        ACS(rfiles[3]),
        ACS(rfiles[4]),
        ACS(rfiles[5]),
        ACS(rfiles[6]),
        ACS(rfiles[7]),
        ACS(rfiles[8]),
        ACS(rfiles[9]),

        ACS(rdirs[0]),
        ACS(rdirs[1]),
        ACS(rdirs[2]),
        ACS(rdirs[3]),
        ACS(rdirs[4]),
        ACS(rdirs[5]),
        ACS(rdirs[6]),
        ACS(rdirs[7]),
        ACS(rdirs[8]),
        ACS(rdirs[9]),

        AC(ntsccol),AC(ntsctint),AC(ntschue),

        NAC("palyo",palyo),
	NAC("genie",genie),
	NAC("fs",fullscreen),
	NAC("vgamode",vmod),
	NAC("sound",soundo),
        NAC("sicon",status_icon),

        ACS(gfsdir),

        NACS("odcheats",DOvers[0]),
        NACS("odmisc",DOvers[1]),
        NACS("odnonvol",DOvers[2]),
        NACS("odstates",DOvers[3]),
        NACS("odsnaps",DOvers[4]),
        NACS("odfdsrom",DOvers[5]),
        NACS("odmemw",DOvers[6]),
        NACS("odbbot",DOvers[7]),
        NACS("odbase",DOvers[8]),

        AC(winspecial),
        AC(winsizemulx),
        AC(winsizemuly),
        NAC("saspectw987",saspectw),
        NAC("saspecth987",saspecth),

        AC(soundrate),
        AC(soundbuftime),
        AC(soundoptions),
        AC(soundquality),
        AC(soundvolume),

        AC(goptions),
        NAC("eoptions",eoptions),
        NACA("cpalette",cpalette),

        NACA("InputType",UsrInputType),

        NAC("vmcx",vmodes[0].x),
        NAC("vmcy",vmodes[0].y),
        NAC("vmcb",vmodes[0].bpp),
        NAC("vmcf",vmodes[0].flags),
        NAC("vmcxs",vmodes[0].xscale),
        NAC("vmcys",vmodes[0].yscale),
        NAC("vmspecial",vmodes[0].special),

        NAC("srendline",srendlinen),
        NAC("erendline",erendlinen),
        NAC("srendlinep",srendlinep),
        NAC("erendlinep",erendlinep),

        AC(disvaccel),
        AC(winsync),
        NAC("988fssync",fssync),

        AC(ismaximized),
        AC(maxconbskip),
        AC(ffbskip),

        ADDCFGSTRUCT(NetplayConfig),
        ADDCFGSTRUCT(InputConfig),
        ADDCFGSTRUCT(HotkeyConfig),

        AC(moviereadonly),
	AC(autoHoldKey),
	AC(autoHoldClearKey),
	AC(frame_display),
	AC(input_display),
	ACS(MemWatchDir),
	ACS(BasicBotDir),
	AC(EnableBackgroundInput),
	AC(MemToolsUpdateSpeed),
	AC(EnablePauseAfterPlayback),
	AC(EnableStopMessageInAvi),
		ENDCFGSTRUCT
};

static void SaveConfig(char *filename)
{
        SaveFCEUConfig(filename,fceuconfig);
}

static void LoadConfig(char *filename)
{
        FCEUI_GetNTSCTH(&ntsctint,&ntschue);
        LoadFCEUConfig(filename,fceuconfig);
        FCEUI_SetNTSCTH(ntsccol,ntsctint,ntschue);
}

