/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    video.cpp      : MPEG-1 video decoder
*/

//  notice: this program assumes >> operator preserves sign for signed integer.
//          memset() in block() is not portable.

#include <windows.h>
#include "idct.h"
#include "idctcoef.h"
#include "resource.h"
#include "wodeon.h"

//  video parameters
static int mb_cols      =0;             // macroblock column  size
static int mb_rows      =0;             // macroblock row     size
static int picture_type =0;             // 1:I 2:P 3:B
static int qscale       =0;             // quantizer scale
static int full_fwd     =0;             // full_pell_forward_vector
static int fwd_rsize    =0;             // forward_r_size
static int interval     =0;             // 64*1000 / picture rate
static int picture_interval[]={
    0, 2669, 2667, 2560, 2135, 2133, 1280, 1068, 1067};

//  GOP time code
static int gop_time;                    // time (ms)
static int gop_pictures;

//  video input buffer
static unsigned char   *video_in=NULL;  // buffer head
static int              vib_size=0;     // buffer size
static unsigned char   *vib_end;        // points just behind filled area
static unsigned char   *vip;            // current byte position
static int              bit;            // current bit  position
                                        // 0-7, MSB:7 LSB:0
//  video output buffer
static y_buffer         video_y[2], *new_y, *old_y;
static c_buffer         video_b[2], *new_b, *old_b;
static c_buffer         video_r[2], *new_r, *old_r;
static int              last_picture;   // 0, 1 index of video_y etc.

//  (non) intra quantizer matrix
static int intraq[64];
static int noninq[64];

//  initialize video decoder
void reset_video()
{
    int size=option.large_buffer? VIDEO_BUFF_SIZE2: VIDEO_BUFF_SIZE1;

    if(vib_size!=size){                 // allocate video input buffer
        free(video_in); video_in=NULL;
        vib_size=0;
    }
    if(!video_in){                      // +2 is a guard for vlc()
        video_in=(unsigned char *)malloc(size+2);
        if(!video_in) error(IDS_ERR_MEMORY);
        vib_size=size;
    }

    gop_time=-1; gop_pictures=0;
    vib_end=vip=video_in;
    last_picture=0;
}

//  wind up video decoder
void close_video()
{
    free(video_in);
}

//  get pointer to video input buffer
unsigned char *get_video_in(            // O points to empty area
    int length                          // I buffer length required
    )
{
    unsigned char *p=vib_end;

    if(video_in+vib_size-vib_end < length) error(IDS_ERR_BUFFER);
    vib_end+=length;
    return p;
}

//  put input buffer contents in front
static void cleanout_buffer()
{
    unsigned char *p=video_in;
    while(vip<vib_end) *p++=*vip++;
    vib_end=p; vip=video_in;
}

//  seek header
static int header()                     // O header code (-1: reach buffer end)
{
    for(; vip+3 < vib_end; vip++)       // find 00 00 01 series
        if(*vip==0x00 && *(vip+1)==0x00 && *(vip+2)==0x01){
            vip+=3;
            return *vip++;
        }
    return -1;
}

//  extract n bits
static unsigned emask[]={0, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};

static int extract(                     // O right aligned bits
    int n                               // I size 1-8
    )
{
    int v=(*vip <<8 | *(vip+1))>>(bit+9-n) & emask[n];

    bit-=n;
    if(bit<0) bit+=8, vip++;
    return v;
}

//  get element: sequence_header, group, picture
unsigned char *next_header;

static int element()                    // O start code (-1: not found)
{
    unsigned char  *start_point;
    int             code, c;

    do{                                 // seek element start point
        c=header();
        if((c==-1 && packet(TRUE, -1)<0) || c==0xb7) return -1;
    }while(c!=0x00 && c!=0xb3 && c!=0xb8);
    start_point=vip;
    code=c;

    do{                                 // read packets until next
        c=header();
        if(c==-1 && packet(TRUE, -1)<0) return -1;
    }while(c!=0x00 && c!=0xb3 && c!=0xb8);
    next_header=vip-4;
    vip=start_point;
    return code;
}

