#include "UiControls.h"

using namespace ui;

#include <algorithm>

#include "ChatClient.h"

namespace {
    int ScreenDpi() {
        HDC dc = GetDC(nullptr);
        int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(nullptr, dc);
        if (dpi <= 0) dpi = 96;
        return dpi;
    }

    void MakeFont(CFont& font, int points, int weight, const wchar_t* family = L"Segoe UI") {
        int height = -MulDiv(points, ScreenDpi(), 72);
        font.DeleteObject();
        font.CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
    }

    void TrackLeave(CWnd* wnd, bool& tracking) {
        if (tracking) return;
        TRACKMOUSEEVENT event = {};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = wnd->GetSafeHwnd();
        if (TrackMouseEvent(&event)) tracking = true;
    }

    void DrawSingleLine(CDC& dc, const CString& text, CRect rect,
                        CFont& font, COLORREF color, UINT format) {
        CFont* old = dc.SelectObject(&font);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(color);
        dc.DrawText(text, &rect, format | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(old);
    }

    CString UnreadText(int count) {
        if (count > 99) return L"99+";
        CString text;
        text.Format(L"%d", count);
        return text;
    }
}

BEGIN_MESSAGE_MAP(CFlatButton, CButton)
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

BOOL CFlatButton::Create(const CString& text, const CRect& rect, CWnd* parent, UINT id) {
    if (!CButton::Create(text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                         rect, parent, id)) {
        return FALSE;
    }
    CreateFontIfNeeded();
    SetFont(&m_font);
    return TRUE;
}

void CFlatButton::SetColors(COLORREF background, COLORREF hover, COLORREF textColor) {
    m_background = background;
    m_hover = hover;
    m_textColor = textColor;
    Invalidate();
}

void CFlatButton::CreateFontIfNeeded() {
    if (m_font.GetSafeHandle() == nullptr) {
        MakeFont(m_font, m_fontPoints / 10, FW_SEMIBOLD);
    }
}

void CFlatButton::DrawItem(LPDRAWITEMSTRUCT drawItem) {
    CDC dc;
    dc.Attach(drawItem->hDC);
    CRect rect = drawItem->rcItem;

    bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    COLORREF fill = m_hovering ? m_hover : m_background;
    if (disabled) fill = RGB(203, 213, 225);
    if (pressed) fill = RGB(
        static_cast<BYTE>(GetRValue(fill) * 0.88),
        static_cast<BYTE>(GetGValue(fill) * 0.88),
        static_cast<BYTE>(GetBValue(fill) * 0.88));

    FillRounded(dc, rect, m_radius, fill, CLR_NONE);
    CString text;
    GetWindowText(text);
    CreateFontIfNeeded();
    DrawSingleLine(dc, text, rect, m_font,
                   disabled ? RGB(248, 250, 252) : m_textColor,
                   DT_CENTER | DT_VCENTER);
    dc.Detach();
}

void CFlatButton::OnMouseMove(UINT flags, CPoint point) {
    if (!m_hovering) {
        m_hovering = true;
        Invalidate();
    }
    TrackLeave(this, m_tracking);
    CButton::OnMouseMove(flags, point);
}

void CFlatButton::OnMouseLeave() {
    m_hovering = false;
    m_tracking = false;
    Invalidate();
}

BEGIN_MESSAGE_MAP(CConversationList, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

BOOL CConversationList::Create(CWnd* parent, UINT id, const CRect& rect) {
    CString className = AfxRegisterWndClass(
        CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
        LoadCursor(nullptr, IDC_HAND),
        static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)), nullptr);
    if (!CWnd::CreateEx(0, className, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                        rect, parent, id)) {
        return FALSE;
    }
    CreateFonts();
    return TRUE;
}

void CConversationList::CreateFonts() {
    MakeFont(m_nameFont, 10, FW_SEMIBOLD);
    MakeFont(m_previewFont, 8, FW_NORMAL);
    MakeFont(m_timeFont, 7, FW_NORMAL);
    MakeFont(m_badgeFont, 7, FW_BOLD);
    MakeFont(m_emptyFont, 9, FW_NORMAL);
}

