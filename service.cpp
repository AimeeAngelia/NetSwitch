// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 AimeeAngelia

#include "core.h"
#include <aclapi.h>

namespace ns {
static std::wstring InstalledPath(){PWSTR folder=nullptr;Check(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&folder),L"读取服务安装目录");std::wstring path=folder;CoTaskMemFree(folder);return path+L"\\网卡切换器\\NetSwitch.exe";}
static HANDLE stopEvent=nullptr;
static SERVICE_STATUS_HANDLE statusHandle=nullptr;
static SERVICE_STATUS status{};
static std::mutex statusMutex;
static void Report(DWORD state,DWORD error=NO_ERROR){std::lock_guard<std::mutex> lock(statusMutex);status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;status.dwWin32ExitCode=error;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    status.dwWaitHint=state==SERVICE_START_PENDING||state==SERVICE_STOP_PENDING?15000:0;SetServiceStatus(statusHandle,&status);}
static bool Transfer(HANDLE pipe,void* data,DWORD size,bool write,DWORD timeout,HANDLE stop=nullptr){
    OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD total=0;bool success=true;
    while(total<size){ResetEvent(operation.hEvent);DWORD moved=0;BOOL ok=write?WriteFile(pipe,static_cast<BYTE*>(data)+total,size-total,&moved,&operation):ReadFile(pipe,static_cast<BYTE*>(data)+total,size-total,&moved,&operation);
        if(!ok){if(GetLastError()!=ERROR_IO_PENDING){success=false;break;}HANDLE events[2]={operation.hEvent,stop};DWORD wait=WaitForMultipleObjects(stop?2:1,events,FALSE,timeout);
            if(wait!=WAIT_OBJECT_0){CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&moved,TRUE);success=false;break;}
            if(!GetOverlappedResult(pipe,&operation,&moved,FALSE)){success=false;break;}}
        if(!moved){success=false;break;}total+=moved;}
    CloseHandle(operation.hEvent);return success;
}
static bool IsLocalUser(HANDLE token){
    BYTE usersBuffer[SECURITY_MAX_SID_SIZE],adminsBuffer[SECURITY_MAX_SID_SIZE];DWORD usersSize=sizeof(usersBuffer),adminsSize=sizeof(adminsBuffer);BOOL users=FALSE,admins=FALSE;
    if(CreateWellKnownSid(WinBuiltinUsersSid,nullptr,usersBuffer,&usersSize))CheckTokenMembership(token,usersBuffer,&users);
    if(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,adminsBuffer,&adminsSize))CheckTokenMembership(token,adminsBuffer,&admins);
    return users||admins;
}
static bool ClientAllowed(HANDLE pipe,const std::wstring& owner,bool allUsers){
    if(!ImpersonateNamedPipeClient(pipe))return false;bool allowed=false;HANDLE token=nullptr;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token)){DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> data(bytes);
        if(allUsers)allowed=IsLocalUser(token);
        else if(GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes)){LPWSTR text=nullptr;if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&text)){allowed=owner==text;LocalFree(text);}}
        CloseHandle(token);}
    RevertToSelf();return allowed;
}
static VOID WINAPI ServiceControl(DWORD code){if(code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN){Report(SERVICE_STOP_PENDING);SetEvent(stopEvent);}}
static VOID WINAPI ServiceMain(DWORD,LPWSTR*){
    statusHandle=RegisterServiceCtrlHandlerW(ServiceName,ServiceControl);if(!statusHandle)return;Report(SERVICE_START_PENDING);
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto owner=ReadSetting(HKEY_LOCAL_MACHINE,ConfigKey,L"OwnerSid");PSID validate=nullptr;
    if(owner.empty()||!ConvertStringSidToSidW(owner.c_str(),&validate)){Report(SERVICE_STOPPED,ERROR_INVALID_DATA);CoUninitialize();CloseHandle(stopEvent);return;}LocalFree(validate);
    bool allUsers=ReadSetting(HKEY_LOCAL_MACHINE,ConfigKey,L"AccessScope")==L"LocalUsers";
    auto sddl=allUsers?std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)"):L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;"+owner+L")";PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)){Report(SERVICE_STOPPED,GetLastError());CoUninitialize();CloseHandle(stopEvent);return;}
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};auto network=std::make_unique<WindowsNetwork>();bool running=true;DWORD error=NO_ERROR;
    while(running&&WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0){
        HANDLE pipe=CreateNamedPipeW(PipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(Result),sizeof(Request),0,&security);
        if(pipe==INVALID_HANDLE_VALUE){error=GetLastError();break;}
        Report(SERVICE_RUNNING);OVERLAPPED connection{};connection.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        BOOL connected=ConnectNamedPipe(pipe,&connection);DWORD connectionError=connected?NO_ERROR:GetLastError();bool accepted=!!connected||connectionError==ERROR_PIPE_CONNECTED;
        if(connectionError==ERROR_IO_PENDING){HANDLE events[]={connection.hEvent,stopEvent};DWORD wait=WaitForMultipleObjects(2,events,FALSE,INFINITE);
            if(wait==WAIT_OBJECT_0){DWORD done=0;accepted=!!GetOverlappedResult(pipe,&connection,&done,FALSE);}else{CancelIoEx(pipe,&connection);DWORD done=0;GetOverlappedResult(pipe,&connection,&done,TRUE);running=false;}}
        CloseHandle(connection.hEvent);
        if(accepted){Request request;Result reply;
            if(Transfer(pipe,&request,sizeof(request),false,5000,stopEvent)){
                if(!ClientAllowed(pipe,owner,allUsers))wcsncpy_s(reply.message,L"此服务未授权给当前用户。",_TRUNCATE);
                else if(request.magic!=Magic||request.version!=2)wcsncpy_s(reply.message,L"服务协议不匹配，请更新程序。",_TRUNCATE);
                else if(request.operation==1){reply.success=1;wcsncpy_s(reply.message,L"后台服务已就绪",_TRUNCATE);
                    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))){reply.workingSet=memory.WorkingSetSize;reply.privateBytes=memory.PrivateUsage;}
                    FILETIME created,ended,kernel,user;if(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user)){ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;reply.cpuTime=k.QuadPart+u.QuadPart;}}
                else reply=Apply(*network,request);
                Transfer(pipe,&reply,sizeof(reply),true,5000,stopEvent);
            }
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    }
    network.reset();LocalFree(descriptor);CoUninitialize();CloseHandle(stopEvent);Report(SERVICE_STOPPED,error);
}
int RunService(){SERVICE_TABLE_ENTRYW table[]={{const_cast<LPWSTR>(ServiceName),ServiceMain},{nullptr,nullptr}};if(!StartServiceCtrlDispatcherW(table))return static_cast<int>(GetLastError());return 0;}

