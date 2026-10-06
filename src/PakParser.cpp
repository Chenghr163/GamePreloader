#include "PakParser.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace
{
    // 小端读取辅助
    template<typename T>
    T ReadLE(const uint8_t* p)
    {
        T v{};
        std::memcpy(&v, p, sizeof(T));
        return v;
    }

    // 读取 FString（ANSI，长度前缀包含 null 终止符）
    bool ReadFString(const uint8_t*& p, const uint8_t* end, std::wstring& out)
    {
        if (p + 4 > end) return false;
        int32_t len = ReadLE<int32_t>(p);
        p += 4;
        if (len == 0) { out.clear(); return true; }
        if (len < 0)
        {
            // 负数表示 UTF-16
            int32_t charCount = -len;
            if (p + charCount * 2 > end) return false;
            out.assign((const wchar_t*)p, charCount - 1);
            p += charCount * 2;
        }
        else
        {
            if (p + len > end) return false;
            std::string ansi((const char*)p, len - 1);
            out = Util::Utf8ToWide(ansi); // Pak 文件名通常是 UTF-8/ANSI
            p += len;
        }
        return true;
    }
}

bool PakParser::Parse(const std::wstring& pakPath, PakInfo& outInfo)
{
    std::ifstream fin(pakPath, std::ios::binary | std::ios::ate);
    if (!fin.is_open()) return false;

    uint64_t fileSize = (uint64_t)fin.tellg();
    if (fileSize < 64) return false;

    // 读取文件末尾 4096 字节用于查找 footer magic
    uint64_t tailSize = std::min<uint64_t>(fileSize, 4096);
    fin.seekg(fileSize - tailSize, std::ios::beg);
    std::vector<uint8_t> tail(tailSize);
    fin.read((char*)tail.data(), tailSize);

    // 查找 magic: 0x5A6F12E1 小端 = E1 12 6F 5A
    const uint8_t magicBytes[4] = { 0xE1, 0x12, 0x6F, 0x5A };
    int64_t magicOff = -1;
    for (uint64_t i = 0; i + 4 <= tailSize; ++i)
    {
        if (std::memcmp(tail.data() + i, magicBytes, 4) == 0)
        {
            magicOff = (int64_t)i;
            break;
        }
    }
    if (magicOff < 0) return false;

    const uint8_t* fp = tail.data() + magicOff;
    const uint8_t* fend = tail.data() + tailSize;

    uint32_t magic = ReadLE<uint32_t>(fp); fp += 4;
    if (magic != PAK_MAGIC) return false;

    int32_t version = ReadLE<int32_t>(fp); fp += 4;
    uint64_t indexOffset = ReadLE<uint64_t>(fp); fp += 8;
    uint64_t indexSize = ReadLE<uint64_t>(fp); fp += 8;
    // IndexHash 20 bytes
    if (fp + 20 > fend) return false;
    fp += 20;

    bool indexEncrypted = false;
    if (version >= 3)
    {
        if (fp + 1 > fend) return false;
        indexEncrypted = (*fp != 0);
        fp += 1;
    }
    // 版本 >=4: CompressionMethods 数组（int32 count + 若干FString），跳过
    if (version >= 4)
    {
        if (fp + 4 > fend) goto skip_footer_rest;
        int32_t count = ReadLE<int32_t>(fp); fp += 4;
        for (int32_t i = 0; i < count; ++i)
        {
            std::wstring dummy;
            if (!ReadFString(fp, fend, dummy)) goto skip_footer_rest;
        }
    }
    // 版本 >=5: IndexHashExtended (32 bytes)
    if (version >= 5) { if (fp + 32 > fend) goto skip_footer_rest; fp += 32; }
    // 版本 >=6: EncryptionKeyGuid (16 bytes)
    if (version >= 6) { if (fp + 16 > fend) goto skip_footer_rest; fp += 16; }
    // 版本 >=7: 新增字段（通常是 bool 或 guid），容错跳过
    // 版本 >=8: IndexIsFrozen bool
    // 后续版本字段不影响核心解析，直接停止 footer 解析

skip_footer_rest:

    outInfo.filePath = pakPath;
    outInfo.version = version;
    outInfo.indexOffset = indexOffset;
    outInfo.indexSize = indexSize;
    outInfo.indexEncrypted = indexEncrypted;

    if (indexEncrypted || indexSize == 0 || indexOffset >= fileSize)
    {
        // 加密索引无法解析清单，但文件本身是合法 Pak
        return true;
    }

    // 读取索引数据
    fin.seekg(indexOffset, std::ios::beg);
    std::vector<uint8_t> index(indexSize);
    fin.read((char*)index.data(), indexSize);
    if ((uint64_t)fin.gcount() < indexSize) return false;
    fin.close();

    // 解析索引：连续的 FString + FPakEntry，以空字符串结束
    const uint8_t* p = index.data();
    const uint8_t* iend = index.data() + indexSize;

    while (p < iend)
    {
        std::wstring fname;
        if (!ReadFString(p, iend, fname)) break;
        if (fname.empty()) break; // 空文件名标记索引结束

        PakEntry entry;
        entry.fileName = fname;

        // FPakEntry 序列化字段（兼容 V8+）
        if (p + 8 > iend) break;
        entry.offset = ReadLE<uint64_t>(p); p += 8;
        if (p + 8 > iend) break;
        entry.uncompressedSize = ReadLE<uint64_t>(p); p += 8;
        if (p + 4 > iend) break;
        entry.compressionMethod = ReadLE<int32_t>(p); p += 4;
        if (p + 8 > iend) break;
        /* int64 timestamp = */ ReadLE<int64_t>(p); p += 8;
        if (p + 20 > iend) break;
        std::memcpy(entry.sha1, p, 20); p += 20;
        if (p + 4 > iend) break;
        uint32_t blockSize = ReadLE<uint32_t>(p); p += 4;
        if (p + 4 > iend) break;
        int32_t blockCount = ReadLE<int32_t>(p); p += 4;

        uint64_t lastBlockEnd = 0;
        for (int32_t b = 0; b < blockCount; ++b)
        {
            if (p + 16 > iend) break;
            uint64_t cs = ReadLE<uint64_t>(p); p += 8;
            uint64_t ce = ReadLE<uint64_t>(p); p += 8;
            lastBlockEnd = ce;
        }
        if (p + 1 > iend) break;
        entry.encrypted = (*p != 0); p += 1;

        // 计算 compressedSize
        if (blockCount > 0)
            entry.compressedSize = lastBlockEnd - entry.offset;
        else
            entry.compressedSize = entry.uncompressedSize;

        (void)blockSize;

        outInfo.entries.push_back(std::move(entry));
    }

    return true;
}

