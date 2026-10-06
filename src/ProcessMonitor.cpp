#include "ProcessMonitor.h"
#include <tlhelp32.h>
#include <algorithm>

ProcessMonitor::ProcessMonitor() = default;

ProcessMonitor::~ProcessMonitor()
{
    Stop();
}

void ProcessMonitor::SetWatchPaths(const std::vector<std::wstring>& paths)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_watchPaths = paths;
}

void ProcessMonitor::UpdateWatchPaths(const std::vector<std::wstring>& paths)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_watchPaths = paths;
}

void ProcessMonitor::Start()
{
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&ProcessMonitor::WorkerLoop, this);
}

void ProcessMonitor::Stop()
{
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
}

GameProcessInfo ProcessMonitor::GetCurrentGame() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_current;
}

std::vector<GameProcessInfo> ProcessMonitor::EnumMatchingProcesses() const
{
    std::vector<GameProcessInfo> result;
    std::vector<std::wstring> watchPaths;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        watchPaths = m_watchPaths;
    }
    if (watchPaths.empty()) return result;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            // 跳过自身和系统空闲进程
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == GetCurrentProcessId())
                continue;

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hProc) continue;

            wchar_t pathBuf[MAX_PATH] = { 0 };
            DWORD pathLen = MAX_PATH;
            BOOL ok = QueryFullProcessImageNameW(hProc, 0, pathBuf, &pathLen);
            CloseHandle(hProc);
            if (!ok) continue;

            std::wstring fullPath(pathBuf);
            for (const auto& root : watchPaths)
            {
                // V2: 用目录边界 + 精确 exe 匹配，避免误判同级路径/常驻进程
                if (Util::IsProcessInWatchPath(fullPath, root))
                {
                    GameProcessInfo info;
                    info.pid = pe.th32ProcessID;
                    info.exeName = pe.szExeFile;
                    info.fullPath = fullPath;
                    result.push_back(info);
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

void ProcessMonitor::WorkerLoop()
{
    DWORD lastPid = 0;
    while (m_running.load())
    {
        auto procs = EnumMatchingProcesses();
        if (!procs.empty())
        {
            // 取第一个匹配的进程
            const auto& p = procs.front();
            if (!m_gameRunning.load() || p.pid != lastPid)
            {
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    m_current = p;
                }
                m_gameRunning.store(true);
                lastPid = p.pid;
                if (m_onStart) m_onStart(p);
            }
        }
        else
        {
            if (m_gameRunning.exchange(false))
            {
                GameProcessInfo exited;
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    exited = m_current;
                    m_current = GameProcessInfo{};
                }
                lastPid = 0;
                if (m_onExit) m_onExit(exited);
            }
        }
        // 每 1.5 秒轮询一次，平衡响应速度与 CPU 占用
        for (int i = 0; i < 15 && m_running.load(); ++i)
            Sleep(100);
    }
}
