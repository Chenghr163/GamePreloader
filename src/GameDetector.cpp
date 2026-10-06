#include "GameDetector.h"
#include <psapi.h>   // K32* 声明，实现在 kernel32.dll（非 psapi.dll）
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

DxVersion GameDetector::DetectDxVersion(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return DxVersion::Unknown;

    HMODULE mods[1024];
    DWORD needed = 0;
    DxVersion result = DxVersion::Unknown;

    // V2: K32EnumProcessModules — kernel32 原生实现，psapi 版本在 Win11 已弃用/合并
    if (K32EnumProcessModules(hProc, mods, sizeof(mods), &needed))
    {
        int count = needed / sizeof(HMODULE);
        for (int i = 0; i < count; ++i)
        {
            wchar_t name[MAX_PATH] = { 0 };
            if (K32GetModuleBaseNameW(hProc, mods[i], name, MAX_PATH))
            {
                std::wstring n = Util::ToLower(name);
                if (n == L"d3d12.dll") { result = DxVersion::DX12; break; }
                if (n == L"d3d11.dll") { result = DxVersion::DX11; /* 不 break，优先 DX12 */ }
            }
        }
    }
    CloseHandle(hProc);
    return result;
}

bool GameDetector::DetectUE5(const GameProcessInfo& info)
{
    std::wstring root = FindGameRoot(info.fullPath);
    if (root.empty()) return false;

    // V2: 定向定位 UE5 标准 Pak 目录（非递归），避免暴力遍历整个游戏根目录导致数秒延迟
    std::vector<std::wstring> paksDirs;
    try {
        // 一级子目录的 <Game>/Content/Paks
        for (auto& entry : fs::directory_iterator(root))
        {
            if (entry.is_directory())
            {
                std::wstring paks = entry.path().wstring() + L"\\Content\\Paks";
                if (fs::exists(paks)) paksDirs.push_back(paks);
            }
        }
    } catch (...) {}
    // 直接根下的 Content/Paks
    {
        std::wstring paks = root + L"\\Content\\Paks";
        if (fs::exists(paks)) paksDirs.push_back(paks);
    }

    // V1.8: 同时识别传统 .pak 与 IoStore（.utoc/.ucas）——黑神话悟空等现代 UE5 主用 IoStore
    for (auto& paksDir : paksDirs)
    {
        try {
            for (auto& entry : fs::directory_iterator(paksDir))
            {
                if (entry.is_regular_file())
                {
                    std::wstring ext = Util::ToLower(entry.path().extension().wstring());
                    if (ext == L".pak" || ext == L".utoc" || ext == L".ucas")
                        return true;
                }
                else if (entry.is_directory())
                {
                    // 再看一层子目录（pakchunk 分块目录）
                    try {
                        for (auto& sub : fs::directory_iterator(entry.path()))
                        {
                            if (sub.is_regular_file())
                            {
                                std::wstring ext = Util::ToLower(sub.path().extension().wstring());
                                if (ext == L".pak" || ext == L".utoc" || ext == L".ucas")
                                    return true;
                            }
                        }
                    } catch (...) {}
                }
            }
        } catch (...) {}
    }

    // 特征2：存在 Engine/Binaries 或特定 UE dll
    std::wstring engineDll = root + L"\\Engine\\Binaries\\ThirdParty\\NVIDIA\\NVaftermath\\Win64\\GFSDK_Aftermath_Lib.x64.dll";
    if (fs::exists(engineDll)) return true;

    // 特征3：exe 同目录下有 UE 特征 dll
    std::wstring exeDir = info.fullPath.substr(0, info.fullPath.find_last_of(L"\\/"));
    try {
        for (auto& entry : fs::directory_iterator(exeDir))
        {
            if (!entry.is_regular_file()) continue;
            std::wstring fn = Util::ToLower(entry.path().filename().wstring());
            if (fn.find(L"unreal") != std::wstring::npos ||
                fn == L"d3d12core.dll" /* UE5 常带 */)
                return true;
        }
    } catch (...) {}

    return false;
}

