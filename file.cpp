/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    file.cpp       : file input
*/

#include <windows.h>
#include <commdlg.h>
#include <setjmp.h>
#include <string.h>
#include "resource.h"
#include "wodeon.h"

static TCHAR   *filter=TEXT("MPEG, playlist\0*.mpeg;*.mpg;*.pls\0")
                       TEXT("MPEG(*.mpeg, *.mpg)\0*.mpeg;*.mpg\0")
                       TEXT("playlist(*.pls)\0*.pls\0*.*\0*.*\0");
static TCHAR    directory[MAX_PATH]=TEXT("\\");
static TCHAR    listname [MAX_PATH]=TEXT("");
static TCHAR    filename [MAX_PATH]=TEXT("");
static HANDLE   list        =INVALID_HANDLE_VALUE;
static HANDLE   file        =INVALID_HANDLE_VALUE;
static int      packet_end  =TRUE;
static int      time_base;              // base of packet time stamp
static jmp_buf  file_level;             // treat sudden end of file

static int    (*getb)();                // holders of file access functions
static void   (*read)(unsigned char *, unsigned);
static void   (*skip)(unsigned);

//  time slots (4096msec interval) for rewind
const int       slot_size=3516;         // slot_size * 4096msec == 4hours
static LONG     slot[slot_size];        // byte position of slots in file
static int      slot_end;               // no. of available slots

//  clear time slots
static void clear_slots()
{
    int i;

    for(i=0; i<slot_size; i++) slot[i]=0;
    slot_end=0;
    time_base=0xfffffff;
}

//  get last slot time
int last_slot()                         // O last slot time (msec)
{
    return (slot_end-1)<<12;
}

//  local file access functions
//  get a byte from local file
static int getb_file()                  // O lower 8bit is effective
{
    unsigned char   b;
    DWORD           n;

    if(!ReadFile(file, &b, 1, &n, NULL) || n!=1) longjmp(file_level, 1);
    return b;
}

//  get specified bytes from local file
static void read_file(
    unsigned char  *buff,               // O read out buffer
    unsigned        num                 // I read num bytes
    )
{
    DWORD   n;

    if(!ReadFile(file, buff, num, &n, NULL) || num!=n) longjmp(file_level, 1);
}

//  skip local file over specified bytes
static void skip_file(
    unsigned    num                     // I skip num bytes
    )
{
    SetFilePointer(file, (LONG)num, NULL, FILE_CURRENT);
}

//  close playlist
void close_list()
{
    if(list!=INVALID_HANDLE_VALUE) CloseHandle(list);
    list=INVALID_HANDLE_VALUE;
}

//  check wheather local file or net stream
static LPCTSTR http_str=TEXT("http://");

int is_local()                          // O TRUE: local file
{
    return _tcsncmp(filename, http_str, 7);
}

// open URL dialog box
BOOL CALLBACK openURL_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
    HWND hec=GetDlgItem(hDlg, IDC_URL);

    switch(uMsg){
    case WM_INITDIALOG:
        SetWindowText(hec, is_local()? http_str: filename);
        SendMessage(hec, EM_SETSEL, 0, -1);
        SetFocus(hec);
        return TRUE;
    case WM_COMMAND:
        switch(wParam){
        case IDOK:
            GetWindowText(hec, filename, MAX_PATH);
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
    }
    return FALSE;
}

//  open file dialog box
void get_filename()
{
    OPENFILENAME    fs;
    int             i;

    quit_play();
    close_list();
    memset((void *)&fs, 0, sizeof(OPENFILENAME));
    fs.lStructSize      =sizeof(OPENFILENAME);
    fs.hwndOwner        =main_window;
    fs.lpstrFilter      =filter;
    fs.lpstrFile        =filename;
    fs.nMaxFile         =MAX_PATH;
    fs.lpstrInitialDir  =directory;
    fs.Flags            =OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if(GetOpenFileName(&fs)){
        for(i=0; directory[i]=filename[i]; i++);
        while(directory[--i]!=TEXT('\\'));
        directory[i]=NULL;
    }else
        DialogBox(instance, MAKEINTRESOURCE(IDD_OPENURL), main_window,
                  openURL_proc);
    set_filename(NULL);
}

//  return last directory string
TCHAR *get_directory()
{
    return directory;
}

//  set filename string to open and check if playlist
void set_filename(
    LPTSTR  name                        // I filename string (NULL: check only)
    )
{
    size_t s;

    if(name){
        if(MAX_PATH <= _tcslen(name)) return;
        _tcscpy(filename, name);
    }

    s=_tcslen(filename);
    if(4<=s && _tcscmp(filename+s-4, TEXT(".pls"))==0){
        _tcscpy(listname, filename);
        list_next();
    }else
        *listname=NULL;
    clear_slots();
}