bool PakParser::ReadEntryRaw(const PakInfo& info, const PakEntry& entry, std::vector<uint8_t>& outData)
{
    if (entry.encrypted || entry.compressionMethod != 0)
        return false; // 仅支持未压缩未加密

    std::ifstream fin(info.filePath, std::ios::binary);
    if (!fin.is_open()) return false;
    fin.seekg(entry.offset, std::ios::beg);
    outData.resize(entry.uncompressedSize);
    fin.read((char*)outData.data(), entry.uncompressedSize);
    return (uint64_t)fin.gcount() == entry.uncompressedSize;
}

// V3: 资源分类
// V1.5: 加入 Cinematics（剧情/过场）分类，优先级在核心三类之后、Other 之前
PakParser::ResourceClass PakParser::ClassifyResource(const std::wstring& fileName)
{
    std::wstring fn = Util::ToLower(fileName);
    if (IsLevelResource(fn)) return ResourceClass::Level;
    if (IsCharacterActionResource(fn)) return ResourceClass::CharacterAction;
    if (IsBulkTexture(fn)) return ResourceClass::BulkTexture;
    if (IsCinematicsResource(fn)) return ResourceClass::Cinematics;
    return ResourceClass::Other;
}

bool PakParser::IsLevelResource(const std::wstring& fn)
{
    // UE5 关卡文件 .umap（本局/本章场景），通常位于 Content/Maps
    return fn.find(L".umap") != std::wstring::npos;
}

bool PakParser::IsCharacterActionResource(const std::wstring& fn)
{
    // 第三人称/角色动作相关资源：
    // 动画序列/蓝图、骨架网格、物理资产、控制绑定、蒙太奇
    if (fn.find(L".umap") != std::wstring::npos) return false;
    if (fn.find(L".ubulk") != std::wstring::npos) return false;
    bool isAsset = fn.find(L".uasset") != std::wstring::npos;
    bool hint =
        fn.find(L"anim") != std::wstring::npos ||
        fn.find(L"skeletalmesh") != std::wstring::npos ||
        fn.find(L"skeletal") != std::wstring::npos ||
        fn.find(L"physicsasset") != std::wstring::npos ||
        fn.find(L"controlrig") != std::wstring::npos ||
        fn.find(L"montage") != std::wstring::npos ||
        fn.find(L"character") != std::wstring::npos ||
        fn.find(L"thirdperson") != std::wstring::npos;
    return isAsset && hint;
}

bool PakParser::IsBulkTexture(const std::wstring& fn)
{
    // 贴图 bulk 数据（体积大，通常是本局高清贴图主体）
    return fn.find(L".ubulk") != std::wstring::npos;
}

