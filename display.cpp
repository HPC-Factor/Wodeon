/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    display.cpp    : video output display
*/

//  notice: this program assumes >> operator preserves sign for signed integer.

#include <windows.h>
#include "resource.h"
#include "wodeon.h"

static int              sx, sy, sw, sh; // screen (display area) position, size
static int              wx, wy, ww, wh; // window   in picture
static int              vx, vy, vw, vh; // viewport in screen

static HDC              memDC=NULL;
static unsigned char    convert[316];
static unsigned char    palette[sizeof(LOGPALETTE)+sizeof(PALETTEENTRY)*226];
static int            (*dither)[4];

//  Bayer dither matrices
static int dither_BW4[2][4][4]={        // biased +30
    {54, 22, 54, 22,                    // 2x2 for small picture
      6, 38,  6, 38,
     54, 22, 54, 22,
      6, 38,  6, 38},
    {60, 28, 52, 20,                    // 4x4 for large picture
     12, 44,  4, 36,
     48, 16, 56, 24,
      0, 32,  8, 40}};

static int dither_BW16[2][4][4]={       // biased +8
    {14,  6, 14,  6,                    // 2x2 for small picture
      2, 10,  2, 10,
     14,  6, 14,  6,
      2, 10,  2, 10},
    {16,  7, 14,  5,                    // 4x4 for large picture
      3, 12,  1, 10,
     13,  4, 15,  6,
      0,  9,  2, 11}};

static int dither_C256[2][4][4]={
    { 3072,  -960,  3072,  -960,        // 2x2 for small picture
     -3072,   960, -3072,   960,
      3072,  -960,  3072,  -960,
     -3072,   960, -3072,   960},
    { 3840,  -192,  2880, -1344,        // 4x4 for large picture
     -2304,  1728, -3264,   768,
      2304, -1728,  3264,  -768,
     -3840,   192, -2880,  1344}};

static int dither_C4096[2][4][4]={
    { 1536,  -512,  1536, -512,         // 2x2 for small picture
     -1536,   512, -1536,  512,
      1536,  -512,  1536, -512,
     -1536,   512, -1536,  512},
    { 2048,  -256,  1536, -768,         // 4x4 for large picture
     -1280,  1024, -1792,  512,
      1280, -1024,  1792, -512,
     -2048,   256, -1536,  768}};

//  initialize display
void reset_display()
{
    int i;

    sw=GetSystemMetrics(SM_CXSCREEN);
    sh=GetSystemMetrics(SM_CYSCREEN);
    sx=sy=0;
    if(!option.hide_buttons)
        if(device==HPC){
            sx=LEFT_MARGIN; sw-=LEFT_MARGIN;
        }else{
            sy=TOP_MARGIN;  sh-=TOP_MARGIN;
        }

    if(!memDC) memDC=CreateCompatibleDC(NULL);
    if(!memDC) error(IDS_ERR_DISPLAY);

    if(option.display==BW4 || option.display==BW4F){
        for(i=  0; i<= 29; i++) convert[i]=0;
        for(i= 30; i<=285; i++) convert[i]=(i-30)>>6;
        for(i=286; i<=315; i++) convert[i]=3;
    }else
    if(option.display==BW16){
        for(i=  0; i<=  7; i++) convert[i]=0;
        for(i=  8; i<=263; i++) convert[i]=(i-8)>>4;
        for(i=264; i<=271; i++) convert[i]=15;
    }
    set_palette();
}

//  wind up display
void close_display()
{
    if(memDC) DeleteDC(memDC);
}