//  seek picture: vip points next to picture start code at return
static int picture(                     // O TRUE: picture found
    int     level                       // I decoded picture type <= level
    )
{
    int hsize, vsize, i;

    for(;;){
        if((vib_size>>1) <= vib_end-video_in) cleanout_buffer();
        switch(element()){
        case 0xb3:                      // sequence header
            hsize=(*vip <<4)+(*(vip+1) >>4);      mb_cols=(hsize+15)>>4;
            vsize=(*(vip+1) <<8 &0xf00)+*(vip+2); mb_rows=(vsize+15)>>4;
            if(HORIZONTAL_SIZE<hsize || VERTICAL_SIZE<vsize)
                error(IDS_ERR_PICTURE);
            set_display(hsize, vsize);
            i=*(vip+3) & 0xf;           // picture_rate
            if(i<1 || 8<i) error(IDS_ERR_VIDEO);
            interval=picture_interval[i];
            vip+=7; bit=1;
            if(extract(1))
                    for(i=0; i<64; i++) intraq[i]=extract(8)*idct_coef[i];
            else    for(i=0; i<64; i++) intraq[i]=intraq_default[i];
            if(extract(1))
                    for(i=0; i<64; i++) noninq[i]=extract(8)*idct_coef[i];
            else    for(i=0; i<64; i++) noninq[i]=noninq_default[i];
            break;
        case 0xb8:                      // GOP
            bit=6;
            i=extract(5)*60;            // time_code
            i=(i+extract(6))*60;
            bit--;
            gop_time=(i+extract(6))*1000;
            gop_pictures=extract(6);
            break;
        case 0x00:                      // picture
            picture_type=*(vip+1) >>3 &0x07;
            if(picture_type<=level) return TRUE;
            break;
        case   -1:
            return FALSE;
        }
        vip=next_header;
    }
}

//  dct_coeff_first & dct_coeff_next code table
//  bit20-16: length, bit15-8: run, bit7-0: level
static int dct_coeff0[]={
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x100112, 0x100111, 0x100110, 0x10010f,
    0x100603, 0x101002, 0x100f02, 0x100e02,
    0x100d02, 0x100c02, 0x100b02, 0x101f01,
    0x101e01, 0x101d01, 0x101c01, 0x101b01,
    0xf0028, 0xf0028, 0xf0027, 0xf0027, 0xf0026, 0xf0026, 0xf0025, 0xf0025,
    0xf0024, 0xf0024, 0xf0023, 0xf0023, 0xf0022, 0xf0022, 0xf0021, 0xf0021,
    0xf0020, 0xf0020, 0xf010e, 0xf010e, 0xf010d, 0xf010d, 0xf010c, 0xf010c,
    0xf010b, 0xf010b, 0xf010a, 0xf010a, 0xf0109, 0xf0109, 0xf0108, 0xf0108,
    0xe001f, 0xe001f, 0xe001f, 0xe001f, 0xe001e, 0xe001e, 0xe001e, 0xe001e,
    0xe001d, 0xe001d, 0xe001d, 0xe001d, 0xe001c, 0xe001c, 0xe001c, 0xe001c,
    0xe001b, 0xe001b, 0xe001b, 0xe001b, 0xe001a, 0xe001a, 0xe001a, 0xe001a,
    0xe0019, 0xe0019, 0xe0019, 0xe0019, 0xe0018, 0xe0018, 0xe0018, 0xe0018,
    0xe0017, 0xe0017, 0xe0017, 0xe0017, 0xe0016, 0xe0016, 0xe0016, 0xe0016,
    0xe0015, 0xe0015, 0xe0015, 0xe0015, 0xe0014, 0xe0014, 0xe0014, 0xe0014,
    0xe0013, 0xe0013, 0xe0013, 0xe0013, 0xe0012, 0xe0012, 0xe0012, 0xe0012,
    0xe0011, 0xe0011, 0xe0011, 0xe0011, 0xe0010, 0xe0010, 0xe0010, 0xe0010,
    0xd0a02, 0xd0a02, 0xd0a02, 0xd0a02, 0xd0a02, 0xd0a02, 0xd0a02, 0xd0a02,
    0xd0902, 0xd0902, 0xd0902, 0xd0902, 0xd0902, 0xd0902, 0xd0902, 0xd0902,
    0xd0503, 0xd0503, 0xd0503, 0xd0503, 0xd0503, 0xd0503, 0xd0503, 0xd0503,
    0xd0304, 0xd0304, 0xd0304, 0xd0304, 0xd0304, 0xd0304, 0xd0304, 0xd0304,
    0xd0205, 0xd0205, 0xd0205, 0xd0205, 0xd0205, 0xd0205, 0xd0205, 0xd0205,
    0xd0107, 0xd0107, 0xd0107, 0xd0107, 0xd0107, 0xd0107, 0xd0107, 0xd0107,
    0xd0106, 0xd0106, 0xd0106, 0xd0106, 0xd0106, 0xd0106, 0xd0106, 0xd0106,
    0xd000f, 0xd000f, 0xd000f, 0xd000f, 0xd000f, 0xd000f, 0xd000f, 0xd000f,
    0xd000e, 0xd000e, 0xd000e, 0xd000e, 0xd000e, 0xd000e, 0xd000e, 0xd000e,
    0xd000d, 0xd000d, 0xd000d, 0xd000d, 0xd000d, 0xd000d, 0xd000d, 0xd000d,
    0xd000c, 0xd000c, 0xd000c, 0xd000c, 0xd000c, 0xd000c, 0xd000c, 0xd000c,
    0xd1a01, 0xd1a01, 0xd1a01, 0xd1a01, 0xd1a01, 0xd1a01, 0xd1a01, 0xd1a01,
    0xd1901, 0xd1901, 0xd1901, 0xd1901, 0xd1901, 0xd1901, 0xd1901, 0xd1901,
    0xd1801, 0xd1801, 0xd1801, 0xd1801, 0xd1801, 0xd1801, 0xd1801, 0xd1801,
    0xd1701, 0xd1701, 0xd1701, 0xd1701, 0xd1701, 0xd1701, 0xd1701, 0xd1701,
    0xd1601, 0xd1601, 0xd1601, 0xd1601, 0xd1601, 0xd1601, 0xd1601, 0xd1601};

