/*  Blur Box - filter for VirtualDub
    Copyright (C) 2003,2004 Emiliano Ferrari   <macinapepe@tiscali.it>

    Box Blur code is part of VirtualDub
    Copyright (C) 1998,2001 Avery Lee

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA */

// Version 0.1 (10-10-2003) EF
// Version 1.1 (21-05-2004) EF

#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include "filter.h"
#include "ScriptValue.h"
#include "resource.h"

#define VLCN_PADCHANGE 2

bool FssProc(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen);
void StringProc(const FilterActivation *fa, const FilterFunctions *ff, char *str);
void ScriptConfig(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc);


int active = 0;

typedef struct
{
  NMHDR hdr;
  int dx,dy;
} NMVLPADCHANGE;

const char g_szPadControlName[]="bbPadControl";

typedef struct
{
  int sx,sy,ex,ey;
  bool capture;
} PadControlData;

inline void Clipping (int &dato,const int min,const int max)
{
	if (dato<min) dato=min; else if (dato>max) dato=max;
}

static LRESULT APIENTRY PadControlWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  PadControlData *pcd= (PadControlData *)GetWindowLong(hwnd, 0);

  switch(msg)
  {
    case WM_NCCREATE:
      if (!(pcd = new PadControlData)) return FALSE;
      memset(pcd,0,sizeof(PadControlData));
      pcd->capture= false;
      SetWindowLong(hwnd, 0, (LONG)pcd);
      return TRUE;

    case WM_CREATE:
    case WM_SIZE:
      break;

    case WM_DESTROY:
        delete pcd;
        SetWindowLong(hwnd, 0, 0);
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc;
        hdc= BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_LBUTTONDOWN:
        {
          unsigned short px= LOWORD(lParam);
          unsigned short py= HIWORD(lParam);
          pcd->sx= *((short *)&px);
          pcd->sy= *((short *)&py);
          pcd->capture= true;
          SetCapture(hwnd);
        }
       break;

    case WM_LBUTTONUP:
        if (pcd->capture)
        {
          ReleaseCapture();
          pcd->capture= false;
        }
        break;

    case WM_MOUSEMOVE:
       if (pcd->capture)
       {
          unsigned short px= LOWORD(lParam);
          unsigned short py= HIWORD(lParam);
          pcd->ex= *((short *)&px);
          pcd->ey= *((short *)&py);
          NMVLPADCHANGE nmvltc;
          nmvltc.hdr.code    = VLCN_PADCHANGE;
          nmvltc.hdr.hwndFrom= hwnd;
          nmvltc.hdr.idFrom  = GetWindowLong(hwnd, GWL_ID);
          nmvltc.dx= pcd->ex-pcd->sx;
          nmvltc.dy= pcd->ey-pcd->sy;
          SendMessage(GetParent(hwnd), WM_NOTIFY, nmvltc.hdr.idFrom, (LPARAM)&nmvltc);
          pcd->sx= pcd->ex;
          pcd->sy= pcd->ey;
        }
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return FALSE;
}

void RegisterPadControl(HINSTANCE hinst)
{

  WNDCLASS wc;
  wc.style= 0;
  wc.lpfnWndProc= PadControlWndProc;
  wc.cbClsExtra= 0;
  wc.cbWndExtra= sizeof(PadControlData *);
  wc.hInstance= hinst;
  wc.hIcon= NULL;
  wc.hCursor= LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground= (HBRUSH)(COLOR_3DFACE);
  wc.lpszMenuName= NULL;
  wc.lpszClassName= g_szPadControlName;
  RegisterClass(&wc);
}

#define MAX_KEYFRAMES 1000


typedef unsigned short Pixel16;

typedef struct panStep {
	int frameNumber;
	POINT ptUL;
	POINT ptSize;
	int kstrength;
	int mode;
} panStep;


typedef struct
{
	struct panStep **stepData;
	int stepCount;
	panStep prevStep;
	panStep nextStep;
	panStep nxnextStep;
	long saveFrame;
	int mode;
	char* cstring;
	float asp;
	int run;
  Pixel32 *rows;
  Pixel16 *trow;
  HWND dlgHwnd, lbHwnd;
  int filter_width;
  int  x,y,w,h;
//  KEYFRAME keys;
  IFilterPreview *ifp;
} MyFilterData;

//MyFilterData *tmpfd;


int smoothit (int pos[], int val[], int degree, int desiredPos)  { 
   float retVal = 0; 
 
   for (int i = 0; i < degree; ++i) { 
      float weight = 1; 
 
      for (int j = 0; j < degree; ++j) {
         // The i-th term has to be skipped
         if (j != i) {
            weight *= ((float)desiredPos - (float)pos[j]) / ((float)pos[i] - (float)pos[j]);
         }
      }
 
      retVal += weight * (float)val[i]; 
   } 
 
   return (int)retVal; 
}

BOOL insertStep(void *filterData, int frame, int x, int y, int mode, int w, int h, int fw) {
    MyFilterData *mfd = (MyFilterData *)filterData;

	int step = 0;
	unsigned char shift = 0;
	struct panStep **n = (panStep **)malloc((sizeof(struct panStep *) * (mfd->stepCount + 1)));
	struct panStep *nstep = (struct panStep *)malloc(sizeof(struct panStep));



//	if (frame < 1)
//		return FALSE;

	mfd->stepCount++;

	if (mfd->stepCount == 1) {
		nstep->frameNumber = frame;
		nstep->ptUL.x = x;
		nstep->ptUL.y = y;
		nstep->mode = mode;
		nstep->ptSize.x = w;
		nstep->ptSize.y = h;
		nstep->kstrength = fw;
		n[0] = nstep;
	} else {
		for (int i = 0; i < mfd->stepCount - 1; i++)
			if (frame == mfd->stepData[i]->frameNumber) {
				mfd->stepCount--;
				return FALSE;
			}
		while (step < mfd->stepCount) {
			if (!shift && ((step == mfd->stepCount - 1) 
				|| (mfd->stepData[step]->frameNumber > frame))) {

				nstep->frameNumber = frame;
				nstep->ptUL.x = x;
				nstep->ptUL.y = y;
				nstep->mode = mode;
				nstep->ptSize.x = w;
				nstep->ptSize.y = h;
				nstep->kstrength = fw;
				n[step] = nstep;
				shift = 1;
			} else {
				n[step] = mfd->stepData[step - shift];
			}
			step++;
		}
	}
	if (mfd->stepData)
		free(mfd->stepData);
	mfd->stepData = n;
	return TRUE;
}


void deleteFaderStep(void *filterData, int index) {
    MyFilterData *mfd = (MyFilterData *)filterData;
	if (index == 0) return;
	
	struct panStep **n = (panStep **)malloc((sizeof(struct panStep *) * (mfd->stepCount - 1)));
	int shift = 0;
	for (int i = 0; i < mfd->stepCount - 1; i++) {
		if (index == i)
			shift = 1;
		n[i] = mfd->stepData[i + shift];
	}
	mfd->stepCount--;
	free(mfd->stepData);
	mfd->stepData = n;
//	for(i = 0; i < mfd->stepCount; i++) {
//		if(mfd->saveFrame >= mfd->stepData[i]->frameNumber) {
//			mfd->x = mfd->stepData[i]->ptUL.x;
//			mfd->y = mfd->stepData[i]->ptUL.y;
//		}
//	}
/*	if(mfd->saveFrame > mfd->stepData[mfd->stepCount - 1]->frameNumber) {
			mfd->x = mfd->stepData[i]->ptUL.x;
			mfd->y = mfd->stepData[i]->ptUL.y;
	}*/
}