Result CallService(const Request& request){
    Result result;HANDLE pipe=INVALID_HANDLE_VALUE;
    try{
        if(!WaitNamedPipeW(PipeName,2500))throw std::runtime_error("后台服务未就绪，请在设置中安装或检查服务。");
        pipe=CreateFileW(PipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("无法连接后台服务。");
        ULONG pid=0;if(!GetNamedPipeServerProcessId(pipe,&pid))throw std::runtime_error("无法核对后台服务身份。");
        bool trusted=false;SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
        if(manager){SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG);if(service){SERVICE_STATUS_PROCESS state{};DWORD bytes=0;
            if(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&bytes)&&state.dwProcessId==pid){DWORD needed=0;QueryServiceConfigW(service,nullptr,0,&needed);std::vector<BYTE> buffer(needed);
                if(needed&&QueryServiceConfigW(service,reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data()),needed,&needed)){auto expected=L"\""+InstalledPath()+L"\" --service";trusted=_wcsicmp(reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data())->lpBinaryPathName,expected.c_str())==0;}}
            CloseServiceHandle(service);}CloseServiceHandle(manager);}
        if(!trusted)throw std::runtime_error("后台服务的程序路径不匹配。");
        Request copy=request;if(!Transfer(pipe,&copy,sizeof(copy),true,5000)||!Transfer(pipe,&result,sizeof(result),false,90000))throw std::runtime_error("等待后台服务响应超时，请核对实际网卡状态。");
        if(result.magic!=Magic)throw std::runtime_error("后台服务返回了无效结果。");
    }catch(const std::exception& ex){result={};wcsncpy_s(result.message,Utf8ToWide(ex.what()).c_str(),_TRUNCATE);}
    if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);return result;
}
bool ServiceReady(){
    if(ReadSetting(HKEY_LOCAL_MACHINE,ConfigKey,L"AccessScope")==L"LocalUsers"){if(!IsLocalUser(nullptr))return false;}
    else if(ReadSetting(HKEY_LOCAL_MACHINE,ConfigKey,L"OwnerSid")!=CurrentSid())return false;
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_QUERY_STATUS);bool running=false;
    if(service){SERVICE_STATUS_PROCESS state{};DWORD bytes=0;running=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&bytes)&&state.dwCurrentState==SERVICE_RUNNING;CloseServiceHandle(service);}
    CloseServiceHandle(manager);return running;
}
static bool Administrator(){SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID group=nullptr;BOOL member=FALSE;
    AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&group);CheckTokenMembership(nullptr,group,&member);FreeSid(group);return !!member;}
