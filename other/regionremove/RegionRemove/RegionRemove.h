/*

	RegionRemove - Plugin Filter for VirtualDub
    Copyright (C) 2001  Shaun Faulds

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	Acknowledgments:

	Chris Wojdon (LogoAway Filter)
	Although I do not believe I have taken any actual code from the
	LogoAway filter I used some of the ideas and initially the drive
	to produce the filter came from using the LogoAway filter.

	Avery Lee
	A lot of this code comes from the VirtualDub Filter SDK Tutorial written
	by Avery Lee.

*/

// Filter Data Structure to hold the config info
typedef struct RRFilterData {
	// Filter Data
	FilterActivation *fa;
	Pixel32* finalPix;
	int removeWidth;
	int removeHeight;
	int removeX;
	int removeY;
	bool border;
	int frameWidth;
	int frameHeight;

	bool ignoreTop;
	bool ignoreBottom;
	bool ignoreLeft;
	bool ignoreRight;

	bool softEdges;
	int softPixels;

	bool softONOFF;
	int softFRAME;

	char ranges[1024];
	long rangeStart[512];
	long rangeStop[512];
	int totalRanges;

} RRFilterData;

///////////////////////////////////////////////////////////////////////////
// Export functions 
///////////////////////////////////////////////////////////////////////////

int RRRunProc(const FilterActivation *fa, const FilterFunctions *ff);
int RRStartProc(FilterActivation *fa, const FilterFunctions *ff);
int RREndProc(FilterActivation *fa, const FilterFunctions *ff);
long RRParamProc(FilterActivation *fa, const FilterFunctions *ff);
int RRConfigProc(FilterActivation *fa, const FilterFunctions *ff, HWND hwnd);
void RRStringProc(const FilterActivation *fa, const FilterFunctions *ff, char *str);
void RRScriptConfig(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc);
bool RRFssProc(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen);
int RRInitProc(FilterActivation *fa, const FilterFunctions *ff);
//void RRDeinitProc(FilterActivation *fa, const FilterFunctions *ff);


void SetupPropDlg(HWND hdlg, RRFilterData *mfd);
Pixel32 *GetDstPixelAddress( const FilterActivation *fa, int x, int y );
void CalculateXYintoperlation(RRFilterData *mfd);
void DialogChangeEvents(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam, RRFilterData *mfd);
void SetupSpinMaxMin(HWND hdlg, RRFilterData *mfd);
void setRanges(RRFilterData *mfd, HWND hdlg, char data[]);
bool int2bool(int value);

