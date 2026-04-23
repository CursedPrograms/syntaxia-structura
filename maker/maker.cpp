/*
 * Structura Maker  —  C++ Win32 visual .syn document builder
 * Compile:
 *   g++ -std=c++14 -mwindows -O2 -o StructuraMaker.exe maker.cpp
 *       -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32
 */

// UNICODE not defined — WinMain uses LPSTR. W-suffix APIs called explicitly.
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
#include <functional>

// ── IDs ───────────────────────────────────────────────────────────────────────
#define ID_NEW       101
#define ID_OPEN      102
#define ID_SAVE      103
#define ID_EXPORT    104
#define ID_META      105
#define ID_MOVE_UP   111
#define ID_MOVE_DOWN 112
#define ID_DELETE    113
#define ID_ADD_BASE  200  // +0..+9 for each element type
#define IDC_LIST     300
#define IDC_PREVIEW  301
#define IDC_APPLY    302
// Property field slots: label = 400+i*2, control = 401+i*2  (10 slots)
#define IDC_PROP_LBL(i) (400 + (i)*2)
#define IDC_PROP_CTL(i) (401 + (i)*2)
#define MAX_PROP_SLOTS 10

// ── Data model ────────────────────────────────────────────────────────────────
struct Element {
    std::string type;
    std::map<std::string,std::string> props;
};

static std::vector<Element> g_els;
static int                  g_sel = -1;
static std::map<std::string,std::string> g_meta;
static std::string          g_path;
static bool                 g_dirty = false;

// ── Win32 handles ─────────────────────────────────────────────────────────────
static HWND g_hwnd;
static HWND g_hList;
static HWND g_hPreview;
static HWND g_hPropsPanel;
static HFONT g_hFont;
static HWND g_propLbls[MAX_PROP_SLOTS];
static HWND g_propCtls[MAX_PROP_SLOTS];
static std::string g_propKeys[MAX_PROP_SLOTS];  // which element.props key each slot maps to
static int  g_propCount = 0;

// ── String helpers ────────────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string stripComment(const std::string& s) {
    auto p = s.find("--");
    return (p == std::string::npos) ? s : trim(s.substr(0, p));
}
// Wide ↔ UTF-8
static std::wstring toW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back()==0) w.pop_back();
    return w;
}
static std::string fromW(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back()==0) s.pop_back();
    return s;
}
static std::string getWndText(HWND h) {
    int n = GetWindowTextLengthW(h) + 1;
    std::wstring w(n, 0); GetWindowTextW(h, &w[0], n);
    if (!w.empty() && w.back()==0) w.pop_back();
    return fromW(w);
}
static void setWndText(HWND h, const std::string& s) {
    SetWindowTextW(h, toW(s).c_str());
}
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) out.push_back(tok);
    return out;
}
static std::string join(const std::vector<std::string>& v, const std::string& d) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) { if (i) out += d; out += v[i]; }
    return out;
}

// ── Element defaults & labels ─────────────────────────────────────────────────
static const char* PALETTE_TYPES[] = {
    "heading","text","hr","var","toggle","const","derive","math","display","table"
};
static const wchar_t* PALETTE_LABELS[] = {
    L"# Heading", L"¶ Paragraph", L"— Divider",
    L"◈ Variable", L"☑ Toggle", L"■ Constant",
    L"⇒ Derive", L"∑ Math Block", L"▶ Display", L"▦ Table"
};
static const int PALETTE_COUNT = 10;

static Element defaultElement(const std::string& t) {
    Element e; e.type = t;
    if (t=="heading") { e.props["level"]="1"; e.props["text"]="New Section"; }
    else if (t=="text")    { e.props["content"]="Enter text here."; }
    else if (t=="var")     { e.props["name"]="my_var"; e.props["value"]="0";
                             e.props["widget"]="slider"; e.props["label"]="My Variable";
                             e.props["min"]="0"; e.props["max"]="100"; e.props["step"]="1"; }
    else if (t=="toggle")  { e.props["name"]="include_x"; e.props["value"]="1"; e.props["label"]="Include X"; }
    else if (t=="const")   { e.props["name"]="MY_K"; e.props["value"]="1.0"; }
    else if (t=="derive")  { e.props["name"]="result"; e.props["expr"]="a + b"; }
    else if (t=="math")    { e.props["content"]="y = f(x)"; }
    else if (t=="display") { e.props["expr"]="result"; e.props["label"]="Result"; e.props["format"]="%.2f"; }
    else if (t=="table")   { e.props["headers"]="Item, Price, Total";
                             e.props["rows"]="{{item}}, {{price}}, {{total}}"; }
    return e;
}

