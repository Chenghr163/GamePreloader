#include "FloatingWindow.h"
#include "Config.h"
#include <shellapi.h>
#include <commdlg.h>
#include <windowsx.h>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")

#define WM_TRAYMSG (WM_USER + 100)

FloatingWindow::FloatingWindow() = default;
FloatingWindow::~FloatingWindow()
{
    if (m_hwnd) DestroyWindow(m_hwnd);
}

bool FloatingWindow::Create(HINSTANCE hInst, HWND parent)
{
    m_hInst = hInst;

    const wchar_t* className = L"GamePreloaderFloatWnd";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    RegisterClassExW(&wc);

    POINT pos = Config::Instance().GetWindowPos();

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | (m_alwaysOnTop ? WS_EX_TOPMOST : 0),
        className, L"GamePreloader",
        WS_POPUP,
        pos.x, pos.y, WIN_W, WIN_H,
        parent, nullptr, hInst, this);

    if (!m_hwnd) return false;

    // 半透明
    SetLayeredWindowAttributes(m_hwnd, 0, 235, LWA_ALPHA);

    // 创建托盘图标
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYMSG;
    nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wcscpy_s(nid.szTip, L"GamePreloader 游戏加载预处理");
    Shell_NotifyIconW(NIM_ADD, &nid);

    return true;
}

void FloatingWindow::Show()
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOW);
    m_visible = true;
    Refresh();
}

void FloatingWindow::Hide()
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
}

void FloatingWindow::SetAlwaysOnTop(bool on)
{
    m_alwaysOnTop = on;
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Config::Instance().SetAlwaysOnTop(on);
}

void FloatingWindow::Refresh()
{
    if (m_hwnd && m_visible)
        InvalidateRect(m_hwnd, nullptr, TRUE);
}

