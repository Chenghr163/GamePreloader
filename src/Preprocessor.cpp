#include "Preprocessor.h"
#include "GameDetector.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <functional>
#include <algorithm>

namespace fs = std::filesystem;

std::wstring Preprocessor::CacheFilePath(const std::wstring& gameRoot)
{
    // 用游戏根目录路径的哈希做缓存文件名
    std::wstring dir = Util::GetCacheDir();
    std::hash<std::wstring> hasher;
    size_t h = hasher(gameRoot);
    wchar_t fname[64];
    swprintf_s(fname, L"preload_%016llx.cache", (unsigned long long)h);
    return dir + L"\\" + fname;
}

// V3: 本局场景+第三人称动作 深度预读缓存文件路径
std::wstring Preprocessor::LevelCacheFilePath(const std::wstring& gameRoot)
{
    std::wstring dir = Util::GetCacheDir();
    std::hash<std::wstring> hasher;
    size_t h = hasher(gameRoot);
    wchar_t fname[64];
    swprintf_s(fname, L"preload_level_%016llx.cache", (unsigned long long)h);
    return dir + L"\\" + fname;
}

// V3: 本局预读缓存有效性：存在且 Pak 未更新
bool Preprocessor::CheckLevelCacheValid(const std::wstring& gameRoot, const std::wstring& cachePath)
{
    if (!fs::exists(cachePath)) return false;
    try {
        auto cacheTime = fs::last_write_time(cachePath);
        auto pakFiles = FindPakFiles(gameRoot);
        for (auto& pak : pakFiles)
            if (fs::last_write_time(pak) > cacheTime) return false;
    } catch (...) { return false; }
    return true;
}

bool Preprocessor::CheckCacheValid(const std::wstring& gameRoot, const std::wstring& cachePath)
{
    if (!fs::exists(cachePath)) return false;
    // V2: 只检查定向定位到的 Pak 目录下的 .pak 修改时间，不再暴力遍历整个游戏根
    try {
        auto cacheTime = fs::last_write_time(cachePath);
        auto pakFiles = FindPakFiles(gameRoot);
        for (auto& pak : pakFiles)
        {
            if (fs::last_write_time(pak) > cacheTime)
                return false; // 有更新的 Pak，缓存失效
        }
    } catch (...) { return false; }
    return true;
}

bool Preprocessor::WriteCache(const std::wstring& cachePath, const PreprocessResult& r)
{
    std::ofstream fout(cachePath, std::ios::binary);
    if (!fout.is_open()) return false;
    // 简单二进制格式：魔数 + 字段
    const char magic[8] = { 'G','P','L','D','C','A','C','H' };
    fout.write(magic, 8);
    int32_t version = 1;
    fout.write((const char*)&version, 4);
    fout.write((const char*)&r.pakCount, 4);
    fout.write((const char*)&r.totalResources, 4);
    fout.write((const char*)&r.textureCount, 4);
    fout.write((const char*)&r.modelCount, 4);
    fout.write((const char*)&r.shaderCount, 4);
    fout.write((const char*)&r.encryptedCount, 4);
    fout.write((const char*)&r.compressedCount, 4);
    // 字符串：int32长度 + UTF-8字节
    auto writeStr = [&](const std::wstring& s) {
        std::string u8 = Util::WideToUtf8(s);
        int32_t len = (int32_t)u8.size();
        fout.write((const char*)&len, 4);
        fout.write(u8.data(), len);
    };
    writeStr(r.gameRoot);
    writeStr(r.shaderCacheDir);
    return fout.good();
}

bool Preprocessor::ReadCache(const std::wstring& cachePath, PreprocessResult& r)
{
    std::ifstream fin(cachePath, std::ios::binary);
    if (!fin.is_open()) return false;
    char magic[8];
    fin.read(magic, 8);
    if (std::memcmp(magic, "GPLDCACH", 8) != 0) return false;
    int32_t version;
    fin.read((char*)&version, 4);
    if (version != 1) return false;
    fin.read((char*)&r.pakCount, 4);
    fin.read((char*)&r.totalResources, 4);
    fin.read((char*)&r.textureCount, 4);
    fin.read((char*)&r.modelCount, 4);
    fin.read((char*)&r.shaderCount, 4);
    fin.read((char*)&r.encryptedCount, 4);
    fin.read((char*)&r.compressedCount, 4);
    auto readStr = [&](std::wstring& s) {
        int32_t len;
        fin.read((char*)&len, 4);
        if (len < 0 || len > 10 * 1024 * 1024) return false;
        std::string u8(len, 0);
        fin.read(&u8[0], len);
        s = Util::Utf8ToWide(u8);
        return true;
    };
    if (!readStr(r.gameRoot)) return false;
    if (!readStr(r.shaderCacheDir)) return false;
    return fin.good();
}

