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

#include <windows.h>
#include <stdio.h>
#include <commctrl.h>

#include "ScriptInterpreter.h"
#include "ScriptError.h"
#include "ScriptValue.h"

#include "resource.h"
#include "filter.h"

#include "RegionRemove.h"

///////////////////////////////////////////////////////////////////////////
// Script support data structures 
///////////////////////////////////////////////////////////////////////////
ScriptFunctionDef RR_func_defs[]={
    { (ScriptFunctionPtr)RRScriptConfig, "Config", "0iiiiiiiiiiiis" },
    { NULL },
};

CScriptObject RR_obj={
    NULL, RR_func_defs
};

///////////////////////////////////////////////////////////////////////////
// Describe the filter interfaces
///////////////////////////////////////////////////////////////////////////
struct FilterDefinition filterDef_RR = {

	NULL, NULL, NULL,		// next, prev, module
	"Region Remove",	// name
    "Region Remove\nVersion 1.1\nRemoves a region of video and replaces it with re-calculated pixels.",
							// desc
    "Shaun Faulds",			// maker
    NULL,					// private_data
    sizeof(RRFilterData),	// inst_data_size

    RRInitProc,				// initProc
    NULL,//RRDeinitProc,			// deinitProc
    RRRunProc,				// runProc
    RRParamProc,			// paramProc
    RRConfigProc,			// configProc
    RRStringProc,			// stringProc
    RRStartProc,			// startProc
    RREndProc,				// endProc

    &RR_obj,				// script_obj
    RRFssProc,				// fssProc

};

///////////////////////////////////////////////////////////////////////////
// Stuff for VirtualDub to use the filter
///////////////////////////////////////////////////////////////////////////
extern "C" int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat);
extern "C" void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff);
static FilterDefinition *fd_RR;
int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat) {
    if (!(fd_RR = ff->addFilter(fm, &filterDef_RR, sizeof(FilterDefinition))))
        return 1;

	vdfd_ver = VIRTUALDUB_FILTERDEF_VERSION;
	vdfd_compat = VIRTUALDUB_FILTERDEF_COMPATIBLE;

    return 0;
}
void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff) {
    ff->removeFilter(fd_RR);
}

