/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    audio.cpp      : MPEG-1 audio decoder
*/

//  notice: this program assumes >> operator preserves sign for signed integer.

#include <windows.h>
#include <stdlib.h>
#include "filter.h"
#include "resource.h"
#include "wodeon.h"

//  audio parameters
static int  header  =0;                 // header bit pattern for check
static int  bit_rate=0;                 // 0-14
static int  fsample =0;                 // 0-2 sampling frequency
static int  mode    =0;                 // 0-3
static int  mode_ext=0;                 // 0-3 mode extension
static int  channels=1;                 // 2 if data and option is stereo

//  audio input buffer
static unsigned char    audio_in[AUDIO_BUFF_SIZE+2];
static unsigned char   *aib_end;        // points just behind filled area
static unsigned char   *aip;            // current byte position
static int              bit;            // current bit  position
                                        // 0-7, MSB:7 LSB:0
//  audio intermediate buffer
static int              vbuff[2][256];
static int              vhead[2];       // head of ring buffer

//  initialize audio decoder state
void reset_audio(
    int all                             // I FALSE: clear buffers only
    )
{
    int ch, i;

    aib_end=aip=audio_in;
    for(ch=0; ch<2; ch++) for(i=0; i<256; i++) vbuff[ch][i]=0;
    vhead[0]=vhead[1]=0;
    if(all) header=0;
}

//  get pointer to audio input buffer
unsigned char *get_audio_in(            // O points to empty area
    int length                          // I buffer length required
    )
{
    unsigned char *p=aib_end;

    if(audio_in+AUDIO_BUFF_SIZE-aib_end < length) error(IDS_ERR_BUFFER);
    aib_end+=length;
    return p;
}

//  put input buffer contents in front
static void cleanout_buffer()
{
    unsigned char *p=audio_in;
    while(aip<aib_end) *p++=*aip++;
    aib_end=p; aip=audio_in;
}

//  seek syncword, aip points syncword at return
static int sync()                       // O FALSE: reach buffer end
{
    for(; aip+3 < aib_end; aip++)       // find ff fx series
        if(*aip==0xff && (*(aip+1) & 0xf0)==0xf0) return TRUE;
    return FALSE;
}

//  extract n bits
static unsigned emask[]={
    0, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
       0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff};

static int extract(                     // O right aligned bits
    int n                               // I size 1-16
    )
{
    int v=(*aip<<16 | *(aip+1)<<8 | *(aip+2))>>(bit+17-n) & emask[n];
    for(bit-=n; bit<0; bit+=8) aip++;
    return v;
}

//  subband filter
//  notice: as sampling rate is reduced to 1/4, subbands only 0 - 7 are used
//  and sizes of Nij, Vi, Di are also 1/4.

const int c1=2009;                      // cn=2048*cos(n*pi/16)
const int c2=1892;
const int c3=1703;
const int c4=1448;
const int c5=1138;
const int c6= 784;
const int c7= 400;

static void sb_filter(
    int     s[],                        // I rescaled sample
    int     ch,                         // I 0 or 1
    WORD  *&audio_out                   // X output buffer position
    )
{
    int *v, h, t0, t1, t2, t3, t4, t5, t6, t7, t, sum, i, j;

    // DCT, coefficient Nik=-cos((4+i)*(2k+1)*pi/16)/8
    v=vbuff[ch]+vhead[ch];

    t0=s[0]+s[7]; t1=s[1]+s[6]; t2=s[2]+s[5]; t3=s[3]+s[4];
    t7=s[0]-s[7]; t6=s[1]-s[6]; t5=s[2]-s[5]; t4=s[3]-s[4];

    v[12]=   (t0+t1+t2+t3)>>3;
    v[ 8]=c4*(t0-t1-t2+t3)>>14; v[0]=-v[8];

    t=c6*(t0+t1-t2-t3);
    v[10]=v[14]=(t+(c2-c6)*(t0-t3))>>14;
    v[ 6]      =(t-(c2+c6)*(t1-t2))>>14; v[2]=-v[6];

    t0=(c7-c3)*(t4+t7);
    t1=(c1+c3)*(t5+t6);
    t=c3*(t4+t5+t6+t7);
    t2=(c5+c3)*(t4+t6)-t;
    t3=(c5-c3)*(t5+t7)+t;

    v[ 5]=      ( t0-t2+(-c1+c3+c5-c7)*t4)>>14; v[3]=-v[5];
    v[ 7]=      (-t1+t3+( c1+c3-c5+c7)*t5)>>14; v[1]=-v[7];
    v[ 9]=v[15]=(-t1-t2+( c1+c3+c5-c7)*t6)>>14;
    v[11]=v[13]=( t0+t3+( c1+c3-c5-c7)*t7)>>14;
    // end of DCT, v[4] is always 0

    v=vbuff[ch];
    h=vhead[ch];
    for(j=0; j<8; j++){
        sum=0;
        // i 0-7 & 120-127 is skipped as Dcoeff is 0
        for(i=j+8; i<120; i+=8)
            sum+=Dcoeff[i]*v[(((i&0x78)<<1)|(i&0x0f))+h & 0xff];
        *audio_out=sum>>7;
        audio_out+=channels;
    }
    vhead[ch]=vhead[ch]-16 & 0xff;
}