static void StopAndWait(SC_HANDLE service){SERVICE_STATUS_PROCESS current{};DWORD bytes=0;if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&current),sizeof(current),&bytes))return;
    if(current.dwCurrentState==SERVICE_STOPPED)return;SERVICE_STATUS ignored{};ControlService(service,SERVICE_CONTROL_STOP,&ignored);
    for(int i=0;i<200;i++){if(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&current),sizeof(current),&bytes)&&current.dwCurrentState==SERVICE_STOPPED)return;Sleep(100);}
    throw std::runtime_error("服务尚未停止，请等待切换完成后再试。");}
int InstallService(const std::wstring& owner){
    if(!Administrator())throw std::runtime_error("安装后台服务需要管理员授权。");PSID sid=nullptr;if(!ConvertStringSidToSidW(owner.c_str(),&sid))throw std::runtime_error("用户标识无效。");LocalFree(sid);
    auto target=InstalledPath(),folder=target.substr(0,target.find_last_of(L'\\'));
    if(!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error(Utf8(L"无法创建服务目录："+ErrorText(GetLastError())));
    if(GetFileAttributesW(folder.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("服务安装目录不能是重解析链接。");
    PSECURITY_DESCRIPTOR descriptor=nullptr;ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)",SDDL_REVISION_1,&descriptor,nullptr);
    if(!descriptor||!SetFileSecurityW(folder.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,descriptor)){if(descriptor)LocalFree(descriptor);throw std::runtime_error("无法保护服务目录。");}LocalFree(descriptor);
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE);if(!manager)throw std::runtime_error("无法打开服务管理器。");
    SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_ALL_ACCESS);
    try{
        if(service)StopAndWait(service);
        if(_wcsicmp(ExePath().c_str(),target.c_str())!=0&&!CopyFileW(ExePath().c_str(),target.c_str(),FALSE))throw std::runtime_error(Utf8(L"复制服务程序失败："+ErrorText(GetLastError())));
        HKEY key;LONG registry=RegCreateKeyExW(HKEY_LOCAL_MACHINE,ConfigKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(registry!=ERROR_SUCCESS)throw std::runtime_error("无法登记服务配置。");
        registry=RegSetValueExW(key,L"OwnerSid",0,REG_SZ,reinterpret_cast<const BYTE*>(owner.c_str()),static_cast<DWORD>((owner.size()+1)*2));
        const wchar_t scope[]=L"LocalUsers";if(registry==ERROR_SUCCESS)registry=RegSetValueExW(key,L"AccessScope",0,REG_SZ,reinterpret_cast<const BYTE*>(scope),sizeof(scope));RegCloseKey(key);if(registry!=ERROR_SUCCESS)throw std::runtime_error("无法保存服务权限范围。");
        auto command=L"\""+target+L"\" --service";
        if(!service)service=CreateServiceW(manager,ServiceName,L"网卡切换器后台服务",SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,command.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);
        else if(!ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,command.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr))throw std::runtime_error("无法更新后台服务。");
        if(!service)throw std::runtime_error(Utf8(L"注册后台服务失败："+ErrorText(GetLastError())));
        SERVICE_DESCRIPTIONW description{const_cast<LPWSTR>(L"为本地 Windows 用户切换网卡；空闲时等待本地请求，不定时轮询网络。")};ChangeServiceConfig2W(service,SERVICE_CONFIG_DESCRIPTION,&description);
        if(!StartServiceW(service,0,nullptr)&&GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)throw std::runtime_error("无法启动后台服务。");
        SERVICE_STATUS_PROCESS current{};DWORD bytes=0;bool running=false;
        for(int i=0;i<100;i++){if(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&current),sizeof(current),&bytes)&&current.dwCurrentState==SERVICE_RUNNING){running=true;break;}if(current.dwCurrentState==SERVICE_STOPPED)break;Sleep(100);}
        if(!running)throw std::runtime_error("后台服务未成功进入运行状态。");
    }catch(...){if(service)CloseServiceHandle(service);CloseServiceHandle(manager);throw;}
    CloseServiceHandle(service);CloseServiceHandle(manager);return 0;
}
int UninstallService(){
    if(!Administrator())throw std::runtime_error("卸载后台服务需要管理员授权。");SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)throw std::runtime_error("无法打开服务管理器。");
    SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_STOP|SERVICE_QUERY_STATUS|DELETE);
    if(service){try{StopAndWait(service);if(!DeleteService(service))throw std::runtime_error("无法移除服务登记。");}catch(...){CloseServiceHandle(service);CloseServiceHandle(manager);throw;}CloseServiceHandle(service);}
    CloseServiceHandle(manager);auto target=InstalledPath();if(_wcsicmp(ExePath().c_str(),target.c_str())!=0){DeleteFileW(target.c_str());RemoveDirectoryW(target.substr(0,target.find_last_of(L'\\')).c_str());}
    HKEY key;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,ConfigKey,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){RegDeleteValueW(key,L"OwnerSid");RegDeleteValueW(key,L"AccessScope");RegCloseKey(key);}
    // Remove only this installed executable's startup entry from loaded profiles.
    auto startup=L"\""+InstalledPath()+L"\" --tray";
    for(DWORD index=0;;index++){wchar_t name[256];DWORD count=256;LONG next=RegEnumKeyExW(HKEY_USERS,index,name,&count,nullptr,nullptr,nullptr,nullptr);if(next==ERROR_NO_MORE_ITEMS)break;if(next!=ERROR_SUCCESS)continue;
        auto path=std::wstring(name)+L"\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        if(_wcsicmp(ReadSetting(HKEY_USERS,path.c_str(),L"NetSwitch").c_str(),startup.c_str())==0&&RegOpenKeyExW(HKEY_USERS,path.c_str(),0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){RegDeleteValueW(key,L"NetSwitch");RegCloseKey(key);}}
    return 0;
}
int StartExistingService(){
    if(!Administrator())throw std::runtime_error("启动服务需要管理员权限。");SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)throw std::runtime_error("无法打开服务管理器。");
    SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_START);if(!service){DWORD error=GetLastError();CloseServiceHandle(manager);if(error==ERROR_SERVICE_DOES_NOT_EXIST)return 0;throw std::runtime_error("无法打开后台服务。");}
    bool ok=!!StartServiceW(service,0,nullptr)||GetLastError()==ERROR_SERVICE_ALREADY_RUNNING;CloseServiceHandle(service);CloseServiceHandle(manager);if(!ok)throw std::runtime_error("无法恢复后台服务。");return 0;
}
int PrepareUpdate(){
    if(!Administrator())throw std::runtime_error("安装或卸载需要管理员权限。");SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)throw std::runtime_error("无法打开服务管理器。");
    SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_STOP|SERVICE_QUERY_STATUS);
    if(service){try{StopAndWait(service);}catch(...){CloseServiceHandle(service);CloseServiceHandle(manager);throw;}CloseServiceHandle(service);}else if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST){CloseServiceHandle(manager);throw std::runtime_error("无法检查后台服务。");}CloseServiceHandle(manager);
    try{
        for(int attempt=0;attempt<10;attempt++){HWND window=FindWindowW(L"LocalNetSwitch.Main.v2",nullptr);if(!window)break;DWORD pid=0;DWORD thread=GetWindowThreadProcessId(window,&pid);DWORD_PTR canClose=0;
            if(!SendMessageTimeoutW(window,WM_QUERYENDSESSION,0,ENDSESSION_CLOSEAPP,SMTO_ABORTIFHUNG,2000,&canClose)||!canClose)throw std::runtime_error("网卡切换器正在操作中，请稍后再试。");
            UINT closeMessage=RegisterWindowMessageW(L"LocalNetSwitch.Exit.v2");DWORD_PTR closed=0;SendMessageTimeoutW(window,closeMessage,0,0,SMTO_ABORTIFHUNG,1000,&closed);
            if(!closed)PostThreadMessageW(thread,WM_QUIT,0,0);
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(process){DWORD wait=WaitForSingleObject(process,5000);CloseHandle(process);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("程序尚未退出，请关闭窗口后重试。");}}
    }catch(...){try{StartExistingService();}catch(...){}throw;}
    return 0;
}
}