void CConversationList::SetItems(const std::vector<ConversationItem>& items) {
    m_items = items;
    ClampScroll();
    Invalidate();
}

void CConversationList::SetSelectedId(const CString& id) {
    m_selectedId = id;
    Invalidate();
}

int CConversationList::MaxScroll() const {
    CRect rect;
    GetClientRect(&rect);
    int content = static_cast<int>(m_items.size()) * m_rowHeight + 8;
    return (std::max)(0, content - rect.Height());
}

void CConversationList::ClampScroll() {
    m_scroll = (std::max)(0, (std::min)(m_scroll, MaxScroll()));
}

CRect CConversationList::ThumbRect() const {
    CRect rect;
    GetClientRect(&rect);
    int content = static_cast<int>(m_items.size()) * m_rowHeight + 8;
    if (content <= rect.Height() || rect.Height() == 0) return CRect(0, 0, 0, 0);
    int trackHeight = rect.Height();
    int thumbHeight = (std::max)(30, trackHeight * trackHeight / content);
    int travel = trackHeight - thumbHeight;
    int top = travel <= 0 ? 0 : m_scroll * travel / (std::max)(1, MaxScroll());
    return CRect(rect.right - 6, top + 2, rect.right - 2, top + thumbHeight - 2);
}

bool CConversationList::OnScrollbar(CPoint point) const {
    CRect rect;
    GetClientRect(&rect);
    return MaxScroll() > 0 && point.x >= rect.right - 14;
}

