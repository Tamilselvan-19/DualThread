#pragma once

#include <vector>

#include <afxwin.h>

#include "UiTheme.h"

struct ConversationItem {
    CString id;
    CString name;
    CString preview;
    CString time;
    int unread = 0;
    bool online = false;
    bool group = false;
    int memberCount = 0;
    ULONGLONG lastTick = 0;
};

struct ChatMessageItem {
    CString id;
    CString sender;
    CString text;
    CString time;
    bool mine = false;
    bool system = false;
    bool error = false;
    bool pending = false;
    bool failed = false;
};

class CFlatButton : public CButton {
public:
    BOOL Create(const CString& text, const CRect& rect, CWnd* parent, UINT id);
    void SetColors(COLORREF background, COLORREF hover, COLORREF textColor);
    void SetRadius(int radius) { m_radius = radius; }
    void SetFontSize(int points) { m_fontPoints = points; }

protected:
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();
    virtual void DrawItem(LPDRAWITEMSTRUCT drawItem);
    DECLARE_MESSAGE_MAP()

private:
    void CreateFontIfNeeded();

    COLORREF m_background = ui::Accent;
    COLORREF m_hover = ui::AccentHover;
    COLORREF m_textColor = RGB(255, 255, 255);
    int m_radius = 10;
    int m_fontPoints = 100;
    bool m_hovering = false;
    bool m_tracking = false;
    CFont m_font;
};

class CConversationList : public CWnd {
public:
    BOOL Create(CWnd* parent, UINT id, const CRect& rect);
    void SetItems(const std::vector<ConversationItem>& items);
    void SetSelectedId(const CString& id);
    CString GetSelectedId() const { return m_selectedId; }
    const std::vector<ConversationItem>& Items() const { return m_items; }
    void SetEmptyText(const CString& text) { m_emptyText = text; Invalidate(); }

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
    afx_msg void OnMouseLeave();
    DECLARE_MESSAGE_MAP()

private:
    void CreateFonts();
    int MaxScroll() const;
    void ClampScroll();
    CRect ThumbRect() const;
    bool OnScrollbar(CPoint point) const;

    std::vector<ConversationItem> m_items;
    CString m_selectedId;
    CString m_emptyText = L"No conversations yet";
    int m_scroll = 0;
    int m_rowHeight = 68;
    bool m_dragThumb = false;
    int m_dragOffset = 0;
    bool m_tracking = false;
    int m_hoverIndex = -1;
    CFont m_nameFont;
    CFont m_previewFont;
    CFont m_timeFont;
    CFont m_badgeFont;
    CFont m_emptyFont;
};

class CMessageView : public CWnd {
public:
    BOOL Create(CWnd* parent, UINT id, const CRect& rect);
    void SetMessages(const std::vector<ChatMessageItem>& messages);
    void AppendMessage(const ChatMessageItem& message);
    void SetTyping(const CString& userName);
    void SetEmptyState(const CString& title, const CString& subtitle);
    void ScrollToBottom();
    void RefreshLayout();

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();
    DECLARE_MESSAGE_MAP()

private:
    struct BubbleLayout {
        ChatMessageItem message;
        CRect bubble;
        CRect textRect;
        CRect metaRect;
        CRect senderRect;
        CRect statusRect;
        bool system = false;
        bool typing = false;
    };

    void CreateFonts();
    void Layout();
    int MaxScroll() const;
    void ClampScroll();
    bool IsAtBottom() const;
    CRect ThumbRect() const;
    bool OnScrollbar(CPoint point) const;

    std::vector<ChatMessageItem> m_messages;
    std::vector<BubbleLayout> m_layout;
    CString m_typingName;
    CString m_emptyTitle = L"Select a conversation";
    CString m_emptySubtitle = L"Choose someone from the list to start messaging.";
    int m_scroll = 0;
    int m_contentHeight = 0;
    bool m_dragThumb = false;
    int m_dragOffset = 0;
    bool m_tracking = false;
    CFont m_bodyFont;
    CFont m_senderFont;
    CFont m_metaFont;
    CFont m_systemFont;
    CFont m_emptyTitleFont;
    CFont m_emptyBodyFont;
};

class CComposerEdit : public CEdit {
public:
    BOOL Create(CWnd* parent, UINT id, const CRect& rect);

protected:
    afx_msg void OnKeyDown(UINT character, UINT repeatCount, UINT flags);
    DECLARE_MESSAGE_MAP()
};