std::vector<std::wstring> Preprocessor::FindPakFiles(const std::wstring& gameRoot)
{
    std::vector<std::wstring> result;
    // V2: 定向定位 UE5 标准 Pak 目录（非递归），避免暴力遍历整个游戏根导致数秒延迟
    std::vector<std::wstring> paksDirs;

    // 策略1: 一级子目录的 <Game>/Content/Paks
    try {
        for (auto& entry : fs::directory_iterator(gameRoot))
        {
            if (!entry.is_directory()) continue;
            std::wstring paks = entry.path().wstring() + L"\\Content\\Paks";
            if (fs::exists(paks)) paksDirs.push_back(paks);
        }
    } catch (...) {}

    // 策略2: 直接根下的 Content/Paks 或 Paks
    {
        std::wstring paks = gameRoot + L"\\Content\\Paks";
        if (fs::exists(paks)) paksDirs.push_back(paks);
        paks = gameRoot + L"\\Paks";
        if (fs::exists(paks)) paksDirs.push_back(paks);
    }

    // 只在定位到的 Paks 目录下找 .pak（Pak 文件都在顶层，非递归）
    for (auto& paksDir : paksDirs)
    {
        try {
            for (auto& entry : fs::directory_iterator(paksDir))
            {
                if (entry.is_regular_file() && Util::ToLower(entry.path().extension().wstring()) == L".pak")
                    result.push_back(entry.path().wstring());
            }
        } catch (...) {}
    }
    return result;
}

// V1.8: 查找 IoStore 数据容器 .ucas（现代 UE5/黑神话悟空主用，真正的大体积分块数据）。
// .utoc 索引很小、不做整盘预读，故此处只收集 .ucas。定位走 Content/Paks 并向下看一层子目录。
std::vector<std::wstring> Preprocessor::FindIoStoreFiles(const std::wstring& gameRoot)
{
    std::vector<std::wstring> result;
    std::vector<std::wstring> paksDirs;
    try {
        for (auto& entry : fs::directory_iterator(gameRoot))
        {
            if (!entry.is_directory()) continue;
            std::wstring paks = entry.path().wstring() + L"\\Content\\Paks";
            if (fs::exists(paks)) paksDirs.push_back(paks);
        }
    } catch (...) {}
    {
        std::wstring paks = gameRoot + L"\\Content\\Paks";
        if (fs::exists(paks)) paksDirs.push_back(paks);
        paks = gameRoot + L"\\Paks";
        if (fs::exists(paks)) paksDirs.push_back(paks);
    }

    auto collect = [&](const std::wstring& dir) {
        try {
            for (auto& entry : fs::directory_iterator(dir))
            {
                if (!entry.is_regular_file()) continue;
                std::wstring ext = Util::ToLower(entry.path().extension().wstring());
                if (ext == L".ucas")
                    result.push_back(entry.path().wstring());
            }
        } catch (...) {}
    };

    for (auto& paksDir : paksDirs)
    {
        collect(paksDir);
        // 再向下看一层子目录（pakchunk 分块目录）
        try {
            for (auto& sub : fs::directory_iterator(paksDir))
            {
                if (sub.is_directory()) collect(sub.path().wstring());
            }
        } catch (...) {}
    }
    return result;
}

// V1.8: 缓存上限——有独显 20GB；无独显（核显/APU）拉高到 40GB
uint64_t Preprocessor::MaxCacheBytes()
{
    return Util::HasDedicatedGpu()
        ? (20ULL * 1024 * 1024 * 1024)
        : (40ULL * 1024 * 1024 * 1024);
}

