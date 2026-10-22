#pragma once

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>

#include "resource.h"
#include "UiControls.h"

class ChatSession;

class CMainDlg : public CDialogEx {
public:
    explicit CMainDlg(std::shared_ptr<ChatSession> session, CWnd* parent = nullptr);
    enum { IDD = IDD_MAIN_DIALOG };

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* dataExchange) override;

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT controlColor);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR timerId);
    afx_msg void OnSendButton();
    afx_msg void OnNewGroup();
    afx_msg void OnLogout();
    afx_msg void OnComposeChanged();
    afx_msg LRESULT OnSendRequested(WPARAM, LPARAM);
    afx_msg LRESULT OnConversationSelected(WPARAM, LPARAM);
    afx_msg LRESULT OnSessionFrame(WPARAM, LPARAM);
    afx_msg LRESULT OnSessionStatus(WPARAM status, LPARAM text);
    afx_msg LRESULT OnHeartbeat(WPARAM, LPARAM text);
    DECLARE_MESSAGE_MAP()

private:
    void CreateFonts();
    void LayoutControls();
    void DoLogout(int result);

    ConversationItem* FindConversation(const CString& id);
    bool IsGroupSelected() const;
    void EnsureConversation(const CString& id, const CString& name,
                            bool group, int memberCount = 0);
    void RebuildList();
    void SelectConversation(const CString& id, bool requestHistory);
    void RefreshCurrentView();
    void UpdateConversationPreview(const CString& key, const CString& preview,
                                   const CString& time, bool unread,
                                   bool bumpOrder);
    void UpdatePreviewFromThread(const CString& key);
    void RequestHistoryFor(const CString& id);
    void SendCurrentMessage();
    void SendTypingState(bool typing);
    void ShowTransient(const CString& text, bool error);
    void ClearTyping();
    void HandleServerMessage(const std::map<std::string, std::string>& message);
    void HandleHistoryItem(const std::map<std::string, std::string>& message);
    void HandleChat(const std::map<std::string, std::string>& message, bool group);
    void HandleAck(const std::map<std::string, std::string>& message);
    void HandleError(const std::map<std::string, std::string>& message);
    void HandlePresence(const std::map<std::string, std::string>& message);
    void HandleTyping(const std::map<std::string, std::string>& message);
    void RemoveConversation(const CString& id);
    void InvalidateHeader() { InvalidateRect(CRect(320, 0, 4000, 72), FALSE); }

    std::shared_ptr<ChatSession> m_session;
    CConversationList m_conversationsView;
    CMessageView m_messageView;
    CComposerEdit m_compose;
    CFlatButton m_sendButton;
    CFlatButton m_newGroupButton;
    CFlatButton m_logoutButton;

    std::vector<ConversationItem> m_conversations;
    std::map<CString, std::vector<ChatMessageItem>> m_threads;
    std::set<CString> m_loadedHistory;
    std::set<CString> m_onlineUsers;
    std::map<CString, std::vector<CString>> m_groupMembers;

    CString m_userId;
    CString m_serverId;
    CString m_selectedId;
    CString m_typingName;
    CString m_connectionText = L"Connecting...";
    CString m_transient;
    bool m_connectionError = false;
    bool m_connected = true;
    bool m_closing = false;
    bool m_transientError = false;
    bool m_sentTyping = false;
    int m_rtt = -1;
    ULONGLONG m_typingExpiry = 0;
    ULONGLONG m_lastTypingChange = 0;
    ULONGLONG m_lastTypingSent = 0;
    ULONGLONG m_transientExpiry = 0;
    ULONGLONG m_clientMessageSeq = 0;

    CFont m_logoFont;
    CFont m_brandFont;
    CFont m_brandSubFont;
    CFont m_userFont;
    CFont m_userSubFont;
    CFont m_headerNameFont;
    CFont m_headerSubFont;
    CFont m_pillFont;
    CFont m_hintFont;
    CFont m_editFont;
    CBrush m_composeBrush;
};