//  open and initialize state
void open_file()
{
    TCHAR           buff[STRING_SIZE*2+MAX_PATH], text[STRING_SIZE];
    int             net;

    if(*filename==NULL) get_filename();
    close_file();
    net=-1;
    if(is_local())
            file=CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    else    net=open_net(filename);
    if(file==INVALID_HANDLE_VALUE && net){
        close_file();
        packet_end=TRUE;
        if(net==IDS_ERR_ABORT) return;
        LoadString(instance, IDS_ERR_OPEN, text, STRING_SIZE);
        wsprintf(buff, text, filename);
        if(0<net){
            LoadString(instance, net, text, STRING_SIZE);
            _tcscat(buff, TEXT("\n")); _tcscat(buff, text);
        }
        LoadString(instance, IDS_ERR_CAPTION, text, STRING_SIZE);
        MessageBox(main_window, buff, text, MB_OK);
        return;
    }

    packet_end=FALSE;
    if(net==0)  getb=getb_net,  read=read_net,  skip=skip_net;
    else        getb=getb_file, read=read_file, skip=skip_file;
}

void close_file()
{
    if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
    file=INVALID_HANDLE_VALUE;
    close_net();
}

//  read packet data (skip data if presentation time < time_bound)
enum packet_type {AUDIO, VIDEO, WASTE};

static int packet_data(                 // O presentation time (-1: skipped)
    enum packet_type    type,           // I
    int                 time_bound      // I (-1: not used)
    )
{
    unsigned        total, length;
    unsigned char   pts[4];
    int             time, c, i;
    LONG            dummy;

    total =getb()<<8;                   // do not put together these 2 lines
    total+=getb();                      // getb() order is critical
    length=total;
    if(type==WASTE){
        skip(length);
        return -1;
    }
    time= time_bound==-1? 0: -1;

    do{                                 // waste stuffing byte
        c=getb(); length--;
    }while(c==0xff);

    if((c&0xc0)==0x40){                 // waste STD buffer size
        skip(1); c=getb(); length-=2;
    }

    if((c&0xe0)==0x20){                 // presentation time stamp
        read(pts, 4); length-=4;
        time=((((((unsigned)c &0x0e)<<7 | pts[0])<<8 |(pts[1]&0xfe))<<7
               | pts[2])<<6 | pts[3]>>2)/45;
        if(time<time_base) time_base=time;
        time-=time_base;
        i=time>>12;
        if((type==VIDEO || !option.video_enable) && (time&4095)<1000 &&
           i<slot_size && slot[i]==0 && file!=INVALID_HANDLE_VALUE){
                                        // if net stream, slot not used
            dummy=0;
            slot[i]=SetFilePointer(file, 0, &dummy, FILE_CURRENT)
                    -(total-length+6);  // get packet header position
            if(slot_end<=i) slot_end=i+1;
        }
        if((c&0xf0)==0x30){
            skip(5); length-=5;
        }
    }
    if(time<time_bound){
        skip(length);
        return -1;
    }

    read((type==AUDIO? get_audio_in(length): get_video_in(length)), length);
    return time;
}

//  search video/audio packet
int packet(                             // O presentation time  (-1: not found)
    int     video,                      // I TRUE: video, FALSE: audio request
    int     time_bound                  // I <=presentation time (-1: not used)
    )
{
    int     t, a, b, c;

    if(packet_end) return -1;
    if(setjmp(file_level)){              // sudden end of file
        packet_end=TRUE;
        return -1;
    }
    for(;;){
        a=getb(); b=getb(); c=getb();   // find start code prefix 00 00 01
        while(a!=0x00 || b!=0x00 || c!=0x01){
            a=b; b=c; c=getb();
        }
        switch(getb()){
        case 0xc0:                      // audio stream 0
            if(option.audio_enable && !search_busy()){
                t=packet_data(AUDIO, time_bound);
                if(0<=t && !video) return t;
            }else packet_data(WASTE, -1);
            break;
        case 0xe0:                      // video stream 0
            if(option.video_enable){
                t=packet_data(VIDEO, (video? time_bound: -1));
                if(0<=t &&  video) return t;
            }else packet_data(WASTE, -1);
            break;
        case 0xba:                      // pack
            skip(8);
            break;
        case 0xb9:                      // iso_11172_end_code
            packet_end=TRUE;
            return -1;
        case 0xb3:                      // raw video stream
            error(IDS_ERR_ELEMENT);
            return -1;
        default:
            packet_data(WASTE, -1);
            break;
        }
    }
}

