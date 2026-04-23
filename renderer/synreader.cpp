/*
 * SynReader.exe — Syntaxia Visual Reader
 * Renders .syn documents as physical pages (Acrobat-style print view).
 * Compile:
 *   g++ -std=c++14 -mwindows -O2 -static-libgcc -static-libstdc++ \
 *       -o SynReader.exe synreader.cpp \
 *       -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cmath>

// ── IDs ───────────────────────────────────────────────────────────────────────
#define ID_FILE_OPEN    101
#define ID_FILE_EXIT    102
#define ID_VIEW_ZOOMIN  201
#define ID_VIEW_ZOOMOUT 202
#define ID_VIEW_FIT     203
#define ID_HELP_CREDITS 301
#define IDC_CANVAS      1001
#define IDC_PREV_PAGE   1002
#define IDC_NEXT_PAGE   1003
#define IDC_PAGE_LABEL  1004

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
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static COLORREF hexToCol(const std::string &s) {
    std::string h = s; if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() < 6) return 0;
    unsigned long v = strtoul(h.c_str(), nullptr, 16);
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ── Data model ────────────────────────────────────────────────────────────────
struct Box {
    float x, y, w, h;          // mm on page
    std::string text;
    COLORREF fg  = RGB(0,0,0);
    COLORREF bg  = RGB(255,255,255);
    bool hasBg   = false;
    int  size    = 12;          // pt
    int  align   = 0;           // 0=left 1=center 2=right
    bool isHead  = false;
    int  headLvl = 1;
};
struct RPage { std::vector<Box> boxes; };
struct Doc {
    std::string title, author;
    int pw = 210, ph = 297;     // mm
    std::vector<RPage> pages;
};

static const struct { const char *name; int w, h; } PSIZES[] = {
    {"A4",210,297},{"A3",297,420},{"A5",148,210},
    {"Letter",216,279},{"Legal",216,356},{"Tabloid",279,432},
};

// ── Parser: StructArch layout format ─────────────────────────────────────────
static Doc parseStructArch(const std::string &src) {
    Doc doc;
    std::istringstream ss(src); std::string line;
    int curPage = -1; Box curBox; bool inBox = false; std::string boxTxt;

    while (std::getline(ss, line)) {
        std::string s = trim(line);
        if (s.empty()) continue;
        if (s.substr(0,7)  == "@title ")    { doc.title  = trim(s.substr(7));  continue; }
        if (s.substr(0,8)  == "@author ")   { doc.author = trim(s.substr(8));  continue; }
        if (s.substr(0,10) == "@pagesize ") {
            std::string nm = trim(s.substr(10));
            for (auto &ps : PSIZES) if (nm == ps.name) { doc.pw=ps.w; doc.ph=ps.h; break; }
            continue;
        }
        if (s.substr(0,6) == "@page ") {
            doc.pages.push_back(RPage()); curPage = (int)doc.pages.size()-1; continue;
        }
        if (s == "@endpage") { curPage = -1; continue; }
        if (s.substr(0,9) == "@textbox ") {
            inBox = true; boxTxt = ""; curBox = {};
            std::istringstream kss(s.substr(9)); std::string kv;
            while (kss >> kv) {
                auto eq = kv.find('='); if (eq == std::string::npos) continue;
                std::string k = kv.substr(0,eq), v = kv.substr(eq+1);
                if (k=="x")      curBox.x    = std::stof(v);
                else if (k=="y") curBox.y    = std::stof(v);
                else if (k=="w") curBox.w    = std::stof(v);
                else if (k=="h") curBox.h    = std::stof(v);
                else if (k=="color")   curBox.fg    = hexToCol(v);
                else if (k=="bgcolor") curBox.bg    = hexToCol(v);
                else if (k=="hasbg")   curBox.hasBg = (v=="1");
                else if (k=="size")    curBox.size  = std::stoi(v);
                else if (k=="align")   curBox.align = std::stoi(v);
            }
            continue;
        }
        if (s == "@end" && inBox) {
            curBox.text = trim(boxTxt);
            if (curPage >= 0 && curPage < (int)doc.pages.size())
                doc.pages[curPage].boxes.push_back(curBox);
            inBox = false; continue;
        }
        if (inBox) { if (!boxTxt.empty()) boxTxt += "\n"; boxTxt += line; }
    }
    if (doc.pages.empty()) doc.pages.push_back(RPage());
    return doc;
}

// ── Parser: modern Structura format (reflow onto pages) ──────────────────────
static Doc parseModern(const std::string &src) {
    Doc doc; doc.pages.push_back(RPage());

    const float MX = 20.f, MY = 20.f;   // margins mm
    const float TW = doc.pw - MX*2;     // text width
    const float TH = doc.ph - MY*2;     // text height
    float curY = MY;
    int   curPg = 0;

    auto newPage = [&]() {
        doc.pages.push_back(RPage()); curPg++; curY = MY;
    };
    auto addBox = [&](const std::string &text, float h, int fsz, int align, bool bold,
                       COLORREF fg = RGB(0,0,0)) {
        if (curY + h > MY + TH) newPage();
        Box b; b.x=MX; b.y=curY; b.w=TW; b.h=h;
        b.text=text; b.fg=fg; b.size=fsz; b.align=align;
        b.isHead=bold; doc.pages[curPg].boxes.push_back(b);
        curY += h + 2.f;
    };

    std::istringstream ss(src); std::string line;
    bool inMath = false; std::string mathAcc;
    std::vector<std::string> tableHdrs; std::vector<std::string> tableRows;
    bool inTable = false;

    auto flushTable = [&]() {
        if (!inTable) return;
        std::string txt; bool first = true;
        for (auto &h : tableHdrs) { if (!first) txt+=" | "; txt+=h; first=false; }
        txt += "\n";
        for (auto &r : tableRows) txt += r + "\n";
        float h = 5.f + tableRows.size() * 5.f;
        addBox(txt, h < 20.f ? 20.f : h, 10, 0, false);
        inTable = false; tableHdrs.clear(); tableRows.clear();
    };

    while (std::getline(ss, line)) {
        std::string s = trim(line);

        if (inMath) {
            if (s == "$$") {
                addBox(mathAcc, 4.f + std::count(mathAcc.begin(),mathAcc.end(),'\n')*5.f, 11, 1, false, RGB(60,60,120));
                inMath = false; mathAcc = "";
            } else { if (!mathAcc.empty()) mathAcc+="\n"; mathAcc+=s; }
            continue;
        }

        if (inTable && (s.empty() || s[0]!='|')) flushTable();

        if (s.empty()) { curY += 3.f; continue; }

        if (s.substr(0,7)  == "@title ")  { doc.title  = trim(s.substr(7));  addBox(doc.title, 12.f, 22, 1, true); continue; }
        if (s.substr(0,8)  == "@author ") { doc.author = trim(s.substr(8));  addBox(doc.author, 7.f, 11, 1, false, RGB(80,80,80)); continue; }
        if (s.substr(0,13) == "@description ") { addBox(trim(s.substr(13)), 7.f, 10, 1, false, RGB(100,100,100)); continue; }

        if (!s.empty() && s[0]=='#') {
            size_t n=0; while(n<s.size()&&s[n]=='#') n++;
            std::string txt = trim(s.substr(n));
            float sizes[] = {16.f, 13.f, 11.f};
            float heights[] = {12.f, 10.f, 8.f};
            int lv = (int)std::min(n,(size_t)3)-1;
            curY += 4.f;
            addBox(txt, heights[lv], (int)sizes[lv], 0, true);
            continue;
        }
        if (s == "---") { curY += 2.f; addBox("", 1.f, 10, 0, false, RGB(180,180,180)); curY += 2.f; continue; }
        if (s == "$$") { inMath = true; continue; }

        if (!s.empty() && s[0]=='|') {
            // Table row
            auto splitPipe = [](const std::string &row) {
                std::vector<std::string> cells;
                std::istringstream rs(row.substr(1, row.size()-2));
                std::string cell;
                while (std::getline(rs, cell, '|')) {
                    cells.push_back(trim(cell));
                }
                return cells;
            };
            bool isSep = true; for (char c : s) if (c!='|'&&c!='-'&&c!=' '&&c!=':') { isSep=false; break; }
            if (isSep) { inTable = true; continue; }
            auto cells = splitPipe(s);
            std::string row;
            for (size_t i=0;i<cells.size();i++) { if(i) row+=" | "; row+=cells[i]; }
            if (!inTable) { tableHdrs=cells; inTable=true; }
            else tableRows.push_back(row);
            continue;
        }

        // Skip pure directive lines (@var, @const, @derive, [widget:...])
        if (!s.empty() && s[0]=='@') continue;
        if (!s.empty() && s[0]=='[') continue;

        // Regular paragraph text
        float h = 6.f; // single line
        addBox(s, h, 11, 0, false);
    }
    flushTable();
    if (doc.pages.size() > 1 && doc.pages.back().boxes.empty()) doc.pages.pop_back();
    return doc;
}

static Doc parseSyn(const std::string &src) {
    // Detect format: StructArch has @page blocks
    if (src.find("@page ") != std::string::npos || src.find("@textbox ") != std::string::npos)
        return parseStructArch(src);
    return parseModern(src);
}

// ── Globals ────────────────────────────────────────────────────────────────────
static Doc    g_doc;
static float  g_zoom    = 1.0f;
static int    g_scrollX = 0, g_scrollY = 0;
static HWND   g_hwnd, g_hCanvas;
static HWND   g_hPrev, g_hNext, g_hPageLbl;
static HFONT  g_font, g_fontBold;
static int    g_focusPage = 0;   // first fully-visible page
static std::string g_filePath;

static float ppm()        { return 3.0f * g_zoom; }
static int   mm2px(float m){ return (int)(m * ppm()); }

static int canvasPageX(HWND hc) {
    RECT rc; GetClientRect(hc, &rc);
    return (rc.right - mm2px((float)g_doc.pw)) / 2 - g_scrollX;
}
static int canvasPageY(int idx) {
    int y = 24 - g_scrollY;
    for (int i=0; i<idx; i++) y += mm2px((float)g_doc.ph) + 24;
    return y;
}

// ── Canvas rendering ──────────────────────────────────────────────────────────
static void setWndText(HWND h, const std::string &s) { SetWindowTextW(h, toW(s).c_str()); }

static void updatePageLabel() {
    if (!g_hPageLbl) return;
    std::string s = "Page " + std::to_string(g_focusPage+1) + " / " + std::to_string((int)g_doc.pages.size());
    setWndText(g_hPageLbl, s);
}

static void paintCanvas(HDC hdc, HWND hc) {
    RECT rc; GetClientRect(hc, &rc);
    // Background — dark slate, like Acrobat
    HBRUSH bgBr = CreateSolidBrush(RGB(72, 72, 80));
    FillRect(hdc, &rc, bgBr); DeleteObject(bgBr);

    int pw = mm2px((float)g_doc.pw), ph = mm2px((float)g_doc.ph);
    int px0 = (rc.right - pw) / 2 - g_scrollX;

    // Track which page is first visible (for page number label)
    int firstVis = 0;
    for (int pi = 0; pi < (int)g_doc.pages.size(); pi++) {
        int py = canvasPageY(pi);
        if (py + ph > 0) { firstVis = pi; break; }
    }
    if (firstVis != g_focusPage) { g_focusPage = firstVis; updatePageLabel(); }

    for (int pi = 0; pi < (int)g_doc.pages.size(); pi++) {
        int py = canvasPageY(pi);
        // Cull pages not in viewport
        if (py + ph < -24 || py > rc.bottom + 24) continue;

        // Drop shadow
        RECT sh = { px0+5, py+5, px0+pw+5, py+ph+5 };
        HBRUSH shBr = CreateSolidBrush(RGB(20,20,25)); FillRect(hdc, &sh, shBr); DeleteObject(shBr);

        // Page white
        RECT pr = { px0, py, px0+pw, py+ph };
        HBRUSH wb = CreateSolidBrush(RGB(255,255,255)); FillRect(hdc, &pr, wb); DeleteObject(wb);

        // Subtle border
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(140,140,148));
        HPEN op = (HPEN)SelectObject(hdc, pen);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH); HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
        Rectangle(hdc, px0, py, px0+pw+1, py+ph+1);
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pen);

        // Page number tag (bottom-center of page)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(160,162,170));
            std::string lbl = std::to_string(pi+1);
            HFONT of = (HFONT)SelectObject(hdc, g_font);
            SIZE sz; GetTextExtentPoint32W(hdc, toW(lbl).c_str(), (int)lbl.size(), &sz);
            TextOutW(hdc, px0 + pw/2 - sz.cx/2, py + ph + 6, toW(lbl).c_str(), (int)lbl.size());
            SelectObject(hdc, of);
        }

        // Render boxes
        const RPage &page = g_doc.pages[pi];
        for (auto &box : page.boxes) {
            int bx = px0 + mm2px(box.x);
            int by = py  + mm2px(box.y);
            int bw = mm2px(box.w);
            int bh = mm2px(box.h);
            RECT br = { bx, by, bx+bw, by+bh };

            // Background fill
            if (box.hasBg) {
                HBRUSH bb = CreateSolidBrush(box.bg); FillRect(hdc, &br, bb); DeleteObject(bb);
            }

            // Separator line (empty box used as HR)
            if (box.text.empty() && !box.hasBg && box.h <= 1.5f) {
                HPEN hp = CreatePen(PS_SOLID, 1, RGB(180,180,190));
                HPEN ohp = (HPEN)SelectObject(hdc, hp);
                MoveToEx(hdc, bx, by, nullptr); LineTo(hdc, bx+bw, by);
                SelectObject(hdc, ohp); DeleteObject(hp);
                continue;
            }

            // Text
            if (!box.text.empty()) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, box.fg);
                LOGFONTW lf = {};
                lf.lfHeight   = -(int)(box.size * g_zoom);
                lf.lfWeight   = box.isHead ? FW_BOLD : FW_NORMAL;
                lf.lfCharSet  = DEFAULT_CHARSET;
                wcscpy(lf.lfFaceName, L"Segoe UI");
                HFONT tf = CreateFontIndirectW(&lf);
                HFONT of = (HFONT)SelectObject(hdc, tf);
                UINT fmt = DT_WORDBREAK | DT_NOPREFIX;
                if (box.align == 1) fmt |= DT_CENTER;
                else if (box.align == 2) fmt |= DT_RIGHT;
                RECT tr = br; InflateRect(&tr, -2, -2);
                DrawTextW(hdc, toW(box.text).c_str(), -1, &tr, fmt);
                SelectObject(hdc, of); DeleteObject(tf);
            }
        }
    }
}

// ── Canvas WndProc ────────────────────────────────────────────────────────────
static LRESULT CALLBACK CanvasProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP bm  = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mdc, bm);
        paintCanvas(mdc, hWnd);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old); DeleteObject(bm); DeleteDC(mdc);
        EndPaint(hWnd, &ps); return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wP);
        if (GetKeyState(VK_CONTROL) & 0x8000)
            g_zoom = std::max(0.2f, std::min(5.f, g_zoom + (delta>0 ? 0.15f : -0.15f)));
        else
            g_scrollY = std::max(0, g_scrollY - delta / 3);
        updatePageLabel();
        InvalidateRect(hWnd, nullptr, FALSE); return 0;
    }
    case WM_KEYDOWN:
        if (wP == VK_DOWN || wP == VK_NEXT)  { g_scrollY += mm2px((float)g_doc.ph) + 24; InvalidateRect(hWnd,nullptr,FALSE); }
        if (wP == VK_UP   || wP == VK_PRIOR) { g_scrollY = std::max(0, g_scrollY - mm2px((float)g_doc.ph) - 24); InvalidateRect(hWnd,nullptr,FALSE); }
        if (wP == VK_HOME) { g_scrollY = 0; InvalidateRect(hWnd,nullptr,FALSE); }
        if (wP == VK_END)  {
            g_scrollY = (int)g_doc.pages.size() * (mm2px((float)g_doc.ph)+24);
            InvalidateRect(hWnd,nullptr,FALSE);
        }
        return 0;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ── Layout ────────────────────────────────────────────────────────────────────
static const int TB_H = 42, SB_H = 22;

static void doLayout(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    MoveWindow(g_hCanvas, 0, TB_H, W, H - TB_H - SB_H, TRUE);
}

// ── Credits ───────────────────────────────────────────────────────────────────
static void showCredits(HWND parent) {
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"#32770",
        L"Credits", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        280, 180, 360, 230, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    LOGFONTW lf = {}; lf.lfHeight=-16; lf.lfWeight=FW_BOLD; wcscpy(lf.lfFaceName, L"Segoe UI");
    HFONT hB = CreateFontIndirectW(&lf); lf.lfWeight=FW_NORMAL; lf.lfHeight=-14;
    HFONT hN = CreateFontIndirectW(&lf);
    auto mk = [&](const char *t, int y, HFONT f) {
        HWND h = CreateWindowExW(0, L"STATIC", toW(t).c_str(),
            WS_CHILD|WS_VISIBLE|SS_CENTER, 10, y, 340, 20,
            hD, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    };
    mk("Syntaxia Reader",          12, hB);
    mk("Cursed Entertainment",     38, hN);
    mk("Designer:  Farica Kimora", 60, hN);
    mk("\xa9 2026 Cursed Entertainment", 82, hN);
    HWND hOK = CreateWindowExW(0, L"BUTTON", L"Close",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 140, 144, 80, 26,
        hD, (HMENU)IDOK, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)hN, TRUE);
    ShowWindow(hD, SW_SHOW); EnableWindow(parent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
        if (!IsWindow(hD)) break;
        if ((msg.hwnd==hD||IsChild(hD,msg.hwnd)) && msg.message==WM_COMMAND && LOWORD(msg.wParam)==IDOK)
            { DestroyWindow(hD); break; }
    }
    EnableWindow(parent, TRUE);
    if (IsWindow(hD)) DestroyWindow(hD);
    DeleteObject(hB); DeleteObject(hN); SetForegroundWindow(parent);
}

// ── File open ─────────────────────────────────────────────────────────────────
static bool pickFile(std::string &path) {
    wchar_t buf[MAX_PATH] = {};
    if (!path.empty()) wcscpy(buf, toW(path).c_str());
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Syntaxia Document (*.syn)\0*.syn\0All Files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = fromW(buf); return true;
}

static void loadFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        MessageBoxW(g_hwnd,
            (std::wstring(L"Cannot open:\n") + toW(path)).c_str(),
            L"SynReader", MB_OK | MB_ICONERROR);
        return;
    }
    std::string src((std::istreambuf_iterator<char>(f)), {});
    g_doc = parseSyn(src);
    g_filePath = path;
    g_scrollX = 0; g_scrollY = 0; g_focusPage = 0;
    std::string title = "Syntaxia Reader";
    if (!g_doc.title.empty()) title += " — " + g_doc.title;
    else {
        // use filename
        auto slash = path.find_last_of("/\\");
        title += " — " + (slash==std::string::npos ? path : path.substr(slash+1));
    }
    SetWindowTextW(g_hwnd, toW(title).c_str());
    updatePageLabel();
    InvalidateRect(g_hCanvas, nullptr, FALSE);
}

// ── Status bar ────────────────────────────────────────────────────────────────
static HWND g_hStatus;
static void createStatusBar(HWND parent) {
    g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent, nullptr, GetModuleHandle(nullptr), nullptr);
}
static void updateStatus() {
    if (!g_hStatus) return;
    std::string s = g_doc.title.empty() ? "No document" : g_doc.title;
    if (!g_doc.author.empty()) s += "  |  " + g_doc.author;
    s += "  |  " + std::to_string((int)g_doc.pages.size()) + " page(s)";
    s += "  |  Zoom: " + std::to_string((int)(g_zoom*100)) + "%";
    SetWindowTextW(g_hStatus, toW(s).c_str());
}

// ── Main WndProc ──────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hWnd;
        g_font = CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        g_fontBold = CreateFontW(-13,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

        // Menu
        HMENU hM = CreateMenu();
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING, ID_FILE_OPEN, L"Open...\tCtrl+O");
            AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(sub, MF_STRING, ID_FILE_EXIT, L"Exit");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"File");
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
            AppendMenuW(sub, MF_STRING, ID_HELP_CREDITS, L"Credits");
            AppendMenuW(hM, MF_POPUP, (UINT_PTR)sub, L"Help");
        }
        SetMenu(hWnd, hM);

        // Toolbar
        int bx = 6;
        auto btn = [&](const char *t, HMENU id, int w) {
            HWND h = CreateWindowExW(0, L"BUTTON", toW(t).c_str(),
                WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, bx, 9, w, 26, hWnd, id,
                GetModuleHandle(nullptr), nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); bx += w + 4;
        };
        btn("Open",    (HMENU)ID_FILE_OPEN,    60);
        bx += 8;
        btn("Zoom +",  (HMENU)ID_VIEW_ZOOMIN,  60);
        btn("Zoom -",  (HMENU)ID_VIEW_ZOOMOUT, 60);
        btn("Fit",     (HMENU)ID_VIEW_FIT,     44);
        bx += 8;
        // Page nav
        g_hPrev = CreateWindowExW(0, L"BUTTON", L"◀",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, bx, 9, 28, 26, hWnd,
            (HMENU)IDC_PREV_PAGE, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hPrev, WM_SETFONT, (WPARAM)g_font, TRUE); bx += 32;
        g_hPageLbl = CreateWindowExW(0, L"STATIC", L"Page 1 / 1",
            WS_CHILD|WS_VISIBLE|SS_CENTER|SS_CENTERIMAGE,
            bx, 9, 110, 26, hWnd, (HMENU)IDC_PAGE_LABEL, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hPageLbl, WM_SETFONT, (WPARAM)g_font, TRUE); bx += 114;
        g_hNext = CreateWindowExW(0, L"BUTTON", L"▶",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, bx, 9, 28, 26, hWnd,
            (HMENU)IDC_NEXT_PAGE, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hNext, WM_SETFONT, (WPARAM)g_font, TRUE);

        // Canvas
        WNDCLASSEXW wcc = {}; wcc.cbSize=sizeof(wcc); wcc.lpfnWndProc=CanvasProc;
        wcc.hInstance=GetModuleHandle(nullptr); wcc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wcc.lpszClassName=L"SynCanvas"; RegisterClassExW(&wcc);
        g_hCanvas = CreateWindowExW(0, L"SynCanvas", nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)IDC_CANVAS, GetModuleHandle(nullptr), nullptr);

        createStatusBar(hWnd);
        doLayout(hWnd);
        updatePageLabel();
        updateStatus();
        break;
    }
    case WM_SIZE:
        doLayout(hWnd);
        SendMessageW(g_hStatus, WM_SIZE, 0, 0);
        InvalidateRect(g_hCanvas, nullptr, FALSE);
        break;
    case WM_KEYDOWN:
        SendMessageW(g_hCanvas, WM_KEYDOWN, wP, lP);
        break;
    case WM_COMMAND: {
        int id = LOWORD(wP);
        if (id == ID_FILE_OPEN) {
            std::string p = g_filePath;
            if (pickFile(p)) loadFile(p);
            updateStatus();
            break;
        }
        if (id == IDC_PREV_PAGE) {
            g_scrollY = std::max(0, g_scrollY - mm2px((float)g_doc.ph) - 24);
            InvalidateRect(g_hCanvas, nullptr, FALSE); break;
        }
        if (id == IDC_NEXT_PAGE) {
            g_scrollY += mm2px((float)g_doc.ph) + 24;
            InvalidateRect(g_hCanvas, nullptr, FALSE); break;
        }
        if (id == ID_VIEW_ZOOMIN) {
            g_zoom = std::min(5.f, g_zoom + 0.25f);
            updateStatus(); InvalidateRect(g_hCanvas, nullptr, FALSE); break;
        }
        if (id == ID_VIEW_ZOOMOUT) {
            g_zoom = std::max(0.2f, g_zoom - 0.25f);
            updateStatus(); InvalidateRect(g_hCanvas, nullptr, FALSE); break;
        }
        if (id == ID_VIEW_FIT) {
            RECT rc; GetClientRect(g_hCanvas, &rc);
            if (rc.right > 40 && rc.bottom > 40)
                g_zoom = std::min((rc.right - 40) / (g_doc.pw * 3.f),
                                  (rc.bottom - 80) / (g_doc.ph * 3.f));
            g_scrollX = 0; g_scrollY = 0;
            updateStatus(); InvalidateRect(g_hCanvas, nullptr, FALSE); break;
        }
        if (id == ID_HELP_CREDITS) { showCredits(hWnd); break; }
        if (id == ID_FILE_EXIT)    { SendMessageW(hWnd, WM_CLOSE, 0, 0); break; }
        break;
    }
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wP;
        wchar_t buf[MAX_PATH] = {};
        if (DragQueryFileW(hDrop, 0, buf, MAX_PATH)) {
            std::string p = fromW(buf);
            loadFile(p); updateStatus();
        }
        DragFinish(hDrop); break;
    }
    case WM_CLOSE:   DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_font)     DeleteObject(g_font);
        if (g_fontBold) DeleteObject(g_fontBold);
        PostQuitMessage(0); break;
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int nShow) {
    InitCommonControls();

    WNDCLASSEXW wc = {}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_3DFACE+1); wc.lpszClassName=L"SynReader";
    wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hw = CreateWindowExW(WS_EX_ACCEPTFILES, L"SynReader",
        L"Syntaxia Reader", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 820,
        nullptr, nullptr, hInst, nullptr);

    // Open file from command-line arg
    if (lpCmd && lpCmd[0]) {
        std::string arg(lpCmd);
        if (!arg.empty() && arg.front()=='"') { arg=arg.substr(1); if(arg.back()=='"') arg.pop_back(); }
        if (!arg.empty()) loadFile(arg);
    } else {
        // No arg — show picker immediately
        std::string p;
        if (pickFile(p)) loadFile(p);
    }

    ShowWindow(hw, nShow); UpdateWindow(hw);
    SetFocus(g_hCanvas);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
