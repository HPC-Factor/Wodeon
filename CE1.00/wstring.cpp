/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    wstring.cpp    : substitutes for widechar functions
*/

//  these functions are only for the earliest CE1.0 devices as I use CE2.0 SDK.

#include <windows.h>

int MultiByteToWideChar(
    UINT,
    DWORD,
    LPCSTR  lpMultiByteStr,
    int,
    LPWSTR  lpWideCharStr,
    int     cchWideChar)
{
	return mbstowcs(lpWideCharStr, lpMultiByteStr, cchWideChar);
}

int WideCharToMultiByte(
    UINT,
    DWORD,
    LPCWSTR lpWideCharStr,
    int,
    LPSTR   lpMultiByteStr,
    int     cbMultiByte,
    LPCSTR,
    LPBOOL)
{
    return wcstombs(lpMultiByteStr, lpWideCharStr, cbMultiByte);
}