void CConversationList::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    if (client.IsRectEmpty()) return;

    CDC mem;
    mem.CreateCompatibleDC(&dc);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
    CBitmap* oldBitmap = mem.SelectObject(&bitmap);
    mem.FillSolidRect(client, ui::Sidebar);
    mem.SetBkMode(TRANSPARENT);

    if (m_items.empty()) {
        CRect empty = client;
        empty.DeflateRect(20, 30);
        CFont* old = mem.SelectObject(&m_emptyFont);
        mem.SetTextColor(ui::SidebarMuted);
        mem.DrawText(m_emptyText, &empty, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        mem.SelectObject(old);
    }

    int first = (std::max)(0, (m_scroll - 8) / m_rowHeight);
    int last = (std::min)(static_cast<int>(m_items.size()),
                          (m_scroll + client.Height()) / m_rowHeight + 2);

    for (int i = first; i < last; ++i) {
        const ConversationItem& item = m_items[i];
        int top = 4 + i * m_rowHeight - m_scroll;
        CRect row(4, top, client.right - 4, top + m_rowHeight - 2);
        if (row.bottom < 0 || row.top > client.bottom) continue;

        bool selected = item.id == m_selectedId;
        bool hovered = i == m_hoverIndex;
        if (selected) {
            FillRounded(mem, row, 10, ui::SidebarRaised, CLR_NONE);
            CRect accent(row.left, row.top + 12, row.left + 3, row.bottom - 12);
            FillRounded(mem, accent, 2, ui::Accent, CLR_NONE);
        } else if (hovered) {
            FillRounded(mem, row, 10, ui::SidebarHover, CLR_NONE);
        }

        CRect avatar(row.left + 12, row.top + 12, row.left + 54, row.top + 54);
        COLORREF avatarColor = ui::AvatarColor(item.name);
        if (item.group) {
            FillRounded(mem, avatar, 14, avatarColor, CLR_NONE);
        } else {
            FillCircle(mem, avatar, avatarColor);
        }
        CString initials = ui::Initials(item.name);
        if (item.group && initials.GetLength() > 2) initials = initials.Left(2);
        CRect initialsRect = avatar;
        DrawSingleLine(mem, initials, initialsRect, m_nameFont,
                       RGB(255, 255, 255), DT_CENTER | DT_VCENTER);

        if (!item.group) {
            CRect ring(avatar.right - 14, avatar.bottom - 14,
                       avatar.right + 1, avatar.bottom + 1);
            FillCircle(mem, ring, ui::SidebarRaised);
            CRect dot = ring;
            dot.DeflateRect(2, 2);
            FillCircle(mem, dot, item.online ? ui::Online : RGB(100, 116, 139));
        }

        int textLeft = avatar.right + 12;
        int textRight = row.right - 12;
        CRect nameRect(textLeft, row.top + 9, textRight - 40, row.top + 29);
        DrawSingleLine(mem, item.name, nameRect, m_nameFont,
                       selected ? RGB(255, 255, 255) : ui::SidebarText,
                       DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

        CString time = item.time;
        CRect timeRect(row.right - 56, row.top + 10, row.right - 10, row.top + 27);
        DrawSingleLine(mem, time, timeRect, m_timeFont, ui::SidebarMuted,
                       DT_RIGHT | DT_VCENTER);

        CString preview = item.preview;
        if (preview.IsEmpty()) {
            preview = item.group ? L"Group conversation" : L"Start a conversation";
        }
        CRect previewRect(textLeft, row.top + 32,
                          textRight - (item.unread > 0 ? 30 : 0), row.top + 52);
        DrawSingleLine(mem, preview, previewRect, m_previewFont,
                       selected ? RGB(203, 213, 225) : ui::SidebarMuted,
                       DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

        if (item.unread > 0) {
            CString badge = UnreadText(item.unread);
            int width = badge.GetLength() > 1 ? 28 : 20;
            CRect badgeRect(row.right - width - 10, row.top + 36,
                            row.right - 10, row.top + 54);
            FillRounded(mem, badgeRect, 9, RGB(239, 68, 68), CLR_NONE);
            DrawSingleLine(mem, badge, badgeRect, m_badgeFont,
                           RGB(255, 255, 255), DT_CENTER | DT_VCENTER);
        }
    }

    CRect thumb = ThumbRect();
    if (!thumb.IsRectEmpty()) {
        FillRounded(mem, thumb, 2, RGB(71, 85, 105), CLR_NONE);
    }

    dc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(oldBitmap);
}

BOOL CConversationList::OnEraseBkgnd(CDC*) {
    return TRUE;
}

void CConversationList::OnSize(UINT type, int cx, int cy) {
    CWnd::OnSize(type, cx, cy);
    ClampScroll();
    Invalidate();
}

void CConversationList::OnMouseMove(UINT flags, CPoint point) {
    TrackLeave(this, m_tracking);
    CRect client;
    GetClientRect(&client);

    if (m_dragThumb) {
        CRect thumb = ThumbRect();
        int travel = client.Height() - thumb.Height();
        int target = point.y - m_dragOffset;
        if (travel > 0) {
            m_scroll = target * MaxScroll() / travel;
            ClampScroll();
            Invalidate();
        }
        return;
    }

    int index = (point.y + m_scroll - 4) / m_rowHeight;
    bool valid = index >= 0 && index < static_cast<int>(m_items.size()) &&
                 point.x >= 0 && point.x <= client.Width();
    int hover = valid ? index : -1;
    if (hover != m_hoverIndex) {
        m_hoverIndex = hover;
        Invalidate();
    }
    CWnd::OnMouseMove(flags, point);
}

void CConversationList::OnLButtonDown(UINT flags, CPoint point) {
    SetFocus();
    if (OnScrollbar(point)) {
        CRect thumb = ThumbRect();
        if (thumb.PtInRect(point)) {
            m_dragThumb = true;
            m_dragOffset = point.y - thumb.top;
            SetCapture();
        } else {
            CRect client;
            GetClientRect(&client);
            m_scroll += point.y < thumb.top ? -client.Height() / 2
                                             : client.Height() / 2;
            ClampScroll();
            Invalidate();
        }
        return;
    }

    int index = (point.y + m_scroll - 4) / m_rowHeight;
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        m_selectedId = m_items[index].id;
        Invalidate();
        GetParent()->PostMessage(WM_APP_CONVERSATION_SELECTED, 0, 0);
    }
    CWnd::OnLButtonDown(flags, point);
}

void CConversationList::OnLButtonUp(UINT flags, CPoint point) {
    if (m_dragThumb) {
        m_dragThumb = false;
        ReleaseCapture();
    }
    CWnd::OnLButtonUp(flags, point);
}

BOOL CConversationList::OnMouseWheel(UINT flags, short delta, CPoint point) {
    m_scroll -= (delta / WHEEL_DELTA) * m_rowHeight * 2;
    ClampScroll();
    Invalidate();
    return TRUE;
}

void CConversationList::OnMouseLeave() {
    m_tracking = false;
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        Invalidate();
    }
}