static std::string elementListLabel(const Element& e) {
    const std::string& t = e.type;
    std::ostringstream o;
    auto p = [&](const std::string& k, const std::string& def="") {
        auto it = e.props.find(k);
        return it!=e.props.end() ? it->second : def;
    };
    if (t=="heading") { o<<"  "; for(int i=0;i<std::stoi(p("level","1"));i++) o<<'#'; o<<" "<<p("text"); }
    else if (t=="text")    { std::string c=p("content"); o<<"  ¶  "<<c.substr(0,50); if(c.size()>50) o<<"..."; }
    else if (t=="hr")      { o<<"  —  ----------------------------------------"; }
    else if (t=="var")     { o<<"  ◈  @var "<<p("name")<<" = "<<p("value"); }
    else if (t=="toggle")  { o<<"  ☑  @var "<<p("name")<<" = "<<p("value"); }
    else if (t=="const")   { o<<"  ■  @const "<<p("name")<<" = "<<p("value"); }
    else if (t=="derive")  { std::string ex=p("expr"); o<<"  ⇒  @derive "<<p("name")<<" = "<<ex.substr(0,30); }
    else if (t=="math")    { o<<"  ∑  $$ "<<p("content").substr(0,35); }
    else if (t=="display") { o<<"  ▶  display("<<p("expr")<<")"; }
    else if (t=="table")   { o<<"  ▦  table [ "<<p("headers").substr(0,30)<<" ]"; }
    return o.str();
}

// ── .syn generator ────────────────────────────────────────────────────────────
static std::string elementToSyn(const Element& e) {
    const std::string& t = e.type;
    std::ostringstream o;
    auto p = [&](const std::string& k, const std::string& def="") {
        auto it = e.props.find(k); return it!=e.props.end() ? it->second : def;
    };
    if (t=="heading") {
        std::string lvl = p("level","1");
        int n = std::stoi(lvl); if(n<1) n=1; if(n>3) n=3;
        for(int i=0;i<n;i++) o<<'#';
        o<<" "<<p("text");
    } else if (t=="text") {
        o<<p("content");
    } else if (t=="hr") {
        o<<"---";
    } else if (t=="var") {
        std::string name=p("name","x"), val=p("value","0"),
                    wgt=p("widget","none"), lbl=p("label",name),
                    mn=p("min","0"), mx=p("max","100"), st=p("step","1");
        o<<"@var "<<name<<" = "<<val<<"\n";
        if (wgt=="slider")
            o<<"[slider: "<<name<<", min="<<mn<<", max="<<mx<<", step="<<st<<", label=\""<<lbl<<"\"]";
        else if (wgt=="input")
            o<<"[input: "<<name<<", label=\""<<lbl<<"\"]";
    } else if (t=="toggle") {
        std::string name=p("name","flag"), val=p("value","1"), lbl=p("label",name);
        o<<"@var "<<name<<" = "<<val<<"\n";
        o<<"[toggle: "<<name<<", label=\""<<lbl<<"\"]";
    } else if (t=="const") {
        o<<"@const "<<p("name","K")<<" = "<<p("value","1");
    } else if (t=="derive") {
        o<<"@derive "<<p("name","r")<<" = "<<p("expr","0");
    } else if (t=="math") {
        o<<"$$\n";
        for (auto& ln : split(p("content"), '\n')) o<<"  "<<ln<<"\n";
        o<<"$$";
    } else if (t=="display") {
        std::string ex=p("expr","x"), lbl=p("label","Value"), fmt=p("format","");
        o<<"[display: "<<ex<<", label=\""<<lbl<<"\"";
        if (!fmt.empty()) o<<", format=\""<<fmt<<"\"";
        o<<"]";
    } else if (t=="table") {
        auto hdrs = split(p("headers","A, B"), ',');
        std::vector<std::string> sep(hdrs.size(),"---");
        o<<"| ";
        for(size_t i=0;i<hdrs.size();i++) { if(i) o<<" | "; o<<trim(hdrs[i]); }
        o<<" |\n| "<<join(sep," | ")<<" |";
        for (auto& row : split(p("rows",""), '\n')) {
            if (!trim(row).empty()) {
                auto cells = split(row, ',');
                o<<"\n| ";
                for(size_t i=0;i<cells.size();i++) { if(i) o<<" | "; o<<trim(cells[i]); }
                o<<" |";
            }
        }
    }
    return o.str();
}

