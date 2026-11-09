// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 AimeeAngelia

#include "core.h"

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
    int count=0;LPWSTR* arguments=CommandLineToArgvW(GetCommandLineW(),&count);std::vector<std::wstring> args;
    for(int i=1;i<count;i++)args.emplace_back(arguments[i]);LocalFree(arguments);int code=0;std::wstring report;
    try{
        if(!args.empty()&&args[0]==L"--service"){CoUninitialize();return ns::RunService();}
        if(args.size()==2&&args[0]==L"--install-service-all"){report=args[1];code=ns::InstallService(ns::CurrentSid());ns::WriteUtf8(report,L"{\"Success\":true}");}
        else if(args.size()==3&&args[0]==L"--install-service"){report=args[2];code=ns::InstallService(args[1]);ns::WriteUtf8(report,L"{\"Success\":true}");}
        else if(args.size()==2&&args[0]==L"--uninstall-service"){report=args[1];code=ns::UninstallService();ns::WriteUtf8(report,L"{\"Success\":true}");}
        else if(args.size()==2&&args[0]==L"--prepare-update"){report=args[1];code=ns::PrepareUpdate();ns::WriteUtf8(report,L"{\"Success\":true}");}
        else if(args.size()==2&&args[0]==L"--start-service"){report=args[1];code=ns::StartExistingService();ns::WriteUtf8(report,L"{\"Success\":true}");}
        else if(args.size()==2&&args[0]==L"--diagnose"){report=args[1];ns::WindowsNetwork network;ns::WriteUtf8(report,ns::AdaptersJson(network.Read()));}
        else if(args.size()==2&&args[0]==L"--self-test"){report=args[1];code=ns::RunSelfTests(report);}
        else if(args.size()==4&&args[0]==L"--test-service"){report=args[3];code=ns::RunHardwareTest(report,ns::ParseGuid(args[1]),ns::ParseGuid(args[2]));}
        else if(args.size()==2&&args[0]==L"--health"){ns::Request request;request.operation=1;auto result=ns::CallService(request);ns::WriteUtf8(args[1],L"{\"Success\":"+std::wstring(result.success?L"true":L"false")+L",\"Message\":"+ns::EscapeJson(result.message)+L",\"WorkingSetBytes\":"+std::to_wstring(result.workingSet)+L",\"PrivateBytes\":"+std::to_wstring(result.privateBytes)+L",\"Cpu100ns\":"+std::to_wstring(result.cpuTime)+L"}");code=result.success?0:1;}
        else if(args.empty()||(args.size()==1&&args[0]==L"--tray")||(args.size()==2&&args[0]==L"--ui-report"))code=ns::RunUi(instance,std::find(args.begin(),args.end(),L"--tray")!=args.end());
        else throw std::runtime_error("启动参数无效，请查阅 README。");
    }catch(const std::exception& ex){code=1;auto error=ns::Utf8ToWide(ex.what());
        if(!report.empty())try{ns::WriteUtf8(report,L"{\"Success\":false,\"Error\":"+ns::EscapeJson(error)+L"}");}catch(...){}
        else if(args.empty())MessageBoxW(nullptr,error.c_str(),L"网卡切换器",MB_OK|MB_ICONERROR);}
    CoUninitialize();return code;
}
