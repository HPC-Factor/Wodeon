/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    idct.cpp       : inverse discrete cosine transform
*/

//  notice: this program assumes >> operator preserves sign for signed integer.

#include "idct.h"

const int c2=3784;                      // cn=2048*2*cos(n*pi/16)
const int c4=2896;
const int c6=1567;

//  8 x 8 inverse DCT based on Tseng and Miller's algorithm
//  this is not a complete IDCT alone. some multiplications are folded into
//  quantizer matrix of video decoder. see idctcoef.h

void idct(
    int data[64]                        // X data[y*8+x] is element [y][x]
    )
{
    int i, *d, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9;

    for(i=0, d=data; i<8; i++, d+=8){   // 1 dimentional IDCT of each row
        s1=d[1]+d[7]; s3=d[5]+d[3]; s0=s1+s3; s1-=s3;
        s2=d[1]-d[7]; s7=d[5]-d[3];
        s9=d[2]+d[6];

        s3=c6*(s2+s7); s2=((c6+c2)*s2-s3)>>11; s7=((c6-c2)*s7-s3)>>11;

        s2-=s0; s1=s2-(c4*s1 >>11);
        s5=d[0]-d[4];
        s6=d[0]+d[4];
        s8=(c4*(d[2]-d[6]) >>11)-s9;

        s3=s5+s8; s4=s6+s9;
        s5-=s8;   s6-=s9;   s7-=s1;

        d[0]=s4+s0; d[1]=s3+s2; d[2]=s5-s1; d[3]=s6-s7;
        d[7]=s4-s0; d[6]=s3-s2; d[5]=s5+s1; d[4]=s6+s7;
    }

    for(i=0, d=data; i<8; i++, d++){    // 1 dimentional IDCT of each column
        s1=d[8]+d[56]; s3=d[40]+d[24]; s0=s1+s3; s1-=s3;
        s2=d[8]-d[56]; s7=d[40]-d[24];
        s9=d[16]+d[48];

        s3=c6*(s2+s7); s2=((c6+c2)*s2-s3)>>11; s7=((c6-c2)*s7-s3)>>11;

        s2-=s0; s1=s2-(c4*s1 >>11);
        s5=d[0]-d[32];
        s6=d[0]+d[32];
        s8=(c4*(d[16]-d[48]) >>11)-s9;

        s3=s5+s8; s4=s6+s9;
        s5-=s8;   s6-=s9;   s7-=s1;

        d[ 0]=s4+s0; d[ 8]=s3+s2; d[16]=s5-s1; d[24]=s6-s7;
        d[56]=s4-s0; d[48]=s3-s2; d[40]=s5+s1; d[32]=s6+s7;
    }
}
