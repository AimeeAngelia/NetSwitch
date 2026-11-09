// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 AimeeAngelia

#include "core.h"
#include <gdiplus.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <map>
#include <cmath>

using namespace Gdiplus;
namespace ns {
struct Palette{Color bg,card,text,muted,line,accent,soft;bool dark=false;};
static Palette theme;
static HWND mainWindow=nullptr,settingsWindow=nullptr;
static HINSTANCE appInstance=nullptr;
static std::vector<Adapter> adapters;
static bool physical=true,reading=false,readAgain=false,switching=false,installing=false,ready=false,startHidden=false;
static int scrollY=0;
static UINT showMessage=0,exitMessage=0;
static std::wstring statusText,detailText;
static bool statusError=false;
static HANDLE addressNotify=nullptr,interfaceNotify=nullptr;
static std::atomic<unsigned> refreshCount=0,notificationCount=0;
static ULONG_PTR gdiplusToken=0;
static std::wstring uiReportPath;
struct Button{HWND hwnd=nullptr,owner=nullptr;std::wstring label;bool accent=false,active=false;int glyph=0;GUID target{};};
static std::map<HWND,std::unique_ptr<Button>> buttons;
static std::vector<HWND> cardButtons;
static HWND physicalButton,allButton,refreshButton,gearButton,closeButton,minButton,startupButton=nullptr,installButton=nullptr,uninstallButton=nullptr;
struct ReadResult{std::vector<Adapter> list;std::wstring error;bool service=false;};
static float Scale(HWND w){return GetDpiForWindow(w)/96.0f;}
static int Px(HWND w,float value){return static_cast<int>(value*Scale(w)+0.5f);}
static Color RGBColor(unsigned hex){return Color(255,(hex>>16)&255,(hex>>8)&255,hex&255);}
static void SetTheme(){
    DWORD light=1,bytes=4;RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    theme=light?Palette{RGBColor(0xF5F6F8),RGBColor(0xFFFFFF),RGBColor(0x172033),RGBColor(0x788195),RGBColor(0xE7EAF0),RGBColor(0x346DFF),RGBColor(0xEAF0FF),false}:
        Palette{RGBColor(0x101217),RGBColor(0x1A1D25),RGBColor(0xF1F3F8),RGBColor(0x8992A5),RGBColor(0x2B303C),RGBColor(0x7196FF),RGBColor(0x26334F),true};
    BOOL dark=theme.dark;if(mainWindow){DwmSetWindowAttribute(mainWindow,20,&dark,sizeof(dark));InvalidateRect(mainWindow,nullptr,FALSE);}if(settingsWindow){DwmSetWindowAttribute(settingsWindow,20,&dark,sizeof(dark));InvalidateRect(settingsWindow,nullptr,FALSE);}
    for(auto&[h,b]:buttons)InvalidateRect(h,nullptr,FALSE);
}
static void Round(Graphics& g,const RectF& r,float radius,Color color,Color* border=nullptr){
    GraphicsPath path;float d=radius*2;path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();SolidBrush fill(color);g.FillPath(&fill,&path);if(border){Pen pen(*border,1);g.DrawPath(&pen,&path);}}
static void Text(Graphics& g,const std::wstring& text,float x,float y,float w,float h,float size,Color color,bool bold=false,bool mono=false,int alignment=0){
    Font font(mono?L"Consolas":L"Microsoft YaHei UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush brush(color);StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetLineAlignment(StringAlignmentCenter);format.SetAlignment(alignment==1?StringAlignmentCenter:alignment==2?StringAlignmentFar:StringAlignmentNear);g.DrawString(text.c_str(),-1,&font,RectF(x,y,w,h),&format,&brush);}
static LRESULT CALLBACK ButtonProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){if(msg==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}else if(msg==WM_MOUSELEAVE||msg==WM_ENABLE||msg==WM_SETFOCUS||msg==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);return DefSubclassProc(h,msg,wp,lp);}
static HWND MakeButton(HWND parent,int id,const std::wstring& accessible,const std::wstring& label,bool accent=false,int glyph=0){
    HWND h=CreateWindowExW(0,L"BUTTON",accessible.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),appInstance,nullptr);
    auto b=std::make_unique<Button>();b->hwnd=h;b->owner=parent;b->label=label;b->accent=accent;b->glyph=glyph;buttons[h]=std::move(b);SetWindowSubclass(h,ButtonProc,1,0);return h;
}
static void Place(HWND h,float x,float y,float w,float height){HWND parent=GetParent(h);MoveWindow(h,Px(parent,x),Px(parent,y),Px(parent,w),Px(parent,height),TRUE);}
static void DrawButton(DRAWITEMSTRUCT* item){
    auto found=buttons.find(item->hwndItem);if(found==buttons.end())return;auto&b=*found->second;Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);float scale=Scale(item->hwndItem);g.ScaleTransform(scale,scale);float w=(item->rcItem.right-item->rcItem.left)/scale,h=(item->rcItem.bottom-item->rcItem.top)/scale;
    g.Clear(GetParent(item->hwndItem)==mainWindow&&b.glyph==0&&GetDlgCtrlID(item->hwndItem)>=200?theme.card:theme.bg);
    POINT mouse;GetCursorPos(&mouse);RECT rect;GetWindowRect(item->hwndItem,&rect);bool hover=PtInRect(&rect,mouse),disabled=!IsWindowEnabled(item->hwndItem),pressed=(item->itemState&ODS_SELECTED)!=0;
    Color fill=b.accent?theme.accent:b.active?theme.card:theme.bg;
    if(hover&&!disabled)fill=b.accent?(theme.dark?RGBColor(0x86A5FF):RGBColor(0x245BDD)):(theme.dark?RGBColor(0x282D38):RGBColor(0xE8ECF3));
    if(pressed)fill=theme.soft;
    if(b.glyph==0){Color border=b.accent?fill:b.active?theme.line:fill;Round(g,RectF(0.5f,0.5f,w-1,h-1),9,fill,&border);Text(g,b.label,0,0,w,h,13,disabled?theme.muted:b.accent?(theme.dark?RGBColor(0x101726):RGBColor(0xFFFFFF)):theme.text,true,false,1);}
    else{
        if(hover)Round(g,RectF(1,1,w-2,h-2),9,fill);Pen pen(disabled?theme.muted:theme.muted,1.65f);float cx=w/2,cy=h/2;
        if(b.glyph==1){g.DrawLine(&pen,cx-4,cy-4,cx+4,cy+4);g.DrawLine(&pen,cx+4,cy-4,cx-4,cy+4);}
        else if(b.glyph==2)g.DrawLine(&pen,cx-5,cy,cx+5,cy);
        else if(b.glyph==3){g.DrawEllipse(&pen,RectF(cx-6,cy-6,12,12));g.DrawEllipse(&pen,RectF(cx-2,cy-2,4,4));for(int i=0;i<8;i++){double angle=i*3.14159265/4;g.DrawLine(&pen,cx+static_cast<float>(cos(angle)*7),cy+static_cast<float>(sin(angle)*7),cx+static_cast<float>(cos(angle)*9),cy+static_cast<float>(sin(angle)*9));}}
        else if(b.glyph==4){g.DrawArc(&pen,RectF(cx-6,cy-6,12,12),40,285);g.DrawLine(&pen,cx+5,cy-4,cx+6,cy+1);g.DrawLine(&pen,cx+6,cy+1,cx+1,cy);}
    }
    if(item->itemState&ODS_FOCUS){Pen focus(theme.accent,1);g.DrawRectangle(&focus,3,3,static_cast<int>(w)-7,static_cast<int>(h)-7);}
}
static std::vector<Adapter> Scope(){std::vector<Adapter> result;for(auto&a:adapters)if(!physical||a.hardware)result.push_back(a);return result;}
static void UpdateActions(){
    EnableWindow(physicalButton,!switching);EnableWindow(allButton,!switching);EnableWindow(refreshButton,!switching&&!reading);
    buttons[physicalButton]->active=physical;buttons[allButton]->active=!physical;
    auto scope=Scope();unsigned enabled=0;for(auto&a:scope)enabled+=a.enabled;
    for(auto h:cardButtons){auto&b=*buttons[h];auto found=std::find_if(scope.begin(),scope.end(),[&](auto&a){return Same(a.id,b.target);});bool chosen=found!=scope.end()&&found->enabled&&enabled==1;
        b.label=chosen?L"使用中":L"切换";b.accent=!chosen;if(found!=scope.end()){auto accessible=L"切换到 "+found->name+L"，"+found->state()+L"，IP "+(found->ipv4.empty()?L"无地址":found->ipv4);SetWindowTextW(h,accessible.c_str());}
        EnableWindow(h,!switching&&ready&&!chosen);InvalidateRect(h,nullptr,FALSE);}
    InvalidateRect(physicalButton,nullptr,FALSE);InvalidateRect(allButton,nullptr,FALSE);
}
static void Layout(bool resize=false){
    if(!mainWindow)return;auto scope=Scope();RECT rect;GetClientRect(mainWindow,&rect);float width=rect.right/Scale(mainWindow);
    if(resize){MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST),&monitor);float maxHeight=(monitor.rcWork.bottom-monitor.rcWork.top-48)/Scale(mainWindow);float height=std::min(maxHeight,178.0f+std::max<size_t>(scope.size(),1)*112.0f);
        SetWindowPos(mainWindow,nullptr,0,0,Px(mainWindow,width),Px(mainWindow,height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);GetClientRect(mainWindow,&rect);width=rect.right/Scale(mainWindow);}
    Place(closeButton,width-48,18,30,30);Place(minButton,width-120,18,30,30);Place(gearButton,width-84,18,30,30);
    Place(physicalButton,24,83,116,34);Place(allButton,144,83,116,34);Place(refreshButton,width-60,83,34,34);
    float visible=rect.bottom/Scale(mainWindow)-168;int maxScroll=static_cast<int>(std::max(0.0f,static_cast<float>(scope.size()*112)-12-visible));scrollY=std::clamp(scrollY,0,maxScroll);
    for(size_t i=0;i<cardButtons.size();i++){float y=142+static_cast<float>(i)*112-scrollY;Place(cardButtons[i],width-132,y+32,84,36);ShowWindow(cardButtons[i],y>=125&&y+100<rect.bottom/Scale(mainWindow)-20?SW_SHOW:SW_HIDE);}
    InvalidateRect(mainWindow,nullptr,FALSE);
}
static void RebuildCards(bool resize=false){
    for(auto h:cardButtons){DestroyWindow(h);buttons.erase(h);}cardButtons.clear();auto scope=Scope();
    for(size_t i=0;i<scope.size();i++){auto h=MakeButton(mainWindow,200+static_cast<int>(i),L"切换到 "+scope[i].name,L"切换",true);buttons[h]->target=scope[i].id;cardButtons.push_back(h);}
    UpdateActions();Layout(resize);
}
static void SetStatus(const std::wstring& text,bool error=false){statusText=text;statusError=error;InvalidateRect(mainWindow,nullptr,FALSE);}
static void BeginRead(){
    if(reading){readAgain=true;return;}reading=true;UpdateActions();HWND destination=mainWindow;
    std::thread([destination]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto result=new ReadResult;
        try{WindowsNetwork backend;result->list=backend.Read();result->service=ServiceReady();}catch(const std::exception& ex){result->error=Utf8ToWide(ex.what());}
        CoUninitialize();if(!PostMessageW(destination,WM_READ_DONE,0,reinterpret_cast<LPARAM>(result)))delete result;}).detach();
}
static void SwitchTo(const GUID& id){
    if(switching||installing)return;auto scope=Scope();if(scope.size()>32){SetStatus(L"网卡数量超出本版本支持范围",true);return;}
    Request request;request.target=id;request.physical=physical?1:0;request.count=static_cast<unsigned>(scope.size());for(size_t i=0;i<scope.size();i++)request.scope[i]=scope[i].id;
    switching=true;SetStatus(L"正在切换…");UpdateActions();HWND destination=mainWindow;
    std::thread([destination,request]{auto result=new Result(CallService(request));if(!PostMessageW(destination,WM_SWITCH_DONE,0,reinterpret_cast<LPARAM>(result)))delete result;}).detach();
}
static void ChangeMode(bool value){if(switching)return;try{WritePhysicalMode(value);physical=value;scrollY=0;RebuildCards(true);SetStatus(L"");}catch(const std::exception& ex){detailText=Utf8ToWide(ex.what());SetStatus(L"无法保存设置，点击此处查看",true);}}
static void ShowMain(){ShowWindow(mainWindow,SW_SHOW);ShowWindow(mainWindow,SW_RESTORE);SetForegroundWindow(mainWindow);PostMessageW(mainWindow,WM_REFRESH,0,0);}
static void Tray(bool add){NOTIFYICONDATAW icon{sizeof(icon)};icon.hWnd=mainWindow;icon.uID=1;icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;icon.uCallbackMessage=WM_TRAY;icon.hIcon=LoadIconW(appInstance,MAKEINTRESOURCEW(1));wcscpy_s(icon.szTip,L"网卡切换器");Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&icon);}
static void NetworkIcon(Graphics& g,float x,float y,bool wifi){Pen pen(theme.accent,1.8f);if(wifi){g.DrawArc(&pen,RectF(x+5,y+11,28,24),210,120);g.DrawArc(&pen,RectF(x+11,y+17,16,15),210,120);SolidBrush dot(theme.accent);g.FillEllipse(&dot,RectF(x+17,y+26,4,4));}else{g.DrawRectangle(&pen,RectF(x+9,y+9,20,15));g.DrawLine(&pen,x+19,y+24,x+19,y+30);g.DrawLine(&pen,x+13,y+30,x+25,y+30);}}
static void PaintMain(HWND window){PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT rect;GetClientRect(window,&rect);if(rect.right<=0||rect.bottom<=0){EndPaint(window,&ps);return;}Bitmap buffer(rect.right,rect.bottom,PixelFormat32bppPARGB);Graphics g(&buffer);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.Clear(theme.bg);float scale=Scale(window);g.ScaleTransform(scale,scale);float w=rect.right/scale,h=rect.bottom/scale;
    Text(g,L"网卡切换器",24,20,w-190,37,23,theme.text,true);
    Round(g,RectF(20,79,244,42),11,theme.dark?RGBColor(0x171A21):RGBColor(0xE9EDF3));
    auto scope=Scope();Region clip(RectF(0,129,w,h-153));g.SetClip(&clip);
    if(scope.empty())Text(g,reading?L"正在读取网卡…":L"没有可显示的网卡",24,160,w-48,100,14,theme.muted,false,false,1);
    for(size_t i=0;i<scope.size();i++){auto&a=scope[i];float y=142+static_cast<float>(i)*112-scrollY;if(y+100<129||y>h-24)continue;
        Round(g,RectF(24,y,w-48,100),14,theme.card,&theme.line);Round(g,RectF(40,y+28,42,42),12,theme.soft);NetworkIcon(g,42,y+28,a.description.find(L"WiFi")!=std::wstring::npos||a.name.find(L"Wi-Fi")!=std::wstring::npos);
        Text(g,a.name,98,y+15,w-252,26,16,theme.text,true);
        SolidBrush dot(a.connected?RGBColor(0x35B581):theme.muted);g.FillEllipse(&dot,RectF(99,y+50,5,5));Text(g,a.state(),110,y+41,w-264,22,12,theme.muted);
        Text(g,a.enabled?(a.ipv4.empty()?L"等待 IP 地址":a.ipv4):L"—",98,y+65,w-252,22,13,theme.muted,false,!a.ipv4.empty());
    }
    g.ResetClip();if(!statusText.empty())Text(g,statusText,26,h-27,w-52,22,11.5f,statusError?RGBColor(0xE77575):theme.muted);
    Graphics target(dc);target.DrawImage(&buffer,0,0);EndPaint(window,&ps);
}
static void Settings();
static void TrayMenu(){
    HMENU menu=CreatePopupMenu(),scopeMenu=CreatePopupMenu(),switchMenu=CreatePopupMenu();auto scope=Scope();
    AppendMenuW(menu,MF_STRING,400,L"打开窗口");AppendMenuW(scopeMenu,MF_STRING|(physical?MF_CHECKED:0),401,L"仅实体网卡");AppendMenuW(scopeMenu,MF_STRING|(!physical?MF_CHECKED:0),402,L"实体＋虚拟网卡");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(scopeMenu),L"切换范围");
    for(size_t i=0;i<scope.size();i++){auto label=scope[i].name+L"  ·  "+scope[i].state();AppendMenuW(switchMenu,MF_STRING|((switching||!ready)?MF_GRAYED:0),500+i,label.c_str());}AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(switchMenu),L"切换到");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,403,L"设置");AppendMenuW(menu,MF_STRING|(StartupEnabled()?MF_CHECKED:0),404,L"开机进入托盘");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|((switching||installing)?MF_GRAYED:0),405,L"退出");
    POINT p;GetCursorPos(&p);SetForegroundWindow(mainWindow);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,p.x,p.y,0,mainWindow,nullptr);DestroyMenu(menu);PostMessageW(mainWindow,WM_NULL,0,0);
    if(selected==400)ShowMain();else if(selected==401||selected==402)ChangeMode(selected==401);else if(selected==403)Settings();else if(selected==404){try{SetStartup(!StartupEnabled());}catch(const std::exception& ex){MessageBoxW(mainWindow,Utf8ToWide(ex.what()).c_str(),L"无法设置自启",MB_ICONERROR);}}
    else if(selected==405&&!switching&&!installing)DestroyWindow(mainWindow);else if(selected>=500&&selected<500+static_cast<int>(scope.size()))SwitchTo(scope[selected-500].id);
}
static void SetupService(bool remove){
    if(installing||switching)return;
    PWSTR localData=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&localData))){SetStatus(L"无法读取当前用户目录",true);return;}
    std::wstring path=std::wstring(localData)+L"\\NetSwitch";CoTaskMemFree(localData);
    if(!CreateDirectoryW(path.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){SetStatus(L"无法创建安装记录目录",true);return;}path+=L"\\service-setup-result.json";
    installing=true;if(installButton)EnableWindow(installButton,FALSE);if(uninstallButton)EnableWindow(uninstallButton,FALSE);HWND destination=mainWindow;auto sid=CurrentSid();
    std::thread([destination,sid,remove,path]{auto result=new Result;
        auto parameters=(remove?std::wstring(L"--uninstall-service "):L"--install-service "+sid+L" ")+L"\""+path+L"\"";auto executable=ExePath();
        SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;info.hwnd=destination;info.lpVerb=L"runas";info.lpFile=executable.c_str();info.lpParameters=parameters.c_str();info.nShow=SW_HIDE;
        if(ShellExecuteExW(&info)){WaitForSingleObject(info.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);result->success=code==0;wcsncpy_s(result->message,code==0?(remove?L"已卸载后台服务":L"后台服务已安装"):L"服务设置未完成，安装记录位于本地应用数据的 NetSwitch 文件夹。",_TRUNCATE);}
        else wcsncpy_s(result->message,GetLastError()==ERROR_CANCELLED?L"已取消管理员授权":L"无法启动服务安装程序",_TRUNCATE);
        if(!PostMessageW(destination,WM_INSTALL_DONE,remove?1:0,reinterpret_cast<LPARAM>(result)))delete result;}).detach();
}
static void UpdateSettings(){
    if(!settingsWindow)return;if(startupButton){buttons[startupButton]->label=StartupEnabled()?L"开启":L"关闭";buttons[startupButton]->accent=StartupEnabled();InvalidateRect(startupButton,nullptr,FALSE);}
    if(installButton){buttons[installButton]->label=ready?L"重新安装":L"安装服务";EnableWindow(installButton,!installing&&!switching);InvalidateRect(installButton,nullptr,FALSE);}
    if(uninstallButton){ShowWindow(uninstallButton,ready?SW_SHOW:SW_HIDE);EnableWindow(uninstallButton,!installing&&!switching);}
    InvalidateRect(settingsWindow,nullptr,FALSE);
}
static LRESULT CALLBACK SettingsProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{DWORD corner=2;DwmSetWindowAttribute(w,33,&corner,sizeof(corner));
        startupButton=MakeButton(w,310,L"开机进入托盘",L"关闭");Place(startupButton,344,113,92,34);
        installButton=MakeButton(w,311,L"安装或更新后台服务",L"安装服务");Place(installButton,344,177,92,34);
        uninstallButton=MakeButton(w,312,L"卸载后台服务",L"卸载");Place(uninstallButton,274,177,62,34);
        auto details=MakeButton(w,313,L"网络详情",L"网络详情");Place(details,24,251,112,36);
        auto done=MakeButton(w,314,L"完成",L"完成",true);Place(done,344,251,92,36);
        auto close=MakeButton(w,315,L"关闭设置",L"",false,1);Place(close,410,20,30,30);return 0;}
    case WM_NCCALCSIZE:return 0;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);if(p.y<Px(w,64))return HTCAPTION;return HTCLIENT;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);Bitmap bitmap(r.right,r.bottom,PixelFormat32bppPARGB);Graphics g(&bitmap);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.Clear(theme.bg);g.ScaleTransform(Scale(w),Scale(w));
        Text(g,L"设置",24,20,350,38,23,theme.text,true);Text(g,L"外观",24,70,160,26,14,theme.text);Text(g,theme.dark?L"跟随 Windows · 深色":L"跟随 Windows · 浅色",210,70,226,26,12,theme.muted,false,false,2);
        Text(g,L"开机进入托盘",24,116,260,26,14,theme.text);Pen line(theme.line,1);g.DrawLine(&line,24.0f,160.0f,436.0f,160.0f);
        Text(g,L"后台服务",24,176,190,25,14,theme.text);Text(g,installing?L"正在设置…":ready?L"已就绪":L"未就绪",24,201,200,22,12,ready?RGBColor(0x35B581):theme.muted);
        Graphics target(dc);target.DrawImage(&bitmap,0,0);EndPaint(w,&ps);return 0;}
    case WM_DRAWITEM:DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_COMMAND:switch(LOWORD(wp)){case 310:try{SetStartup(!StartupEnabled());UpdateSettings();}catch(const std::exception& ex){MessageBoxW(w,Utf8ToWide(ex.what()).c_str(),L"无法设置自启",MB_ICONERROR);}break;
        case 311:SetupService(false);break;case 312:SetupService(true);break;case 313:{std::wstring text;for(auto&a:adapters)text+=a.name+L" · "+a.state()+L"\n"+a.description+L"\nIPv4："+(a.ipv4.empty()?L"—":a.ipv4)+L"\n\n";if(!detailText.empty())text+=L"上次操作：\n"+detailText;
            MessageBoxW(w,text.c_str(),L"网络详情",MB_OK|MB_ICONINFORMATION);break;}case 314:case 315:SendMessageW(w,WM_CLOSE,0,0);break;}return 0;
    case WM_CLOSE:if(!installing){EnableWindow(mainWindow,TRUE);DestroyWindow(w);SetForegroundWindow(mainWindow);}return 0;
    case WM_DESTROY:{for(auto it=buttons.begin();it!=buttons.end();){if(it->second->owner==w)it=buttons.erase(it);else ++it;}startupButton=installButton=uninstallButton=nullptr;settingsWindow=nullptr;return 0;}
    case WM_SETTINGCHANGE:SetTheme();return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static void Settings(){
    if(settingsWindow){SetForegroundWindow(settingsWindow);return;}RECT r;GetWindowRect(mainWindow,&r);settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LocalNetSwitch.Settings.v2",L"网卡切换器设置",WS_POPUP|WS_CAPTION|WS_SYSMENU,
        r.left+Px(mainWindow,70),r.top+Px(mainWindow,40),Px(mainWindow,460),Px(mainWindow,310),mainWindow,nullptr,appInstance,nullptr);BOOL dark=theme.dark;DwmSetWindowAttribute(settingsWindow,20,&dark,sizeof(dark));EnableWindow(mainWindow,FALSE);UpdateSettings();ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);}