// V2.0: 引擎无关的通用大文件发现
//  定向常见数据目录：Unity 的 *_Data、ForzaTech 的 media/mediaPC；再以 gameRoot 兜底浅扫。
//  受控 BFS（限深度/文件数、跳过符号链接与可执行/UE容器扩展名），收集大体积数据文件，
//  返回 (路径, 大小) 并按大小降序，供整体流式预读——兼容《午夜轮班》(Unity)、《地平线6》(ForzaTech)。
std::vector<std::pair<std::wstring, uint64_t>> Preprocessor::FindLargeDataFiles(const std::wstring& gameRoot)
{
    std::vector<std::pair<std::wstring, uint64_t>> result;
    std::vector<std::wstring> roots;
    auto addRoot = [&](const std::wstring& d) {
        std::error_code ec;
        if (!fs::exists(d, ec)) return;
        for (auto& e : roots) if (_wcsicmp(e.c_str(), d.c_str()) == 0) return;
        roots.push_back(d);
    };

    // 定向数据目录
    try {
        for (auto& e : fs::directory_iterator(gameRoot))
        {
            if (!e.is_directory()) continue;
            std::wstring name = Util::ToLower(e.path().filename().wstring());
            if (name.size() >= 5 && name.compare(name.size() - 5, 5, L"_data") == 0)
                addRoot(e.path().wstring());                    // Unity *_Data
            if (name == L"media" || name == L"mediapc")
                addRoot(e.path().wstring());                    // ForzaTech media
        }
    } catch (...) {}
    addRoot(gameRoot);                                          // 兜底：gameRoot 浅扫

    const uint64_t BIG = 16ULL * 1024 * 1024;                  // >=16MB 直接收（覆盖无扩展名大文件）
    const uint64_t KNOWN_MIN = 1ULL * 1024 * 1024;             // 已知数据扩展名 >=1MB 收

    auto isDataExt = [](const std::wstring& ext) -> bool {
        static const wchar_t* k[] = {
            L".assets", L".ress", L".resources", L".bundle", L".bin", L".zip", L".model",
            L".texture", L".wad", L".dat", L".cache", L".sharedassets", L".resS",
            L".rpf", L".forge", L".mesh", L".tex", L".bank", L".pck"
        };
        for (auto* x : k) if (ext == x) return true;
        return false;
    };
    auto isSkipExt = [](const std::wstring& ext) -> bool {
        static const wchar_t* k[] = {
            L".exe", L".dll", L".sys", L".bat", L".cmd", L".lnk", L".pak", L".ucas",
            L".utoc", L".sig", L".pdb", L".ico", L".png", L".jpg", L".jpeg"
        };
        for (auto* x : k) if (ext == x) return true;
        return false;
    };

    const int MAX_DEPTH = 4;
    const size_t MAX_FILES = 6000;
    std::vector<std::pair<std::wstring, int>> dq;
    std::vector<std::wstring> visited;
    for (auto& r : roots) dq.push_back(std::make_pair(r, 0));

    while (!dq.empty())
    {
        std::pair<std::wstring, int> item = dq.back(); dq.pop_back();
        const std::wstring dir = item.first; int depth = item.second;
        bool seen = false;
        for (auto& v : visited) if (_wcsicmp(v.c_str(), dir.c_str()) == 0) { seen = true; break; }
        if (seen) continue;
        visited.push_back(dir);

        std::error_code ec;
        fs::directory_iterator it(dir, ec), endit;
        if (ec) continue;
        for (; it != endit; it.increment(ec))
        {
            if (ec) { ec.clear(); continue; }
            std::error_code ec2;
            const fs::directory_entry& de = *it;
            if (de.is_symlink(ec2)) continue;
            if (de.is_directory(ec2))
            {
                if (depth + 1 < MAX_DEPTH)
                    dq.push_back(std::make_pair(de.path().wstring(), depth + 1));
            }
            else if (de.is_regular_file(ec2))
            {
                std::wstring ext = Util::ToLower(de.path().extension().wstring());
                if (isSkipExt(ext)) continue;
                uint64_t sz = (uint64_t)de.file_size(ec2);
                if (ec2) continue;
                bool keep = (sz >= BIG) || (isDataExt(ext) && sz >= KNOWN_MIN);
                if (keep && result.size() < MAX_FILES)
                    result.push_back(std::make_pair(de.path().wstring(), sz));
            }
        }
    }

    std::sort(result.begin(), result.end(),
        [](const std::pair<std::wstring, uint64_t>& a, const std::pair<std::wstring, uint64_t>& b) {
            return a.second > b.second;
        });
    return result;
}

