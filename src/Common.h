#pragma once
// ============================================================
// GamePreloader - 公共定义
// 游戏加载预处理工具：进程监控 / DX识别 / UE5 Pak解析 / 缓存 / 悬浮窗
// ============================================================

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

// ---------- 全局状态枚举 ----------
enum class RunState
{
    Idle,           // 未检测到游戏
    GameDetected,   // 检测到游戏进程
    Loading,        // 游戏处于加载阶段，正在预处理
    Preprocessed,   // 预处理完成，游戏运行中
    Error           // 出错
};

enum class DxVersion
{
    Unknown,
    DX11,
    DX12
};

// ---------- 运行时共享状态（线程安全）----------
struct AppState
{
    std::mutex          mtx;
    std::atomic<RunState>   state{ RunState::Idle };
    std::atomic<DxVersion>  dxVer{ DxVersion::Unknown };
    std::atomic<bool>       isUE5{ false };
    std::atomic<bool>       cacheHit{ false };
    std::atomic<uint64_t>   levelCacheBytes{ 0 }; // V3: 本局预读缓存文件大小（字节）
    std::atomic<int>        progress{ 0 };        // 0-100
    std::atomic<int>        targetFps{ 30 };      // 帧率基准
    std::atomic<DWORD>      gamePid{ 0 };
    std::wstring            gamePath;             // 受保护，读写需加锁
    std::wstring            statusText;           // 状态文字

    void SetStatus(const std::wstring& s)
    {
        std::lock_guard<std::mutex> lk(mtx);
        statusText = s;
    }
    std::wstring GetStatus()
    {
        std::lock_guard<std::mutex> lk(mtx);
        return statusText;
    }
    void SetGamePath(const std::wstring& s)
    {
        std::lock_guard<std::mutex> lk(mtx);
        gamePath = s;
    }
    std::wstring GetGamePath()
    {
        std::lock_guard<std::mutex> lk(mtx);
        return gamePath;
    }
};

// ---------- 全局单例状态 ----------
extern AppState g_state;

// ---------- 工具函数 ----------
namespace Util
{
    std::wstring Utf8ToWide(const std::string& s);
    std::string  WideToUtf8(const std::wstring& s);
    std::wstring GetExeDir();
    std::wstring GetCacheDir();
    std::wstring GetLogDir();
    std::wstring GetConfigDir();
    std::wstring GetConfigPath();
    bool         CreateDirRecursive(const std::wstring& path);
    bool         PathStartsWith(const std::wstring& path, const std::wstring& prefix);
    // V2: 判断进程路径是否匹配监控规则
    //  - watchRule 以 .exe 结尾 -> 精确匹配该 exe（"添加游戏"选中的主程序）
    //  - 否则 -> 目录边界前缀匹配（不会误匹配同级路径，如 "Delta Force" vs "Delta Force.exe"）
    bool         IsProcessInWatchPath(const std::wstring& processPath, const std::wstring& watchRule);
    std::wstring ToLower(const std::wstring& s);

    // V1.8: 检测本机是否有独立显卡（DXGI 枚举适配器）
    //  - NVIDIA / AMD 独显（独立显存>2GB）视为有独显
    //  - 仅 Intel 核显 / AMD APU 核显 视为无独显
    //  - 检测失败时保守返回 true（按有独显处理，避免误提高缓存上限）
    bool         HasDedicatedGpu();
}
