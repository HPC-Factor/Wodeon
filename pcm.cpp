/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    pcm.cpp        : PCM audio output
*/

#include <windows.h>
#include "resource.h"
#include "wodeon.h"

static int          active  =FALSE;     // TRUE: audio output device opened
static WAVEFORMATEX wformat ={WAVE_FORMAT_PCM, 0, 0, 0, 0, 16, 0};
static HWAVEOUT     hwave;
static WAVEHDR      whead[WAVE_BUFFERS];

//  wind up PCM output
void reset_pcm()
{
    int i;

    if(!active) return;
    waveOutReset(hwave);
    for(i=0; i<WAVE_BUFFERS; i++)
        waveOutUnprepareHeader(hwave, &whead[i], sizeof(WAVEHDR));
    waveOutClose(hwave);
    active=FALSE;
}

//  output wave data to audio device
void pcm_out(
    WORD   *buffer,                     // I wave data
    int     length,                     // I length of data by the WORD
    int     bufferID,                   // I 0 to WAVE_BUFFERS-1
    int     rate,                       // I sampling rate (Hz)
    int     channels                    // I 1 or 2
    )
{
    WAVEHDR    *wp;
    UINT        u;

    if(!active){
        wformat.nChannels       =channels;
        wformat.nSamplesPerSec  =rate;
        wformat.nAvgBytesPerSec =rate*channels <<1;
        wformat.nBlockAlign     =channels <<1;
        for(u=0; ; u++){
            if(waveOutGetNumDevs()<=u) error(IDS_ERR_WAVE);
            if(waveOutOpen(&hwave, WAVE_MAPPER, &wformat, NULL, NULL,
                           CALLBACK_NULL)==MMSYSERR_NOERROR) break;
        }
        active=TRUE;
    }

    wp=&whead[bufferID];
    wp->lpData          =(LPSTR)buffer;
    wp->dwBufferLength  =length<<1;
    wp->dwFlags         =0;
    wp->lpNext          =NULL;
    wp->reserved        =0;
    if(waveOutPrepareHeader(hwave, wp, sizeof(WAVEHDR))
        !=MMSYSERR_NOERROR) error(IDS_ERR_WAVE);
    if(waveOutWrite(hwave, wp, sizeof(WAVEHDR))
        !=MMSYSERR_NOERROR) error(IDS_ERR_WAVE);
}

//  check if buffer output finished
int pcm_done(                           // O TRUE: finished
    int bufferID                        // I select buffer
    )
{
    if(whead[bufferID].dwFlags & WHDR_DONE){
        waveOutUnprepareHeader(hwave, &whead[bufferID], sizeof(WAVEHDR));
        return TRUE;
    }
    return FALSE;
}

//  pause / restart PCM output
void pcm_pause()
{
    if(active) waveOutPause(hwave);
}

void pcm_restart()
{
    if(active) waveOutRestart(hwave);
}

//  up/down volume
void change_volume(
    int up                              // I TRUE: up, FALSE: down
    )
{
    OSVERSIONINFO   ver;
    DWORD           vol;
    int             v;

    ver.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);
    if(!GetVersionEx(&ver) || ver.dwMajorVersion<=1) return;

    if(waveOutGetVolume(NULL, &vol)!=MMSYSERR_NOERROR) return;
    v=(int)(vol & 0xffff)+(up? 0x2000: -0x2000);
    if(0xffff<v) v=0xffff; else
    if(v<0)      v=0;
    waveOutSetVolume(NULL, MAKELONG(v, v));
}