static VOID WINAPI AddressChanged(PVOID,PMIB_UNICASTIPADDRESS_ROW,MIB_NOTIFICATION_TYPE){notificationCount++;if(mainWindow)PostMessageW(mainWindow,WM_REFRESH,0,0);}
static VOID WINAPI InterfaceChanged(PVOID,PMIB_IPINTERFACE_ROW,MIB_NOTIFICATION_TYPE){notificationCount++;if(mainWindow)PostMessageW(mainWindow,WM_REFRESH,0,0);}
static void WriteUiReport(){if(uiReportPath.empty())return;try{WriteUtf8(uiReportPath,L"{\"RefreshCount\":"+std::to_wstring(refreshCount.load())+L",\"NetworkNotifications\":"+std::to_wstring(notificationCount.load())+L",\"ServiceReady\":"+(ready?L"true":L"false")+L",\"Switching\":"+(switching?L"true":L"false")+L",\"Status\":"+EscapeJson(statusText)+L",\"Adapters\":"+AdaptersJson(adapters)+L"}");}catch(...){} }
static LRESULT CALLBACK MainProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(showMessage&&msg==showMessage){ShowMain();return 0;}
    if(exitMessage&&msg==exitMessage){if(switching||installing)return FALSE;DestroyWindow(w);return TRUE;}
    switch(msg){
    case WM_CREATE:{mainWindow=w;DWORD corner=2;DwmSetWindowAttribute(w,33,&corner,sizeof(corner));SetTheme();
        closeButton=MakeButton(w,101,L"关闭到托盘",L"",false,1);minButton=MakeButton(w,102,L"最小化",L"",false,2);gearButton=MakeButton(w,103,L"设置",L"",false,3);
        physicalButton=MakeButton(w,110,L"仅实体网卡",L"实体网卡");allButton=MakeButton(w,111,L"实体及虚拟网卡",L"全部网卡");refreshButton=MakeButton(w,112,L"立即刷新",L"",false,4);
        Layout();Tray(true);
        DWORD address=NotifyUnicastIpAddressChange(AF_UNSPEC,AddressChanged,nullptr,FALSE,&addressNotify),network=NotifyIpInterfaceChange(AF_UNSPEC,InterfaceChanged,nullptr,FALSE,&interfaceNotify);
        if(address!=NO_ERROR||network!=NO_ERROR){detailText=L"网络变化通知注册失败："+std::to_wstring(address)+L" / "+std::to_wstring(network);SetStatus(L"自动更新不可用，可点击刷新",true);}BeginRead();return 0;}
    case WM_NCCALCSIZE:return 0;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);if(p.y<Px(w,70)&&p.x<Px(w,440))return HTCAPTION;return HTCLIENT;}
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={Px(w,600),Px(w,350)};return 0;}
    case WM_SIZE:Layout();return 0;
    case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout(true);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:PaintMain(w);return 0;
    case WM_DRAWITEM:DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_MOUSEWHEEL:scrollY-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*56;Layout();return 0;
    case WM_LBUTTONUP:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};RECT r;GetClientRect(w,&r);if(statusError&&p.y>r.bottom-Px(w,30)&&!detailText.empty())MessageBoxW(w,detailText.c_str(),L"操作详情",MB_OK|MB_ICONERROR);return 0;}
    case WM_COMMAND:{int id=LOWORD(wp);if(id==101)ShowWindow(w,SW_HIDE);else if(id==102)ShowWindow(w,SW_MINIMIZE);else if(id==103)Settings();else if(id==110||id==111)ChangeMode(id==110);else if(id==112){SetStatus(L"");BeginRead();}
        else if(id>=200&&id<232){auto h=reinterpret_cast<HWND>(lp);auto found=buttons.find(h);if(found!=buttons.end())SwitchTo(found->second->target);}return 0;}
    case WM_REFRESH:SetTimer(w,1,180,nullptr);SetTimer(w,3,900,nullptr);return 0;
    case WM_TIMER:KillTimer(w,wp);if(wp==1||wp==2||wp==3)BeginRead();return 0;
    case WM_READ_DONE:{std::unique_ptr<ReadResult> result(reinterpret_cast<ReadResult*>(lp));reading=false;refreshCount++;
        if(result->error.empty()){bool countChanged=Scope().size()!=(physical?std::count_if(result->list.begin(),result->list.end(),[](auto&a){return a.hardware;}):result->list.size());adapters=std::move(result->list);ready=result->service;
            if(!ready&&!switching&&!statusError)SetStatus(L"请在设置中安装后台服务");else if(ready&&statusText==L"请在设置中安装后台服务")SetStatus(L"");RebuildCards(countChanged);UpdateSettings();}
        else{detailText=result->error;SetStatus(L"读取失败，点击此处查看",true);UpdateActions();}
        WriteUiReport();if(readAgain){readAgain=false;BeginRead();}return 0;}
    case WM_SWITCH_DONE:{std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));switching=false;detailText=result->message;SetStatus(result->success?result->message:L"切换未完成，点击此处查看",!result->success);UpdateActions();BeginRead();SetTimer(w,2,700,nullptr);
        if(!IsWindowVisible(w)){NOTIFYICONDATAW icon{sizeof(icon)};icon.hWnd=w;icon.uID=1;icon.uFlags=NIF_INFO;wcsncpy_s(icon.szInfoTitle,result->success?L"切换完成":L"切换未完成",_TRUNCATE);wcsncpy_s(icon.szInfo,result->message,_TRUNCATE);icon.dwInfoFlags=result->success?NIIF_INFO:NIIF_WARNING;Shell_NotifyIconW(NIM_MODIFY,&icon);}return 0;}
    case WM_INSTALL_DONE:{std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));installing=false;detailText=result->message;SetStatus(result->message,!result->success);BeginRead();UpdateSettings();return 0;}
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:SetTheme();UpdateSettings();return 0;
    case WM_TRAY:if(lp==WM_LBUTTONDBLCLK)ShowMain();else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)TrayMenu();return 0;
    case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
    case WM_QUERYENDSESSION:return !switching;
    case WM_DESTROY:if(addressNotify)CancelMibChangeNotify2(addressNotify);if(interfaceNotify)CancelMibChangeNotify2(interfaceNotify);Tray(false);KillTimer(w,1);KillTimer(w,2);KillTimer(w,3);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