int setUpPan(const FilterActivation *fa) {
    MyFilterData *mfd = (MyFilterData *)fa->filter_data;

	int  i;



	//*** Catch invalid cases ***
	if(mfd->saveFrame > mfd->stepData[mfd->stepCount - 1]->frameNumber) {  //-- After last step
		mfd->x = mfd->stepData[mfd->stepCount - 1]->ptUL.x;
		mfd->y = mfd->stepData[mfd->stepCount - 1]->ptUL.y;
		mfd->w = mfd->stepData[mfd->stepCount - 1]->ptSize.x;
		mfd->h = mfd->stepData[mfd->stepCount - 1]->ptSize.y;
		mfd->filter_width = mfd->stepData[mfd->stepCount - 1]->kstrength;
	//	if(mfd->stepCount == 1) {
	//		mfd->x = mfd->x;
	//		mfd->y = mfd->y;
	//	}
		return 0;
	}


	//--- Already set up ---
/*	if(mfd->saveFrame > mfd->prevStep.frameNumber 
		&& mfd->saveFrame <= mfd->nextStep.frameNumber
		&& mfd->saveFrame != 0) {
		return 1;
	}
*/

	//**** Find PrevStep/NextStep ****

	for( i=0;i<mfd->stepCount;i++) {
		if(mfd->stepData[i]->frameNumber >= mfd->saveFrame) {
			break;
		}
	}

	if(i > 0) {

		//*** Found a step before & after current frame
		mfd->prevStep.frameNumber = mfd->stepData[i-1]->frameNumber;
		mfd->prevStep.ptUL.x = mfd->stepData[i-1]->ptUL.x;
		mfd->prevStep.ptUL.y = mfd->stepData[i-1]->ptUL.y;
		mfd->prevStep.mode = mfd->stepData[i-1]->mode;
		mfd->prevStep.ptSize.x = mfd->stepData[i-1]->ptSize.x;
		mfd->prevStep.ptSize.y = mfd->stepData[i-1]->ptSize.y;
		mfd->prevStep.kstrength = mfd->stepData[i-1]->kstrength;
	
		mfd->nextStep.frameNumber = mfd->stepData[i]->frameNumber;
		mfd->nextStep.ptUL.x = mfd->stepData[i]->ptUL.x;
		mfd->nextStep.ptUL.y = mfd->stepData[i]->ptUL.y;
		mfd->nextStep.mode = mfd->stepData[i]->mode;
		mfd->nextStep.ptSize.x = mfd->stepData[i]->ptSize.x;
		mfd->nextStep.ptSize.y = mfd->stepData[i]->ptSize.y;
		mfd->nextStep.kstrength = mfd->stepData[i]->kstrength;
	} else {
		//*** Found a step after but not before so fake it
		mfd->prevStep.frameNumber = 0;
		mfd->prevStep.ptUL.x = mfd->stepData[0]->ptUL.x;
		mfd->prevStep.ptUL.y = mfd->stepData[0]->ptUL.y;
		mfd->prevStep.mode = 1;
		mfd->prevStep.ptSize.x = mfd->stepData[0]->ptSize.x;
		mfd->prevStep.ptSize.y = mfd->stepData[0]->ptSize.y;
		mfd->prevStep.kstrength = mfd->stepData[0]->kstrength;
	
		mfd->nextStep.frameNumber = mfd->stepData[i]->frameNumber;
		mfd->nextStep.ptUL.x = mfd->stepData[i]->ptUL.x;
		mfd->nextStep.ptUL.y = mfd->stepData[i]->ptUL.y;
		mfd->nextStep.mode = mfd->stepData[i]->mode;
		mfd->nextStep.ptSize.x = mfd->stepData[i]->ptSize.x;
		mfd->nextStep.ptSize.y = mfd->stepData[i]->ptSize.y;
		mfd->nextStep.kstrength = mfd->stepData[i]->kstrength;
	}

	return 1;

}



void box_filter_mult_row(Pixel16 *dst, Pixel32 *src, int cnt, int mult)
{
        __asm {
            mov         eax,src
            movd        mm6,mult
            mov         edx,dst
            punpcklwd   mm6,mm6
            mov         ecx,cnt
            punpckldq   mm6,mm6
            pxor        mm7,mm7
            lea         eax,[eax+ecx*4]
            lea         edx,[edx+ecx*8]
            neg         ecx
xloop:      movd        mm0,[eax+ecx*4]
            punpcklbw   mm0,mm7
            pmullw      mm0,mm6
            movq        [edx+ecx*8],mm0
            inc         ecx
            jne         xloop
            emms
        }
}

void box_filter_add_row(Pixel16 *dst, Pixel32 *src, int cnt)
{
  __asm {   mov       eax,src
            mov       edx,dst
            mov       ecx,cnt
            pxor      mm7,mm7
            lea       eax,[eax+ecx*4]
            lea       edx,[edx+ecx*8]
            neg       ecx
            align     16
xloop:      movd      mm0,[eax+ecx*4]
            punpcklbw mm0,mm7
            paddw     mm0,[edx+ecx*8]
            movq      [edx+ecx*8],mm0
            inc       ecx
            jne       xloop
            emms
        }
}

static void box_filter_produce_row(Pixel32 *dst, Pixel16 *tmp, Pixel32 *src_add, Pixel32 *src_sub, int cnt, int filter_width)
{
  int mult = 0x10000 / (2*filter_width+1);

   __asm {
      movd      mm6,mult
      punpcklwd mm6,mm6
      punpckldq mm6,mm6
      mov       eax,src_add
      mov       edx,tmp
      mov       ecx,cnt
      mov       ebx,src_sub
      mov       edi,dst
      pxor      mm7,mm7
      lea       edx,[edx+ecx*8]
      lea       eax,[eax+ecx*4]
      lea       ebx,[ebx+ecx*4]
      lea       edi,[edi+ecx*4]
      neg       ecx
      align     16
xloop:movq      mm0,[edx+ecx*8]
      movq      mm1,mm0
      pmulhw    mm0,mm6
      packuswb  mm0,mm0
      movd      [edi+ecx*4],mm0
      movd      mm2,[eax+ecx*4]
      punpcklbw mm2,mm7
      movd      mm3,[ebx+ecx*4]
      punpcklbw mm3,mm7
      paddw     mm2,mm1
      psubw     mm2,mm3
      movq      [edx+ecx*8],mm2
      inc       ecx
      jne       xloop
      emms
        }

}

static void box_filter_produce_row2(Pixel32 *dst, Pixel16 *tmp, int cnt, int filter_width)
{
  int mult = 0x10000 / (2*filter_width+1);

  __asm {

      mov        esi,[tmp]    // sorgente valori temporanei
      mov        edi,[dst]    // destinazione
      mov        ecx,[cnt]    // contatore

      lea        esi,[esi+ecx*8]
      lea        edi,[edi+ecx*4]
      neg        ecx

      movd       mm6,[mult]  // 16 bit  0000 0000 : 0000 mmmm
      punpcklwd  mm6,mm6
      punpckldq  mm6,mm6

      align      16
xloop:movq       mm0,[esi+ecx*8]    // legge 1 pixel  xxxx bbbb gggg rrrr
      pmulhw     mm0,mm6            // moltiplica per valore a 16 bit
      packuswb   mm0,mm0            // xxbbggrr:xxbbggrr
      movd       [edi+ecx*4],mm0 // write Pixel
      inc        ecx
      jnz        xloop
      emms
  }
}

void box_do_vertical_pass(Pixel32 *dst, PixOffset dstpitch, Pixel32 *src, PixOffset srcpitch, Pixel16 *trow, int w, int h, int filtwidth)
{
  Pixel32 *srch = src;
  int j;

  box_filter_mult_row(trow, src, w, filtwidth + 1);
  src = (Pixel32 *)((char *)src + srcpitch);
  for(j=0; j<filtwidth; j++)
  {
    box_filter_add_row(trow, src, w);
    src = (Pixel32 *)((char *)src + srcpitch);
  }

  for(j=0; j<filtwidth; j++)
  {
    box_filter_produce_row(dst, trow, src, srch, w, filtwidth);
    src = (Pixel32 *)((char *)src + srcpitch);
    dst = (Pixel32 *)((char *)dst + dstpitch);
  }

  for(j=0; j<h - (2*filtwidth+1); j++)
  {
    box_filter_produce_row(dst, trow, src, (Pixel32 *)((char *)src - srcpitch*(2*filtwidth+1)), w, filtwidth);
    src = (Pixel32 *)((char *)src + srcpitch);
    dst = (Pixel32 *)((char *)dst + dstpitch);
  }

  srch = (Pixel32 *)((char *)src - srcpitch);

  for(j=0; j<filtwidth; j++)
  {
    box_filter_produce_row(dst, trow, srch, (Pixel32 *)((char *)src - srcpitch*(2*filtwidth+1)), w, filtwidth);
    src = (Pixel32 *)((char *)src + srcpitch);
    dst = (Pixel32 *)((char *)dst + dstpitch);
   }

  box_filter_produce_row2(dst, trow, w, filtwidth);
}

