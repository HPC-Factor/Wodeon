/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    idctcoef.h     : inverse discrete cosine transform coefficient
*/

//  8192*Cm*Cn, C0=0.5/sqrt(2), Cn=0.5*cos(n*pi/16)
//  arranged in zigzag order
static const int idct_coef[64]={
    1024, 1420, 1420, 1338, 1970, 1338, 1204, 1856,
    1856, 1204, 1024, 1670, 1748, 1670, 1024,  805,
    1420, 1573, 1573, 1420,  805,  554, 1116, 1338,
    1416, 1338, 1116,  554,  283,  769, 1051, 1204,
    1204, 1051,  769,  283,  392,  724,  946, 1024,
     946,  724,  392,  369,  652,  805,  805,  652,
     369,  332,  554,  632,  554,  332,  283,  435,
     435,  283,  222,  300,  222,  153,  153,   78};

//  idct_coef * default intra quantizer matrix
static const int intraq_default[64]={
     8192, 22720, 22720, 25422, 31520, 25422, 26488, 40832,
    40832, 26488, 22528, 36740, 45448, 40080, 26624, 21735,
    38340, 42471, 40898, 36920, 20930, 14404, 30132, 36126,
    38232, 38802, 32364, 16066,  9622, 26146, 35734, 34916,
    34916, 30479, 20763,  7641, 11368, 20996, 30272, 32768,
    32164, 24616, 14504, 14022, 24124, 28175, 28175, 22168,
    12915, 12616, 21052, 25280, 22160, 13280, 13584, 20880,
    20010, 13018, 12432, 16800, 12876, 10557, 10557,  6474};

//  idct_coef * default non intra quantizer matrix
static const int noninq_default[64]={
    16384, 22720, 22720, 21408, 31520, 21408, 19264, 29696,
    29696, 19264, 16384, 26720, 27968, 26720, 16384, 12880,
    22720, 25168, 25168, 22720, 12880,  8864, 17856, 21408,
    22656, 21408, 17856,  8864,  4528, 12304, 16816, 19264,
    19264, 16816, 12304,  4528,  6272, 11584, 15136, 16384,
    15136, 11584,  6272,  5904, 10432, 12880, 12880, 10432,
     5904,  5312,  8864, 10112,  8864,  5312,  4528,  6960,
     6960,  4528,  3552,  4800,  3552,  2448,  2448,  1248};
