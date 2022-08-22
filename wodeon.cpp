/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    wodeon.cpp     : wodeon main
*/

#include <windows.h>
#include <setjmp.h>
#include <string.h>
#include "resource.h"
#include "wodeon.h"

struct option_type  option      ={TRUE, 2, FALSE, FALSE, BW4, FALSE, FALSE,
                                  FALSE, FALSE, 0, FALSE, FALSE, FALSE};
HWND                main_window =NULL;
HINSTANCE           instance    =NULL;
enum device_type    device      =HPC;

//  registry I/O
static LPCTSTR subkey=TEXT("Software\\wodeon");

static void reg_write()
{
    HKEY    hKey;
    DWORD   d;
    TCHAR  *p;

    if(RegCreateKeyEx(HKEY_CURRENT_USER, subkey, 0, TEXT(""), 0, 0, NULL,
                      &hKey, &d)) return;
    RegSetValueEx(hKey, TEXT("option"),    0, REG_BINARY,
                  (const BYTE *)&option, sizeof(option));
    p=get_directory();
    RegSetValueEx(hKey, TEXT("directory"), 0, REG_SZ,
                  (const BYTE *)p, (_tcslen(p)+1)*sizeof(TCHAR));
    RegCloseKey(hKey);
}

static void reg_read()
{
    HKEY    hKey;
    DWORD   size;

    if(RegOpenKeyEx(HKEY_CURRENT_USER, subkey, 0, 0, &hKey)) return;
    size=sizeof(option);
    RegQueryValueEx(hKey, TEXT("option"),    NULL, NULL,
                    (LPBYTE)&option, &size);
    size=MAX_PATH*sizeof(TCHAR);
    RegQueryValueEx(hKey, TEXT("directory"), NULL, NULL,
                    (LPBYTE)get_directory(), &size);
    RegCloseKey(hKey);
}

static void reg_erase()
{
    RegDeleteKey(HKEY_CURRENT_USER, subkey);
}

//  option dialog box
//  SendDlgItemMessage() cannot run on CE1.0 with CE2.0 library
#define set_check(id, state) SendMessage(GetDlgItem(hDlg, id), BM_SETCHECK, \
                                         state? BST_CHECKED: BST_UNCHECKED, 0)
#define get_check(id)        SendMessage(GetDlgItem(hDlg, id), BM_GETCHECK, \
                                         0, 0)==BST_CHECKED
#define set_sel(id, index)   SendMessage(GetDlgItem(hDlg, id), CB_SETCURSEL, \
                                         index, 0)
#define get_sel(id)          SendMessage(GetDlgItem(hDlg, id), CB_GETCURSEL, \
                                         0, 0)
#define add_list(id, strid)  LoadString(instance, strid, text, STRING_SIZE), \
                             SendMessage(GetDlgItem(hDlg, id), CB_ADDSTRING, \
                                         0, (long)text)

BOOL CALLBACK option_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
    TCHAR   text[STRING_SIZE];
    int     i;

    switch(uMsg){
    case WM_INITDIALOG:
        set_check(IDC_VIDEO,     option.video_enable);
        set_check(IDC_RATE,      option.picture_level==2);
        set_check(IDC_AUDIO,     option.audio_enable);
        set_check(IDC_STEREO,    option.stereo);
        set_check(IDC_MAG2,      option.mag2);
        set_check(IDC_ROTATE,    option.rotate);
        set_check(IDC_BAR,       option.hide_bar);
        set_check(IDC_BUTTONS,   option.hide_buttons);
        set_check(IDC_ENDLESS,   option.endless);
        set_check(IDC_BUFFER,    option.large_buffer);
        set_check(IDC_UNINSTALL, option.uninstall);
        for(i=IDS_CB_BW4; i<=IDS_CB_COLOR; i++) add_list(IDC_DISPLAY, i);
        set_sel(IDC_DISPLAY, option.display);
        for(i=IDS_CB_DLY0; i<=IDS_CB_DLY3; i++) add_list(IDC_DELAY, i);
        set_sel(IDC_DELAY, option.audio_delay);
        return TRUE;
    case WM_COMMAND:
        switch(wParam){
        case IDOK:
            option.video_enable =get_check(IDC_VIDEO);
            option.picture_level=get_check(IDC_RATE)? 2: 1;
            option.audio_enable =get_check(IDC_AUDIO);
            option.stereo       =get_check(IDC_STEREO);
            option.display      =(enum display_type)get_sel(IDC_DISPLAY);
            option.mag2         =get_check(IDC_MAG2);
            option.rotate       =get_check(IDC_ROTATE);
            option.hide_bar     =get_check(IDC_BAR);
            option.hide_buttons =get_check(IDC_BUTTONS);
            option.audio_delay  =get_sel(IDC_DELAY);
            option.endless      =get_check(IDC_ENDLESS);
            option.large_buffer =get_check(IDC_BUFFER);
            option.uninstall    =get_check(IDC_UNINSTALL);
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
    }
    return FALSE;
}