static std::string documentToSyn() {
    std::ostringstream o;
    auto it = g_meta.find("title");
    if (it!=g_meta.end() && !it->second.empty()) o<<"@title "<<it->second<<"\n";
    it = g_meta.find("author");
    if (it!=g_meta.end() && !it->second.empty()) o<<"@author "<<it->second<<"\n";
    it = g_meta.find("description");
    if (it!=g_meta.end() && !it->second.empty()) o<<"@description "<<it->second<<"\n";
    if (!g_meta.empty()) o<<"\n";
    for (auto& e : g_els) {
        std::string syn = elementToSyn(e);
        if (!syn.empty()) o<<syn<<"\n\n";
    }
    return o.str();
}

// ── .syn parser (round-trip) ──────────────────────────────────────────────────
// Parse key=val pairs from "name, key=val, key2=val2"
static void parseKVPairs(const std::string& rest,
                          std::map<std::string,std::string>& out) {
    for (auto& kv : split(rest, ',')) {
        auto kv2 = trim(kv);
        auto eq = kv2.find('=');
        if (eq != std::string::npos) {
            std::string k = trim(kv2.substr(0,eq));
            std::string v = trim(kv2.substr(eq+1));
            if (v.size()>=2 && v.front()=='"') v = v.substr(1, v.size()-2);
            out[k] = v;
        }
    }
}

// Returns true if s is a table separator row: |---|---|
static bool isTableSep(const std::string& s) {
    if (s.empty() || s.front()!='|') return false;
    for (char c : s) if (c!='|' && c!='-' && c!=' ' && c!=':') return false;
    return true;
}

// Parse [slider: name, ...] or [toggle: name, ...] from a line
static bool parseWidgetLine(const std::string& s, std::string& kind,
                             std::string& name,
                             std::map<std::string,std::string>& kv) {
    if (s.empty() || s.front()!='[' || s.back()!=']') return false;
    std::string inner = trim(s.substr(1, s.size()-2));
    auto colon = inner.find(':');
    if (colon == std::string::npos) return false;
    kind = trim(inner.substr(0, colon));
    if (kind!="slider" && kind!="toggle") return false;
    std::string after = trim(inner.substr(colon+1));
    auto comma = after.find(',');
    name = trim(after.substr(0, comma==std::string::npos ? after.size() : comma));
    if (comma != std::string::npos)
        parseKVPairs(after.substr(comma+1), kv);
    return true;
}

