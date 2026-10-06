#include "Config.h"
#include <fstream>
#include <sstream>

Config& Config::Instance()
{
    static Config inst;
    return inst;
}

bool Config::Load(const std::wstring& path)
{
    std::wifstream fin(path);
    if (!fin.is_open()) return false;

    std::wstring line, section;
    while (std::getline(fin, line))
    {
        // 去首尾空白
        while (!line.empty() && (line.front() == L' ' || line.front() == L'\t' || line.front() == L'\r')) line.erase(line.begin());
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t' || line.back() == L'\r')) line.pop_back();
        if (line.empty() || line.front() == L';' || line.front() == L'#') continue;

        if (line.front() == L'[' && line.back() == L']')
        {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq);
        std::wstring v = line.substr(eq + 1);
        while (!k.empty() && (k.back() == L' ' || k.back() == L'\t')) k.pop_back();
        while (!v.empty() && (v.front() == L' ' || v.front() == L'\t')) v.erase(v.begin());
        m_data[section][k] = v;
    }
    return true;
}

bool Config::Save(const std::wstring& path) const
{
    std::wofstream fout(path);
    if (!fout.is_open()) return false;
    for (auto& [sec, kv] : m_data)
    {
        fout << L"[" << sec << L"]\n";
        for (auto& [k, v] : kv)
            fout << k << L"=" << v << L"\n";
        fout << L"\n";
    }
    return true;
}

std::vector<std::wstring> Config::GetWatchPaths() const
{
    std::vector<std::wstring> result;
    auto it = m_data.find(L"Watch");
    if (it == m_data.end()) return result;
    for (auto& [k, v] : it->second)
    {
        if (k.rfind(L"path", 0) == 0 && !v.empty())
            result.push_back(v);
    }
    return result;
}

void Config::SetWatchPaths(const std::vector<std::wstring>& paths)
{
    auto& sec = m_data[L"Watch"];
    // 清除旧的 path*
    for (auto it = sec.begin(); it != sec.end(); )
    {
        if (it->first.rfind(L"path", 0) == 0) it = sec.erase(it);
        else ++it;
    }
    for (size_t i = 0; i < paths.size(); ++i)
        sec[L"path" + std::to_wstring(i + 1)] = paths[i];
}

void Config::AddWatchPath(const std::wstring& path)
{
    // V2: 去重后追加（保留现有目录规则与精确 exe 规则）
    auto existing = GetWatchPaths();
    for (auto& p : existing)
        if (_wcsicmp(p.c_str(), path.c_str()) == 0) return; // 已存在
    existing.push_back(path);
    SetWatchPaths(existing);
}

int  Config::GetTargetFps() const { return GetInt(L"General", L"target_fps", 30); }
void Config::SetTargetFps(int fps) { SetValue(L"General", L"target_fps", std::to_wstring(fps)); }

bool Config::GetAutoStart() const { return GetBool(L"General", L"auto_start", false); }
void Config::SetAutoStart(bool on) { SetValue(L"General", L"auto_start", on ? L"1" : L"0"); }

bool Config::GetAlwaysOnTop() const { return GetBool(L"General", L"always_on_top", true); }
void Config::SetAlwaysOnTop(bool on) { SetValue(L"General", L"always_on_top", on ? L"1" : L"0"); }

POINT Config::GetWindowPos() const
{
    POINT pt{ 20, 20 };
    pt.x = GetInt(L"Window", L"x", 20);
    pt.y = GetInt(L"Window", L"y", 20);
    return pt;
}
void Config::SetWindowPos(POINT pt)
{
    SetValue(L"Window", L"x", std::to_wstring(pt.x));
    SetValue(L"Window", L"y", std::to_wstring(pt.y));
}

std::wstring Config::GetString(const std::wstring& section, const std::wstring& key, const std::wstring& def) const
{
    auto sit = m_data.find(section);
    if (sit == m_data.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    return kit->second;
}

int Config::GetInt(const std::wstring& section, const std::wstring& key, int def) const
{
    auto s = GetString(section, key, L"");
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

bool Config::GetBool(const std::wstring& section, const std::wstring& key, bool def) const
{
    auto s = GetString(section, key, L"");
    if (s == L"1" || s == L"true" || s == L"True" || s == L"TRUE") return true;
    if (s == L"0" || s == L"false" || s == L"False" || s == L"FALSE") return false;
    return def;
}

void Config::SetValue(const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
    m_data[section][key] = value;
}