static void __declspec(naked) box_filter_row_MMX(Pixel32 *dst, Pixel32 *src, int filtwidth, int cnt, int divisor)
{
    __asm {
        push        ebx

        mov         ecx,[esp+12+4]            ;ecx = filtwidth
        mov         eax,[esp+8+4]            ;eax = src
        movd        mm6,ecx
        pxor        mm7,mm7
        movd        mm5,[eax]                ;A = source pixel
        punpcklwd   mm6,mm6
        pcmpeqw     mm4,mm4                    ;mm4 = all -1's
        punpckldq   mm6,mm6
        psubw       mm6,mm4                    ;mm6 = filtwidth+1
        punpcklbw   mm5,mm7                    ;mm5 = src[0] (word)
        movq        mm0,mm5                    ;mm0 = A
        pmullw      mm5,mm6                    ;mm5 = src[0]*(filtwidth+1)
        add         eax,4                    ;next source pixel
xloop1:
        movd        mm1,[eax]                ;B = next source pixel
        pxor        mm7,mm7
        punpcklbw   mm1,mm7
        add         eax,4
        paddw       mm5,mm1
        dec         ecx
        jne         xloop1

        mov         ecx,[esp+12+4]            ;ecx = filtwidth
        movd        mm6,[esp+20+4]
        punpcklwd   mm6,mm6
        mov         edx,[esp+4+4]            ;edx = dst
        punpckldq   mm6,mm6
xloop2:
        movd        mm1,[eax]                ;B = next source pixel
        movq        mm2,mm5                    ;mm1 = accum

        pmulhw      mm2,mm6
        punpcklbw   mm1,mm7

        psubw       mm5,mm0                    ;accum -= A
        add         eax,4

        paddw       mm5,mm1                    ;accum += B
        add         edx,4

        packuswb    mm2,mm2
        dec         ecx

        movd        [edx-4],mm2
        jne         xloop2

        ;main loop.

        mov         ebx,[esp+12+4]            ;ebx = filtwidth
        mov         ecx,[esp+16+4]            ;ecx = cnt
        lea         ebx,[ebx+ebx+1]            ;ebx = 2*filtwidth+1
        sub         ecx,ebx                    ;ecx = cnt - (2*filtwidth+1)
        shl         ebx,2
        neg         ebx                        ;ebx = -4*(2*filtwidth+1)
xloop3:
        movd        mm0,[eax+ebx]            ;mm0 = A = src[-(2*filtwidth+1)]
        movq        mm2,mm5

        movd        mm1,[eax]                ;mm0 = B = src[0]
        pmulhw      mm2,mm6

        punpcklbw   mm0,mm7
        add         edx,4

        punpcklbw   mm1,mm7
        add         eax,4

        psubw       mm5,mm0                    ;accum -= A
        packuswb    mm2,mm2                    ;pack finished pixel

        paddw       mm5,mm1                    ;accum += B
        dec         ecx

        movd        [edx-4],mm2
        jne         xloop3

        ;finish up remaining pixels

        mov         ecx,[esp+12+4]            ;ecx = filtwidth
xloop4:
        movd        mm0,[eax+ebx]            ;mm0 = A = src[-(2*filtwidth+1)]
        movq        mm2,mm5

        pmulhw      mm2,mm6
        add         edx,4

        punpcklbw   mm0,mm7
        add         eax,4

        psubw       mm5,mm0                    ;accum -= A
        packuswb    mm2,mm2                    ;pack finished pixel

        paddw       mm5,mm1                    ;accum += B
        dec         ecx

        movd        [edx-4],mm2
        jne         xloop4

        pmulhw      mm5,mm6
        packuswb    mm5,mm5
        movd        [edx],mm5

        emms
        pop         ebx
        ret
    }
}