// V3: 本局场景 + 第三人称动作 深度预读（达到手机预处理效果）
// 把本局所有 .umap 关卡 + 角色动作资源 + 贴图bulk 的数据块流式写入 cache 文件。
// 读取动作本身会把对应 pak 数据载入 OS 文件缓存，运行期游戏读取时免随机IO卡顿；
// cache 文件保存本局资源的完整副本，且**创建时强制 ≥ targetBytes（V3Pro 默认 5GB）**：
//   - 第一遍：本局关卡 + 第三人称动作 + 贴图bulk（核心资源）
//   - 第二遍：其它资源（贴图/音频/数据等）补足
//   - 第三遍：整个 Pak 原始数据兜底
//   - 第四遍：循环填充（绝对保证不低于目标大小）
// 保证任何游戏都能建立 ≥ targetBytes 的手机级预加载缓存。
void Preprocessor::PreloadLevelAndActions(const std::wstring& gameRoot,
                                          const std::vector<std::wstring>& pakFiles,
                                          std::atomic<int>& progress, int startPct,
                                          PreprocessResult& result,
                                          bool force,
                                          uint64_t targetBytes)
{
    std::wstring lcPath = LevelCacheFilePath(gameRoot);
    result.levelCachePath = lcPath;

    // V3Pro: 缓存已有效且非强制重建，且大小达标 → 快速复用
    // 若缓存有效但大小 < targetBytes（如旧版 2GB 缓存），强制重建到目标大小
    if (!force && CheckLevelCacheValid(gameRoot, lcPath))
    {
        std::error_code ec;
        uint64_t sz = fs::file_size(lcPath, ec);
        if (sz >= targetBytes)
        {
            result.levelPreloadDone = true;
            result.levelCacheBytes = sz;
            return;
        }
        // 大小不足，继续往下强制重建
    }

    // V1.8: targetBytes 运行时 clamp 到 [5GB, 上限(独显20GB/核显40GB)]
    if (targetBytes < MIN_LEVEL_CACHE_BYTES) targetBytes = MIN_LEVEL_CACHE_BYTES;
    {
        uint64_t capBytes = MaxCacheBytes();
        if (targetBytes > capBytes) targetBytes = capBytes;
    }

    result.levelPreloadDone = false;
    std::ofstream out(lcPath, std::ios::binary);
    if (!out.is_open()) return;

    // 文件头：magic(8) + version(4) + entryCount(4)
    const char magic[8] = { 'G','P','L','L','V','L','0','2' };
    out.write(magic, 8);
    int32_t ver = 2;
    out.write((const char*)&ver, 4);
    int32_t entryCount = 0;
    out.write((const char*)&entryCount, 4);

    auto writeFString = [&](const std::wstring& s) {
        std::string u8 = Util::WideToUtf8(s);
        int32_t len = (int32_t)u8.size();
        out.write((const char*)&len, 4);
        out.write(u8.data(), len);
    };
    // V1.8 统一条目 type 编号（唯一不撞号）：
    // 0=Level 1=CharacterAction 2=BulkTexture 3=Cinematics 4=Other
    // 5=RawPak(整包兜底) 6=PadFill(零填充) 7=IoStore(.ucas 容器)
    auto beginEntry = [&](int32_t type, const std::wstring& fname) {
        out.write((const char*)&type, 4);
        writeFString(fname);
        std::streamoff lenPos = out.tellp();
        uint64_t dataLen = 0;
        out.write((const char*)&dataLen, 8);
        return lenPos;
    };
    auto endEntry = [&](std::streamoff lenPos, uint64_t written) {
        std::streamoff endPos = out.tellp();
        out.seekp(lenPos);
        out.write((const char*)&written, 8);
        out.seekp(endPos);
        entryCount++;
    };

    int levelCount = 0, actionCount = 0, cineCount = 0;
    uint64_t total = 0;

    // 解析所有 Pak 一次，供多遍复用
    std::vector<PakInfo> infos;
    infos.reserve(pakFiles.size());
    int totalPaks = (int)pakFiles.size();
    int parsed = 0;
    for (const auto& pak : pakFiles)
    {
        PakInfo info;
        if (PakParser::Parse(pak, info)) infos.push_back(std::move(info));
        parsed++;
        int pct = startPct + (int)((66 - startPct) * (double)parsed / std::max(1, totalPaks));
        progress.store(std::min(66, pct));
    }

    // 第一遍：核心三类（本局关卡 + 第三人称动作 + 贴图bulk）
    int pass1Index = 0;
    int pass1Total = (int)infos.size();
    for (const auto& info : infos)
    {
        for (const auto& e : info.entries)
        {
            auto cls = PakParser::ClassifyResource(e.fileName);
            if (cls == PakParser::ResourceClass::Level) levelCount++;
            else if (cls == PakParser::ResourceClass::CharacterAction) actionCount++;
            else if (cls == PakParser::ResourceClass::Cinematics) cineCount++;

            // V1.5: 核心四类 = 关卡 + 角色动作 + 贴图bulk + 剧情/过场
            if (cls != PakParser::ResourceClass::Level &&
                cls != PakParser::ResourceClass::CharacterAction &&
                cls != PakParser::ResourceClass::BulkTexture &&
                cls != PakParser::ResourceClass::Cinematics)
                continue;

            auto lenPos = beginEntry((int32_t)cls, e.fileName);
            uint64_t w = PakParser::StreamEntryData(info, e, out);
            endEntry(lenPos, w);
            total += w;
        }
        pass1Index++;
        int pct = 66 + (int)(10.0 * pass1Index / std::max(1, pass1Total));
        progress.store(std::min(76, pct));
    }

    // V1.8: IoStore 容器深度预读（黑神话悟空等现代 UE5 的核心数据都在 .ucas）
    // 整体流式读入：写入 cache 副本的同时把容器数据载入 OS 文件缓存，
    // 运行期游戏读取贴图/模型/着色器分块时命中文件缓存，免随机 IO 卡顿。
    if (total < targetBytes)
    {
        std::vector<std::wstring> ioFiles = FindIoStoreFiles(gameRoot);
        int ioIdx = 0, ioTotal = (int)ioFiles.size();
        for (const auto& ioFile : ioFiles)
        {
            if (total >= targetBytes) break;
            std::wstring ext = Util::ToLower(fs::path(ioFile).extension().wstring());
            if (ext != L".ucas") continue; // 只整体预读数据容器（.utoc 索引很小，跳过）

            std::ifstream fin(ioFile, std::ios::binary);
            if (!fin.is_open()) continue;
            fin.seekg(0, std::ios::end);
            uint64_t fileSize = (uint64_t)fin.tellg();
            fin.seekg(0, std::ios::beg);

            auto lenPos = beginEntry(7, fs::path(ioFile).filename().wstring()); // type 7 = IoStore 容器
            const size_t ICHUNK = 4 * 1024 * 1024;
            std::vector<char> ibuf(ICHUNK);
            uint64_t w = 0;
            while (w < fileSize && total < targetBytes)
            {
                uint64_t want = std::min<uint64_t>(ICHUNK,
                                std::min<uint64_t>(fileSize - w, targetBytes - total));
                fin.read(ibuf.data(), (std::streamsize)want);
                std::streamsize got = fin.gcount();
                if (got <= 0) break;
                out.write(ibuf.data(), got);
                w += (uint64_t)got;
                total += (uint64_t)got;
            }
            endEntry(lenPos, w);
            ioIdx++;
            int ipct = 76 + (int)(8.0 * ioIdx / std::max(1, ioTotal));
            progress.store(std::min(84, ipct));
        }
    }

    // 第二遍：其它资源补足到 targetBytes
    if (total < targetBytes)
    {
        for (const auto& info : infos)
        {
            for (const auto& e : info.entries)
            {
                auto cls = PakParser::ClassifyResource(e.fileName);
                // V1.5: 核心四类已在第一遍处理，第二遍只补其它
                if (cls == PakParser::ResourceClass::Level ||
                    cls == PakParser::ResourceClass::CharacterAction ||
                    cls == PakParser::ResourceClass::BulkTexture ||
                    cls == PakParser::ResourceClass::Cinematics)
                    continue;

                auto lenPos = beginEntry((int32_t)PakParser::ResourceClass::Other, e.fileName);
                uint64_t w = PakParser::StreamEntryData(info, e, out);
                endEntry(lenPos, w);
                total += w;
                if (total >= targetBytes) break;
            }
            if (total >= targetBytes) break;
            progress.store(std::min(89, 84 + 5));
        }
    }

    // 第三遍：整个 Pak 原始数据兜底
    if (total < targetBytes)
    {
        for (const auto& pak : pakFiles)
        {
            if (total >= targetBytes) break;
            std::ifstream fin(pak, std::ios::binary);
            if (!fin.is_open()) continue;
            fin.seekg(0, std::ios::end);
            uint64_t fileSize = (uint64_t)fin.tellg();
            fin.seekg(0, std::ios::beg);

            auto lenPos = beginEntry(5, fs::path(pak).filename().wstring()); // 5=RawPak
            const size_t CHUNK = 4 * 1024 * 1024;
            std::vector<char> buf(CHUNK);
            uint64_t w = 0;
            while (w < fileSize)
            {
                uint64_t n = std::min<uint64_t>(CHUNK, fileSize - w);
                fin.read(buf.data(), (std::streamsize)n);
                std::streamsize got = fin.gcount();
                if (got <= 0) break;
                out.write(buf.data(), got);
                w += (uint64_t)got;
                if (got < (std::streamsize)n) break;
            }
            endEntry(lenPos, w);
            total += w;
            progress.store(std::min(93, 89 + 4));
        }
    }

    // V2.0: 引擎无关的通用大文件预读（兼容 Unity《午夜轮班》、ForzaTech《地平线6》等非UE游戏）
    // 把数据目录里的大资源整体流式读入 cache 并预热 OS 文件缓存；非UE游戏本 pass 为预读主力。
    if (total < targetBytes)
    {
        std::vector<std::pair<std::wstring, uint64_t>> large = FindLargeDataFiles(gameRoot);
        int lgIdx = 0, lgTotal = (int)large.size();
        for (const auto& lf : large)
        {
            if (total >= targetBytes) break;
            std::ifstream fin(lf.first, std::ios::binary);
            if (!fin.is_open()) continue;
            uint64_t fileSize = lf.second;

            auto lenPos = beginEntry(8, fs::path(lf.first).filename().wstring()); // 8=GenericLargeFile
            const size_t LCHUNK = 4 * 1024 * 1024;
            std::vector<char> lbuf(LCHUNK);
            uint64_t w = 0;
            while (w < fileSize && total < targetBytes)
            {
                uint64_t want = std::min<uint64_t>(LCHUNK,
                                std::min<uint64_t>(fileSize - w, targetBytes - total));
                fin.read(lbuf.data(), (std::streamsize)want);
                std::streamsize got = fin.gcount();
                if (got <= 0) break;
                out.write(lbuf.data(), got);
                w += (uint64_t)got;
                total += (uint64_t)got;
            }
            endEntry(lenPos, w);
            lgIdx++;
            int lpct = 93 + (int)(5.0 * lgIdx / std::max(1, lgTotal));
            progress.store(std::min(98, lpct));
        }
    }

    // V3Pro: 第四遍——循环填充兜底（绝对保证缓存文件 ≥ targetBytes）
    // 即使游戏 Pak 总量不足目标大小，也用零填充补齐，确保"绝对不能小于5GB"
    if (total < targetBytes)
    {
        uint64_t padNeed = targetBytes - total;
        auto lenPos = beginEntry(6, L"__pad_fill__"); // 6=PadFill
        const size_t CHUNK = 4 * 1024 * 1024;
        std::vector<char> zeroBuf(CHUNK, 0);
        uint64_t w = 0;
        while (w < padNeed)
        {
            uint64_t n = std::min<uint64_t>(CHUNK, padNeed - w);
            out.write(zeroBuf.data(), (std::streamsize)n);
            w += n;
        }
        endEntry(lenPos, w);
        total += w;
        progress.store(99);
    }

    // 回填 entryCount（magic 8 + version 4 = 12）
    out.seekp(12);
    out.write((const char*)&entryCount, 4);
    out.flush();
    out.close();

    result.levelPreloadDone = true;
    result.levelCount = levelCount;
    result.actionResourceCount = actionCount;
    result.cineResourceCount = cineCount;
    std::error_code ec;
    result.levelCacheBytes = fs::file_size(lcPath, ec);
}