static void option_box()
{
    quit_play();
    DialogBox(instance, MAKEINTRESOURCE(IDD_OPTIONBOX), main_window,
              option_proc);
}

//  exit button
static void quit()
{
    DestroyWindow(main_window);
}

//  command buttons
struct button_params{
    int         bitmap;                 // resource bitmap ID
    int         x, y, w, h;             // position, size
    void      (*func)();                // function called when clicked
    HWND        hwnd;
};

static void dummy(){}

struct button_params button_hpc[]={
    {IDB_OPEN,   0,   0, 48, 25, get_filename},
    {IDB_FWD,    0,  25, 48, 25, dummy},
    {IDB_PLAY,   0,  50, 48, 25, start_play},
    {IDB_REW,    0,  75, 48, 25, dummy},
    {IDB_JUMP,   0, 100, 48, 25, jump},
    {IDB_OPTION, 0, 125, 48, 25, option_box},
    {IDB_QUIT,   0, 150, 48, 25, quit},
    {0, 0, 0, 0, 0, NULL}
};

struct button_params button_ppc[]={
    {IDB_OPEN,     0, 0, 34, 20, get_filename},
    {IDB_REW,     34, 0, 34, 20, dummy},
    {IDB_PLAY,    68, 0, 34, 20, start_play},
    {IDB_FWD,    102, 0, 34, 20, dummy},
    {IDB_JUMP,   136, 0, 34, 20, jump},
    {IDB_OPTION, 170, 0, 34, 20, option_box},
    {IDB_QUIT,   204, 0, 34, 20, quit},
    {0, 0, 0, 0, 0, NULL}
};

static HWND     play_button=NULL;
static HWND     fwd_button =NULL;
static HWND     rew_button =NULL;
static UINT     button_num =0;

//  show / hide command buttons
static void show_buttons(
    int nCmdShow                        // I SW_SHOW or SW_HIDE
    )
{
    struct button_params *p= device==HPC? button_hpc: button_ppc;
    while(p->bitmap) ShowWindow(p++->hwnd, nCmdShow);
    UpdateWindow(main_window);
}

//  draw command bitmap button
static void draw_button(
    LPDRAWITEMSTRUCT    dis             // I
    )
{
    struct button_params *p;
    HBITMAP     hb;
    BITMAP      bmp;
    HDC         memDC;
    HGDIOBJ     old;
    int         i;

    p=(device==HPC? button_hpc: button_ppc)+ dis->CtlID -1;
    i=(dis->itemState & ODS_SELECTED)? GRAY_BRUSH: BLACK_BRUSH;
    old=SelectObject(dis->hDC, GetStockObject(i));
    PatBlt(dis->hDC, 0, 0, p->w, p->h, PATCOPY);
    SelectObject(dis->hDC, old);

    i=(p->bitmap==IDB_PLAY && play_busy())? IDB_PAUSE: p->bitmap;
    hb=LoadBitmap(instance, MAKEINTRESOURCE(i));
    GetObject(hb, sizeof(bmp), &bmp);
    memDC=CreateCompatibleDC(dis->hDC);
    old=SelectObject(memDC, hb);
    SetTextColor(dis->hDC, RGB(  0,   0,   0));
    SetBkColor  (dis->hDC, RGB(192, 192, 192));
    BitBlt(dis->hDC, (p->w -bmp.bmWidth)>>1, (p->h -bmp.bmHeight)>>1,
           bmp.bmWidth, bmp.bmHeight, memDC, 0, 0, SRCPAINT);
    SelectObject(memDC, old);
    DeleteDC(memDC);
    DeleteObject(hb);
}