static int dct_coeff1[]={
    0xc000b, 0xc0802, 0xc0403, 0xc000a, 0xc0204, 0xc0702, 0xc1501, 0xc1401,
    0xc0009, 0xc1301, 0xc1201, 0xc0105, 0xc0303, 0xc0008, 0xc0602, 0xc1101};

static int dct_coeff2[]={0xa1001, 0xa0502, 0xa0007, 0xa0203};

static int dct_coeff3[]={0xa0104, 0xa0f01, 0xa0e01, 0xa0402};

static int dct_coeff4[]={
    0x00000, 0x00000, 0x00000, 0x00000, 0x60000, 0x60000, 0x60000, 0x60000,
    0x70202, 0x70202, 0x70901, 0x70901, 0x70004, 0x70004, 0x70801, 0x70801,
    0x60701, 0x60701, 0x60701, 0x60701, 0x60601, 0x60601, 0x60601, 0x60601,
    0x60102, 0x60102, 0x60102, 0x60102, 0x60501, 0x60501, 0x60501, 0x60501,
    0x80d01, 0x80006, 0x80c01, 0x80b01, 0x80302, 0x80103, 0x80005, 0x80a01,
    0x50003, 0x50003, 0x50003, 0x50003, 0x50003, 0x50003, 0x50003, 0x50003,
    0x50401, 0x50401, 0x50401, 0x50401, 0x50401, 0x50401, 0x50401, 0x50401,
    0x50301, 0x50301, 0x50301, 0x50301, 0x50301, 0x50301, 0x50301, 0x50301,
    0x40002, 0x40002, 0x40002, 0x40002, 0x40002, 0x40002, 0x40002, 0x40002,
    0x40002, 0x40002, 0x40002, 0x40002, 0x40002, 0x40002, 0x40002, 0x40002,
    0x40201, 0x40201, 0x40201, 0x40201, 0x40201, 0x40201, 0x40201, 0x40201,
    0x40201, 0x40201, 0x40201, 0x40201, 0x40201, 0x40201, 0x40201, 0x40201,
    0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101,
    0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101,
    0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101,
    0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101, 0x30101};

