/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    idct.h         : inverse discrete cosine transform
*/

//  2 dimensions 8 x 8 inverse DCT
extern void idct(
    int data[64]                        // X data[y*8+x] is element [y][x]
    );