BEGIN_MESSAGE_MAP(CMessageView, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

BOOL CMessageView::Create(CWnd* parent, UINT id, const CRect& rect) {
    CString className = AfxRegisterWndClass(
        CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
        LoadCursor(nullptr, IDC_ARROW),
        static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)), nullptr);
    if (!CWnd::CreateEx(0, className, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                        rect, parent, id)) {
        return FALSE;
    }
    CreateFonts();
    return TRUE;
}

void CMessageView::CreateFonts() {
    MakeFont(m_bodyFont, 10, FW_NORMAL);
    MakeFont(m_senderFont, 8, FW_SEMIBOLD);
    MakeFont(m_metaFont, 7, FW_NORMAL);
    MakeFont(m_systemFont, 8, FW_NORMAL);
    MakeFont(m_emptyTitleFont, 15, FW_SEMIBOLD);
    MakeFont(m_emptyBodyFont, 9, FW_NORMAL);
}

void CMessageView::SetEmptyState(const CString& title, const CString& subtitle) {
    m_emptyTitle = title;
    m_emptySubtitle = subtitle;
    Invalidate();
}

void CMessageView::SetMessages(const std::vector<ChatMessageItem>& messages) {
    bool atBottom = IsAtBottom();
    m_messages = messages;
    Layout();
    if (atBottom || m_scroll == 0) ScrollToBottom();
    Invalidate();
}

void CMessageView::AppendMessage(const ChatMessageItem& message) {
    bool atBottom = IsAtBottom();
    m_messages.push_back(message);
    Layout();
    if (atBottom) ScrollToBottom();
    Invalidate();
}

void CMessageView::SetTyping(const CString& userName) {
    if (m_typingName == userName) return;
    bool atBottom = IsAtBottom();
    m_typingName = userName;
    Layout();
    if (atBottom) ScrollToBottom();
    Invalidate();
}

void CMessageView::ScrollToBottom() {
    m_scroll = MaxScroll();
    Invalidate();
}

void CMessageView::RefreshLayout() {
    Layout();
    Invalidate();
}

int CMessageView::MaxScroll() const {
    CRect rect;
    GetClientRect(&rect);
    return (std::max)(0, m_contentHeight - rect.Height());
}

bool CMessageView::IsAtBottom() const {
    return m_scroll >= MaxScroll() - 3;
}

void CMessageView::ClampScroll() {
    m_scroll = (std::max)(0, (std::min)(m_scroll, MaxScroll()));
}

