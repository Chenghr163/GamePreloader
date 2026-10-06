#pragma once
// 预处理引擎：缓存复用 / Pak清单预解析 / ShaderCache预热 / 帧率基准调度
#include "Common.h"
#include "PakParser.h"
#include <string>
#include <vector>
#include <atomic>
#include <utility>

struct PreprocessResult
{
    bool        cacheHit = false;
    int         pakCount = 0;
    int         totalResources = 0;
    int         textureCount = 0;
    int         modelCount = 0;
    int         shaderCount = 0;
    int         encryptedCount = 0;
    int         compressedCount = 0;
    std::wstring shaderCacheDir;
    std::wstring gameRoot;
    uint64_t    elapsedMs = 0;
    // V3: 本局场景 + 第三人称动作 深度预读结果
    bool        levelPreloadDone = false;
    int         levelCount = 0;           // 本局关卡(.umap)数
    int         actionResourceCount = 0;  // 第三人称动作资源数
    int         cineResourceCount = 0;    // V1.5: 剧情/过场资源数
    uint64_t    levelCacheBytes = 0;      // 本局预读缓存文件大小
    std::wstring levelCachePath;
};

class Preprocessor
{
public:
    // 执行预处理（同步，建议在后台线程调用）
    // gameExePath: 游戏 exe 完整路径
    // targetFps: 帧率基准，影响预计算粒度
    // progress: 输出 0-100 进度
    static PreprocessResult Run(const std::wstring& gameExePath, int targetFps, std::atomic<int>& progress);

    // V3Pro: 帧率优先——扩容本局缓存到更大目标（直到帧率达标）。
    // 强制重建缓存文件到 newTargetBytes 大小（下限 5GB，上限 20GB）。
    static void ExpandLevelCache(const std::wstring& gameRoot, uint64_t newTargetBytes,
                                 std::atomic<int>& progress, PreprocessResult& result);

private:
    // 缓存文件路径
    static std::wstring CacheFilePath(const std::wstring& gameRoot);

    // V3: 本局场景 + 第三人称动作 预读缓存文件路径
    static std::wstring LevelCacheFilePath(const std::wstring& gameRoot);

    // V3: 检查本局预读缓存是否有效
    static bool CheckLevelCacheValid(const std::wstring& gameRoot, const std::wstring& cachePath);

    // V3: 本局场景 + 第三人称动作 深度预读。
    // 把本局所有 .umap 关卡 + 角色动作资源 + 贴图bulk 的数据块流式写入 cache 文件，
    // 达到手机预处理效果（提前把本局资源从磁盘读入本地缓存，运行期免IO卡顿）。
    // 缓存文件创建时强制 ≥ targetBytes（V3Pro 默认 5GB）：核心资源不足时
    // 继续写入其它资源、整个 Pak 原始数据、并兜底填充，确保绝对不低于目标大小。
    // force=true 时强制重建（用于帧率扩容）；否则缓存有效且大小达标则快速复用。
    static constexpr uint64_t MIN_LEVEL_CACHE_BYTES = 5ULL * 1024 * 1024 * 1024; // 5GB 下限
    // V1.8: 缓存上限运行时决定——检测到独立显卡为 20GB；无独显（核显/APU）拉高到 40GB，
    // 用更大的本地预读缓存为核显兜底，覆盖大型复杂场景与剧情（如黑神话悟空）。
    static uint64_t MaxCacheBytes();
    static void PreloadLevelAndActions(const std::wstring& gameRoot,
                                       const std::vector<std::wstring>& pakFiles,
                                       std::atomic<int>& progress, int startPct,
                                       PreprocessResult& result,
                                       bool force = false,
                                       uint64_t targetBytes = MIN_LEVEL_CACHE_BYTES);

    // 检查缓存是否有效（存在且游戏根目录文件未变更）
    static bool CheckCacheValid(const std::wstring& gameRoot, const std::wstring& cachePath);

    // 写入缓存
    static bool WriteCache(const std::wstring& cachePath, const PreprocessResult& result);

    // 读取缓存
    static bool ReadCache(const std::wstring& cachePath, PreprocessResult& result);

    // 扫描目录下所有 .pak 文件（传统 Pak）
    static std::vector<std::wstring> FindPakFiles(const std::wstring& gameRoot);

    // V1.8: 扫描 IoStore 数据容器 .ucas（现代 UE5/黑神话悟空主用的大体积分块数据）
    static std::vector<std::wstring> FindIoStoreFiles(const std::wstring& gameRoot);

    // V2.0: 引擎无关的通用大文件发现（Unity 的 *_Data、ForzaTech 的 media/mediaPC、通用数据目录）
    // 返回 (文件路径, 字节大小)，按大小降序，供整体流式预读，兼容《午夜轮班》《地平线6》等非UE游戏。
    static std::vector<std::pair<std::wstring, uint64_t>> FindLargeDataFiles(const std::wstring& gameRoot);

    // 预热 ShaderCache：touch 目录下文件，让 OS 文件缓存预读
    static void WarmupShaderCache(const std::wstring& dir);
};