//  tables for layer II

//  select from Table 3-B.2x in ISO document (0:a 1:b 2:c 3:d 9:illegal)
//  index 1st: 0 is single channel, 2nd: sampling frequency, 3rd: bit rate
static int select_table[2][3][15]={
    {{1, 2, 2, 0, 0, 0, 1, 1, 1, 1, 1, 9, 9, 9, 9}, // single channel mode
     {0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9},
     {1, 3, 3, 0, 0, 0, 1, 1, 1, 1, 1, 9, 9, 9, 9}},
    {{1, 9, 9, 9, 2, 9, 2, 0, 0, 0, 1, 1, 1, 1, 1}, // other
     {0, 9, 9, 9, 2, 9, 2, 0, 0, 0, 0, 0, 0, 0, 0},
     {1, 9, 9, 9, 3, 9, 3, 0, 0, 0, 1, 1, 1, 1, 1}}};

static int sblimit_table[4]={27, 30, 8, 12};

//  index 1st: select_table[], 2nd: sb
static int nbal[4][30]={
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3,
     3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2},
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3,
     3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2},
    {4, 4, 3, 3, 3, 3, 3, 3},
    {4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}};

// index 1st: select_table[], 2nd: sb
static int qsub[4][30]={
    {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
     2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3},
    {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
     2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3},
    {4, 4, 5, 5, 5, 5, 5, 5},
    {4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}};