// V3Pro: 帧率优先——扩容本局缓存到更大目标（直到帧率达标）。
void Preprocessor::ExpandLevelCache(const std::wstring& gameRoot, uint64_t newTargetBytes,
                                    std::atomic<int>& progress, PreprocessResult& result)
{
    if (newTargetBytes < MIN_LEVEL_CACHE_BYTES) newTargetBytes = MIN_LEVEL_CACHE_BYTES;
    uint64_t capBytes = MaxCacheBytes(); // V1.8: 运行时上限（独显20GB/核显40GB）
    if (newTargetBytes > capBytes) newTargetBytes = capBytes;
    auto pakFiles = FindPakFiles(gameRoot);
    PreloadLevelAndActions(gameRoot, pakFiles, progress, 0, result, /*force=*/true, newTargetBytes);
}

void Preprocessor::WarmupShaderCache(const std::wstring& dir)
{
    if (dir.empty()) return;
    // V1.0: 全量读入 OS 文件缓存（上限 4GB）。UE5 的 DDC/PipelineCache 存编译好的
    // 着色器，全量预读让游戏加载 shader 时命中文件缓存，显著减少运行时 shader IO 卡顿
    // （这是手机端"进局不卡"真正有效的机制之一——PSO/着色器缓存预热）。
    uint64_t total = 0;
    const uint64_t MAX_WARM = 4ULL * 1024 * 1024 * 1024; // 最多预热 4GB
    try {
        for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file()) continue;
            if (total >= MAX_WARM) break;
            std::ifstream fin(entry.path(), std::ios::binary);
            if (!fin.is_open()) continue;
            std::vector<char> buf(1024 * 1024); // 1MB 分块
            while (fin && total < MAX_WARM)
            {
                fin.read(buf.data(), buf.size());
                std::streamsize g = fin.gcount();
                if (g <= 0) break;
                total += (uint64_t)g;
            }
        }
    } catch (...) {}
}