//  prepare physical palette for 256 colors / 256 grayscale
void set_palette()
{
    LOGPALETTE      *pal=(LOGPALETTE *)palette;
    PALETTEENTRY    *pe;
    HPALETTE        hpal;
    HDC             hdc;
    int             i, r, g, b;

    if(option.display!=BW256 && option.display!=C256) return;

    pal->palVersion=0x0200;
    pe=pal->palPalEntry+10;             // 10 for system colors
    if(option.display==BW256){          // 128 levels grayscale
        pal->palNumEntries=128+10;
        for(i=0; i<128; i++, pe++){
            pe->peRed  =
            pe->peGreen=
            pe->peBlue =i<<1;
            pe->peFlags=0;
        }
    }else{                              // 6*6*6 RGB
        pal->palNumEntries=216+10;
        for(b=0; b<6; b++)
        for(g=0; g<6; g++)
        for(r=0; r<6; r++, pe++){
            pe->peRed  =r*51;
            pe->peGreen=g*51;
            pe->peBlue =b*51;
            pe->peFlags=0;
        }
    }
    hdc=GetDC(main_window);
    GetSystemPaletteEntries(hdc, 0, 10, pal->palPalEntry);
    hpal=CreatePalette(pal);
    if(hpal){
        SelectPalette(hdc, hpal, FALSE);
        RealizePalette(hdc);
        DeleteObject(hpal);
    }
    ReleaseDC(main_window, hdc);
}

//  set picture size
void set_display(
    int pw,                             // I picture width
    int ph                              // I picture height
    )
{
    int i;

    if(option.mag2){
        if(option.rotate){
            vh=pw<<1;
            if(vh<=sh) ww=pw,              wx=0,          vy=((sh-vh)>>1)+sy;
            else       ww=sh>>1, vh=ww<<1, wx=(pw-ww)>>1, vy=sy;
            vw=ph<<1;
            if(vw<=sw) wh=ph,              wy=0,          vx=((sw-vw)>>1)+sx;
            else       wh=sw>>1, vw=wh<<1, wy=(ph-wh)>>1, vx=sx;
        }else{
            vw=pw<<1;
            if(vw<=sw) ww=pw,              wx=0,          vx=((sw-vw)>>1)+sx;
            else       ww=sw>>1, vw=ww<<1, wx=(pw-ww)>>1, vx=sx;
            vh=ph<<1;
            if(vh<=sh) wh=ph,              wy=0,          vy=((sh-vh)>>1)+sy;
            else       wh=sh>>1, vh=wh<<1, wy=(ph-wh)>>1, vy=sy;
        }
    }else{
        if(option.rotate){
            if(pw<=sh) ww=vh=pw, wx=0,          vy=((sh-pw)>>1)+sy;
            else       ww=vh=sh, wx=(pw-sh)>>1, vy=sy;

            if(ph<=sw) wh=vw=ph, wy=0,          vx=((sw-ph)>>1)+sx;
            else       wh=vw=sw, wy=(ph-sw)>>1, vx=sx;
        }else{
            if(pw<=sw) ww=vw=pw, wx=0,          vx=((sw-pw)>>1)+sx;
            else       ww=vw=sw, wx=(pw-sw)>>1, vx=sx;

            if(ph<=sh) wh=vh=ph, wy=0,          vy=((sh-ph)>>1)+sy;
            else       wh=vh=sh, wy=(ph-sh)>>1, vy=sy;
        }
    }

    wx &=0xfffe; wy &=0xfffe;           // for alignment of chrominance

    i=(240<=pw || option.mag2)? 1: 0;
    switch(option.display){
    case BW4    :
    case BW4F   : dither=dither_BW4  [i]; break;
    case BW16   : dither=dither_BW16 [i]; break;
    case C256   : dither=dither_C256 [i]; break;
    case C4096  : dither=dither_C4096[i]; break;
    }
}

//  make bitmap image (D: magnify x 2)

static void map_BW4(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, *d;
    unsigned char   *lyp, byte;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*vw-1; i=vh-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-vw;   i=0;
    }
    lyp= & ly[wy][wx];
    for(y=0; y<vh; y++, i+=yd){
        d=dither[y & 0x3];
        for(x=0; x<vw; x++, i+=xd){
            byte=(byte<<2) | convert[lyp[i]+d[x & 0x3]];
            if((x & 0x3)==3) *map++=byte;
        }
    }
}