//  main window procedure
static int      fwd_press=FALSE;        // forward/rewind key is down
static int      rew_press=FALSE;
static jmp_buf  toplevel;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    struct button_params *p;
    UINT    id;
    HWND    ht;

    switch(uMsg){
    case WM_COMMAND:
        id=LOWORD(wParam);
        if(HIWORD(wParam)==BN_CLICKED && 1<=id && id<=button_num){
            if(device==HPC) (button_hpc[id-1].func)();
            else            (button_ppc[id-1].func)();
            SetFocus(main_window);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        switch(wParam){
        case VK_ESCAPE: if(IsWindowVisible(play_button)) break;
                        show_buttons(SW_SHOW);  return 0;
        case 'O':       get_filename();         return 0;
        case VK_RETURN:
        case VK_SPACE:  start_play();           return 0;
        case VK_RIGHT:
        case VK_UP:     fwd_press=TRUE;         return 0;
        case VK_LEFT:
        case VK_DOWN:   rew_press=TRUE;         return 0;
        case 'J':       jump();                 return 0;
        case 'S':       option_box();           return 0;
        case 'X':       quit();                 return 0;
        }
        break;
    case WM_KEYUP:
        switch(wParam){
        case VK_RIGHT:
        case VK_UP:     fwd_press=FALSE; return 0;
        case VK_LEFT:
        case VK_DOWN:   rew_press=FALSE; return 0;
        }
        break;
    case WM_CHAR:
        switch(wParam){
        case TEXT(','): change_volume(FALSE); return 0;
        case TEXT('.'): change_volume(TRUE);  return 0;
        }
        break;
    case WM_DRAWITEM:
        id=((LPDRAWITEMSTRUCT)lParam)->CtlID;
        if(1<=id && id<=button_num){
            draw_button((LPDRAWITEMSTRUCT)lParam);
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if(IsWindowVisible(play_button)) break;
        show_buttons(SW_SHOW);
        return 0;
    case WM_ACTIVATE:
        ht=FindWindow(TEXT("HHTaskBar"), NULL);
        if(LOWORD(wParam)==WA_INACTIVE){
            if(ht && option.hide_bar) ShowWindow(ht, SW_SHOW);
        }else{
            if(ht && option.hide_bar) ShowWindow(ht, SW_HIDE);
            set_palette();
            InvalidateRect(hWnd, NULL, TRUE);
            UpdateWindow(hWnd);
        }
        break;
    case WM_CREATE:
        SendMessage(hWnd, WM_SETICON, FALSE, (LPARAM)LoadImage(instance,
                    MAKEINTRESOURCE(IDI_WODEON), IMAGE_ICON, 16, 16, 0));
        p= device==HPC? button_hpc: button_ppc;
        for(button_num=0; p->bitmap; p++, button_num++){
            p->hwnd=CreateWindow(TEXT("BUTTON"), TEXT(""),
                      WS_CHILD|WS_VISIBLE|BS_OWNERDRAW, p->x, p->y, p->w, p->h,
                      hWnd, (HMENU)(button_num+1), instance, NULL);
            switch(p->bitmap){
            case IDB_PLAY:  play_button=p->hwnd; break;
            case IDB_FWD:   fwd_button =p->hwnd; break;
            case IDB_REW:   rew_button =p->hwnd; break;
            }
        }
        reg_read();
        return 0;
    case WM_DESTROY:
        quit_play();
        close_display(); close_video();
        close_list();
        if(option.uninstall)    reg_erase();
        else                    reg_write();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static const LPCTSTR CLASS_NAME=TEXT("wodeon");

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpCmdLine, int nShow)
{
    WNDCLASS    wc;
    TCHAR       title[STRING_SIZE];
    int         w, h;
    MSG         msg;
    DWORD       time, last_time;

    main_window=FindWindow(CLASS_NAME, NULL);       // check double run
    if(main_window) SendMessage(main_window, WM_CLOSE, 0, 0);

    instance=hInstance;
    device= 480<=GetSystemMetrics(SM_CXSCREEN)? HPC: PPC;

    wc.style            =CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc      =(WNDPROC)WndProc;
    wc.cbClsExtra       =0;
    wc.cbWndExtra       =0;
    wc.hInstance        =hInstance;
    wc.hIcon            =NULL;
    wc.hCursor          =NULL;
    wc.hbrBackground    =(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName     =NULL;
    wc.lpszClassName    =CLASS_NAME;
    if(!RegisterClass(&wc)) return FALSE;

    LoadString(hInstance, IDS_APP_TITLE, title, STRING_SIZE);
    w=GetSystemMetrics(SM_CXSCREEN);
    h=GetSystemMetrics(SM_CYSCREEN);
    main_window=CreateWindow(CLASS_NAME, title, WS_VISIBLE, 0, 0, w, h,
                             NULL, NULL, hInstance, NULL);
    if(!main_window) return FALSE;
    ShowWindow(main_window, nShow);
    UpdateWindow(main_window);

    set_filename(lpCmdLine);
    setjmp(toplevel);                               // error handling top
    last_time=GetTickCount();
    for(;;){
        show_state();
        if(is_local() && fwd_rew()){
            if(!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) continue;
        }else
        if(play_busy()){
            keep_time();
            time=GetTickCount();
            if((time-last_time)>>13){               // about every 8sec
                keybd_event(VK_F24, 0, KEYEVENTF_KEYUP|KEYEVENTF_SILENT, 0);
                // reset auto-suspending timer (this works even on CE1.0)
                last_time=time;
            }
            if(!play_busy()){
                if(list_next() || (list_next(), option.endless)){
                    Sleep(LOOP_INTERVAL);
                    InvalidateRect(main_window, NULL, TRUE);
                    UpdateWindow(main_window);
                    start_play();
                }else
                    close_file();
            }
            if(!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) continue;
        }else
        if(GetMessage(&msg, NULL, 0, 0)!=TRUE) return msg.wParam;
        TranslateMessage(&msg);
        DispatchMessage (&msg);
    }
}

//  error handling
void error(
    int errno                           // I resource ID of error string
    )                                   //   (0: no message)
{
    TCHAR   caption[STRING_SIZE];
    TCHAR   text   [STRING_SIZE];

    quit_play();
    if(errno){
        LoadString(instance, IDS_ERR_CAPTION, caption, STRING_SIZE);
        LoadString(instance, errno,           text,    STRING_SIZE);
        MessageBox(main_window, text, caption, MB_OK);
    }
    longjmp(toplevel, 1);
}

//  show play state on play button
void show_state()
{
    static int state=FALSE;

    if(play_busy()==state || !IsWindow(play_button)) return;
    state=!state;
    if(IsWindowVisible(play_button))
        if(state && option.hide_buttons) show_buttons(SW_HIDE);
        else{
            InvalidateRect(play_button, NULL, FALSE);
            UpdateWindow(play_button);
        }
}

//  show message on main window
void show_message(
    int     message                     // I resource ID of message string
    )                                   //   (0: erase)
{
    TCHAR   text[STRING_SIZE];
    RECT    rect;
    HDC     hDC;

    InvalidateRect(main_window, NULL, TRUE);
    UpdateWindow(main_window);
    if(message){
        LoadString(instance, message, text, STRING_SIZE);
        GetWindowRect(main_window, &rect);
        hDC=GetDC(main_window);
        SetTextColor(hDC, RGB(255, 255, 255));
        SetBkColor  (hDC, RGB(  0,   0,   0));
        DrawText(hDC, text, -1, &rect, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        ReleaseDC(main_window, hDC);
    }
}

//  check if forward/rewind button/key is pushed
enum search search_control()            // O
{
    // GetAsyncKeyState() is for fast button-release response
    if(fwd_press && (HIBYTE(GetAsyncKeyState(VK_RIGHT)) ||
                     HIBYTE(GetAsyncKeyState(VK_UP))))   return FWD;
    if(rew_press && (HIBYTE(GetAsyncKeyState(VK_LEFT))  ||
                     HIBYTE(GetAsyncKeyState(VK_DOWN)))) return REW;
    if((SendMessage(fwd_button, BM_GETSTATE, 0, 0) & BST_PUSHED) &&
       HIBYTE(GetAsyncKeyState(VK_LBUTTON))) return FWD;
    if((SendMessage(rew_button, BM_GETSTATE, 0, 0) & BST_PUSHED) &&
       HIBYTE(GetAsyncKeyState(VK_LBUTTON))) return REW;
    return NONE;
}