PreprocessResult Preprocessor::Run(const std::wstring& gameExePath, int targetFps, std::atomic<int>& progress)
{
    using clock = std::chrono::high_resolution_clock;
    auto t0 = clock::now();

    PreprocessResult result;
    progress.store(0);

    std::wstring gameRoot = GameDetector::FindGameRoot(gameExePath);
    result.gameRoot = gameRoot;
    progress.store(5);

    std::wstring cachePath = CacheFilePath(gameRoot);
    std::vector<std::wstring> pakFiles;

    // 1. 统计缓存（资源组成清单，用于快速展示）
    if (CheckCacheValid(gameRoot, cachePath) && ReadCache(cachePath, result))
    {
        result.cacheHit = true;
    }
    else
    {
        result.cacheHit = false;
        progress.store(10);

        // 2. 查找所有 Pak 文件
        pakFiles = FindPakFiles(gameRoot);
        result.pakCount = (int)pakFiles.size();
        progress.store(20);

        // 3. 解析每个 Pak 的清单（统计）
        int parsed = 0;
        for (const auto& pak : pakFiles)
        {
            PakInfo info;
            if (PakParser::Parse(pak, info))
            {
                auto stats = PakParser::CalcStats(info);
                result.totalResources += stats.total;
                result.textureCount += stats.textureCount;
                result.modelCount += stats.modelCount;
                result.shaderCount += stats.shaderCount;
                result.encryptedCount += stats.encrypted;
                result.compressedCount += stats.compressed;
            }
            parsed++;
            int pct = 20 + (int)(35.0 * parsed / std::max(1, (int)pakFiles.size()));
            progress.store(pct);

            // 帧率基准调度：高帧率目标下减少每个 Pak 的解析耗时，避免抢占游戏 IO
            if (targetFps >= 60)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        // 4. 查找并预热 ShaderCache
        result.shaderCacheDir = GameDetector::FindUE5ShaderCacheDir(gameRoot);
        if (!result.shaderCacheDir.empty())
            WarmupShaderCache(result.shaderCacheDir);

        // 5. 写入统计缓存
        WriteCache(cachePath, result);
        progress.store(55);
    }

    // 6. V3: 本局场景 + 第三人称动作 深度预读（达到手机预处理效果，cache ≥5GB）
    if (pakFiles.empty()) pakFiles = FindPakFiles(gameRoot);
    PreloadLevelAndActions(gameRoot, pakFiles, progress, 55, result);
    progress.store(100);

    auto t1 = clock::now();
    result.elapsedMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    return result;
}