static void map_BW4D(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, line, x, y, *d0, *d1, xr, c;
    unsigned char   *lyp, byte0, byte1;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*(vw>>1)-1; i=ww-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-(vw>>1);   i=0;
    }
    lyp= & ly[wy][wx]; line=vw>>2;
    for(y=0; y<vh; y+=2, i+=yd, map+=line){
        d0=dither[y & 0x3]; d1=dither[(y & 0x3)+1];
        for(x=0; x<vw; x+=2, i+=xd){
            xr=x & 0x3; c=lyp[i];
            byte0=(byte0<<2 | convert[c+d0[xr]])<<2 | convert[c+d0[xr+1]];
            byte1=(byte1<<2 | convert[c+d1[xr]])<<2 | convert[c+d1[xr+1]];
            if(xr==2){
                *map=byte0; *(map++ +line)=byte1;
            }
        }
    }
}

static void map_BW16(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, *d;
    unsigned char   *lyp, byte;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*vw-1; i=vh-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-vw;   i=0;
    }
    lyp= & ly[wy][wx];
    for(y=0; y<vh; y++, i+=yd){
        d=dither[y & 0x3];
        for(x=0; x<vw; x++, i+=xd){
            byte=(byte <<4) | convert[lyp[i]+d[x & 0x3]];
            if(x & 0x1) *map++=byte;
        }
    }
}

static void map_BW16D(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, line, x, y, *d0, *d1, xr, c;
    unsigned char   *lyp;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*(vw>>1)-1; i=ww-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-(vw>>1);   i=0;
    }
    lyp= & ly[wy][wx];  line=vw>>1;
    for(y=0; y<vh; y+=2, i+=yd, map+=line){
        d0=dither[y & 0x3]; d1=dither[(y & 0x3)+1];
        for(x=0; x<vw; x+=2, i+=xd){
            xr=x & 0x3; c=lyp[i];
            * map         =convert[c+d0[xr]]<<4 | convert[c+d0[xr+1]];
            *(map++ +line)=convert[c+d1[xr]]<<4 | convert[c+d1[xr+1]];
        }
    }
}

static void map_BW256(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y;
    unsigned char   *lyp;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*vw-1; i=vh-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-vw;   i=0;
    }
    lyp= & ly[wy][wx];
    for(y=0; y<vh; y++, i+=yd)
        for(x=0; x<vw; x++, i+=xd) *map++=(lyp[i]>>1)+10;
}

static void map_BW256D(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y;
    unsigned char   *lyp, *mp;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*(vw>>1)-1; i=ww-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-(vw>>1);   i=0;
    }
    lyp= & ly[wy][wx]; mp=map;
    for(y=0; y<vh; y+=2, i+=yd, mp+=vw)
        for(x=0; x<vw; x+=2, i+=xd, mp+=2) *mp=(lyp[i]>>1)+10;

    mp=map+1;                           // fill gap pixels
    for(y=0; y<vh; y+=2, mp+=vw+2){
        for(x=3; x<vw; x+=2, mp+=2) *mp=(*(mp-1) + *(mp+1))>>1;
        *mp=*(mp-1);
    }
    mp=map+vw;
    for(y=3; y<vh; y+=2, mp+=vw)
        for(x=0; x<vw; x++, mp++) *mp=(*(mp-vw) + *(mp+vw))>>1;
    for(x=0; x<vw; x++, mp++) *mp=*(mp-vw);
}

static void map_BW(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y;
    unsigned char   *lyp, c;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*vw-1; i=vh-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-vw;   i=0;
    }
    lyp= & ly[wy][wx];
    for(y=0; y<vh; y++, i+=yd)
        for(x=0; x<vw; x++, i+=xd){
            c=lyp[i]; *map++=c; *map++=c; *map++=c;
        }
}

