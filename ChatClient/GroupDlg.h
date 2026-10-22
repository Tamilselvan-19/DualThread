#pragma once

#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdialogex.h>

#include "resource.h"
#include "UiControls.h"

class CGroupDlg : public CDialogEx {
public:
    explicit CGroupDlg(CWnd* parent = nullptr);
    enum { IDD = IDD_GROUP_DIALOG };

    int Mode() const { return m_mode; }
    CString GroupName() const { return m_groupName; }
    CString GroupId() const { return m_groupId; }
    CString Members() const { return m_members; }

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* dataExchange) override;
    void OnOK() override;

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT controlColor);
    afx_msg void OnModeChanged();
    afx_msg void OnCancel();
    DECLARE_MESSAGE_MAP()

private:
    void CreateFonts();
    void LayoutControls();
    void ApplyMode();

    CButton m_createRadio;
    CButton m_joinRadio;
    CEdit m_name;
    CEdit m_membersEdit;
    CEdit m_groupIdEdit;
    CFlatButton m_ok;
    CFlatButton m_cancel;

    int m_mode = 0;
    CString m_groupName;
    CString m_groupId;
    CString m_members;

    CRect m_nameRect;
    CRect m_membersRect;
    CRect m_groupIdRect;

    CFont m_titleFont;
    CFont m_subtitleFont;
    CFont m_labelFont;
    CFont m_editFont;
    CFont m_radioFont;
    CBrush m_fieldBrush;
};