std::wstring GameDetector::FindUE5ShaderCacheDir(const std::wstring& gameRoot)
{
    // UE5 典型 ShaderCache 位置：
    // <GameRoot>/<GameName>/Saved/
    // 或 <GameRoot>/Engine/DerivedDataCache/
    std::vector<std::wstring> candidates = {
        gameRoot + L"\\Engine\\DerivedDataCache",
    };
    // 遍历一级子目录找 Saved
    try {
        for (auto& entry : fs::directory_iterator(gameRoot))
        {
            if (entry.is_directory())
            {
                std::wstring saved = entry.path().wstring() + L"\\Saved";
                if (fs::exists(saved)) candidates.push_back(saved);
                std::wstring ddcache = entry.path().wstring() + L"\\Saved\\DerivedDataCache";
                if (fs::exists(ddcache)) candidates.push_back(ddcache);
            }
        }
    } catch (...) {}

    for (auto& c : candidates)
        if (fs::exists(c)) return c;
    return L"";
}

std::wstring GameDetector::FindGameRoot(const std::wstring& exePath)
{
    fs::path p(exePath);
    // V2: 从 exe 所在目录向上最多找 5 层，每层只检测是否存在 <dir>/Content/Paks 目录
    // （非递归遍历，避免暴力扫描整个游戏根导致数秒延迟）
    auto cur = p.parent_path();
    for (int i = 0; i < 5; ++i)
    {
        if (!fs::exists(cur)) break;
        std::wstring paks = cur.wstring() + L"\\Content\\Paks";
        if (fs::exists(paks)) return cur.wstring();
        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return p.parent_path().wstring();
}

DWORD GameDetector::GetProcessStartTimeMs(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return 0;
    FILETIME ct, ex, kt, ut;
    DWORD result = 0;
    if (GetProcessTimes(hProc, &ct, &ex, &kt, &ut))
    {
        // 进程启动时间（100ns 间隔，自 1601-01-01）
        ULARGE_INTEGER startLi;
        startLi.LowPart = ct.dwLowDateTime;
        startLi.HighPart = ct.dwHighDateTime;

        // 当前系统时间
        FILETIME nowFt;
        GetSystemTimeAsFileTime(&nowFt);
        ULARGE_INTEGER nowLi;
        nowLi.LowPart = nowFt.dwLowDateTime;
        nowLi.HighPart = nowFt.dwHighDateTime;

        // 差值换算为毫秒
        result = (DWORD)((nowLi.QuadPart - startLi.QuadPart) / 10000);
    }
    CloseHandle(hProc);
    return result;
}

bool GameDetector::IsInLoadingPhase(DWORD pid, DWORD startTimeMs)
{
    (void)startTimeMs;
    // 规则1：启动后 90 秒内一律视为加载阶段（UE5 游戏首启通常需要较长加载）
    DWORD elapsed = GetProcessStartTimeMs(pid);
    if (elapsed < 90000) return true;

    // 规则2：检查是否有顶层可见主窗口（有窗口通常意味着已进入可交互阶段）
    struct WndEnumData { DWORD pid; bool hasVisibleTopWnd; };
    WndEnumData data{ pid, false };
    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        auto* d = (WndEnumData*)lParam;
        DWORD wpid = 0;
        GetWindowThreadProcessId(hWnd, &wpid);
        if (wpid == d->pid && IsWindowVisible(hWnd) && GetWindow(hWnd, GW_OWNER) == nullptr)
        {
            // 排除极小的工具窗口
            RECT rc; GetWindowRect(hWnd, &rc);
            if (rc.right - rc.left > 200 && rc.bottom - rc.top > 200)
            {
                d->hasVisibleTopWnd = true;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&data);

    // 有大窗口且启动超过90秒，认为加载结束
    if (data.hasVisibleTopWnd && elapsed > 90000) return false;

    // 规则3：IO 读取速率高则仍在加载
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProc)
    {
        IO_COUNTERS c1{}, c2{};
        if (GetProcessIoCounters(hProc, &c1))
        {
            Sleep(500);
            if (GetProcessIoCounters(hProc, &c2))
            {
                ULONGLONG readDelta = c2.ReadTransferCount - c1.ReadTransferCount;
                // 500ms 内读取超过 2MB 视为仍在加载
                if (readDelta > 2 * 1024 * 1024) { CloseHandle(hProc); return true; }
            }
        }
        CloseHandle(hProc);
    }
    return false;
}
