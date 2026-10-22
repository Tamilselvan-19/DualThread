#pragma once

#include <memory>

#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>

#include "resource.h"
#include "UiControls.h"

class ChatSession;

class CLoginDlg : public CDialogEx {
public:
    CLoginDlg(std::shared_ptr<ChatSession>& outSession, CWnd* parent = nullptr);
    enum { IDD = IDD_LOGIN_DIALOG };

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* dataExchange) override;

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT controlColor);
    afx_msg void OnSignIn();
    afx_msg void OnExit();
    afx_msg void OnClose();
    afx_msg LRESULT OnLoginDone(WPARAM success, LPARAM result);
    DECLARE_MESSAGE_MAP()

private:
    void CreateFonts();
    void LoadDefaults();
    void LayoutControls();
    void SetStatus(const CString& text, bool error);
    void SetFormEnabled(bool enabled);

    std::shared_ptr<ChatSession>& m_outSession;
    CEdit m_server;
    CEdit m_port;
    CEdit m_user;
    CEdit m_password;
    CFlatButton m_signIn;
    CFlatButton m_exit;

    CString m_serverText;
    CString m_portText;
    int m_udpPort = 5001;
    int m_heartbeatMs = 5000;
    bool m_connecting = false;

    CString m_status = L"Enter your account details to continue.";
    bool m_statusError = false;

    CRect m_serverRect;
    CRect m_portRect;
    CRect m_userRect;
    CRect m_passwordRect;

    CFont m_brandFont;
    CFont m_brandSubFont;
    CFont m_logoFont;
    CFont m_titleFont;
    CFont m_subtitleFont;
    CFont m_labelFont;
    CFont m_editFont;
    CFont m_featureFont;
    CFont m_statusFont;
    CFont m_footerFont;

    CBrush m_fieldBrush;
};
