#include "GroupDlg.h"

#include "ChatClient.h"

BEGIN_MESSAGE_MAP(CGroupDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_BN_CLICKED(IDC_GROUP_CREATE_RADIO, OnModeChanged)
    ON_BN_CLICKED(IDC_GROUP_JOIN_RADIO, OnModeChanged)
    ON_BN_CLICKED(IDC_GROUP_OK, OnOK)
    ON_BN_CLICKED(IDC_GROUP_CANCEL, OnCancel)
END_MESSAGE_MAP()

CGroupDlg::CGroupDlg(CWnd* parent) : CDialogEx(IDD_GROUP_DIALOG, parent) {
}

BOOL CGroupDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();
    SetWindowText(L"DualThread - Groups");
    SetWindowPos(nullptr, 0, 0, 520, 430, SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    CreateFonts();
    m_fieldBrush.CreateSolidBrush(RGB(248, 250, 252));

    CRect empty(0, 0, 10, 10);
    m_createRadio.Create(L"Create a new group",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
                         empty, this, IDC_GROUP_CREATE_RADIO);
    m_joinRadio.Create(L"Join with a group ID",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                       empty, this, IDC_GROUP_JOIN_RADIO);
    m_createRadio.SetFont(&m_radioFont);
    m_joinRadio.SetFont(&m_radioFont);
    m_createRadio.SetCheck(BST_CHECKED);

    m_name.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                  empty, this, IDC_GROUP_NAME);
    m_membersEdit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                     empty, this, IDC_GROUP_MEMBERS);
    m_groupIdEdit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                     empty, this, IDC_GROUP_ID);
    m_name.SetFont(&m_editFont);
    m_membersEdit.SetFont(&m_editFont);
    m_groupIdEdit.SetFont(&m_editFont);
    m_name.SetLimitText(128);
    m_membersEdit.SetLimitText(512);
    m_groupIdEdit.SetLimitText(128);

    const UINT marginFlags = EC_LEFTMARGIN | EC_RIGHTMARGIN;
    m_name.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));
    m_membersEdit.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));
    m_groupIdEdit.SendMessage(EM_SETMARGINS, marginFlags, MAKELPARAM(12, 12));
    m_name.SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. Product launch"));
    m_membersEdit.SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"user2,user3"));
    m_groupIdEdit.SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"grp-..."));

    m_ok.Create(L"Create group", CRect(0, 0, 10, 10), this, IDC_GROUP_OK);
    m_ok.SetColors(ui::Accent, ui::AccentHover, RGB(255, 255, 255));
    m_ok.SetFont(&m_editFont);
    m_cancel.Create(L"Cancel", CRect(0, 0, 10, 10), this, IDC_GROUP_CANCEL);
    m_cancel.SetColors(RGB(241, 245, 249), RGB(226, 232, 240), ui::Muted);
    m_cancel.SetFont(&m_editFont);

    LayoutControls();
    ApplyMode();
    m_name.SetFocus();
    return FALSE;
}

void CGroupDlg::DoDataExchange(CDataExchange* dataExchange) {
    CDialogEx::DoDataExchange(dataExchange);
}

