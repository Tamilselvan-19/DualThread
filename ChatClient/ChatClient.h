#pragma once

#include <afxwin.h>

#include "resource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

// Worker -> UI messages. The lParam is always a heap-allocated std::string
// owned by the message handler.
#define WM_APP_SESSION_FRAME     (WM_APP + 20)
#define WM_APP_SESSION_STATUS    (WM_APP + 21)
#define WM_APP_SESSION_HEARTBEAT (WM_APP + 22)
#define WM_APP_LOGIN_DONE        (WM_APP + 30)
#define WM_APP_SEND_REQUESTED    (WM_APP + 40)
#define WM_APP_CONVERSATION_SELECTED (WM_APP + 41)

enum SessionStatus {
    SESSION_CONNECTING = 1,
    SESSION_CONNECTED = 2,
    SESSION_DISCONNECTED = 3,
    SESSION_RECONNECTING = 4,
    SESSION_AUTH_FAILED = 5
};

#define ID_LOGOUT 200

class CChatClientApp : public CWinApp {
public:
    CChatClientApp();
    virtual BOOL InitInstance();
    virtual int ExitInstance();

private:
    ULONG_PTR m_gdiplusToken = 0;
};

extern CChatClientApp theApp;