//  rewind file for backward search
void rewind(
    int     time                        // I objective time
    )
{
    int     i, j;
    LONG    dummy;

    i=j=time>>12;
    if(i<slot_end){
        while(0<=i && slot[i]==0) i--;
        if(j-i<8){
            dummy=0;
            SetFilePointer(file, (0<=i? slot[i]: 0), &dummy, FILE_BEGIN);
            return;
        }
    }
    seek(time);
}

//  save/restore current file position
void file_pos(
    int restore                         // I TRUE: restore, FALSE: save
    )
{
    static LONG position;
    LONG        dummy=0;

    if(restore)   SetFilePointer(file, position, &dummy, FILE_BEGIN);
    else position=SetFilePointer(file, 0, &dummy, FILE_CURRENT);
}

//  get next filename of playlist
int list_next()                         // O TRUE: next filename found
{
    TCHAR   buff[STRING_SIZE+MAX_PATH], text[STRING_SIZE];
    char    line[MAX_PATH];
    DWORD   n;
    int     i;

    if(*listname==NULL) return FALSE;
    *filename=NULL;
    if(list==INVALID_HANDLE_VALUE){
        list=CreateFile(listname, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if(list==INVALID_HANDLE_VALUE){
            LoadString(instance, IDS_ERR_OPEN,    text, STRING_SIZE);
            wsprintf(buff, text, listname);
            LoadString(instance, IDS_ERR_CAPTION, text, STRING_SIZE);
            MessageBox(main_window, buff, text, MB_OK);
            return FALSE;
        }
    }

    for(i=0; i<sizeof(line)-1; i++){
        if(!ReadFile(list, line+i, 1, &n, NULL) || n!=1) break;
        if(1<=i && line[i-1]=='\r' && line[i]=='\n'){
            i--;
            break;
        }
    }
    if(i==0){
        close_list();
        return FALSE;
    }
    line[i]=NULL;

    if(MultiByteToWideChar(CP_ACP, 0, line, -1, filename, sizeof(filename))<=0){
        *filename=NULL;
        return FALSE;
    }
    return TRUE;
}

//  seek
//  find presentation time stamp
static int find_pts(                    // O presentation time (msec)
    ULONG   size,                       // I file size
    LONG   &head                        // O position of packet
    )
{
    LONG            current, dummy;
    unsigned        length;
    unsigned char   pts[4];
    int             a, b, c, d;

    for(;;){
        a=getb(); b=getb(); c=getb(); d=getb();
        while(a!=0x00 || b!=0x00 || c!=0x01 || (d!=0xe0 && d!=0xc0)){
            a=b; b=c; c=d; d=getb();
        }                               // find video/audio packet
        dummy=0;
        current=SetFilePointer(file, 0, &dummy, FILE_CURRENT);
        length =getb()<<8;
        length+=getb();
        if(size-(ULONG)current < length+6){
            SetFilePointer(file, -2, NULL, FILE_CURRENT);
            continue;                   // not real packet
        }
        SetFilePointer(file, length, NULL, FILE_CURRENT);
        if(getb()!=0x00 || getb()!=0x00 || getb()!=0x01){
            dummy=0;
            SetFilePointer(file, current, &dummy, FILE_BEGIN);
            continue;                   // not real packet
        }
        if((d==0xe0 && !option.video_enable) ||
           (d==0xc0 &&  option.video_enable)){
            SetFilePointer(file, -3, NULL, FILE_CURRENT);
            continue;                   // not required packet type
        }
        dummy=0;
        SetFilePointer(file, current+2, &dummy, FILE_BEGIN);
        while((c=getb())==0xff);        // waste stuffing byte
        if((c&0xc0)==0x40){             // waste STD buffer size
            skip(1); c=getb();
        }
        if((c&0xe0)==0x20){             // presentation time stamp
            head=current-4;
            read(pts, 4);
            return ((((((unsigned)c &0x0e)<<7 | pts[0])<<8 |(pts[1]&0xfe))
                     <<7 | pts[2])<<6 | pts[3]>>2)/45 -time_base;
        }
        dummy=0;                        // move to next packet
        SetFilePointer(file, current+length+2, &dummy, FILE_BEGIN);
    }
}

void seek(
    int     time                        // I objective time
    )
{
    ULONG   size, pos, stride;
    LONG    head, dummy;
    int     t;

    size=GetFileSize(file, NULL);
    pos=head=0;
    if(setjmp(file_level)==0)
        for(stride=size>>1; stride; stride>>=1){
            dummy=0;
            SetFilePointer(file, pos, &dummy, FILE_BEGIN);
            t=find_pts(size, head);
            if(t< time-10000)        pos+=stride; else
            if(time<t && stride<pos) pos-=stride; else
            break;
        }
    dummy=0;
    SetFilePointer(file, head, &dummy, FILE_BEGIN);
}
