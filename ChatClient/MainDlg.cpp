#include "MainDlg.h"

#include <algorithm>
#include <memory>
#include <string>

#include "ChatClient.h"
#include "ChatSession.h"
#include "GroupDlg.h"
#include "../Common/JsonUtil.h"

using namespace ui;

namespace {
    void DrawTextLine(CDC& dc, const CString& text, const CRect& rect, CFont& font,
                      COLORREF color, UINT format) {
        CFont* old = dc.SelectObject(&font);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(color);
        CRect target = rect;
        dc.DrawText(text, &target, format | DT_NOPREFIX);
        dc.SelectObject(old);
    }

    int ToInt(const CString& text) {
        wchar_t* end = nullptr;
        long value = wcstol(text, &end, 10);
        if (!end || end == static_cast<LPCWSTR>(text)) return 0;
        return static_cast<int>(value);
    }

    bool SplitHeartbeat(const CString& text, CString& server, int& rtt, CString& state) {
        int first = text.Find(L'|');
        if (first < 0) return false;
        int second = text.Find(L'|', first + 1);
        if (second < 0) return false;
        server = text.Left(first);
        CString rttText = text.Mid(first + 1, second - first - 1);
        state = text.Mid(second + 1);
        rtt = rttText.IsEmpty() ? -1 : ToInt(rttText);
        return true;
    }
}

BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_BTN_SEND_MAIN, OnSendButton)
    ON_BN_CLICKED(IDC_BTN_NEWGROUP, OnNewGroup)
    ON_BN_CLICKED(ID_LOGOUT, OnLogout)
    ON_EN_CHANGE(IDC_EDIT_COMPOSE, OnComposeChanged)
    ON_MESSAGE(WM_APP_SEND_REQUESTED, OnSendRequested)
    ON_MESSAGE(WM_APP_CONVERSATION_SELECTED, OnConversationSelected)
    ON_MESSAGE(WM_APP_SESSION_FRAME, OnSessionFrame)
    ON_MESSAGE(WM_APP_SESSION_STATUS, OnSessionStatus)
    ON_MESSAGE(WM_APP_SESSION_HEARTBEAT, OnHeartbeat)
END_MESSAGE_MAP()

CMainDlg::CMainDlg(std::shared_ptr<ChatSession> session, CWnd* parent)
    : CDialogEx(IDD_MAIN_DIALOG, parent), m_session(std::move(session)) {
}

BOOL CMainDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    SetWindowPos(nullptr, 0, 0, 1200, 780, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    CreateFonts();
    m_composeBrush.CreateSolidBrush(RGB(248, 250, 252));

    CRect empty(0, 0, 10, 10);
    m_conversationsView.Create(this, IDC_CONV_LIST, empty);
    m_messageView.Create(this, IDC_MSG_VIEW, empty);
    m_compose.Create(this, IDC_EDIT_COMPOSE, empty);
    m_compose.SetFont(&m_editFont);

    m_sendButton.Create(L"Send", empty, this, IDC_BTN_SEND_MAIN);
    m_sendButton.SetColors(ui::Accent, ui::AccentHover, RGB(255, 255, 255));
    m_sendButton.SetRadius(10);
    m_sendButton.SetFont(&m_editFont);

    m_newGroupButton.Create(L"+ New group", empty, this, IDC_BTN_NEWGROUP);
    m_newGroupButton.SetColors(ui::SidebarRaised, ui::SidebarHover, ui::SidebarText);
    m_newGroupButton.SetRadius(8);
    m_newGroupButton.SetFont(&m_brandSubFont);

    m_logoutButton.Create(L"Sign out", empty, this, ID_LOGOUT);
    m_logoutButton.SetColors(ui::SidebarRaised, ui::SidebarHover, ui::SidebarText);
    m_logoutButton.SetRadius(8);
    m_logoutButton.SetFont(&m_brandSubFont);

    m_userId = ui::FromUtf8(m_session->UserId());
    m_serverId = ui::FromUtf8(m_session->ServerId());
    m_connected = m_session->IsConnected();
    m_rtt = m_session->RttMs();
    m_connectionText = m_connected ? CString(L"Connected") : CString(L"Connecting...");
    m_connectionError = !m_connected;

    SetWindowText(CString(L"DualThread - ") + m_userId);
    m_conversationsView.SetEmptyText(L"Loading conversations...");
    m_messageView.SetEmptyState(L"Select a conversation",
                                L"Choose someone from the list to start messaging.");
    m_compose.EnableWindow(TRUE);
    m_sendButton.EnableWindow(m_connected);

    LayoutControls();
    m_session->Start(m_hWnd);
    SetTimer(1, 1000, nullptr);
    return FALSE;
}

void CMainDlg::DoDataExchange(CDataExchange* dataExchange) {
    CDialogEx::DoDataExchange(dataExchange);
}

