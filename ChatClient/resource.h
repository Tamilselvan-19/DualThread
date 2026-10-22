#pragma once

// Dialogs
#define IDD_LOGIN_DIALOG        101
#define IDD_CHATCLIENT_DIALOG   101   // backwards-compatible alias
#define IDD_MAIN_DIALOG         102
#define IDD_GROUP_DIALOG        103

// Legacy control ids (kept so old references remain valid)
#define IDC_EDIT_SERVERIP       1001
#define IDC_EDIT_PORT           1002
#define IDC_EDIT_USERID         1003
#define IDC_EDIT_PASSWORD       1004
#define IDC_EDIT_MESSAGE        1005
#define IDC_BTN_CONNECT         1006
#define IDC_BTN_DISCONNECT      1007
#define IDC_BTN_SEND            1008
#define IDC_LIST_CHAT           1009
#define IDC_STATIC_STATUS       1010
#define IDC_STATIC_HEARTBEAT    1011

// Login screen
#define IDC_LOGIN_SERVERIP      2001
#define IDC_LOGIN_PORT          2002
#define IDC_LOGIN_USERID        2003
#define IDC_LOGIN_PASSWORD      2004
#define IDC_LOGIN_BTN           2005
#define IDC_LOGIN_EXIT          2006
#define IDC_LOGIN_STATUS        2007

// Main window
#define IDC_CONV_LIST           2101
#define IDC_MSG_VIEW            2102
#define IDC_EDIT_COMPOSE        2103
#define IDC_BTN_SEND_MAIN       2104
#define IDC_BTN_NEWGROUP        2105

// Group dialog
#define IDC_GROUP_CREATE_RADIO  2201
#define IDC_GROUP_JOIN_RADIO    2202
#define IDC_GROUP_NAME          2203
#define IDC_GROUP_ID            2204
#define IDC_GROUP_MEMBERS       2205
#define IDC_GROUP_OK            2206
#define IDC_GROUP_CANCEL        2207
