/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    play.cpp       : player timing control
*/

#include <windows.h>
#include "resource.h"
#include "wodeon.h"

//  INTRO is audio state between play start and sound start
enum play_state {STOP, INTRO, RUN, PAUSE};
static enum play_state audio_state=STOP;
static enum play_state video_state=STOP;

static enum search search_state=NONE;

static DWORD    start_time;             // play start system time
static int      picture_time;           // presentation time
static DWORD    delay_time[]={0, 300, 600, 900};
                                        // audio delay to video (ms)
static int      audio_time, audio_rem;  // audio play time

//  audio output buffer
WORD   audio_out   [WAVE_BUFFERS][FRAMES_PER_BUFFER*576];
int    audio_length[WAVE_BUFFERS];
int    audio_rate;
int    audio_next;                      // audio_out[] used next
int    channels;                        // 1:mono, 2:stereo
//  if channels==2, audio_out[i][0]=L, audio_out[i][1]=R,
//                  audio_out[i][2]=L, audio_out[i][3]=R ...

//  decode audio frames
static int audio_block()                // O FALSE: end
{
    int     frame, rate;
    WORD    *p;

    p=audio_out[audio_next];
    for(frame=0; frame<FRAMES_PER_BUFFER; frame++){
        rate=decode_audio(p, channels);
        if(!rate) break;
        audio_rate=rate;
    }
    audio_length[audio_next]=p-audio_out[audio_next];
    return frame;
}

//  start/pause/restart play
void start_play()
{
    if(audio_state==STOP && video_state==STOP){     // start play
        open_file();
        reset_video();     reset_display();
        reset_audio(TRUE); reset_pcm();

        if(option.video_enable){
            picture_time=decode_video(1);
            video_state= 0<=picture_time? RUN: STOP;
        }

        if(option.audio_enable){
            for(audio_next=0; audio_next<WAVE_BUFFERS; audio_next++)
                if(!audio_block()) break;
            audio_state= audio_next==0? STOP: INTRO;
        }

        search_state=NONE;
        start_time=GetTickCount();
        audio_time=delay_time[option.audio_delay]; audio_rem=0;
    }else
    if(audio_state==PAUSE || video_state==PAUSE){   // restart play
        start_time=GetTickCount()-start_time;
        if(audio_state==PAUSE){
            pcm_restart();
            audio_state=RUN;
        }
        if(video_state==PAUSE) video_state=RUN;
    }else
    if((audio_state==RUN || video_state==RUN) && search_state==NONE){
        start_time=GetTickCount()-start_time;       // pause play
        if(audio_state==RUN){
            pcm_pause();
            audio_state=PAUSE;
        }
        if(video_state==RUN) video_state=PAUSE;
    }
}

//  check time for next audio frames and picture
void keep_time()
{
    DWORD   time, t;

    time=GetTickCount()-start_time;

    if(video_state==RUN && (DWORD)picture_time<=time){
        output_video();
        if(option.audio_enable && 300<=time-picture_time)
                // thin out pictures to catch up with audio
                picture_time=decode_video(1);
        else    picture_time=decode_video(option.picture_level);
        if(picture_time<0) video_state=STOP;
    }else
        fill_buff(0);                   // precedent net stream input

    if(audio_state==INTRO && audio_time<=(int)time){
        for(audio_next=0; audio_next<WAVE_BUFFERS; audio_next++){
            if(audio_length[audio_next]==0) break;
            pcm_out(audio_out[audio_next], audio_length[audio_next],
                    audio_next, audio_rate, channels);
        }
        audio_state= audio_next==WAVE_BUFFERS? RUN: STOP;
        audio_next=0;
    }

    if(audio_state==RUN && pcm_done(audio_next)){
        // correct a lag between audio and system time
        if(channels==2) audio_rem+=audio_length[audio_next]>>1;
        else            audio_rem+=audio_length[audio_next];
        for(; audio_rem>=audio_rate; audio_rem-=audio_rate) audio_time+=1000;
        t=audio_rem*1000/audio_rate +audio_time;
        if(time+100 <=t) start_time-=30; else
        if(t+100 <=time) start_time+=30;

        if(audio_block()){
            pcm_out(audio_out[audio_next], audio_length[audio_next],
                    audio_next, audio_rate, channels);
            if(++audio_next==WAVE_BUFFERS) audio_next=0;
        }else
            audio_state=STOP;
    }
}

//  wind up player
void quit_play()
{
    audio_state=video_state=STOP;
    search_state=NONE;
    reset_pcm();
    close_file();
}

//  check if player runnning
int play_busy()
{
    return video_state==RUN  || audio_state==RUN ||
          (video_state==STOP && audio_state==INTRO);
}

