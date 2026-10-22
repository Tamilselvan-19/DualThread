#include "LoginDlg.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "ChatClient.h"
#include "ChatSession.h"
#include "../Common/Ini.h"

namespace {
    CString FindClientConfig() {
        wchar_t module[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0) return CString();
        CString path(module);
        int slash = path.ReverseFind(L'\\');
        CString directory = slash >= 0 ? path.Left(slash) : CString(L".");
        const wchar_t* candidates[] = {
            L"\\config\\client.ini",
            L"\\..\\config\\client.ini",
            L"\\..\\..\\config\\client.ini",
            L"\\..\\..\\..\\config\\client.ini"
        };
        for (const wchar_t* candidate : candidates) {
            CString candidatePath = directory + candidate;
            DWORD attributes = GetFileAttributesW(candidatePath);
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                return candidatePath;
            }
        }
        return CString();
    }

    void DrawTextLine(CDC& dc, const CString& text, const CRect& rect, CFont& font,
                      COLORREF color, UINT format) {
        CFont* old = dc.SelectObject(&font);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(color);
        CRect target = rect;
        dc.DrawText(text, &target, format | DT_NOPREFIX);
        dc.SelectObject(old);
    }

    struct LoginContext {
        HWND hwnd = nullptr;
        std::shared_ptr<ChatSession> session;
        std::string host;
        int port = 5000;
        int udpPort = 5001;
        int heartbeatMs = 5000;
        std::string userId;
        std::string password;
    };

    struct LoginCompletion {
        bool success = false;
        std::string error;
        std::shared_ptr<ChatSession> session;
    };
}

BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_BN_CLICKED(IDC_LOGIN_BTN, OnSignIn)
    ON_BN_CLICKED(IDC_LOGIN_EXIT, OnExit)
    ON_WM_CLOSE()
    ON_MESSAGE(WM_APP_LOGIN_DONE, OnLoginDone)
END_MESSAGE_MAP()

CLoginDlg::CLoginDlg(std::shared_ptr<ChatSession>& outSession, CWnd* parent)
    : CDialogEx(IDD_LOGIN_DIALOG, parent), m_outSession(outSession) {
}

BOOL CLoginDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    SetWindowText(L"DualThread - Sign in");
    SetWindowPos(nullptr, 0, 0, 1000, 660, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    CreateFonts();
    m_fieldBrush.CreateSolidBrush(RGB(248, 250, 252));

    CRect empty(0, 0, 10, 10);
    m_server.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    empty, this, IDC_LOGIN_SERVERIP);
    m_port.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                  empty, this, IDC_LOGIN_PORT);
    m_user.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                  empty, this, IDC_LOGIN_USERID);
    m_password.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                      empty, this, IDC_LOGIN_PASSWORD);

    m_server.SetFont(&m_editFont);
    m_port.SetFont(&m_editFont);
    m_user.SetFont(&m_editFont);
    m_password.SetFont(&m_editFont);

    m_server.SetLimitText(64);
    m_port.SetLimitText(5);
    m_user.SetLimitText(64);
    m_password.SetLimitText(128);
    m_password.SetPasswordChar(L'*');

    const UINT marginFlags = EC_LEFTMARGIN | EC_RIGHTMARGIN;
    m_server.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));
    m_port.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));
    m_user.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));
    m_password.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));

    m_signIn.Create(L"Sign in", CRect(0, 0, 10, 10), this, IDC_LOGIN_BTN);
    m_signIn.SetColors(ui::Accent, ui::AccentHover, RGB(255, 255, 255));
    m_signIn.SetRadius(10);
    m_signIn.SetFont(&m_editFont);

    m_exit.Create(L"Exit", CRect(0, 0, 10, 10), this, IDC_LOGIN_EXIT);
    m_exit.SetColors(RGB(255, 255, 255), RGB(241, 245, 249), ui::Muted);
    m_exit.SetRadius(9);
    m_exit.SetFont(&m_editFont);

    LayoutControls();
    LoadDefaults();
    SetStatus(m_status, false);

    if (m_user.GetWindowTextLength() == 0) {
        m_user.SetFocus();
    } else {
        m_password.SetFocus();
    }
    return FALSE;
}

void CLoginDlg::DoDataExchange(CDataExchange* dataExchange) {
    CDialogEx::DoDataExchange(dataExchange);
}