static void parseSyn(const std::string& src) {
    g_meta.clear(); g_els.clear();
    // Pre-scan: build widgetMap from [slider:] / [toggle:] lines
    std::map<std::string,std::map<std::string,std::string>> widgetMap;
    {
        std::istringstream pre(src); std::string line;
        while (std::getline(pre, line)) {
            std::string s = trim(line);
            std::string kind, name;
            std::map<std::string,std::string> kv;
            if (parseWidgetLine(s, kind, name, kv)) {
                kv["widget"] = kind;
                if (!kv.count("label")) kv["label"] = name;
                widgetMap[name] = kv;
            }
        }
    }

    std::istringstream ss(src);
    std::string line;
    bool inMath = false; std::vector<std::string> mathLines;
    bool inTable = false; std::vector<std::string> tableHdrs; std::vector<std::string> tableRows;

    while (std::getline(ss, line)) {
        std::string s = trim(line);

        if (inMath) {
            if (s=="$$") {
                Element e; e.type="math"; e.props["content"]=join(mathLines,"\n");
                g_els.push_back(e); inMath=false; mathLines.clear();
            } else { mathLines.push_back(trim(s)); }
            continue;
        }

        // Flush pending table
        if (inTable && (s.empty() || s[0]!='|')) {
            Element e; e.type="table";
            e.props["headers"] = join(tableHdrs,", ");
            e.props["rows"]    = join(tableRows,"\n");
            g_els.push_back(e); inTable=false; tableHdrs.clear(); tableRows.clear();
        }

        if (s.empty()) continue;

        if (s.substr(0,7)=="@title ")  { g_meta["title"]  = trim(s.substr(7)); continue; }
        if (s.substr(0,8)=="@author ") { g_meta["author"] = trim(s.substr(8)); continue; }
        if (s.substr(0,13)=="@description ") { g_meta["description"] = trim(s.substr(13)); continue; }

        if (s.substr(0,7)=="@const " && s.find('=')!=std::string::npos) {
            auto eq=s.find('='); std::string rest=s.substr(7,eq-7);
            Element e; e.type="const";
            e.props["name"]=trim(rest); e.props["value"]=stripComment(s.substr(eq+1));
            g_els.push_back(e); continue;
        }
        if (s.substr(0,8)=="@derive " && s.find('=')!=std::string::npos) {
            auto eq=s.find('='); std::string rest=s.substr(8,eq-8);
            Element e; e.type="derive";
            e.props["name"]=trim(rest); e.props["expr"]=stripComment(s.substr(eq+1));
            g_els.push_back(e); continue;
        }
        if (s.substr(0,5)=="@var " && s.find('=')!=std::string::npos) {
            auto eq=s.find('=');
            std::string name=trim(s.substr(5,eq-5));
            std::string val =stripComment(s.substr(eq+1));
            auto wit = widgetMap.find(name);
            if (wit!=widgetMap.end() && wit->second.count("widget")) {
                std::string wt = wit->second.at("widget");
                if (wt=="toggle") {
                    Element e; e.type="toggle";
                    e.props["name"]=name; e.props["value"]=val;
                    e.props["label"]=wit->second.count("label") ? wit->second.at("label") : name;
                    g_els.push_back(e);
                } else {
                    Element e; e.type="var";
                    e.props["name"]=name; e.props["value"]=val;
                    e.props["widget"]=wt;
                    e.props["label"]  = wit->second.count("label") ? wit->second.at("label") : name;
                    e.props["min"]    = wit->second.count("min")   ? wit->second.at("min")   : "0";
                    e.props["max"]    = wit->second.count("max")   ? wit->second.at("max")   : "100";
                    e.props["step"]   = wit->second.count("step")  ? wit->second.at("step")  : "1";
                    g_els.push_back(e);
                }
            } else {
                Element e; e.type="var"; e.props["name"]=name; e.props["value"]=val;
                e.props["widget"]="none"; e.props["label"]=name;
                e.props["min"]="0"; e.props["max"]="100"; e.props["step"]="1";
                g_els.push_back(e);
            }
            continue;
        }
        if (s=="$$") { inMath=true; continue; }
        if (s=="---") { Element e; e.type="hr"; g_els.push_back(e); continue; }
        if (!s.empty() && s[0]=='#') {
            size_t n=0; while(n<s.size()&&s[n]=='#') n++;
            Element e; e.type="heading";
            e.props["level"]=std::to_string(std::min((size_t)3,n));
            e.props["text"]=trim(s.substr(n));
            g_els.push_back(e); continue;
        }
        if (!s.empty() && s[0]=='[') {
            // Skip widget lines already consumed; handle [display:]
            if (s.find("display:")==1) {
                std::string inner = s.substr(1, s.size()-2);
                auto parts = split(inner.substr(8), ',');
                Element e; e.type="display";
                e.props["expr"]   = parts.empty() ? "x" : trim(parts[0]);
                e.props["label"]  = "Value"; e.props["format"] = "";
                for (size_t i=1;i<parts.size();i++) {
                    auto& p2=parts[i]; auto eq=p2.find('=');
                    if (eq!=std::string::npos) {
                        std::string k=trim(p2.substr(0,eq));
                        std::string v=trim(p2.substr(eq+1));
                        if (!v.empty()&&v.front()=='"') v=v.substr(1,v.size()-2);
                        e.props[k]=v;
                    }
                }
                g_els.push_back(e);
            }
            continue;
        }
        if (!s.empty() && s[0]=='|') {
            if (isTableSep(s)) {
                if (!tableHdrs.empty()) inTable=true;
                continue;
            }
            auto cells = split(s.substr(1, s.size()-2), '|');
            std::vector<std::string> row;
            for (auto& c : cells) row.push_back(trim(c));
            if (tableHdrs.empty()) { tableHdrs=row; inTable=true; }
            else { tableRows.push_back(join(row, ", ")); }
            continue;
        }
        if (!s.empty() && s[0]!='@') {
            Element e; e.type="text"; e.props["content"]=s;
            g_els.push_back(e); continue;
        }
    }
    // Flush pending table
    if (inTable && !tableHdrs.empty()) {
        Element e; e.type="table";
        e.props["headers"]=join(tableHdrs,", "); e.props["rows"]=join(tableRows,"\n");
        g_els.push_back(e);
    }
}

