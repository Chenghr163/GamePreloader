#pragma once
// 桌面悬浮窗：轻量化、可拖动、置顶、实时状态展示、右键菜单
#include "Common.h"
#include <windows.h>
#include <functional>

class FloatingWindow
{
public:
    // 菜单命令 ID
    enum MenuId
    {
        IDM_HIDE = 1001,
        IDM_SHOW,
        IDM_OPEN_CONFIG,
        IDM_ADD_GAME,
        IDM_FPS_30,
        IDM_FPS_60,
        IDM_FPS_90,
        IDM_FPS_120,
        IDM_PIN_TOGGLE,
        IDM_EXIT
    };

    // V2: 用户通过文件对话框添加游戏后触发的回调（传出选中的 exe 路径）
    using AddGameCallback = std::function<void(const std::wstring& exePath)>;
    void SetAddGameCallback(AddGameCallback cb) { m_onAddGame = std::move(cb); }

    FloatingWindow();
    ~FloatingWindow();

    bool Create(HINSTANCE hInst, HWND parent = nullptr);
    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }
    void Toggle() { m_visible ? Hide() : Show(); }

    void SetAlwaysOnTop(bool on);
    bool IsAlwaysOnTop() const { return m_alwaysOnTop; }

    HWND GetHwnd() const { return m_hwnd; }

    // 触发重绘（状态变化后调用）
    void Refresh();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMsg(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HDC hdc);
    void OnContextMenu(int x, int y);
    void OnDrag(int x, int y);
    // V2: 弹出 Windows 文件选择对话框，选择游戏 exe
    void ShowAddGameDialog();

    HWND     m_hwnd = nullptr;
    HINSTANCE m_hInst = nullptr;
    bool     m_visible = false;
    bool     m_alwaysOnTop = true;
    bool     m_dragging = false;
    POINT    m_dragOffset{ 0, 0 };
    AddGameCallback m_onAddGame;

    static constexpr int WIN_W = 220;
    static constexpr int WIN_H = 150;
};