static void map_BWD(
    y_buffer        ly,                 // I luminance
    c_buffer          ,                 // not used
    c_buffer          ,                 // not used
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, vw3;
    unsigned char   *lyp, *mp, c;

    if(option.rotate){
        xd=HORIZONTAL_SIZE; yd=-HORIZONTAL_SIZE*(vw>>1)-1; i=ww-1;
    }else{
        xd=1;               yd= HORIZONTAL_SIZE-(vw>>1);   i=0;
    }
    lyp= & ly[wy][wx]; mp=map; vw3=vw*3;
    for(y=0; y<vh; y+=2, i+=yd, mp+=vw3)
        for(x=0; x<vw; x+=2, i+=xd,  mp+=3){
            c=lyp[i]; *mp++=c; *mp++=c; *mp++=c;
        }

    mp=map+3;                           // fill gap pixels
    for(y=0; y<vh; y+=2, mp+=vw3+3){
        for(x=3; x<vw; x+=2,  mp+=3){
            c=(*(mp-3) + *(mp+3))>>1; *mp++=c; *mp++=c; *mp++=c;
        }
        c=*(mp-3); *mp++=c; *mp++=c; *mp++=c;
    }
    mp=map+vw3;
    for(y=3; y<vh; y+=2, mp+=vw3)
        for(x=0; x<vw; x++){
            c=(*(mp-vw3) + *(mp+vw3))>>1; *mp++=c; *mp++=c; *mp++=c;
        }
    for(x=0; x<vw; x++){
        c=*(mp-vw3); *mp++=c; *mp++=c; *mp++=c;
    }
}