void CLoginDlg::CreateFonts() {
    CDC* screen = GetDC();
    int dpi = GetDeviceCaps(screen->GetSafeHdc(), LOGPIXELSY);
    ReleaseDC(screen);
    if (dpi <= 0) dpi = 96;
    auto create = [dpi](CFont& font, int points, int weight) {
        font.CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };

    create(m_brandFont, 22, FW_BOLD);
    create(m_brandSubFont, 9, FW_NORMAL);
    create(m_logoFont, 17, FW_BOLD);
    create(m_titleFont, 21, FW_SEMIBOLD);
    create(m_subtitleFont, 9, FW_NORMAL);
    create(m_labelFont, 8, FW_SEMIBOLD);
    create(m_editFont, 10, FW_NORMAL);
    create(m_featureFont, 9, FW_NORMAL);
    create(m_statusFont, 8, FW_NORMAL);
    create(m_footerFont, 7, FW_NORMAL);
}

void CLoginDlg::LayoutControls() {
    CRect client;
    GetClientRect(&client);
    if (client.Width() < 500) client.SetRect(0, 0, 984, 621);

    const int left = 400;
    const int right = client.right - 40;
    const int portWidth = 140;
    const int gap = 12;
    const int serverWidth = right - left - gap - portWidth;

    m_serverRect = CRect(left, 154, left + serverWidth, 196);
    m_portRect = CRect(left + serverWidth + gap, 154, right, 196);
    m_userRect = CRect(left, 242, right, 284);
    m_passwordRect = CRect(left, 330, right, 372);

    m_server.MoveWindow(m_serverRect);
    m_port.MoveWindow(m_portRect);
    m_user.MoveWindow(m_userRect);
    m_password.MoveWindow(m_passwordRect);
    m_signIn.MoveWindow(CRect(left, 404, right, 450));
    m_exit.MoveWindow(CRect(left, 462, left + 96, 496));
    Invalidate();
}

void CLoginDlg::LoadDefaults() {
    CString path = FindClientConfig();
    std::string narrowPath = ui::ToUtf8(path);
    m_serverText = L"127.0.0.1";
    m_portText = L"5000";
    if (!narrowPath.empty()) {
        m_serverText = ui::FromUtf8(IniFile::ReadValue(
            narrowPath, "client", "server_ip", "127.0.0.1"));
        int tcpPort = IniFile::ReadInt(narrowPath, "client", "tcp_port", 5000);
        m_udpPort = IniFile::ReadInt(narrowPath, "client", "udp_port", 5001);
        m_heartbeatMs = IniFile::ReadInt(
            narrowPath, "client", "heartbeat_interval_ms", 5000);
        m_portText.Format(L"%d", tcpPort);
    }
    if (m_heartbeatMs < 1000) m_heartbeatMs = 5000;
    m_server.SetWindowText(m_serverText);
    m_port.SetWindowText(m_portText);
    m_user.SetWindowText(L"");
    m_password.SetWindowText(L"");
}

void CLoginDlg::SetStatus(const CString& text, bool error) {
    m_status = text;
    m_statusError = error;
    Invalidate();
}

void CLoginDlg::SetFormEnabled(bool enabled) {
    m_server.EnableWindow(enabled);
    m_port.EnableWindow(enabled);
    m_user.EnableWindow(enabled);
    m_password.EnableWindow(enabled);
    m_signIn.EnableWindow(enabled);
    m_exit.EnableWindow(enabled);
}

