#include <stdio.h>
#include <stdint.h>
#define BITMAP WINDOWS_BITMAP
#include <ddraw.h>
#undef BITMAP
#include "win-ddraw-screenshot.h"
#include "video.h"

extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw();

extern "C" void ddraw_init(HWND h);
extern "C" void ddraw_close();

HBITMAP hbitmap;

int xs, ys, ys2;

void CopySurface(IDirectDrawSurface4 *pDDSurface)
{ 
    HDC hdc, hmemdc;

    HBITMAP hprevbitmap;

    DDSURFACEDESC2 ddsd2;

    pDDSurface->GetDC(&hdc);

    hmemdc = CreateCompatibleDC(hdc); 

    ZeroMemory(&ddsd2 ,sizeof( ddsd2 )); // better to clear before using

    ddsd2.dwSize = sizeof( ddsd2 ); //initialize with size 

    pDDSurface->GetSurfaceDesc(&ddsd2);

    hbitmap = CreateCompatibleBitmap( hdc ,xs ,ys);

    hprevbitmap = (HBITMAP) SelectObject( hmemdc, hbitmap );

    BitBlt(hmemdc,0 ,0 ,xs ,ys ,hdc ,0 ,0,SRCCOPY);    

    SelectObject(hmemdc,hprevbitmap); // restore the old bitmap 

    DeleteDC(hmemdc);

    pDDSurface->ReleaseDC(hdc);

    return ; 
}

void DoubleLines(uint8_t *dst, uint8_t *src)
{
	int i = 0;
	uint8_t temp_buf[8320];
	for (i = 0; i < ys; i++)
	{
		memcpy(dst + (i * xs * 8), src + (i * xs * 4), xs * 4);
		memcpy(dst + ((i * xs * 8) + (xs * 4)), src + (i * xs * 4), xs * 4);
	}
}

void SaveBitmap(char *szFilename,HBITMAP hBitmap)
{
    HDC                 hdc=NULL;
    FILE*               fp=NULL;
    LPVOID              pBuf=NULL;
    LPVOID              pBuf2=NULL;
    BITMAPINFO          bmpInfo;
    BITMAPFILEHEADER    bmpFileHeader; 

    do{ 

        hdc=GetDC(NULL);

        ZeroMemory(&bmpInfo,sizeof(BITMAPINFO));

        bmpInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);

        GetDIBits(hdc,hBitmap,0,0,NULL,&bmpInfo,DIB_RGB_COLORS); 

        if(bmpInfo.bmiHeader.biSizeImage<=0)
            bmpInfo.bmiHeader.biSizeImage=bmpInfo.bmiHeader.biWidth*abs(bmpInfo.bmiHeader.biHeight)*(bmpInfo.bmiHeader.biBitCount+7)/8;

        if((pBuf = malloc(bmpInfo.bmiHeader.biSizeImage))==NULL)
        {
            // pclog("ERROR: Unable to Allocate Bitmap Memory");
            break;
        }

	if (ys2 <= 250)
	{
		pBuf2 = malloc(bmpInfo.bmiHeader.biSizeImage * 2);
	}

        bmpInfo.bmiHeader.biCompression=BI_RGB;

        GetDIBits(hdc,hBitmap,0,bmpInfo.bmiHeader.biHeight,pBuf, &bmpInfo, DIB_RGB_COLORS);

        if((fp = fopen(szFilename,"wb"))==NULL)
        {
            MessageBox( NULL, "Unable to Create Bitmap File", "Error", MB_OK|MB_ICONERROR);
            break;
        } 

        bmpFileHeader.bfReserved1=0;

        bmpFileHeader.bfReserved2=0;

	if (pBuf2)
	{
		bmpInfo.bmiHeader.biSizeImage <<= 1;
		bmpInfo.bmiHeader.biHeight <<= 1;
	}

        bmpFileHeader.bfSize=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+bmpInfo.bmiHeader.biSizeImage;

        bmpFileHeader.bfType='MB';

        bmpFileHeader.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER); 

        fwrite(&bmpFileHeader,sizeof(BITMAPFILEHEADER),1,fp);

        fwrite(&bmpInfo.bmiHeader,sizeof(BITMAPINFOHEADER),1,fp);

	if (pBuf2)
	{
		DoubleLines((uint8_t *) pBuf2, (uint8_t *) pBuf);
		fwrite(pBuf2,bmpInfo.bmiHeader.biSizeImage,1,fp); 
	}
	else
	{
	        fwrite(pBuf,bmpInfo.bmiHeader.biSizeImage,1,fp); 
	}

    }while(false); 

    if(hdc)     ReleaseDC(NULL,hdc); 

    if(pBuf2)    free(pBuf2); 

    if(pBuf)    free(pBuf); 

    if(fp)      fclose(fp);
}

void ddraw_common_take_screenshot(char *fn, IDirectDrawSurface4 *pDDSurface)
{
	xs = xsize;
	ys = ys2 = ysize;
	/* For EGA/(S)VGA, the size is NOT adjusted for overscan. */
	if ((overscan_y > 16) && enable_overscan)
	{
		xs += overscan_x;
		ys += overscan_y;
	}
	/* For CGA, the width is adjusted for overscan, but the height is not. */
	if (overscan_y == 16)
	{
		if (ys2 <= 250)
			ys += (overscan_y >> 1);
		else
			ys += overscan_y;
	}
	CopySurface(pDDSurface);
	SaveBitmap(fn, hbitmap);
}