///////////////////////////////////////////////////////////////////////////
// Start and End functions
///////////////////////////////////////////////////////////////////////////
int RRStartProc(FilterActivation *fa, const FilterFunctions *ff) {

	RRFilterData *mfd = (RRFilterData *)fa->filter_data;
	mfd->frameHeight = fa->src.h;
    mfd->frameWidth = fa->src.w;
    return 0;
}
int RREndProc(FilterActivation *fa, const FilterFunctions *ff) {
	
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// Initialisation and deinitialisation functions
///////////////////////////////////////////////////////////////////////////
int RRInitProc(FilterActivation *fa, const FilterFunctions *ff)
{
    RRFilterData *mfd = (RRFilterData *)fa->filter_data;

	mfd->fa = fa;

	mfd->removeWidth = 1;
	mfd->removeHeight = 1;
	mfd->removeX = 0;
	mfd->removeY = 0;
	mfd->border = false;

	mfd->ignoreTop = false;
	mfd->ignoreBottom = false;
	mfd->ignoreLeft = false;
	mfd->ignoreRight = false;

	mfd->softEdges = false;
	mfd->softPixels = 10;

	mfd->softFRAME = 5;
	mfd->softONOFF = false;

	mfd->totalRanges = 0;

	return 0;
}

//void RRDeinitProc(FilterActivation *fa, const FilterFunctions *ff)
//{

//}

///////////////////////////////////////////////////////////////////////////
// Main process function that is called for each frame
///////////////////////////////////////////////////////////////////////////
int RRRunProc(const FilterActivation *fa, const FilterFunctions *ff)
{

    RRFilterData *mfd = NULL;
	mfd = (RRFilterData *)fa->filter_data;

	// Set the frame width and height
	mfd->frameHeight = mfd->fa->src.h;
	mfd->frameWidth = mfd->fa->src.w;

	PixDim h = fa->src.h;
    PixDim w = fa->src.w;

	fa->dst.data = fa->src.data;

	// Check ranges and if not in a range do not process
	FilterStateInfo *fsi = (FilterStateInfo *)fa->pfsi;
	long frameNumber = fsi->lCurrentFrame;
	int frameAlpha = 0;
	if(mfd->totalRanges > 0)
	{
		for(int x=0; x < mfd->totalRanges; x++)
		{
			if(frameNumber > mfd->rangeStart[x] && frameNumber < mfd->rangeStop[x])
			{
				frameAlpha = 255;
				break;
			}
			if(frameNumber > (mfd->rangeStart[x] - mfd->softFRAME) && frameNumber <= mfd->rangeStart[x])
			{
				frameAlpha = ((frameNumber - (mfd->rangeStart[x] - mfd->softFRAME)) * 255) / mfd->softFRAME;
				break;
			}
			if(frameNumber >= mfd->rangeStop[x] && frameNumber < (mfd->rangeStop[x] + mfd->softFRAME))
			{
				frameAlpha = (((mfd->rangeStop[x] + mfd->softFRAME) - frameNumber) * 255) / mfd->softFRAME;
				break;
			}
		}
	}
	else
	{
		frameAlpha = 255;
	}

	//If alpha is set to "0" then do nothing and return
	if(frameAlpha == 0) return 0;

	// OK now do the region remove stuff
	mfd->finalPix  = new Pixel32[mfd->removeWidth * mfd->removeHeight];

	int counter = 0;
	for(int y=mfd->removeY; y < (mfd->removeY + mfd->removeHeight); y++)
	{
		for(int x=mfd->removeX; x < (mfd->removeX + mfd->removeWidth); x++)
		{
			Pixel32 *pixel = GetDstPixelAddress(fa, x, y);
			mfd->finalPix[counter++] = *pixel;
		}
	}

	CalculateXYintoperlation(mfd);

	counter = 0;
	int softEdge = mfd->softPixels;
	for(y=mfd->removeY; y < (mfd->removeY + mfd->removeHeight); y++)
	{
		int alphaY = 255;

		if(mfd->softEdges)
		{
			if(y-mfd->removeY < softEdge)
			{
				alphaY = ((y - mfd->removeY) * 255) / softEdge;
			}
			else if(y > ((mfd->removeY + mfd->removeHeight)-softEdge))
			{
				alphaY = (((mfd->removeY + mfd->removeHeight) - y) * 255) /softEdge;
			}
		}

		for(int x=mfd->removeX; x < (mfd->removeX + mfd->removeWidth); x++)
		{
			int alphaX = 255;

			Pixel32 *pixel = GetDstPixelAddress(fa, x, y);
			Pixel32 oldPixel = *pixel;
			Pixel32 newPixel = mfd->finalPix[counter++];

			if(mfd->softEdges)
			{
				if(x-mfd->removeX < softEdge)
				{
					alphaX = ((x - mfd->removeX) * 255) / softEdge;
				}
				else if(x > ((mfd->removeX + mfd->removeWidth)-softEdge))
				{
					alphaX = (((mfd->removeX + mfd->removeWidth) - x) * 255) / softEdge;
				}
			}

			if(mfd->softEdges || mfd->softONOFF)
			{
				int alpha = 255;

				if(mfd->softEdges)
					alpha = alpha - ((255 - alphaY) + (255 - alphaX));

				if(mfd->softONOFF)
					alpha = alpha - (255 - frameAlpha);

				if(alpha > 255) alpha = 255;
				if(alpha < 0) alpha = 0;

				int redOld = (oldPixel & 0x00FF0000) >> 16;
				int greenOld = (oldPixel & 0x0000FF00) >> 8;
				int blueOld = (oldPixel & 0x000000FF);

				int redNew = (newPixel & 0x00FF0000) >> 16;
				int greenNew = (newPixel & 0x0000FF00) >> 8;
				int blueNew = (newPixel & 0x000000FF);

				int redFinal = ((redOld * (255-alpha)) / 255) + ((redNew * alpha) / 255);
				int greenFinal = ((greenOld * (255-alpha)) / 255) + ((greenNew * alpha) / 255); 
				int blueFinal = ((blueOld * (255-alpha)) / 255) + ((blueNew * alpha) / 255);

				int final = redFinal << 16 | greenFinal << 8 | blueFinal;

				*pixel = final;
			}
			else
				*pixel = newPixel;
		}
	}

	delete[] mfd->finalPix;
	mfd->finalPix = NULL;

    return 0;
}

///////////////////////////////////////////////////////////////////////////
// Returns the address of a pixel at xy location starting at top left
///////////////////////////////////////////////////////////////////////////
Pixel32 *GetDstPixelAddress( const FilterActivation *fa, int x, int y )
{
	return (Pixel32*)(((char*)(fa->dst.data + x )) + fa->src.pitch * (fa->src.h - (y+1)));
}

///////////////////////////////////////////////////////////////////////////
// Calcuate the XYintoperlation
///////////////////////////////////////////////////////////////////////////
void CalculateXYintoperlation(RRFilterData *mfd)
{
	int counter = 0;
	int width = mfd->removeWidth;
	int height = mfd->removeHeight;
	int leftPixel;
	int rightPixel;
	int topPixel;
	int bottomPixel;

	for(int y=0; y < height; y++)
	{
		//Work out y Stuff
		//This ignores one side or the other
		if(mfd->ignoreLeft)
			leftPixel = width+(y*width)-1;
		else
			leftPixel = (width*y)+1;
		if(mfd->ignoreRight)
			rightPixel = (width*y)+1;
		else
			rightPixel = width+(y*width)-1;
		
		int redDeltaYLeft = (mfd->finalPix[leftPixel] & 0x00FF0000) >> 16;
		int greenDeltaYLeft = (mfd->finalPix[leftPixel] & 0x0000FF00) >> 8;
		int blueDeltaYLeft = (mfd->finalPix[leftPixel] & 0x000000FF);

		int redDeltaYRight = (mfd->finalPix[rightPixel] & 0x00FF0000) >> 16;
		int greenDeltaYRight = (mfd->finalPix[rightPixel] & 0x0000FF00) >> 8;
		int blueDeltaYRight = (mfd->finalPix[rightPixel] & 0x000000FF);

		int redDeltaY = redDeltaYRight - redDeltaYLeft;
		int greenDeltaY = greenDeltaYRight - greenDeltaYLeft;
		int blueDeltaY = blueDeltaYRight - blueDeltaYLeft;

		for(int x=0; x < width; x++)
		{
			int redDeltaYFinal = redDeltaYLeft + ((redDeltaY * x) / width);
			int	greenDeltaYFinal = greenDeltaYLeft + ((greenDeltaY * x) / width);
			int blueDeltaYFinal = blueDeltaYLeft + ((blueDeltaY * x) / width);

			//Work out X stuff
			//This ignores top or bottom
			if(mfd->ignoreTop)
				topPixel = x + ((height-1) * width);
			else
				topPixel = x;
			if(mfd->ignoreBottom)
				bottomPixel = x;
			else
				bottomPixel = x + ((height-1) * width);

			int redDeltaXTop = (mfd->finalPix[topPixel] & 0x00FF0000) >> 16;
			int greenDeltaXTop = (mfd->finalPix[topPixel] & 0x0000FF00) >> 8;
			int blueDeltaXTop = (mfd->finalPix[topPixel] & 0x000000FF);

			int redDeltaXBottom = (mfd->finalPix[bottomPixel] & 0x00FF0000) >> 16;
			int greenDeltaXBottom = (mfd->finalPix[bottomPixel] & 0x0000FF00) >> 8;
			int blueDeltaXBottom = (mfd->finalPix[bottomPixel] & 0x000000FF);

			int redDeltaX = redDeltaXBottom - redDeltaXTop;
			int greenDeltaX = greenDeltaXBottom - greenDeltaXTop;
			int blueDeltaX = blueDeltaXBottom - blueDeltaXTop;

			int redDeltaXFinal = redDeltaXTop + ((redDeltaX * y) / height);
			int greenDeltaXFinal = greenDeltaXTop + ((greenDeltaX * y) / height);
			int blueDeltaXFinal = blueDeltaXTop + ((blueDeltaX * y) / height);

			int finalRed = ((redDeltaXFinal * 50) / 100) + ((redDeltaYFinal * 50) / 100);
			int finalGreen = ((greenDeltaXFinal * 50) / 100) + ((greenDeltaYFinal * 50) / 100);
			int finalBlue = ((blueDeltaXFinal * 50) / 100) + ((blueDeltaYFinal * 50) / 100);

			int final = finalRed << 16 | finalGreen << 8 | finalBlue;

			mfd->finalPix[counter++] = final;
		}
	}

	// If required add borders to the pixel array
	if(mfd->border)
	{
		for(int x=0; x < width; x++)
			if((x)%2 == 1)
				mfd->finalPix[x] = 0X00000000;
			else 
				mfd->finalPix[x] = 0Xffffffff;

		for(x=0; x < width; x++)
			if((x)%2 == 1)
				mfd->finalPix[x + ((height-1) * width)] = 0X00000000;
			else 
				mfd->finalPix[x + ((height-1) * width)] = 0Xffffffff;

		for(x=0; x < height; x++)
			if((x)%2 == 1)
				mfd->finalPix[x * width] = 0X00000000;
			else 
				mfd->finalPix[x * width] = 0Xffffffff;
			
		for(x=0; x < height; x++)
			if((x)%2 == 1)
				mfd->finalPix[(x * width) + (width-1)] = 0X00000000;
			else 
				mfd->finalPix[(x * width) + (width-1)] = 0Xffffffff;
	}
}

long RRParamProc(FilterActivation *fa, const FilterFunctions *ff) {

	//do the pixel operation in place 
	fa->dst.offset = fa->src.offset;
	return 0;
}

///////////////////////////////////////////////////////////////////////////
// Call-back event handler for the dialog properties box
///////////////////////////////////////////////////////////////////////////
BOOL CALLBACK RRConfigDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {

    RRFilterData *mfd = (RRFilterData *)GetWindowLong(hdlg, DWL_USER);

	switch(msg)
	{
		case WM_CLOSE:
			break;

		case WM_INITDIALOG:
			mfd = (RRFilterData *)lParam;
            SetWindowLong(hdlg, DWL_USER, lParam);
			SetupPropDlg(hdlg, mfd);
            return TRUE;

		case WM_HSCROLL:
			break;

		case WM_COMMAND:
			switch(HIWORD(wParam))
			{
	  			case EN_CHANGE:
					DialogChangeEvents(hdlg, msg, wParam, lParam, mfd);
					break;

	   			case CBN_SELCHANGE:
	      			break;

	   			case BN_CLICKED:
	    			switch( LOWORD(wParam) )
	     			{
						case IDC_SOFT_ONOFF:
							mfd->softONOFF = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_SOFT_EDGES:
							mfd->softEdges = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_IGNORE_TOP:
							mfd->ignoreTop = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_IGNORE_BOTTOM:
							mfd->ignoreBottom = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_IGNORE_LEFT:
							mfd->ignoreLeft = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_IGNORE_RIGHT:
							mfd->ignoreRight = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_BORDER:
							mfd->border = (SendMessage( (HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDC_PREVIEW:
							mfd->fa->ifp->Toggle((HWND)lParam);
							break;

						case IDC_UPDATE_ONOFF:
							setRanges(mfd, hdlg, NULL);
							mfd->fa->ifp->RedoFrame();
							break;

						case IDOK:
							mfd->border = false;
							EndDialog(hdlg, 0);
							return TRUE;
				}
		}
	}

    return FALSE;
}

///////////////////////////////////////////////////////////////////////////
// Handle the ONchange Events from the Dialog
///////////////////////////////////////////////////////////////////////////
void DialogChangeEvents(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam, RRFilterData *mfd) {

	if(mfd == NULL) return;
	int newValue;

	switch(LOWORD(wParam))
	{

		case IDC_SOFT_FRAMES:
			newValue = GetDlgItemInt( hdlg, IDC_SOFT_FRAMES, NULL, FALSE );

			if(newValue > 50)
				newValue = 50;
			if(newValue < 0)
				newValue = 0;

			mfd->softFRAME = newValue;
			mfd->fa->ifp->RedoFrame();
			break;

		case IDC_SOFT_PIX:
			newValue = GetDlgItemInt( hdlg, IDC_SOFT_PIX, NULL, FALSE );

			if(newValue > 50)
				newValue = 50;
			if(newValue < 0)
				newValue = 0;

			mfd->softPixels = newValue;
			mfd->fa->ifp->RedoFrame();
			break;

		case IDC_STARTX:
			newValue = GetDlgItemInt( hdlg, IDC_STARTX, NULL, FALSE );

			if(newValue > (mfd->frameWidth - mfd->removeWidth))
				newValue = mfd->frameWidth - mfd->removeWidth;
			if(newValue < 0)
				newValue = 0;
			
			mfd->removeX = newValue;
			SetupSpinMaxMin(hdlg, mfd);
			mfd->fa->ifp->RedoFrame();
			break;

		case IDC_STARTY:
			newValue = GetDlgItemInt( hdlg, IDC_STARTY, NULL, FALSE );

			if(newValue > (mfd->frameHeight - mfd->removeHeight))
				newValue = mfd->frameHeight - mfd->removeHeight;
			if(newValue < 0)
				newValue = 0;

			mfd->removeY = newValue;
			SetupSpinMaxMin(hdlg, mfd);
			mfd->fa->ifp->RedoFrame();
			break;	
							
		case IDC_WIDTH:
			newValue = GetDlgItemInt( hdlg, IDC_WIDTH, NULL, FALSE );

			if(newValue > (mfd->frameWidth - mfd->removeX))
				newValue = mfd->frameWidth - mfd->removeX;
			if(newValue < 1)
				newValue = 1;

			mfd->removeWidth = newValue;
			SetupSpinMaxMin(hdlg, mfd);
			mfd->fa->ifp->RedoFrame();
			break;	

		case IDC_HEIGHT:
			newValue = GetDlgItemInt( hdlg, IDC_HEIGHT, NULL, FALSE );

			if(newValue > (mfd->frameHeight - mfd->removeY))
				newValue = mfd->frameHeight - mfd->removeY;
			if(newValue < 1)
				newValue = 1;

			mfd->removeHeight = newValue;
			SetupSpinMaxMin(hdlg, mfd);
			mfd->fa->ifp->RedoFrame();
			break;						
	}

}

///////////////////////////////////////////////////////////////////////////
// Setup the Max Min on all the Spin controls
///////////////////////////////////////////////////////////////////////////
void SetupSpinMaxMin(HWND hdlg, RRFilterData *mfd) {

	HWND hwndCtl;
	hwndCtl = GetDlgItem( hdlg, IDC_SPINWIDTH );
	SendMessage(hwndCtl, UDM_SETRANGE, 0, MAKELPARAM((mfd->frameWidth - mfd->removeX), 1));// 0<<16 | (mfd->frameWidth - mfd->removeX));

	hwndCtl = GetDlgItem( hdlg, IDC_SPINHEIGHT );
	SendMessage(hwndCtl, UDM_SETRANGE, 0, MAKELPARAM(1, (mfd->frameHeight - mfd->removeY)));//0<<16 | (mfd->frameHeight - mfd->removeY));

	hwndCtl = GetDlgItem( hdlg, IDC_SPINX );
	SendMessage(hwndCtl, UDM_SETRANGE, 0, MAKELPARAM((mfd->frameWidth - mfd->removeWidth), 0));

	hwndCtl = GetDlgItem( hdlg, IDC_SPINY );
	SendMessage(hwndCtl, UDM_SETRANGE, 0, MAKELPARAM(0, (mfd->frameHeight - mfd->removeHeight)));

	hwndCtl = GetDlgItem( hdlg, IDC_SPIN_SOFT );
	SendMessage(hwndCtl, UDM_SETRANGE, 0, MAKELPARAM(50, 0));

	hwndCtl = GetDlgItem( hdlg, IDC_SPINONOFF );
	SendMessage(hwndCtl, UDM_SETRANGE, 0, MAKELPARAM(50, 0));
}

///////////////////////////////////////////////////////////////////////////
// Setup the Properties Dialog Box 
///////////////////////////////////////////////////////////////////////////
void SetupPropDlg(HWND hdlg, RRFilterData *mfd) {

	// Set the frame width and height
	mfd->frameHeight = mfd->fa->src.h;
	mfd->frameWidth = mfd->fa->src.w;

	// If remove location and size is not set set it to some values now.
	if(mfd->removeWidth == 1 && mfd->removeHeight == 1 && mfd->removeX == 0 && mfd->removeY == 0)
	{
		mfd->removeWidth = mfd->frameWidth/5;
		mfd->removeHeight = mfd->frameHeight/5;
		mfd->removeX = (mfd->frameWidth/2) - (mfd->removeWidth/2);
		mfd->removeY = (mfd->frameHeight/2) - (mfd->removeHeight/2);
		//mfd->border = false;
	}

	char buffer[16];

	SetupSpinMaxMin(hdlg, mfd);

	sprintf(buffer, "%d", mfd->removeX );
	SetDlgItemText( hdlg, IDC_STARTX, buffer );

	sprintf(buffer, "%d", mfd->removeY );
	SetDlgItemText( hdlg, IDC_STARTY, buffer );

	sprintf(buffer, "%d", mfd->removeWidth );
	SetDlgItemText( hdlg, IDC_WIDTH, buffer );

	sprintf(buffer, "%d", mfd->removeHeight );
	SetDlgItemText( hdlg, IDC_HEIGHT, buffer );

	sprintf(buffer, "%d", mfd->softPixels );
	SetDlgItemText( hdlg, IDC_SOFT_PIX, buffer );

	sprintf(buffer, "%d", mfd->softFRAME );
	SetDlgItemText( hdlg, IDC_SOFT_FRAMES, buffer );

	CheckDlgButton(hdlg, IDC_SOFT_EDGES, mfd->softEdges?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hdlg, IDC_SOFT_ONOFF, mfd->softONOFF?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hdlg, IDC_BORDER, mfd->border?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hdlg, IDC_IGNORE_TOP, mfd->ignoreTop?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hdlg, IDC_IGNORE_BOTTOM, mfd->ignoreBottom?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hdlg, IDC_IGNORE_LEFT, mfd->ignoreLeft?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hdlg, IDC_IGNORE_RIGHT, mfd->ignoreRight?BST_CHECKED:BST_UNCHECKED);

	mfd->fa->ifp->InitButton(GetDlgItem( hdlg, IDC_PREVIEW ));

	SetDlgItemText( hdlg, IDC_RANGE_LIST, mfd->ranges );
}

int RRConfigProc(FilterActivation *fa, const FilterFunctions *ff, HWND hwnd)
{

	RRFilterData *mfd = (RRFilterData *) fa->filter_data;
	mfd->fa = fa;

    return DialogBoxParam(fa->filter->module->hInstModule,
            MAKEINTRESOURCE(IDD_FILTER_REGIONREMOVE), hwnd,
            RRConfigDlgProc, (LPARAM)fa->filter_data);
}

///////////////////////////////////////////////////////////////////////////
// Process the String for display in the filter chain box
///////////////////////////////////////////////////////////////////////////
void RRStringProc(const FilterActivation *fa, const FilterFunctions *ff, char *str) {
    
	RRFilterData *mfd = (RRFilterData *)fa->filter_data;
	wsprintf( str, " %dx%d @ %d,%d", mfd->removeHeight, mfd->removeWidth, mfd->removeX, mfd->removeY);
}

///////////////////////////////////////////////////////////////////////////
// Do the script store and load stuff
///////////////////////////////////////////////////////////////////////////
void RRScriptConfig(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc) {

    FilterActivation *fa = (FilterActivation *)lpVoid;
    RRFilterData *mfd = (RRFilterData *)fa->filter_data;

	mfd->removeWidth = argv[0].asInt();
	mfd->removeHeight = argv[1].asInt();
	mfd->removeX = argv[2].asInt();
	mfd->removeY = argv[3].asInt();
	mfd->softEdges = int2bool(argv[4].asInt());
	mfd->softONOFF = int2bool(argv[5].asInt());
	mfd->ignoreTop = int2bool(argv[6].asInt());
	mfd->ignoreBottom = int2bool(argv[7].asInt());
	mfd->ignoreLeft = int2bool(argv[8].asInt());
	mfd->ignoreRight = int2bool(argv[9].asInt());
	mfd->softPixels = argv[10].asInt();
	mfd->softFRAME = argv[11].asInt();

	setRanges( mfd, NULL, *argv[12].asString());
}

bool RRFssProc(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen) {

    RRFilterData *mfd = (RRFilterData *)fa->filter_data;

	char ranges[1024], *rangesSan = ranges;
	strcpy(ranges, mfd->ranges);
	while( *rangesSan++ != 0 )
	{
		if( *rangesSan=='\n')
			*rangesSan = ' ';
		if( *rangesSan=='\r')
			*rangesSan = ',';
	}

	_snprintf(buf, buflen, "Config(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, \"%s\")",
		mfd->removeWidth,
		mfd->removeHeight,
		mfd->removeX,
		mfd->removeY,
		mfd->softEdges?1:0,
		mfd->softONOFF?1:0,
		mfd->ignoreTop?1:0,
		mfd->ignoreBottom?1:0,
		mfd->ignoreLeft?1:0,
		mfd->ignoreRight?1:0,
		mfd->softPixels,
		mfd->softFRAME,
		ranges);

    return true;
}

bool int2bool(int value)
{
	if(value == 0)
		return false;
	else
		return true;
}

///////////////////////////////////////////////////////////////////////////
// Load and store ranges in Start and Stop arrays
///////////////////////////////////////////////////////////////////////////
void setRanges(RRFilterData *mfd, HWND hdlg, char data[])
{
	char test[1024] = "";
	if(data == NULL)
		GetDlgItemText( hdlg, IDC_RANGE_LIST, test, 1020 );
	else
		strcpy(test, data);

    int counter = 0;
	boolean type = true;
	long value = 0;
	char buf[1024] = "";
	char * pch;
	pch = strtok (test,", \r\n");
    while (pch != NULL)
    {
		value = atol(pch);
		if(type)
		{
			mfd->rangeStart[counter] = value;
		}
		else
		{
			if(value > mfd->rangeStart[counter])
			{
				mfd->rangeStop[counter] = value;
				counter++;
			}
		}

		type = !type;
		pch = strtok (NULL, ", \r\n");
    }
	mfd->totalRanges = counter;
    
	int messageLength = 0;
    for(int x=0; x < counter; x++)
    {
		messageLength = strlen(buf);
		sprintf(buf+messageLength, "%d %d\r\n", mfd->rangeStart[x], mfd->rangeStop[x]);
    }

    strcpy(mfd->ranges, buf);

	SetDlgItemText( hdlg, IDC_RANGE_LIST, mfd->ranges );

}

