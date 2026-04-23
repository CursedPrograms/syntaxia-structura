/*
 * StructArch.exe — Structura Architect  (Page-Layout DTP Editor)
 * Compile (MinGW):
 *   g++ -std=c++14 -mwindows -O2 -static-libgcc -static-libstdc++ \
 *       -o StructArch.exe structarch.cpp \
 *       -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

// ── IDs ───────────────────────────────────────────────────────────────────────
#define ID_FILE_NEW        101
#define ID_FILE_OPEN       102
#define ID_FILE_SAVE       103
#define ID_FILE_SAVEAS     104
#define ID_FILE_EXIT       105
#define ID_INSERT_TEXTBOX  201
#define ID_INSERT_PAGE     202
#define ID_VIEW_ZOOMIN     301
#define ID_VIEW_ZOOMOUT    302
#define ID_VIEW_FIT        303
#define ID_TOOLS_PAGESETUP 401
#define ID_HELP_CREDITS    501

#define IDC_PAGES_LIST   1001
#define IDC_BTN_NEWPAGE  1002
#define IDC_BTN_DELPAGE  1003
#define IDC_CANVAS       1004
#define IDC_APPLY        1005
#define IDC_BTN_COLOR    1006
#define IDC_BTN_BGCOLOR  1007
#define IDC_TOOL_SELECT  1008
#define IDC_TOOL_TEXT    1009
#define IDC_PROP_TEXT    1010
#define IDC_PROP_FSIZE   1011
#define IDC_PROP_ALIGN   1012
#define IDC_PROP_BINDING 1013
#define IDC_COLORPREV    1014
#define IDC_BGCOLORPREV  1015
#define IDC_HASBG        1016

// Color picker dialog
#define IDC_CP_HEX       2001
#define IDC_CP_BRIGHT    2002
#define IDC_CP_PREV      2003

// ── Color helpers ─────────────────────────────────────────────────────────────
static void hsvToRgb(float h, float s, float v, int &r, int &g, int &b) {
    h = fmodf(h, 360.f); if (h < 0) h += 360.f;
    float c = v * s, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
    float r1 = 0, g1 = 0, b1 = 0;
    if (h < 60)       { r1 = c; g1 = x; }
    else if (h < 120) { r1 = x; g1 = c; }
    else if (h < 180) { g1 = c; b1 = x; }
    else if (h < 240) { g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; b1 = c; }
    else              { r1 = c; b1 = x; }
    r = (int)((r1 + m) * 255);
    g = (int)((g1 + m) * 255);
    b = (int)((b1 + m) * 255);
}
static void rgbToHsv(COLORREF col, float &h, float &s, float &v) {
    float rf = GetRValue(col) / 255.f, gf = GetGValue(col) / 255.f, bf = GetBValue(col) / 255.f;
    float mx = std::max({ rf, gf, bf }), mn = std::min({ rf, gf, bf }), d = mx - mn;
    v = mx; s = (mx > 0 ? d / mx : 0.f);
    if (d < 1e-6f) { h = 0; return; }
    if (mx == rf)      h = 60.f * (gf - bf) / d;
    else if (mx == gf) h = 60.f * ((bf - rf) / d + 2.f);
    else               h = 60.f * ((rf - gf) / d + 4.f);
    if (h < 0) h += 360.f;
}
static std::string colToHex(COLORREF c) {
    char buf[8]; sprintf(buf, "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}
static COLORREF hexToCol(const std::string &s) {
    std::string h = s; if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() < 6) return 0;
    unsigned long v = strtoul(h.c_str(), nullptr, 16);
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ── String helpers ────────────────────────────────────────────────────────────
static std::wstring toW(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back(); return w;
}
static std::string fromW(const std::wstring &w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back(); return s;
}
static std::string getWndText(HWND h) {
    int n = GetWindowTextLengthW(h) + 1; std::wstring w(n, 0);
    GetWindowTextW(h, &w[0], n); if (!w.empty() && w.back() == 0) w.pop_back();
    return fromW(w);
}
static void setWndText(HWND h, const std::string &s) { SetWindowTextW(h, toW(s).c_str()); }
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ── Data model ────────────────────────────────────────────────────────────────
struct TextBox {
    std::string id;
    float x = 20, y = 20, w = 160, h = 40;   // mm on page
    std::string text = "Text";
    COLORREF textColor = RGB(0, 0, 0);
    COLORREF bgColor   = RGB(255, 255, 255);
    bool     hasBg     = false;
    int      fontSize  = 12;
    int      align     = 0;   // 0=left 1=center 2=right
    std::string varBinding;
};

struct Page { std::vector<TextBox> boxes; };

static const struct { const char *name; int w, h; } PAGE_SIZES[] = {
    { "A4",      210, 297 }, { "A3",     297, 420 }, { "A5",    148, 210 },
    { "Letter",  216, 279 }, { "Legal",  216, 356 }, { "Tabloid",279,432 },
};
static const int NPAGE_SIZES = 6;

struct Document {
    std::string title = "Untitled", author, description;
    int sizeIdx = 0, pageW = 210, pageH = 297;
    std::vector<Page> pages;
};

static Document     g_doc;
static int          g_curPage  = 0;
static int          g_selBox   = -1;
static std::string  g_path;
static bool         g_dirty    = false;
static int          g_tool     = 0;   // 0=select  1=textbox
static float        g_zoom     = 1.0f;
static int          g_scrollX  = 0, g_scrollY = 0;
static bool         g_dragging = false, g_creating = false;
static int          g_dragX    = 0,    g_dragY    = 0;
static RECT         g_createRc = {};
static int          g_tbSerial = 0;

static HWND  g_hwnd, g_hCanvas, g_hPagesList, g_hPropsPanel;
static HWND  g_hBtnNew, g_hBtnDel;
static HWND  g_hPropText, g_hPropFsize, g_hPropAlign, g_hPropBind;
static HWND  g_hColorPrev, g_hBgPrev, g_hHasBg;
static HWND  g_hToolSel, g_hToolTxt;
static HFONT g_font, g_fontBold;

static COLORREF g_curTextCol = RGB(0, 0, 0);
static COLORREF g_curBgCol   = RGB(255, 255, 255);
static bool     g_curHasBg   = false;
static HBRUSH   g_brushText  = nullptr, g_brushBg = nullptr;

static std::string newId() { return "tb" + std::to_string(++g_tbSerial); }

// ── Canvas math ───────────────────────────────────────────────────────────────
static float ppm()      { return 3.0f * g_zoom; }
static int   mm2px(float m) { return (int)(m * ppm()); }
static float px2mm(int p)   { return p / ppm(); }

static int canvasPageX(HWND hc) {
    RECT rc; GetClientRect(hc, &rc);
    return (rc.right - mm2px((float)g_doc.pageW)) / 2 - g_scrollX;
}
static int canvasPageY(int idx) {
    int y = 20 - g_scrollY;
    for (int i = 0; i < idx; i++) y += mm2px((float)g_doc.pageH) + 20;
    return y;
}
static bool hitPage(HWND hc, int cx, int cy, int &pi, float &pmx, float &pmy) {
    int px0 = canvasPageX(hc), pw = mm2px((float)g_doc.pageW), ph = mm2px((float)g_doc.pageH);
    for (int i = 0; i < (int)g_doc.pages.size(); i++) {
        int py = canvasPageY(i);
        if (cx >= px0 && cx < px0 + pw && cy >= py && cy < py + ph) {
            pi = i; pmx = px2mm(cx - px0); pmy = px2mm(cy - py); return true;
        }
    }
    return false;
}

// ── Custom color picker ────────────────────────────────────────────────────────
struct CPData { float h, s, v; COLORREF result; bool ok; };

static HBITMAP makeWheel(int sz, float brightness) {
    BITMAPINFOHEADER bi = {}; bi.biSize = sizeof(bi); bi.biWidth = sz;
    bi.biHeight = -sz; bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
    BITMAPINFO bmi; bmi.bmiHeader = bi;
    void *bits = nullptr;
    HBITMAP hbm = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm || !bits) return nullptr;
    DWORD *px = (DWORD *)bits;
    float cx = sz / 2.f, cy = sz / 2.f, rad = cx - 2.f;
    for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
        float dx = x - cx, dy = y - cy, dist = sqrtf(dx * dx + dy * dy);
        if (dist > rad) { px[y * sz + x] = 0xFF808080; continue; }
        float hue = atan2f(dy, dx) * 180.f / 3.14159265f; if (hue < 0) hue += 360.f;
        int r, g, b; hsvToRgb(hue, dist / rad, brightness, r, g, b);
        px[y * sz + x] = (DWORD)b | ((DWORD)g << 8) | ((DWORD)r << 16) | 0xFF000000;
    }
    return hbm;
}

static LRESULT CALLBACK CPProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    CPData *d = (CPData *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    static const int WH = 200, WX = 10, WY = 10;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        // Draw wheel
        HBITMAP hbm = makeWheel(WH, d->v);
        HDC mdc = CreateCompatibleDC(hdc); HBITMAP old = (HBITMAP)SelectObject(mdc, hbm);
        BitBlt(hdc, WX, WY, WH, WH, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old); DeleteDC(mdc); DeleteObject(hbm);
        // Draw crosshair on wheel
        {
            float cx = WX + WH / 2.f + d->s * (WH / 2.f - 2) * cosf(d->h * 3.14159f / 180.f);
            float cy = WY + WH / 2.f + d->s * (WH / 2.f - 2) * sinf(d->h * 3.14159f / 180.f);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HPEN op  = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, (int)cx - 5, (int)cy, nullptr); LineTo(hdc, (int)cx + 5, (int)cy);
            MoveToEx(hdc, (int)cx, (int)cy - 5, nullptr); LineTo(hdc, (int)cx, (int)cy + 5);
            SelectObject(hdc, op); DeleteObject(pen);
        }
        // Preview swatch
        int r, g, b; hsvToRgb(d->h, d->s, d->v, r, g, b);
        COLORREF col = RGB(r, g, b);
        HBRUSH br = CreateSolidBrush(col);
        RECT pr = { 220, 10, 340, 70 }; FillRect(hdc, &pr, br); DeleteObject(br);
        FrameRect(hdc, &pr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE: {
        if (msg == WM_MOUSEMOVE && !(wP & MK_LBUTTON)) break;
        int cx = LOWORD(lP), cy = HIWORD(lP);
        float dx = cx - (WX + WH / 2.f), dy = cy - (WY + WH / 2.f);
        float dist = sqrtf(dx * dx + dy * dy), rad = WH / 2.f - 2.f;
        if (dist <= rad) {
            d->h = atan2f(dy, dx) * 180.f / 3.14159265f; if (d->h < 0) d->h += 360.f;
            d->s = dist / rad;
            int r, g, b; hsvToRgb(d->h, d->s, d->v, r, g, b);
            char buf[8]; sprintf(buf, "#%02X%02X%02X", r, g, b);
            HWND hHex = GetDlgItem(hWnd, IDC_CP_HEX); setWndText(hHex, buf);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_HSCROLL: {
        HWND hTrack = (HWND)lP;
        if (GetDlgItem(hWnd, IDC_CP_BRIGHT) == hTrack) {
            int pos = (int)SendMessageW(hTrack, TBM_GETPOS, 0, 0);
            d->v = pos / 100.f;
            int r, g, b; hsvToRgb(d->h, d->s, d->v, r, g, b);
            char buf[8]; sprintf(buf, "#%02X%02X%02X", r, g, b);
            setWndText(GetDlgItem(hWnd, IDC_CP_HEX), buf);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wP), notif = HIWORD(wP);
        if (id == IDC_CP_HEX && notif == EN_CHANGE) {
            std::string hex = getWndText(GetDlgItem(hWnd, IDC_CP_HEX));
            if (hex.size() >= 7) {
                COLORREF col = hexToCol(hex);
                rgbToHsv(col, d->h, d->s, d->v);
                HWND hTrk = GetDlgItem(hWnd, IDC_CP_BRIGHT);
                SendMessageW(hTrk, TBM_SETPOS, TRUE, (LPARAM)(int)(d->v * 100));
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        if (id == IDOK) {
            int r, g, b; hsvToRgb(d->h, d->s, d->v, r, g, b);
            d->result = RGB(r, g, b); d->ok = true;
            PostMessageW(hWnd, WM_USER, 0, 0);
        }
        if (id == IDCANCEL) PostMessageW(hWnd, WM_USER, 0, 0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

static bool showColorPicker(HWND parent, COLORREF &result) {
    static bool regDone = false;
    if (!regDone) {
        WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = CPProc;
        wc.hInstance = GetModuleHandle(nullptr); wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1); wc.lpszClassName = L"ColorPicker";
        RegisterClassExW(&wc); regDone = true;
    }
    CPData data = {}; rgbToHsv(result, data.h, data.s, data.v); data.ok = false;
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"ColorPicker",
        L"Color Picker", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        200, 150, 370, 320, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&data);

    // Hex input label + field
    HWND hLbl = CreateWindowExW(0, L"STATIC", L"Hex:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        220, 80, 30, 20, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLbl, WM_SETFONT, (WPARAM)g_font, TRUE);
    HWND hHex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", toW(colToHex(result)).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 255, 78, 90, 22, hDlg,
        (HMENU)IDC_CP_HEX, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hHex, WM_SETFONT, (WPARAM)g_font, TRUE);

    // Brightness trackbar
    HWND hBLbl = CreateWindowExW(0, L"STATIC", L"Brightness:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 220, 80, 18, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hBLbl, WM_SETFONT, (WPARAM)g_font, TRUE);
    HWND hTrack = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        95, 218, 250, 24, hDlg, (HMENU)IDC_CP_BRIGHT, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(hTrack, TBM_SETPOS, TRUE, (LPARAM)(int)(data.v * 100));

    // OK / Cancel
    auto mkBtn = [&](const wchar_t *t, int x, HMENU id) {
        HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, 255, 80, 26, hDlg, id, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    };
    mkBtn(L"OK", 80, (HMENU)IDOK); mkBtn(L"Cancel", 175, (HMENU)IDCANCEL);

    ShowWindow(hDlg, SW_SHOW); EnableWindow(parent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
        if (!IsWindow(hDlg)) break;
        if (msg.message == WM_USER && msg.hwnd == hDlg) break;
    }
    EnableWindow(parent, TRUE);
    if (IsWindow(hDlg)) DestroyWindow(hDlg);
    SetForegroundWindow(parent);
    result = data.result; return data.ok;
}

// ── Prop panel helpers ────────────────────────────────────────────────────────
static void syncPropsFromSel() {
    bool has = g_selBox >= 0 && g_curPage >= 0 && g_curPage < (int)g_doc.pages.size()
               && g_selBox < (int)g_doc.pages[g_curPage].boxes.size();
    EnableWindow(g_hPropText,  has);
    EnableWindow(g_hPropFsize, has);
    EnableWindow(g_hPropAlign, has);
    EnableWindow(g_hPropBind,  has);
    if (!has) { setWndText(g_hPropText, ""); setWndText(g_hPropFsize, "12"); return; }
    const TextBox &tb = g_doc.pages[g_curPage].boxes[g_selBox];
    setWndText(g_hPropText,  tb.text);
    setWndText(g_hPropFsize, std::to_string(tb.fontSize));
    SendMessageW(g_hPropAlign, CB_SETCURSEL, tb.align, 0);
    setWndText(g_hPropBind,  tb.varBinding);
    SendMessageW(g_hHasBg, BM_SETCHECK, tb.hasBg ? BST_CHECKED : BST_UNCHECKED, 0);
    g_curTextCol = tb.textColor; g_curBgCol = tb.bgColor; g_curHasBg = tb.hasBg;
    if (g_brushText) { DeleteObject(g_brushText); g_brushText = nullptr; }
    if (g_brushBg)   { DeleteObject(g_brushBg);   g_brushBg   = nullptr; }
    InvalidateRect(g_hColorPrev, nullptr, TRUE); InvalidateRect(g_hBgPrev, nullptr, TRUE);
}

static void applyPropsToSel() {
    if (g_selBox < 0 || g_curPage < 0 || g_curPage >= (int)g_doc.pages.size()) return;
    if (g_selBox >= (int)g_doc.pages[g_curPage].boxes.size()) return;
    TextBox &tb = g_doc.pages[g_curPage].boxes[g_selBox];
    tb.text       = getWndText(g_hPropText);
    try { tb.fontSize = std::stoi(getWndText(g_hPropFsize)); } catch (...) {}
    tb.align      = std::max(0, (int)SendMessageW(g_hPropAlign, CB_GETCURSEL, 0, 0));
    tb.varBinding = getWndText(g_hPropBind);
    tb.textColor  = g_curTextCol;
    tb.bgColor    = g_curBgCol;
    tb.hasBg      = (SendMessageW(g_hHasBg, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_dirty       = true;
    InvalidateRect(g_hCanvas, nullptr, FALSE);
}

// ── Canvas WndProc ────────────────────────────────────────────────────────────
static void paintCanvas(HDC hdc, HWND hc) {
    RECT rc; GetClientRect(hc, &rc);
    HBRUSH bgBr = CreateSolidBrush(RGB(80, 82, 90));
    FillRect(hdc, &rc, bgBr); DeleteObject(bgBr);

    int pw = mm2px((float)g_doc.pageW), ph = mm2px((float)g_doc.pageH);
    int px0 = (rc.right - pw) / 2 - g_scrollX;

    for (int pi = 0; pi < (int)g_doc.pages.size(); pi++) {
        int py = canvasPageY(pi);
        // Shadow
        RECT sh = { px0 + 4, py + 4, px0 + pw + 4, py + ph + 4 };
        HBRUSH shBr = CreateSolidBrush(RGB(30, 30, 35)); FillRect(hdc, &sh, shBr); DeleteObject(shBr);
        // Page
        RECT pr = { px0, py, px0 + pw, py + ph };
        HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255)); FillRect(hdc, &pr, wb); DeleteObject(wb);
        HPEN border = CreatePen(PS_SOLID, 1, RGB(160, 162, 170));
        HPEN op = (HPEN)SelectObject(hdc, border);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH); HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
        Rectangle(hdc, px0, py, px0 + pw + 1, py + ph + 1);
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(border);
        // Page number
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(190, 192, 200));
        std::string lbl = "Page " + std::to_string(pi + 1);
        TextOutW(hdc, px0, py - 18, toW(lbl).c_str(), (int)lbl.size());

        // Text boxes
        for (int bi = 0; bi < (int)g_doc.pages[pi].boxes.size(); bi++) {
            const TextBox &tb = g_doc.pages[pi].boxes[bi];
            int bx = px0 + mm2px(tb.x), by2 = py + mm2px(tb.y);
            int bw = mm2px(tb.w), bh = mm2px(tb.h);
            RECT br = { bx, by2, bx + bw, by2 + bh };
            if (tb.hasBg) { HBRUSH bb = CreateSolidBrush(tb.bgColor); FillRect(hdc, &br, bb); DeleteObject(bb); }
            bool sel = (pi == g_curPage && bi == g_selBox);
            HPEN fp = CreatePen(sel ? PS_SOLID : PS_DASH, 1, sel ? RGB(0, 120, 215) : RGB(110, 140, 200));
            HPEN op2 = (HPEN)SelectObject(hdc, fp);
            HBRUSH ob2 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, bx, by2, bx + bw, by2 + bh);
            SelectObject(hdc, op2); SelectObject(hdc, ob2); DeleteObject(fp);
            if (!tb.text.empty()) {
                SetTextColor(hdc, tb.textColor); SetBkMode(hdc, TRANSPARENT);
                LOGFONTW lf = {}; lf.lfHeight = -(int)(tb.fontSize * g_zoom);
                lf.lfCharSet = DEFAULT_CHARSET; wcscpy(lf.lfFaceName, L"Segoe UI");
                HFONT tf = CreateFontIndirectW(&lf); HFONT of = (HFONT)SelectObject(hdc, tf);
                UINT fmt = DT_WORDBREAK | DT_NOPREFIX;
                if (tb.align == 1) fmt |= DT_CENTER;
                else if (tb.align == 2) fmt |= DT_RIGHT;
                RECT tr = br; InflateRect(&tr, -3, -3);
                DrawTextW(hdc, toW(tb.text).c_str(), -1, &tr, fmt);
                SelectObject(hdc, of); DeleteObject(tf);
            }
            if (sel) {
                HBRUSH hb = CreateSolidBrush(RGB(0, 120, 215));
                auto dot = [&](int x, int y) { RECT r = { x-4,y-4,x+4,y+4 }; FillRect(hdc, &r, hb); };
                dot(bx,by2); dot(bx+bw/2,by2); dot(bx+bw,by2);
                dot(bx,by2+bh/2); dot(bx+bw,by2+bh/2);
                dot(bx,by2+bh); dot(bx+bw/2,by2+bh); dot(bx+bw,by2+bh);
                DeleteObject(hb);
            }
        }
    }
    if (g_creating) {
        HPEN cp = CreatePen(PS_DOT, 1, RGB(0, 200, 80));
        HPEN ocp = (HPEN)SelectObject(hdc, cp);
        HBRUSH ob3 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, g_createRc.left, g_createRc.top, g_createRc.right, g_createRc.bottom);
        SelectObject(hdc, ocp); SelectObject(hdc, ob3); DeleteObject(cp);
    }
}

static LRESULT CALLBACK CanvasProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP mbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mdc, mbm);
        paintCanvas(mdc, hWnd);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old); DeleteObject(mbm); DeleteDC(mdc);
        EndPaint(hWnd, &ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        SetCapture(hWnd);
        int cx = LOWORD(lP), cy = HIWORD(lP);
        int pi; float pmx, pmy;
        if (hitPage(hWnd, cx, cy, pi, pmx, pmy)) {
            g_curPage = pi;
            SendMessageW(g_hPagesList, LB_SETCURSEL, pi, 0);
            if (g_tool == 1) {
                g_creating = true; g_dragX = cx; g_dragY = cy;
                g_createRc = { cx, cy, cx, cy };
            } else {
                int found = -1;
                int px0 = canvasPageX(hWnd), py0 = canvasPageY(pi);
                auto &boxes = g_doc.pages[pi].boxes;
                for (int bi = (int)boxes.size() - 1; bi >= 0; bi--) {
                    auto &tb = boxes[bi];
                    int bx = px0 + mm2px(tb.x), by2 = py0 + mm2px(tb.y);
                    if (cx >= bx && cx < bx + mm2px(tb.w) && cy >= by2 && cy < by2 + mm2px(tb.h)) {
                        found = bi; break;
                    }
                }
                if (found != g_selBox) { g_selBox = found; syncPropsFromSel(); }
                if (found >= 0) { g_dragging = true; g_dragX = cx; g_dragY = cy; }
            }
        } else { g_selBox = -1; syncPropsFromSel(); }
        InvalidateRect(hWnd, nullptr, FALSE); return 0;
    }
    case WM_MOUSEMOVE: {
        int cx = LOWORD(lP), cy = HIWORD(lP);
        if (g_creating) { g_createRc.right = cx; g_createRc.bottom = cy; InvalidateRect(hWnd, nullptr, FALSE); }
        else if (g_dragging && g_selBox >= 0 && g_curPage >= 0 && g_curPage < (int)g_doc.pages.size()) {
            int dx = cx - g_dragX, dy = cy - g_dragY; g_dragX = cx; g_dragY = cy;
            TextBox &tb = g_doc.pages[g_curPage].boxes[g_selBox];
            tb.x = std::max(0.f, tb.x + px2mm(dx));
            tb.y = std::max(0.f, tb.y + px2mm(dy));
            g_dirty = true; InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (g_creating) {
            g_creating = false;
            int x0 = std::min(g_createRc.left, g_createRc.right);
            int y0 = std::min(g_createRc.top,  g_createRc.bottom);
            int x1 = std::max(g_createRc.left, g_createRc.right);
            int y1 = std::max(g_createRc.top,  g_createRc.bottom);
            if (x1 - x0 > 8 && y1 - y0 > 8 && g_curPage >= 0 && g_curPage < (int)g_doc.pages.size()) {
                int px0 = canvasPageX(hWnd), py0 = canvasPageY(g_curPage);
                TextBox tb; tb.id = newId();
                tb.x = px2mm(x0 - px0); tb.y = px2mm(y0 - py0);
                tb.w = px2mm(x1 - x0);  tb.h = px2mm(y1 - y0);
                tb.textColor = g_curTextCol; tb.bgColor = g_curBgCol; tb.hasBg = g_curHasBg;
                g_doc.pages[g_curPage].boxes.push_back(tb);
                g_selBox = (int)g_doc.pages[g_curPage].boxes.size() - 1;
                g_dirty  = true; g_tool = 0;
                SendMessageW(g_hToolSel, BM_SETCHECK, BST_CHECKED,   0);
                SendMessageW(g_hToolTxt, BM_SETCHECK, BST_UNCHECKED, 0);
                syncPropsFromSel();
            }
        }
        g_dragging = false; InvalidateRect(hWnd, nullptr, FALSE); return 0;
    }
    case WM_RBUTTONDOWN: {
        if (g_selBox >= 0 && g_curPage >= 0 && g_curPage < (int)g_doc.pages.size()) {
            auto &bxs = g_doc.pages[g_curPage].boxes;
            if (g_selBox < (int)bxs.size()) {
                bxs.erase(bxs.begin() + g_selBox);
                g_selBox = -1; g_dirty = true;
                syncPropsFromSel(); InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wP);
        if (GetKeyState(VK_CONTROL) & 0x8000)
            g_zoom = std::max(0.2f, std::min(5.f, g_zoom + (delta > 0 ? 0.15f : -0.15f)));
        else
            g_scrollY = std::max(0, g_scrollY - delta / 3);
        InvalidateRect(hWnd, nullptr, FALSE); return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, g_tool == 1 ? IDC_CROSS : IDC_ARROW)); return TRUE;
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ── Props panel WndProc ───────────────────────────────────────────────────────
static LRESULT CALLBACK PropProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wP);
        if (id == IDC_APPLY)     { applyPropsToSel(); return 0; }
        if (id == IDC_BTN_COLOR) {
            if (showColorPicker(hWnd, g_curTextCol)) {
                if (g_brushText) { DeleteObject(g_brushText); g_brushText = nullptr; }
                InvalidateRect(g_hColorPrev, nullptr, TRUE);
                applyPropsToSel();
            }
            return 0;
        }
        if (id == IDC_BTN_BGCOLOR) {
            if (showColorPicker(hWnd, g_curBgCol)) {
                g_curHasBg = true; SendMessageW(g_hHasBg, BM_SETCHECK, BST_CHECKED, 0);
                if (g_brushBg) { DeleteObject(g_brushBg); g_brushBg = nullptr; }
                InvalidateRect(g_hBgPrev, nullptr, TRUE);
                applyPropsToSel();
            }
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HWND hCtl = (HWND)lP;
        if (hCtl == g_hColorPrev) {
            if (!g_brushText) g_brushText = CreateSolidBrush(g_curTextCol);
            SetBkColor((HDC)wP, g_curTextCol); return (LRESULT)g_brushText;
        }
        if (hCtl == g_hBgPrev) {
            if (!g_brushBg) g_brushBg = CreateSolidBrush(g_curBgCol);
            SetBkColor((HDC)wP, g_curBgCol); return (LRESULT)g_brushBg;
        }
        break;
    }
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ── Document I/O ──────────────────────────────────────────────────────────────
static std::string docToSyn() {
    std::ostringstream o;
    o << "@title " << g_doc.title << "\n";
    if (!g_doc.author.empty())      o << "@author " << g_doc.author << "\n";
    if (!g_doc.description.empty()) o << "@description " << g_doc.description << "\n";
    o << "@pagesize " << PAGE_SIZES[g_doc.sizeIdx].name << "\n\n";
    for (int pi = 0; pi < (int)g_doc.pages.size(); pi++) {
        o << "@page " << pi << "\n";
        for (auto &tb : g_doc.pages[pi].boxes) {
            o << "@textbox id=" << tb.id
              << " x=" << tb.x << " y=" << tb.y << " w=" << tb.w << " h=" << tb.h
              << " color=" << colToHex(tb.textColor)
              << " bgcolor=" << colToHex(tb.bgColor)
              << " hasbg=" << (tb.hasBg ? 1 : 0)
              << " size=" << tb.fontSize
              << " align=" << tb.align;
            if (!tb.varBinding.empty()) o << " bind=" << tb.varBinding;
            o << "\n" << tb.text << "\n@end\n";
        }
        o << "@endpage\n\n";
    }
    return o.str();
}

static void parseSynDoc(const std::string &src) {
    g_doc = Document(); g_doc.pages.clear(); g_tbSerial = 0;
    std::istringstream ss(src); std::string line;
    int curPage = -1; TextBox curBox; bool inBox = false; std::string boxText;
    while (std::getline(ss, line)) {
        std::string s = trim(line);
        if (s.empty()) continue;
        if (s.substr(0, 7) == "@title ")    { g_doc.title  = trim(s.substr(7)); continue; }
        if (s.substr(0, 8) == "@author ")   { g_doc.author = trim(s.substr(8)); continue; }
        if (s.substr(0, 10) == "@pagesize ") {
            std::string nm = trim(s.substr(10));
            for (int i = 0; i < NPAGE_SIZES; i++) if (nm == PAGE_SIZES[i].name) {
                g_doc.sizeIdx = i; g_doc.pageW = PAGE_SIZES[i].w; g_doc.pageH = PAGE_SIZES[i].h; break;
            }
            continue;
        }
        if (s.substr(0, 6) == "@page ") { g_doc.pages.push_back(Page()); curPage = (int)g_doc.pages.size()-1; continue; }
        if (s == "@endpage") { curPage = -1; continue; }
        if (s.substr(0, 9) == "@textbox ") {
            inBox = true; boxText = ""; curBox = {};
            std::istringstream kss(s.substr(9)); std::string kv;
            while (kss >> kv) {
                auto eq = kv.find('='); if (eq == std::string::npos) continue;
                std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                if (k == "id")     curBox.id = v;
                else if (k == "x") curBox.x  = std::stof(v);
                else if (k == "y") curBox.y  = std::stof(v);
                else if (k == "w") curBox.w  = std::stof(v);
                else if (k == "h") curBox.h  = std::stof(v);
                else if (k == "color")   curBox.textColor = hexToCol(v);
                else if (k == "bgcolor") curBox.bgColor   = hexToCol(v);
                else if (k == "hasbg")   curBox.hasBg     = (v == "1");
                else if (k == "size")    curBox.fontSize  = std::stoi(v);
                else if (k == "align")   curBox.align     = std::stoi(v);
                else if (k == "bind")    curBox.varBinding = v;
            }
            if (curBox.id.empty()) curBox.id = newId();
            continue;
        }
        if (s == "@end" && inBox) {
            curBox.text = trim(boxText);
            if (curPage >= 0 && curPage < (int)g_doc.pages.size())
                g_doc.pages[curPage].boxes.push_back(curBox);
            inBox = false; continue;
        }
        if (inBox) { if (!boxText.empty()) boxText += "\n"; boxText += line; }
    }
    if (g_doc.pages.empty()) g_doc.pages.push_back(Page());
}

// ── Dialogs ───────────────────────────────────────────────────────────────────
static void showCredits(HWND parent) {
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"#32770",
        L"Credits", WS_POPUP | WS_CAPTION | WS_SYSMENU, 280, 180, 360, 230,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    LOGFONTW lf = {}; lf.lfHeight = -16; lf.lfWeight = FW_BOLD; wcscpy(lf.lfFaceName, L"Segoe UI");
    HFONT hB = CreateFontIndirectW(&lf); lf.lfWeight = FW_NORMAL; lf.lfHeight = -14;
    HFONT hN = CreateFontIndirectW(&lf);
    auto mkS = [&](const char *t, int y, HFONT f) {
        HWND h = CreateWindowExW(0, L"STATIC", toW(t).c_str(), WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, y, 340, 20, hD, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    };
    mkS("Structura Architect",  12, hB);
    mkS("Cursed Entertainment", 38, hN);
    mkS("Designer:  Farica Kimora", 60, hN);
    mkS("\xa9 2026 Cursed Entertainment", 82, hN);
    mkS("All rights reserved.", 104, hN);
    HWND hOK = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        140, 152, 80, 26, hD, (HMENU)IDOK, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)hN, TRUE);
    ShowWindow(hD, SW_SHOW); EnableWindow(parent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
        if (!IsWindow(hD)) break;
        if ((msg.hwnd == hD || IsChild(hD, msg.hwnd)) && msg.message == WM_COMMAND
                && LOWORD(msg.wParam) == IDOK) { DestroyWindow(hD); break; }
    }
    EnableWindow(parent, TRUE);
    if (IsWindow(hD)) DestroyWindow(hD);
    DeleteObject(hB); DeleteObject(hN); SetForegroundWindow(parent);
}

static void showPageSetup(HWND parent) {
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"#32770",
        L"Page Setup", WS_POPUP | WS_CAPTION | WS_SYSMENU, 280, 180, 300, 170,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    LOGFONTW lf = {}; lf.lfHeight = -14; wcscpy(lf.lfFaceName, L"Segoe UI");
    HFONT hF = CreateFontIndirectW(&lf);
    HWND hL = CreateWindowExW(0, L"STATIC", L"Page Size:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 20, 80, 20, hD, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hL, WM_SETFONT, (WPARAM)hF, TRUE);
    HWND hC = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        96, 18, 170, 200, hD, (HMENU)1, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hC, WM_SETFONT, (WPARAM)hF, TRUE);
    for (int i = 0; i < NPAGE_SIZES; i++)
        SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)toW(PAGE_SIZES[i].name).c_str());
    SendMessageW(hC, CB_SETCURSEL, g_doc.sizeIdx, 0);
    auto mkBtn = [&](const wchar_t *t, int x, HMENU id) {
        HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, 90, 80, 26, hD, id, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)hF, TRUE);
    };
    mkBtn(L"OK", 60, (HMENU)IDOK); mkBtn(L"Cancel", 155, (HMENU)IDCANCEL);
    ShowWindow(hD, SW_SHOW); EnableWindow(parent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
        if (!IsWindow(hD)) break;
        if (msg.hwnd == hD || IsChild(hD, msg.hwnd)) {
            if (msg.message == WM_COMMAND) {
                if (LOWORD(msg.wParam) == IDOK) {
                    int idx = (int)SendMessageW(hC, CB_GETCURSEL, 0, 0);
                    if (idx >= 0 && idx < NPAGE_SIZES) {
                        g_doc.sizeIdx = idx; g_doc.pageW = PAGE_SIZES[idx].w; g_doc.pageH = PAGE_SIZES[idx].h;
                        g_dirty = true; InvalidateRect(g_hCanvas, nullptr, FALSE);
                    }
                    DestroyWindow(hD); break;
                }
                if (LOWORD(msg.wParam) == IDCANCEL) { DestroyWindow(hD); break; }
            }
        }
    }
    EnableWindow(parent, TRUE);
    if (IsWindow(hD)) DestroyWindow(hD);
    DeleteObject(hF); SetForegroundWindow(parent);
}

// ── Layout ────────────────────────────────────────────────────────────────────
static const int TB_H = 44, SB_H = 22, LP_W = 185, RP_W = 262;

static void doLayout(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom, cH = H - TB_H - SB_H;
    int cW = W - LP_W - RP_W;
    MoveWindow(g_hPagesList, 2, TB_H + 2, LP_W - 4, cH - 60, TRUE);
    MoveWindow(g_hBtnNew,    2, TB_H + cH - 54, (LP_W - 6) / 2, 24, TRUE);
    MoveWindow(g_hBtnDel,    LP_W / 2 + 1, TB_H + cH - 54, (LP_W - 6) / 2, 24, TRUE);
    MoveWindow(g_hCanvas,    LP_W, TB_H, cW > 0 ? cW : 0, cH, TRUE);
    MoveWindow(g_hPropsPanel,LP_W + (cW > 0 ? cW : 0), TB_H, RP_W, cH, TRUE);
}

static void buildPropsPanel(HWND hP) {
    RECT rc; GetClientRect(hP, &rc);
    int pw = rc.right, y = 6;
    LOGFONTW lf = {}; lf.lfHeight = -13; lf.lfWeight = FW_BOLD; wcscpy(lf.lfFaceName, L"Segoe UI");
    // Title
    HWND hTit = CreateWindowExW(0, L"STATIC", L"Properties", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, y, pw, 20, hP, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hTit, WM_SETFONT, (WPARAM)g_fontBold, TRUE); y += 26;

    auto lbl = [&](const char *t, int yy) {
        HWND h = CreateWindowExW(0, L"STATIC", toW(t).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
            6, yy + 3, 90, 18, hP, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    };
    auto edt = [&](int yy, int hh, HMENU id, bool multi = false) -> HWND {
        DWORD st = multi ? ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL;
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | st,
            98, yy, pw - 106, hh, hP, id, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); return h;
    };

    lbl("Text:", y);      g_hPropText  = edt(y, 72, (HMENU)IDC_PROP_TEXT, true); y += 80;
    lbl("Font Size:", y); g_hPropFsize = edt(y, 22, (HMENU)IDC_PROP_FSIZE);      y += 28;

    lbl("Alignment:", y);
    g_hPropAlign = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        98, y, pw - 106, 22 + 80, hP, (HMENU)IDC_PROP_ALIGN, GetModuleHandle(nullptr), nullptr);
    SendMessageW(g_hPropAlign, WM_SETFONT, (WPARAM)g_font, TRUE);
    const wchar_t* alignOpts[] = { L"Left", L"Center", L"Right" };
    for (int _ai = 0; _ai < 3; _ai++)
        SendMessageW(g_hPropAlign, CB_ADDSTRING, 0, (LPARAM)alignOpts[_ai]);
    SendMessageW(g_hPropAlign, CB_SETCURSEL, 0, 0); y += 30;

    lbl("Text Color:", y);
    g_hColorPrev = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 98, y, 28, 20, hP,
        (HMENU)IDC_COLORPREV, GetModuleHandle(nullptr), nullptr);
    HWND hPickT = CreateWindowExW(0, L"BUTTON", L"Pick", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, y, pw - 138, 20, hP, (HMENU)IDC_BTN_COLOR, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hPickT, WM_SETFONT, (WPARAM)g_font, TRUE); y += 28;

    lbl("BG Color:", y);
    g_hBgPrev = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 98, y, 28, 20, hP,
        (HMENU)IDC_BGCOLORPREV, GetModuleHandle(nullptr), nullptr);
    HWND hPickB = CreateWindowExW(0, L"BUTTON", L"Pick", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, y, pw - 138, 20, hP, (HMENU)IDC_BTN_BGCOLOR, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hPickB, WM_SETFONT, (WPARAM)g_font, TRUE); y += 28;

    g_hHasBg = CreateWindowExW(0, L"BUTTON", L"Fill Background",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 98, y, pw - 106, 20, hP,
        (HMENU)IDC_HASBG, GetModuleHandle(nullptr), nullptr);
    SendMessageW(g_hHasBg, WM_SETFONT, (WPARAM)g_font, TRUE); y += 28;

    lbl("Var Binding:", y); g_hPropBind = edt(y, 22, (HMENU)IDC_PROP_BINDING); y += 30;

    HWND hApply = CreateWindowExW(0, L"BUTTON", L"Apply Changes",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 6, y + 4, pw - 12, 28, hP,
        (HMENU)IDC_APPLY, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hApply, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
}

// ── Pages list ────────────────────────────────────────────────────────────────
static void refreshPagesList() {
    SendMessageW(g_hPagesList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)g_doc.pages.size(); i++) {
        std::string s = "  Page " + std::to_string(i + 1) + "  [" + std::to_string(g_doc.pages[i].boxes.size()) + " box]";
        SendMessageW(g_hPagesList, LB_ADDSTRING, 0, (LPARAM)toW(s).c_str());
    }
    if (g_curPage < (int)g_doc.pages.size())
        SendMessageW(g_hPagesList, LB_SETCURSEL, g_curPage, 0);
}

// ── File helpers ──────────────────────────────────────────────────────────────
static void updateTitle() {
    std::string t = "StructArch — " + (g_path.empty() ? "Untitled" : g_path) + (g_dirty ? " *" : "");
    SetWindowTextW(g_hwnd, toW(t).c_str());
}
static bool fileDlg(bool save, std::string &path) {
    wchar_t buf[MAX_PATH] = {}; if (!path.empty()) wcscpy(buf, toW(path).c_str());
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Structura Document (*.syn)\0*.syn\0All Files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    if (save) { ofn.Flags = OFN_OVERWRITEPROMPT; ofn.lpstrDefExt = L"syn"; }
    else      { ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST; }
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok) { path = fromW(buf); return true; } return false;
}
static void saveDoc(const std::string &path) {
    std::ofstream f(path, std::ios::binary);
    std::string s = docToSyn(); f.write(s.c_str(), (std::streamsize)s.size());
    g_dirty = false; updateTitle();
}
static void newDoc() {
    g_doc = Document(); g_doc.pages.push_back(Page());
    g_curPage = 0; g_selBox = -1; g_path = ""; g_dirty = false; g_tbSerial = 0;
    refreshPagesList(); syncPropsFromSel(); InvalidateRect(g_hCanvas, nullptr, FALSE); updateTitle();
}
static void openDoc(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { MessageBoxW(g_hwnd, L"Cannot open file.", L"Error", MB_OK | MB_ICONERROR); return; }
    std::string src((std::istreambuf_iterator<char>(f)), {});
    parseSynDoc(src); g_curPage = 0; g_selBox = -1; g_path = path; g_dirty = false;
    refreshPagesList(); syncPropsFromSel(); InvalidateRect(g_hCanvas, nullptr, FALSE); updateTitle();
}

// ── Main WndProc ──────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hWnd;
        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_fontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        // Menu
        HMENU hM = CreateMenu();
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING,    ID_FILE_NEW,    L"New\tCtrl+N");
            AppendMenuW(sub, MF_STRING,    ID_FILE_OPEN,   L"Open...\tCtrl+O");
            AppendMenuW(sub, MF_STRING,    ID_FILE_SAVE,   L"Save\tCtrl+S");
            AppendMenuW(sub, MF_STRING,    ID_FILE_SAVEAS, L"Save As...");
            AppendMenuW(sub, MF_SEPARATOR, 0,              nullptr);
            AppendMenuW(sub, MF_STRING,    ID_FILE_EXIT,   L"Exit");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"File");
        }
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING, ID_INSERT_TEXTBOX, L"Text Box\tT");
            AppendMenuW(sub, MF_STRING, ID_INSERT_PAGE,    L"New Page");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"Insert");
        }
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING, ID_VIEW_ZOOMIN,  L"Zoom In\t+");
            AppendMenuW(sub, MF_STRING, ID_VIEW_ZOOMOUT, L"Zoom Out\t-");
            AppendMenuW(sub, MF_STRING, ID_VIEW_FIT,     L"Fit Page\tF");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"View");
        }
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING, ID_TOOLS_PAGESETUP, L"Page Setup...");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"Tools");
        }
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING, ID_HELP_CREDITS, L"Credits");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"Help");
        }
        SetMenu(hWnd, hM);

        // Toolbar buttons
        int bx = 6;
        auto tb = [&](const char *t, HMENU id, int w = 65) {
            HWND h = CreateWindowExW(0, L"BUTTON", toW(t).c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, 9, w, 26, hWnd, id,
                GetModuleHandle(nullptr), nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); bx += w + 4;
        };
        tb("New",    (HMENU)ID_FILE_NEW,  52);
        tb("Open",   (HMENU)ID_FILE_OPEN, 52);
        tb("Save",   (HMENU)ID_FILE_SAVE, 52);
        bx += 8;
        g_hToolSel = CreateWindowExW(0, L"BUTTON", L"Select",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, bx, 11, 62, 22, hWnd,
            (HMENU)IDC_TOOL_SELECT, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hToolSel, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_hToolSel, BM_SETCHECK, BST_CHECKED, 0); bx += 67;
        g_hToolTxt = CreateWindowExW(0, L"BUTTON", L"Text Box",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, bx, 11, 72, 22, hWnd,
            (HMENU)IDC_TOOL_TEXT, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hToolTxt, WM_SETFONT, (WPARAM)g_font, TRUE); bx += 78;
        bx += 8;
        tb("Zoom +", (HMENU)ID_VIEW_ZOOMIN,  60);
        tb("Zoom -", (HMENU)ID_VIEW_ZOOMOUT, 60);
        tb("Fit",    (HMENU)ID_VIEW_FIT,     42);
        bx += 8;
        tb("Page Setup",  (HMENU)ID_TOOLS_PAGESETUP, 86);
        tb("Credits",     (HMENU)ID_HELP_CREDITS,     65);

        // Pages list
        g_hPagesList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
            0, 0, 0, 0, hWnd, (HMENU)IDC_PAGES_LIST, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hPagesList, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_hBtnNew = CreateWindowExW(0, L"BUTTON", L"+ Page", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 80, 24, hWnd, (HMENU)IDC_BTN_NEWPAGE, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hBtnNew, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_hBtnDel = CreateWindowExW(0, L"BUTTON", L"- Page", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 80, 24, hWnd, (HMENU)IDC_BTN_DELPAGE, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hBtnDel, WM_SETFONT, (WPARAM)g_font, TRUE);

        // Canvas
        WNDCLASSEXW wcc = {}; wcc.cbSize = sizeof(wcc); wcc.lpfnWndProc = CanvasProc;
        wcc.hInstance = GetModuleHandle(nullptr); wcc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcc.lpszClassName = L"StructCanvas"; RegisterClassExW(&wcc);
        g_hCanvas = CreateWindowExW(WS_EX_CLIENTEDGE, L"StructCanvas", nullptr,
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_CANVAS, GetModuleHandle(nullptr), nullptr);

        // Props panel
        WNDCLASSEXW wcp = {}; wcp.cbSize = sizeof(wcp); wcp.lpfnWndProc = PropProc;
        wcp.hInstance = GetModuleHandle(nullptr);
        wcp.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1); wcp.lpszClassName = L"PropsPanel";
        RegisterClassExW(&wcp);
        g_hPropsPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"PropsPanel", nullptr,
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        doLayout(hWnd);
        buildPropsPanel(g_hPropsPanel);
        refreshPagesList();
        syncPropsFromSel();
        updateTitle();
        break;
    }
    case WM_SIZE: doLayout(hWnd); break;
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wP == 'S') SendMessageW(hWnd, WM_COMMAND, ID_FILE_SAVE, 0);
            if (wP == 'O') SendMessageW(hWnd, WM_COMMAND, ID_FILE_OPEN, 0);
            if (wP == 'N') SendMessageW(hWnd, WM_COMMAND, ID_FILE_NEW, 0);
        }
        if (wP == VK_DELETE && g_selBox >= 0 && g_curPage >= 0 && g_curPage < (int)g_doc.pages.size()) {
            auto &bxs = g_doc.pages[g_curPage].boxes;
            if (g_selBox < (int)bxs.size()) {
                bxs.erase(bxs.begin() + g_selBox); g_selBox = -1; g_dirty = true;
                syncPropsFromSel(); InvalidateRect(g_hCanvas, nullptr, FALSE); updateTitle();
            }
        }
        break;
    case WM_COMMAND: {
        int id = LOWORD(wP), notif = HIWORD(wP);
        if (id == IDC_PAGES_LIST && notif == LBN_SELCHANGE) {
            int idx = (int)SendMessageW(g_hPagesList, LB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx < (int)g_doc.pages.size()) {
                g_curPage = idx; g_selBox = -1; syncPropsFromSel();
                InvalidateRect(g_hCanvas, nullptr, FALSE);
            }
            break;
        }
        if (id == IDC_BTN_NEWPAGE || id == ID_INSERT_PAGE) {
            int ins = (g_curPage >= 0) ? g_curPage + 1 : (int)g_doc.pages.size();
            g_doc.pages.insert(g_doc.pages.begin() + ins, Page());
            g_curPage = ins; g_selBox = -1; g_dirty = true;
            refreshPagesList(); syncPropsFromSel(); InvalidateRect(g_hCanvas, nullptr, FALSE); updateTitle(); break;
        }
        if (id == IDC_BTN_DELPAGE) {
            if (g_doc.pages.size() <= 1) { MessageBoxW(hWnd, L"Cannot delete the only page.", L"", MB_OK); break; }
            if (MessageBoxW(hWnd, L"Delete this page?", L"Delete Page", MB_YESNO) == IDNO) break;
            g_doc.pages.erase(g_doc.pages.begin() + g_curPage);
            g_curPage = std::min(g_curPage, (int)g_doc.pages.size() - 1);
            g_selBox = -1; g_dirty = true;
            refreshPagesList(); syncPropsFromSel(); InvalidateRect(g_hCanvas, nullptr, FALSE); updateTitle(); break;
        }
        if (id == IDC_TOOL_SELECT) { g_tool = 0; break; }
        if (id == IDC_TOOL_TEXT || id == ID_INSERT_TEXTBOX) {
            g_tool = 1; SendMessageW(g_hToolTxt, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(g_hToolSel, BM_SETCHECK, BST_UNCHECKED, 0); break;
        }
        if (id == ID_VIEW_ZOOMIN)  { g_zoom = std::min(5.f, g_zoom + 0.25f); InvalidateRect(g_hCanvas, nullptr, FALSE); break; }
        if (id == ID_VIEW_ZOOMOUT) { g_zoom = std::max(0.2f, g_zoom - 0.25f); InvalidateRect(g_hCanvas, nullptr, FALSE); break; }
        if (id == ID_VIEW_FIT) {
            RECT rc; GetClientRect(g_hCanvas, &rc);
            if (rc.right > 40 && rc.bottom > 40)
                g_zoom = std::min((rc.right - 40) / (g_doc.pageW * 3.f), (rc.bottom - 60) / (g_doc.pageH * 3.f));
            g_scrollX = 0; g_scrollY = 0; InvalidateRect(g_hCanvas, nullptr, FALSE); break;
        }
        if (id == ID_TOOLS_PAGESETUP) { showPageSetup(hWnd); break; }
        if (id == ID_HELP_CREDITS)    { showCredits(hWnd);   break; }
        if (id == ID_FILE_NEW) {
            if (g_dirty && MessageBoxW(hWnd, L"Discard unsaved changes?", L"New", MB_YESNO) == IDNO) break;
            newDoc(); break;
        }
        if (id == ID_FILE_OPEN) {
            if (g_dirty && MessageBoxW(hWnd, L"Discard unsaved changes?", L"Open", MB_YESNO) == IDNO) break;
            std::string p; if (fileDlg(false, p)) openDoc(p); break;
        }
        if (id == ID_FILE_SAVE) {
            if (g_path.empty()) { if (!fileDlg(true, g_path)) break; }
            saveDoc(g_path); break;
        }
        if (id == ID_FILE_SAVEAS) {
            std::string p; if (fileDlg(true, p)) { g_path = p; saveDoc(g_path); } break;
        }
        if (id == ID_FILE_EXIT) { SendMessageW(hWnd, WM_CLOSE, 0, 0); break; }
        break;
    }
    case WM_CLOSE:
        if (g_dirty && MessageBoxW(hWnd, L"Unsaved changes. Quit?", L"Quit", MB_YESNO) == IDNO) break;
        DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_font)      DeleteObject(g_font);
        if (g_fontBold)  DeleteObject(g_fontBold);
        if (g_brushText) DeleteObject(g_brushText);
        if (g_brushBg)   DeleteObject(g_brushBg);
        PostQuitMessage(0); break;
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int nShow) {
    InitCommonControls();
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1); wc.lpszClassName = L"StructArch";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION); RegisterClassExW(&wc);

    g_doc.pages.push_back(Page());
    HWND hw = CreateWindowExW(0, L"StructArch", L"StructArch — Untitled",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900,
        nullptr, nullptr, hInst, nullptr);

    if (lpCmd && lpCmd[0]) {
        std::string arg(lpCmd);
        if (!arg.empty() && arg.front() == '"') { arg = arg.substr(1); if (arg.back() == '"') arg.pop_back(); }
        if (!arg.empty()) openDoc(arg);
    }

    ShowWindow(hw, nShow); UpdateWindow(hw);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