void CMessageView::Layout() {
    m_layout.clear();
    CRect client;
    GetClientRect(&client);
    int width = client.Width();
    if (width <= 0) {
        m_contentHeight = 0;
        return;
    }

    CClientDC dc(this);
    dc.SetBkMode(TRANSPARENT);

    const int padX = 14;
    const int padY = 10;
    const int metaHeight = 15;
    const int side = 18;
    const int maxBubbleWidth = (std::max)(250, (std::min)(560, width * 62 / 100));
    const int maxTextWidth = maxBubbleWidth - padX * 2;

    int y = 16;
    for (const auto& message : m_messages) {
        BubbleLayout layout;
        layout.message = message;

        if (message.system) {
            CFont* old = dc.SelectObject(&m_systemFont);
            CRect measure(0, 0, (std::max)(240, width * 72 / 100), 0);
            dc.DrawText(message.text, &measure,
                        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            dc.SelectObject(old);
            int bubbleWidth = (std::min)(width - 40, measure.Width() + 30);
            int bubbleHeight = measure.Height() + 16;
            layout.bubble = CRect((width - bubbleWidth) / 2, y,
                                  (width + bubbleWidth) / 2, y + bubbleHeight);
            layout.textRect = layout.bubble;
            layout.textRect.DeflateRect(15, 8);
            layout.system = true;
            y += bubbleHeight + 8;
            m_layout.push_back(layout);
            continue;
        }

        CRect textMeasure(0, 0, maxTextWidth, 0);
        CFont* old = dc.SelectObject(&m_bodyFont);
        dc.DrawText(message.text, &textMeasure,
                    DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        dc.SelectObject(old);

        int textWidth = (std::min)(maxTextWidth, textMeasure.Width() + 2);
        textWidth = (std::max)(60, textWidth);
        CRect finalMeasure(0, 0, textWidth, 0);
        old = dc.SelectObject(&m_bodyFont);
        dc.DrawText(message.text, &finalMeasure,
                    DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        dc.SelectObject(old);

        int bubbleWidth = textWidth + padX * 2;
        int bubbleHeight = padY + finalMeasure.Height() + 4 + metaHeight + 7;

        int senderHeight = message.mine ? 0 : 18;
        int top = y + senderHeight;
        int left = message.mine ? width - side - bubbleWidth : side;

        layout.bubble = CRect(left, top, left + bubbleWidth, top + bubbleHeight);
        layout.textRect = CRect(left + padX, top + padY,
                                left + padX + textWidth,
                                top + padY + finalMeasure.Height());
        layout.metaRect = CRect(left + padX, layout.bubble.bottom - metaHeight - 6,
                                left + bubbleWidth - padX, layout.bubble.bottom - 5);
        if (!message.mine) {
            layout.senderRect = CRect(left + 2, y, left + bubbleWidth, y + 16);
        }

        y = layout.bubble.bottom + 10;
        m_layout.push_back(layout);
    }

    if (!m_typingName.IsEmpty()) {
        CString typingText = m_typingName + L" is typing...";
        CRect measure(0, 0, maxTextWidth, 0);
        CFont* old = dc.SelectObject(&m_systemFont);
        dc.DrawText(typingText, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        dc.SelectObject(old);
        int bubbleWidth = measure.Width() + 30;
        BubbleLayout layout;
        layout.typing = true;
        layout.bubble = CRect(side, y, side + bubbleWidth, y + measure.Height() + 18);
        layout.textRect = layout.bubble;
        layout.textRect.DeflateRect(15, 8);
        m_layout.push_back(layout);
        y = layout.bubble.bottom + 10;
    }

    m_contentHeight = y + 10;
    ClampScroll();
}

CRect CMessageView::ThumbRect() const {
    CRect rect;
    GetClientRect(&rect);
    if (m_contentHeight <= rect.Height() || rect.Height() == 0) return CRect(0, 0, 0, 0);
    int trackHeight = rect.Height();
    int thumbHeight = (std::max)(30, trackHeight * trackHeight / m_contentHeight);
    int travel = trackHeight - thumbHeight;
    int top = travel <= 0 ? 0 : m_scroll * travel / (std::max)(1, MaxScroll());
    return CRect(rect.right - 7, top + 2, rect.right - 3, top + thumbHeight - 2);
}

bool CMessageView::OnScrollbar(CPoint point) const {
    CRect rect;
    GetClientRect(&rect);
    return MaxScroll() > 0 && point.x >= rect.right - 14;
}

void CMessageView::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    if (client.IsRectEmpty()) return;

    CDC mem;
    mem.CreateCompatibleDC(&dc);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
    CBitmap* oldBitmap = mem.SelectObject(&bitmap);
    mem.FillSolidRect(client, ui::WindowBg);
    mem.SetBkMode(TRANSPARENT);

    if (m_messages.empty() && m_typingName.IsEmpty()) {
        CRect icon(client.CenterPoint().x - 42, client.CenterPoint().y - 110,
                   client.CenterPoint().x + 42, client.CenterPoint().y - 26);
        FillCircle(mem, icon, RGB(226, 231, 255));
        CRect bubble(icon.left + 20, icon.top + 26, icon.right - 20, icon.bottom - 26);
        FillRounded(mem, bubble, 9, RGB(255, 255, 255), CLR_NONE);
        CRect dot1(bubble.left + 10, bubble.top + 14, bubble.left + 16, bubble.top + 20);
        CRect dot2(bubble.left + 20, bubble.top + 14, bubble.left + 26, bubble.top + 20);
        CRect dot3(bubble.left + 30, bubble.top + 14, bubble.left + 36, bubble.top + 20);
        FillCircle(mem, dot1, ui::Accent);
        FillCircle(mem, dot2, ui::Accent);
        FillCircle(mem, dot3, ui::Accent);

        CRect title(client.left + 40, icon.bottom + 22,
                    client.right - 40, icon.bottom + 54);
        CFont* old = mem.SelectObject(&m_emptyTitleFont);
        mem.SetTextColor(ui::Text);
        mem.DrawText(m_emptyTitle, &title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        mem.SelectObject(old);

        CRect subtitle(client.left + 60, icon.bottom + 58,
                       client.right - 60, icon.bottom + 104);
        old = mem.SelectObject(&m_emptyBodyFont);
        mem.SetTextColor(ui::Muted);
        mem.DrawText(m_emptySubtitle, &subtitle,
                     DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        mem.SelectObject(old);

        dc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
        mem.SelectObject(oldBitmap);
        return;
    }

    mem.SetBkMode(TRANSPARENT);
    for (const auto& layout : m_layout) {
        CRect bubble = layout.bubble;
        bubble.OffsetRect(0, -m_scroll);
        if (bubble.bottom < -20 || bubble.top > client.bottom + 20) continue;

        if (layout.system) {
            COLORREF fill = layout.message.error ? ui::DangerSoft : RGB(232, 236, 244);
            COLORREF text = layout.message.error ? RGB(153, 27, 27) : ui::Muted;
            FillRounded(mem, bubble, 9, fill, CLR_NONE);
            CFont* old = mem.SelectObject(&m_systemFont);
            mem.SetTextColor(text);
            CRect textRect = layout.textRect;
            textRect.OffsetRect(0, -m_scroll);
            mem.DrawText(layout.message.text, &textRect,
                         DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            mem.SelectObject(old);
            continue;
        }

        if (layout.typing) {
            FillRounded(mem, bubble, 13, RGB(255, 255, 255), ui::Border);
            CString typing = m_typingName + L" is typing...";
            CFont* old = mem.SelectObject(&m_systemFont);
            mem.SetTextColor(ui::Muted);
            CRect textRect = layout.textRect;
            textRect.OffsetRect(0, -m_scroll);
            mem.DrawText(typing, &textRect,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            mem.SelectObject(old);
            continue;
        }

        COLORREF fill = layout.message.mine ? ui::Accent : ui::Card;
        COLORREF border = layout.message.mine ? ui::Accent : ui::Border;
        if (layout.message.failed) {
            fill = RGB(254, 242, 242);
            border = RGB(252, 165, 165);
        }
        FillRounded(mem, bubble, 12, fill, border);

        if (!layout.message.mine) {
            CRect sender = layout.senderRect;
            sender.OffsetRect(0, -m_scroll);
            DrawSingleLine(mem, layout.message.sender, sender, m_senderFont,
                           ui::Muted, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }

        CRect textRect = layout.textRect;
        textRect.OffsetRect(0, -m_scroll);
        CFont* old = mem.SelectObject(&m_bodyFont);
        mem.SetTextColor(layout.message.mine ? RGB(255, 255, 255)
                                            : (layout.message.failed ? RGB(153, 27, 27)
                                                                     : ui::Text));
        mem.DrawText(layout.message.text, &textRect,
                     DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        mem.SelectObject(old);

        CString meta = layout.message.time;
        if (layout.message.pending) meta += L"  Sending...";
        else if (layout.message.failed) meta = L"Not sent  " + layout.message.time;
        CRect metaRect = layout.metaRect;
        metaRect.OffsetRect(0, -m_scroll);
        DrawSingleLine(mem, meta, metaRect, m_metaFont,
                       layout.message.mine ? RGB(224, 231, 255)
                                           : (layout.message.failed ? ui::Danger : ui::Muted),
                       DT_RIGHT | DT_VCENTER);
    }

    CRect thumb = ThumbRect();
    if (!thumb.IsRectEmpty()) {
        FillRounded(mem, thumb, 2, RGB(203, 213, 225), CLR_NONE);
    }

    dc.BitBlt(0, 0, client.Width(), client.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(oldBitmap);
}

BOOL CMessageView::OnEraseBkgnd(CDC*) {
    return TRUE;
}

void CMessageView::OnSize(UINT type, int cx, int cy) {
    bool atBottom = IsAtBottom();
    CWnd::OnSize(type, cx, cy);
    Layout();
    if (atBottom) ScrollToBottom();
    Invalidate();
}

BOOL CMessageView::OnMouseWheel(UINT flags, short delta, CPoint point) {
    m_scroll -= (delta / WHEEL_DELTA) * 72;
    ClampScroll();
    Invalidate();
    return TRUE;
}

void CMessageView::OnLButtonDown(UINT flags, CPoint point) {
    if (OnScrollbar(point)) {
        CRect thumb = ThumbRect();
        if (thumb.PtInRect(point)) {
            m_dragThumb = true;
            m_dragOffset = point.y - thumb.top;
            SetCapture();
        } else {
            CRect client;
            GetClientRect(&client);
            m_scroll += point.y < thumb.top ? -client.Height() / 2
                                             : client.Height() / 2;
            ClampScroll();
            Invalidate();
        }
    }
    CWnd::OnLButtonDown(flags, point);
}

void CMessageView::OnLButtonUp(UINT flags, CPoint point) {
    if (m_dragThumb) {
        m_dragThumb = false;
        ReleaseCapture();
    }
    CWnd::OnLButtonUp(flags, point);
}

void CMessageView::OnMouseMove(UINT flags, CPoint point) {
    TrackLeave(this, m_tracking);
    if (m_dragThumb) {
        CRect thumb = ThumbRect();
        CRect client;
        GetClientRect(&client);
        int travel = client.Height() - thumb.Height();
        int target = point.y - m_dragOffset;
        if (travel > 0) {
            m_scroll = target * MaxScroll() / travel;
            ClampScroll();
            Invalidate();
        }
        return;
    }
    CWnd::OnMouseMove(flags, point);
}

void CMessageView::OnMouseLeave() {
    m_tracking = false;
}

BEGIN_MESSAGE_MAP(CComposerEdit, CEdit)
    ON_WM_KEYDOWN()
END_MESSAGE_MAP()

BOOL CComposerEdit::Create(CWnd* parent, UINT id, const CRect& rect) {
    if (!CEdit::Create(
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
                ES_WANTRETURN | ES_NOHIDESEL,
            rect, parent, id)) {
        return FALSE;
    }
    SetLimitText(8000);
    SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Write a message..."));
    return TRUE;
}

void CComposerEdit::OnKeyDown(UINT character, UINT repeatCount, UINT flags) {
    if (character == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        GetParent()->PostMessage(WM_APP_SEND_REQUESTED, 0, 0);
        return;
    }
    CEdit::OnKeyDown(character, repeatCount, flags);
}