int RunProc(const FilterActivation *fa, const FilterFunctions *ff)
{
  MyFilterData *mfd= (MyFilterData *)fa->filter_data;
  const int w= fa->dst.w;
  const int h= fa->dst.h;
  float rat;
  Pixel32 *dst= fa->dst.data;
  Pixel32 *src= fa->src.data;
  const int spitch= fa->src.pitch>>2;
  const int dpitch= fa->dst.pitch>>2;
  int filtw;
  const int srcpitch= ((w+1)&-2)*4;
  const int dstpitch= fa->dst.pitch;
  Pixel32 *tmp= mfd->rows;
  POINT cur, size;
  cur.x = 0; cur.y=0; size.x = 0; size.y = 0;
  int strength;
  int fx = mfd->x;
  int fy = mfd->y;
  int fw = mfd->w;
  int fh = mfd->h;
  int fs = mfd->filter_width;
//  int t;
  int i, j, k, l, cnt;
  double ratio;

	int xdp[MAX_KEYFRAMES];
	int ydp[MAX_KEYFRAMES];
	int fdp[MAX_KEYFRAMES];
	int wdp[MAX_KEYFRAMES];
	int hdp[MAX_KEYFRAMES];
	//float sdp[MAX_KEYFRAMES];


  mfd->saveFrame = fa->pfsi->lCurrentFrame;

  if (!ff->isMMXEnabled) ff->Except ("MMX CPU Required");


  memcpy (dst,src,fa->dst.size);


  	if(mfd->stepCount > 1) {
		if(setUpPan(fa)) {
			ratio = (double)(mfd->saveFrame - mfd->prevStep.frameNumber) / (double)(mfd->nextStep.frameNumber - mfd->prevStep.frameNumber); 
			cur.x = int(mfd->prevStep.ptUL.x);
			cur.y = int(mfd->prevStep.ptUL.y);
			size.x = int(mfd->prevStep.ptSize.x);
			size.y = int(mfd->prevStep.ptSize.y);
			strength = int(mfd->prevStep.kstrength);
			switch(mfd->nextStep.mode) {
				case 1:
					break;

				case 2:
					if(mfd->prevStep.ptUL.x != mfd->nextStep.ptUL.x) {
						cur.x = int((mfd->prevStep.ptUL.x + (mfd->nextStep.ptUL.x - mfd->prevStep.ptUL.x) * ratio)+0.5);
						cur.y = int((mfd->prevStep.ptUL.y + (mfd->nextStep.ptUL.y - mfd->prevStep.ptUL.y) * ratio)+0.5);
					}
					if(mfd->prevStep.ptSize.x != mfd->nextStep.ptSize.x) {
						size.x = int((mfd->prevStep.ptSize.x + (mfd->nextStep.ptSize.x - mfd->prevStep.ptSize.x) * ratio)+0.5);
						size.y = int((mfd->prevStep.ptSize.y + (mfd->nextStep.ptSize.y - mfd->prevStep.ptSize.y) * ratio)+0.5);
					}
					if(mfd->prevStep.kstrength != mfd->nextStep.kstrength) {
						strength = int(((float)mfd->prevStep.kstrength + ((float)mfd->nextStep.kstrength - (float)mfd->prevStep.kstrength) * (float)ratio)+0.5);
					}
					break;

				case 3:
					for (i = 0; i < mfd->stepCount; i++) {
						if (mfd->stepData[i]->frameNumber >= mfd->saveFrame) break;
					}
					//char integ[512];
					//itoa(i, integ, 10);

					//MessageBox(GetParent(mfd->dlgHwnd),integ,"Debug",MB_OK);
					for (j=i; j >= 0; j = j - 1) {
						//itoa(j,integ,10);
						//MessageBox(GetParent(mfd->dlgHwnd),integ,"Debug",MB_OK);
						if (j == i - 2) break;
						if (mfd->stepData[j]->mode != 3) break;
					}
					for (k=i; k < mfd->stepCount - 1; k++) {
						if (k == i + 2) break;
						if (mfd->stepData[k]->mode != 3) break;
					}
					if(k - j < 2) {
						if(mfd->prevStep.ptUL.x != mfd->nextStep.ptUL.x) {
							cur.x = int((mfd->prevStep.ptUL.x + (mfd->nextStep.ptUL.x - mfd->prevStep.ptUL.x) * ratio)+0.5);
							cur.y = int((mfd->prevStep.ptUL.y + (mfd->nextStep.ptUL.y - mfd->prevStep.ptUL.y) * ratio)+0.5);
						}
						if(mfd->prevStep.ptSize.x != mfd->nextStep.ptSize.x) {
							size.x = int((mfd->prevStep.ptSize.x + (mfd->nextStep.ptSize.x - mfd->prevStep.ptSize.x) * ratio)+0.5);
							size.y = int((mfd->prevStep.ptSize.y + (mfd->nextStep.ptSize.y - mfd->prevStep.ptSize.y) * ratio)+0.5);
						}
						if(mfd->prevStep.kstrength != mfd->nextStep.kstrength) {
							strength = int(((float)mfd->prevStep.kstrength + ((float)mfd->nextStep.kstrength - (float)mfd->prevStep.kstrength) * (float)ratio)+0.5);
						}
						break;
					}

					cnt = 0;
					for (l = j; l <= k; l++) {
						xdp[cnt]=mfd->stepData[l]->ptUL.x;
						ydp[cnt]=mfd->stepData[l]->ptUL.y;
						fdp[cnt]=mfd->stepData[l]->frameNumber;
						wdp[cnt]=mfd->stepData[l]->ptSize.x;
						hdp[cnt]=mfd->stepData[l]->ptSize.y;
						//sdp[cnt]=(float)mfd->stepData[l]->kstrength;
						cnt++;
					}
					/*cnt = 0;
					for (i = 0; i < mfd->stepCount; i++) {
						xdp[i]=mfd->stepData[i]->ptUL.x;
						ydp[i]=mfd->stepData[i]->ptUL.y;
						fdp[i]=mfd->stepData[i]->frameNumber;
						cnt++;
					}*/
					//char* integ;
					//itoa(k - j, integ, 10);
				//SetDlgItemInt(mfd->dlgHwnd,IDC_J,j,true);
				//SetDlgItemInt(mfd->dlgHwnd,IDC_K,k,true);
				//SetDlgItemInt(mfd->dlgHwnd,IDC_DEBUG,k - j,true);

					//MessageBox(GetParent(mfd->dlgHwnd),integ,"Debug",MB_OK);

					if(mfd->prevStep.ptUL.x != mfd->nextStep.ptUL.x) {
						cur.x = smoothit(fdp, xdp, cnt, mfd->saveFrame);
						cur.y = smoothit(fdp, ydp, cnt, mfd->saveFrame);
					}
					if(mfd->prevStep.ptSize.x != mfd->nextStep.ptSize.x) {
						size.x = smoothit(fdp, wdp, cnt, mfd->saveFrame);
						size.y = smoothit(fdp, hdp, cnt, mfd->saveFrame);
					}
					if(mfd->prevStep.kstrength != mfd->nextStep.kstrength) {
						strength = int(((float)mfd->prevStep.kstrength + ((float)mfd->nextStep.kstrength - (float)mfd->prevStep.kstrength) * (float)ratio)+0.5);
					}
					//strength = int(smoothit(fdp, sdp, cnt, (float)mfd->saveFrame)+0.5);
					break;

				default:
					cur.x = int(mfd->prevStep.ptUL.x);
					cur.y = int(mfd->prevStep.ptUL.y);
					size.x = int(mfd->prevStep.ptSize.x);
					size.y = int(mfd->prevStep.ptSize.y);
					strength = int(mfd->prevStep.kstrength);
			}
			fx = cur.x;
			fy = cur.y;
			fw = size.x;
			fh = size.y;
			fs = strength;
		}
	}
	int key = 0;
	for (i = 0; i < mfd->stepCount; i++) {
		//struct panStep *step;
		//step = mfd->stepData[i];
		if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
			fx = mfd->stepData[i]->ptUL.x;
			fy = mfd->stepData[i]->ptUL.y;
			fw = mfd->stepData[i]->ptSize.x;
			fh = mfd->stepData[i]->ptSize.y;
			fs = mfd->stepData[i]->kstrength;
			key = i;
			break;
		}
	}
	if(mfd->saveFrame > mfd->stepData[mfd->stepCount - 1]->frameNumber) {
		fx = mfd->stepData[mfd->stepCount - 1]->ptUL.x;
		fy = mfd->stepData[mfd->stepCount - 1]->ptUL.y;
		fw = mfd->stepData[mfd->stepCount - 1]->ptSize.x;
		fh = mfd->stepData[mfd->stepCount - 1]->ptSize.y;
		fs = mfd->stepData[mfd->stepCount - 1]->kstrength;
	}

	if(!active || !mfd->run) {
		SetDlgItemInt(mfd->dlgHwnd,IDC_X,fx,true);
		SetDlgItemInt(mfd->dlgHwnd,IDC_Y,fy,true);
		SetDlgItemInt(mfd->dlgHwnd,IDC_W,fw,true);
		SetDlgItemInt(mfd->dlgHwnd,IDC_H,fh,true);
		SendMessage(GetDlgItem(mfd->dlgHwnd,IDC_STRENGTH),TBM_SETPOS,1,fs);
		SetDlgItemInt(mfd->dlgHwnd,IDC_SSTREN,fs,true);
	}

	if(!mfd->run) {
		if(key) {ListView_SetItemState(mfd->lbHwnd,key,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);}
		else {ListView_SetItemState(mfd->lbHwnd,-1,0,LVIS_SELECTED);}
		//if(key) {SendMessage(mfd->lbHwnd,LVM_SETSELECTIONMARK,0,(LPARAM)key);}
		//else {SendMessage(mfd->lbHwnd,LVM_SETSELECTIONMARK,0,(LPARAM)-1);}
	}



  int x1= (w/2+fx) - fw/2;
  int x2= x1+(fw-1);
  int y1= (h/2-fy) - fh/2;
  int y2= y1+(fh-1);

  if (x1>x2) { int t=x1; x1=x2; x2=t; }
  if (y1>y2) { int t=y1; y1=y2; y2=t; }

  if (x2<0 || x1>w || y2<0 || y1>h) return 0;
  if (x1<0) x1= 0;
  if (x2>=w) x2= w-1;
  if (y1<0) y1= 0;
  if (y2>=h) y2= h-1;
  int width = x2 - x1;
  int height = y2 - y1;
  rat = (float)fs / 30.0f;
  if (width >= height) {
	  filtw = (int)((((height / 2) -2) * rat) + 0.5);
	  if (height > 64) filtw = fs;
  }
  else {
	  filtw = (int)((((width / 2) -2) * rat) + 0.5);
	  if (width > 64) filtw = fs;
  }

  if (filtw > 0) {
	//if (fw > (mfd->w/2)-2) fw = (mfd->w/2)-2;
	//if (fw > (mfd->h/2)-2) fw = (mfd->h/2)-2;
	//fw = 20;
	int mult= 0x10000/(2*filtw+1);

	src+= x1+y1*spitch;
	dst+= x1+y1*dpitch;
	tmp+= x1+y1*((w+1)&-2);
	for (int y=y1; y<y2; y++)
	{
	  box_filter_row_MMX(tmp,src,filtw,x2-x1,mult);
	  box_filter_row_MMX(dst,tmp,filtw,x2-x1,mult);
	  box_filter_row_MMX(tmp,dst,filtw,x2-x1,mult);
	  src+= spitch;
	  dst+= dpitch;
	  tmp+= (w+1)&-2;
	}
	 src= mfd->rows+ (x1+y1*((w+1)&-2));
	 dst= fa->dst.data + (x1+y1*dpitch);

	 box_do_vertical_pass(dst,dstpitch, src,srcpitch, mfd->trow, x2-x1,y2-y1, filtw);
	 box_do_vertical_pass(src,srcpitch, dst,dstpitch, mfd->trow, x2-x1,y2-y1, filtw);
	 box_do_vertical_pass(dst,dstpitch, src,srcpitch, mfd->trow, x2-x1,y2-y1, filtw);
 }

  if (!mfd->run && key && mfd->stepCount > 1) {
	  int N;
	 src = fa->dst.data;
	 dst = fa->dst.data;
	 for (int y=0; y < fa->src.h; y++){
		  for (int x=0; x < fa->src.w; x++){
			  int P=src[x+y*spitch];
			 N=P^0xFFFFFF;
			 if ((y >= y1 && y <= y2) && (x >= x1 && x <= x2)) P = N;
			 dst[x+y*dpitch] = P;
		 }
	}
  }



  return 0;
}

int InitProc(FilterActivation *fa, const FilterFunctions *ff)
{
  MyFilterData *mfd = (MyFilterData *)fa->filter_data;

  mfd->x= 0;
  mfd->y= 0;
  mfd->w= 100;
  mfd->h= 100;
  mfd->filter_width= 10;
  return 0;
}

int StartProc(FilterActivation *fa, const FilterFunctions *ff)
{
  MyFilterData *mfd= (MyFilterData *)fa->filter_data;
  //memcpy(&tmpfd,&fa->filter_data,sizeof(fa->filter_data));
  //tmpfd = (MyFilterData *)fa->filter_data;
  if (mfd->filter_width<0) mfd->filter_width= 0;
  mfd->rows= NULL;
  mfd->trow= NULL;
  mfd->rows= new Pixel32[((fa->dst.w+1)&-2)*fa->dst.h+100];
  if (!mfd->rows) ff->ExceptOutOfMemory();
  mfd->trow= new Pixel16[fa->dst.w*4+100];
  if (!mfd->trow) ff->ExceptOutOfMemory();
  return 0;
}