//  search variable length code
struct pattern{
    int         length;                 // effective length of code
    unsigned    code;                   // max 16 bits left aligned
    int         value;                  // corresponding value to code
};          // length==0 terminates pattern array (error if code!=0)

static unsigned smask[]={
    0, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00,
       0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc, 0xfffe, 0xffff};

static int vlc(                         // O matched code value
    struct pattern *p                   // I pattern array
    )
{
    unsigned c=((*vip <<8) | *(vip+1))<<(7-bit) | (*(vip+2) >>(bit+1));

    do{
        if((c & smask[p->length])==p->code){
            for(bit-=p->length; bit<0; bit+=8) vip++;
            return p->value;
        }
        p++;
    }while(p->length);
    if(p->code) error(IDS_ERR_VIDEO);   // end of pattern array
    return p->value;
}

//  tables for block
//  dct_dc_size_luminance / dct_dc_size_chrominance
static struct pattern dc_size_luminance[]={
    {3, 0x8000, 0}, {2, 0x0000, 1}, {2, 0x4000, 2}, {3, 0xa000, 3},
    {3, 0xc000, 4}, {4, 0xe000, 5}, {5, 0xf000, 6}, {6, 0xf800, 7},
    {7, 0xfc00, 8}, {0, 0x0001, 0}};

static struct pattern dc_size_chrominance[]={
    {2, 0x0000, 0}, {2, 0x4000, 1}, {2, 0x8000, 2}, {3, 0xc000, 3},
    {4, 0xe000, 4}, {5, 0xf000, 5}, {6, 0xf800, 6}, {7, 0xfc00, 7},
    {8, 0xfe00, 8}, {0, 0x0001, 0}};

//  zigzag scan
static int zigzag[]={
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63};

//  block: result block_data[y*8+x] is element [y][x] of 8 x 8 pel
static int block_data[64];
static int dc_y_past, dc_b_past, dc_r_past;

static void block(
    int block_no,                       // I block number in macroblock (0-5)
    int intra                           // I TRUE: intra macroblock
    )
{
    int dc, dc_size, next, run, level, c, i;
 
    dc=0;                               // dc is dct_zz[0] in ISO document
    if(intra){
        dc_size= block_no<4? vlc(dc_size_luminance): vlc(dc_size_chrominance);
        if(dc_size){
            dc=extract(dc_size);
            if(!(dc & 1<<(dc_size-1))) dc+=(-1<<dc_size)+1;
        }
    }

    memset(block_data, 0, sizeof(int)*64);
    i=0;
    for(next=intra; ; next=TRUE){   // FALSE only if non intra and 1st coeff
        c=((*vip <<8) | *(vip+1))>>(bit+1) & 0xff;      //  decode dct coeff
        if(c & 0x80){
            if(next){
                bit-=2; if(bit<0) bit+=8, vip++;
                if(!(c & 0x40)) break;                  // "10" end of block
            }else
            if(--bit<0) bit=7, vip++;
            c=0x0001;
        }else{
            if(3<c) c=dct_coeff4[c];
            else
            switch(c){
            case 3: c=dct_coeff3[(*(vip+1)<<8 | *(vip+2))>>(bit+7) &0x3]; break;
            case 2: c=dct_coeff2[(*(vip+1)<<8 | *(vip+2))>>(bit+7) &0x3]; break;
            case 1: c=dct_coeff1[(*(vip+1)<<8 | *(vip+2))>>(bit+5) &0xf]; break;
            default:c=dct_coeff0[(*(vip+1)<<8 | *(vip+2))>>(bit+1) &0xff];
                    if(!c) error(IDS_ERR_VIDEO);
            }
            for(bit-=c>>16; bit<0; bit+=8) vip++;
            c &=0xffff;
        }

        if(c){                                          // normal dct_coeff
            run=c>>8; level=c & 0xff;
            if(*vip >>bit & 0x01) level=-level;
            if(--bit<0) bit=7, vip++;
        }else{                                          // escape
            run=extract(6); level=extract(8);
            if(level & 0x7f) level-=(level & 0x80)<<1;
            else  level=extract(8)-((level & 0x80)<<1);
        }

        i= next? i+run+1: run;
        if(intra)
            block_data[zigzag[i]]=level*qscale*intraq[i] >>16;
        else{
            level<<=1;
            if(0<level) level++;
            else        level--;
            block_data[zigzag[i]]=level*qscale*noninq[i] >>17;
        }
    }

    if(intra){
        switch(block_no){
        case 4 :    dc+=dc_b_past; dc_b_past=dc; break;
        case 5 :    dc+=dc_r_past; dc_r_past=dc; break;
        default:    dc+=dc_y_past; dc_y_past=dc; break;
        }
        block_data[0]=dc;
    }

    if(block_no<4 || C256<=option.display) idct(block_data);
}

