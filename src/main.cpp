// ============================================================
// GamePreloader - 主入口
// 游戏加载预处理工具：开机自启 / 悬浮窗 / 进程监控 / DX识别 / UE5 Pak解析 / 缓存
// ============================================================
#include "Common.h"
#include "Config.h"
#include "ProcessMonitor.h"
#include "GameDetector.h"
#include "Preprocessor.h"
#include "FloatingWindow.h"

#include <windows.h>
#include <thread>
#include <atomic>
#include <filesystem>
#include <algorithm>
#include <powrprof.h>

#pragma comment(lib, "powrprof.lib")

namespace fs = std::filesystem;

// ---------- 全局对象 ----------
static ProcessMonitor   g_monitor;
static FloatingWindow   g_floatWnd;
static std::atomic<bool> g_preprocessing{ false };

// V1.0: 系统级游戏加速（仅监测到游戏进程后生效）
static GUID g_origScheme{};
static bool g_schemeSaved = false;

// V3Pro: 帧率扩容节流
static ULONGLONG g_lastExpandMs = 0;                    // 上次扩容时间
static constexpr ULONGLONG V3P_EXPAND_COOLDOWN_MS = 60000; // 扩容最小间隔 60s
// V1.8: 扩容上限运行时决定——检测到独显为 20GB；无独显（核显/APU）拉高到 40GB
static uint64_t GetCacheMaxBytes()
{
    return Util::HasDedicatedGpu()
        ? (20ULL * 1024 * 1024 * 1024)
        : (40ULL * 1024 * 1024 * 1024);
}

// V1.5: 剧情阶段保护——游戏启动后 90 秒内（开场过场/主菜单）不触发扩容，
// 避免剧情阶段 CPU/GPU 高负载误判为"帧率不达标"而抢 IO 重建缓存导致剧情更卡。
static ULONGLONG g_gameStartMs = 0;
static constexpr ULONGLONG V15_CINEMATIC_GUARD_MS = 90000;

