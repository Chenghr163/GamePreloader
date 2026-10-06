#include "Common.h"
#include <shlobj.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <dxgi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dxgi.lib")

AppState g_state;

namespace Util
{
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
        return out;
    }

    std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len, nullptr, nullptr);
        return out;
    }

    std::wstring GetExeDir()
    {
        wchar_t buf[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring p(buf);
        auto pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p = p.substr(0, pos);
        return p;
    }

    std::wstring GetCacheDir()
    {
        std::wstring dir = GetExeDir() + L"\\cache";
        CreateDirRecursive(dir);
        return dir;
    }

    std::wstring GetLogDir()
    {
        std::wstring dir = GetExeDir() + L"\\logs";
        CreateDirRecursive(dir);
        return dir;
    }

    std::wstring GetConfigDir()
    {
        std::wstring dir = GetExeDir() + L"\\config";
        CreateDirRecursive(dir);
        return dir;
    }

    std::wstring GetConfigPath()
    {
        return GetConfigDir() + L"\\config.ini";
    }

    bool CreateDirRecursive(const std::wstring& path)
    {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return !ec;
    }

    bool PathStartsWith(const std::wstring& path, const std::wstring& prefixIn)
    {
        // V2: 去掉前缀末尾分隔符，再做目录边界匹配，防止误匹配同级路径
        std::wstring prefix = prefixIn;
        while (!prefix.empty() && (prefix.back() == L'\\' || prefix.back() == L'/')) prefix.pop_back();
        if (prefix.empty()) return false;
        if (prefix.size() > path.size()) return false;
        if (_wcsnicmp(path.c_str(), prefix.c_str(), prefix.size()) != 0) return false;
        // 边界：path 完全等于 prefix，或 prefix 后紧跟分隔符
        if (path.size() == prefix.size()) return true;
        return path[prefix.size()] == L'\\' || path[prefix.size()] == L'/';
    }

    bool IsProcessInWatchPath(const std::wstring& processPath, const std::wstring& watchRule)
    {
        // 精确 exe 规则：watchRule 以 .exe 结尾 -> 只匹配该 exe 全路径
        std::wstring ruleLower = ToLower(watchRule);
        if (ruleLower.size() >= 4 && ruleLower.substr(ruleLower.size() - 4) == L".exe")
        {
            return _wcsicmp(processPath.c_str(), watchRule.c_str()) == 0;
        }
        // 目录规则：目录边界前缀匹配
        return PathStartsWith(processPath, watchRule);
    }

    std::wstring ToLower(const std::wstring& s)
    {
        std::wstring r = s;
        std::transform(r.begin(), r.end(), r.begin(),
            [](wchar_t c) { return (wchar_t)towlower(c); });
        return r;
    }

    // V1.8: DXGI 枚举显示适配器，判断是否存在独立显卡
    bool HasDedicatedGpu()
    {
        IDXGIFactory* factory = nullptr;
        // CreateDXGIFactory 由 dxgi.lib 提供
        if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
            return true; // 检测失败，保守按有独显处理

        bool hasDedicated = false;
        const uint64_t TWO_GB = 2ULL * 1024 * 1024 * 1024;
        const uint64_t THREE_GB = 3ULL * 1024 * 1024 * 1024;

        IDXGIAdapter* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                // NVIDIA 0x10DE：基本都是独显
                if (desc.VendorId == 0x10DE) hasDedicated = true;
                // AMD 0x1002：独显独立显存通常较大，APU 核显共享内存
                else if (desc.VendorId == 0x1002 && desc.DedicatedVideoMemory >= TWO_GB) hasDedicated = true;
                // Intel 0x8086：核显/Arc，Arc 独显独立显存>=3GB 才视为独显
                else if (desc.VendorId == 0x8086 && desc.DedicatedVideoMemory >= THREE_GB) hasDedicated = true;
                // 其它厂商：独立显存>=3GB 视为独显
                else if (desc.VendorId != 0x8086 && desc.VendorId != 0x1002 &&
                         desc.DedicatedVideoMemory >= THREE_GB) hasDedicated = true;
            }
            adapter->Release();
            adapter = nullptr;
        }
        factory->Release();
        return hasDedicated;
    }
}