//  macroblock
static struct pattern mb_head[]={
    {11, 0x01e0,  1}, {11, 0x0100,  2}, { 0, 0x0000,  0}};
    //   stuffing          escape
static struct pattern mb_adrs_inc[]={
    { 1, 0x8000,  1}, { 3, 0x6000,  2}, { 3, 0x4000,  3}, { 4, 0x3000,  4},
    { 4, 0x2000,  5}, { 5, 0x1800,  6}, { 5, 0x1000,  7}, { 7, 0x0e00,  8},
    { 7, 0x0c00,  9}, { 8, 0x0b00, 10}, { 8, 0x0a00, 11}, { 8, 0x0900, 12},
    { 8, 0x0800, 13}, { 8, 0x0700, 14}, { 8, 0x0600, 15}, {10, 0x05c0, 16},
    {10, 0x0580, 17}, {10, 0x0540, 18}, {10, 0x0500, 19}, {10, 0x04c0, 20},
    {10, 0x0480, 21}, {11, 0x0460, 22}, {11, 0x0440, 23}, {11, 0x0420, 24},
    {11, 0x0400, 25}, {11, 0x03e0, 26}, {11, 0x03c0, 27}, {11, 0x03a0, 28},
    {11, 0x0380, 29}, {11, 0x0360, 30}, {11, 0x0340, 31}, {11, 0x0320, 32},
    {11, 0x0300, 33}, { 0, 0x0001,  0}};

static struct pattern mb_type[][8]={
   {{1, 0x8000, 0x01}, {2, 0x4000, 0x11}, {0, 0x0001, 0x00}},   // I picture
   {{1, 0x8000, 0x0a}, {2, 0x4000, 0x02}, {3, 0x2000, 0x08},    // P picture
    {5, 0x1800, 0x01}, {5, 0x1000, 0x1a}, {5, 0x0800, 0x12},
    {6, 0x0400, 0x11}, {0, 0x0001, 0x00}}};
    // value bit 4:quant, 3:forward, 2:backward, 1:pattern, 0:intra
static struct pattern mb_motion[]={
    { 1, 0x8000,  0}, { 2, 0x4000,  1}, { 3, 0x2000,  2}, { 4, 0x1000,  3},
    { 6, 0x0c00,  4}, { 7, 0x0a00,  5}, { 7, 0x0800,  6}, { 7, 0x0600,  7},
    { 9, 0x0580,  8}, { 9, 0x0500,  9}, { 9, 0x0480, 10}, {10, 0x0440, 11},
    {10, 0x0400, 12}, {10, 0x03c0, 13}, {10, 0x0380, 14}, {10, 0x0340, 15},
    {10, 0x0300, 16}, { 0, 0x0001,  0}};

