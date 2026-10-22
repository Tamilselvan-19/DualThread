#pragma once

#include <string>
#include <vector>

#include <afxwin.h>
#include <gdiplus.h>

namespace ui {
    constexpr COLORREF Sidebar = RGB(17, 24, 39);
    constexpr COLORREF SidebarRaised = RGB(30, 41, 59);
    constexpr COLORREF SidebarHover = RGB(39, 51, 70);
    constexpr COLORREF SidebarText = RGB(226, 232, 240);
    constexpr COLORREF SidebarMuted = RGB(148, 163, 184);
    constexpr COLORREF Accent = RGB(79, 70, 229);
    constexpr COLORREF AccentHover = RGB(67, 56, 202);
    constexpr COLORREF AccentSoft = RGB(224, 231, 255);
    constexpr COLORREF WindowBg = RGB(244, 246, 250);
    constexpr COLORREF Card = RGB(255, 255, 255);
    constexpr COLORREF Text = RGB(17, 24, 39);
    constexpr COLORREF Muted = RGB(100, 116, 139);
    constexpr COLORREF Border = RGB(226, 232, 240);
    constexpr COLORREF Online = RGB(34, 197, 94);
    constexpr COLORREF Danger = RGB(220, 38, 38);
    constexpr COLORREF DangerSoft = RGB(254, 226, 226);
    constexpr COLORREF WarnSoft = RGB(254, 243, 199);
    constexpr COLORREF WarnText = RGB(146, 64, 14);

    inline int Scale(HWND wnd, int value) {
        UINT dpi = 96;
        if (wnd) {
            HMONITOR monitor = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
            typedef UINT(WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
            HMODULE shcore = LoadLibraryW(L"Shcore.dll");
            if (shcore) {
                auto fn = reinterpret_cast<GetDpiForMonitorFn>(
                    GetProcAddress(shcore, "GetDpiForMonitor"));
                if (fn) {
                    UINT x = 96, y = 96;
                    if (SUCCEEDED(fn(monitor, 0, &x, &y))) dpi = y;
                }
                FreeLibrary(shcore);
            }
        }
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    inline CString FromUtf8(const std::string& value) {
        if (value.empty()) return CString();
        int length = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
        if (length <= 0) return CString();
        CString out;
        wchar_t* buffer = out.GetBuffer(length);
        MultiByteToWideChar(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()), buffer, length);
        out.ReleaseBuffer(length);
        return out;
    }

    inline std::string ToUtf8(const CString& value) {
        if (value.IsEmpty()) return std::string();
        int length = WideCharToMultiByte(CP_UTF8, 0, value, value.GetLength(),
                                         nullptr, 0, nullptr, nullptr);
        if (length <= 0) return std::string();
        std::string out(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, value.GetLength(),
                            &out[0], length, nullptr, nullptr);
        return out;
    }

    inline CString DisplayTime(const CString& timestamp) {
        if (timestamp.GetLength() >= 16 && timestamp[10] == L'T') {
            return timestamp.Mid(11, 5);
        }
        CTime now = CTime::GetCurrentTime();
        return now.Format(L"%H:%M");
    }

    inline CString NowTime() {
        return CTime::GetCurrentTime().Format(L"%H:%M");
    }

    inline CString Initials(const CString& name) {
        CString trimmed = name;
        trimmed.Trim();
        if (trimmed.IsEmpty()) return L"?";
        CString out;
        out.AppendChar(towupper(trimmed[0]));
        for (int i = 1; i < trimmed.GetLength(); ++i) {
            if (trimmed[i] == L' ' && i + 1 < trimmed.GetLength()) {
                out.AppendChar(towupper(trimmed[i + 1]));
                break;
            }
        }
        return out;
    }

    inline COLORREF AvatarColor(const CString& name) {
        static const COLORREF palette[] = {
            RGB(79, 70, 229), RGB(14, 165, 233), RGB(16, 185, 129),
            RGB(245, 158, 11), RGB(236, 72, 153), RGB(139, 92, 246),
            RGB(239, 68, 68), RGB(20, 184, 166)
        };
        unsigned int hash = 2166136261u;
        for (int i = 0; i < name.GetLength(); ++i) {
            hash ^= static_cast<unsigned int>(name[i]);
            hash *= 16777619u;
        }
        return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
    }

    inline void AddRoundedRect(Gdiplus::GraphicsPath& path,
                               const Gdiplus::RectF& rect, float radius) {
        float d = radius * 2.0f;
        if (d > rect.Width) d = rect.Width;
        if (d > rect.Height) d = rect.Height;
        path.Reset();
        path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }

    inline void FillRounded(CDC& dc, const CRect& rect, int radius,
                            COLORREF fill, COLORREF border = CLR_NONE,
                            float borderWidth = 1.0f) {
        Gdiplus::Graphics graphics(dc.GetSafeHdc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        AddRoundedRect(path,
            Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                           static_cast<float>(rect.Width()), static_cast<float>(rect.Height())),
            static_cast<float>(radius));
        Gdiplus::SolidBrush brush(Gdiplus::Color(
            255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        graphics.FillPath(&brush, &path);
        if (border != CLR_NONE) {
            Gdiplus::Pen pen(Gdiplus::Color(
                255, GetRValue(border), GetGValue(border), GetBValue(border)),
                borderWidth);
            graphics.DrawPath(&pen, &path);
        }
    }

    inline void FillCircle(CDC& dc, const CRect& rect, COLORREF fill) {
        Gdiplus::Graphics graphics(dc.GetSafeHdc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush brush(Gdiplus::Color(
            255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        graphics.FillEllipse(&brush, rect.left, rect.top,
                             rect.Width(), rect.Height());
    }

    inline void DrawCircleBorder(CDC& dc, const CRect& rect, COLORREF color, float width) {
        Gdiplus::Graphics graphics(dc.GetSafeHdc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(
            255, GetRValue(color), GetGValue(color), GetBValue(color)), width);
        graphics.DrawEllipse(&pen, rect.left, rect.top, rect.Width(), rect.Height());
    }

    inline std::vector<CString> SplitCsv(const CString& value) {
        std::vector<CString> items;
        int start = 0;
        while (start <= value.GetLength()) {
            int comma = value.Find(L',', start);
            CString item = comma < 0 ? value.Mid(start)
                                     : value.Mid(start, comma - start);
            item.Trim();
            if (!item.IsEmpty()) items.push_back(item);
            if (comma < 0) break;
            start = comma + 1;
        }
        return items;
    }

    inline CString JoinCsv(const std::vector<CString>& values) {
        CString out;
        for (const auto& value : values) {
            if (!out.IsEmpty()) out += L",";
            out += value;
        }
        return out;
    }
}