int RunUi(HINSTANCE instance,bool trayOnly){
    appInstance=instance;startHidden=trayOnly;physical=ReadPhysicalMode();GdiplusStartupInput startup;GdiplusStartup(&gdiplusToken,&startup,nullptr);
    showMessage=RegisterWindowMessageW(L"LocalNetSwitch.Show.v2");exitMessage=RegisterWindowMessageW(L"LocalNetSwitch.Exit.v2");auto mutexName=L"Local\\LocalNetSwitch.UI.v2."+CurrentSid();HANDLE mutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(L"LocalNetSwitch.Main.v2",nullptr);if(existing&&!trayOnly){AllowSetForegroundWindow(ASFW_ANY);PostMessageW(existing,showMessage,0,0);}CloseHandle(mutex);GdiplusShutdown(gdiplusToken);return 0;}
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i+1<argc;i++)if(std::wstring(args[i])==L"--ui-report")uiReportPath=args[i+1];LocalFree(args);
    WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=MainProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"LocalNetSwitch.Main.v2";cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));cls.hIconSm=cls.hIcon;RegisterClassExW(&cls);
    cls.lpfnWndProc=SettingsProc;cls.lpszClassName=L"LocalNetSwitch.Settings.v2";RegisterClassExW(&cls);
    RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);UINT dpi=GetDpiForSystem();int width=MulDiv(600,dpi,96),height=MulDiv(402,dpi,96);
    HWND window=CreateWindowExW(0,L"LocalNetSwitch.Main.v2",L"网卡切换器",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
        (work.left+work.right-width)/2,(work.top+work.bottom-height)/2,width,height,nullptr,nullptr,instance,nullptr);
    if(!window){CloseHandle(mutex);GdiplusShutdown(gdiplusToken);return 1;}if(!trayOnly)ShowWindow(window,SW_SHOW);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(settingsWindow?settingsWindow:window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    mainWindow=nullptr;buttons.clear();CloseHandle(mutex);GdiplusShutdown(gdiplusToken);return 0;
}
}