static struct pattern mb_block_pattern[]={
    { 3, 0xe000, 60}, { 4, 0xd000,  4}, { 4, 0xc000,  8}, { 4, 0xb000, 16},
    { 4, 0xa000, 32}, { 5, 0x9800, 12}, { 5, 0x9000, 48}, { 5, 0x8800, 20},
    { 5, 0x8000, 40}, { 5, 0x7800, 28}, { 5, 0x7000, 44}, { 5, 0x6800, 52},
    { 5, 0x6000, 56}, { 5, 0x5800,  1}, { 5, 0x5000, 61}, { 5, 0x4800,  2},
    { 5, 0x4000, 62}, { 6, 0x3c00, 24}, { 6, 0x3800, 36}, { 6, 0x3400,  3},
    { 6, 0x3000, 63}, { 7, 0x2e00,  5}, { 7, 0x2c00,  9}, { 7, 0x2a00, 17},
    { 7, 0x2800, 33}, { 7, 0x2600,  6}, { 7, 0x2400, 10}, { 7, 0x2200, 18},
    { 7, 0x2000, 34}, { 8, 0x1f00,  7}, { 8, 0x1e00, 11}, { 8, 0x1d00, 19},
    { 8, 0x1c00, 35}, { 8, 0x1b00, 13}, { 8, 0x1a00, 49}, { 8, 0x1900, 21},
    { 8, 0x1800, 41}, { 8, 0x1700, 14}, { 8, 0x1600, 50}, { 8, 0x1500, 22},
    { 8, 0x1400, 42}, { 8, 0x1300, 15}, { 8, 0x1200, 51}, { 8, 0x1100, 23},
    { 8, 0x1000, 43}, { 8, 0x0f00, 25}, { 8, 0x0e00, 37}, { 8, 0x0d00, 26},
    { 8, 0x0c00, 38}, { 8, 0x0b00, 29}, { 8, 0x0a00, 45}, { 8, 0x0900, 53},
    { 8, 0x0800, 57}, { 8, 0x0700, 30}, { 8, 0x0600, 46}, { 8, 0x0500, 54},
    { 8, 0x0400, 58}, { 9, 0x0380, 31}, { 9, 0x0300, 47}, { 9, 0x0280, 55},
    { 9, 0x0200, 59}, { 9, 0x0180, 27}, { 9, 0x0100, 39}, { 0, 0x0001,  0}};

static int motion_fwd[2];               // [0]: recon_right_for_prev
                                        // [1]: recon_down_for_prev
static int motion_max, motion_min;

static int dx[]={0, 8, 0, 8};           // block position in macroblock
static int dy[]={0, 0, 8, 8};

static void macroblock(
    int   &row,                         // X position of previous macroblock
    int   &col                          // X and returns current position
    )
{
    int             type, dl, db,  fx, fy, intra, cbp, block_no;
    int             *bp, vd, c, i, j;
    unsigned char   *np, *op;

    i=0;
    while(c=vlc(mb_head)) if(c==2) i+=33;
    i+=vlc(mb_adrs_inc);
    for(col+=i; mb_cols<=col; col-=mb_cols) row++;
    if(1<i){                                // macroblock skip occured
        dc_y_past=dc_b_past=dc_r_past=128;  // 1024/8
        motion_fwd[0]=motion_fwd[1]=0;
    }

    type=vlc(mb_type[picture_type-1]);
    if(type & 0x10) qscale=extract(5);
    if(type & 0x08){                        // motion forward
        for(i=0; i<2; i++){
            dl=vlc(mb_motion)<<fwd_rsize;
            if(dl && extract(1)) dl=-dl;
            c= (0<fwd_rsize && dl)? (1<<fwd_rsize)-1-extract(fwd_rsize): 0;
            if(0<dl){dl-=c; db=dl-(1<<(fwd_rsize+5));}else
            if(dl<0){dl+=c; db=dl+(1<<(fwd_rsize+5));}else
                            db=0;
            c=motion_fwd[i]+dl;
            if(c<motion_min || motion_max<c)    motion_fwd[i]+=db;
            else                                motion_fwd[i] =c;
        }
    }else   motion_fwd[0]=motion_fwd[1]=0;
    fx=motion_fwd[0]; fy=motion_fwd[1];
    if(!full_fwd) fx>>=1, fy>>=1;           // half pel process omitted

    intra=type & 0x01;
    if(!intra) dc_y_past=dc_b_past=dc_r_past=128;
    if(type & 0x02) cbp=vlc(mb_block_pattern);
    else            cbp=intra? 0x3f: 0x00;
    for(block_no=0; block_no<6; block_no++, cbp<<=1){
        if(cbp & 0x20) block(block_no, intra);
        if(4<=block_no && option.display<C256) continue;
        switch(block_no){
        case 4 :
            np= & (*new_b)[ row<<3         ][ col<<3         ];
            op= & (*old_b)[(row<<3)+(fy>>1)][(col<<3)+(fx>>1)];
            vd=HORIZONTAL_SIZE/2 -8;
            break;
        case 5 :
            np= & (*new_r)[ row<<3         ][ col<<3         ];
            op= & (*old_r)[(row<<3)+(fy>>1)][(col<<3)+(fx>>1)];
            vd=HORIZONTAL_SIZE/2 -8;
            break;
        default:
            np= & (*new_y)[(row<<4)+dy[block_no]   ][(col<<4)+dx[block_no]   ];
            op= & (*old_y)[(row<<4)+dy[block_no]+fy][(col<<4)+dx[block_no]+fx];
            vd=HORIZONTAL_SIZE-8;
            break;
        }
        if(cbp & 0x20)
            for(i=0, bp=block_data; i<8; i++, np+=vd, op+=vd)
                for(j=0; j<8; j++){
                    c=*bp++;
                    if(!intra) c+= *op++;
                    if(255< c) c=255; else
                    if(c  < 0) c=0;
                    *np++=c;
                }
        else
            for(i=0; i<8; i++, np+=vd, op+=vd)
                for(j=0; j<8; j++) *np++=*op++;
    }
}