// ── Control layout helpers ────────────────────────────────────────────────────
static HWND mkCtl(LPCWSTR cls, LPCWSTR txt, DWORD style, int x, int y, int w, int h, HWND par, HMENU id) {
    HWND hw = CreateWindowExW(0, cls, txt, WS_CHILD|WS_VISIBLE|style,
                               x, y, w, h, par, id, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return hw;
}
static HWND mkEdit(const std::string& txt, int x, int y, int w, int h, HWND par, HMENU id, bool multi=false) {
    DWORD style = ES_AUTOHSCROLL;
    if (multi) style = ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN;
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", toW(txt).c_str(),
                               WS_CHILD|WS_VISIBLE|style, x, y, w, h, par, id,
                               GetModuleHandle(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return hw;
}
static HWND mkLabel(const std::string& txt, int x, int y, int w, int h, HWND par) {
    return mkCtl(L"STATIC", toW(txt).c_str(), SS_LEFT, x, y, w, h, par, nullptr);
}
static HWND mkButton(const std::string& txt, int x, int y, int w, int h, HWND par, HMENU id) {
    return mkCtl(L"BUTTON", toW(txt).c_str(), BS_PUSHBUTTON, x, y, w, h, par, id);
}
static HWND mkCombo(const std::vector<std::string>& items, const std::string& sel,
                    int x, int y, int w, int h, HWND par, HMENU id) {
    HWND hw = mkCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST|WS_VSCROLL, x, y, w, h+200, par, id);
    int cur = 0;
    for (size_t i=0;i<items.size();i++) {
        SendMessageW(hw, CB_ADDSTRING, 0, (LPARAM)toW(items[i]).c_str());
        if (items[i]==sel) cur=(int)i;
    }
    SendMessageW(hw, CB_SETCURSEL, cur, 0);
    return hw;
}

// ── Properties panel ──────────────────────────────────────────────────────────
static void clearProps() {
    for (int i=0;i<MAX_PROP_SLOTS;i++) {
        if (g_propLbls[i]) { DestroyWindow(g_propLbls[i]); g_propLbls[i]=nullptr; }
        if (g_propCtls[i]) { DestroyWindow(g_propCtls[i]); g_propCtls[i]=nullptr; }
        g_propKeys[i]="";
    }
    g_propCount=0;
}

struct PropField { std::string key, label, value; bool multi=false; bool isCombo=false; std::vector<std::string> opts; };

static void buildProps(const std::vector<PropField>& fields) {
    clearProps();
    RECT rc; GetClientRect(g_hPropsPanel, &rc);
    int pw = rc.right - rc.left;
    int y = 8;
    int labelW = 90, editX = 100, editW = pw - editX - 8;
    g_propCount = (int)std::min(fields.size(), (size_t)MAX_PROP_SLOTS);

    for (int i=0;i<g_propCount;i++) {
        const auto& f = fields[i];
        g_propKeys[i] = f.key;
        int h = f.multi ? 80 : 22;
        g_propLbls[i] = mkLabel(f.label+":", 8, y+2, labelW, 20, g_hPropsPanel);
        if (f.isCombo) {
            g_propCtls[i] = mkCombo(f.opts, f.value, editX, y, editW, 22, g_hPropsPanel, (HMENU)(size_t)(IDC_PROP_CTL(i)));
        } else {
            g_propCtls[i] = mkEdit(f.value, editX, y, editW, h, g_hPropsPanel, (HMENU)(size_t)(IDC_PROP_CTL(i)), f.multi);
        }
        y += h + 8;
    }
    // Apply button
    mkButton("Apply Changes", 8, y+4, pw-16, 26, g_hPropsPanel, (HMENU)IDC_APPLY);
}

static void showPropsForElement(const Element& e) {
    const std::string& t = e.type;
    auto p = [&](const std::string& k, const std::string& def="") {
        auto it=e.props.find(k); return it!=e.props.end() ? it->second : def;
    };

    std::vector<PropField> fields;
    if (t=="heading") {
        fields.push_back({"level","Level",p("level","1"),false,true,{"1","2","3"}});
        fields.push_back({"text","Text",p("text")});
    } else if (t=="text") {
        fields.push_back({"content","Content",p("content"),true});
    } else if (t=="var") {
        fields.push_back({"name","Name",p("name")});
        fields.push_back({"value","Default",p("value")});
        fields.push_back({"widget","Widget",p("widget","slider"),false,true,{"slider","input","none"}});
        fields.push_back({"label","Label",p("label")});
        fields.push_back({"min","Min",p("min","0")});
        fields.push_back({"max","Max",p("max","100")});
        fields.push_back({"step","Step",p("step","1")});
    } else if (t=="toggle") {
        fields.push_back({"name","Name",p("name")});
        fields.push_back({"value","Default",p("value","1"),false,true,{"1","0"}});
        fields.push_back({"label","Label",p("label")});
    } else if (t=="const") {
        fields.push_back({"name","Name",p("name")});
        fields.push_back({"value","Value",p("value")});
    } else if (t=="derive") {
        fields.push_back({"name","Name",p("name")});
        fields.push_back({"expr","Expression",p("expr"),true});
    } else if (t=="math") {
        fields.push_back({"content","Equations",p("content"),true});
    } else if (t=="display") {
        fields.push_back({"expr","Expression",p("expr")});
        fields.push_back({"label","Label",p("label")});
        fields.push_back({"format","Format",p("format")});
    } else if (t=="table") {
        fields.push_back({"headers","Headers",p("headers")});
        fields.push_back({"rows","Rows",p("rows"),true});
    } else {
        // hr or unknown
    }
    buildProps(fields);
}

static void applyProps() {
    if (g_sel<0 || g_sel>=(int)g_els.size()) return;
    Element& e = g_els[g_sel];
    for (int i=0;i<g_propCount;i++) {
        if (!g_propCtls[i] || g_propKeys[i].empty()) continue;
        // Check if it's a combo box
        wchar_t cls[64]={0};
        GetClassNameW(g_propCtls[i], cls, 64);
        std::string val;
        if (wcscmp(cls, L"ComboBox")==0) {
            int idx = (int)SendMessageW(g_propCtls[i], CB_GETCURSEL, 0, 0);
            if (idx>=0) {
                int len = (int)SendMessageW(g_propCtls[i], CB_GETLBTEXTLEN, idx, 0);
                std::wstring w(len+1, 0);
                SendMessageW(g_propCtls[i], CB_GETLBTEXT, idx, (LPARAM)&w[0]);
                if (!w.empty() && w.back()==0) w.pop_back();
                val = fromW(w);
            }
        } else {
            val = getWndText(g_propCtls[i]);
        }
        e.props[g_propKeys[i]] = val;
    }
    // Refresh list label
    std::string lbl = elementListLabel(e);
    SendMessageW(g_hList, LB_DELETESTRING, g_sel, 0);
    SendMessageW(g_hList, LB_INSERTSTRING, g_sel, (LPARAM)toW(lbl).c_str());
    SendMessageW(g_hList, LB_SETCURSEL, g_sel, 0);
    // Update preview
    std::string syn = documentToSyn();
    setWndText(g_hPreview, syn);
    g_dirty = true;
}

// ── List helpers ──────────────────────────────────────────────────────────────
static void refreshList(int keepSel=-1) {
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (auto& e : g_els) {
        std::string lbl = elementListLabel(e);
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)toW(lbl).c_str());
    }
    if (keepSel>=0 && keepSel<(int)g_els.size()) {
        SendMessageW(g_hList, LB_SETCURSEL, keepSel, 0);
        g_sel = keepSel;
        showPropsForElement(g_els[g_sel]);
    } else if (!g_els.empty()) {
        SendMessageW(g_hList, LB_SETCURSEL, 0, 0);
        g_sel = 0;
        showPropsForElement(g_els[0]);
    } else {
        clearProps();
        g_sel = -1;
    }
    std::string syn = documentToSyn();
    setWndText(g_hPreview, syn);
}

