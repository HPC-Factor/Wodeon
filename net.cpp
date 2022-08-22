/*
    project        : wodeon, MPEG-1 video player for WindowsCE
    author         : omiokone
    net.cpp        : net stream input
*/

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <winsock.h>
#include "resource.h"
#include "wodeon.h"

//  net input buffer
static char    *net_in  =NULL;          // buffer head
static int      nib_size=0;             // buffer size
static int      inx, outx;              // I/O index (0<= <buffsize)

//  open net stream
static int      winsock =FALSE;         // TRUE: winsock loaded
static SOCKET   sock    =INVALID_SOCKET;

int open_net(                           // O error string ID (0: success)
    LPCTSTR     url                     // I URL begins with "http://"
    )
{
    static char             urlmb[MAX_PATH*2+15], t[MAX_PATH*2+25];
    static struct timeval   time={0, 0};
    int                     port, i;
    unsigned long           addr, arg;
    struct hostent         *hp;
    struct sockaddr_in      host;
    DWORD                   last_tick;
    fd_set                  fds;
    char                   *p;
    MSG                     msg;
    static WSADATA          dummy;

    nib_size= option.large_buffer? NET_BUFF_SIZE2: NET_BUFF_SIZE1;
    net_in=(char *)malloc(nib_size);
    if(!net_in) error(IDS_ERR_MEMORY);
    inx=outx=0;

    show_message(IDS_CONNECT);
    if(!winsock && WSAStartup(MAKEWORD(1,1), &dummy)) return IDS_ERR_SOCKET;
    winsock=TRUE;
    if(WideCharToMultiByte(CP_ACP, 0, url, -1, urlmb, MAX_PATH*2,
                           NULL, NULL)<=0)            return IDS_ERR_HOST;
    port=80;
    for(p=urlmb+7; *p!='/'; p++){
        if(*p==NULL){
            *(p+1)=NULL;
            break;
        }
        if(*p==':'){
            port=atoi(p+1);                     // CE doesn't have strtol() !
            *p=NULL;
        }
    }
    *p=NULL;
    strcpy(t, "Host: ");                        // t[] holds headers
    addr=inet_addr(urlmb+7);
    if(addr==INADDR_NONE){
        hp=gethostbyname(urlmb+7);
        if(!hp) return IDS_ERR_HOST;
        addr=*(unsigned long *)(hp->h_addr_list[0]);
        strcat(t, urlmb+7);
    }   // if raw IP address, Host header is empty
    host.sin_family     =AF_INET;
    host.sin_port       =htons(port);
    host.sin_addr.s_addr=addr;

    sock=socket(PF_INET, SOCK_STREAM, 0);
    if(sock==INVALID_SOCKET) return IDS_ERR_SOCKET;
    arg=1; ioctlsocket(sock, FIONBIO, &arg);    // set nonblocking
    if(connect(sock, (struct sockaddr *)&host, sizeof(host)) &&
       WSAGetLastError()!=WSAEWOULDBLOCK) return IDS_ERR_CONNECT;
    for(;;){                                    // wait connect completion
        FD_ZERO(&fds); FD_SET(sock, &fds);
        if(0<select(FD_SETSIZE, NULL, &fds, NULL, &time)) break;
        FD_ZERO(&fds); FD_SET(sock, &fds);
        if(0<select(FD_SETSIZE, NULL, NULL, &fds, &time))
            return IDS_ERR_CONNECT;
        if(PeekMessage(&msg, main_window, WM_KEYDOWN, WM_KEYDOWN,
                       PM_REMOVE)){
            show_message(0);
            return IDS_ERR_ABORT;
        }
    }

    p-=4; strncpy(p, "GET /", 5); strcat(p, " HTTP/1.0\r\n");
    if(send(sock, p, strlen(p), 0)==SOCKET_ERROR) return IDS_ERR_CONNECT;
    strcat(t, "\r\nConnection: close\r\n\r\n");
    if(send(sock, t, strlen(t), 0)==SOCKET_ERROR) return IDS_ERR_CONNECT;

    while(getb_net()!=' ');                     // check status response
    for(p=t, i=0; (*p=getb_net())!=' ' && i<5; p++, i++);
    *p=NULL;
    i=atoi(t);
    if(i<200 || 299<i) return IDS_ERR_STATUS;
    while(getb_net()!='\n');
    do                                          // waste header
        for(i=0; getb_net()!='\n'; i++);
    while(1<i);

    last_tick=GetTickCount();
    while(GetTickCount()-last_tick <1000) fill_buff(0);
    show_message(0);
    return 0;
}

void close_net()
{
    free(net_in);
    net_in=NULL;
    if(sock!=INVALID_SOCKET) closesocket(sock);
    sock=INVALID_SOCKET;
    if(winsock) WSACleanup();
    winsock=FALSE;
}

//  read bytes into input buffer
static int receive(                     // O size read in
    int num                             // I size requested
    )
{
    int nr, n;

    n=nib_size-inx;
    if(num<=n) nr=recv(sock, net_in+inx, num, 0);
    else{
        nr=recv(sock, net_in+inx, n, 0);
        if(n<=nr) nr+=recv(sock, net_in, num-nr, 0);
    }
    inx+=nr; if(nib_size<=inx) inx-=nib_size;
    return nr;
}

//  fill input buffer
void fill_buff(
    int num                             // I size requested
    )                                   //   (0: as much as possible)
{
    static struct timeval time={NET_TIMEOUT, 0};
    fd_set  rfds;
    u_long  ns;
    int     n, i;

    if(sock==INVALID_SOCKET) return;
    if(0<num){
        for(i=0; i<100; i++){
            n=inx-outx;    if(n<0) n+=nib_size;
            n-=num;        if(0<=n) return;
            FD_ZERO(&rfds); FD_SET(sock, &rfds);
            if(select(FD_SETSIZE, &rfds, NULL, NULL, &time)<=0) break;
            n=receive(-n); if(n==0) break;
        }
        error(IDS_ERR_BROKEN);
    }else{
        n=outx-inx; if(n<=0) n+=nib_size;
        n--;        if(n<=0) return;
        if(ioctlsocket(sock, FIONREAD, &ns)) error(IDS_ERR_BROKEN);
        if(ns==0) return;
        receive(n);
    }
}

//  get a byte from net stream
int getb_net()                          // O lower 8bits are effective
{
    unsigned char   b;

    fill_buff(1);
    b=net_in[outx++];
    if(nib_size<=outx) outx=0;
    return b;
}

//  get specified bytes from net stream
void read_net(
    unsigned char  *buff,               // O read out buffer
    unsigned        num                 // I read num bytes
    )
{
    unsigned n;

    fill_buff(num);
    n=nib_size-outx;
    if(num<=n){
        memcpy(buff, net_in+outx, num);
        outx+=num; if(nib_size<=outx) outx=0;
    }else{
        memcpy(buff, net_in+outx, n);
        num-=n;
        memcpy(buff+n, net_in, num);
        outx=num;
    }
}

//  skip net stream over specified bytes
void skip_net(
    unsigned    num                     // I skip num bytes
    )
{
    fill_buff(num);
    outx+=num;
    if(nib_size<=outx) outx-=nib_size;
}
