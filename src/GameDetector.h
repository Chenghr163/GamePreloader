#pragma once
// 游戏检测器：DX版本识别 / UE5引擎识别 / 加载阶段判定
#include "Common.h"
#include "ProcessMonitor.h"

class GameDetector
{
public:
    // 检测进程当前加载的 DX 版本（通过已加载模块判断）
    static DxVersion DetectDxVersion(DWORD pid);

    // 判断是否为 UE5 引擎游戏（基于目录特征 + exe 版本信息）
    static bool DetectUE5(const GameProcessInfo& info);

    // 查找 UE5 ShaderCache 目录
    static std::wstring FindUE5ShaderCacheDir(const std::wstring& gameRoot);

    // 查找游戏根目录（exe 所在目录向上找包含 .pak 的 Content/Paks 目录）
    static std::wstring FindGameRoot(const std::wstring& exePath);

    // 判定是否处于加载阶段：
    // 启发式：进程启动后 60s 内，或 IO 读取速率显著高于基线，或主窗口尚未出现
    static bool IsInLoadingPhase(DWORD pid, DWORD startTimeMs);

    // 获取进程启动时间（毫秒，相对于系统启动）
    static DWORD GetProcessStartTimeMs(DWORD pid);
};