int EndProc(FilterActivation *fa, const FilterFunctions *ff)
{
  MyFilterData *mfd= (MyFilterData *)fa->filter_data;
  delete[]mfd->rows; mfd->rows= NULL;
  delete[]mfd->trow; mfd->trow= NULL;
  return 0;
}

long ParamProc(FilterActivation *fa, const FilterFunctions *ff)
{
  MyFilterData *mfd= (MyFilterData *)fa->filter_data;
  if(mfd->stepCount == 0) {
	insertStep(mfd,0,mfd->x,mfd->y,1,mfd->w,mfd->h,mfd->filter_width);
  }
  return FILTERPARAM_SWAP_BUFFERS;
}

void rebuildListView(HWND hListbox, void *filterData) {
    MyFilterData *mfd = (MyFilterData *)filterData;
	LVITEM item = {LVIF_PARAM | LVIF_TEXT, 0, 0, 0, 0, NULL, 0, NULL, 0, 0};
	ListView_DeleteAllItems(hListbox);

	for (int i = 0; i < mfd->stepCount; i++) {
		char t[10];
		struct panStep *step;

		step = mfd->stepData[i];
		int mode = step->mode;
		int strength = step->kstrength;

		sprintf(t, "%lu", step->frameNumber);
		if (item.pszText) {
			free(item.pszText);
		}
		item.mask |= LVIF_PARAM;
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.lParam = step->frameNumber;
		item.iItem = i;
		item.iSubItem = 0;
		item.iItem = ListView_InsertItem(hListbox, &item);
		char szmode[10];
		switch (mode) {
		case 1:
			if(step->frameNumber == 0) strcpy(szmode,"Start");
			else strcpy(szmode,"Jump");
			break;
		case 2:
			strcpy(szmode,"Linear");
			break;
		case 3:
			strcpy(szmode,"Smooth");
			break;
		default:
			strcpy(szmode,"");
			break;
		}
		sprintf(t, "%s", szmode);
		item.mask &= ~LVIF_PARAM;
		if (item.pszText) {
			free(item.pszText);
		}
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.iSubItem = 1;
		ListView_SetItem(hListbox, &item);


		sprintf(t, "%d", step->ptUL.x);
		item.mask &= ~LVIF_PARAM;
		if (item.pszText) {
			free(item.pszText);
		}
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.iSubItem = 2;
		ListView_SetItem(hListbox, &item);


		sprintf(t, "%d", step->ptUL.y);
		item.mask &= ~LVIF_PARAM;
		if (item.pszText) {
			free(item.pszText);
		}
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.iSubItem = 3;
		ListView_SetItem(hListbox, &item);

		sprintf(t, "%d", step->ptSize.x);
		item.mask &= ~LVIF_PARAM;
		if (item.pszText) {
			free(item.pszText);
		}
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.iSubItem = 4;
		ListView_SetItem(hListbox, &item);

		sprintf(t, "%d", step->ptSize.y);
		item.mask &= ~LVIF_PARAM;
		if (item.pszText) {
			free(item.pszText);
		}
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.iSubItem = 5;
		ListView_SetItem(hListbox, &item);

		sprintf(t, "%d", step->kstrength);
		item.mask &= ~LVIF_PARAM;
		if (item.pszText) {
			free(item.pszText);
		}
		item.pszText = (char *)malloc(strlen(t) + 1);
		strcpy(item.pszText, t);
		item.iSubItem = 6;
		ListView_SetItem(hListbox, &item);


	}
}



BOOL CALLBACK ConfigDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	MyFilterData *mfd= (MyFilterData *)GetWindowLong(hdlg, DWL_USER);
	HWND hListBox = GetDlgItem(hdlg, IDC_LIST);
	LVCOLUMN column = {LVCF_FMT | LVCF_TEXT | LVCF_WIDTH, LVCFMT_CENTER, 70, "Frame", 0, 0, -1, -1};
	int indexLV;
	int i;
	char buf1[128];
	char buf2[80000];

	switch(msg)
	{
		case WM_INITDIALOG:
			SetWindowLong(hdlg, DWL_USER, lParam);
			mfd = (MyFilterData *)lParam;
			mfd->dlgHwnd = hdlg;
			ListView_SetExtendedListViewStyle(hListBox, ListView_GetExtendedListViewStyle(hListBox)| LVS_EX_FULLROWSELECT);

			// now create the columns
			ListView_InsertColumn(hListBox, 0, &column); // dummy column!
			ListView_InsertColumn(hListBox, 1, &column);
			column.pszText = "Type"; column.cx = 50;
			ListView_InsertColumn(hListBox, 2, &column);
			column.pszText = "X"; 
			ListView_InsertColumn(hListBox, 3, &column);
			column.pszText = "Y"; 
			ListView_InsertColumn(hListBox, 4, &column);
			column.pszText = "W"; 
			ListView_InsertColumn(hListBox, 5, &column);
			column.pszText = "H"; 
			ListView_InsertColumn(hListBox, 6, &column);
			column.pszText = "Strength"; 
			ListView_InsertColumn(hListBox, 7, &column);
			ListView_DeleteColumn(hListBox, 0);
			rebuildListView(hListBox, mfd);
			SetDlgItemInt(hdlg,IDC_X,mfd->x,true);
			SetDlgItemInt(hdlg,IDC_Y,mfd->y,true);
			SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_SETRANGE,0,MAKELONG(0,30));
//			SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_SETRANGEMIN,FALSE,(LPARAM)1);
			SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_SETPOS,1,mfd->filter_width);
			SendMessage(GetDlgItem(hdlg,IDC_XSPIN),UDM_SETRANGE,0,(LPARAM)MAKELONG(8192,-8192));
			SendMessage(GetDlgItem(hdlg,IDC_YSPIN),UDM_SETRANGE,0,(LPARAM)MAKELONG(-8192,8192));
			SetDlgItemInt(hdlg,IDC_W,mfd->w,false);
			SetDlgItemInt(hdlg,IDC_H,mfd->h,false);
			SendMessage(GetDlgItem(hdlg,IDC_WSPIN),UDM_SETRANGE,0,(LPARAM)MAKELONG(8192,0));
			SendMessage(GetDlgItem(hdlg,IDC_HSPIN),UDM_SETRANGE,0,(LPARAM)MAKELONG(0,8192));
			SetDlgItemInt(hdlg,IDC_SSTREN,mfd->filter_width,false);
			if(mfd->ifp) {
				mfd->ifp->InitButton(GetDlgItem(hdlg, IDPREVIEW));
				mfd->ifp->Toggle(hdlg);
			}
			mfd->run = 0;
			mfd->lbHwnd = hListBox;
			return TRUE;

		case WM_COMMAND:
//			int tf;
			switch(LOWORD(wParam))
			{
				case IDPREVIEW:
					if(mfd->ifp) {
						mfd->run= 0;
						mfd->ifp->Toggle(hdlg); 
					}
					return FALSE;

				case IDOK:     
					mfd->run= 1;
					//mfd->h= GetDlgItemInt(hdlg,IDC_H,NULL,false);
					//mfd->w= GetDlgItemInt(hdlg,IDC_W,NULL,false);
					buf1[0] = 0;
					buf2[0] = 0;
					strcpy(buf2,"\"");
					for (i = 0; i < mfd->stepCount; i++) {
						sprintf(buf1,"%d,%d,%d,%d,%d,%d,%d",mfd->stepData[i]->frameNumber,mfd->stepData[i]->ptUL.x,mfd->stepData[i]->ptUL.y,mfd->stepData[i]->mode,mfd->stepData[i]->ptSize.x,mfd->stepData[i]->ptSize.y,mfd->stepData[i]->kstrength);
						if (i > 0) strcat(buf2,",");
						strcat(buf2,buf1);
					}
					strcat(buf2,"\"");
					mfd->cstring = buf2;
					EndDialog(hdlg,0); 
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return TRUE;

				case IDCANCEL: 
					//memcpy(&mfd,&tmpfd,sizeof(tmpfd));
					//mfd = tmpfd;
					mfd->run = 1;
					EndDialog(hdlg,1);
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return TRUE;

				case IDC_RESET:
					mfd->w = 100;
					mfd->h = 100;
					mfd->x = 0;
					mfd->y = 0;
					mfd->filter_width = 10;
					SetDlgItemInt(hdlg,IDC_W,mfd->w,false);
					SetDlgItemInt(hdlg,IDC_H,mfd->h,false);
					SetDlgItemInt(hdlg,IDC_X,mfd->x,false);
					SetDlgItemInt(hdlg,IDC_Y,mfd->y,false);
					SetDlgItemInt(hdlg,IDC_SSTREN,mfd->filter_width,false);
					SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_SETPOS,1,mfd->filter_width);
					mfd->asp = (float)1;
					rebuildListView(hListBox,mfd);
					//mfd->ifp->RedoSystem();
					for (i = 0; i < mfd->stepCount; i++) {
						//struct panStep *step;
						//step = mfd->stepData[i];
						if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
							mfd->stepData[i]->ptUL.x = mfd->x;
							mfd->stepData[i]->ptUL.y = mfd->y;
							mfd->stepData[i]->ptSize.x = mfd->w;
							mfd->stepData[i]->ptSize.y = mfd->h;
							mfd->stepData[i]->kstrength = mfd->filter_width;
							rebuildListView(hListBox,mfd);
						}
					}
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return FALSE;

				case IDC_JUMP:
					indexLV = SendMessage( hListBox, LVM_GETSELECTIONMARK,0,0 );
					if(indexLV != -1 && indexLV != 0 && mfd->stepData[indexLV]->frameNumber < mfd->saveFrame) {
						mfd->stepData[indexLV]->mode = 1;
						rebuildListView(hListBox, mfd);
						mfd->ifp->RedoSystem();
						mfd->ifp->RedoFrame();
						return FALSE;
					}
					mfd->x= GetDlgItemInt(hdlg,IDC_X,NULL,true);
					mfd->y= GetDlgItemInt(hdlg,IDC_Y,NULL,true);
					mfd->w= GetDlgItemInt(hdlg,IDC_W,NULL,true);
					mfd->h= GetDlgItemInt(hdlg,IDC_H,NULL,true);
					mfd->filter_width= SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_GETPOS,0,0);
					if(insertStep(mfd, mfd->saveFrame, mfd->x, mfd->y, 1, mfd->w, mfd->h, mfd->filter_width)) {
						rebuildListView(hListBox, mfd);
					}
					//SendMessage(GetDlgItem(FindWindow(0,"Filter preview"), IDC_POSITION), (WM_USER+0x103), (WPARAM)TRUE, 100);
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return FALSE;

				case IDC_LINEAR:
					indexLV = SendMessage( hListBox, LVM_GETSELECTIONMARK,0,0 );
					if(indexLV != -1 && indexLV != 0 && mfd->stepData[indexLV]->frameNumber < mfd->saveFrame) {
						mfd->stepData[indexLV]->mode = 2;
						rebuildListView(hListBox, mfd);
						mfd->ifp->RedoSystem();
						mfd->ifp->RedoFrame();
						return FALSE;
					}
					mfd->x= GetDlgItemInt(hdlg,IDC_X,NULL,true);
					mfd->y= GetDlgItemInt(hdlg,IDC_Y,NULL,true);
					mfd->w= GetDlgItemInt(hdlg,IDC_W,NULL,true);
					mfd->h= GetDlgItemInt(hdlg,IDC_H,NULL,true);
					mfd->filter_width= SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_GETPOS,0,0);
					if(insertStep(mfd, mfd->saveFrame, mfd->x, mfd->y, 2, mfd->w, mfd->h, mfd->filter_width)) {
						rebuildListView(hListBox, mfd);
					}
					//SendMessage(GetDlgItem(FindWindow(0,"Filter preview"), IDC_POSITION), (WM_USER+0x103), (WPARAM)TRUE, 100);
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return FALSE;

				case IDC_SMOOTH:
					indexLV = SendMessage( hListBox, LVM_GETSELECTIONMARK,0,0 );
					if(indexLV != -1 && indexLV != 0 && mfd->stepData[indexLV]->frameNumber < mfd->saveFrame) {
						mfd->stepData[indexLV]->mode = 3;
						rebuildListView(hListBox, mfd);
						mfd->ifp->RedoSystem();
						mfd->ifp->RedoFrame();
						return FALSE;
					}
					mfd->x= GetDlgItemInt(hdlg,IDC_X,NULL,true);
					mfd->y= GetDlgItemInt(hdlg,IDC_Y,NULL,true);
					mfd->w= GetDlgItemInt(hdlg,IDC_W,NULL,true);
					mfd->h= GetDlgItemInt(hdlg,IDC_H,NULL,true);
					mfd->filter_width= SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_GETPOS,0,0);
					if(insertStep(mfd, mfd->saveFrame, mfd->x, mfd->y, 3, mfd->w, mfd->h, mfd->filter_width)) {
						rebuildListView(hListBox, mfd);
					}
					//SendMessage(GetDlgItem(FindWindow(0,"Filter preview"), IDC_POSITION), (WM_USER+0x103), (WPARAM)TRUE, 100);
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return FALSE;
		
 				case IDC_SELECTED: 
					indexLV = SendMessage( hListBox, LVM_GETSELECTIONMARK,0,0 );
					if(indexLV != -1) {
						deleteFaderStep(mfd, indexLV);
						rebuildListView(hListBox, mfd);
					}
					//mfd->ifp->RedoFrame();
					if(mfd->stepCount == 1) {
						SetDlgItemInt(hdlg,IDC_X,mfd->stepData[0]->ptUL.x,true);
						SetDlgItemInt(hdlg,IDC_Y,mfd->stepData[0]->ptUL.y,true);
						SetDlgItemInt(hdlg,IDC_W,mfd->stepData[0]->ptSize.x,true);
						SetDlgItemInt(hdlg,IDC_H,mfd->stepData[0]->ptSize.y,true);
						SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_SETPOS,(WPARAM)TRUE,(LPARAM)mfd->stepData[0]->kstrength);
						mfd->x = mfd->stepData[0]->ptUL.x;
						mfd->y = mfd->stepData[0]->ptUL.y;
						mfd->w = mfd->stepData[0]->ptSize.x;
						mfd->h = mfd->stepData[0]->ptSize.y;
						mfd->filter_width = mfd->stepData[0]->kstrength;
					}
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return FALSE;

				case IDC_ALL:
					for(i=mfd->stepCount - 1; i > 0; i--) {
						deleteFaderStep(mfd, i);
						rebuildListView(hListBox, mfd);
					}
					mfd->x = mfd->stepData[0]->ptUL.x;
					mfd->y = mfd->stepData[0]->ptUL.y;
					mfd->w = mfd->stepData[0]->ptSize.x;
					mfd->h = mfd->stepData[0]->ptSize.y;
					mfd->filter_width = mfd->stepData[0]->kstrength;
					mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
					return FALSE;

			   case IDC_X:
					if ((HIWORD(wParam)==EN_CHANGE) && (mfd!=NULL)) {
						int x= GetDlgItemInt(hdlg,IDC_X,NULL,true);
						if (x!=mfd->x && active)
						{
							mfd->x= x;
							Clipping (mfd->x,-4096,4096);
							if(mfd->stepCount > 1) {
								for (int i = 0; i < mfd->stepCount; i++) {
									//struct panStep *step;
									//step = mfd->stepData[i];
									if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
										mfd->stepData[i]->ptUL.x = mfd->x;
										//mfd->stepData[i]->ptUL.y = y;
										rebuildListView(hListBox,mfd);
									}
								}
							}
							else {
								mfd->stepData[0]->ptUL.x = mfd->x;
								rebuildListView(hListBox,mfd);
							}
							mfd->ifp->RedoFrame();
						}
					}
					break;

				case IDC_Y:
					if ((HIWORD(wParam)==EN_CHANGE) && (mfd!=NULL))
					{
						int y= GetDlgItemInt(hdlg,IDC_Y,NULL,true);
						if (y!=mfd->y && active)
						{
							mfd->y= y;
							Clipping (mfd->y,-4096,4096);
							if(mfd->stepCount > 1) {
								for (int i = 0; i < mfd->stepCount; i++) {
									//struct panStep *step;
									//step = mfd->stepData[i];
									if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
										//mfd->stepData[i]->ptUL.x = x;
										mfd->stepData[i]->ptUL.y = mfd->y;
										rebuildListView(hListBox,mfd);
									}
								}
							}
							else {
								mfd->stepData[0]->ptUL.y = mfd->y;
								rebuildListView(hListBox,mfd);
							}
							mfd->ifp->RedoFrame();
						}
					}
					break;

				case IDC_W:
					if ((HIWORD(wParam)==EN_CHANGE) && (mfd!=NULL))
					{
						int w= GetDlgItemInt(hdlg,IDC_W,NULL,false);
						if (w!=mfd->w && active)
						{
							mfd->w= w;
							Clipping (mfd->w,0,8192);
							mfd->asp= (float)mfd->w / (float)mfd->h;
							if(mfd->stepCount > 1) {
								for (int i = 0; i < mfd->stepCount; i++) {
									if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
										mfd->stepData[i]->ptSize.x = mfd->w;
										rebuildListView(hListBox,mfd);
									}
								}
							}
							else {
								mfd->stepData[0]->ptSize.x = mfd->w;
								rebuildListView(hListBox,mfd);
							}
							//mfd->ifp->RedoSystem();
							mfd->ifp->RedoFrame();
						}
						//mfd->ifp->RedoFrame();
					}
					break;

				case IDC_H:
					if ((HIWORD(wParam)==EN_CHANGE) && (mfd!=NULL))
					{
						int h= GetDlgItemInt(hdlg,IDC_H,NULL,false);
						if (h!=mfd->h && active)
						{
							mfd->h= h;
							Clipping (mfd->h,0,8192);
 							mfd->asp= (float)mfd->w / (float)mfd->h;
							if(mfd->stepCount > 1) {
								for (int i = 0; i < mfd->stepCount; i++) {
									if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
										mfd->stepData[i]->ptSize.y = mfd->h;
										rebuildListView(hListBox,mfd);
									}
								}
							}
							else {
								mfd->stepData[0]->ptSize.y = mfd->h;
								rebuildListView(hListBox,mfd);
							}
							//mfd->ifp->RedoSystem();
							mfd->ifp->RedoFrame();
						}
						//mfd->ifp->RedoFrame();
					}
					break;
			}
			break;

			case WM_NOTIFY:
				if (IDC_PAD==(int)wParam)
				{
					active = 1;
					mfd->x= GetDlgItemInt(hdlg,IDC_X,NULL,true);
					mfd->y= GetDlgItemInt(hdlg,IDC_Y,NULL,true);
					int x = mfd->x;
					int y = mfd->y;
					NMHDR *pnmh= (NMHDR *)lParam;
					NMVLPADCHANGE *pnmvltc= (NMVLPADCHANGE *)lParam;
					x+= pnmvltc->dx;
					y+= pnmvltc->dy;
					mfd->x = x;
					mfd->y = y;
					SetDlgItemInt(hdlg,IDC_X,mfd->x,true);
					SetDlgItemInt(hdlg,IDC_Y,mfd->y,true);
					Clipping (mfd->x,-4096,4096);
					Clipping (mfd->y,-4096,4096);
					if(mfd->stepCount > 1) {
						for (int i = 0; i < mfd->stepCount; i++) {
							//struct panStep *step;
							//step = mfd->stepData[i];
							if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
								mfd->stepData[i]->ptUL.x = mfd->x;
								mfd->stepData[i]->ptUL.y = mfd->y;
								rebuildListView(hListBox,mfd);
							}
						}
					}
					else {
						mfd->stepData[0]->ptUL.x = mfd->x;
						mfd->stepData[0]->ptUL.y = mfd->y;
						rebuildListView(hListBox,mfd);
					}
					active = 0;
					mfd->ifp->RedoFrame();
				}

				if (IDC_SIZEC==(int)wParam)
				{
					active = 1;
					int h = mfd->h;
					NMHDR *pnmh= (NMHDR *)lParam;
					NMVLPADCHANGE *pnmvltc= (NMVLPADCHANGE *)lParam;
					h+= pnmvltc->dx;
					int w= (int)((float)h * mfd->asp);
					if (w<0) w=0;
					if (h<0) h=0;
					mfd->h = h;
					mfd->w = w;
					SetDlgItemInt(hdlg,IDC_W,mfd->w,false);
					SetDlgItemInt(hdlg,IDC_H,mfd->h,false);
					Clipping (mfd->w,0,8192);
					Clipping (mfd->h,0,8192);
					if(mfd->stepCount > 1) {
						for (int i = 0; i < mfd->stepCount; i++) {
							//struct panStep *step;
							//step = mfd->stepData[i];
							if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
								mfd->stepData[i]->ptSize.x = mfd->w;
								mfd->stepData[i]->ptSize.y = mfd->h;
								rebuildListView(hListBox,mfd);
							}
						}
					}
					else {
						mfd->stepData[0]->ptSize.x = mfd->w;
						mfd->stepData[0]->ptSize.y = mfd->h;
						rebuildListView(hListBox,mfd);
					}
					active = 0;
					//mfd->ifp->RedoSystem();
					mfd->ifp->RedoFrame();
				}
