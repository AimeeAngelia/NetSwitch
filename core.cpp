// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 AimeeAngelia

#include "core.h"
#include <comdef.h>
#include <ws2tcpip.h>

namespace ns {
static std::runtime_error Failure(const std::wstring& text) { return std::runtime_error(Utf8(text)); }
void Check(HRESULT hr, const wchar_t* operation) {
    if (FAILED(hr)) { wchar_t code[32]; swprintf_s(code,L" (0x%08X)",static_cast<unsigned>(hr));
        throw Failure(std::wstring(operation) + L"：" + ErrorText(hr) + code); }
}
std::wstring ErrorText(DWORD code) {
    wchar_t* buffer=nullptr; DWORD n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,code,0,reinterpret_cast<wchar_t*>(&buffer),0,nullptr);
    std::wstring text=n?std::wstring(buffer,n):L"Windows 返回错误";
    if(buffer)LocalFree(buffer);
    while(!text.empty()&&(text.back()==L'\r'||text.back()==L'\n'||text.back()==L' '))text.pop_back();
    return text;
}
std::wstring Utf8ToWide(const std::string& text) {
    if(text.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n);return out;
}
std::string Utf8(const std::wstring& text) {
    if(text.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);return out;
}
std::wstring GuidText(const GUID& id) { wchar_t text[40]; StringFromGUID2(id,text,40);return text; }
GUID ParseGuid(const std::wstring& text) { GUID id{};Check(CLSIDFromString(text.c_str(),&id),L"解析网卡标识");return id; }
std::wstring ExePath(){ wchar_t path[32768]; DWORD n=GetModuleFileNameW(nullptr,path,32768);return std::wstring(path,n); }
std::wstring ExeFolder(){ auto path=ExePath();return path.substr(0,path.find_last_of(L"\\/")); }
void WriteUtf8(const std::wstring& path,const std::wstring& text){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw Failure(L"无法写入文件："+ErrorText(GetLastError()));
    auto data=Utf8(text);DWORD written=0;bool ok=!!WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr);CloseHandle(file);
    if(!ok||written!=data.size())throw Failure(L"文件未完整写入。");
}
std::wstring EscapeJson(const std::wstring& text){
    std::wstring out=L"\"";
    for(wchar_t c:text){if(c==L'"')out+=L"\\\"";else if(c==L'\\')out+=L"\\\\";else if(c==L'\n')out+=L"\\n";else if(c==L'\r')out+=L"\\r";
        else if(c==L'\t')out+=L"\\t";else if(c<32){wchar_t b[8];swprintf_s(b,L"\\u%04x",c);out+=b;}else out+=c;}
    return out+L"\"";
}
std::wstring AdaptersJson(const std::vector<Adapter>& list){
    std::wstring out=L"[";bool first=true;
    for(auto&a:list){if(!first)out+=L",";first=false;out+=L"\n{\"Id\":"+EscapeJson(GuidText(a.id))+L",\"Name\":"+EscapeJson(a.name)+
        L",\"Hardware\":"+(a.hardware?L"true":L"false")+L",\"Enabled\":"+(a.enabled?L"true":L"false")+
        L",\"Connected\":"+(a.connected?L"true":L"false")+L",\"IPv4\":"+EscapeJson(a.ipv4)+L"}";}
    return out+L"\n]";
}
std::wstring CurrentSid(){
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw Failure(L"无法读取当前用户。");
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> buffer(size);
    if(!GetTokenInformation(token,TokenUser,buffer.data(),size,&size)){CloseHandle(token);throw Failure(L"无法读取当前用户标识。");}
    LPWSTR sid=nullptr;ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid);CloseHandle(token);
    std::wstring text=sid?sid:L"";if(sid)LocalFree(sid);return text;
}
std::wstring ReadSetting(HKEY hive,const wchar_t* key,const wchar_t* name){
    DWORD bytes=0;if(RegGetValueW(hive,key,name,RRF_RT_REG_SZ,nullptr,nullptr,&bytes)!=ERROR_SUCCESS)return {};
    std::wstring value(bytes/sizeof(wchar_t),0);
    if(RegGetValueW(hive,key,name,RRF_RT_REG_SZ,nullptr,value.data(),&bytes)!=ERROR_SUCCESS)return {};
    while(!value.empty()&&value.back()==0)value.pop_back();return value;
}
static VARIANT Get(IWbemClassObject* row,const wchar_t* key){VARIANT v;VariantInit(&v);Check(row->Get(key,0,&v,nullptr,nullptr),key);return v;}
static std::wstring Str(IWbemClassObject* row,const wchar_t* key){VARIANT v=Get(row,key);std::wstring out=(v.vt==VT_BSTR&&v.bstrVal)?v.bstrVal:L"";VariantClear(&v);return out;}
static ULONG Num(IWbemClassObject* row,const wchar_t* key){VARIANT v=Get(row,key);ULONG out=0;
    if(v.vt==VT_I4||v.vt==VT_UI4)out=v.ulVal;else if(v.vt==VT_I2||v.vt==VT_UI2)out=v.uiVal;else if(v.vt==VT_BOOL)out=v.boolVal!=VARIANT_FALSE;
    VariantClear(&v);return out;}