static void map_C256(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, w, u, v, r, g, b, *d;
    unsigned char   *lyp, *cbp, *crp;

    if(option.rotate){
        wh=vw; xd=-vw; yd=vh*vw+1; i=(vh-1)*vw;
    }else{
        ww=vw; xd=1;   yd=0;       i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        d=dither[y & 0x3];
        for(x=0; x<ww; x++, i+=xd){
            w=*lyp++ *192 +d[x & 0x3]; u=*cbp-128; v=*crp-128;
            r=(w        +269*v)>>13; if(5< r) r=5; else if(r <0) r=0;
            g=(w - 66*u -137*v)>>13; if(5< g) g=5; else if(g <0) g=0;
            b=(w +340*u       )>>13; if(5< b) b=5; else if(b <0) b=0;
            map[i]=(b*6+g)*6+r+10;
            if(x & 1) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(y & 1) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }
}

static void map_C256D(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, j, k, x, y, xr, yr, d, n;
    int             w, u, v, r, g, b, rt, gt, bt;
    unsigned char   *lyp, *cbp, *crp;

    if(option.rotate){
        xd=-vw<<1; yd=vh*vw+2; i=(vh-2)*vw;
    }else{
        xd=2;      yd=vw;      i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        yr=(y&1)<<1;
        for(x=0; x<ww; x++, i+=xd){
            xr=(x&1)<<1;
            w=*lyp++ *192; u=*cbp-128; v=*crp-128;
            rt=w        +269*v;
            gt=w - 66*u -137*v;
            bt=w +340*u;
            for(k=0, n=0; k<2; k++, n=vw) for(j=0; j<2; j++){
                d=dither[yr+k][xr+j];
                r=(rt+d)>>13; if(5< r) r=5; else if(r <0) r=0;
                g=(gt+d)>>13; if(5< g) g=5; else if(g <0) g=0;
                b=(bt+d)>>13; if(5< b) b=5; else if(b <0) b=0;
                map[i+j+n]=(b*6+g)*6+r+10;
            }
            if(xr) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(yr) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }
}

static void map_C4096(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, w, u, v, r, g, b, *d;
    unsigned char   *lyp, *cbp, *crp;

    if(option.rotate){
        xd=-vw; yd=vh*vw+1; i=(vh-1)*vw;
    }else{
        xd=1;   yd=0;       i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        d=dither[y & 0x3];
        for(x=0; x<ww; x++, i+=xd){
            w=(*lyp++ <<8)+d[x & 0x3]; u=*cbp-128; v=*crp-128;
            r=(w        +359*v)>>11; if(31< r) r=31; else if(r <0) r=0;
            g=(w - 88*u -183*v)>>10; if(63< g) g=63; else if(g <0) g=0;
            b=(w +454*u       )>>11; if(31< b) b=31; else if(b <0) b=0;
            ((WORD *)map)[i]= r<<11 | g<<5 | b;
            if(x & 1) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(y & 1) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }
}

static void map_C4096D(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, j, k, x, y, xr, yr, d, n;
    int             w, u, v, r, g, b, rt, gt, bt;
    unsigned char   *lyp, *cbp, *crp;

    if(option.rotate){
        xd=-vw<<1; yd=vh*vw+2; i=(vh-2)*vw;
    }else{
        xd=2;      yd=vw;      i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        yr=(y&1)<<1;
        for(x=0; x<ww; x++, i+=xd){
            xr=(x&1)<<1;
            w=*lyp++ <<8; u=*cbp-128; v=*crp-128;
            rt=w        +359*v;
            gt=w - 88*u -183*v;
            bt=w +454*u;
            for(k=0, n=0; k<2; k++, n=vw) for(j=0; j<2; j++){
                d=dither[yr+k][xr+j];
                r=(rt+d)>>11; if(31< r) r=31; else if(r <0) r=0;
                g=(gt+d)>>10; if(63< g) g=63; else if(g <0) g=0;
                b=(bt+d)>>11; if(31< b) b=31; else if(b <0) b=0;
                ((WORD *)map)[i+j+n]= r<<11 | g<<5 | b;
            }
            if(xr) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(yr) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }
}

static void map_C65536(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, w, u, v, r, g, b;
    unsigned char   *lyp, *cbp, *crp;

    if(option.rotate){
        xd=-vw; yd=vh*vw+1; i=(vh-1)*vw;
    }else{
        xd=1;   yd=0;       i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        for(x=0; x<ww; x++, i+=xd){
            w=*lyp++ <<8; u=*cbp-128; v=*crp-128;
            r=(w        +359*v)>>11; if(31< r) r=31; else if(r <0) r=0;
            g=(w - 88*u -183*v)>>10; if(63< g) g=63; else if(g <0) g=0;
            b=(w +454*u       )>>11; if(31< b) b=31; else if(b <0) b=0;
            ((WORD *)map)[i]= r<<11 | g<<5 | b;
            if(x & 1) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(y & 1) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }
}

static void map_C65536D(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, w, u, v, r, g, b;
    unsigned char   *lyp, *cbp, *crp;
    WORD            *mp;

    if(option.rotate){
        xd=-vw<<1; yd=vh*vw+2; i=(vh-2)*vw;
    }else{
        xd=2;      yd=vw;      i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        for(x=0; x<ww; x++, i+=xd){
            w=*lyp++ <<8; u=*cbp-128; v=*crp-128;
            r=(w        +359*v)>>11; if(31< r) r=31; else if(r <0) r=0;
            g=(w - 88*u -183*v)>>10; if(63< g) g=63; else if(g <0) g=0;
            b=(w +454*u       )>>11; if(31< b) b=31; else if(b <0) b=0;
            ((WORD *)map)[i]= r<<11 | g<<5 | b;
            if(x & 1) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(y & 1) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }

    mp=(WORD *)map+1;                   // fill gap pixels
    for(y=0; y<vh; y+=2, mp+=vw+2){
        for(x=3; x<vw; x+=2, mp+=2)
            *mp=((*(mp-1) & 0xf800)+(*(mp+1) & 0xf800))>>1 & 0xf800 |
                ((*(mp-1) & 0x07e0)+(*(mp+1) & 0x07e0))>>1 & 0x07e0 |
                ((*(mp-1) & 0x001f)+(*(mp+1) & 0x001f))>>1 & 0x001f;
        *mp=*(mp-1);
    }
    mp=(WORD *)map+vw;
    for(y=3; y<vh; y+=2, mp+=vw)
        for(x=0; x<vw; x++, mp++)
            *mp=((*(mp-vw) & 0xf800)+(*(mp+vw) & 0xf800))>>1 & 0xf800 |
                ((*(mp-vw) & 0x07e0)+(*(mp+vw) & 0x07e0))>>1 & 0x07e0 |
                ((*(mp-vw) & 0x001f)+(*(mp+vw) & 0x001f))>>1 & 0x001f;
    for(x=0; x<vw; x++, mp++) *mp=*(mp-vw);
}

static void map_COLOR(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, w, u, v, r, g, b;
    unsigned char   *lyp, *cbp, *crp;

    if(option.rotate){
        wh=vw; xd=-3*vw; yd=3*(vh*vw+1); i=3*(vh-1)*vw;
    }else{
        ww=vw; xd=3;     yd=0;           i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        for(x=0; x<ww; x++, i+=xd){
            w=*lyp++ <<8; u=*cbp-128; v=*crp-128;
            r=(w        +359*v)>>8; if(255< r) r=255; else if(r <0) r=0;
            g=(w - 88*u -183*v)>>8; if(255< g) g=255; else if(g <0) g=0;
            b=(w +454*u       )>>8; if(255< b) b=255; else if(b <0) b=0;
            map[i]=b; map[i+1]=g; map[i+2]=r;
            if(x & 1) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(y & 1) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }
}

static void map_COLORD(
    y_buffer        ly,                 // I luminance
    c_buffer        cb,                 // I chrominance Cb
    c_buffer        cr,                 // I chrominance Cr
    unsigned char  *map                 // O bitmap image
    )
{
    int             xd, yd, i, x, y, w, u, v, r, g, b, vw3;
    unsigned char   *lyp, *cbp, *crp, *mp;

    vw3=vw*3;
    if(option.rotate){
        wh=vw>>1; xd=-6*vw; yd=3*(vh*vw+2); i=3*(vh-2)*vw;
    }else{
        ww=vw>>1; xd=6;     yd=vw3;         i=0;
    }
    lyp= & ly[wy][wx];
    cbp= & cb[wy>>1][wx>>1];
    crp= & cr[wy>>1][wx>>1];
    for(y=0; y<wh; y++, i+=yd){
        for(x=0; x<ww; x++, i+=xd){
            w=*lyp++ <<8; u=*cbp-128; v=*crp-128;
            r=(w        +359*v)>>8; if(255< r) r=255; else if(r <0) r=0;
            g=(w - 88*u -183*v)>>8; if(255< g) g=255; else if(g <0) g=0;
            b=(w +454*u       )>>8; if(255< b) b=255; else if(b <0) b=0;
            map[i]=b; map[i+1]=g; map[i+2]=r;
            if(x & 1) cbp++, crp++;
        }
        lyp+=HORIZONTAL_SIZE-ww;
        cbp-=ww>>1; crp-=ww>>1;
        if(y & 1) cbp+=HORIZONTAL_SIZE>>1, crp+=HORIZONTAL_SIZE>>1;
    }

    mp=map+3;                           // fill gap pixels
    for(y=0; y<vh; y+=2, mp+=vw3+6){
        for(x=3; x<vw; x+=2, mp+=6){
            * mp   =(*(mp-3) + *(mp+3))>>1;
            *(mp+1)=(*(mp-2) + *(mp+4))>>1;
            *(mp+2)=(*(mp-1) + *(mp+5))>>1;
        }
        *mp=*(mp-3); *(mp+1)=*(mp-2); *(mp+2)=*(mp-1);
    }
    mp=map+vw3;
    for(y=3; y<vh; y+=2, mp+=vw3)
        for(x=0; x<vw; x++){
            *mp=(*(mp-vw3) + *(mp+vw3))>>1; mp++;
            *mp=(*(mp-vw3) + *(mp+vw3))>>1; mp++;
            *mp=(*(mp-vw3) + *(mp+vw3))>>1; mp++;
        }
    for(x=0; x<vw; x++){
        *mp=*(mp-vw3); mp++; *mp=*(mp-vw3); mp++; *mp=*(mp-vw3); mp++;
    }
}

// hardware specific parameters
static struct display_param{
    int     align;                      // width mask for line alignment
    WORD    bpp;
    DWORD   compression;
    UINT    usage;
    void  (*map )(y_buffer, c_buffer, c_buffer, unsigned char *);
    void  (*mapD)(y_buffer, c_buffer, c_buffer, unsigned char *);
}   dparam[]={
        {0xfff8,  2, 0,            0,              map_BW4,    map_BW4D   },
        {0xfff0,  2, BI_RGB,       DIB_PAL_COLORS, map_BW4,    map_BW4D   },
        {0xfff8,  4, BI_RGB,       DIB_PAL_COLORS, map_BW16,   map_BW16D  },
        {0xfffc,  8, BI_RGB,       DIB_PAL_COLORS, map_BW256,  map_BW256D },
        {0xfffc, 24, BI_RGB,       DIB_RGB_COLORS, map_BW,     map_BWD    },
        {0xfffc,  8, BI_RGB,       DIB_PAL_COLORS, map_C256,   map_C256D  },
        {0xfffe, 16, BI_BITFIELDS, DIB_RGB_COLORS, map_C4096,  map_C4096D },
        {0xfffe, 16, BI_BITFIELDS, DIB_RGB_COLORS, map_C65536, map_C65536D},
        {0xfffc, 24, BI_RGB,       DIB_RGB_COLORS, map_COLOR,  map_COLORD }
    };

static struct{
    BITMAPINFOHEADER    bmiHeader;
    RGBQUAD             mask[3];        // for 565 16bits colors
}   bmi={{sizeof(BITMAPINFOHEADER), 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}};

//  draw picture image on screen
void draw_picture(
    y_buffer    ly,                     // I luminance
    c_buffer    cb,                     // I chrominance Cb
    c_buffer    cr                      // I chrominance Cr
    )
{
    struct display_param   *dp;
    unsigned char          *map;
    HBITMAP                 hb, hold;
    HDC                     hdc;

    dp=& dparam[(int)option.display];
    vw &=dp->align;
    if(option.display==BW4){
        map=(unsigned char *)malloc((vw*vh+3)>>2);
        if(!map) error(IDS_ERR_MEMORY);
    }else{
        bmi.bmiHeader.biWidth       = vw;
        bmi.bmiHeader.biHeight      =-vh;
        bmi.bmiHeader.biBitCount    =dp->bpp;
        bmi.bmiHeader.biCompression =dp->compression;
        *(DWORD *)(bmi.mask   )=0x1f<<11;   // for 565 16bits colors
        *(DWORD *)(bmi.mask +1)=0x3f<<5;
        *(DWORD *)(bmi.mask +2)=0x1f;
        hdc=GetDC(NULL);
        hb=CreateDIBSection(hdc, (BITMAPINFO *)&bmi, dp->usage,
                            (void **)&map, NULL, 0);
        ReleaseDC(NULL, hdc);
        if(!hb) error(IDS_ERR_DISPLAY);
    }

    if(option.mag2) dp->mapD(ly, cb, cr, map);
    else            dp->map (ly, cb, cr, map);

    if(option.display==BW4){
        hb=CreateBitmap(vw, vh, 1, 2, map);
        free(map);
        if(!hb) error(IDS_ERR_DISPLAY);
    }

    hold=SelectObject(memDC, hb);
    hdc=GetDC(main_window);
    BitBlt(hdc, vx, vy, vw, vh, memDC, 0, 0, SRCCOPY);
    ReleaseDC(main_window, hdc);
    SelectObject(memDC, hold);
    DeleteObject(hb);
}