// V1.5: 剧情/过场资源识别
// 覆盖：视频文件（.bk2/.bik/.mp4/.webm/.wmv/.avi/.mov/.m4v）、
//       剧情目录（Cinematics/Movies/Sequences/Cutscenes/Video/Intro/Outro/Story/FMV）、
//       Sequencer 过场资产（LevelSequence/CineCamera）
bool PakParser::IsCinematicsResource(const std::wstring& fn)
{
    // 1. 视频文件扩展名（剧情过场最常见的大体积资源）
    static const wchar_t* videoExts[] = {
        L".bk2", L".bik", L".mp4", L".webm", L".wmv",
        L".avi", L".mov", L".m4v", L".ogv", L".mkv"
    };
    for (auto ext : videoExts)
    {
        if (fn.find(ext) != std::wstring::npos) return true;
    }
    // 2. 剧情/过场目录关键词
    static const wchar_t* cineDirs[] = {
        L"cinematics", L"movies", L"sequences", L"cutscenes",
        L"video", L"videos", L"cutscene", L"intro", L"outro",
        L"story", L"plot", L"fmv", L"sequencer"
    };
    for (auto d : cineDirs)
    {
        if (fn.find(d) != std::wstring::npos) return true;
    }
    // 3. Sequencer 过场资产（LevelSequence / CineCamera 相关 .uasset）
    if (fn.find(L".uasset") != std::wstring::npos)
    {
        if (fn.find(L"levelsequence") != std::wstring::npos ||
            fn.find(L"cinecamera") != std::wstring::npos ||
            fn.find(L"cameracut") != std::wstring::npos)
            return true;
    }
    return false;
}

// V3: 把条目的数据块（压缩态原始字节）流式复制到输出流
uint64_t PakParser::StreamEntryData(const PakInfo& info, const PakEntry& entry, std::ofstream& out)
{
    // 需要读取的字节数：压缩则读压缩块，未压缩读原始
    uint64_t bytesToRead = (entry.compressionMethod != 0)
        ? entry.compressedSize
        : entry.uncompressedSize;
    if (bytesToRead == 0) return 0;

    std::ifstream fin(info.filePath, std::ios::binary);
    if (!fin.is_open()) return 0;
    fin.seekg((std::streamoff)entry.offset, std::ios::beg);
    if (!fin.good()) return 0;

    const size_t CHUNK = 4 * 1024 * 1024; // 4MB 分块，不占大内存
    std::vector<char> buf(CHUNK);
    uint64_t written = 0;
    while (written < bytesToRead)
    {
        uint64_t n = std::min<uint64_t>(CHUNK, bytesToRead - written);
        fin.read(buf.data(), (std::streamsize)n);
        std::streamsize got = fin.gcount();
        if (got <= 0) break;
        out.write(buf.data(), got);
        written += (uint64_t)got;
        if (got < (std::streamsize)n) break; // 文件提前结束
    }
    return written;
}

PakParser::ResourceStats PakParser::CalcStats(const PakInfo& info)
{
    ResourceStats s{};
    for (const auto& e : info.entries)
    {
        s.total++;
        if (e.encrypted) s.encrypted++;
        if (e.compressionMethod != 0) s.compressed++;

        std::wstring fn = Util::ToLower(e.fileName);
        if (fn.find(L".uasset") != std::wstring::npos ||
            fn.find(L".ubulk") != std::wstring::npos ||
            fn.find(L".png") != std::wstring::npos ||
            fn.find(L".tga") != std::wstring::npos ||
            fn.find(L".exr") != std::wstring::npos ||
            fn.find(L"texture") != std::wstring::npos ||
            fn.find(L"textures") != std::wstring::npos)
            s.textureCount++;
        if (fn.find(L".fbx") != std::wstring::npos ||
            fn.find(L".obj") != std::wstring::npos ||
            fn.find(L"mesh") != std::wstring::npos ||
            fn.find(L"model") != std::wstring::npos ||
            fn.find(L"skeletalmesh") != std::wstring::npos ||
            fn.find(L"staticmesh") != std::wstring::npos)
            s.modelCount++;
        if (fn.find(L".ushader") != std::wstring::npos ||
            fn.find(L".ushaderbytecode") != std::wstring::npos ||
            fn.find(L"shader") != std::wstring::npos)
            s.shaderCount++;
    }
    return s;
}

// V1.8: IoStore 容器识别（现代 UE5 / 黑神话悟空主用 IoStore 而非传统 Pak）
bool PakParser::IsIoStoreDataFile(const std::wstring& fileName)
{
    std::wstring fn = Util::ToLower(fileName);
    return fn.find(L".ucas") != std::wstring::npos;
}

bool PakParser::IsIoStoreIndexFile(const std::wstring& fileName)
{
    std::wstring fn = Util::ToLower(fileName);
    return fn.find(L".utoc") != std::wstring::npos;
}