/*
				if (IDC_STRENGTH==(int)wParam)
				{
					int s;
					//active = 1;
					if (mfd!=NULL && active)
					{
						s= SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_GETPOS,0,0);
						mfd->filter_width= s;
						SetDlgItemInt(hdlg,IDC_SSTREN,mfd->filter_width,false);
						if(mfd->stepCount > 1) {
							for (int i = 0; i < mfd->stepCount; i++) {
								//struct panStep *step;
								//step = mfd->stepData[i];
								if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
									mfd->stepData[i]->kstrength = s;
									rebuildListView(hListBox,mfd);
								}
							}
						}
						else {
							mfd->stepData[0]->kstrength = mfd->filter_width;
							rebuildListView(hListBox,mfd);
						}
						mfd->ifp->RedoFrame();
						//}
					}
					//active = 0;
				}*/
				break;

    case WM_HSCROLL:
		if((HWND)lParam == GetDlgItem(hdlg,IDC_WSPIN)) {
			int w= GetDlgItemInt(hdlg,IDC_W,NULL,false);
			if (w!=mfd->w) {
				//active = 1;
				mfd->w = w;
				Clipping (mfd->w,0,8192);
				if(mfd->stepCount > 1) {
					for (int i = 0; i < mfd->stepCount; i++) {
						//struct panStep *step;
						//step = mfd->stepData[i];
						if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
							mfd->stepData[i]->ptSize.x = w;
							rebuildListView(hListBox,mfd);
						}
					}
				}
				else {
					mfd->stepData[0]->ptSize.x = mfd->w;
					rebuildListView(hListBox,mfd);
				}
				mfd->asp= (float)mfd->w / (float)mfd->h; 
				mfd->ifp->RedoFrame();
				//active = 0;
			}
		}

		if((HWND)lParam == GetDlgItem(hdlg,IDC_XSPIN)) {
			int x= GetDlgItemInt(hdlg,IDC_X,NULL,false);
			if (x!=mfd->x) {
				//active = 1;
				mfd->x = x;
				Clipping (mfd->w,0,8192);
				if(mfd->stepCount > 1) {
					for (int i = 0; i < mfd->stepCount; i++) {
						//struct panStep *step;
						//step = mfd->stepData[i];
						if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
							mfd->stepData[i]->ptUL.x = x;
							rebuildListView(hListBox,mfd);
						}
					}
				}
				else {
					mfd->stepData[0]->ptUL.x = mfd->x;
					rebuildListView(hListBox,mfd);
				}
				mfd->ifp->RedoFrame();
				//active = 0;
			}
		}
		break;


    case WM_VSCROLL:
		if((HWND)lParam == GetDlgItem(hdlg,IDC_HSPIN)) {
			int h= GetDlgItemInt(hdlg,IDC_H,NULL,false);
			if (h!=mfd->h) {
				//active = 1;
				mfd->h = h;
				Clipping (mfd->h,0,8192);
				if(mfd->stepCount > 1) {
					for (int i = 0; i < mfd->stepCount; i++) {
						//struct panStep *step;
						//step = mfd->stepData[i];
						if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
							mfd->stepData[i]->ptSize.y = h;
							rebuildListView(hListBox,mfd);
						}
					}
				}
				else {
					mfd->stepData[0]->ptSize.y = mfd->h;
					rebuildListView(hListBox,mfd);
				}
				mfd->asp= (float)mfd->w / (float)mfd->h;
				mfd->ifp->RedoFrame();
				//active = 0;
			}
		}

		if((HWND)lParam == GetDlgItem(hdlg,IDC_YSPIN)) {
			int y= GetDlgItemInt(hdlg,IDC_Y,NULL,false);
			if (y!=mfd->y) {
				//active = 1;
				mfd->y = y;
				Clipping (mfd->y,0,8192);
				if(mfd->stepCount > 1) {
					for (int i = 0; i < mfd->stepCount; i++) {
						//struct panStep *step;
						//step = mfd->stepData[i];
						if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
							mfd->stepData[i]->ptUL.y = y;
							rebuildListView(hListBox,mfd);
						}
					}
				}
				else {
					mfd->stepData[0]->ptUL.y = mfd->y;
					rebuildListView(hListBox,mfd);
				}
				mfd->ifp->RedoFrame();
				//active = 0;
			}
		}

		if((HWND)lParam == GetDlgItem(hdlg,IDC_STRENGTH)) {
			int s= SendMessage(GetDlgItem(hdlg,IDC_STRENGTH),TBM_GETPOS,0,0);
			if (s!=mfd->filter_width) {
				//active = 1;
				mfd->filter_width = s;
				SetDlgItemInt(hdlg,IDC_SSTREN,mfd->filter_width,false);
				//Clipping (mfd->y,0,8192);
				if(mfd->stepCount > 1) {
					for (int i = 0; i < mfd->stepCount; i++) {
						//struct panStep *step;
						//step = mfd->stepData[i];
						if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
							mfd->stepData[i]->kstrength = s;
							rebuildListView(hListBox,mfd);
						}
					}
				}
				else {
					mfd->stepData[0]->kstrength = mfd->filter_width;
					rebuildListView(hListBox,mfd);
				}
				mfd->ifp->RedoFrame();
				//active = 0;
			}
		}
		break;