void WindowsNetwork::Connect(){
    if(service)return;ComPtr<IWbemLocator> locator;Check(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)),L"打开 Windows 网络管理");
    BSTR path=SysAllocString(L"ROOT\\StandardCimv2");HRESULT hr=locator->ConnectServer(path,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service);SysFreeString(path);Check(hr,L"连接 Windows 网络管理");
    Check(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE),L"设置本机网络管理权限");
}
std::vector<Adapter> WindowsNetwork::Read(){
    Connect();ComPtr<IEnumWbemClassObject> items;
    BSTR lang=SysAllocString(L"WQL"),query=SysAllocString(L"SELECT Name,InterfaceGuid,InterfaceIndex,InterfaceDescription,HardwareInterface,Virtual,Hidden,InterfaceAdminStatus,InterfaceOperationalStatus FROM MSFT_NetAdapter WHERE Hidden = FALSE");
    HRESULT hr=service->ExecQuery(lang,query,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&items);SysFreeString(lang);SysFreeString(query);Check(hr,L"读取网卡");
    std::vector<Adapter> list;
    for(;;){ComPtr<IWbemClassObject> row;ULONG count=0;hr=items->Next(5000,1,&row,&count);
        if(hr==WBEM_S_TIMEDOUT)throw Failure(L"读取网卡超时。");Check(hr,L"读取网卡属性");if(!count)break;
        Adapter a;a.id=ParseGuid(Str(row.Get(),L"InterfaceGuid"));a.name=Str(row.Get(),L"Name");a.description=Str(row.Get(),L"InterfaceDescription");
        a.index=Num(row.Get(),L"InterfaceIndex");a.hardware=!!Num(row.Get(),L"HardwareInterface");a.enabled=Num(row.Get(),L"InterfaceAdminStatus")==1;
        a.connected=a.enabled&&Num(row.Get(),L"InterfaceOperationalStatus")==1;list.push_back(std::move(a));}
    ULONG bytes=16384;std::vector<BYTE> buffer(bytes);ULONG result=ERROR_BUFFER_OVERFLOW;
    for(int i=0;i<3&&result==ERROR_BUFFER_OVERFLOW;i++){buffer.resize(bytes);result=GetAdaptersAddresses(AF_INET,GAA_FLAG_INCLUDE_ALL_INTERFACES|GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&bytes);}
    if(result!=NO_ERROR&&result!=ERROR_NO_DATA)throw Failure(L"读取 IP 地址失败："+ErrorText(result));
    if(result==NO_ERROR)for(auto p=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());p;p=p->Next){
        auto match=std::find_if(list.begin(),list.end(),[&](auto&a){return a.index==p->IfIndex;});
        if(match==list.end()||!match->enabled)continue;
        for(auto ip=p->FirstUnicastAddress;ip;ip=ip->Next){if(!ip->Address.lpSockaddr||ip->Address.lpSockaddr->sa_family!=AF_INET)continue;
            if(ip->DadState==IpDadStateInvalid||ip->DadState==IpDadStateDuplicate)continue;
            wchar_t text[INET_ADDRSTRLEN];auto ipv4=reinterpret_cast<sockaddr_in*>(ip->Address.lpSockaddr);
            if(InetNtopW(AF_INET,&ipv4->sin_addr,text,INET_ADDRSTRLEN)){if(!match->ipv4.empty())match->ipv4+=L" · ";match->ipv4+=text;}}
    }
    std::sort(list.begin(),list.end(),[](auto&a,auto&b){if(a.hardware!=b.hardware)return a.hardware>b.hardware;return _wcsicmp(a.name.c_str(),b.name.c_str())<0;});return list;
}
void WindowsNetwork::Set(const GUID& id,bool enabled){
    Connect();ComPtr<IEnumWbemClassObject> rows;auto q=L"SELECT * FROM MSFT_NetAdapter WHERE Hidden = FALSE AND InterfaceGuid = '"+GuidText(id)+L"'";
    BSTR query=SysAllocString(q.c_str()),lang=SysAllocString(L"WQL");HRESULT hr=service->ExecQuery(lang,query,WBEM_FLAG_FORWARD_ONLY,nullptr,&rows);SysFreeString(query);SysFreeString(lang);Check(hr,L"定位目标网卡");
    ComPtr<IWbemClassObject> row;ULONG count=0;Check(rows->Next(5000,1,&row,&count),L"读取目标网卡");if(count!=1)throw Failure(L"目标网卡已不存在，请刷新。");
    auto objectPath=Str(row.Get(),L"__PATH");BSTR path=SysAllocString(objectPath.c_str()),method=SysAllocString(enabled?L"Enable":L"Disable");ComPtr<IWbemClassObject> output;
    hr=service->ExecMethod(path,method,0,nullptr,nullptr,&output,nullptr);SysFreeString(path);SysFreeString(method);Check(hr,enabled?L"启用网卡":L"禁用网卡");
    ULONG code=output?Num(output.Get(),L"ReturnValue"):0;if(code!=0&&code!=4096)throw Failure((enabled?L"启用网卡失败：":L"禁用网卡失败：")+std::to_wstring(code));
    for(int n=0;n<40;n++){auto current=Read();auto found=std::find_if(current.begin(),current.end(),[&](auto&a){return Same(a.id,id);});
        if(found!=current.end()&&found->enabled==enabled)return;Sleep(125);}
    throw Failure(L"Windows 尚未确认请求的网卡状态。");
}
Result Apply(Network& backend,const Request& request){
    Result result;std::vector<Adapter> touched;
    try{
        if(request.magic!=Magic||request.version!=2||request.operation!=2||request.count==0||request.count>32||request.physical>1)throw Failure(L"切换请求无效。");
        auto initial=backend.Read();std::vector<Adapter> scope;
        for(unsigned i=0;i<request.count;i++){
            if(std::any_of(scope.begin(),scope.end(),[&](auto&a){return Same(a.id,request.scope[i]);}))throw Failure(L"切换范围有重复网卡。");
            auto found=std::find_if(initial.begin(),initial.end(),[&](auto&a){return Same(a.id,request.scope[i]);});
            if(found==initial.end())throw Failure(L"网卡列表发生变化，请刷新后重试。");
            if(request.physical&&!found->hardware)throw Failure(L"实体模式不能操作虚拟网卡。");scope.push_back(*found);
        }
        auto target=std::find_if(scope.begin(),scope.end(),[&](auto&a){return Same(a.id,request.target);});if(target==scope.end())throw Failure(L"目标不在切换范围内。");
        if(!target->enabled){touched.push_back(*target);backend.Set(target->id,true);}
        for(auto&a:scope)if(!Same(a.id,target->id)&&a.enabled){touched.push_back(a);backend.Set(a.id,false);}
        auto final=backend.Read();for(auto&a:scope){auto current=std::find_if(final.begin(),final.end(),[&](auto&b){return Same(a.id,b.id);});
            if(current==final.end()||current->enabled!=Same(a.id,target->id))throw Failure(L"切换后的状态核对未通过。");}
        result.success=1;auto message=L"已切换到 "+target->name;wcsncpy_s(result.message,message.c_str(),_TRUNCATE);return result;
    }catch(const std::exception& ex){
        std::stable_sort(touched.begin(),touched.end(),[](auto&a,auto&b){return a.enabled>b.enabled;});std::wstring errors;
        for(auto&a:touched)try{backend.Set(a.id,a.enabled);auto now=backend.Read();auto found=std::find_if(now.begin(),now.end(),[&](auto&b){return Same(a.id,b.id);});if(found==now.end()||found->enabled!=a.enabled)throw Failure(L"恢复状态核对失败。");}
            catch(const std::exception& rollback){result.rollbackFailed=1;errors+=L"\n"+a.name+L"："+Utf8ToWide(rollback.what());}
        auto message=Utf8ToWide(ex.what());if(!touched.empty())message+=result.rollbackFailed?L"\n部分网卡恢复失败，请查看网络连接。"+errors:L"\n已恢复本次改动。";
        wcsncpy_s(result.message,message.c_str(),_TRUNCATE);return result;
    }
}
static std::wstring StartupCommand(){return L"\""+ExePath()+L"\" --tray";}
bool StartupEnabled(){return _wcsicmp(ReadSetting(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"NetSwitch").c_str(),StartupCommand().c_str())==0;}
void SetStartup(bool enabled){
    HKEY key;LONG error=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(error!=ERROR_SUCCESS)throw Failure(ErrorText(error));
    auto command=StartupCommand();error=enabled?RegSetValueExW(key,L"NetSwitch",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*2)):RegDeleteValueW(key,L"NetSwitch");RegCloseKey(key);
    if(error!=ERROR_SUCCESS&&!(error==ERROR_FILE_NOT_FOUND&&!enabled))throw Failure(ErrorText(error));
}
bool ReadPhysicalMode(){DWORD physical=1,bytes=sizeof(physical);if(RegGetValueW(HKEY_CURRENT_USER,ConfigKey,L"PhysicalOnly",RRF_RT_REG_DWORD,nullptr,&physical,&bytes)==ERROR_SUCCESS)return !!physical;
    std::ifstream old(ExeFolder()+L"\\data\\settings.json");std::string data((std::istreambuf_iterator<char>(old)),{});auto key=data.find("\"PhysicalOnly\"");if(key!=std::string::npos){auto colon=data.find(':',key);auto p=data.find_first_not_of(" \r\n\t",colon+1);if(p!=std::string::npos&&data.compare(p,5,"false")==0)return false;}return true;}
void WritePhysicalMode(bool physical){HKEY key;LONG error=RegCreateKeyExW(HKEY_CURRENT_USER,ConfigKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(error!=ERROR_SUCCESS)throw Failure(ErrorText(error));DWORD value=physical?1:0;
    error=RegSetValueExW(key,L"PhysicalOnly",0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value));RegCloseKey(key);if(error!=ERROR_SUCCESS)throw Failure(ErrorText(error));}
}