void CLoginDlg::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    if (client.IsRectEmpty()) return;

    const int brandWidth = 360;
    {
        Gdiplus::Graphics graphics(dc.GetSafeHdc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::LinearGradientBrush brush(
            Gdiplus::Rect(0, 0, brandWidth, client.Height()),
            Gdiplus::Color(255, 15, 23, 42),
            Gdiplus::Color(255, 49, 46, 129), 90.0f);
        graphics.FillRectangle(&brush, 0, 0, brandWidth, client.Height());
    }
    dc.FillSolidRect(CRect(brandWidth, 0, client.right, client.bottom),
                     RGB(255, 255, 255));

    // Brand block
    CRect logo(40, 54, 92, 106);
    ui::FillRounded(dc, logo, 14, ui::Accent, CLR_NONE);
    DrawTextLine(dc, L"DT", logo, m_logoFont, RGB(255, 255, 255),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextLine(dc, L"DualThread", CRect(40, 124, 330, 162), m_brandFont,
                 RGB(255, 255, 255), DT_LEFT | DT_SINGLELINE);
    DrawTextLine(dc, L"Two threads. One conversation.",
                 CRect(40, 164, 330, 186), m_brandSubFont,
                 RGB(196, 181, 253), DT_LEFT | DT_SINGLELINE);

    DrawTextLine(dc, L"BUILT FOR REAL-TIME TEAMS",
                 CRect(40, 224, 330, 242), m_labelFont,
                 RGB(148, 163, 184), DT_LEFT | DT_SINGLELINE);

    const wchar_t* features[] = {
        L"Personal and group messaging",
        L"Live presence and typing signals",
        L"Automatic reconnection",
        L"Persistent history in MySQL"
    };
    int y = 254;
    for (const wchar_t* feature : features) {
        CRect dot(42, y + 7, 50, y + 15);
        ui::FillCircle(dc, dot, RGB(129, 140, 248));
        DrawTextLine(dc, feature, CRect(62, y, 330, y + 22), m_featureFont,
                     RGB(226, 232, 240), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += 30;
    }

    // A soft glass card behind the bottom brand note.
    CRect card(28, client.bottom - 96, brandWidth - 28, client.bottom - 30);
    {
        Gdiplus::Graphics graphics(dc.GetSafeHdc());
        Gdiplus::SolidBrush brush(Gdiplus::Color(38, 255, 255, 255));
        Gdiplus::GraphicsPath path;
        ui::AddRoundedRect(path,
            Gdiplus::RectF(static_cast<float>(card.left), static_cast<float>(card.top),
                           static_cast<float>(card.Width()), static_cast<float>(card.Height())),
            12.0f);
        graphics.FillPath(&brush, &path);
    }
    DrawTextLine(dc, L"TCP + UDP heartbeat, Kafka, Redis and",
                 CRect(44, client.bottom - 82, brandWidth - 28, client.bottom - 64),
                 m_footerFont, RGB(203, 213, 225),
                 DT_LEFT | DT_SINGLELINE);
    DrawTextLine(dc, L"MySQL behind one clean desktop client.",
                 CRect(44, client.bottom - 64, brandWidth - 28, client.bottom - 46),
                 m_footerFont, RGB(203, 213, 225),
                 DT_LEFT | DT_SINGLELINE);

    // Sign-in panel
    const int left = 400;
    const int right = client.right - 40;
    DrawTextLine(dc, L"Welcome back", CRect(left, 54, right, 90),
                 m_titleFont, ui::Text, DT_LEFT | DT_SINGLELINE);
    DrawTextLine(dc, L"Sign in to continue to your conversations.",
                 CRect(left, 94, right, 116), m_subtitleFont, ui::Muted,
                 DT_LEFT | DT_SINGLELINE);

    auto drawField = [&](const CRect& rect) {
        ui::FillRounded(dc, rect, 9, RGB(248, 250, 252), ui::Border);
    };
    drawField(m_serverRect);
    drawField(m_portRect);
    drawField(m_userRect);
    drawField(m_passwordRect);

    DrawTextLine(dc, L"SERVER ADDRESS", CRect(m_serverRect.left, 132, m_serverRect.right, 150),
                 m_labelFont, ui::Muted, DT_LEFT | DT_SINGLELINE);
    DrawTextLine(dc, L"PORT", CRect(m_portRect.left, 132, m_portRect.right, 150),
                 m_labelFont, ui::Muted, DT_LEFT | DT_SINGLELINE);
    DrawTextLine(dc, L"USER ID", CRect(m_userRect.left, 220, m_userRect.right, 238),
                 m_labelFont, ui::Muted, DT_LEFT | DT_SINGLELINE);
    DrawTextLine(dc, L"PASSWORD", CRect(m_passwordRect.left, 308, m_passwordRect.right, 326),
                 m_labelFont, ui::Muted, DT_LEFT | DT_SINGLELINE);

    DrawTextLine(dc, m_status, CRect(left, 508, right, 550),
                 m_statusFont, m_statusError ? ui::Danger : ui::Muted,
                 DT_LEFT | DT_WORDBREAK);
    DrawTextLine(dc, L"DualThread Desktop 2.0", CRect(left, client.bottom - 40, right, client.bottom - 22),
                 m_footerFont, RGB(148, 163, 184),
                 DT_LEFT | DT_SINGLELINE);
}

BOOL CLoginDlg::OnEraseBkgnd(CDC*) {
    return TRUE;
}

HBRUSH CLoginDlg::OnCtlColor(CDC* dc, CWnd* window, UINT controlColor) {
    HBRUSH brush = CDialogEx::OnCtlColor(dc, window, controlColor);
    if (!window) return brush;
    const UINT id = window->GetDlgCtrlID();
    if (id == IDC_LOGIN_SERVERIP || id == IDC_LOGIN_PORT ||
        id == IDC_LOGIN_USERID || id == IDC_LOGIN_PASSWORD) {
        dc->SetTextColor(ui::Text);
        dc->SetBkColor(RGB(248, 250, 252));
        return static_cast<HBRUSH>(m_fieldBrush.GetSafeHandle());
    }
    return brush;
}

void CLoginDlg::OnSignIn() {
    if (m_connecting) return;

    CString server;
    CString portText;
    CString user;
    CString password;
    m_server.GetWindowText(server);
    m_port.GetWindowText(portText);
    m_user.GetWindowText(user);
    m_password.GetWindowText(password);
    server.Trim();
    portText.Trim();
    user.Trim();

    if (server.IsEmpty()) {
        SetStatus(L"Enter the server address.", true);
        m_server.SetFocus();
        return;
    }
    wchar_t* end = nullptr;
    long port = wcstol(portText, &end, 10);
    if (portText.IsEmpty() || (end && *end != L'\0') || port < 1 || port > 65535) {
        SetStatus(L"Enter a valid TCP port between 1 and 65535.", true);
        m_port.SetFocus();
        return;
    }
    if (user.IsEmpty()) {
        SetStatus(L"Enter your user ID.", true);
        m_user.SetFocus();
        return;
    }
    if (user.GetLength() > 64) {
        SetStatus(L"User ID must be 64 characters or fewer.", true);
        m_user.SetFocus();
        return;
    }
    if (password.IsEmpty()) {
        SetStatus(L"Enter your password.", true);
        m_password.SetFocus();
        return;
    }

    auto session = std::make_shared<ChatSession>();
    auto* context = new LoginContext();
    context->hwnd = GetSafeHwnd();
    context->session = session;
    context->host = ui::ToUtf8(server);
    context->port = static_cast<int>(port);
    context->udpPort = m_udpPort > 0 ? m_udpPort : static_cast<int>(port) + 1;
    context->heartbeatMs = m_heartbeatMs;
    context->userId = ui::ToUtf8(user);
    context->password = ui::ToUtf8(password);

    m_connecting = true;
    SetFormEnabled(false);
    m_signIn.SetWindowText(L"Connecting...");
    SetStatus(L"Connecting to the DualThread server...", false);

    std::thread([context]() {
        std::string error;
        bool ok = context->session->ConnectBlocking(
            context->host, context->port, context->udpPort,
            context->userId, context->password, context->heartbeatMs, error);

        auto* completion = new LoginCompletion();
        completion->success = ok;
        completion->error = error;
        completion->session = context->session;
        if (!::PostMessageW(context->hwnd, WM_APP_LOGIN_DONE,
                            ok ? 1 : 0, reinterpret_cast<LPARAM>(completion))) {
            delete completion;
        }
        delete context;
    }).detach();
}

void CLoginDlg::OnExit() {
    if (m_connecting) return;
    EndDialog(IDCANCEL);
}

void CLoginDlg::OnClose() {
    if (m_connecting) {
        SetStatus(L"Still signing in. Please wait for the connection attempt to finish.", false);
        return;
    }
    EndDialog(IDCANCEL);
}

LRESULT CLoginDlg::OnLoginDone(WPARAM success, LPARAM result) {
    std::unique_ptr<LoginCompletion> completion(
        reinterpret_cast<LoginCompletion*>(result));
    m_connecting = false;
    SetFormEnabled(true);
    m_signIn.SetWindowText(L"Sign in");

    if (success && completion && completion->success) {
        m_outSession = completion->session;
        EndDialog(IDOK);
        return 0;
    }

    CString error = completion && !completion->error.empty()
        ? ui::FromUtf8(completion->error)
        : CString(L"Could not sign in. Check the server address and try again.");
    SetStatus(error, true);
    m_password.SetFocus();
    m_password.SetSel(0, -1);
    return 0;
}