/*
	{
		int x= GetDlgItemInt(hdlg,IDC_X,NULL,true);
		int y= GetDlgItemInt(hdlg,IDC_Y,NULL,true);
		int h= GetDlgItemInt(hdlg,IDC_H,NULL,false);
		if (x!=mfd->x || y!=mfd->y || w!=mfd->w || h!=mfd->h)
		{
			active = 1;
			mfd->x= x;
			mfd->y= y;
			mfd->w= w;
			mfd->h= h;
			//SetDlgItemInt(hdlg,IDC_X,mfd->x,true);
			//SetDlgItemInt(hdlg,IDC_Y,mfd->y,true);
			//SetDlgItemInt(hdlg,IDC_W,mfd->w,false);
			//SetDlgItemInt(hdlg,IDC_H,mfd->h,false);
			Clipping (mfd->w,0,8192);
			Clipping (mfd->h,0,8192);
			Clipping (mfd->x,-4096,4096);
			Clipping (mfd->y,-4096,4096);
			if(mfd->stepCount > 1) {
				for (int i = 0; i < mfd->stepCount; i++) {
					//struct panStep *step;
					//step = mfd->stepData[i];
					if (mfd->saveFrame == mfd->stepData[i]->frameNumber) {
						mfd->stepData[i]->ptUL.x = x;
						mfd->stepData[i]->ptUL.y = y;
						mfd->stepData[i]->ptSize.x = w;
						mfd->stepData[i]->ptSize.y = h;
						rebuildListView(hListBox,mfd);
					}
				}
			}
			else {
				mfd->stepData[0]->ptUL.x = mfd->x;
				mfd->stepData[0]->ptUL.y = mfd->y;
				mfd->stepData[0]->ptSize.x = mfd->w;
				mfd->stepData[0]->ptSize.y = mfd->h;
				rebuildListView(hListBox,mfd);
			}
			mfd->ifp->RedoFrame();
			active = 0;
			//break;
		}
		break;*/
	}

  //}
  return FALSE;
}

int ConfigProc(FilterActivation *fa, const FilterFunctions *ff, HWND hwnd)
{
  MyFilterData *mfd= (MyFilterData *)fa->filter_data;
  MyFilterData mfd_old= *mfd;
  mfd->ifp= fa->ifp;
  mfd->asp = (float)1.0;
  mfd->run = 0;
  RegisterPadControl(fa->filter->module->hInstModule);
  int ret= DialogBoxParam(fa->filter->module->hInstModule,
                          MAKEINTRESOURCE(IDD_FILTER_WIPEOUT),
                          hwnd, ConfigDlgProc, (LPARAM)mfd);
  if (ret) *mfd= mfd_old;
  return ret;
}

ScriptFunctionDef func_defs[]={
	{ (ScriptFunctionPtr)ScriptConfig, "Config", "0s" },
	{ NULL },
};

CScriptObject window_obj={
	NULL, func_defs
};


FilterDefinition filterDef_WipeOut=
{
  NULL, NULL, NULL,        // next, prev, module
  "Wipeout",             // name
  "Keyframed Blur Box.\n"
  "Version 0.1 (09-17-2009)\n"
  "Copyright (C) 2009  Khaver\n"
  "khaver@netzero.net", // desc
  "khaver",      // maker
  NULL,                    // private_data
  sizeof(MyFilterData),    // inst_data_size
  InitProc,                // initProc
  NULL,                    // deinitProc
  RunProc,                 // runProc
  ParamProc,               // paramProc
  ConfigProc,              // configProc
  NULL,                    // stringProc
  StartProc,               // startProc
  EndProc,                 // endProc
  &window_obj,                    // script_obj
  FssProc,                    // fssProc
};

static FilterDefinition *fd_WipeOut;

void StringProc(const FilterActivation *fa, const FilterFunctions *ff, char *str) {
	MyFilterData *mfd = (MyFilterData *)fa->filter_data;

	wsprintf(str, " (keyframes %s)",
			mfd->cstring);
}

void ScriptConfig(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc) {
	FilterActivation *fa = (FilterActivation *)lpVoid;
	MyFilterData *mfd = (MyFilterData *)fa->filter_data;

	char buff[80000];
	//int kframe, kx, ky, mode;
	int sframe;
	int sx;
	int sy;
	int smode;
	int sw;
	int sh;
	int sfw;
	char * number;

	//mfd->w	= argv[0].asInt();
	//mfd->h	= argv[1].asInt();
	strcpy(buff, *argv[0].asString());
	//strcpy(mfd->cstring, buff);
	if(number = strtok(buff," ,")) sframe = atoi(number);
	else return;
	if(number = strtok(NULL," ,")) sx = atoi(number);
	else return;
	if(number = strtok(NULL," ,")) sy = atoi(number);
	else return;
	if(number = strtok(NULL," ,")) smode = atoi(number);
	else return;
	if(number = strtok(NULL," ,")) sw = atoi(number);
	else return;
	if(number = strtok(NULL," ,")) sh = atoi(number);
	else return;
	if(number = strtok(NULL," ,")) sfw = atoi(number);
	insertStep(mfd,sframe,sx,sy,smode,sw,sh,sfw);
	//memset(number,0,sizeof(number));
	while (1) {
		//memset(sframe,0,sizeof(sframe));
		//memset(sx,0,sizeof(sx));
		//memset(sy,0,sizeof(sy));
		//memset(smode,0,sizeof(smode));
		if(number = strtok(NULL, " ,")) sframe = atoi(number);
		else break;
		if(number = strtok(NULL," ,")) sx = atoi(number);
		else return;
		if(number = strtok(NULL," ,")) sy = atoi(number);
		else return;
		if(number = strtok(NULL," ,")) smode = atoi(number);
		else return;
		if(number = strtok(NULL," ,")) sw = atoi(number);
		else return;
		if(number = strtok(NULL," ,")) sh = atoi(number);
		else return;
		if(number = strtok(NULL," ,")) sfw = atoi(number);
		insertStep(mfd,sframe,sx,sy,smode,sw,sh,sfw);
	}
	mfd->run = 1;
	//mfd->ifp->RedoSystem();
	//mfd->ifp->RedoFrame();
}

bool FssProc(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen) {
	MyFilterData *mfd = (MyFilterData *)fa->filter_data;

	_snprintf(buf, buflen, "Config(%s)",
		mfd->cstring);

	return true;
}



extern "C" int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat)
{
  fd_WipeOut= ff->addFilter(fm, &filterDef_WipeOut, sizeof(FilterDefinition));
  if (!fd_WipeOut) return 1;
  vdfd_ver= VIRTUALDUB_FILTERDEF_VERSION;
  vdfd_compat= VIRTUALDUB_FILTERDEF_COMPATIBLE;
  return 0;
}

extern "C" void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff)
{
  ff->removeFilter(fd_WipeOut);
}