LRESULT CALLBACK FloatingWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FloatingWindow* pThis = nullptr;
    if (msg == WM_CREATE)
    {
        auto* cs = (CREATESTRUCTW*)lParam;
        pThis = (FloatingWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = (FloatingWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (pThis) return pThis->HandleMsg(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT FloatingWindow::HandleMsg(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        OnPaint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        m_dragging = true;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(m_hwnd, &pt);
        RECT rc; GetWindowRect(m_hwnd, &rc);
        m_dragOffset.x = pt.x - rc.left;
        m_dragOffset.y = pt.y - rc.top;
        SetCapture(m_hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (m_dragging)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(m_hwnd, &pt);
            SetWindowPos(m_hwnd, nullptr,
                pt.x - m_dragOffset.x, pt.y - m_dragOffset.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (m_dragging)
        {
            m_dragging = false;
            ReleaseCapture();
            RECT rc; GetWindowRect(m_hwnd, &rc);
            Config::Instance().SetWindowPos(POINT{ rc.left, rc.top });
        }
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(m_hwnd, &pt);
        OnContextMenu(pt.x, pt.y);
        return 0;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDM_HIDE: Hide(); return 0;
        case IDM_SHOW: Show(); return 0;
        case IDM_OPEN_CONFIG:
        {
            // V2: 配置文件移动到 config/ 子目录
            std::wstring cfg = Util::GetConfigPath();
            ShellExecuteW(nullptr, L"open", cfg.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case IDM_ADD_GAME:
        {
            // V2: 弹出 Windows 文件选择对话框，选择游戏 exe，无需改 config.ini
            ShowAddGameDialog();
            return 0;
        }
        case IDM_FPS_30: g_state.targetFps.store(30); Config::Instance().SetTargetFps(30); Refresh(); return 0;
        case IDM_FPS_60: g_state.targetFps.store(60); Config::Instance().SetTargetFps(60); Refresh(); return 0;
        case IDM_FPS_90: g_state.targetFps.store(90); Config::Instance().SetTargetFps(90); Refresh(); return 0;
        case IDM_FPS_120: g_state.targetFps.store(120); Config::Instance().SetTargetFps(120); Refresh(); return 0;
        case IDM_PIN_TOGGLE: SetAlwaysOnTop(!m_alwaysOnTop); Refresh(); return 0;
        case IDM_EXIT:
        {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = m_hwnd;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            // V1.7: 退出程序时自动清除所有缓存文件（preload_*.cache）
            try {
                std::wstring cacheDir = Util::GetCacheDir();
                for (auto& entry : fs::directory_iterator(cacheDir))
                {
                    if (entry.is_regular_file())
                    {
                        std::wstring ext = entry.path().extension().wstring();
                        std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
                        if (ext == L".cache")
                            fs::remove(entry.path());
                    }
                }
            } catch (...) {}
            PostQuitMessage(0);
            return 0;
        }
        }
        return 0;
    }
    case WM_TRAYMSG:
    {
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK)
        {
            Toggle();
        }
        else if (lParam == WM_RBUTTONUP)
        {
            POINT pt; GetCursorPos(&pt);
            OnContextMenu(pt.x, pt.y);
        }
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void FloatingWindow::OnPaint(HDC hdc)
{
    RECT rc; GetClientRect(m_hwnd, &rc);

    // 背景：深色圆角矩形
    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 38));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(80, 80, 100));
    HBRUSH oldBg = (HBRUSH)SelectObject(hdc, bg);
    HPEN oldPen = (HPEN)SelectObject(hdc, border);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldBg);
    SelectObject(hdc, oldPen);
    DeleteObject(bg);
    DeleteObject(border);

    // 文字颜色
    SetTextColor(hdc, RGB(230, 230, 240));
    SetBkMode(hdc, TRANSPARENT);

    // 标题
    HFONT titleFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HFONT oldFont = (HFONT)SelectObject(hdc, titleFont);
    TextOutW(hdc, 12, 8, L"GamePreloader", 13);

    HFONT textFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    SelectObject(hdc, textFont);

    int y = 32;
    wchar_t line[256];

    // 状态
    RunState st = g_state.state.load();
    const wchar_t* stateStr = L"空闲";
    COLORREF stateColor = RGB(150, 150, 160);
    switch (st)
    {
    case RunState::Idle: stateStr = L"等待游戏启动"; stateColor = RGB(150, 150, 160); break;
    case RunState::GameDetected: stateStr = L"检测到游戏进程"; stateColor = RGB(100, 200, 255); break;
    case RunState::Loading: stateStr = L"加载中·预处理"; stateColor = RGB(255, 200, 80); break;
    case RunState::Preprocessed: stateStr = L"预处理完成·运行中"; stateColor = RGB(100, 230, 130); break;
    case RunState::Error: stateStr = L"出错"; stateColor = RGB(255, 100, 100); break;
    }
    SetTextColor(hdc, stateColor);
    swprintf_s(line, L"状态: %s", stateStr);
    TextOutW(hdc, 12, y, line, (int)wcslen(line));
    y += 20;

    SetTextColor(hdc, RGB(200, 200, 210));
    // DX 版本
    DxVersion dx = g_state.dxVer.load();
    const wchar_t* dxStr = L"未识别";
    if (dx == DxVersion::DX11) dxStr = L"DirectX 11";
    else if (dx == DxVersion::DX12) dxStr = L"DirectX 12";
    swprintf_s(line, L"渲染: %s%s", dxStr, g_state.isUE5.load() ? L" (UE5)" : L"");
    TextOutW(hdc, 12, y, line, (int)wcslen(line));
    y += 20;

    // 进度
    int prog = g_state.progress.load();
    swprintf_s(line, L"进度: %d%%", prog);
    TextOutW(hdc, 12, y, line, (int)wcslen(line));
    // 进度条
    HBRUSH barBg = CreateSolidBrush(RGB(60, 60, 70));
    HBRUSH barFg = CreateSolidBrush(RGB(100, 200, 130));
    RECT barRc{ 12, y + 16, WIN_W - 12, y + 22 };
    FillRect(hdc, &barRc, barBg);
    RECT fgRc = barRc;
    fgRc.right = barRc.left + (LONG)((barRc.right - barRc.left) * prog / 100);
    FillRect(hdc, &fgRc, barFg);
    DeleteObject(barBg);
    DeleteObject(barFg);
    y += 28;

    // 缓存 + 帧率
    swprintf_s(line, L"缓存: %s | 目标: %dfps",
        g_state.cacheHit.load() ? L"命中" : L"未命中",
        g_state.targetFps.load());
    TextOutW(hdc, 12, y, line, (int)wcslen(line));
    y += 20;

    // V3: 本局场景+第三人称动作 预读缓存大小
    uint64_t lcb = g_state.levelCacheBytes.load();
    if (lcb > 0)
    {
        swprintf_s(line, L"本局缓存: %.2f GB", (double)lcb / (1024.0 * 1024.0 * 1024.0));
        SetTextColor(hdc, RGB(120, 220, 160));
        TextOutW(hdc, 12, y, line, (int)wcslen(line));
        SetTextColor(hdc, RGB(200, 200, 210));
    }

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(textFont);
}

// V2: 弹出 Windows 文件选择对话框（资源管理器），选择游戏 exe，无需手改 config.ini
void FloatingWindow::ShowAddGameDialog()
{
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Game Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择游戏主程序 (exe)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn))
    {
        std::wstring exePath(szFile);
        if (m_onAddGame) m_onAddGame(exePath);
    }
}

void FloatingWindow::OnContextMenu(int x, int y)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SHOW, L"显示悬浮窗");
    AppendMenuW(menu, MF_STRING, IDM_HIDE, L"隐藏悬浮窗");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_ADD_GAME, L"添加游戏（浏览文件）...");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG, L"打开配置文件");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU fpsMenu = CreatePopupMenu();
    int curFps = g_state.targetFps.load();
    UINT flag30 = (curFps == 30) ? MF_CHECKED : MF_STRING;
    UINT flag60 = (curFps == 60) ? MF_CHECKED : MF_STRING;
    UINT flag90 = (curFps == 90) ? MF_CHECKED : MF_STRING;
    UINT flag120 = (curFps == 120) ? MF_CHECKED : MF_STRING;
    AppendMenuW(fpsMenu, flag30, IDM_FPS_30, L"30 fps");
    AppendMenuW(fpsMenu, flag60, IDM_FPS_60, L"60 fps");
    AppendMenuW(fpsMenu, flag90, IDM_FPS_90, L"90 fps");
    AppendMenuW(fpsMenu, flag120, IDM_FPS_120, L"120 fps");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)fpsMenu, L"预处理帧率基准");

    AppendMenuW(menu, m_alwaysOnTop ? MF_CHECKED : MF_STRING, IDM_PIN_TOGGLE, L"窗口置顶");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出程序");

    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}
