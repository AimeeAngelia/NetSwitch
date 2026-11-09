// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 AimeeAngelia

#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <wbemidl.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstddef>

namespace ns {
using Microsoft::WRL::ComPtr;
inline constexpr wchar_t ServiceName[] = L"LocalNetSwitch";
inline constexpr wchar_t PipeName[] = L"\\\\.\\pipe\\LocalNetSwitch.v2";
inline constexpr wchar_t ConfigKey[] = L"SOFTWARE\\LocalNetSwitch";
inline constexpr unsigned Magic = 0x3257534E;
inline constexpr UINT WM_REFRESH = WM_APP + 1, WM_READ_DONE = WM_APP + 2,
    WM_SWITCH_DONE = WM_APP + 3, WM_TRAY = WM_APP + 4, WM_INSTALL_DONE = WM_APP + 5;

struct Adapter {
    GUID id{};
    std::wstring name, description, ipv4;
    ULONG index = 0;
    bool hardware = false, enabled = false, connected = false;
    std::wstring state() const { return !enabled ? L"已禁用" : connected ? L"已连接" : L"未连接"; }
};
inline bool Same(const GUID& a, const GUID& b) { return !!IsEqualGUID(a,b); }
std::wstring GuidText(const GUID& id);
GUID ParseGuid(const std::wstring& text);
std::wstring ExePath();
std::wstring ExeFolder();
std::wstring ErrorText(DWORD code);
std::wstring Utf8ToWide(const std::string& text);
std::string Utf8(const std::wstring& text);
void WriteUtf8(const std::wstring& path, const std::wstring& text);
std::wstring EscapeJson(const std::wstring& text);
std::wstring AdaptersJson(const std::vector<Adapter>& adapters);
std::wstring CurrentSid();
void Check(HRESULT hr, const wchar_t* operation);

class Network {
public:
    virtual ~Network() = default;
    virtual std::vector<Adapter> Read() = 0;
    virtual void Set(const GUID& id, bool enabled) = 0;
};
class WindowsNetwork final : public Network {
    ComPtr<IWbemServices> service;
    void Connect();
public:
    std::vector<Adapter> Read() override;
    void Set(const GUID& id, bool enabled) override;
};
struct Request {
    unsigned magic = Magic, version = 2, operation = 2, physical = 1, count = 0;
    GUID target{}, scope[32]{};
};
struct Result {
    unsigned magic = Magic, success = 0, rollbackFailed = 0;
    unsigned reserved = 0;
    unsigned long long workingSet = 0, privateBytes = 0, cpuTime = 0;
    wchar_t message[2048]{};
};
static_assert(sizeof(Request) == 548);
static_assert(sizeof(Result) == 4136);
static_assert(offsetof(Result, workingSet) == 16);
Result Apply(Network& backend, const Request& request);
Result CallService(const Request& request);
bool ServiceReady();
int InstallService(const std::wstring& sid);
int UninstallService();
int PrepareUpdate();
int StartExistingService();
int RunService();
bool StartupEnabled();
void SetStartup(bool enabled);
bool ReadPhysicalMode();
void WritePhysicalMode(bool physical);
std::wstring ReadSetting(HKEY hive, const wchar_t* key, const wchar_t* name);
int RunSelfTests(const std::wstring& output);
int RunHardwareTest(const std::wstring& output, const GUID& targetId, const GUID& temporaryId);
int RunUi(HINSTANCE instance, bool trayOnly);
}