//  forward/rewind action
int fwd_rew()                           // O TRUE: forward/rewind is processed
{
    static DWORD    last_tick;
    static int      next_time;
    enum search     sc;
    DWORD           now;
    int             t;
    MSG             msg;

    sc=search_control();
    if(sc==NONE){
        if(search_state==NONE) return FALSE;
                                        // button released
        if(option.video_enable){
            picture_time=decode_video(1);
            if(picture_time<0) video_state=STOP;
        }
        if(option.audio_enable){
            if(!option.video_enable && search_state==REW) rewind(next_time);
            search_state=NONE;          // enable audio packet (see packet())
            reset_audio(FALSE); reset_pcm();
            audio_state=STOP;
            t=packet(FALSE, next_time);
            if(0<=t){
                next_time=t;
                for(audio_next=0; audio_next<WAVE_BUFFERS; audio_next++)
                    if(!audio_block()) break;
                if(0<audio_next) audio_state=INTRO;
            }
        }
        search_state=NONE;
        start_time= video_state==PAUSE? next_time: GetTickCount()-next_time;
        audio_time=next_time+delay_time[option.audio_delay]; audio_rem=0;
        return TRUE;
    }

    now=GetTickCount();
    if(search_state==NONE){
        if(option.audio_enable)
            if(audio_state==PAUSE || video_state==PAUSE)
                 next_time=start_time;
            else next_time=now-start_time;
        else     next_time=picture_time;
    }else
    if(search_state==sc && now-last_tick<800) return TRUE;
    search_state=sc;
    if(sc==FWD) next_time+=FWD_INTERVAL;
    else        next_time-=REW_INTERVAL;
    if(next_time<0) next_time=0;

    if(audio_state==STOP && video_state==STOP){
        next_time=0;
        if(sc==REW) next_time=last_slot()-3000;
        if(next_time<0){
            search_state=NONE;
            return FALSE;
        }
        open_file();
        reset_video();        reset_display();
        reset_audio(sc==FWD); reset_pcm();
        if(option.audio_enable) audio_state=PAUSE;
        if(option.video_enable) video_state=PAUSE;
    }

    if(video_state!=STOP){
        reset_video(); file_pos(FALSE);
        if(sc==REW) rewind(next_time);
        t=packet(TRUE, next_time);
        if(search_control()!=sc){       // for fast button-release response
            if(sc==FWD) next_time-=FWD_INTERVAL;
            else        next_time+=REW_INTERVAL;
            if(next_time<0) next_time=0;
            reset_video(); file_pos(TRUE);
        }else
        if(0<=t && 0<=decode_video(1)) output_video();
        else
        if(sc==FWD) next_time-=FWD_INTERVAL;
    }
                                        // remove auto-repeat keys
    while(PeekMessage(&msg, NULL, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    last_tick=GetTickCount();
    return TRUE;
}

//  check if under search
int search_busy()                       // O TRUE: under search
{
    return search_state!=NONE;
}

//  jump to designated position
static int jump_time;

BOOL CALLBACK jump_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
    int     t, i;
    TCHAR   text[STRING_SIZE];
    HWND    cb_hour, cb_min;

    cb_hour=GetDlgItem(hDlg, IDC_HOUR);
    cb_min =GetDlgItem(hDlg, IDC_MIN );
    switch(uMsg){
    case WM_INITDIALOG:
        t=jump_time/60000;              // msec -> min
        if(30000<= jump_time%60000) t++;
        for(i=0; i<4; i++){
            wsprintf(text, TEXT("%2d"), i);
            SendMessage(cb_hour, CB_ADDSTRING, 0, (long)text);
        }
        wsprintf(text, TEXT("%2d"), t/60);
        SetWindowText(cb_hour, text);
        for(i=0; i<60; i+=5){
            wsprintf(text, TEXT("%2d"), i);
            SendMessage(cb_min,  CB_ADDSTRING, 0, (long)text);
        }
        wsprintf(text, TEXT("%2d"), t%60);
        SetWindowText(cb_min,  text);
        SetFocus(GetDlgItem(hDlg, IDC_RESTART));
        return TRUE;
    case WM_COMMAND:
        switch(wParam){
        case IDOK:
            GetWindowText(cb_hour, text, STRING_SIZE-1);
            t=_ttoi(text); if(t<0) t=0;
            jump_time=t*3600000;
            GetWindowText(cb_min,  text, STRING_SIZE-1);
            t=_ttoi(text); if(t<0) t=0;
            jump_time+=t*60000;
            EndDialog(hDlg, 0);
            return TRUE;
        case IDC_RESTART:
            jump_time=0;
            EndDialog(hDlg, 0);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, 1);
            return TRUE;
        }
    }
    return FALSE;
}

void jump()
{
    int     t;
    WORD   *p;

    if(!is_local() || (video_state==STOP && audio_state==INTRO) ||
       search_state!=NONE) return;

    if(play_busy()) start_play();       // pause play if running
    show_state();
    if(audio_state==STOP && video_state==STOP) jump_time=0;          else
    if(option.audio_enable)                    jump_time=start_time; else
                                               jump_time=picture_time;

    if(DialogBox(instance, MAKEINTRESOURCE(IDD_JUMP), main_window, jump_proc))
        return;
    if(jump_time==0){
        quit_play(); start_play();
        return;
    }
    show_message(IDS_SEEK);

    if(audio_state==STOP && video_state==STOP){
        open_file();
        reset_video();     reset_display();
        reset_audio(TRUE); reset_pcm();
        // get time_base & format header
        if(option.video_enable) decode_video(1);
        if(option.audio_enable){
            p=audio_out[0];
            decode_audio(p, channels);
        }
    }

    if(option.video_enable){
        seek(jump_time);
        reset_video(); reset_audio(FALSE);
        video_state=STOP;
        picture_time=decode_video(1);
        if(0<=picture_time){
            jump_time=picture_time;
            video_state=RUN;
        }
    }

    if(option.audio_enable){
        if(!option.video_enable) seek(jump_time);
        reset_audio(FALSE); reset_pcm();
        audio_state=STOP;
        t=packet(FALSE, jump_time);
        if(0<=t){
            jump_time=t;
            for(audio_next=0; audio_next<WAVE_BUFFERS; audio_next++)
                if(!audio_block()) break;
            if(0<audio_next) audio_state=INTRO;
        }
    }

    show_message(0);
    start_time=GetTickCount()-jump_time;
    audio_time=jump_time+delay_time[option.audio_delay]; audio_rem=0;
}