// ---------- 开机自启 ----------
static void SetAutoStart(bool enable)
{
    HKEY hKey = nullptr;
    const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE | KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    const wchar_t* appName = L"GamePreloader";
    if (enable)
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring val = std::wstring(L"\"") + exePath + L"\"";
        RegSetValueExW(hKey, appName, 0, REG_SZ, (const BYTE*)val.c_str(),
            (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(hKey, appName);
    }
    RegCloseKey(hKey);
}

// ---------- 预处理后台线程 ----------
static void RunPreprocess(const std::wstring& exePath)
{
    if (g_preprocessing.exchange(true)) return;

    g_state.state.store(RunState::Loading);
    g_state.progress.store(0);
    g_state.SetStatus(L"正在预处理...");

    int targetFps = g_state.targetFps.load();
    PreprocessResult result = Preprocessor::Run(exePath, targetFps, g_state.progress);

    // V1.6: cacheHit 语义改为"本局预读缓存已生效"（levelPreloadDone），
    // 而非旧的"Pak统计清单缓存命中"——用户关心的是5GB预读缓存是否起作用。
    g_state.cacheHit.store(result.levelPreloadDone);
    g_state.levelCacheBytes.store(result.levelCacheBytes);   // V3: 本局缓存大小
    g_state.state.store(RunState::Preprocessed);

    wchar_t buf[512];
    if (result.levelPreloadDone)
    {
        double gb = (double)result.levelCacheBytes / (1024.0 * 1024.0 * 1024.0);
        swprintf_s(buf,
            L"预处理完成: %d本局关卡, %d动作资源, 本局缓存%.2fGB, 耗时%llums%s",
            result.levelCount, result.actionResourceCount, gb,
            (unsigned long long)result.elapsedMs,
            result.cacheHit ? L" (统计缓存命中)" : L"");
    }
    else
    {
        swprintf_s(buf, L"预处理完成: %d个Pak, %d资源, 耗时%llums%s",
            result.pakCount, result.totalResources,
            (unsigned long long)result.elapsedMs,
            result.cacheHit ? L" (缓存命中)" : L"");
    }
    g_state.SetStatus(buf);

    g_preprocessing.store(false);
    g_floatWnd.Refresh();
}

// ---------- V3Pro: 帧率优先——扩容缓存直到帧率达标 ----------
static void DoExpandCache(const std::wstring& exePath)
{
    if (g_preprocessing.exchange(true)) return;
    // V1.5: 扩容线程降为最低优先级，避免大量写 IO 抢占游戏剧情/玩法的 CPU 与磁盘
    int oldPri = GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

    uint64_t cur = g_state.levelCacheBytes.load();
    if (cur == 0) cur = 5ULL * 1024 * 1024 * 1024; // 未扩容过，从 5GB 起
    uint64_t newTarget = std::min(cur * 2, GetCacheMaxBytes());   // 逐级翻倍扩容
    if (newTarget <= cur) {
        SetThreadPriority(GetCurrentThread(), oldPri);
        g_preprocessing.store(false);
        return;
    }

    g_state.progress.store(0);
    g_state.state.store(RunState::Loading);
    wchar_t buf[128];
    swprintf_s(buf, L"帧率未达标·扩容缓存至 %.1fGB...", (double)newTarget / (1024.0 * 1024.0 * 1024.0));
    g_state.SetStatus(buf);
    g_floatWnd.Refresh();

    std::wstring root = GameDetector::FindGameRoot(exePath);
    PreprocessResult r;
    Preprocessor::ExpandLevelCache(root, newTarget, g_state.progress, r);

    SetThreadPriority(GetCurrentThread(), oldPri); // 恢复线程优先级

    g_state.levelCacheBytes.store(r.levelCacheBytes);
    g_state.state.store(RunState::Preprocessed);
    g_state.progress.store(100);          // V1.6: 扩容完成后进度到100%，修复卡在95%
    g_state.cacheHit.store(true);          // V1.6: 扩容后本局预读缓存已生效，显示命中
    swprintf_s(buf, L"缓存已扩容至 %.2fGB（帧率优化）", (double)r.levelCacheBytes / (1024.0 * 1024.0 * 1024.0));
    g_state.SetStatus(buf);

    g_lastExpandMs = GetTickCount64();
    g_preprocessing.store(false);
    g_floatWnd.Refresh();
}

// ---------- V3Pro: 帧率监测线程（仅 CPU 高负载→扩容，不触发场景重处理）----------
static void FrameMonitorThread(DWORD pid, const std::wstring& exePath)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    ULARGE_INTEGER lastKernel{}, lastUser{}, lastNow{};
    bool hasCpu = false;
    int highCpuCount = 0;

    while (true)
    {
        if (hProc)
        {
            DWORD code = 0;
            if (!GetExitCodeProcess(hProc, &code) || code != STILL_ACTIVE)
            { CloseHandle(hProc); hProc = nullptr; break; }
        }
        else break;
        Sleep(2000);

        ULONGLONG now = GetTickCount64();
        if (!hProc) continue;
        FILETIME ct, ex, kt, ut, nowFt;
        if (GetProcessTimes(hProc, &ct, &ex, &kt, &ut))
        {
            GetSystemTimeAsFileTime(&nowFt);
            ULARGE_INTEGER k, u, n;
            k.LowPart = kt.dwLowDateTime; k.HighPart = kt.dwHighDateTime;
            u.LowPart = ut.dwLowDateTime; u.HighPart = ut.dwHighDateTime;
            n.LowPart = nowFt.dwLowDateTime; n.HighPart = nowFt.dwHighDateTime;
            if (hasCpu)
            {
                ULONGLONG cpuDelta = (k.QuadPart - lastKernel.QuadPart) +
                                     (u.QuadPart - lastUser.QuadPart);
                ULONGLONG wallDelta = n.QuadPart - lastNow.QuadPart;
                double pct = (wallDelta > 0) ? (double)cpuDelta * 100.0 / (double)wallDelta : 0.0;
                if (pct > 85.0) highCpuCount++;
                else highCpuCount = 0;
                // 持续高负载判定性能未达标 → 扩容缓存
                // V1.5: 剧情阶段保护——游戏启动 90 秒内（开场过场/主菜单）不扩容，
                // 避免剧情阶段 CPU/GPU 高负载误判而抢 IO 重建缓存导致剧情更卡。
                if (highCpuCount >= 3 &&
                    (now - g_lastExpandMs) > V3P_EXPAND_COOLDOWN_MS &&
                    (now - g_gameStartMs) > V15_CINEMATIC_GUARD_MS)
                {
                    DoExpandCache(exePath);
                    highCpuCount = 0;
                }
            }
            lastKernel = k; lastUser = u; lastNow = n; hasCpu = true;
        }
    }
}