void CMainDlg::CreateFonts() {
    CDC* screen = GetDC();
    int dpi = GetDeviceCaps(screen->GetSafeHdc(), LOGPIXELSY);
    ReleaseDC(screen);
    if (dpi <= 0) dpi = 96;
    auto create = [dpi](CFont& font, int points, int weight) {
        font.CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    create(m_logoFont, 12, FW_BOLD);
    create(m_brandFont, 15, FW_SEMIBOLD);
    create(m_brandSubFont, 8, FW_NORMAL);
    create(m_userFont, 10, FW_SEMIBOLD);
    create(m_userSubFont, 8, FW_NORMAL);
    create(m_headerNameFont, 12, FW_SEMIBOLD);
    create(m_headerSubFont, 8, FW_NORMAL);
    create(m_pillFont, 8, FW_SEMIBOLD);
    create(m_hintFont, 7, FW_NORMAL);
    create(m_editFont, 10, FW_NORMAL);
}

void CMainDlg::LayoutControls() {
    CRect client;
    GetClientRect(&client);
    const int width = client.Width();
    const int height = client.Height();
    if (width < 700 || height < 500) return;

    const int sidebar = 320;
    const int header = 72;
    const int composer = 96;

    m_conversationsView.MoveWindow(CRect(8, 102, sidebar - 8, height - 92));
    m_newGroupButton.MoveWindow(CRect(18, 64, sidebar - 18, 94));
    m_logoutButton.MoveWindow(CRect(sidebar - 110, height - 68, sidebar - 18, height - 36));

    m_messageView.MoveWindow(CRect(sidebar, header, width, height - composer));

    CRect composeRect(sidebar + 20, height - composer + 22, width - 136, height - 20);
    m_compose.MoveWindow(composeRect);
    CRect formatRect(6, 7, (std::max)(20, composeRect.Width() - 12),
                     (std::max)(20, composeRect.Height() - 7));
    m_compose.SendMessage(EM_SETRECT, 0, reinterpret_cast<LPARAM>(&formatRect));

    m_sendButton.MoveWindow(CRect(width - 116, height - composer + 26,
                                  width - 20, height - composer + 70));
    InvalidateHeader();
    Invalidate();
}

void CMainDlg::OnSize(UINT type, int cx, int cy) {
    CDialogEx::OnSize(type, cx, cy);
    if (cx > 0 && cy > 0) LayoutControls();
}

void CMainDlg::OnGetMinMaxInfo(MINMAXINFO* info) {
    CDialogEx::OnGetMinMaxInfo(info);
    info->ptMinTrackSize.x = 980;
    info->ptMinTrackSize.y = 640;
}

BOOL CMainDlg::OnEraseBkgnd(CDC*) {
    return TRUE;
}

HBRUSH CMainDlg::OnCtlColor(CDC* dc, CWnd* window, UINT controlColor) {
    HBRUSH brush = CDialogEx::OnCtlColor(dc, window, controlColor);
    if (window && window->GetDlgCtrlID() == IDC_EDIT_COMPOSE) {
        dc->SetTextColor(ui::Text);
        dc->SetBkColor(RGB(248, 250, 252));
        return static_cast<HBRUSH>(m_composeBrush.GetSafeHandle());
    }
    return brush;
}

void CMainDlg::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    if (client.IsRectEmpty()) return;

    const int sidebar = 320;
    const int header = 72;
    const int composer = 96;
    const int width = client.Width();
    const int height = client.Height();

    dc.FillSolidRect(client, ui::WindowBg);
    dc.FillSolidRect(CRect(0, 0, sidebar, height), ui::Sidebar);

    // Brand
    CRect logo(18, 18, 56, 56);
    ui::FillRounded(dc, logo, 11, ui::Accent, CLR_NONE);
    DrawTextLine(dc, L"DT", logo, m_logoFont, RGB(255, 255, 255),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextLine(dc, L"DualThread", CRect(66, 14, sidebar - 12, 38),
                 m_brandFont, RGB(255, 255, 255),
                 DT_LEFT | DT_SINGLELINE);
    CString serverText = m_serverId.IsEmpty()
        ? CString(L"Real-time team messaging")
        : CString(L"Server ") + m_serverId;
    DrawTextLine(dc, serverText, CRect(66, 38, sidebar - 12, 56),
                 m_brandSubFont, ui::SidebarMuted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Profile
    dc.FillSolidRect(CRect(0, height - 88, sidebar, height - 87), RGB(45, 55, 72));
    CRect avatar(18, height - 72, 54, height - 36);
    ui::FillCircle(dc, avatar, ui::AvatarColor(m_userId));
    DrawTextLine(dc, ui::Initials(m_userId), avatar, m_userFont,
                 RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawTextLine(dc, m_userId, CRect(64, height - 72, sidebar - 118, height - 52),
                 m_userFont, RGB(255, 255, 255),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    CString profileStatus;
    if (m_connected) {
        profileStatus = m_rtt >= 0
            ? CString(L"Online  |  ") + CString(std::to_wstring(m_rtt).c_str()) + L" ms"
            : CString(L"Online");
    } else {
        profileStatus = L"Reconnecting...";
    }
    DrawTextLine(dc, profileStatus, CRect(64, height - 54, sidebar - 118, height - 34),
                 m_userSubFont, m_connected ? RGB(134, 239, 172) : RGB(252, 211, 77),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Header
    dc.FillSolidRect(CRect(sidebar, 0, width, header), RGB(255, 255, 255));
    dc.FillSolidRect(CRect(sidebar, header - 1, width, header), ui::Border);

    ConversationItem* selected = FindConversation(m_selectedId);
    if (selected) {
        CRect headerAvatar(sidebar + 20, 16, sidebar + 60, 56);
        COLORREF avatarColor = ui::AvatarColor(selected->name);
        if (selected->group) {
            ui::FillRounded(dc, headerAvatar, 12, avatarColor, CLR_NONE);
        } else {
            FillCircle(dc, headerAvatar, avatarColor);
        }
        DrawTextLine(dc, ui::Initials(selected->name), headerAvatar, m_headerNameFont,
                     RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!selected->group) {
            CRect ring(headerAvatar.right - 14, headerAvatar.bottom - 14,
                       headerAvatar.right + 1, headerAvatar.bottom + 1);
            FillCircle(dc, ring, RGB(255, 255, 255));
            CRect dot = ring;
            dot.DeflateRect(2, 2);
            FillCircle(dc, dot, selected->online ? ui::Online : RGB(148, 163, 184));
        }

        DrawTextLine(dc, selected->name,
                     CRect(sidebar + 72, 11, width - 210, 34),
                     m_headerNameFont, ui::Text,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        CString subtitle;
        COLORREF subtitleColor = ui::Muted;
        if (!m_typingName.IsEmpty()) {
            subtitle = m_typingName + L" is typing...";
            subtitleColor = ui::Accent;
        } else if (selected->group) {
            subtitle.Format(L"%d members", selected->memberCount);
            if (selected->memberCount <= 0) subtitle = L"Group conversation";
        } else {
            subtitle = selected->online ? L"Online" : L"Offline";
            subtitleColor = selected->online ? RGB(22, 163, 74) : ui::Muted;
        }
        DrawTextLine(dc, subtitle, CRect(sidebar + 72, 34, width - 210, 58),
                     m_headerSubFont, subtitleColor,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        DrawTextLine(dc, L"Welcome to DualThread",
                     CRect(sidebar + 22, 12, width - 200, 38),
                     m_headerNameFont, ui::Text, DT_LEFT | DT_SINGLELINE);
        DrawTextLine(dc, L"Select a conversation from the left to begin.",
                     CRect(sidebar + 22, 38, width - 200, 58),
                     m_headerSubFont, ui::Muted, DT_LEFT | DT_SINGLELINE);
    }

    CRect pill(width - 194, 22, width - 20, 50);
    COLORREF pillFill = RGB(220, 252, 231);
    COLORREF pillText = RGB(22, 101, 52);
    COLORREF pillDot = ui::Online;
    if (!m_connected) {
        pillFill = m_connectionError ? ui::DangerSoft : ui::WarnSoft;
        pillText = m_connectionError ? RGB(153, 27, 27) : ui::WarnText;
        pillDot = m_connectionError ? ui::Danger : RGB(217, 119, 6);
    }
    ui::FillRounded(dc, pill, 13, pillFill, CLR_NONE);
    CRect dot(pill.left + 14, pill.CenterPoint().y - 4,
              pill.left + 22, pill.CenterPoint().y + 4);
    FillCircle(dc, dot, pillDot);
    CString pillLabel = m_connectionText;
    if (m_connected && m_rtt >= 0) {
        pillLabel += L"  |  ";
        pillLabel += CString(std::to_wstring(m_rtt).c_str());
        pillLabel += L" ms";
    }
    DrawTextLine(dc, pillLabel, CRect(pill.left + 30, pill.top, pill.right - 10, pill.bottom),
                 m_pillFont, pillText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Composer surface
    int composerTop = height - composer;
    dc.FillSolidRect(CRect(sidebar, composerTop, width, height), RGB(255, 255, 255));
    dc.FillSolidRect(CRect(sidebar, composerTop, width, composerTop + 1), ui::Border);

    CRect composeField = m_compose.GetSafeHwnd()
        ? [&]() { CRect r; m_compose.GetWindowRect(&r); ScreenToClient(&r); r.InflateRect(1, 1); return r; }()
        : CRect(sidebar + 19, composerTop + 21, width - 135, height - 19);
    ui::FillRounded(dc, composeField, 10, RGB(248, 250, 252), ui::Border);

    if (!m_transient.IsEmpty()) {
        DrawTextLine(dc, m_transient,
                     CRect(sidebar + 22, composerTop + 3, width - 22, composerTop + 21),
                     m_hintFont, m_transientError ? ui::Danger : ui::Muted,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        DrawTextLine(dc, L"Enter to send   |   Shift+Enter for a new line",
                     CRect(sidebar + 22, composerTop + 3, width - 22, composerTop + 21),
                     m_hintFont, ui::Muted,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

void CMainDlg::OnTimer(UINT_PTR timerId) {
    if (timerId != 1) {
        CDialogEx::OnTimer(timerId);
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (!m_typingName.IsEmpty() && now >= m_typingExpiry) {
        ClearTyping();
    }
    if (!m_transient.IsEmpty() && now >= m_transientExpiry) {
        m_transient.Empty();
        Invalidate();
    }
    if (m_sentTyping && now - m_lastTypingChange > 3000) {
        SendTypingState(false);
    }
}

void CMainDlg::OnClose() {
    DoLogout(IDCANCEL);
}

void CMainDlg::OnDestroy() {
    KillTimer(1);
    if (m_session) m_session->Stop();
    CDialogEx::OnDestroy();
}

void CMainDlg::DoLogout(int result) {
    if (m_closing) return;
    m_closing = true;
    KillTimer(1);
    if (m_session) {
        m_session->SendFields({{"type", "logout"}});
        m_session->Stop();
    }
    EndDialog(result);
}

void CMainDlg::OnLogout() {
    DoLogout(ID_LOGOUT);
}

ConversationItem* CMainDlg::FindConversation(const CString& id) {
    for (auto& conversation : m_conversations) {
        if (conversation.id == id) return &conversation;
    }
    return nullptr;
}

bool CMainDlg::IsGroupSelected() const {
    for (const auto& conversation : m_conversations) {
        if (conversation.id == m_selectedId) return conversation.group;
    }
    return false;
}

void CMainDlg::EnsureConversation(const CString& id, const CString& name,
                                  bool group, int memberCount) {
    if (id.IsEmpty()) return;
    for (auto& conversation : m_conversations) {
        if (conversation.id == id) {
            if (group) {
                conversation.group = true;
                // A group chat or history frame may only carry the group id.
                // Never replace a friendly group name with that raw id.
                if (!name.IsEmpty() && !(name == id && conversation.name != id)) {
                    conversation.name = name;
                }
                if (memberCount > 0) conversation.memberCount = memberCount;
            } else {
                conversation.online = m_onlineUsers.count(id) > 0;
            }
            return;
        }
    }
    ConversationItem item;
    item.id = id;
    item.name = name.IsEmpty() ? id : name;
    item.group = group;
    item.memberCount = memberCount;
    item.online = !group && m_onlineUsers.count(id) > 0;
    m_conversations.push_back(item);
}

void CMainDlg::RebuildList() {
    for (auto& conversation : m_conversations) {
        if (!conversation.group) {
            conversation.online = m_onlineUsers.count(conversation.id) > 0;
        }
    }
    std::stable_sort(m_conversations.begin(), m_conversations.end(),
        [](const ConversationItem& left, const ConversationItem& right) {
            if (left.lastTick != right.lastTick) return left.lastTick > right.lastTick;
            if (left.group != right.group) return !left.group;
            return left.name.CompareNoCase(right.name) < 0;
        });
    m_conversationsView.SetItems(m_conversations);
    m_conversationsView.SetSelectedId(m_selectedId);
}

void CMainDlg::SelectConversation(const CString& id, bool requestHistory) {
    if (id.IsEmpty()) {
        m_selectedId.Empty();
        m_typingName.Empty();
        m_messageView.SetTyping(L"");
        m_messageView.SetMessages({});
        m_messageView.SetEmptyState(L"Select a conversation",
                                    L"Choose someone from the list to start messaging.");
        m_conversationsView.SetSelectedId(L"");
        Invalidate();
        return;
    }

    ConversationItem* conversation = FindConversation(id);
    if (!conversation) return;

    if (m_sentTyping) SendTypingState(false);
    m_selectedId = id;
    conversation->unread = 0;
    m_typingName.Empty();
    m_messageView.SetTyping(L"");
    RebuildList();
    m_conversationsView.SetSelectedId(id);

    CString title = conversation->name;
    CString emptySubtitle;
    if (conversation->group) {
        emptySubtitle = L"No messages yet. Say hello to the group.";
    } else {
        emptySubtitle = L"No messages yet. Say hello to start the conversation.";
    }
    m_messageView.SetEmptyState(title, emptySubtitle);

    auto it = m_threads.find(id);
    if (it == m_threads.end()) m_messageView.SetMessages({});
    else m_messageView.SetMessages(it->second);
    m_messageView.ScrollToBottom();
    if (requestHistory) RequestHistoryFor(id);

    m_compose.SetFocus();
    InvalidateHeader();
    Invalidate();
}

void CMainDlg::RefreshCurrentView() {
    if (m_selectedId.IsEmpty()) {
        m_messageView.SetMessages({});
        m_messageView.SetEmptyState(L"Select a conversation",
                                    L"Choose someone from the list to start messaging.");
        return;
    }
    ConversationItem* conversation = FindConversation(m_selectedId);
    if (!conversation) return;
    m_messageView.SetEmptyState(
        conversation->name,
        conversation->group ? CString(L"No messages yet. Say hello to the group.")
                            : CString(L"No messages yet. Say hello to start the conversation."));
    auto it = m_threads.find(m_selectedId);
    if (it == m_threads.end()) m_messageView.SetMessages({});
    else m_messageView.SetMessages(it->second);
    m_messageView.SetTyping(m_typingName);
    InvalidateHeader();
}

void CMainDlg::UpdateConversationPreview(const CString& key, const CString& preview,
                                         const CString& time, bool unread,
                                         bool bumpOrder) {
    ConversationItem* conversation = FindConversation(key);
    if (!conversation) {
        EnsureConversation(key, key, key.Left(4) == L"grp-");
        conversation = FindConversation(key);
    }
    if (!conversation) return;
    conversation->preview = preview;
    conversation->time = time;
    if (bumpOrder) conversation->lastTick = GetTickCount64();
    if (unread) ++conversation->unread;
    RebuildList();
}

void CMainDlg::UpdatePreviewFromThread(const CString& key) {
    ConversationItem* conversation = FindConversation(key);
    if (!conversation) return;
    auto it = m_threads.find(key);
    if (it == m_threads.end() || it->second.empty()) return;
    const ChatMessageItem& last = it->second.back();
    CString preview = last.text;
    if (conversation->group && !last.mine && !last.sender.IsEmpty()) {
        preview = last.sender + L": " + preview;
    }
    conversation->preview = preview;
    conversation->time = last.time;
    RebuildList();
}

void CMainDlg::RequestHistoryFor(const CString& id) {
    if (!m_session || !m_session->IsConnected()) return;
    if (m_loadedHistory.count(id) > 0) return;
    bool group = false;
    for (const auto& conversation : m_conversations) {
        if (conversation.id == id) {
            group = conversation.group;
            break;
        }
    }
    if (group) {
        m_session->RequestHistory("", ui::ToUtf8(id), 100);
    } else {
        m_session->RequestHistory(ui::ToUtf8(id), "", 100);
    }
}

void CMainDlg::SendCurrentMessage() {
    CString raw;
    m_compose.GetWindowText(raw);
    CString text = raw;
    text.Trim();

    if (text.IsEmpty()) {
        ShowTransient(L"Type a message first.", true);
        m_compose.SetFocus();
        return;
    }
    if (text.GetLength() > 8000) {
        ShowTransient(L"Messages are limited to 8000 characters.", true);
        return;
    }
    if (m_selectedId.IsEmpty()) {
        ShowTransient(L"Select a conversation before sending.", true);
        return;
    }
    if (!m_connected || !m_session->IsConnected()) {
        ShowTransient(L"Not connected. Your message was not sent - reconnecting.", true);
        return;
    }

    bool group = IsGroupSelected();
    CString clientId;
    clientId.Format(L"client-%llu-%llu",
                    static_cast<unsigned long long>(GetTickCount64()),
                    static_cast<unsigned long long>(++m_clientMessageSeq));

    ChatMessageItem item;
    item.id = clientId;
    item.sender = m_userId;
    item.text = text;
    item.time = ui::NowTime();
    item.mine = true;
    item.pending = true;

    auto& thread = m_threads[m_selectedId];
    thread.push_back(item);
    m_messageView.AppendMessage(item);
    m_compose.SetWindowText(L"");
    SendTypingState(false);
    UpdateConversationPreview(m_selectedId, text, item.time, false, true);

    bool ok = group
        ? m_session->SendGroupChat(ui::ToUtf8(m_selectedId), ui::ToUtf8(text),
                                   ui::ToUtf8(clientId))
        : m_session->SendChat(ui::ToUtf8(m_selectedId), ui::ToUtf8(text),
                              ui::ToUtf8(clientId));

    if (!ok) {
        for (auto& message : thread) {
            if (message.id == clientId) {
                message.pending = false;
                message.failed = true;
                break;
            }
        }
        m_messageView.SetMessages(thread);
        m_compose.SetWindowText(raw);
        m_compose.SetSel(-1, -1);
        ShowTransient(L"Message not sent. The connection is reconnecting - try again.", true);
    }

    m_compose.SetFocus();
    InvalidateHeader();
}

void CMainDlg::SendTypingState(bool typing) {
    if (!m_session || m_selectedId.IsEmpty()) {
        m_sentTyping = false;
        return;
    }
    if (typing == m_sentTyping) return;
    if (!m_session->IsConnected()) {
        m_sentTyping = false;
        return;
    }
    bool group = IsGroupSelected();
    if (typing) {
        m_session->SendTyping(group ? "" : ui::ToUtf8(m_selectedId),
                              group ? ui::ToUtf8(m_selectedId) : "", true);
        m_sentTyping = true;
        m_lastTypingSent = GetTickCount64();
    } else {
        m_session->SendTyping(group ? "" : ui::ToUtf8(m_selectedId),
                              group ? ui::ToUtf8(m_selectedId) : "", false);
        m_sentTyping = false;
    }
}

void CMainDlg::ShowTransient(const CString& text, bool error) {
    m_transient = text;
    m_transientError = error;
    m_transientExpiry = GetTickCount64() + 5000;
    CRect client;
    GetClientRect(&client);
    InvalidateRect(CRect(320, client.bottom - 100, client.right, client.bottom - 68),
                   FALSE);
}

void CMainDlg::ClearTyping() {
    if (m_typingName.IsEmpty()) return;
    m_typingName.Empty();
    m_typingExpiry = 0;
    m_messageView.SetTyping(L"");
    InvalidateHeader();
}

void CMainDlg::OnComposeChanged() {
    if (m_closing) return;
    CString text;
    m_compose.GetWindowText(text);
    text.Trim();
    ULONGLONG now = GetTickCount64();
    m_lastTypingChange = now;
    if (text.IsEmpty()) {
        if (m_sentTyping) SendTypingState(false);
        return;
    }
    if (!m_sentTyping || now - m_lastTypingSent > 2000) {
        SendTypingState(true);
        m_lastTypingSent = now;
    }
}

void CMainDlg::OnSendButton() {
    SendCurrentMessage();
}

LRESULT CMainDlg::OnSendRequested(WPARAM, LPARAM) {
    SendCurrentMessage();
    return 0;
}

LRESULT CMainDlg::OnConversationSelected(WPARAM, LPARAM) {
    CString id = m_conversationsView.GetSelectedId();
    if (!id.IsEmpty()) SelectConversation(id, true);
    return 0;
}

void CMainDlg::OnNewGroup() {
    CGroupDlg dialog(this);
    if (dialog.DoModal() != IDOK) return;

    bool sent = false;
    if (dialog.Mode() == 0) {
        sent = m_session->SendFields({
            {"type", "group_create"},
            {"name", ui::ToUtf8(dialog.GroupName())},
            {"members", ui::ToUtf8(dialog.Members())}
        });
    } else {
        sent = m_session->SendFields({
            {"type", "group_join"},
            {"groupId", ui::ToUtf8(dialog.GroupId())}
        });
    }
    if (!sent) {
        ShowTransient(L"Group request was not sent. The connection is reconnecting.", true);
    } else {
        ShowTransient(dialog.Mode() == 0 ? L"Creating group..." : L"Joining group...",
                      false);
    }
}

void CMainDlg::HandleServerMessage(const std::map<std::string, std::string>& message) {
    const std::string type = JsonGet(message, "type");

    if (type == "user_list") {
        std::vector<CString> users = ui::SplitCsv(ui::FromUtf8(JsonGet(message, "users")));
        for (const auto& user : users) {
            if (user == m_userId) continue;
            EnsureConversation(user, user, false);
        }
        RebuildList();
        if (m_selectedId.IsEmpty() && !m_conversations.empty()) {
            SelectConversation(m_conversations.front().id, true);
        }
        m_conversationsView.SetEmptyText(L"No contacts yet");
        return;
    }

    if (type == "presence_list") {
        m_onlineUsers.clear();
        for (const auto& user : ui::SplitCsv(ui::FromUtf8(JsonGet(message, "users")))) {
            m_onlineUsers.insert(user);
        }
        RebuildList();
        if (!m_selectedId.IsEmpty()) InvalidateHeader();
        return;
    }

    if (type == "presence") {
        HandlePresence(message);
        return;
    }

    if (type == "group") {
        CString id = ui::FromUtf8(JsonGet(message, "groupId"));
        CString name = ui::FromUtf8(JsonGet(message, "groupName", JsonGet(message, "groupId").c_str()));
        int members = ToInt(ui::FromUtf8(JsonGet(message, "memberCount")));
        EnsureConversation(id, name, true, members);
        RebuildList();
        return;
    }

    if (type == "group_list_end") {
        if (m_selectedId.IsEmpty() && !m_conversations.empty()) {
            SelectConversation(m_conversations.front().id, true);
        }
        return;
    }

    if (type == "group_created" || type == "group_joined") {
        CString id = ui::FromUtf8(JsonGet(message, "groupId"));
        CString name = ui::FromUtf8(JsonGet(message, "groupName"));
        int members = ToInt(ui::FromUtf8(JsonGet(message, "memberCount")));
        EnsureConversation(id, name, true, members);
        m_groupMembers[id] = ui::SplitCsv(ui::FromUtf8(JsonGet(message, "members")));
        RebuildList();
        SelectConversation(id, true);
        ShowTransient(type == "group_created" ? L"Group created." : L"Joined group.",
                      false);
        return;
    }

    if (type == "group_added") {
        CString id = ui::FromUtf8(JsonGet(message, "groupId"));
        CString name = ui::FromUtf8(JsonGet(message, "groupName"));
        EnsureConversation(id, name, true);
        RebuildList();
        m_session->RequestGroups();
        ShowTransient(L"Added to " + name + L".", false);
        return;
    }

    if (type == "group_removed" || type == "group_left") {
        CString id = ui::FromUtf8(JsonGet(message, "groupId"));
        CString name = ui::FromUtf8(JsonGet(message, "groupName"));
        RemoveConversation(id);
        ShowTransient(type == "group_left" ? L"Left " + name + L"."
                                           : L"Removed from " + name + L".", false);
        return;
    }

    if (type == "chat") {
        HandleChat(message, false);
        return;
    }
    if (type == "group_chat") {
        HandleChat(message, true);
        return;
    }
    if (type == "history_item") {
        HandleHistoryItem(message);
        return;
    }
    if (type == "history_end") {
        CString groupId = ui::FromUtf8(JsonGet(message, "groupId"));
        CString peer = ui::FromUtf8(JsonGet(message, "with"));
        CString key = groupId.IsEmpty() ? peer : groupId;
        if (key.IsEmpty()) return;
        m_loadedHistory.insert(key);
        UpdatePreviewFromThread(key);
        if (key == m_selectedId) {
            RefreshCurrentView();
            m_messageView.ScrollToBottom();
        }
        return;
    }
    if (type == "ack") {
        HandleAck(message);
        return;
    }
    if (type == "error") {
        HandleError(message);
        return;
    }
    if (type == "typing") {
        HandleTyping(message);
        return;
    }
    if (type == "bye") {
        ShowTransient(L"The server ended this session.", true);
        return;
    }
}

void CMainDlg::HandleChat(const std::map<std::string, std::string>& message,
                          bool group) {
    CString sender = ui::FromUtf8(JsonGet(message, "userId"));
    CString target = ui::FromUtf8(JsonGet(message, "targetId"));
    CString groupId = ui::FromUtf8(JsonGet(message, "groupId"));
    CString text = ui::FromUtf8(JsonGet(message, "message"));
    CString messageId = ui::FromUtf8(JsonGet(message, "messageId"));
    CString groupName = ui::FromUtf8(JsonGet(message, "groupName"));
    if (text.IsEmpty()) return;

    CString key;
    bool groupConversation = group;
    if (group) {
        key = groupId;
        if (key.IsEmpty()) return;
        EnsureConversation(key, groupName.IsEmpty() ? key : groupName, true);
    } else {
        if (target.IsEmpty() || target == L"*") {
            key = L"*";
            groupConversation = true;
            EnsureConversation(key, L"All hands", true);
        } else if (sender == m_userId) {
            key = target;
            EnsureConversation(key, key, false);
        } else {
            key = sender;
            EnsureConversation(key, key, false);
        }
    }

    auto& thread = m_threads[key];
    if (!messageId.IsEmpty()) {
        for (const auto& existing : thread) {
            if (existing.id == messageId) return;
        }
    }

    ChatMessageItem item;
    item.id = messageId;
    item.sender = sender;
    item.text = text;
    item.time = ui::DisplayTime(ui::FromUtf8(JsonGet(message, "timestamp")));
    item.mine = (sender == m_userId);
    thread.push_back(item);

    bool current = (key == m_selectedId);
    if (current) {
        m_messageView.AppendMessage(item);
        m_messageView.SetEmptyState(
            FindConversation(key) ? FindConversation(key)->name : key,
            groupConversation ? CString(L"No messages yet. Say hello to the group.")
                              : CString(L"No messages yet. Say hello to start the conversation."));
        if (!m_typingName.IsEmpty() && m_typingName == sender) ClearTyping();
    }

    CString preview = text;
    if (groupConversation && !item.mine) preview = sender + L": " + text;
    bool unread = !item.mine && !current;
    UpdateConversationPreview(key, preview, item.time, unread, true);
    InvalidateHeader();
}

void CMainDlg::HandleHistoryItem(const std::map<std::string, std::string>& message) {
    CString groupId = ui::FromUtf8(JsonGet(message, "groupId"));
    CString peer = ui::FromUtf8(JsonGet(message, "with"));
    CString key = groupId.IsEmpty() ? peer : groupId;
    if (key.IsEmpty()) return;

    bool group = !groupId.IsEmpty();
    CString groupName = ui::FromUtf8(JsonGet(message, "groupName"));
    EnsureConversation(key, group ? groupName : key, group);

    ChatMessageItem item;
    item.id = ui::FromUtf8(JsonGet(message, "messageId"));
    item.sender = ui::FromUtf8(JsonGet(message, "userId"));
    item.text = ui::FromUtf8(JsonGet(message, "message"));
    item.time = ui::DisplayTime(ui::FromUtf8(JsonGet(message, "timestamp")));
    item.mine = (item.sender == m_userId);
    if (item.text.IsEmpty()) return;

    auto& thread = m_threads[key];
    if (!item.id.IsEmpty()) {
        for (const auto& existing : thread) {
            if (existing.id == item.id) return;
        }
    }
    thread.push_back(item);
}

void CMainDlg::HandleAck(const std::map<std::string, std::string>& message) {
    CString clientId = ui::FromUtf8(JsonGet(message, "clientMsgId"));
    if (clientId.IsEmpty()) return;
    CString messageId = ui::FromUtf8(JsonGet(message, "messageId"));
    CString timestamp = ui::FromUtf8(JsonGet(message, "timestamp"));
    CString time = ui::DisplayTime(timestamp);

    for (auto& entry : m_threads) {
        for (auto& item : entry.second) {
            if (item.id != clientId) continue;
            item.pending = false;
            item.failed = false;
            if (!messageId.IsEmpty()) item.id = messageId;
            if (!time.IsEmpty()) item.time = time;
            if (entry.first == m_selectedId) {
                m_messageView.SetMessages(entry.second);
            }
            return;
        }
    }
}

void CMainDlg::HandleError(const std::map<std::string, std::string>& message) {
    CString text = ui::FromUtf8(JsonGet(message, "message", "Server error."));
    CString clientId = ui::FromUtf8(JsonGet(message, "clientMsgId"));

    if (!clientId.IsEmpty()) {
        for (auto& entry : m_threads) {
            for (auto& item : entry.second) {
                if (item.id != clientId) continue;
                item.pending = false;
                item.failed = true;
                if (entry.first == m_selectedId) {
                    m_messageView.SetMessages(entry.second);
                }
                break;
            }
        }
    }

    ShowTransient(L"Error: " + text, true);

    if (!m_selectedId.IsEmpty() && clientId.IsEmpty()) {
        ChatMessageItem notice;
        notice.text = text;
        notice.time = ui::NowTime();
        notice.system = true;
        notice.error = true;
        m_threads[m_selectedId].push_back(notice);
        m_messageView.AppendMessage(notice);
    }
}

void CMainDlg::HandlePresence(const std::map<std::string, std::string>& message) {
    CString user = ui::FromUtf8(JsonGet(message, "userId"));
    if (user.IsEmpty() || user == m_userId) return;
    bool online = JsonGetBool(message, "online", false);
    if (online) m_onlineUsers.insert(user);
    else m_onlineUsers.erase(user);
    RebuildList();
    if (m_selectedId == user) InvalidateHeader();
}

void CMainDlg::HandleTyping(const std::map<std::string, std::string>& message) {
    CString sender = ui::FromUtf8(JsonGet(message, "userId"));
    CString groupId = ui::FromUtf8(JsonGet(message, "groupId"));
    bool typing = JsonGetBool(message, "isTyping", false);
    if (sender.IsEmpty()) return;

    bool current = groupId.IsEmpty()
        ? (m_selectedId == sender)
        : (m_selectedId == groupId);
    if (!current) return;

    if (typing) {
        m_typingName = sender;
        m_typingExpiry = GetTickCount64() + 4000;
        m_messageView.SetTyping(sender);
        InvalidateHeader();
    } else if (m_typingName == sender) {
        ClearTyping();
    }
}

void CMainDlg::RemoveConversation(const CString& id) {
    if (id.IsEmpty()) return;
    m_conversations.erase(
        std::remove_if(m_conversations.begin(), m_conversations.end(),
                       [&id](const ConversationItem& item) { return item.id == id; }),
        m_conversations.end());
    m_threads.erase(id);
    m_loadedHistory.erase(id);
    m_groupMembers.erase(id);
    if (m_selectedId == id) {
        m_selectedId.Empty();
        m_typingName.Empty();
        m_messageView.SetTyping(L"");
        m_messageView.SetMessages({});
        m_messageView.SetEmptyState(L"Select a conversation",
                                    L"Choose someone from the list to start messaging.");
    }
    RebuildList();
    if (m_selectedId.IsEmpty() && !m_conversations.empty()) {
        SelectConversation(m_conversations.front().id, true);
    }
    Invalidate();
}

LRESULT CMainDlg::OnSessionFrame(WPARAM, LPARAM payload) {
    std::unique_ptr<std::string> frame(reinterpret_cast<std::string*>(payload));
    if (!frame) return 0;
    std::map<std::string, std::string> message;
    if (JsonParse(*frame, message)) HandleServerMessage(message);
    return 0;
}

LRESULT CMainDlg::OnSessionStatus(WPARAM status, LPARAM payload) {
    std::unique_ptr<std::string> textPtr(reinterpret_cast<std::string*>(payload));
    CString text = textPtr ? ui::FromUtf8(*textPtr) : CString();

    if (status == SESSION_CONNECTED) {
        bool reconnected = !m_connected;
        m_connected = true;
        m_connectionError = false;
        m_connectionText = L"Connected";
        m_serverId = ui::FromUtf8(m_session->ServerId());
        m_rtt = m_session->RttMs();
        m_sendButton.EnableWindow(TRUE);
        m_compose.EnableWindow(TRUE);
        m_loadedHistory.clear();
        m_session->RequestGroups();
        m_session->RequestPresence();
        if (!m_selectedId.IsEmpty()) RequestHistoryFor(m_selectedId);
        if (reconnected) ShowTransient(L"Reconnected. Messages are flowing again.", false);
    } else if (status == SESSION_AUTH_FAILED) {
        m_connected = false;
        m_connectionError = true;
        m_connectionText = L"Sign-in expired";
        m_sendButton.EnableWindow(FALSE);
        ShowTransient(L"Sign-in failed. Sign out and check your credentials.", true);
    } else {
        m_connected = false;
        m_connectionError = true;
        m_connectionText = status == SESSION_RECONNECTING ? L"Reconnecting..." : L"Connection lost";
        m_sendButton.EnableWindow(FALSE);
        m_loadedHistory.clear();
        ClearTyping();
        if (!text.IsEmpty()) ShowTransient(text, true);
    }
    Invalidate();
    return 0;
}

LRESULT CMainDlg::OnHeartbeat(WPARAM, LPARAM payload) {
    std::unique_ptr<std::string> textPtr(reinterpret_cast<std::string*>(payload));
    if (!textPtr) return 0;
    CString server;
    CString state;
    int rtt = -1;
    if (!SplitHeartbeat(ui::FromUtf8(*textPtr), server, rtt, state)) return 0;
    if (!server.IsEmpty()) m_serverId = server;
    if (state == L"ok") {
        m_rtt = rtt;
        if (m_connected) {
            m_connectionText = L"Connected";
            m_connectionError = false;
        }
    } else {
        m_rtt = -1;
    }
    InvalidateHeader();
    return 0;
}
