#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>
#include "typing.h"
_Static_assert(sizeof(wchar_t)==sizeof(uint16_t),"Windows UTF-16 required");
enum { TEXT=101,PASTE,CLEAR,START,STOP,HOLD,GAP,MODE,ENTERS,STATUS,PROGRESS,COUNT };
static HWND app,textbox,statusbox,bar;
static HFONT font,titlefont,codefont;
static HBRUSH background,fieldbrush;
static Plan plan;
static Engine engine;
static int pending,hot8,hot9,dpi=96,keyboard_mode,caps;
static unsigned hold_ms,gap_ms;
static ULONGLONG due;
static HWND target;
static DWORD pid,last_error;
static int scale(int v){return MulDiv(v,dpi,96);}
static HWND control(int id){return GetDlgItem(app,id);}
static void status(const wchar_t *s){SetWindowTextW(statusbox,s);}
static HWND add(const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id,HFONT f){
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,scale(x),scale(y),scale(w),scale(h),app,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(c,WM_SETFONT,(WPARAM)(f?f:font),TRUE);return c;
}
static void label(const wchar_t *s,int x,int y,int w,int h){add(L"STATIC",s,0,x,y,w,h,0,NULL);}
static void button(const wchar_t *s,int x,int y,int w,int id){add(L"BUTTON",s,BS_OWNERDRAW|WS_TABSTOP,x,y,w,38,id,NULL);}
static void busy(int b){
    const int ids[]={TEXT,PASTE,CLEAR,START,HOLD,GAP,MODE,ENTERS};
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)EnableWindow(control(ids[i]),!b);
    EnableWindow(control(STOP),b);
}
static int focused(void){
    DWORD now=0;HWND w=GetForegroundWindow();
    if(!target||w!=target||!IsWindow(w))return 0;
    GetWindowThreadProcessId(w,&now);return now==pid;
}
static void key_input(INPUT *i,WORD value,int unicode,int down){
    ZeroMemory(i,sizeof(*i));i->type=INPUT_KEYBOARD;i->ki.wScan=value;
    i->ki.dwFlags=(unicode?KEYEVENTF_UNICODE:KEYEVENTF_SCANCODE)|(down?0:KEYEVENTF_KEYUP);
    i->ki.dwExtraInfo=(ULONG_PTR)0x41555459;
}
static int emit(void *ctx,Unit u,int down){
    (void)ctx;
    if(down&&!focused()){last_error=ERROR_CANCELLED;return 0;}
    INPUT inputs[4];UINT n=0;
    if(down&&u.shift)key_input(&inputs[n++],0x2a,0,1);
    key_input(&inputs[n++],u.value,u.unicode,down);
    if(u.second)key_input(&inputs[n++],u.second,u.unicode,down);
    if(!down&&u.shift)key_input(&inputs[n++],0x2a,0,0);
    SetLastError(0);
    if(SendInput(n,inputs,sizeof(INPUT))==n)return 1;
    last_error=GetLastError();
    /* A partial batch may have pressed Shift: always attempt cleanup. */
    n=0;key_input(&inputs[n++],u.value,u.unicode,0);
    if(u.second)key_input(&inputs[n++],u.second,u.unicode,0);
    if(u.shift)key_input(&inputs[n++],0x2a,0,0);
    SendInput(n,inputs,sizeof(INPUT));return 0;
}
static int released(void){
    const int keys[]={VK_F8,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(GetAsyncKeyState(keys[i])&0x8000)return 0;
    int own_shift=engine.state==RUNNING&&engine.held&&engine.plan->units[engine.index].shift;
    return own_shift||!(GetAsyncKeyState(VK_SHIFT)&0x8000);
}
static void finish(int state){
    KillTimer(app,1);pending=0;busy(0);
    wchar_t s[240];
    if(state==DONE)status(L"입력을 마쳤어요. 대상 프로그램에서 결과를 확인하세요.");
    else if(state==FOCUS_LOST||(state==SEND_FAILED&&last_error==ERROR_CANCELLED))status(L"창이 바뀌어 입력을 중단했어요.");
    else if(state==KEYS_HELD)status(L"다른 단축키가 눌려 있어 중단했어요. 키를 놓고 다시 시작하세요.");
    else if(state==SEND_FAILED){_snwprintf(s,240,L"키 입력 실패 (오류 %lu). 대상 프로그램과 실행 권한이 같은지 확인하세요.",last_error);status(s);}
    else status(L"입력을 중단했어요. 대상 창의 현재 입력 내용을 확인하세요.");
}
static unsigned number(int id,unsigned lo,unsigned hi){
    wchar_t t[20],*end;GetWindowTextW(control(id),t,20);long v=wcstol(t,&end,10);
    return t[0]&&!*end&&v>=(long)lo&&v<=(long)hi?(unsigned)v:0;
}
static wchar_t *get_text(size_t *length){
    int n=GetWindowTextLengthW(textbox);
    if(n<=0){*length=0;return NULL;}
    *length=(size_t)n;
    wchar_t *s=malloc(((size_t)n+1)*sizeof(wchar_t));
    if(!s)return NULL;
    int got=GetWindowTextW(textbox,s,n+1);*length=got>0?(size_t)got:0;return s;
}
static void start_now(void){
    target=GetForegroundWindow();
    if(!target||target==app||!IsWindow(target)){
        finish(CANCELLED);status(L"내용을 입력할 다른 창을 클릭한 뒤 F8을 누르세요.");return;
    }
    GetWindowThreadProcessId(target,&pid);
    if(pid==GetCurrentProcessId()){finish(CANCELLED);status(L"입력할 대상 창으로 전환한 뒤 F8을 누르세요.");return;}
    caps=!!(GetKeyState(VK_CAPITAL)&1);
    size_t length=0,bad=0;wchar_t *text=get_text(&length);
    if(!text){finish(CANCELLED);status(length?L"내용을 읽을 메모리가 부족해요.":L"먼저 자동으로 입력할 내용을 붙여넣으세요.");return;}
    int enters=(int)SendMessageW(control(ENTERS),CB_GETCURSEL,0,0);
    int result=build_plan(&plan,(uint16_t*)text,length,keyboard_mode,caps,enters,&bad);free(text);
    if(result!=BUILD_OK){
        finish(CANCELLED);
        if(result==BUILD_UNSUPPORTED){wchar_t s[180];_snwprintf(s,180,L"%llu번째 문자는 키보드 방식으로 입력할 수 없어요. '일반 텍스트' 방식을 선택하세요.",(unsigned long long)bad+1);status(s);}
        else status(L"입력 내용이 비어 있거나 메모리가 부족해요.");
        return;
    }
    last_error=0;
    if(!begin_typing(&engine,&plan,hold_ms,gap_ms,GetTickCount64())){finish(CANCELLED);return;}
    pending=0;busy(1);SendMessageW(bar,PBM_SETPOS,0,0);
    status(L"자동 입력 중 · F9 중단 / 다른 창으로 전환해도 중단됩니다.");SetTimer(app,1,15,NULL);
}
static void request_start(int wait){
    if(pending||engine.state==RUNNING)return;
    if(!GetWindowTextLengthW(textbox)){status(L"먼저 자동으로 입력할 내용을 붙여넣으세요.");return;}
    hold_ms=number(HOLD,5,500);gap_ms=number(GAP,5,1000);
    if(!hold_ms||!gap_ms){status(L"누름 시간은 5~500ms, 키 사이 대기는 5~1000ms로 설정하세요.");return;}
    keyboard_mode=SendMessageW(control(MODE),CB_GETCURSEL,0,0)==1;
    if(wait){pending=1;due=GetTickCount64()+3000;busy(1);status(L"3초 안에 입력할 창을 클릭하세요.");SetTimer(app,1,15,NULL);}
    else start_now();
}
static void tick(void){
    if(GetAsyncKeyState(VK_F9)&0x8000){stop_typing(&engine,CANCELLED,emit,NULL);finish(CANCELLED);return;}
    ULONGLONG now=GetTickCount64();
    if(pending){if(now>=due)start_now();return;}
    int clear=released();
    if(keyboard_mode&&!!(GetKeyState(VK_CAPITAL)&1)!=caps)clear=0;
    int state=tick_typing(&engine,now,focused(),clear,0,emit,NULL);
    SendMessageW(bar,PBM_SETPOS,(WPARAM)(1000.0*(double)engine.index/(double)plan.count),0);
    if(state!=RUNNING)finish(state);
}
static void paste(void){
    if(!OpenClipboard(app)){status(L"클립보드를 열 수 없어요.");return;}
    HANDLE h=GetClipboardData(CF_UNICODETEXT);const wchar_t *s=h?GlobalLock(h):NULL;
    if(s){SIZE_T n=GlobalSize(h)/sizeof(wchar_t);if(n&&wcsnlen(s,n)<n)SetWindowTextW(textbox,s);GlobalUnlock(h);}
    CloseClipboard();
}
static void create_ui(void){
    add(L"STATIC",L"AUTO TYPER",0,26,20,760,45,0,titlefont);
    label(L"붙여넣은 내용을 원하는 입력창에 한 글자씩 입력합니다.  F8 시작 / F9 중단",26,74,790,30);
    button(L"붙여넣기",26,114,140,PASTE);button(L"내용 지우기",178,114,130,CLEAR);
    add(L"STATIC",L"0자",SS_RIGHT,450,121,360,26,COUNT,NULL);
    textbox=add(L"EDIT",L"",WS_BORDER|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_WANTRETURN,26,164,784,232,TEXT,codefont);
    SendMessageW(textbox,EM_SETLIMITTEXT,0x7ffffffe,0);
    label(L"키 누름 (ms)",26,419,130,26);add(L"EDIT",L"80",WS_BORDER|ES_NUMBER|WS_TABSTOP,160,412,90,34,HOLD,NULL);
    label(L"키 사이 대기 (ms)",290,419,170,26);add(L"EDIT",L"120",WS_BORDER|ES_NUMBER|WS_TABSTOP,465,412,90,34,GAP,NULL);
    label(L"입력 방식",26,464,350,25);label(L"끝에 Enter 추가",410,464,300,25);
    HWND mode=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,26,493,360,150,MODE,NULL);
    SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"일반 텍스트 (한글·기호 포함)");SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"키보드 입력 (게임·영문 기호)");SendMessageW(mode,CB_SETCURSEL,0,0);
    HWND enter=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,410,493,180,150,ENTERS,NULL);
    SendMessageW(enter,CB_ADDSTRING,0,(LPARAM)L"누르지 않음");SendMessageW(enter,CB_ADDSTRING,0,(LPARAM)L"1번");SendMessageW(enter,CB_ADDSTRING,0,(LPARAM)L"2번");SendMessageW(enter,CB_SETCURSEL,0,0);
    button(L"3초 후 입력",26,549,240,START);button(L"중단 (F9)",282,549,160,STOP);EnableWindow(control(STOP),FALSE);
    statusbox=add(L"STATIC",L"내용을 붙여넣고, 입력할 창을 클릭한 다음 F8을 누르세요.",0,26,606,784,45,STATUS,NULL);
    bar=add(PROGRESS_CLASSW,L"",0,26,660,784,10,PROGRESS,NULL);SendMessageW(bar,PBM_SETRANGE32,0,1000);
    label(L"줄바꿈·탭은 Enter·Tab 키로 입력됩니다. 마지막 Enter는 기본으로 추가하지 않습니다.",26,684,784,26);
}
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        app=w;create_ui();hot8=RegisterHotKey(w,1,MOD_NOREPEAT,VK_F8);hot9=RegisterHotKey(w,2,MOD_NOREPEAT,VK_F9);
        if(!hot8)status(L"F8을 다른 프로그램이 사용 중입니다. '3초 후 입력' 버튼을 사용하세요.");
        else if(!hot9)status(L"F9을 다른 프로그램이 사용 중입니다. 창을 전환해 중단할 수 있습니다.");
        return 0;
    case WM_HOTKEY:
        if(wp==1)request_start(0);
        else if(wp==2&&(pending||engine.state==RUNNING)){stop_typing(&engine,CANCELLED,emit,NULL);finish(CANCELLED);}
        return 0;
    case WM_TIMER:if(wp==1)tick();return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==TEXT&&HIWORD(wp)==EN_CHANGE){wchar_t t[100];_snwprintf(t,100,L"%d자",GetWindowTextLengthW(textbox));SetWindowTextW(control(COUNT),t);}
        if(HIWORD(wp)==BN_CLICKED)switch(LOWORD(wp)){
        case PASTE:paste();break;
        case CLEAR:SetWindowTextW(textbox,L"");break;
        case START:request_start(1);break;
        case STOP:stop_typing(&engine,CANCELLED,emit,NULL);finish(CANCELLED);break;
        }return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp,RGB(224,240,244));SetBkColor((HDC)wp,RGB(8,20,25));return (LRESULT)background;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp,RGB(224,240,244));SetBkColor((HDC)wp,RGB(16,37,44));return (LRESULT)fieldbrush;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;HBRUSH b=CreateSolidBrush((d->itemState&ODS_DISABLED)?RGB(25,40,45):RGB(13,71,81));FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);
        SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,(d->itemState&ODS_DISABLED)?RGB(110,130,135):RGB(235,248,250));SelectObject(d->hDC,font);
        wchar_t t[100];GetWindowTextW(d->hwndItem,t,100);DrawTextW(d->hDC,t,-1,&d->rcItem,DT_SINGLELINE|DT_VCENTER|DT_CENTER);
        if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);
        return TRUE;
    }
    case WM_CLOSE:stop_typing(&engine,CANCELLED,emit,NULL);KillTimer(w,1);DestroyWindow(w);return 0;
    case WM_DESTROY:if(hot8)UnregisterHotKey(w,1);if(hot9)UnregisterHotKey(w,2);PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR cmd,int show){
    (void)previous;(void)cmd;HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\SCCodeTyper_1_0");
    if(mutex&&GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(NULL,L"기존 입력기를 종료한 뒤 실행하세요.",L"자동 타이핑",MB_OK);CloseHandle(mutex);return 0;}
    SetProcessDPIAware();HDC dc=GetDC(NULL);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);
    font=CreateFontW(-scale(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Malgun Gothic");
    titlefont=CreateFontW(-scale(32),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    codefont=CreateFontW(-scale(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
    background=CreateSolidBrush(RGB(8,20,25));fieldbrush=CreateSolidBrush(RGB(16,37,44));
    INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_PROGRESS_CLASS};InitCommonControlsEx(&cc);
    WNDCLASSW cls={0};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"AutoTyperWindow";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hbrBackground=background;cls.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    if(!RegisterClassW(&cls))return 1;
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT r={0,0,scale(836),scale(722)};AdjustWindowRect(&r,style,FALSE);
    HWND w=CreateWindowW(cls.lpszClassName,L"자동 타이핑 1.1",style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,NULL,NULL,instance,NULL);
    if(!w)return 1;
    ShowWindow(w,show);UpdateWindow(w);MSG m;int result;
    while((result=GetMessageW(&m,NULL,0,0))>0)if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}
    free_plan(&plan);DeleteObject(font);DeleteObject(titlefont);DeleteObject(codefont);DeleteObject(background);DeleteObject(fieldbrush);if(mutex)CloseHandle(mutex);
    return result<0?1:(int)m.wParam;
}