// ---------- V1.0: 系统级游戏加速（仅监测到游戏进程后执行）----------
// 原理：核显/低配电脑流畅度瓶颈在 CPU 调度与功耗策略。
//  1) 提升游戏进程优先级（ABOVE_NORMAL）→ CPU 更优先分配计算资源，减少卡顿
//  2) 切换到"高性能"电源计划 → 释放核显/CPU 频率与功耗上限（真实提升核显帧率）
// 均为可逆安全操作：游戏退出时自动恢复原电源计划。
static void ApplySystemBoost(DWORD pid)
{
    // 1. 提升游戏进程优先级
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (h)
    {
        SetPriorityClass(h, ABOVE_NORMAL_PRIORITY_CLASS);
        CloseHandle(h);
    }
    // 2. 切换到高性能电源计划（记录原计划，退出时恢复）
    if (!g_schemeSaved)
    {
        GUID hp = { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c } };
        GUID* pOrig = nullptr;
        if (PowerGetActiveScheme(nullptr, &pOrig) == ERROR_SUCCESS && pOrig)
        {
            g_origScheme = *pOrig;
            LocalFree(pOrig);
            if (PowerSetActiveScheme(nullptr, &hp) == ERROR_SUCCESS)
                g_schemeSaved = true;
        }
    }
}

static void RestoreSystemBoost()
{
    if (g_schemeSaved)
    {
        PowerSetActiveScheme(nullptr, &g_origScheme);
        g_schemeSaved = false;
    }
}

// ---------- 游戏启动回调 ----------
static void OnGameStarted(const GameProcessInfo& info)
{
    g_state.gamePid.store(info.pid);
    g_state.SetGamePath(info.fullPath);
    g_state.state.store(RunState::GameDetected);
    g_state.SetStatus(L"检测到游戏: " + info.exeName);

    // 检测 DX 版本
    DxVersion dx = GameDetector::DetectDxVersion(info.pid);
    g_state.dxVer.store(dx);

    // 检测 UE5
    bool ue5 = GameDetector::DetectUE5(info);
    g_state.isUE5.store(ue5);

    g_floatWnd.Refresh();

    // V1.0: 系统级游戏加速（仅监测到游戏进程后执行）
    ApplySystemBoost(info.pid);
    g_state.SetStatus(L"检测到游戏: " + info.exeName + L"（已启用系统加速）");

    // V3Pro: 启动帧率监测线程（CPU 高负载→扩容缓存；不做场景切换重处理）
    g_lastExpandMs = 0;
    g_gameStartMs = GetTickCount64(); // V1.5: 记录游戏启动时间，用于剧情阶段保护
    std::thread(FrameMonitorThread, info.pid, info.fullPath).detach();

    // 等待进入加载阶段后触发预处理
    // 简单策略：检测到进程后延迟 3 秒（让游戏初始化模块加载完毕），然后启动预处理
    std::thread([exePath = info.fullPath, pid = info.pid]() {
        Sleep(3000);
        // 再次确认进程还在
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return;
        CloseHandle(h);

        // 判断是否在加载阶段
        if (GameDetector::IsInLoadingPhase(pid, 0))
        {
            RunPreprocess(exePath);
        }
        else
        {
            // 即使不在加载阶段也执行预处理（资源预解析对后续运行也有帮助）
            RunPreprocess(exePath);
        }
    }).detach();
}

// ---------- 游戏退出回调 ----------
static void OnGameExited(const GameProcessInfo& info)
{
    (void)info;
    g_state.state.store(RunState::Idle);
    g_state.gamePid.store(0);
    g_state.progress.store(0);
    g_state.cacheHit.store(false);
    g_state.dxVer.store(DxVersion::Unknown);
    g_state.isUE5.store(false);
    g_state.SetStatus(L"等待游戏启动");
    RestoreSystemBoost();   // V1.0: 恢复原电源计划
    g_floatWnd.Refresh();
}

