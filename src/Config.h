#pragma once
// 配置管理：INI 格式，零外部依赖
#include "Common.h"
#include <string>
#include <unordered_map>

class Config
{
public:
    static Config& Instance();

    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

    // 游戏监控根目录（可配置多个）
    std::vector<std::wstring> GetWatchPaths() const;
    void  SetWatchPaths(const std::vector<std::wstring>& paths);
    // V2: 动态添加一个监控路径（去重），供"添加游戏"使用
    void  AddWatchPath(const std::wstring& path);

    // 目标帧率基准
    int   GetTargetFps() const;
    void  SetTargetFps(int fps);

    // 开机自启
    bool  GetAutoStart() const;
    void  SetAutoStart(bool on);

    // 悬浮窗置顶
    bool  GetAlwaysOnTop() const;
    void  SetAlwaysOnTop(bool on);

    // 悬浮窗位置
    POINT GetWindowPos() const;
    void  SetWindowPos(POINT pt);

    // 通用键值
    std::wstring GetString(const std::wstring& section, const std::wstring& key, const std::wstring& def = L"") const;
    int          GetInt(const std::wstring& section, const std::wstring& key, int def = 0) const;
    bool         GetBool(const std::wstring& section, const std::wstring& key, bool def = false) const;
    void         SetValue(const std::wstring& section, const std::wstring& key, const std::wstring& value);

private:
    Config() = default;
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, std::wstring>> m_data;
};