void CGroupDlg::CreateFonts() {
    CDC* screen = GetDC();
    int dpi = GetDeviceCaps(screen->GetSafeHdc(), LOGPIXELSY);
    ReleaseDC(screen);
    if (dpi <= 0) dpi = 96;
    auto create = [dpi](CFont& font, int points, int weight) {
        font.CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    create(m_titleFont, 17, FW_SEMIBOLD);
    create(m_subtitleFont, 9, FW_NORMAL);
    create(m_labelFont, 8, FW_SEMIBOLD);
    create(m_editFont, 10, FW_NORMAL);
    create(m_radioFont, 9, FW_NORMAL);
}

void CGroupDlg::LayoutControls() {
    CRect client;
    GetClientRect(&client);
    int left = 28;
    int right = client.right - 28;

    m_createRadio.MoveWindow(CRect(left, 88, left + 200, 112));
    m_joinRadio.MoveWindow(CRect(left + 210, 88, right, 112));

    m_nameRect = CRect(left, 154, right, 196);
    m_membersRect = CRect(left, 234, right, 276);
    m_groupIdRect = CRect(left, 154, right, 196);
    m_name.MoveWindow(m_nameRect);
    m_membersEdit.MoveWindow(m_membersRect);
    m_groupIdEdit.MoveWindow(m_groupIdRect);

    m_ok.MoveWindow(CRect(left, client.bottom - 74, left + 150, client.bottom - 30));
    m_cancel.MoveWindow(CRect(left + 160, client.bottom - 74,
                              left + 260, client.bottom - 30));
    Invalidate();
}

void CGroupDlg::ApplyMode() {
    const bool create = (m_mode == 0);
    m_name.ShowWindow(create ? SW_SHOW : SW_HIDE);
    m_membersEdit.ShowWindow(create ? SW_SHOW : SW_HIDE);
    m_groupIdEdit.ShowWindow(create ? SW_HIDE : SW_SHOW);
    m_name.EnableWindow(create);
    m_membersEdit.EnableWindow(create);
    m_groupIdEdit.EnableWindow(!create);
    m_ok.SetWindowText(create ? L"Create group" : L"Join group");
    SetWindowText(create ? L"DualThread - Create group" : L"DualThread - Join group");
    Invalidate();
}

void CGroupDlg::OnModeChanged() {
    m_mode = m_joinRadio.GetCheck() == BST_CHECKED ? 1 : 0;
    ApplyMode();
    if (m_mode == 0) m_name.SetFocus();
    else m_groupIdEdit.SetFocus();
}

void CGroupDlg::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(client, RGB(255, 255, 255));

    CRect title(client.left + 28, 24, client.right - 28, 58);
    CFont* old = dc.SelectObject(&m_titleFont);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(ui::Text);
    dc.DrawText(m_mode == 0 ? L"Create a group" : L"Join a group",
                &title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    dc.SelectObject(old);

    CRect subtitle(client.left + 28, 60, client.right - 28, 82);
    old = dc.SelectObject(&m_subtitleFont);
    dc.SetTextColor(ui::Muted);
    dc.DrawText(m_mode == 0
                    ? L"Name your group and add teammates by user ID."
                    : L"Enter the group ID shared by the group owner.",
                &subtitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    dc.SelectObject(old);

    if (m_mode == 0) {
        ui::FillRounded(dc, m_nameRect, 9, RGB(248, 250, 252), ui::Border);
        ui::FillRounded(dc, m_membersRect, 9, RGB(248, 250, 252), ui::Border);
        old = dc.SelectObject(&m_labelFont);
        dc.SetTextColor(ui::Muted);
        CRect nameLabel(28, 134, client.right - 28, 152);
        dc.DrawText(L"GROUP NAME", &nameLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        CRect membersLabel(28, 214, client.right - 28, 232);
        dc.DrawText(L"MEMBERS (OPTIONAL, COMMA-SEPARATED USER IDS)",
                    &membersLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(old);

        CRect hint(28, 290, client.right - 28, 312);
        old = dc.SelectObject(&m_subtitleFont);
        dc.SetTextColor(ui::Muted);
        dc.DrawText(L"You are added automatically as the group owner.",
                    &hint, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(old);
    } else {
        ui::FillRounded(dc, m_groupIdRect, 9, RGB(248, 250, 252), ui::Border);
        old = dc.SelectObject(&m_labelFont);
        dc.SetTextColor(ui::Muted);
        CRect idLabel(28, 134, client.right - 28, 152);
        dc.DrawText(L"GROUP ID", &idLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(old);

        CRect hint(28, 220, client.right - 28, 260);
        old = dc.SelectObject(&m_subtitleFont);
        dc.SetTextColor(ui::Muted);
        dc.DrawText(L"Ask the group owner for the ID. Your history starts as soon as you join.",
                    &hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        dc.SelectObject(old);
    }
}

BOOL CGroupDlg::OnEraseBkgnd(CDC*) {
    return TRUE;
}

HBRUSH CGroupDlg::OnCtlColor(CDC* dc, CWnd* window, UINT controlColor) {
    HBRUSH brush = CDialogEx::OnCtlColor(dc, window, controlColor);
    if (!window) return brush;
    const UINT id = window->GetDlgCtrlID();
    if (id == IDC_GROUP_NAME || id == IDC_GROUP_MEMBERS || id == IDC_GROUP_ID) {
        dc->SetTextColor(ui::Text);
        dc->SetBkColor(RGB(248, 250, 252));
        return static_cast<HBRUSH>(m_fieldBrush.GetSafeHandle());
    }
    if (id == IDC_GROUP_CREATE_RADIO || id == IDC_GROUP_JOIN_RADIO) {
        dc->SetTextColor(ui::Text);
        dc->SetBkColor(RGB(255, 255, 255));
        return static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    }
    return brush;
}

void CGroupDlg::OnOK() {
    CString name;
    CString members;
    CString groupId;
    m_name.GetWindowText(name);
    m_membersEdit.GetWindowText(members);
    m_groupIdEdit.GetWindowText(groupId);
    name.Trim();
    members.Trim();
    groupId.Trim();

    if (m_mode == 0) {
        if (name.IsEmpty()) {
            AfxMessageBox(L"Enter a group name.", MB_ICONINFORMATION | MB_OK);
            m_name.SetFocus();
            return;
        }
        if (name.GetLength() > 128) {
            AfxMessageBox(L"Group name must be 128 characters or fewer.",
                          MB_ICONINFORMATION | MB_OK);
            m_name.SetFocus();
            return;
        }
        m_groupName = name;
        m_members = members;
        m_groupId.Empty();
    } else {
        if (groupId.IsEmpty()) {
            AfxMessageBox(L"Enter the group ID.", MB_ICONINFORMATION | MB_OK);
            m_groupIdEdit.SetFocus();
            return;
        }
        m_groupId = groupId;
        m_groupName.Empty();
        m_members.Empty();
    }

    EndDialog(IDOK);
}

void CGroupDlg::OnCancel() {
    CDialogEx::OnCancel();
}
