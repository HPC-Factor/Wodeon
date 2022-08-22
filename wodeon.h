/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    wodeon.h       : wodeon main
*/

//  limits
#define HORIZONTAL_SIZE     352         // picture size (multiple of 16)
#define VERTICAL_SIZE       288
#define VIDEO_BUFF_SIZE1 150000         // video input buffer in bytes
#define VIDEO_BUFF_SIZE2 450000
#define AUDIO_BUFF_SIZE   30000         // audio input buffer in bytes
#define WAVE_BUFFERS          4         // no. of wave output buffers
#define FRAMES_PER_BUFFER    10         // audio frames per buffer
#define STRING_SIZE         100         // resource string size
#define LEFT_MARGIN          48         // HPC display button area
#define TOP_MARGIN           20         // PPC display button area
#define FWD_INTERVAL       5000         // forward/rewind search
#define REW_INTERVAL       5000         // time interval (msec)
#define LOOP_INTERVAL       500         // loop/playlist interval (msec)
#define NET_BUFF_SIZE1    20000         // network input buffer in bytes
#define NET_BUFF_SIZE2   200000
#define NET_TIMEOUT          10         // receive timeout (sec)

//  global variables
extern HWND                 main_window;
extern HINSTANCE            instance;

enum device_type  {HPC, PPC};
extern enum device_type     device;

enum display_type {BW4, BW4F, BW16, BW256, BW, C256, C4096, C65536, COLOR};

struct option_type{
    int                 video_enable;   // TRUE: video decoded
    int                 picture_level;  // decoded picture 1: I, 2: I&P
    int                 audio_enable;   // TRUE: audio decoded
    int                 stereo;         // TRUE: enable streo audio
    enum display_type   display;
    int                 mag2;           // TRUE: magnify picture x2
    int                 rotate;         // TRUE: rotate screen
    int                 hide_bar;       // TRUE: hide taskbar
    int                 hide_buttons;   // TRUE: hide buttons
    int                 audio_delay;    // 0-3
    int                 endless;        // TRUE: endless play
    int                 large_buffer;   // TRUE: large video input buffer
    int                 uninstall;      // TRUE: clear registry at exit
};

extern struct option_type   option;

//  video image buffer type
typedef unsigned char y_buffer[VERTICAL_SIZE  ][HORIZONTAL_SIZE  ];
typedef unsigned char c_buffer[VERTICAL_SIZE/2][HORIZONTAL_SIZE/2];

//  search (forward/rewind) control/state
enum search {NONE, FWD, REW};

//  global functions
//  audio.cpp
int             decode_audio(WORD *&, int &);
unsigned char  *get_audio_in(int);
void            reset_audio(int);
//  display.cpp
void            close_display();
void            draw_picture(y_buffer, c_buffer, c_buffer);
void            reset_display();
void            set_display(int, int);
void            set_palette();
//  file.cpp
void            close_file();
void            close_list();
void            file_pos(int);
TCHAR          *get_directory();
void            get_filename();
int             is_local();
int             last_slot();
int             list_next();
void            open_file();
int             packet(int, int);
void            rewind(int);
void            seek(int);
void            set_filename(LPTSTR);
//  net.cpp
void            close_net();
void            fill_buff(int);
int             getb_net();
int             open_net(LPCTSTR);
void            read_net(unsigned char *, unsigned);
void            skip_net(unsigned);
//  pcm.cpp
void            change_volume(int);
int             pcm_done(int);
void            pcm_out(WORD *, int, int, int, int);
void            pcm_pause();
void            pcm_restart();
void            reset_pcm();
//  play.cpp
int             fwd_rew();
void            jump();
void            keep_time();
int             play_busy();
void            quit_play();
int             search_busy();
void            start_play();
//  video.cpp
void            close_video();
int             decode_video(int);
unsigned char  *get_video_in(int);
void            output_video();
void            reset_video();
//  wodeon.cpp
void            error(int);
enum search     search_control();
void            show_message(int);
void            show_state();
