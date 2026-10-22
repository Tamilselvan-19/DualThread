#include "ChatClient.h"

#include <memory>

#include <gdiplus.h>

#include "ChatSession.h"
#include "LoginDlg.h"
#include "MainDlg.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CChatClientApp theApp;

CChatClientApp::CChatClientApp() {
}

BOOL CChatClientApp::InitInstance() {
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    if (!CWinApp::InitInstance()) return FALSE;

    WSADATA winsock = {};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        AfxMessageBox(L"Could not initialize Windows Sockets.", MB_ICONERROR | MB_OK);
        return FALSE;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        WSACleanup();
        AfxMessageBox(L"Could not initialize the graphics engine.", MB_ICONERROR | MB_OK);
        return FALSE;
    }

    while (true) {
        std::shared_ptr<ChatSession> session;
        CLoginDlg login(session);
        INT_PTR loginResult = login.DoModal();
        if (loginResult != IDOK || !session) break;

        CMainDlg mainWindow(session);
        INT_PTR mainResult = mainWindow.DoModal();
        if (mainResult == ID_LOGOUT) continue;
        break;
    }

    if (m_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    WSACleanup();
    return FALSE;
}

int CChatClientApp::ExitInstance() {
    return CWinApp::ExitInstance();
}
