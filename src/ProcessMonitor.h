#pragma once
// 进程监控：后台线程轮询，发现监控目录下的 exe 启动/退出
#include "Common.h"
#include <functional>
#include <thread>
#include <atomic>

struct GameProcessInfo
{
    DWORD       pid = 0;
    std::wstring exeName;
    std::wstring fullPath;
};

class ProcessMonitor
{
public:
    using Callback = std::function<void(const GameProcessInfo&)>;

    ProcessMonitor();
    ~ProcessMonitor();

    // 设置监控目录列表
    void SetWatchPaths(const std::vector<std::wstring>& paths);
    // V2: 运行时更新监控路径（添加游戏后调用，线程安全）
    void UpdateWatchPaths(const std::vector<std::wstring>& paths);

    // 注册游戏启动/退出回调
    void OnGameStarted(Callback cb)  { m_onStart = std::move(cb); }
    void OnGameExited(Callback cb)   { m_onExit  = std::move(cb); }

    void Start();
    void Stop();

    // 当前是否有游戏在运行
    bool IsGameRunning() const { return m_gameRunning.load(); }
    GameProcessInfo GetCurrentGame() const;

private:
    void WorkerLoop();
    std::vector<GameProcessInfo> EnumMatchingProcesses() const;

    std::vector<std::wstring> m_watchPaths;
    Callback m_onStart;
    Callback m_onExit;

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_gameRunning{ false };

    mutable std::mutex m_mtx;
    GameProcessInfo    m_current;
};