// ── File I/O ──────────────────────────────────────────────────────────────────
static void saveToPath(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    std::string syn = documentToSyn();
    f.write(syn.c_str(), (std::streamsize)syn.size());
    g_dirty = false;
    SetWindowTextW(g_hwnd, toW("Structura Maker — " + path).c_str());
}
static bool fileDlg(bool save, std::string& path) {
    wchar_t buf[MAX_PATH]={0};
    if (!path.empty()) wcscpy(buf, toW(path).c_str());
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"Syntaxia Document (*.syn)\0*.syn\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (save) { ofn.Flags = OFN_OVERWRITEPROMPT; ofn.lpstrDefExt = L"syn"; }
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok) { path = fromW(buf); return true; }
    return false;
}

static void showMetaDialog() {
    // Build a simple dialog at runtime (no resource file needed)
    // We'll use a CreateDialog approach by constructing a DLGTEMPLATE in memory.
    // For simplicity, use a child window instead.
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
        L"#32770", L"Document Metadata",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        300, 200, 400, 220,
        g_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    SendMessageW(hDlg, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    auto mk = [&](const std::string& lbl, const std::string& val, int y, HMENU id) {
        mkLabel(lbl+":", 10, y, 80, 20, hDlg);
        return mkEdit(val, 95, y-2, 280, 22, hDlg, id);
    };
    HWND hT = mk("Title",       g_meta["title"],       15, (HMENU)101);
    HWND hA = mk("Author",      g_meta["author"],      45, (HMENU)102);
    HWND hD = mk("Description", g_meta["description"], 75, (HMENU)103);
    mkButton("Save", 95, 110, 120, 28, hDlg, (HMENU)IDOK);
    mkButton("Cancel", 235, 110, 80, 28, hDlg, (HMENU)IDCANCEL);

    ShowWindow(hDlg, SW_SHOW);
    EnableWindow(g_hwnd, FALSE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.hwnd==hDlg || IsChild(hDlg, msg.hwnd)) {
            if (msg.message==WM_COMMAND) {
                if (LOWORD(msg.wParam)==IDOK) {
                    g_meta["title"]       = getWndText(hT);
                    g_meta["author"]      = getWndText(hA);
                    g_meta["description"] = getWndText(hD);
                    setWndText(g_hPreview, documentToSyn());
                    break;
                }
                if (LOWORD(msg.wParam)==IDCANCEL) break;
            }
            if (msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE) break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(g_hwnd, TRUE);
    DestroyWindow(hDlg);
    SetForegroundWindow(g_hwnd);
}

// ── Layout ────────────────────────────────────────────────────────────────────
static void layoutControls() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    const int TB_H    = 42;
    const int PREV_H  = 160;
    const int LEFT_W  = W * 42 / 100;
    const int RIGHT_W = W - LEFT_W;
    const int MID_H   = H - TB_H - PREV_H - 6;

    // List panel
    MoveWindow(g_hList, 6, TB_H+2, LEFT_W-12, MID_H-2, TRUE);
    // Props panel
    MoveWindow(g_hPropsPanel, LEFT_W+2, TB_H+2, RIGHT_W-8, MID_H-2, TRUE);
    // Preview
    MoveWindow(g_hPreview, 6, TB_H+MID_H+4, W-12, PREV_H-8, TRUE);
}

// ── Window procedure ──────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Font
        g_hFont = CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                               DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS, L"Segoe UI");

        // Toolbar buttons
        int bx = 6;
        auto tb = [&](const std::string& t, HMENU id, int w=70) {
            mkButton(t, bx, 8, w, 26, hWnd, id); bx+=w+4; };
        tb("New",    (HMENU)ID_NEW,  60);
        tb("Open",   (HMENU)ID_OPEN, 60);
        tb("Save",   (HMENU)ID_SAVE, 60);
        bx+=8;
        tb("Add...", (HMENU)(ID_ADD_BASE+99), 70);
        bx+=8;
        tb("Up",     (HMENU)ID_MOVE_UP,   40);
        tb("Down",   (HMENU)ID_MOVE_DOWN, 50);
        tb("Delete", (HMENU)ID_DELETE,    60);
        bx+=8;
        tb("Meta",   (HMENU)ID_META,  55);
        tb("Export .syn", (HMENU)ID_EXPORT, 90);

        // Element list (left)
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS,
            0,0,0,0, hWnd, (HMENU)IDC_LIST, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Properties panel (right)
        g_hPropsPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", nullptr,
            WS_CHILD|WS_VISIBLE|SS_LEFT, 0,0,0,0,
            hWnd, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hPropsPanel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Preview (bottom)
        g_hPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|
            WS_HSCROLL|ES_AUTOHSCROLL|ES_READONLY,
            0,0,0,0, hWnd, (HMENU)IDC_PREVIEW, GetModuleHandle(nullptr), nullptr);
        SendMessageW(g_hPreview, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        memset(g_propLbls,0,sizeof(g_propLbls));
        memset(g_propCtls,0,sizeof(g_propCtls));

        layoutControls();
        refreshList(0);
        break;
    }
    case WM_SIZE:
        layoutControls();
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notif = HIWORD(wParam);

        if (id==IDC_LIST && notif==LBN_SELCHANGE) {
            int idx = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
            if (idx>=0 && idx<(int)g_els.size()) {
                g_sel = idx;
                showPropsForElement(g_els[idx]);
            }
            break;
        }
        if (id==IDC_APPLY) { applyProps(); break; }

        // Add element menu
        if (id==ID_ADD_BASE+99) {
            HMENU menu = CreatePopupMenu();
            for (int i=0;i<PALETTE_COUNT;i++)
                AppendMenuW(menu, MF_STRING, ID_ADD_BASE+i, PALETTE_LABELS[i]);
            POINT pt={6+4*(60+4+60+4+60+4+8), 8};
            ClientToScreen(hWnd, &pt);
            TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, pt.x, pt.y+26, 0, hWnd, nullptr);
            DestroyMenu(menu);
            break;
        }
        if (id>=ID_ADD_BASE && id<ID_ADD_BASE+PALETTE_COUNT) {
            int ti = id - ID_ADD_BASE;
            Element e = defaultElement(PALETTE_TYPES[ti]);
            int ins = (g_sel>=0) ? g_sel+1 : (int)g_els.size();
            g_els.insert(g_els.begin()+ins, e);
            refreshList(ins);
            break;
        }

        if (id==ID_MOVE_UP && g_sel>0) {
            std::swap(g_els[g_sel], g_els[g_sel-1]);
            refreshList(g_sel-1); break;
        }
        if (id==ID_MOVE_DOWN && g_sel>=0 && g_sel<(int)g_els.size()-1) {
            std::swap(g_els[g_sel], g_els[g_sel+1]);
            refreshList(g_sel+1); break;
        }
        if (id==ID_DELETE && g_sel>=0) {
            g_els.erase(g_els.begin()+g_sel);
            int ns = std::min(g_sel, (int)g_els.size()-1);
            refreshList(ns<0?0:ns); break;
        }
        if (id==ID_META) { showMetaDialog(); break; }

        if (id==ID_NEW) {
            if (g_dirty && MessageBoxW(hWnd, L"Discard unsaved changes?", L"New", MB_YESNO)==IDNO) break;
            g_meta = {{"title","Untitled"},{"author",""},{"description",""}};
            g_els = { defaultElement("heading"), defaultElement("text") };
            g_path.clear(); g_dirty=false;
            SetWindowTextW(hWnd, L"Structura Maker — Untitled");
            refreshList(0); break;
        }
        if (id==ID_OPEN) {
            if (g_dirty && MessageBoxW(hWnd, L"Discard unsaved changes?", L"Open", MB_YESNO)==IDNO) break;
            std::string path;
            if (fileDlg(false, path)) {
                std::ifstream f(path, std::ios::binary);
                std::string src((std::istreambuf_iterator<char>(f)),{});
                parseSyn(src);
                g_path = path;
                g_dirty = false;
                SetWindowTextW(hWnd, toW("Structura Maker — "+path).c_str());
                refreshList(0);
            }
            break;
        }
        if (id==ID_SAVE) {
            if (g_path.empty()) {
                if (!fileDlg(true, g_path)) break;
            }
            saveToPath(g_path); break;
        }
        if (id==ID_EXPORT) {
            std::string p;
            if (fileDlg(true, p)) saveToPath(p);
            break;
        }
        break;
    }
    case WM_CLOSE:
        if (g_dirty && MessageBoxW(hWnd,L"Unsaved changes. Quit?",L"Quit",MB_YESNO)==IDNO) break;
        DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0); break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int nShow) {
    InitCommonControls();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE+1);
    wc.lpszClassName = L"StructuraMaker";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_meta = {{"title","Untitled"},{"author",""},{"description",""}};
    g_els  = { defaultElement("heading"), defaultElement("text") };

    g_hwnd = CreateWindowExW(0, L"StructuraMaker", L"Structura Maker — Untitled",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 800, nullptr, nullptr, hInst, nullptr);

    // Open file from command line if provided
    if (lpCmd && lpCmd[0]) {
        std::string arg(lpCmd);
        if (arg.front()=='"') { arg=arg.substr(1); if(arg.back()=='"') arg.pop_back(); }
        if (!arg.empty()) {
            std::ifstream f(arg, std::ios::binary);
            if (f) {
                std::string src((std::istreambuf_iterator<char>(f)),{});
                parseSyn(src);
                g_path = arg;
                SetWindowTextW(g_hwnd, toW("Structura Maker — "+arg).c_str());
                refreshList(0);
            }
        }
    }

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