// ---------- 添加游戏回调（V2：文件对话框选择后触发）----------
static void OnAddGame(const std::wstring& exePath)
{
    auto& cfg = Config::Instance();
    cfg.AddWatchPath(exePath);          // 精确记录选中的 exe 路径
    std::wstring cfgPath = Util::GetConfigPath();
    cfg.Save(cfgPath);
    g_monitor.UpdateWatchPaths(cfg.GetWatchPaths());
    g_state.SetStatus(L"已添加: " + exePath);
    g_floatWnd.Refresh();
}

// ---------- 定时刷新悬浮窗 ----------
static void CALLBACK RefreshTimer(HWND, UINT, UINT_PTR, DWORD)
{
    g_floatWnd.Refresh();
}

// ---------- WinMain ----------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    // 防止多开
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GamePreloader_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"GamePreloader 已在运行中。", L"提示", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 1. 创建运行时目录结构（增加稳定性，V2: cache/logs/config 子目录）
    // V1.8: park 文件夹专门放置各游戏引擎解析库（当前为 UE5PakParser.dll）。
    // 解析库采用 delay-load，首次调用 PakParser 时才加载，此时已可从 park 找到。
    {
        std::wstring parkDir = Util::GetExeDir() + L"\\park";
        Util::CreateDirRecursive(parkDir);
        SetDllDirectoryW(parkDir.c_str());
        // 双保险：按全路径显式预加载解析库。加载成功后，后续 delay-load 首次调用
        // PakParser 时会直接复用已加载模块，不再依赖 DLL 搜索路径顺序，兼容性最佳。
        HMODULE hParser = LoadLibraryW((parkDir + L"\\UE5PakParser.dll").c_str());
        (void)hParser; // 即使此处失败也不中断，delay-load 仍会在首次调用时按搜索路径加载
    }

    // 1. 创建运行时目录结构（增加稳定性，V2: cache/logs/config 子目录）
    Util::CreateDirRecursive(Util::GetExeDir() + L"\\cache");
    Util::CreateDirRecursive(Util::GetExeDir() + L"\\logs");
    Util::CreateDirRecursive(Util::GetExeDir() + L"\\config");

    // 加载配置（V2: config/config.ini），自动迁移旧版根目录 config.ini
    std::wstring cfgPath = Util::GetConfigPath();
    if (!fs::exists(cfgPath))
    {
        std::wstring oldCfg = Util::GetExeDir() + L"\\config.ini";
        if (fs::exists(oldCfg)) CopyFileW(oldCfg.c_str(), cfgPath.c_str(), FALSE);
    }

    auto& cfg = Config::Instance();
    bool cfgExists = cfg.Load(cfgPath);
    if (!cfgExists)
    {
        // 首次运行：写入默认配置
        cfg.SetWatchPaths({ L"D:\\SteamLibrary\\Delta Force" });
        cfg.SetTargetFps(30);
        cfg.SetAutoStart(false);
        cfg.SetAlwaysOnTop(true);
        cfg.SetWindowPos(POINT{ 20, 20 });
        cfg.Save(cfgPath);
    }

    // 应用配置
    g_state.targetFps.store(cfg.GetTargetFps());
    if (cfg.GetAutoStart()) SetAutoStart(true);

    // 2. 创建悬浮窗
    if (!g_floatWnd.Create(hInst))
    {
        MessageBoxW(nullptr, L"悬浮窗创建失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    g_floatWnd.SetAlwaysOnTop(cfg.GetAlwaysOnTop());
    g_floatWnd.SetAddGameCallback(OnAddGame);   // V2: 添加游戏回调
    g_floatWnd.Show();

    // 3. 启动进程监控
    g_monitor.SetWatchPaths(cfg.GetWatchPaths());
    g_monitor.OnGameStarted(OnGameStarted);
    g_monitor.OnGameExited(OnGameExited);
    g_monitor.Start();

    g_state.SetStatus(L"等待游戏启动");

    // 4. 定时刷新 UI（500ms）
    SetTimer(nullptr, 1, 500, RefreshTimer);

    // 5. 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 6. 清理
    KillTimer(nullptr, 1);
    g_monitor.Stop();
    cfg.Save(cfgPath);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