//  decode picture to video output buffer
static int mb_skip[VERTICAL_SIZE/16][HORIZONTAL_SIZE/16];

int decode_video(                       // O presentation time (ms) (-1: end)
    int     level                       // I decoded picture type <= level
    )
{
    int             temp_ref, row, col, c, x, y, i, j;
    unsigned char   *p;

    new_y= & video_y[1-last_picture]; old_y= & video_y[last_picture];
    new_b= & video_b[1-last_picture]; old_b= & video_b[last_picture];
    new_r= & video_r[1-last_picture]; old_r= & video_r[last_picture];

    do
        if(!picture(level)) return -1;
    while(gop_time==-1);
    temp_ref=(*vip <<2)+(*(vip+1) >>6);
    vip+=3; bit=2;
    if(2<=picture_type){
        for(row=0; row<mb_rows; row++) for(col=0; col<mb_cols; col++)
            mb_skip[row][col]=TRUE;
        full_fwd =extract(1);
        fwd_rsize=extract(3)-1;
        motion_max=(1<<(fwd_rsize+4))-1;
        motion_min=-1<<(fwd_rsize+4);
    }

    do{                                 // waste until slice
        c=header();
        if(c==-1) return -1;
    }while(c<0x01 || 0xaf<c);

    do{                                 // slice
        row=c-2; col=mb_cols-1;
        qscale=*vip >>3 & 0x1f;
        if(*vip & 0x04) error(IDS_ERR_VIDEO);
                                        // assume no extra information
        bit=1;
        dc_y_past=dc_b_past=dc_r_past=128;
        motion_fwd[0]=motion_fwd[1]=0;
        do{
            macroblock(row, col);
            mb_skip[row][col]=FALSE;
            p= bit==7? vip: vip+1;
        }while(*p || *(p+1) || (*(p+2) & 0xfe));
        c=header();
    }while(0x01<=c && c<=0xaf);         // end of slice
    if(c!=-1) vip-=4;                   // back to next header start

    if(picture_type==2)                 // skipped macroblocks in P picture
        for(row=0; row<mb_rows; row++) for(col=0; col<mb_cols; col++)
            if(mb_skip[row][col]){
                x=col<<4; y=row<<4;
                for(i=0; i<16; i++) for(j=0; j<16; j++)
                    (*new_y)[y+i][x+j]=(*old_y)[y+i][x+j];
                if(option.display < C256) continue;
                x=col<<3; y=row<<3;
                for(i=0; i<8; i++) for(j=0; j<8; j++){
                    (*new_b)[y+i][x+j]=(*old_b)[y+i][x+j];
                    (*new_r)[y+i][x+j]=(*old_r)[y+i][x+j];
                }
            }
 
    last_picture=1-last_picture;
    return gop_time+((gop_pictures+temp_ref)*interval >>6);
}

//  output video data to screen
void output_video()
{
    draw_picture(*new_y, *new_b, *new_r);
}
