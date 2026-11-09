// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 AimeeAngelia

#include "core.h"

namespace ns {
static GUID A= {1},B={2},V={3};
static void Assert(bool value){if(!value)throw std::runtime_error("Assertion failed");}
struct Fake:Network{
    std::vector<Adapter> items;std::vector<std::pair<GUID,bool>> calls;std::vector<int> failBefore,failAfter;int reads=0;bool corruptFinal=false;
    Fake(){Adapter a;a.id=A;a.name=L"Ethernet";a.hardware=true;a.enabled=a.connected=true;items.push_back(a);a.id=B;a.name=L"Wi-Fi";a.enabled=a.connected=false;items.push_back(a);a.id=V;a.name=L"VMware";a.hardware=false;a.enabled=a.connected=true;items.push_back(a);}
    bool Enabled(GUID id){return std::find_if(items.begin(),items.end(),[&](auto&a){return Same(a.id,id);})->enabled;}
    std::vector<Adapter> Read()override{if(++reads==2&&corruptFinal)for(auto&a:items)if(Same(a.id,B))a.enabled=false;return items;}
    void Set(const GUID& id,bool enabled)override{calls.push_back({id,enabled});int n=static_cast<int>(calls.size());if(std::find(failBefore.begin(),failBefore.end(),n)!=failBefore.end())throw std::runtime_error("before mutation failure");
        for(auto&a:items)if(Same(a.id,id))a.enabled=enabled;if(std::find(failAfter.begin(),failAfter.end(),n)!=failAfter.end())throw std::runtime_error("after mutation failure");}
};
static Request Plan(GUID target,std::initializer_list<GUID> scope,bool physical=true){Request r;r.target=target;r.physical=physical;r.count=static_cast<unsigned>(scope.size());unsigned i=0;for(auto&id:scope)r.scope[i++]=id;return r;}
int RunSelfTests(const std::wstring& output){
    std::wstring json=L"{\"SimulatedOnly\":true,\"Tests\":[";int total=0,failed=0;
    auto test=[&](const wchar_t* name,std::function<void()> run){bool success=true;std::wstring error;try{run();}catch(const std::exception& ex){success=false;failed++;error=Utf8ToWide(ex.what());}if(total++)json+=L",";json+=L"\n{\"Name\":"+EscapeJson(name)+L",\"Passed\":"+(success?L"true":L"false")+L",\"Error\":"+EscapeJson(error)+L"}";};
    test(L"physical_scope_preserves_virtual",[]{Fake f;auto r=Apply(f,Plan(B,{A,B}));Assert(r.success&&!f.Enabled(A)&&f.Enabled(B)&&f.Enabled(V));Assert(f.calls.size()==2&&Same(f.calls[0].first,B)&&f.calls[0].second);});
    test(L"all_scope_exclusive",[]{Fake f;auto r=Apply(f,Plan(V,{A,B,V},false));Assert(r.success&&!f.Enabled(A)&&!f.Enabled(B)&&f.Enabled(V));});
    test(L"disconnected_target_allowed",[]{Fake f;auto r=Apply(f,Plan(B,{A,B}));Assert(r.success&&f.Enabled(B)&&!f.Enabled(A));});
    test(L"physical_scope_rejects_virtual",[]{Fake f;auto r=Apply(f,Plan(V,{A,B,V}));Assert(!r.success&&f.calls.empty());});
    test(L"target_outside_scope_rejected",[]{Fake f;auto r=Apply(f,Plan(V,{A,B}));Assert(!r.success&&f.calls.empty());});
    test(L"missing_adapter_rejected",[]{Fake f;GUID unknown={99};auto r=Apply(f,Plan(A,{A,unknown}));Assert(!r.success&&f.calls.empty());});
    test(L"empty_and_duplicate_scope_rejected",[]{Fake f;Assert(!Apply(f,Plan(A,{})).success&&!Apply(f,Plan(A,{A,A})).success&&f.calls.empty());});
    test(L"enable_failure_preserves_old",[]{Fake f;f.failBefore={1};auto r=Apply(f,Plan(B,{A,B}));Assert(!r.success&&f.Enabled(A)&&!f.Enabled(B)&&!r.rollbackFailed);});
    test(L"partial_enable_restored",[]{Fake f;f.failAfter={1};auto r=Apply(f,Plan(B,{A,B}));Assert(!r.success&&f.Enabled(A)&&!f.Enabled(B));});
    test(L"disable_failure_restore_order",[]{Fake f;f.failAfter={2};auto r=Apply(f,Plan(B,{A,B}));Assert(!r.success&&f.Enabled(A)&&!f.Enabled(B)&&f.calls.size()==4);Assert(Same(f.calls[2].first,A)&&f.calls[2].second&&!f.calls[3].second);});
    test(L"rollback_failure_reported",[]{Fake f;f.failAfter={2};f.failBefore={3};auto r=Apply(f,Plan(B,{A,B}));Assert(!r.success&&r.rollbackFailed);});
    test(L"already_selected_no_mutation",[]{Fake f;auto r=Apply(f,Plan(A,{A,B}));Assert(r.success&&f.calls.empty());});
    test(L"final_verification_restores",[]{Fake f;f.corruptFinal=true;auto r=Apply(f,Plan(B,{A,B}));Assert(!r.success&&f.Enabled(A)&&!f.Enabled(B));});
    test(L"invalid_protocol_rejected",[]{Fake f;auto request=Plan(A,{A,B});request.version=99;Assert(!Apply(f,request).success&&f.calls.empty());});
    test(L"oversized_scope_rejected",[]{Fake f;auto request=Plan(A,{A,B});request.count=33;Assert(!Apply(f,request).success&&f.calls.empty());});
    test(L"ipc_reply_has_no_uninitialized_payload_bytes",[]{Result reply;auto bytes=reinterpret_cast<const unsigned char*>(&reply);for(size_t i=sizeof(reply.magic);i<sizeof(reply);i++)Assert(bytes[i]==0);});
    json+=L"\n],\"Total\":"+std::to_wstring(total)+L",\"Failed\":"+std::to_wstring(failed)+L"}";WriteUtf8(output,json);return failed?1:0;
}
int RunHardwareTest(const std::wstring& output,const GUID& ethernet,const GUID& wifi){
    WindowsNetwork backend;auto before=backend.Read();
    auto e=std::find_if(before.begin(),before.end(),[&](auto&a){return Same(a.id,ethernet);}),w=std::find_if(before.begin(),before.end(),[&](auto&a){return Same(a.id,wifi);});
    bool began=false,enabled=false,switched=false,restored=false;std::wstring error,restoreError;std::vector<Adapter> during,after;
    try{
        if(Same(ethernet,wifi)||e==before.end()||w==before.end()||!e->hardware||!w->hardware||!e->enabled||!e->connected||w->connected)throw std::runtime_error("当前网卡状态不符合测试条件，未改动网卡。");
        began=true;auto enable=CallService(Plan(wifi,{wifi}));if(!enable.success)throw std::runtime_error(Utf8(enable.message));
        auto enabledList=backend.Read();auto enabledWifi=std::find_if(enabledList.begin(),enabledList.end(),[&](auto&a){return Same(a.id,wifi);});enabled=enabledWifi!=enabledList.end()&&enabledWifi->enabled;Assert(enabled);
        auto change=CallService(Plan(ethernet,{ethernet,wifi}));if(!change.success)throw std::runtime_error(Utf8(change.message));during=backend.Read();
        for(auto&a:before){auto current=std::find_if(during.begin(),during.end(),[&](auto&b){return Same(a.id,b.id);});Assert(current!=during.end()&&current->enabled==(Same(a.id,wifi)?false:a.enabled));if(Same(a.id,ethernet))Assert(current->connected);}
        switched=true;
    }catch(const std::exception& ex){error=Utf8ToWide(ex.what());}
    if(began)try{
        auto restore=CallService(w->enabled?Plan(wifi,{wifi}):Plan(ethernet,{ethernet,wifi}));if(!restore.success)throw std::runtime_error(Utf8(restore.message));
        after=backend.Read();for(auto&a:before){auto current=std::find_if(after.begin(),after.end(),[&](auto&b){return Same(a.id,b.id);});Assert(current!=after.end()&&current->enabled==a.enabled);}restored=true;
    }catch(const std::exception& ex){restoreError=Utf8ToWide(ex.what());}
    bool success=enabled&&switched&&restored;
    WriteUtf8(output,L"{\"Success\":"+std::wstring(success?L"true":L"false")+L",\"EnableViaServiceVerified\":"+(enabled?L"true":L"false")+L",\"SwitchViaServiceVerified\":"+(switched?L"true":L"false")+L",\"RestoreVerified\":"+(restored?L"true":L"false")+L",\"Error\":"+EscapeJson(error)+L",\"RestoreError\":"+EscapeJson(restoreError)+L",\"Before\":"+AdaptersJson(before)+L",\"During\":"+AdaptersJson(during)+L",\"After\":"+AdaptersJson(after)+L"}");
    return success?0:1;
}
}
