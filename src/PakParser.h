#pragma once
// UE5 Pak 资源包解析器
// 基于 Epic Games 公开的 FPakInfo / FPakEntry 二进制格式
// 能力：读取文件清单、识别压缩/加密状态、提取未压缩未加密资源原始数据
// 边界：不破解加密、不内置 zlib/oodle/zstd 解压（仅标记压缩方式）
#include "Common.h"
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>

// V1.8: 解析库编译为独立 DLL（park/UE5PakParser.dll）
//  - 编译 DLL 时定义 GAMEPAK_EXPORTS -> dllexport
//  - 主程序引用头文件时 -> dllimport（隐式链接 UE5PakParser.lib）
#ifdef GAMEPAK_EXPORTS
#define GAMEPAK_API __declspec(dllexport)
#else
#define GAMEPAK_API __declspec(dllimport)
#endif

struct PakEntry
{
    std::wstring fileName;       // 内部路径，如 ../../../MyGame/Content/Textures/Foo.uasset
    uint64_t     offset = 0;     // 在 pak 文件中的数据偏移
    uint64_t     uncompressedSize = 0;
    uint64_t     compressedSize = 0;
    int32_t      compressionMethod = 0; // 0=None, 1=Zlib, 2=Gzip, 3=Oodle, 4=LZ4, 5=Zstd
    bool         encrypted = false;
    uint8_t      sha1[20] = {0};
};

struct PakInfo
{
    std::wstring filePath;
    int32_t      version = 0;
    uint64_t     indexOffset = 0;
    uint64_t     indexSize = 0;
    bool         indexEncrypted = false;
    std::vector<PakEntry> entries;
};

class GAMEPAK_API PakParser
{
public:
    // 解析单个 .pak 文件，返回是否成功
    static bool Parse(const std::wstring& pakPath, PakInfo& outInfo);

    // 提取未压缩、未加密条目的原始数据
    static bool ReadEntryRaw(const PakInfo& info, const PakEntry& entry, std::vector<uint8_t>& outData);

    // V3: 资源类型分类（用于"本局场景 + 第三人称动作"深度预读）
    // V1.5: 新增 Cinematics（剧情/过场资源），优化剧情阶段加载卡顿
    enum class ResourceClass { Level, CharacterAction, BulkTexture, Cinematics, Other };
    static ResourceClass ClassifyResource(const std::wstring& fileName);
    static bool IsLevelResource(const std::wstring& fn);            // .umap 本局关卡
    static bool IsCharacterActionResource(const std::wstring& fn);  // 动画/骨架网格/物理资产
    static bool IsBulkTexture(const std::wstring& fn);              // .ubulk 贴图bulk数据
    static bool IsCinematicsResource(const std::wstring& fn);       // V1.5: 剧情/过场视频与资源

    // V3: 把条目的数据块（压缩态）流式写入输出流，返回写入字节数。
    // 支持大文件，分块复制不占大内存；用于把本局资源预读到 cache。
    static uint64_t StreamEntryData(const PakInfo& info, const PakEntry& entry, std::ofstream& out);

    // 统计资源类型数量
    struct ResourceStats
    {
        int textureCount = 0;   // .uasset/.ubulk 中含贴图 / .png / .tga / .exr
        int modelCount = 0;     // .fbx / .uasset 中含模型
        int shaderCount = 0;    // .ushader / .ushaderbytecode
        int total = 0;
        int compressed = 0;
        int encrypted = 0;
    };
    static ResourceStats CalcStats(const PakInfo& info);

    // Pak 文件魔数（Epic 公开定义）
    static constexpr uint32_t PAK_MAGIC = 0x5A6F12E1;

    // V1.8: IoStore 容器识别（黑神话悟空等现代 UE5 游戏主用 .utoc/.ucas）
    //  .ucas = 分块数据容器（实际资源数据，体积大）
    //  .utoc = 容器索引表
    //  .sig  = IoStore 签名
    static bool IsIoStoreDataFile(const std::wstring& fileName);   // .ucas
    static bool IsIoStoreIndexFile(const std::wstring& fileName);  // .utoc
};