// index 1st: qsub[][], 2nd: allocation
static int qindex[6][16]={
    {0, 0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
    {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16},
    {0, 0, 1, 2, 3, 4, 5, 16},
    {0, 0, 1, 16},
    {0, 0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {0, 0, 1, 3, 4, 5, 6, 7}};

//  Table 3-B.4 in ISO document
struct qclass_type{
    int nlevels;
    int C, D;
    int group;
    int bits;
};
//  index: qindex[]
static struct qclass_type qclass[17]={
    {    3, 21845,     -1, TRUE,   5}, {    5, 13107,     -2, TRUE,   7},
    {    7,  9362,     -3, FALSE,  3}, {    9,  7282,     -4, TRUE,  10},
    {   15,  4369,     -7, FALSE,  4}, {   31,  2114,    -15, FALSE,  5},
    {   63,  1040,    -31, FALSE,  6}, {  127,   516,    -63, FALSE,  7},
    {  255,   257,   -127, FALSE,  8}, {  511,   128,   -255, FALSE,  9},
    { 1023,    64,   -511, FALSE, 10}, { 2047,    32,  -1023, FALSE, 11},
    { 4095,    16,  -2047, FALSE, 12}, { 8191,     8,  -4095, FALSE, 13},
    {16383,     4,  -8191, FALSE, 14}, {32767,     2, -16383, FALSE, 15},
    {65535,     1, -32767, FALSE, 16}};

//  16384 * value of table 3-B.1 in ISO document
static int scale[64]={                  // last element added for safety
    32768, 26007, 20643, 16384, 13004, 10321,  8192,  6502,
     5161,  4096,  3251,  2580,  2048,  1625,  1290,  1024,
      813,   645,   512,   406,   323,   256,   203,   161,
      128,   102,    81,    64,    51,    40,    32,    25,
       20,    16,    13,    10,     8,     6,     5,     4,
        3,     3,     2,     2,     1,     1,     1,     1,
        1,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0};

//  layer II decoder
static int allocation[2][30], scfsi[2][30], scalefactor[2][30][3];
static int sample[2][3][30];

static void layer2(
    WORD *&audio_out                    // X output buffer position
    )
{
    int     chs, select, sblimit, bound, s[3], *sf, gr, sb, ch, c, f, i;
    div_t   t;
    struct qclass_type *p;

    chs= mode==3? 1: 2;
    select=select_table[chs-1][fsample][bit_rate];
    if(select==9) error(IDS_ERR_AUDIO);
    sblimit=sblimit_table[select];
    bound= mode==1? (mode_ext+1)<<2: 32;

    for(sb=0; sb<sblimit; sb++) for(ch=0; ch<chs; ch++){
        allocation[ch][sb]=c=extract(nbal[select][sb]);
        if(bound<=sb){
            allocation[1][sb]=c;
            break;                      // break from ch if intensity stereo
        }
    }

    for(sb=0; sb<sblimit; sb++) for(ch=0; ch<chs; ch++)
        if(allocation[ch][sb]) scfsi[ch][sb]=extract(2);

    for(sb=0; sb<sblimit; sb++) for(ch=0; ch<chs; ch++){
        if(!allocation[ch][sb]) continue;
        sf=scalefactor[ch][sb];
        switch(scfsi[ch][sb]){
        case 0: sf[0]=extract(6); sf[1]=extract(6); sf[2]=extract(6); break;
        case 1: sf[0]=            sf[1]=extract(6); sf[2]=extract(6); break;
        case 2: sf[0]=            sf[1]=            sf[2]=extract(6); break;
        case 3: sf[0]=extract(6); sf[1]=            sf[2]=extract(6); break;
        }
    }

    for(gr=0; gr<12; gr++){
        for(sb=0; sb<sblimit; sb++) for(ch=0; ch<chs; ch++){
            if(allocation[ch][sb]){
                if(ch==0 || sb<bound){
                    p= & qclass[qindex[qsub[select][sb]][allocation[ch][sb]]];
                    if(p->group){
                        c=extract(p->bits);
                        if(sb<8 && ch<channels){    // * see sb_filter() notice
                            t=div(c,      p->nlevels); s[0]=t.rem;
                            t=div(t.quot, p->nlevels); s[1]=t.rem;
                                                       s[2]=t.quot;
                        }
                    }else
                        for(i=0; i<3; i++) s[i]=extract(p->bits);
                }
                if(sb<8 && ch<channels){            // *
                    f=p->C * scale[scalefactor[ch][sb][gr>>2]];
                    for(i=0; i<3; i++)
                        sample[ch][i][sb]=f*(s[i] + p->D) >>15;
                }
            }else   sample[ch][0][sb]=sample[ch][1][sb]=sample[ch][2][sb]=0;
        }
        
        for(i=0; i<3; i++) sb_filter(sample[0][i], 0, audio_out);
        if(channels==1) continue;
        audio_out-=47;
        for(i=0; i<3; i++) sb_filter(sample[1][i], 1, audio_out);
        audio_out--;
    }
}

//  check header bit pattern
static int check_header()               // O TRUE: consistent
{
    int i;

    i=(*(aip+1) & 0x0e)<<8 | (*(aip+2) & 0xfc);
    if(header==0) header=i;
    return header==i;
}

//  decode one frame of audio
static int rate[]={11025, 12000, 8000}; // sampling frequency / 4
static int distance[3][15]={            // between two consecutive syncwords
    {0, 104,  156,  182,  208,  261,  313,  365,
        417,  522,  626,  731,  835, 1044, 1253},
    {0,  96,  144,  168,  192,  240,  288,  336,
        384,  480,  576,  672,  768,  960, 1152},
    {0, 144,  216,  252,  288,  360,  432,  504,
        576,  720,  864, 1008, 1152, 1440, 1728}};
 
int decode_audio(                       // O sampling rate(Hz) (0: end)
    WORD  *&audio_out,                  // X output buffer position
    int    &rchannels                   // O 1: mono, 2: stereo output
    )
{
    int             layer, protect, n;
    unsigned char   *start_point;

    for(;;){                            // find frame header
        while(!sync())
            if(packet(FALSE, -1)<0) return 0;
        layer   =*(aip+1) >>1 & 0x03;
        protect =*(aip+1)     & 0x01;
        bit_rate=*(aip+2) >>4 & 0x0f;
        fsample =*(aip+2) >>2 & 0x03;
        mode    =*(aip+3) >>6 & 0x03;
        mode_ext=*(aip+3) >>4 & 0x03;
        rchannels=channels= (mode<=2 && option.stereo)? 2: 1;
        if(header==0){                  // first header
            if(layer!=2)     error(IDS_ERR_LAYER);
            if(bit_rate==15) error(IDS_ERR_AUDIO);
            if(fsample==3)   error(IDS_ERR_AUDIO);
        }
        if(check_header()) break;
        aip++;
    }
                                        // read a frame
    if(bit_rate){
        n=distance[fsample][bit_rate];
        if(fsample==0 && (*(aip+2) & 0x02)) n++;
        while(aib_end-aip < n)
            if(packet(FALSE, -1)<0) return 0;
    }else{                              // free format
        start_point=aip; aip+=4;
        for(;;)
            if(sync()){
                if(check_header())  break;
                else aip++;
            }else
            if(packet(FALSE, -1)<0) break;
        aip=start_point;
    }

    aip+=4;
    if(!protect) aip+=2;
    bit=7;
    layer2(audio_out);
    if(AUDIO_BUFF_SIZE/2 < aib_end-audio_in) cleanout_buffer();
    return rate[fsample];
}
