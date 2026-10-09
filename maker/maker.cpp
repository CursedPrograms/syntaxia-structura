/*
 * Structura Maker — C++ Win32 .syn source-code editor
 * Features: RichEdit syntax highlighting, undo/redo, tab→spaces,
 *           outline navigator, Structura + Syntaxia snippet inserts,
 *           status bar with line/col, drag-drop.
 * Compile:
 *   g++ -std=c++14 -mwindows -O2 -static-libgcc -static-libstdc++ \
 *       -o StructuraMaker.exe maker.cpp \
 *       -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

// ── IDs ───────────────────────────────────────────────────────────────────────
#define ID_NEW          101
#define ID_OPEN         102
#define ID_SAVE         103
#define ID_EXPORT       104
#define ID_META         105
#define ID_UNDO         106
#define ID_REDO         107
#define ID_PARSE        108
#define ID_ADD_OPEN     200
#define ID_STRUCT_BASE  300   // 300..314
#define ID_SYN_BASE     400   // 400..409
#define IDC_LIST        501
#define IDC_EDITOR      502
#define IDC_STATUS      503
#define TIMER_COLORIZE  1
#define TIMER_OUTLINE   2

// ── String helpers ────────────────────────────────────────────────────────────
static std::wstring toW(const std::string &s){
    if(s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);
    std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&w[0],n);
    if(!w.empty()&&w.back()==0)w.pop_back();return w;}
static std::string fromW(const std::wstring &w){
    if(w.empty()) return "";
    int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,&s[0],n,nullptr,nullptr);
    if(!s.empty()&&s.back()==0)s.pop_back();return s;}
static std::string trim(const std::string &s){
    size_t a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return "";
    size_t b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}

// ── Snippet catalogue ─────────────────────────────────────────────────────────
struct Snip { const wchar_t *label; const char *text; };
static const Snip STRUCT_SNIPS[] = {
    {L"# Heading 1",       "# New Section\n"},
    {L"## Heading 2",      "## Sub-section\n"},
    {L"### Heading 3",     "### Sub-sub-section\n"},
    {L"¶  Paragraph",      "Enter paragraph text here.\n"},
    {L"—  Divider",        "---\n"},
    {L"◈  Variable",       "@var x = 0\n[slider: x, min=0, max=100, step=1, label=\"Variable\"]\n"},
    {L"☑  Toggle",         "@var flag = 1\n[toggle: flag, label=\"Enable\"]\n"},
    {L"[?] Input field",   "@var n = 0\n[input: n, type=number, label=\"Enter value\"]\n"},
    {L"■  Constant",       "@const K = 1.0\n"},
    {L"⇒  Derive",         "@derive result = a + b\n"},
    {L"∑  Math block",     "$$\n  y = f(x)\n$$\n"},
    {L"▶  Display",        "[display: x, label=\"Value\", format=\"%.2f\"]\n"},
    {L"▦  Table",          "| Col A | Col B |\n|-------|-------|\n| {{a}} | {{b}} |\n"},
    {L"[+] Button",        "[button: label=\"Click\", set x = x + 1]\n"},
    {L"[~] Plot",          "[plot: x, x * x, x_min=0, x_max=10, label=\"Plot\"]\n"},
    {L"📄 A4 page",        "<a4page>\n\n</a4page>\n"},
    {L"📄 A3 page",        "<a3page>\n\n</a3page>\n"},
    {L"📄 A5 page",        "<a5page>\n\n</a5page>\n"},
    {L"📄 Letter page",    "<letterpage>\n\n</letterpage>\n"},
    {L"📄 Legal page",     "<legalpage>\n\n</legalpage>\n"},
    {L"🎨 Strss style",    "@strss myclass: color=#1a1a8c, size=14, bold=1, align=left\n"},
};
static const int STRUCT_COUNT = 21;
static const Snip SYN_SNIPS[] = {
    {L"<func>   Function",    "<func name=\"my_func\">\n\n</func>\n"},
    {L"<var>    Variable",    "<var name=\"x\" type=\"int\">0</var>\n"},
    {L"<print>  Print",       "<print>x</print>\n"},
    {L"<if>     If block",    "<if condition=\"x > 0\">\n\n</if>\n"},
    {L"<elif>   Else-if",     "<elif condition=\"x == 0\">\n\n</elif>\n"},
    {L"<else>   Else",        "<else>\n\n</else>\n"},
    {L"<while>  While loop",  "<while condition=\"i < 5\">\n\n</while>\n"},
    {L"<for>    For loop",    "<for condition=\"i < 10\">\n\n</for>\n"},
    {L"<~>      Type cast",   "<~>int==x<~>\n"},
    {L"<@>      Import",      "<@>other.syn</@>\n"},
};
static const int SYN_COUNT = 10;

// ── Globals ───────────────────────────────────────────────────────────────────
static HWND   g_hwnd, g_hList, g_hEditor, g_hStatus;
static HFONT  g_hUi, g_hMono;
static WNDPROC g_origEdProc = nullptr;
static std::string g_path;
static std::vector<int> g_lineNums;
static bool g_colorizing = false;  // guard against recursive colorize

// ── RichEdit class (loaded at start) ─────────────────────────────────────────
static HMODULE g_hRichDll = nullptr;
static const wchar_t *g_richClass = nullptr;
static void loadRichEdit() {
    g_hRichDll = LoadLibraryW(L"MSFTEDIT.DLL");
    g_richClass = g_hRichDll ? L"RICHEDIT50W" : L"RichEdit20W";
    if (!g_hRichDll) g_hRichDll = LoadLibraryW(L"riched20.dll");
}

// ── Editor helpers ────────────────────────────────────────────────────────────
static std::string editorText(){
    int len=GetWindowTextLengthW(g_hEditor);
    std::wstring w(len+1,0);GetWindowTextW(g_hEditor,&w[0],len+1);
    if(!w.empty()&&w.back()==0)w.pop_back();return fromW(w);}
static void setEditorText(const std::string &s){
    SetWindowTextW(g_hEditor,toW(s).c_str());
    SendMessageW(g_hEditor,EM_EMPTYUNDOBUFFER,0,0);}

static void insertSnippet(const char *txt){
    SetFocus(g_hEditor);
    std::string in(txt);std::string out;
    for(size_t i=0;i<in.size();i++){if(in[i]=='\n'&&(i==0||in[i-1]!='\r'))out+='\r';out+=in[i];}
    SendMessageW(g_hEditor,EM_REPLACESEL,TRUE,(LPARAM)toW(out).c_str());}

// ── Syntax highlighting ───────────────────────────────────────────────────────
struct LineStyle { COLORREF col; bool bold, italic; };
static LineStyle styleFor(const std::string &s){
    if(s.empty())             return {RGB(30,30,30),false,false};
    if(s[0]=='#')             return {RGB(0,55,130),true,false};
    if(s.substr(0,7)=="@title "||s.substr(0,8)=="@author "||s.substr(0,13)=="@description ")
                              return {RGB(110,100,140),false,false};
    if(s.substr(0,5)=="@var "||s.substr(0,7)=="@const ")
                              return {RGB(0,110,200),false,false};
    if(s.substr(0,8)=="@derive ")
                              return {RGB(0,140,140),false,false};
    if(s=="---")              return {RGB(160,160,160),false,false};
    if(s=="$$")               return {RGB(110,0,170),true,false};
    if(s[0]=='[')             return {RGB(0,130,50),false,false};
    if(s.find("<func")==0||s.find("</func")==0)
                              return {RGB(170,30,0),true,false};
    if(s[0]=='<'&&s[1]!='!')  return {RGB(190,60,0),false,false};
    return {RGB(30,30,30),false,false};
}
static void applyFmt(int a,int b,COLORREF c,bool bold,bool italic){
    SendMessageW(g_hEditor,EM_SETSEL,a,b);
    CHARFORMAT2W cf={};cf.cbSize=sizeof(cf);
    cf.dwMask=CFM_COLOR|CFM_BOLD|CFM_ITALIC;
    cf.crTextColor=c;
    cf.dwEffects=(bold?CFE_BOLD:0)|(italic?CFE_ITALIC:0);
    SendMessageW(g_hEditor,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf);}

static void colorize(){
    if(g_colorizing) return;
    g_colorizing=true;
    DWORD ss,se;SendMessageW(g_hEditor,EM_GETSEL,(WPARAM)&ss,(LPARAM)&se);
    LRESULT evMask=SendMessageW(g_hEditor,EM_GETEVENTMASK,0,0);
    SendMessageW(g_hEditor,EM_SETEVENTMASK,0,0);
    SendMessageW(g_hEditor,WM_SETREDRAW,FALSE,0);

    int lineCount=(int)SendMessageW(g_hEditor,EM_GETLINECOUNT,0,0);
    for(int i=0;i<lineCount;i++){
        int ci=(int)SendMessageW(g_hEditor,EM_LINEINDEX,i,0);
        int ll=(int)SendMessageW(g_hEditor,EM_LINELENGTH,ci,0);
        if(ll==0)continue;
        // Fetch line text
        std::vector<wchar_t>buf(ll+2,0);
        *(WORD*)buf.data()=(WORD)(ll+1);
        int got=(int)SendMessageW(g_hEditor,EM_GETLINE,i,(LPARAM)buf.data());
        buf[got]=0;
        std::string ls=fromW(std::wstring(buf.data(),got));
        std::string s=trim(ls);

        LineStyle st=styleFor(s);
        applyFmt(ci,ci+ll,st.col,st.bold,st.italic);

        // Highlight {{...}} spans in orange
        size_t p=ls.find("{{");
        while(p!=std::string::npos){
            size_t e=ls.find("}}",p+2);
            if(e==std::string::npos)break;
            applyFmt(ci+(int)p,ci+(int)e+2,RGB(180,90,0),false,false);
            p=ls.find("{{",e+2);
        }
        // Highlight -- comments in gray italic (find after the value)
        size_t cmt=ls.find("--");
        if(cmt!=std::string::npos)
            applyFmt(ci+(int)cmt,ci+ll,RGB(130,130,130),false,true);
    }

    SendMessageW(g_hEditor,EM_SETSEL,ss,se);
    SendMessageW(g_hEditor,WM_SETREDRAW,TRUE,0);
    SendMessageW(g_hEditor,EM_SETEVENTMASK,0,evMask);
    InvalidateRect(g_hEditor,nullptr,FALSE);
    g_colorizing=false;
}

// ── Status bar ────────────────────────────────────────────────────────────────
static void updateStatus(){
    if(!g_hStatus)return;
    DWORD sel=0,selEnd=0;SendMessageW(g_hEditor,EM_GETSEL,(WPARAM)&sel,(LPARAM)&selEnd);
    int ln=(int)SendMessageW(g_hEditor,EM_LINEFROMCHAR,sel,0);
    int ls=(int)SendMessageW(g_hEditor,EM_LINEINDEX,ln,0);
    int col=(int)sel-ls;
    bool mod=(SendMessageW(g_hEditor,EM_GETMODIFY,0,0)!=0);
    std::string s="  Line "+(std::to_string(ln+1))+"  Col "+(std::to_string(col+1));
    if(mod) s+="  *";
    if(!g_path.empty()){auto sl=g_path.find_last_of("/\\");s+="  |  "+(sl==std::string::npos?g_path:g_path.substr(sl+1));}
    SetWindowTextW(g_hStatus,toW(s).c_str());}

// ── Outline ───────────────────────────────────────────────────────────────────
static void reparseOutline(){
    SendMessageW(g_hList,LB_RESETCONTENT,0,0);g_lineNums.clear();
    std::string src=editorText();
    std::istringstream ss(src);std::string line;int li=0;
    while(std::getline(ss,line)){
        std::string s=trim(line),label;
        if(!s.empty()&&s[0]=='#'){
            size_t n=0;while(n<s.size()&&s[n]=='#')n++;
            label=std::string(2*(n-1),' ')+s;
        }else if(s.substr(0,5)=="@var ") label="  ◈ "+s.substr(5,40);
        else if(s.substr(0,7)=="@const ")label="  ■ "+s.substr(7,38);
        else if(s.substr(0,8)=="@derive ")label="  ⇒ "+s.substr(8,36);
        else if(s=="---")                 label="  ─────────────────";
        else if(s=="$$")                  label="  ∑ math block";
        else if(s.find("<func")==0)       label="► "+s.substr(0,std::min(s.size(),(size_t)50));
        else if(s.find("<if")==0||s.find("<while")==0||s.find("<for")==0)
                                          label="  └ "+s.substr(0,std::min(s.size(),(size_t)40));
        else if(!s.empty()&&s[0]=='['){
            auto c=s.find(':');std::string k=c!=std::string::npos?s.substr(1,c-1):"";
            if(k=="slider"||k=="toggle"||k=="display"||k=="button"||k=="plot"||k=="input")
                label="  ["+k+"] "+s.substr(c+1,std::min(s.size()-c-1,(size_t)30));
        }else if(s.substr(0,7)=="@title ")label="★ "+s.substr(7);
        else if(s.substr(0,8)=="@author ")label="  by "+s.substr(8);
        if(!label.empty()){
            SendMessageW(g_hList,LB_ADDSTRING,0,(LPARAM)toW(label).c_str());
            g_lineNums.push_back(li);}
        li++;
    }
}
static void navigateTo(int idx){
    if(idx<0||idx>=(int)g_lineNums.size())return;
    int ci=(int)SendMessageW(g_hEditor,EM_LINEINDEX,g_lineNums[idx],0);
    int ll=(int)SendMessageW(g_hEditor,EM_LINELENGTH,ci,0);
    SendMessageW(g_hEditor,EM_SETSEL,ci,ci+ll);
    SendMessageW(g_hEditor,EM_SCROLLCARET,0,0);SetFocus(g_hEditor);}

// ── Tab-key subclass ──────────────────────────────────────────────────────────
static LRESULT CALLBACK EditorSubProc(HWND hWnd,UINT msg,WPARAM wP,LPARAM lP){
    if(msg==WM_KEYDOWN&&wP==VK_TAB){
        SendMessageW(hWnd,EM_REPLACESEL,TRUE,(LPARAM)L"    ");return 0;}
    if(msg==WM_CHAR&&wP=='\t') return 0; // suppress default tab char
    return CallWindowProcW(g_origEdProc,hWnd,msg,wP,lP);}

// ── Title bar ─────────────────────────────────────────────────────────────────
static void updateTitle(){
    bool mod=(SendMessageW(g_hEditor,EM_GETMODIFY,0,0)!=0);
    std::string t="Structura Maker";
    if(!g_path.empty())t+=" \xe2\x80\x94 "+g_path;
    if(mod)t+=" *";
    SetWindowTextW(g_hwnd,toW(t).c_str());}

// ── File dialogs ──────────────────────────────────────────────────────────────
static bool fileDlg(bool save,std::string&path){
    wchar_t buf[MAX_PATH]={};if(!path.empty())wcscpy(buf,toW(path).c_str());
    OPENFILENAMEW ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_hwnd;
    ofn.lpstrFilter=L"Syntaxia Document (*.syn)\0*.syn\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile=buf;ofn.nMaxFile=MAX_PATH;
    if(save){ofn.Flags=OFN_OVERWRITEPROMPT;ofn.lpstrDefExt=L"syn";}
    else    {ofn.Flags=OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;}
    BOOL ok=save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn);
    if(ok){path=fromW(buf);return true;}return false;}
static bool checkDirty(){
    if(!SendMessageW(g_hEditor,EM_GETMODIFY,0,0))return true;
    return MessageBoxW(g_hwnd,L"Discard unsaved changes?",L"Structura Maker",MB_YESNO|MB_ICONQUESTION)==IDYES;}
static void doOpen(const std::string&path){
    std::ifstream f(path,std::ios::binary);
    if(!f){MessageBoxW(g_hwnd,(std::wstring(L"Cannot open:\n")+toW(path)).c_str(),L"Structura Maker",MB_OK|MB_ICONERROR);return;}
    std::string src((std::istreambuf_iterator<char>(f)),{});
    std::string norm;for(size_t i=0;i<src.size();i++){if(src[i]=='\n'&&(i==0||src[i-1]!='\r'))norm+='\r';norm+=src[i];}
    setEditorText(norm);reparseOutline();colorize();updateTitle();updateStatus();}
static void doSave(const std::string&path){
    std::string src=editorText();std::string out;
    for(size_t i=0;i<src.size();i++)if(src[i]!='\r')out+=src[i];
    std::ofstream f(path,std::ios::binary);f.write(out.c_str(),(std::streamsize)out.size());
    SendMessageW(g_hEditor,EM_SETMODIFY,FALSE,0);updateTitle();updateStatus();}

// ── Meta dialog ───────────────────────────────────────────────────────────────
static void showMetaDialog(){
    std::string srcClean;{std::string s=editorText();for(char c:s)if(c!='\r')srcClean+=c;}
    auto readMeta=[&](const std::string&prefix)->std::string{
        auto p=srcClean.find(prefix);if(p==std::string::npos)return "";
        auto e=srcClean.find('\n',p);
        return trim(srcClean.substr(p+prefix.size(),(e==std::string::npos?srcClean.size():e)-(p+prefix.size())));};

    HWND hD=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,L"#32770",L"Document Metadata",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,300,200,440,230,g_hwnd,nullptr,GetModuleHandle(nullptr),nullptr);
    auto mkL=[&](const char*t,int y){
        HWND h=CreateWindowExW(0,L"STATIC",toW(t).c_str(),WS_CHILD|WS_VISIBLE|SS_LEFT,10,y,85,20,hD,nullptr,GetModuleHandle(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,(WPARAM)g_hUi,TRUE);};
    auto mkE=[&](const std::string&v,int y,HMENU id)->HWND{
        HWND h=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",toW(v).c_str(),WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,100,y-2,316,22,hD,id,GetModuleHandle(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,(WPARAM)g_hUi,TRUE);return h;};
    mkL("Title:",15);    HWND hT =mkE(readMeta("@title "),15,(HMENU)101);
    mkL("Author:",45);   HWND hA =mkE(readMeta("@author "),45,(HMENU)102);
    mkL("Description:",75);HWND hD2=mkE(readMeta("@description "),75,(HMENU)103);
    auto mkB=[&](const char*t,int x,HMENU id){
        HWND h=CreateWindowExW(0,L"BUTTON",toW(t).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,115,90,26,hD,id,GetModuleHandle(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,(WPARAM)g_hUi,TRUE);};
    mkB("Apply",155,(HMENU)IDOK);mkB("Cancel",255,(HMENU)IDCANCEL);
    ShowWindow(hD,SW_SHOW);EnableWindow(g_hwnd,FALSE);

    auto gv=[&](HWND h)->std::string{
        int n=GetWindowTextLengthW(h)+1;std::wstring w(n,0);GetWindowTextW(h,&w[0],n);
        if(!w.empty()&&w.back()==0)w.pop_back();return fromW(w);};
    MSG msg;bool done=false;
    while(!done&&GetMessageW(&msg,nullptr,0,0)){
        if(msg.message==WM_QUIT){PostQuitMessage(0);break;}
        if((msg.hwnd==hD||IsChild(hD,msg.hwnd))&&msg.message==WM_COMMAND){
            if(LOWORD(msg.wParam)==IDOK){
                std::string title=gv(hT),author=gv(hA),desc=gv(hD2);
                std::istringstream ss(srcClean);std::string line,body;
                while(std::getline(ss,line)){
                    std::string s=trim(line);
                    if(s.substr(0,7)=="@title "||s.substr(0,8)=="@author "||s.substr(0,13)=="@description ")continue;
                    body+=line+"\n";}
                while(!body.empty()&&(body[0]=='\n'||body[0]=='\r'))body.erase(0,1);
                std::string hdr;
                if(!title.empty())  hdr+="@title "+title+"\n";
                if(!author.empty()) hdr+="@author "+author+"\n";
                if(!desc.empty())   hdr+="@description "+desc+"\n";
                if(!hdr.empty())    hdr+="\n";
                setEditorText(hdr+body);reparseOutline();colorize();updateTitle();done=true;
            }else if(LOWORD(msg.wParam)==IDCANCEL)done=true;
        }
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE)done=true;
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    EnableWindow(g_hwnd,TRUE);DestroyWindow(hD);SetForegroundWindow(g_hwnd);}

// ── Layout ────────────────────────────────────────────────────────────────────
static const int TB_H=44,SB_H=22,OUTLINE_W=264;
static void doLayout(){
    RECT rc;GetClientRect(g_hwnd,&rc);int W=rc.right,H=rc.bottom;
    if(g_hList)   MoveWindow(g_hList,   0,        TB_H,  OUTLINE_W, H-TB_H-SB_H, TRUE);
    if(g_hEditor) MoveWindow(g_hEditor, OUTLINE_W+1, TB_H, W-OUTLINE_W-1, H-TB_H-SB_H, TRUE);
    if(g_hStatus) MoveWindow(g_hStatus, 0,        H-SB_H,W,         SB_H,        TRUE);}

// ── Add menu ──────────────────────────────────────────────────────────────────
static void showAddMenu(HWND hWnd){
    HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING|MF_GRAYED,0,L"─── Structura elements ───");
    for(int i=0;i<STRUCT_COUNT;i++) AppendMenuW(m,MF_STRING,ID_STRUCT_BASE+i,STRUCT_SNIPS[i].label);
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING|MF_GRAYED,0,L"─── Syntaxia tags ───");
    for(int i=0;i<SYN_COUNT;i++)    AppendMenuW(m,MF_STRING,ID_SYN_BASE+i,SYN_SNIPS[i].label);
    // Position below toolbar
    POINT pt={6+(56+4)*4+8+(56+4)*2+8,TB_H};ClientToScreen(hWnd,&pt);
    TrackPopupMenu(m,TPM_LEFTALIGN|TPM_TOPALIGN,pt.x,pt.y,0,hWnd,nullptr);
    DestroyMenu(m);}

// ── WndProc ───────────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hWnd,UINT msg,WPARAM wP,LPARAM lP){
    switch(msg){
    case WM_CREATE:{
        g_hUi =CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        g_hMono=CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");

        // Toolbar
        int bx=6;
        auto tb=[&](const char*t,HMENU id,int w){
            HWND h=CreateWindowExW(0,L"BUTTON",toW(t).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,bx,9,w,26,hWnd,id,GetModuleHandle(nullptr),nullptr);
            SendMessageW(h,WM_SETFONT,(WPARAM)g_hUi,TRUE);bx+=w+4;};
        tb("New",   (HMENU)ID_NEW,   52); tb("Open",(HMENU)ID_OPEN,  52);
        tb("Save",  (HMENU)ID_SAVE,  52); tb("Export",(HMENU)ID_EXPORT,64);
        bx+=8; tb("Undo",(HMENU)ID_UNDO,52); tb("Redo",(HMENU)ID_REDO,52);
        bx+=8; tb("Add ▼",(HMENU)ID_ADD_OPEN,76); tb("Parse",(HMENU)ID_PARSE,56);
        bx+=8; tb("Meta",(HMENU)ID_META,52);

        // Outline list
        g_hList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",nullptr,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,
            0,0,0,0,hWnd,(HMENU)IDC_LIST,GetModuleHandle(nullptr),nullptr);
        SendMessageW(g_hList,WM_SETFONT,(WPARAM)g_hUi,TRUE);

        // RichEdit source editor
        g_hEditor=CreateWindowExW(WS_EX_CLIENTEDGE,g_richClass,nullptr,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_WANTRETURN|ES_NOHIDESEL,
            0,0,0,0,hWnd,(HMENU)IDC_EDITOR,GetModuleHandle(nullptr),nullptr);
        SendMessageW(g_hEditor,WM_SETFONT,(WPARAM)g_hMono,TRUE);
        SendMessageW(g_hEditor,EM_SETLIMITTEXT,0,0);
        // Default font via CHARFORMAT so RichEdit respects it
        {CHARFORMAT2W cf={};cf.cbSize=sizeof(cf);cf.dwMask=CFM_FACE|CFM_SIZE|CFM_CHARSET;
         cf.yHeight=200;cf.bCharSet=DEFAULT_CHARSET;wcscpy(cf.szFaceName,L"Consolas");
         SendMessageW(g_hEditor,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&cf);}
        // Event mask: notify on change + selection change
        SendMessageW(g_hEditor,EM_SETEVENTMASK,0,ENM_CHANGE|ENM_SELCHANGE);
        // Subclass for Tab
        g_origEdProc=(WNDPROC)SetWindowLongPtrW(g_hEditor,GWLP_WNDPROC,(LONG_PTR)EditorSubProc);
        // Status bar
        g_hStatus=CreateWindowExW(0,STATUSCLASSNAMEW,nullptr,
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,hWnd,(HMENU)IDC_STATUS,GetModuleHandle(nullptr),nullptr);
        // Starter content
        setEditorText("@title Untitled\n@author \n\n# New Document\n\nEnter text here.\n");
        reparseOutline();colorize();doLayout();updateStatus();
        break;}
    case WM_SIZE: doLayout(); break;
    case WM_TIMER:
        if(wP==TIMER_COLORIZE){KillTimer(hWnd,TIMER_COLORIZE);colorize();}
        if(wP==TIMER_OUTLINE) {KillTimer(hWnd,TIMER_OUTLINE); reparseOutline();}
        break;
    case WM_COMMAND:{
        int id=LOWORD(wP),notif=HIWORD(wP);
        if(id==IDC_LIST&&notif==LBN_SELCHANGE){
            navigateTo((int)SendMessageW(g_hList,LB_GETCURSEL,0,0));break;}
        if(id==IDC_EDITOR&&notif==EN_CHANGE){
            KillTimer(hWnd,TIMER_COLORIZE);SetTimer(hWnd,TIMER_COLORIZE,280,nullptr);
            KillTimer(hWnd,TIMER_OUTLINE); SetTimer(hWnd,TIMER_OUTLINE, 200,nullptr);
            updateTitle();break;}
        if(id==IDC_EDITOR&&notif==EN_SELCHANGE){updateStatus();break;}
        if(id==ID_NEW){if(!checkDirty())break;g_path.clear();
            setEditorText("@title Untitled\n@author \n\n# New Document\n\nEnter text here.\n");
            reparseOutline();colorize();updateTitle();updateStatus();break;}
        if(id==ID_OPEN){if(!checkDirty())break;std::string p;
            if(fileDlg(false,p)){g_path=p;doOpen(p);}break;}
        if(id==ID_SAVE){if(g_path.empty()&&!fileDlg(true,g_path))break;doSave(g_path);break;}
        if(id==ID_EXPORT){std::string p;if(fileDlg(true,p))doSave(p);break;}
        if(id==ID_UNDO){SendMessageW(g_hEditor,EM_UNDO,0,0);SetFocus(g_hEditor);break;}
        if(id==ID_REDO){/* RichEdit20W supports redo via Ctrl+Y natively when focused */
            SendMessageW(g_hEditor,WM_KEYDOWN,VK_CONTROL,0);
            PostMessageW(g_hEditor,WM_KEYDOWN,'Y',0);SetFocus(g_hEditor);break;}
        if(id==ID_PARSE){reparseOutline();colorize();SetFocus(g_hEditor);break;}
        if(id==ID_META){showMetaDialog();break;}
        if(id==ID_ADD_OPEN){showAddMenu(hWnd);break;}
        if(id>=ID_STRUCT_BASE&&id<ID_STRUCT_BASE+STRUCT_COUNT){
            insertSnippet(STRUCT_SNIPS[id-ID_STRUCT_BASE].text);reparseOutline();break;}
        if(id>=ID_SYN_BASE&&id<ID_SYN_BASE+SYN_COUNT){
            insertSnippet(SYN_SNIPS[id-ID_SYN_BASE].text);reparseOutline();break;}
        break;}
    case WM_DROPFILES:{
        HDROP h=(HDROP)wP;wchar_t buf[MAX_PATH]={};
        if(DragQueryFileW(h,0,buf,MAX_PATH)&&checkDirty()){
            g_path=fromW(buf);doOpen(g_path);}
        DragFinish(h);break;}
    case WM_CLOSE:if(!checkDirty())break;DestroyWindow(hWnd);break;
    case WM_DESTROY:
        if(g_hRichDll)FreeLibrary(g_hRichDll);
        if(g_hUi)DeleteObject(g_hUi);if(g_hMono)DeleteObject(g_hMono);
        PostQuitMessage(0);break;
    }
    return DefWindowProcW(hWnd,msg,wP,lP);}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR lpCmd,int nShow){
    loadRichEdit();
    InitCommonControls();
    WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_3DFACE+1);wc.lpszClassName=L"StructuraMaker";
    wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);

    g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,L"StructuraMaker",L"Structura Maker",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1300,820,nullptr,nullptr,hInst,nullptr);

    if(lpCmd&&lpCmd[0]){
        std::string arg(lpCmd);
        if(!arg.empty()&&arg.front()=='"'){arg=arg.substr(1);if(arg.back()=='"')arg.pop_back();}
        if(!arg.empty()){g_path=arg;doOpen(arg);}}

    ShowWindow(g_hwnd,nShow);UpdateWindow(g_hwnd);SetFocus(g_hEditor);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)){
        // Pass keyboard input directly to the editor for full native handling
        if(msg.hwnd==g_hEditor||IsChild(g_hEditor,msg.hwnd)){
            TranslateMessage(&msg);DispatchMessageW(&msg);continue;}
        TranslateMessage(&msg);DispatchMessageW(&msg);}
    return(int)msg.wParam;}
