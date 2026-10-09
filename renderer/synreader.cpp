/*
 * SynReader.exe — Syntaxia Visual Reader (interactive)
 * Compile:
 *   g++ -std=c++14 -mwindows -O2 -static-libgcc -static-libstdc++ \
 *       -o SynReader.exe synreader.cpp \
 *       -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32
 */
#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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
#define IDC_SIDEBAR     1005
#define ID_SIDEBAR_BASE 2000  // 3 slots per widget: label, ctrl, vallbl

// ── String helpers ────────────────────────────────────────────────────────────
static std::wstring toW(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);
    std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&w[0],n);
    if (!w.empty()&&w.back()==0) w.pop_back(); return w;
}
static std::string fromW(const std::wstring &w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,&s[0],n,nullptr,nullptr);
    if (!s.empty()&&s.back()==0) s.pop_back(); return s;
}
static std::string trim(const std::string &s) {
    size_t a=s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) return "";
    size_t b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);
}
static COLORREF hexToCol(const std::string &s) {
    std::string h=s; if (!h.empty()&&h[0]=='#') h=h.substr(1);
    if (h.size()<6) return 0;
    unsigned long v=strtoul(h.c_str(),nullptr,16);
    return RGB((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF);
}
static std::string getWndStr(HWND h) {
    int n=GetWindowTextLengthW(h)+1; std::wstring w(n,0);
    GetWindowTextW(h,&w[0],n); if (!w.empty()&&w.back()==0) w.pop_back();
    return fromW(w);
}
static std::string fmtVal(double v, const std::string &spec) {
    // spec is like ".2f", "d", "g", ".0f", etc. (no leading %)
    char buf[128]={};
    if (spec.empty()||spec=="g") { snprintf(buf,sizeof(buf),"%.6g",v); return buf; }
    bool isInt=(!spec.empty()&&(spec.back()=='d'||spec.back()=='i'));
    std::string fmt="%"+spec;
    if (isInt) snprintf(buf,sizeof(buf),fmt.c_str(),(long long)(v));
    else        snprintf(buf,sizeof(buf),fmt.c_str(),v);
    return buf;
}

// ── Reactive variable store ───────────────────────────────────────────────────
static std::map<std::string,double> g_vars;
static std::vector<std::pair<std::string,std::string>> g_derives;

// ── Expression evaluator (full recursive descent) ─────────────────────────────
struct Lexer {
    const std::string &src; size_t pos;
    Lexer(const std::string &s):src(s),pos(0){}
    void skipWS(){ while(pos<src.size()&&(src[pos]==' '||src[pos]=='\t')) pos++; }
    char peek(){ skipWS(); return pos<src.size()?src[pos]:'\0'; }
    char peek2(){ skipWS(); return pos+1<src.size()?src[pos+1]:'\0'; }
    char get(){ skipWS(); return pos<src.size()?src[pos++]:'\0'; }
    bool eof(){ skipWS(); return pos>=src.size(); }
    bool matchKw(const char *kw) {
        skipWS(); size_t n=strlen(kw);
        if (pos+n>src.size()) return false;
        if (src.substr(pos,n)!=kw) return false;
        if (pos+n<src.size()&&(isalnum((unsigned char)src[pos+n])||src[pos+n]=='_')) return false;
        pos+=n; return true;
    }
    bool matchOp(const char *op) {
        skipWS(); size_t n=strlen(op);
        if (pos+n>src.size()||src.substr(pos,n)!=op) return false;
        pos+=n; return true;
    }
    double readNum() {
        skipWS(); double v=0; bool frac=false; double sc=0.1;
        while(pos<src.size()&&(isdigit((unsigned char)src[pos])||src[pos]=='.')) {
            if(src[pos]=='.'){ frac=true; pos++; continue; }
            if(!frac) v=v*10+(src[pos]-'0'); else{v+=sc*(src[pos]-'0');sc*=0.1;} pos++;
        }
        if(pos<src.size()&&(src[pos]=='e'||src[pos]=='E')){
            pos++; int sg=1;
            if(pos<src.size()&&src[pos]=='-'){sg=-1;pos++;}else if(pos<src.size()&&src[pos]=='+') pos++;
            int ex=0; while(pos<src.size()&&isdigit((unsigned char)src[pos])) ex=ex*10+(src[pos++]-'0');
            v*=std::pow(10.0,sg*ex);
        }
        return v;
    }
    std::string readId() {
        skipWS(); std::string id;
        while(pos<src.size()&&(isalnum((unsigned char)src[pos])||src[pos]=='_')) id+=src[pos++];
        return id;
    }
    void skipString(char q) { // skip past closing quote
        pos++; // skip opening
        while(pos<src.size()&&src[pos]!=q) { if(src[pos]=='\\') pos++; pos++; }
        if(pos<src.size()) pos++; // skip closing
    }
};

static double parseTernary(Lexer &lx);

static double parseAtom(Lexer &lx) {
    char c=lx.peek();
    if (c=='(') { lx.get(); double v=parseTernary(lx); if(lx.peek()==')') lx.get(); return v; }
    if (c=='"'||c=='\'') { lx.skipString(c); return 0.0; }  // string literal → 0
    // Unicode π  (UTF-8: 0xCF 0x80)
    if ((unsigned char)c==0xCF && lx.pos+1<lx.src.size() && (unsigned char)lx.src[lx.pos+1]==0x80)
        { lx.pos+=2; return M_PI; }
    // Unicode √  (UTF-8: 0xE2 0x88 0x9A)
    if ((unsigned char)c==0xE2 && lx.pos+2<lx.src.size() &&
        (unsigned char)lx.src[lx.pos+1]==0x88 && (unsigned char)lx.src[lx.pos+2]==0x9A) {
        lx.pos+=3;
        double a; if(lx.peek()=='('){lx.get();a=parseTernary(lx);if(lx.peek()==')') lx.get();}
        else a=parseAtom(lx); return std::sqrt(std::max(0.0,a));
    }
    if (isdigit((unsigned char)c)||c=='.') return lx.readNum();
    if (isalpha((unsigned char)c)||c=='_') {
        std::string id=lx.readId();
        if (lx.peek()=='(') { // function call
            lx.get();
            double a=parseTernary(lx), b=a;
            if (lx.peek()==','){lx.get(); b=parseTernary(lx);}
            if (lx.peek()==')') lx.get();
            if (id=="sqrt")  return std::sqrt(std::max(0.0,a));
            if (id=="sin")   return std::sin(a);
            if (id=="cos")   return std::cos(a);
            if (id=="tan")   return std::tan(a);
            if (id=="abs")   return std::abs(a);
            if (id=="log")   return std::log(a);
            if (id=="log10") return std::log10(a);
            if (id=="exp")   return std::exp(a);
            if (id=="floor") return std::floor(a);
            if (id=="ceil")  return std::ceil(a);
            if (id=="round") return std::round(a);
            if (id=="min")   return std::min(a,b);
            if (id=="max")   return std::max(a,b);
            if (id=="pow")   return std::pow(a,b);
            if (id=="int"||id=="float") return a;
            return 0.0;
        }
        if (id=="pi"||id=="PI") return M_PI;
        if (id=="true")  return 1.0;
        if (id=="false") return 0.0;
        auto it=g_vars.find(id); return it!=g_vars.end()?it->second:0.0;
    }
    return 0.0;
}

static double parsePow(Lexer &lx) {
    double b=parseAtom(lx);
    if(lx.peek()=='*'&&lx.peek2()=='*'){lx.pos+=2; return std::pow(b,parsePow(lx));}
    return b;
}
static double parseUnary(Lexer &lx) {
    if(lx.peek()=='-'){lx.get();return -parsePow(lx);}
    if(lx.peek()=='+'){lx.get();return  parsePow(lx);}
    if(lx.peek()=='!'){lx.get();return parsePow(lx)!=0?0.0:1.0;}
    return parsePow(lx);
}
static double parseMul(Lexer &lx) {
    double v=parseUnary(lx);
    for(;;){
        char c=lx.peek();
        if(c=='*'&&lx.peek2()!='*'){lx.get();v*=parseUnary(lx);}
        else if(c=='/'){lx.get();double d=parseUnary(lx);v=d!=0?v/d:0;}
        else if(c=='%'){lx.get();double d=parseUnary(lx);v=d!=0?std::fmod(v,d):0;}
        else break;
    }
    return v;
}
static double parseAdd(Lexer &lx) {
    double v=parseMul(lx);
    for(;;){
        char c=lx.peek();
        if(c=='+')  {lx.get();v+=parseMul(lx);}
        else if(c=='-'){lx.get();v-=parseMul(lx);}
        else break;
    }
    return v;
}
static double parseCompare(Lexer &lx) {
    double v=parseAdd(lx); lx.skipWS();
    char c=lx.peek(), c2=lx.peek2();
    if(c=='<'&&c2=='='){lx.pos+=2;return v<=parseAdd(lx)?1:0;}
    if(c=='>'&&c2=='='){lx.pos+=2;return v>=parseAdd(lx)?1:0;}
    if(c=='='&&c2=='='){lx.pos+=2;return v==parseAdd(lx)?1:0;}
    if(c=='!'&&c2=='='){lx.pos+=2;return v!=parseAdd(lx)?1:0;}
    if(c=='<'&&c2!='<'){lx.pos++;  return v< parseAdd(lx)?1:0;}
    if(c=='>'&&c2!='>'){lx.pos++;  return v> parseAdd(lx)?1:0;}
    return v;
}
static double parseAnd(Lexer &lx) {
    double v=parseCompare(lx);
    while(lx.peek()=='&'&&lx.peek2()=='&'){
        lx.pos+=2; double r=parseCompare(lx); v=(v!=0&&r!=0)?1:0;
    }
    return v;
}
static double parseOr(Lexer &lx) {
    double v=parseAnd(lx);
    while(lx.peek()=='|'&&lx.peek2()=='|'){
        lx.pos+=2; double r=parseAnd(lx); v=(v!=0||r!=0)?1:0;
    }
    return v;
}
static double parseTernary(Lexer &lx) {
    double b=parseOr(lx); lx.skipWS();
    // C-style:  a ? b : c
    if(lx.peek()=='?'){
        lx.get();
        double tv=parseTernary(lx); lx.skipWS();
        if(lx.peek()==':') lx.get();
        double ev=parseTernary(lx);
        return b!=0?tv:ev;
    }
    // Python-style:  b if a else c
    if(lx.matchKw("if")){
        double a=parseOr(lx);
        double ev=0;
        if(lx.matchKw("else")) ev=parseTernary(lx);
        return a!=0?b:ev;
    }
    return b;
}

static double evalExpr(const std::string &expr) {
    try { Lexer lx(expr); return parseTernary(lx); }
    catch(...){ return 0.0; }
}

// ── Evaluate all @derive expressions ─────────────────────────────────────────
static void reDeriveAll() {
    for (auto &kv:g_derives) g_vars[kv.first]=evalExpr(kv.second);
}

// ── Template substitution: {{expr:fmt}} → computed value ─────────────────────
static std::string evalTemplate(const std::string &tmpl) {
    std::string out; size_t i=0;
    while(i<tmpl.size()){
        if(i+1<tmpl.size()&&tmpl[i]=='{'&&tmpl[i+1]=='{'){
            size_t end=tmpl.find("}}",i+2);
            if(end!=std::string::npos){
                std::string inner=tmpl.substr(i+2,end-i-2);
                std::string expr=inner,fmt="g"; auto col=inner.find(':');
                if(col!=std::string::npos){expr=inner.substr(0,col);fmt=inner.substr(col+1);}
                expr=trim(expr);
                // Determine if fmt has a printf spec char
                bool hasPrintf=false;
                for(char fc:fmt) if(fc=='f'||fc=='e'||fc=='g'||fc=='d'||fc=='i'){hasPrintf=true;break;}
                double val=evalExpr(expr);
                if(hasPrintf) out+=fmtVal(val,fmt);
                else{ char buf[64]; snprintf(buf,sizeof(buf),"%.6g",val); out+=buf;
                      if(!fmt.empty()&&fmt!="g"){out+=" ";out+=fmt;}}
                i=end+2; continue;
            }
        }
        if(i+1<tmpl.size()&&tmpl[i]=='*'&&tmpl[i+1]=='*'){i+=2;continue;}
        out+=tmpl[i++];
    }
    return out;
}

// ── Data model ────────────────────────────────────────────────────────────────
struct PlotInfo  { std::string xVar,yExpr,label; double xMin=0,xMax=10; };
struct Box {
    float x,y,w,h;
    std::string text,tmpl;
    COLORREF fg=RGB(0,0,0),bg=RGB(255,255,255);
    bool hasBg=false; int size=12,align=0; bool isHead=false;
    bool isPlot=false;  PlotInfo plot;
    bool isTable=false; // text holds "h1 | h2\nr1c1 | r1c2\n..." template in tmpl
};
struct RPage { std::vector<Box> boxes; int pw=210,ph=297; };
struct Doc  { std::string title,author; int pw=210,ph=297; std::vector<RPage> pages; };

static const struct{const char*name;const char*tag;int w,h;}PSIZES[]={
    {"A4","a4page",210,297},{"A3","a3page",297,420},{"A5","a5page",148,210},
    {"Letter","letterpage",216,279},{"Legal","legalpage",216,356},{"Tabloid","tabloidpage",279,432}};

// ── Strss style map ───────────────────────────────────────────────────────────
struct StrssStyle { COLORREF fg=RGB(0,0,0);bool hasFg=false;int size=0;bool bold=false;bool hasBold=false;int align=-1; };
static std::map<std::string,StrssStyle> g_styles;

// ── Sidebar widget descriptors ────────────────────────────────────────────────
struct SideWidget {
    std::string kind;     // slider toggle input display button
    std::string label, varName, expr, setTarget, setExpr, inputType;
    double wMin=0,wMax=100,wStep=1;
    HWND hLbl=nullptr,hCtrl=nullptr,hVal=nullptr;
    int nyLbl=0,nyCtrl=0,nyVal=0; // natural Y (unscrolled)
};
static std::vector<SideWidget> g_sideWidgets;
static std::string g_srcText;
static int g_sidebarScrollY=0, g_sidebarContentH=0;

// ── KV parser ─────────────────────────────────────────────────────────────────
static void parseKV(const std::string &s, std::map<std::string,std::string>&out){
    std::istringstream ss(s); std::string tok;
    while(std::getline(ss,tok,',')){
        tok=trim(tok); auto eq=tok.find('='); if(eq==std::string::npos) continue;
        std::string k=trim(tok.substr(0,eq)),v=trim(tok.substr(eq+1));
        if(!v.empty()&&v.front()=='"') v=v.substr(1,v.size()-2); out[k]=v;
    }
}

// ── Pre-pass: extract vars/derives/widgets ────────────────────────────────────
static void parseVarPass(const std::string &src){
    g_vars.clear(); g_derives.clear(); g_sideWidgets.clear(); g_styles.clear();
    std::map<std::string,std::pair<std::string,std::map<std::string,std::string>>> widgetMap;

    // Sub-pass 1: collect widget lines
    {
        std::istringstream ps(src); std::string ln;
        while(std::getline(ps,ln)){
            std::string s=trim(ln); if(s.empty()||s[0]!='[') continue;
            std::string inner=trim(s.substr(1,s.size()-2));
            auto col=inner.find(':'); if(col==std::string::npos) continue;
            std::string kind=trim(inner.substr(0,col));
            std::string after=trim(inner.substr(col+1));
            auto c1=after.find(',');
            std::string first=trim(c1==std::string::npos?after:after.substr(0,c1));
            std::map<std::string,std::string> kv;
            if(c1!=std::string::npos) parseKV(after.substr(c1+1),kv);

            if(kind=="slider"||kind=="toggle"||kind=="input"){
                kv["_kind"]=kind; widgetMap[first]={kind,kv};
            } else if(kind=="display"){
                SideWidget sw; sw.kind="display"; sw.expr=first;
                sw.label=kv.count("label")?kv.at("label"):first;
                g_sideWidgets.push_back(sw);
            } else if(kind=="button"){
                SideWidget sw; sw.kind="button";
                // label= is either in kv or encoded as first= "label=X"
                if(kv.count("label")) sw.label=kv.at("label");
                else if(first.substr(0,6)=="label="){
                    sw.label=first.substr(6);
                    if(!sw.label.empty()&&sw.label.front()=='"') sw.label=sw.label.substr(1,sw.label.size()-2);
                } else sw.label=first;
                // "set var" key in kv
                for(auto &kp:kv){
                    if(kp.first.size()>=5&&kp.first.substr(0,4)=="set "){
                        sw.setTarget=trim(kp.first.substr(4)); sw.setExpr=kp.second; break;
                    }
                }
                // fallback: raw line scan
                if(sw.setTarget.empty()){
                    auto sp=s.find("set "); if(sp!=std::string::npos){
                        std::string rest=trim(s.substr(sp+4));
                        if(!rest.empty()&&rest.back()==']') rest.pop_back(); rest=trim(rest);
                        auto eq=rest.find('=');
                        if(eq!=std::string::npos){sw.setTarget=trim(rest.substr(0,eq));sw.setExpr=trim(rest.substr(eq+1));}
                    }
                }
                g_sideWidgets.push_back(sw);
            }
        }
    }

    // Sub-pass 2: @var @const @derive
    std::istringstream ss(src); std::string line;
    while(std::getline(ss,line)){
        std::string s=trim(line);
        auto cmt=s.find("--"); if(cmt!=std::string::npos) s=trim(s.substr(0,cmt));
        if(s.empty()) continue;
        if(s.substr(0,5)=="@var "&&s.find('=')!=std::string::npos){
            auto eq=s.find('=');
            std::string name=trim(s.substr(5,eq-5)), valStr=trim(s.substr(eq+1));
            double val=evalExpr(valStr); g_vars[name]=val;
            auto wit=widgetMap.find(name);
            if(wit!=widgetMap.end()){
                SideWidget sw; sw.kind=wit->second.first; sw.varName=name;
                auto &kv=wit->second.second;
                sw.label=kv.count("label")?kv.at("label"):name;
                sw.inputType=kv.count("type")?kv.at("type"):"number";
                if(sw.kind=="slider"){
                    sw.wMin =kv.count("min") ?std::stod(kv.at("min")) :0.0;
                    sw.wMax =kv.count("max") ?std::stod(kv.at("max")) :100.0;
                    sw.wStep=kv.count("step")?std::stod(kv.at("step")):1.0;
                }
                g_sideWidgets.push_back(sw);
            }
        } else if(s.substr(0,7)=="@const "&&s.find('=')!=std::string::npos){
            auto eq=s.find('=');
            g_vars[trim(s.substr(7,eq-7))]=evalExpr(trim(s.substr(eq+1)));
        } else if(s.substr(0,8)=="@derive "&&s.find('=')!=std::string::npos){
            auto eq=s.find('=');
            std::string name=trim(s.substr(8,eq-8)), expr=trim(s.substr(eq+1));
            g_derives.push_back({name,expr}); g_vars[name]=evalExpr(expr);
        } else if(s.substr(0,7)=="@strss "&&s.find(':')!=std::string::npos){
            auto co=s.find(':'); std::string nm=trim(s.substr(7,co-7));
            std::map<std::string,std::string>kv; parseKV(s.substr(co+1),kv);
            StrssStyle st;
            if(kv.count("color")){st.fg=hexToCol(kv["color"]);st.hasFg=true;}
            if(kv.count("size")&&!kv["size"].empty()) st.size=std::stoi(kv["size"]);
            if(kv.count("bold")){st.bold=(kv["bold"]=="1"||kv["bold"]=="true");st.hasBold=true;}
            if(kv.count("align")){st.align=(kv["align"]=="center"?1:kv["align"]=="right"?2:0);}
            g_styles[nm]=st;
        }
    }
    reDeriveAll();
}

// ── StructArch parser ─────────────────────────────────────────────────────────
static Doc parseStructArch(const std::string &src){
    Doc doc; std::istringstream ss(src); std::string line;
    int curPage=-1; Box curBox; bool inBox=false; std::string boxTxt;
    while(std::getline(ss,line)){
        std::string s=trim(line); if(s.empty()) continue;
        if(s.substr(0,7)=="@title ")   {doc.title =trim(s.substr(7)); continue;}
        if(s.substr(0,8)=="@author ")  {doc.author=trim(s.substr(8)); continue;}
        if(s.substr(0,10)=="@pagesize "){
            std::string nm=trim(s.substr(10));
            for(auto &ps:PSIZES) if(nm==ps.name){doc.pw=ps.w;doc.ph=ps.h;break;}
            continue;
        }
        if(s.substr(0,6)=="@page "){doc.pages.push_back(RPage());curPage=(int)doc.pages.size()-1;continue;}
        if(s=="@endpage"){curPage=-1;continue;}
        if(s.substr(0,9)=="@textbox "){
            inBox=true;boxTxt="";curBox={};
            std::istringstream ks(s.substr(9));std::string kv;
            while(ks>>kv){auto eq=kv.find('=');if(eq==std::string::npos)continue;
                std::string k=kv.substr(0,eq),v=kv.substr(eq+1);
                if(k=="x")curBox.x=std::stof(v);else if(k=="y")curBox.y=std::stof(v);
                else if(k=="w")curBox.w=std::stof(v);else if(k=="h")curBox.h=std::stof(v);
                else if(k=="color")curBox.fg=hexToCol(v);else if(k=="bgcolor")curBox.bg=hexToCol(v);
                else if(k=="hasbg")curBox.hasBg=(v=="1");else if(k=="size")curBox.size=std::stoi(v);
                else if(k=="align")curBox.align=std::stoi(v);}
            continue;
        }
        if(s=="@end"&&inBox){
            curBox.text=trim(boxTxt);
            if(curPage>=0&&curPage<(int)doc.pages.size()) doc.pages[curPage].boxes.push_back(curBox);
            inBox=false;continue;
        }
        if(inBox){if(!boxTxt.empty())boxTxt+="\n";boxTxt+=line;}
    }
    if(doc.pages.empty()) doc.pages.push_back(RPage());
    return doc;
}

// ── Syntaxia <tag> script renderer ───────────────────────────────────────────
static bool hasModernIndicators(const std::string &src){
    return src.find("@var ")!=std::string::npos||
           src.find("[slider:")!=std::string::npos||
           src.find("[toggle:")!=std::string::npos||
           src.find("[input:")!=std::string::npos||
           src.find("[button:")!=std::string::npos||
           src.find("[display:")!=std::string::npos||
           src.find("[plot:")!=std::string::npos||
           src.find("@derive ")!=std::string::npos||
           src.find("@strss ")!=std::string::npos;
}
static bool isSyntaxiaFormat(const std::string &src){
    if(hasModernIndicators(src)) return false;
    return src.find("<func ")!=std::string::npos||
           src.find("<func\n")!=std::string::npos||
           src.find("<func>") !=std::string::npos;
}
static Doc parseSyntaxia(const std::string &src){
    Doc doc;doc.pages.push_back(RPage());doc.title="Syntaxia Script";
    const float MX=20.f,MY=20.f,TW=doc.pw-MX*2,TH=doc.ph-MY*2;
    float curY=MY;int curPg=0;
    auto newPage=[&](){doc.pages.push_back(RPage());curPg++;curY=MY;};
    auto addBox=[&](const std::string&text,float h,int fsz,bool bold,COLORREF fg=RGB(30,30,30)){
        if(curY+h>MY+TH)newPage();
        Box b;b.x=MX;b.y=curY;b.w=TW;b.h=h;b.text=text;b.fg=fg;b.size=fsz;b.isHead=bold;
        doc.pages[curPg].boxes.push_back(b);curY+=h+1.5f;};
    addBox("Syntaxia Script",10.f,18,true,RGB(40,60,140));curY+=3.f;
    std::istringstream ss(src);std::string line;
    while(std::getline(ss,line)){
        std::string s=trim(line);if(s.empty()){curY+=2.f;continue;}
        COLORREF col=RGB(30,30,30);bool bold=false;int fsz=10;
        if(s.find("<func")==0){col=RGB(0,100,180);bold=true;fsz=11;}
        else if(s.find("</func")==0){col=RGB(0,100,180);bold=true;fsz=11;}
        else if(s.find("<var")==0||s.find("</var")==0)col=RGB(0,130,0);
        else if(s.find("<if")==0||s.find("</if")==0||s.find("<elif")==0||
                s.find("<else")==0||s.find("</else")==0){col=RGB(140,0,140);bold=(s[1]!='/');}
        else if(s.find("<while")==0||s.find("</while")==0||
                s.find("<for")==0||s.find("</for")==0){col=RGB(170,80,0);bold=(s[1]!='/');}
        else if(s.find("<print")==0)col=RGB(0,120,120);
        else if(s.find("<~>")==0)col=RGB(130,60,0);
        else if(s.find("<@>")==0)col=RGB(80,80,200);
        addBox(line,5.5f,fsz,bold,col);
    }
    if(doc.pages.size()>1&&doc.pages.back().boxes.empty()) doc.pages.pop_back();
    return doc;
}

// ── Modern Structura format parser ────────────────────────────────────────────
static Doc parseModern(const std::string &src){
    Doc doc;doc.pages.push_back(RPage());
    const float MX=20.f,MY=20.f;
    float TW=doc.pw-MX*2,TH=doc.ph-MY*2;
    float curY=MY;int curPg=0;
    auto newPage=[&](){
        RPage np;np.pw=doc.pages[curPg].pw;np.ph=doc.pages[curPg].ph;
        doc.pages.push_back(np);curPg++;curY=MY;
        TW=doc.pages[curPg].pw-MX*2;TH=doc.pages[curPg].ph-MY*2;};
    auto addBox=[&](const std::string&text,float h,int fsz,int align,bool bold,
                    COLORREF fg=RGB(0,0,0),const std::string&tmpl=""){
        if(curY+h>MY+TH)newPage();
        Box b;b.x=MX;b.y=curY;b.w=TW;b.h=h;b.text=text;b.tmpl=tmpl;
        b.fg=fg;b.size=fsz;b.align=align;b.isHead=bold;
        doc.pages[curPg].boxes.push_back(b);curY+=h+2.f;};

    std::istringstream ss(src);std::string line;
    bool inMath=false;std::string mathAcc;
    std::vector<std::string> tHdrs,tRows;bool inTable=false;

    auto stripMd=[](const std::string&s)->std::string{
        std::string o;for(size_t i=0;i<s.size();i++){
            if(i+1<s.size()&&s[i]=='*'&&s[i+1]=='*'){i++;continue;}
            o+=s[i];}return o;};

    auto flushTable=[&](){
        if(!inTable)return;
        std::string tmpl,txt;bool first=true;
        for(auto&h:tHdrs){if(!first){tmpl+=" | ";txt+=" | ";}
            tmpl+=h;txt+=evalTemplate(stripMd(h));first=false;}
        tmpl+="\n";txt+="\n";
        for(auto&r:tRows){tmpl+=r+"\n";txt+=evalTemplate(stripMd(r))+"\n";}
        float rowH=6.5f;
        float h=8.f+(float)tRows.size()*rowH;
        if(curY+h>MY+TH)newPage();
        Box b;b.x=MX;b.y=curY;b.w=TW;b.h=h;
        b.text=txt;b.tmpl=tmpl;b.isTable=true;b.size=10;
        doc.pages[curPg].boxes.push_back(b);curY+=h+3.f;
        inTable=false;tHdrs.clear();tRows.clear();};

    while(std::getline(ss,line)){
        std::string s=trim(line);
        if(inMath){
            if(s=="$$"){addBox(mathAcc,4.f+std::count(mathAcc.begin(),mathAcc.end(),'\n')*5.f,11,1,false,RGB(60,60,120));inMath=false;mathAcc="";}
            else{if(!mathAcc.empty())mathAcc+="\n";mathAcc+=s;}
            continue;
        }
        if(inTable&&(s.empty()||s[0]!='|'))flushTable();
        if(s.empty()){curY+=3.f;continue;}

        // Tags: <a4page> etc. are page switches; all other <tags> are silently skipped
        if(!s.empty()&&s[0]=='<'){
            bool pgFound=false;
            for(auto&ps:PSIZES){
                std::string otag=std::string("<")+ps.tag+">";
                std::string ctag=std::string("</")+ps.tag+">";
                if(s==otag){
                    flushTable();
                    if(!doc.pages[curPg].boxes.empty()){
                        RPage np;np.pw=ps.w;np.ph=ps.h;doc.pages.push_back(np);curPg++;
                    } else {doc.pages[curPg].pw=ps.w;doc.pages[curPg].ph=ps.h;}
                    curY=MY;TW=doc.pages[curPg].pw-MX*2;TH=doc.pages[curPg].ph-MY*2;
                    pgFound=true;break;
                }
                if(s==ctag){pgFound=true;break;}
            }
            if(pgFound) continue;
            // Any other <tag ...> line: skip silently (Syntaxia tags in mixed files)
            if(s.back()=='>'||s.find('<')!=std::string::npos) continue;
        }

        if(s.substr(0,7)=="@title "){doc.title=trim(s.substr(7));addBox(doc.title,12.f,22,1,true);continue;}
        if(s.substr(0,8)=="@author "){doc.author=trim(s.substr(8));if(!doc.author.empty())addBox(doc.author,7.f,11,1,false,RGB(80,80,80));continue;}
        if(s.substr(0,13)=="@description "){addBox(trim(s.substr(13)),7.f,10,1,false,RGB(100,100,100));continue;}
        if(s.substr(0,10)=="@pagesize "){std::string nm=trim(s.substr(10));for(auto&ps:PSIZES)if(nm==ps.name){doc.pages[curPg].pw=ps.w;doc.pages[curPg].ph=ps.h;TW=ps.w-MX*2;TH=ps.h-MY*2;break;}continue;}
        if(s.substr(0,7)=="@strss "&&s.find(':')!=std::string::npos){
            auto co=s.find(':');std::string nm=trim(s.substr(7,co-7));
            std::map<std::string,std::string>kv;parseKV(s.substr(co+1),kv);
            StrssStyle st;
            if(kv.count("color")){st.fg=hexToCol(kv["color"]);st.hasFg=true;}
            if(kv.count("size")&&!kv["size"].empty())st.size=std::stoi(kv["size"]);
            if(kv.count("bold")){st.bold=(kv["bold"]=="1"||kv["bold"]=="true");st.hasBold=true;}
            if(kv.count("align")){st.align=(kv["align"]=="center"?1:kv["align"]=="right"?2:0);}
            g_styles[nm]=st;continue;}
        if(!s.empty()&&s[0]=='@')continue;

        if(!s.empty()&&s[0]=='#'){
            size_t n=0;while(n<s.size()&&s[n]=='#')n++;
            std::string txt=trim(s.substr(n));
            float sz[]={16.f,13.f,11.f},ht[]={12.f,10.f,8.f};
            int lv=(int)std::min(n,(size_t)3)-1;
            // {.classname} style prefix
            COLORREF hFg=RGB(0,0,0);int hFsz=(int)sz[lv];int hAlign=0;
            if(txt.size()>3&&txt[0]=='{'&&txt[1]=='.'){auto end2=txt.find('}');
                if(end2!=std::string::npos){std::string cls=txt.substr(2,end2-2);txt=trim(txt.substr(end2+1));
                    auto it=g_styles.find(cls);if(it!=g_styles.end()){auto&st=it->second;
                        if(st.hasFg)hFg=st.fg;if(st.size>0)hFsz=st.size;if(st.align>=0)hAlign=st.align;}}}
            curY+=4.f;addBox(txt,ht[lv],hFsz,hAlign,true,hFg);continue;
        }
        if(s=="---"){curY+=2.f;addBox("",1.f,10,0,false,RGB(180,180,180));curY+=2.f;continue;}
        if(s=="$$"){inMath=true;continue;}

        if(!s.empty()&&s[0]=='|'){
            bool isSep=true;for(char c:s)if(c!='|'&&c!='-'&&c!=' '&&c!=':'){isSep=false;break;}
            if(isSep){inTable=true;continue;}
            std::vector<std::string> cells;
            std::istringstream rs(s.substr(1,s.size()-2));std::string cell;
            while(std::getline(rs,cell,'|'))cells.push_back(trim(cell));
            std::string row;for(size_t i=0;i<cells.size();i++){if(i)row+=" | ";row+=cells[i];}
            if(!inTable){tHdrs=cells;inTable=true;}else tRows.push_back(row);
            continue;
        }

        if(!s.empty()&&s[0]=='['){
            std::string inner2=trim(s.substr(1,s.size()-2));
            auto col2=inner2.find(':');if(col2==std::string::npos){continue;}
            std::string kind=trim(inner2.substr(0,col2));
            std::string after=trim(inner2.substr(col2+1));
            auto c1=after.find(',');
            std::string first=trim(c1==std::string::npos?after:after.substr(0,c1));
            std::map<std::string,std::string>kv;
            if(c1!=std::string::npos)parseKV(after.substr(c1+1),kv);

            if(kind=="display"){
                std::string lbl=kv.count("label")?kv.at("label"):first;
                std::string tmplStr=lbl+":  {{"+first+"}}";
                addBox(lbl+":  "+fmtVal(evalExpr(first),"g"),6.f,11,0,false,RGB(0,80,160),tmplStr);
            } else if(kind=="plot"){
                // Render as inline chart
                std::string yexpr;
                if(c1!=std::string::npos){
                    auto c2=after.find(',',c1+1);
                    yexpr=trim(c2==std::string::npos?after.substr(c1+1):after.substr(c1+1,c2-c1-1));
                    if(yexpr.find('=')!=std::string::npos) yexpr="";
                }
                if(yexpr.empty()) yexpr=first;
                Box b;b.x=MX;b.y=curY;b.w=TW;b.h=60.f;
                b.isPlot=true;
                b.plot.xVar=first;b.plot.yExpr=yexpr;
                b.plot.xMin=kv.count("x_min")?std::stod(kv.at("x_min")):0;
                b.plot.xMax=kv.count("x_max")?std::stod(kv.at("x_max")):10;
                b.plot.label=kv.count("label")?kv.at("label"):"Plot";
                if(curY+60.f>MY+TH)newPage();
                doc.pages[curPg].boxes.push_back(b);curY+=62.f;
            }
            continue;
        }

        // Paragraph text — estimate height so text doesn't clip
        {COLORREF paraFg=RGB(0,0,0);int paraFsz=11;int paraAlign=0;bool paraBold=false;
        std::string ps2=s;
        if(ps2.size()>3&&ps2[0]=='{'&&ps2[1]=='.'){auto end2=ps2.find('}');
            if(end2!=std::string::npos){std::string cls=ps2.substr(2,end2-2);ps2=trim(ps2.substr(end2+1));
                auto it=g_styles.find(cls);if(it!=g_styles.end()){auto&st=it->second;
                    if(st.hasFg)paraFg=st.fg;if(st.size>0)paraFsz=st.size;
                    if(st.hasBold)paraBold=st.bold;if(st.align>=0)paraAlign=st.align;}}}
        bool hasTmpl=ps2.find("{{")==std::string::npos?false:true;
        std::string plain=stripMd(ps2);
        std::string rendered=hasTmpl?evalTemplate(plain):plain;
        float charWmm=(float)paraFsz/72.f*25.4f*0.55f;
        int cpl=std::max(1,(int)(TW/charWmm));
        int lines=std::max(1,(int)std::ceil((float)rendered.size()/(float)cpl));
        float lineH=(float)paraFsz/72.f*25.4f*1.55f;
        float pph=lines*lineH+0.5f;
        addBox(rendered,pph,paraFsz,paraAlign,paraBold,paraFg,hasTmpl?plain:"");}
    }
    flushTable();
    if(doc.pages.size()>1&&doc.pages.back().boxes.empty()) doc.pages.pop_back();
    return doc;
}

static Doc parseSyn(const std::string &src){
    if(src.find("@page ")!=std::string::npos||src.find("@textbox ")!=std::string::npos)
        return parseStructArch(src);
    if(isSyntaxiaFormat(src)) return parseSyntaxia(src);
    return parseModern(src);
}

// ── Globals ───────────────────────────────────────────────────────────────────
static Doc   g_doc;
static float g_zoom=1.0f;
static int   g_scrollX=0,g_scrollY=0,g_focusPage=0;
static HWND  g_hwnd,g_hCanvas,g_hSidebar;
static HWND  g_hPrev,g_hNext,g_hPageLbl,g_hStatus;
static HFONT g_font,g_fontBold;
static std::string g_filePath;

static float ppm()         {return 3.0f*g_zoom;}
static int   mm2px(float m){return (int)(m*ppm());}
static int   canvasPageX(HWND hc){RECT rc;GetClientRect(hc,&rc);return(rc.right-mm2px((float)g_doc.pw))/2-g_scrollX;}
static int   canvasPageY(int i)  {int y=24-g_scrollY;for(int j=0;j<i;j++)y+=mm2px((float)(j<(int)g_doc.pages.size()?g_doc.pages[j].ph:g_doc.ph))+24;return y;}
static int   pageScrollY(int pi) {int y=0;for(int j=0;j<pi&&j<(int)g_doc.pages.size();j++)y+=mm2px((float)g_doc.pages[j].ph)+24;return y;}

static void reRender(){reDeriveAll();if(!g_srcText.empty())g_doc=parseSyn(g_srcText);InvalidateRect(g_hCanvas,nullptr,FALSE);}

// ── Sidebar scroll helper ────────────────────────────────────────────────────
static void updateSidebarScroll(){
    RECT rc;GetClientRect(g_hSidebar,&rc);int visH=rc.bottom-rc.top;
    int maxSc=std::max(0,g_sidebarContentH-visH);
    if(g_sidebarScrollY>maxSc) g_sidebarScrollY=maxSc;
    SCROLLINFO si={sizeof(si),SIF_ALL,(UINT)0,(UINT)g_sidebarContentH,(UINT)visH,(UINT)g_sidebarScrollY,0};
    SetScrollInfo(g_hSidebar,SB_VERT,&si,TRUE);
}
static void repositionSidebarControls(){
    for(auto&sw:g_sideWidgets){
        if(sw.hLbl)  SetWindowPos(sw.hLbl, nullptr,4,sw.nyLbl -g_sidebarScrollY,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(sw.hCtrl) SetWindowPos(sw.hCtrl,nullptr,4,sw.nyCtrl-g_sidebarScrollY,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(sw.hVal)  SetWindowPos(sw.hVal, nullptr,4,sw.nyVal -g_sidebarScrollY,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    InvalidateRect(g_hSidebar,nullptr,TRUE);
}

// ── Sidebar: update display/value labels ─────────────────────────────────────
static void updateSidebarValues(){
    for(auto&sw:g_sideWidgets){
        if(!sw.hVal) continue;
        double v=0;
        if(sw.kind=="slider"||sw.kind=="toggle"||sw.kind=="input"){
            auto it=g_vars.find(sw.varName);v=it!=g_vars.end()?it->second:0;
        }else if(sw.kind=="display"){v=evalExpr(sw.expr);}
        char buf[64];snprintf(buf,sizeof(buf),"%.6g",v);
        SetWindowTextW(sw.hVal,toW(buf).c_str());
    }
}

// ── Sidebar: build controls from scratch ─────────────────────────────────────
static void rebuildSidebar(){
    if(!g_hSidebar) return;
    g_sidebarScrollY=0;
    // Destroy old controls
    for(auto&sw:g_sideWidgets){
        if(sw.hLbl){DestroyWindow(sw.hLbl);sw.hLbl=nullptr;}
        if(sw.hCtrl){DestroyWindow(sw.hCtrl);sw.hCtrl=nullptr;}
        if(sw.hVal){DestroyWindow(sw.hVal);sw.hVal=nullptr;}
    }
    RECT rc;GetClientRect(g_hSidebar,&rc);int W=rc.right-rc.left;
    auto mkS=[&](LPCWSTR cls,LPCWSTR txt,DWORD st,int y,int h,HMENU id)->HWND{
        HWND hw=CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|st,4,y,W-8,h,
                                g_hSidebar,id,GetModuleHandle(nullptr),nullptr);
        SendMessageW(hw,WM_SETFONT,(WPARAM)g_font,TRUE);return hw;};

    int y=6;
    // Header
    {HWND h=mkS(L"STATIC",L"Variables",SS_CENTER,y,18,nullptr);
     SendMessageW(h,WM_SETFONT,(WPARAM)g_fontBold,TRUE);y+=24;}

    for(int i=0;i<(int)g_sideWidgets.size();i++){
        auto&sw=g_sideWidgets[i];
        int base=ID_SIDEBAR_BASE+i*3;
        sw.nyLbl=y;
        sw.hLbl=mkS(L"STATIC",toW(sw.label).c_str(),SS_LEFT,y,16,(HMENU)(size_t)base);
        y+=18;

        if(sw.kind=="slider"){
            double range=sw.wMax-sw.wMin;
            int ticks=(sw.wStep>0&&range>0)?(int)(range/sw.wStep):100;
            ticks=std::max(1,std::min(ticks,10000));
            sw.nyCtrl=y;
            sw.hCtrl=CreateWindowExW(0,TRACKBAR_CLASSW,nullptr,
                WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,4,y,W-8,24,
                g_hSidebar,(HMENU)(size_t)(base+1),GetModuleHandle(nullptr),nullptr);
            SendMessageW(sw.hCtrl,TBM_SETRANGEMIN,FALSE,0);
            SendMessageW(sw.hCtrl,TBM_SETRANGEMAX,FALSE,ticks);
            auto it=g_vars.find(sw.varName);double cv=it!=g_vars.end()?it->second:sw.wMin;
            int pos=(int)((cv-sw.wMin)/(sw.wStep>0?sw.wStep:1));
            pos=std::max(0,std::min(pos,ticks));
            SendMessageW(sw.hCtrl,TBM_SETPOS,TRUE,pos);
            y+=28;
            char buf[32];snprintf(buf,sizeof(buf),"%.6g",cv);
            sw.nyVal=y;
            sw.hVal=mkS(L"STATIC",toW(buf).c_str(),SS_CENTER,y,16,(HMENU)(size_t)(base+2));
            y+=20;
        }else if(sw.kind=="toggle"){
            auto it=g_vars.find(sw.varName);double cv=it!=g_vars.end()?it->second:0;
            sw.nyCtrl=y;
            sw.hCtrl=mkS(L"BUTTON",toW(sw.label).c_str(),BS_AUTOCHECKBOX,y,20,(HMENU)(size_t)(base+1));
            SendMessageW(sw.hCtrl,BM_SETCHECK,cv!=0?BST_CHECKED:BST_UNCHECKED,0);
            sw.hVal=nullptr;y+=24;
        }else if(sw.kind=="input"){
            auto it=g_vars.find(sw.varName);double cv=it!=g_vars.end()?it->second:0;
            char buf[32];snprintf(buf,sizeof(buf),"%.6g",cv);
            sw.nyCtrl=y;
            sw.hCtrl=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",toW(buf).c_str(),
                WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,4,y,W-8,22,
                g_hSidebar,(HMENU)(size_t)(base+1),GetModuleHandle(nullptr),nullptr);
            SendMessageW(sw.hCtrl,WM_SETFONT,(WPARAM)g_font,TRUE);
            sw.hVal=nullptr;y+=26;
        }else if(sw.kind=="display"){
            double v=evalExpr(sw.expr);char buf[64];snprintf(buf,sizeof(buf),"%.6g",v);
            sw.nyCtrl=y; sw.hCtrl=nullptr;
            sw.nyVal=y;
            sw.hVal=CreateWindowExW(WS_EX_CLIENTEDGE,L"STATIC",toW(buf).c_str(),
                WS_CHILD|WS_VISIBLE|SS_CENTER,4,y,W-8,20,
                g_hSidebar,(HMENU)(size_t)(base+2),GetModuleHandle(nullptr),nullptr);
            SendMessageW(sw.hVal,WM_SETFONT,(WPARAM)g_font,TRUE);
            y+=24;
        }else if(sw.kind=="button"){
            sw.nyCtrl=y; sw.hVal=nullptr;
            sw.hCtrl=mkS(L"BUTTON",toW(sw.label).c_str(),BS_PUSHBUTTON,y,24,(HMENU)(size_t)(base+1));
            y+=28;
        }
        y+=4;
    }
    g_sidebarContentH=y+4;
    updateSidebarScroll();
}

// ── Canvas rendering ──────────────────────────────────────────────────────────
static void updatePageLabel(){
    if(g_hPageLbl) SetWindowTextW(g_hPageLbl,
        toW("Page "+std::to_string(g_focusPage+1)+" / "+std::to_string((int)g_doc.pages.size())).c_str());
}

static void drawPlot(HDC hdc,const Box&box,int px0,int py){
    int bx=px0+mm2px(box.x),by=py+mm2px(box.y);
    int bw=mm2px(box.w),bh=mm2px(box.h);
    const int ML=30,MR=8,MT=14,MB=20; // margins px
    int cw=bw-ML-MR,ch=bh-MT-MB;
    if(cw<20||ch<20)return;
    // White background
    RECT br={bx,by,bx+bw,by+bh};HBRUSH wb=CreateSolidBrush(RGB(255,255,255));FillRect(hdc,&br,wb);DeleteObject(wb);
    // Sample
    const int N=200;
    std::vector<double>ys(N);
    double savedX=g_vars.count(box.plot.xVar)?g_vars.at(box.plot.xVar):0;
    double xStep=(box.plot.xMax-box.plot.xMin)/(N-1);
    double yMin=1e300,yMax=-1e300;
    for(int k=0;k<N;k++){
        g_vars[box.plot.xVar]=box.plot.xMin+k*xStep;
        ys[k]=evalExpr(box.plot.yExpr);
        if(ys[k]<yMin)yMin=ys[k];if(ys[k]>yMax)yMax=ys[k];}
    g_vars[box.plot.xVar]=savedX;
    if(yMin==yMax){yMin-=1;yMax+=1;}
    double yRange=yMax-yMin;
    // Axes
    int ax=bx+ML,ay=by+MT;
    HPEN axPen=CreatePen(PS_SOLID,1,RGB(100,100,100));HPEN op=(HPEN)SelectObject(hdc,axPen);
    MoveToEx(hdc,ax,ay,nullptr);LineTo(hdc,ax,ay+ch);LineTo(hdc,ax+cw,ay+ch);
    // Tick labels
    SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,RGB(80,80,80));
    SelectObject(hdc,g_font);
    char buf[32];
    snprintf(buf,sizeof(buf),"%.4g",box.plot.xMin);
    TextOutW(hdc,ax,ay+ch+3,toW(buf).c_str(),(int)strlen(buf));
    snprintf(buf,sizeof(buf),"%.4g",box.plot.xMax);
    SIZE sz;GetTextExtentPoint32W(hdc,toW(buf).c_str(),(int)strlen(buf),&sz);
    TextOutW(hdc,ax+cw-sz.cx,ay+ch+3,toW(buf).c_str(),(int)strlen(buf));
    snprintf(buf,sizeof(buf),"%.4g",yMin);
    TextOutW(hdc,bx+2,ay+ch-10,toW(buf).c_str(),(int)strlen(buf));
    snprintf(buf,sizeof(buf),"%.4g",yMax);
    TextOutW(hdc,bx+2,ay,toW(buf).c_str(),(int)strlen(buf));
    // Title
    if(!box.plot.label.empty()){
        GetTextExtentPoint32W(hdc,toW(box.plot.label).c_str(),(int)box.plot.label.size(),&sz);
        TextOutW(hdc,ax+cw/2-sz.cx/2,by+1,toW(box.plot.label).c_str(),(int)box.plot.label.size());}
    // Curve
    HPEN curvePen=CreatePen(PS_SOLID,2,RGB(0,100,200));SelectObject(hdc,curvePen);
    std::vector<POINT>pts(N);
    for(int k=0;k<N;k++){
        pts[k].x=ax+(int)((k/(double)(N-1))*cw);
        pts[k].y=ay+ch-(int)(((ys[k]-yMin)/yRange)*ch);}
    Polyline(hdc,pts.data(),(int)N);
    SelectObject(hdc,op);DeleteObject(axPen);DeleteObject(curvePen);
}

static void paintCanvas(HDC hdc,HWND hc){
    RECT rc;GetClientRect(hc,&rc);
    HBRUSH bgBr=CreateSolidBrush(RGB(72,72,80));FillRect(hdc,&rc,bgBr);DeleteObject(bgBr);
    int firstVis=0;
    for(int pi=0;pi<(int)g_doc.pages.size();pi++){int py=canvasPageY(pi);int ph2=mm2px((float)g_doc.pages[pi].ph);if(py+ph2>0){firstVis=pi;break;}}
    if(firstVis!=g_focusPage){g_focusPage=firstVis;updatePageLabel();}

    for(int pi=0;pi<(int)g_doc.pages.size();pi++){
        int pw=mm2px((float)g_doc.pages[pi].pw);
        int ph=mm2px((float)g_doc.pages[pi].ph);
        int px0=(rc.right-pw)/2-g_scrollX;
        int py=canvasPageY(pi);
        if(py+ph<-24||py>rc.bottom+24)continue;
        RECT sh={px0+5,py+5,px0+pw+5,py+ph+5};
        HBRUSH shBr=CreateSolidBrush(RGB(20,20,25));FillRect(hdc,&sh,shBr);DeleteObject(shBr);
        RECT pr={px0,py,px0+pw,py+ph};
        HBRUSH wb=CreateSolidBrush(RGB(255,255,255));FillRect(hdc,&pr,wb);DeleteObject(wb);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(140,140,148));HPEN op=(HPEN)SelectObject(hdc,pen);
        HBRUSH nb=(HBRUSH)GetStockObject(NULL_BRUSH);HBRUSH ob=(HBRUSH)SelectObject(hdc,nb);
        Rectangle(hdc,px0,py,px0+pw+1,py+ph+1);
        SelectObject(hdc,op);SelectObject(hdc,ob);DeleteObject(pen);
        {SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,RGB(160,162,170));
         std::string lbl=std::to_string(pi+1);
         HFONT of=(HFONT)SelectObject(hdc,g_font);SIZE sz;
         GetTextExtentPoint32W(hdc,toW(lbl).c_str(),(int)lbl.size(),&sz);
         TextOutW(hdc,px0+pw/2-sz.cx/2,py+ph+6,toW(lbl).c_str(),(int)lbl.size());
         SelectObject(hdc,of);}

        const RPage&page=g_doc.pages[pi];
        for(auto box:page.boxes){
            if(box.isPlot){drawPlot(hdc,box,px0,py);continue;}
            if(!box.tmpl.empty()) box.text=evalTemplate(box.tmpl);
            int bx=px0+mm2px(box.x),by=py+mm2px(box.y);
            int bw=mm2px(box.w),bh=mm2px(box.h);
            RECT br={bx,by,bx+bw,by+bh};

            // ── Table grid renderer ──────────────────────────────────────────
            if(box.isTable){
                // Parse text into rows of cells
                std::vector<std::vector<std::string>> grid;
                std::istringstream ts(box.text);std::string tline;
                while(std::getline(ts,tline)){
                    if(tline.empty()) continue;
                    std::vector<std::string> cells;
                    std::istringstream cs(tline);std::string cell;
                    while(std::getline(cs,cell,'|'))cells.push_back(trim(cell));
                    if(!cells.empty()) grid.push_back(cells);
                }
                if(grid.empty()){continue;}
                int nCols=0;for(auto&r:grid)nCols=std::max(nCols,(int)r.size());
                if(nCols<1)continue;
                int nRows=(int)grid.size();
                int cellW=bw/nCols;
                int hdrH=mm2px(8.f),rowH=(bh-mm2px(8.f))/(nRows>1?nRows-1:1);
                rowH=std::max(rowH,mm2px(5.f));
                LOGFONTW lf={};lf.lfHeight=-(int)(10*g_zoom);lf.lfWeight=FW_NORMAL;lf.lfCharSet=DEFAULT_CHARSET;
                wcscpy(lf.lfFaceName,L"Segoe UI");
                HFONT tf=CreateFontIndirectW(&lf);HFONT of=(HFONT)SelectObject(hdc,tf);
                HPEN gridPen=CreatePen(PS_SOLID,1,RGB(160,170,180));
                HPEN op2=(HPEN)SelectObject(hdc,gridPen);
                int cy=by;
                for(int ri=0;ri<nRows;ri++){
                    int rh=(ri==0)?hdrH:rowH;
                    RECT rr={bx,cy,bx+bw,cy+rh};
                    // Header background
                    if(ri==0){HBRUSH hb=CreateSolidBrush(RGB(220,230,245));FillRect(hdc,&rr,hb);DeleteObject(hb);}
                    else if(ri%2==0){HBRUSH ab=CreateSolidBrush(RGB(248,250,253));FillRect(hdc,&rr,ab);DeleteObject(ab);}
                    // Cell text
                    SetBkMode(hdc,TRANSPARENT);
                    for(int ci=0;ci<nCols;ci++){
                        std::string cellTxt=(ci<(int)grid[ri].size())?grid[ri][ci]:"";
                        RECT cr={bx+ci*cellW+3,cy+2,bx+(ci+1)*cellW-3,cy+rh-2};
                        SetTextColor(hdc,ri==0?RGB(0,50,120):RGB(30,30,30));
                        LOGFONTW lf2=lf;lf2.lfWeight=(ri==0?FW_BOLD:FW_NORMAL);
                        HFONT tf2=CreateFontIndirectW(&lf2);HFONT of2=(HFONT)SelectObject(hdc,tf2);
                        DrawTextW(hdc,toW(cellTxt).c_str(),-1,&cr,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
                        SelectObject(hdc,of2);DeleteObject(tf2);
                    }
                    // Horizontal row border
                    MoveToEx(hdc,bx,cy,nullptr);LineTo(hdc,bx+bw,cy);
                    cy+=rh;
                }
                // Bottom border
                MoveToEx(hdc,bx,cy,nullptr);LineTo(hdc,bx+bw,cy);
                // Vertical column borders
                for(int ci=0;ci<=nCols;ci++){
                    MoveToEx(hdc,bx+ci*cellW,by,nullptr);LineTo(hdc,bx+ci*cellW,cy);}
                SelectObject(hdc,op2);DeleteObject(gridPen);
                SelectObject(hdc,of);DeleteObject(tf);
                continue;
            }

            if(box.hasBg){HBRUSH bb=CreateSolidBrush(box.bg);FillRect(hdc,&br,bb);DeleteObject(bb);}
            if(box.text.empty()&&!box.hasBg&&box.h<=1.5f){
                HPEN hp=CreatePen(PS_SOLID,1,RGB(180,180,190));HPEN ohp=(HPEN)SelectObject(hdc,hp);
                MoveToEx(hdc,bx,by,nullptr);LineTo(hdc,bx+bw,by);
                SelectObject(hdc,ohp);DeleteObject(hp);continue;}
            if(!box.text.empty()){
                SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,box.fg);
                LOGFONTW lf={};lf.lfHeight=-(int)(box.size*g_zoom);
                lf.lfWeight=box.isHead?FW_BOLD:FW_NORMAL;lf.lfCharSet=DEFAULT_CHARSET;
                wcscpy(lf.lfFaceName,L"Segoe UI");
                HFONT tf=CreateFontIndirectW(&lf);HFONT of=(HFONT)SelectObject(hdc,tf);
                UINT fmt=DT_WORDBREAK|DT_NOPREFIX;
                if(box.align==1)fmt|=DT_CENTER;else if(box.align==2)fmt|=DT_RIGHT;
                RECT tr=br;InflateRect(&tr,-2,-2);
                DrawTextW(hdc,toW(box.text).c_str(),-1,&tr,fmt);
                SelectObject(hdc,of);DeleteObject(tf);}
        }
    }
}

// ── Canvas WndProc ────────────────────────────────────────────────────────────
static LRESULT CALLBACK CanvasProc(HWND hWnd,UINT msg,WPARAM wP,LPARAM lP){
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hWnd,&ps);
        RECT rc;GetClientRect(hWnd,&rc);
        HDC mdc=CreateCompatibleDC(hdc);HBITMAP bm=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);
        HBITMAP old=(HBITMAP)SelectObject(mdc,bm);paintCanvas(mdc,hWnd);
        BitBlt(hdc,0,0,rc.right,rc.bottom,mdc,0,0,SRCCOPY);
        SelectObject(mdc,old);DeleteObject(bm);DeleteDC(mdc);EndPaint(hWnd,&ps);return 0;}
    case WM_MOUSEWHEEL:{int delta=GET_WHEEL_DELTA_WPARAM(wP);
        if(GetKeyState(VK_CONTROL)&0x8000) g_zoom=std::max(0.2f,std::min(5.f,g_zoom+(delta>0?0.15f:-0.15f)));
        else g_scrollY=std::max(0,g_scrollY-delta/3);
        updatePageLabel();InvalidateRect(hWnd,nullptr,FALSE);return 0;}
    case WM_KEYDOWN:{
        int curPh=g_focusPage<(int)g_doc.pages.size()?mm2px((float)g_doc.pages[g_focusPage].ph):mm2px((float)g_doc.ph);
        if(wP==VK_DOWN||wP==VK_NEXT){g_scrollY+=curPh+24;InvalidateRect(hWnd,nullptr,FALSE);}
        if(wP==VK_UP||wP==VK_PRIOR){g_scrollY=std::max(0,g_scrollY-curPh-24);InvalidateRect(hWnd,nullptr,FALSE);}
        if(wP==VK_HOME){g_scrollY=0;InvalidateRect(hWnd,nullptr,FALSE);}
        if(wP==VK_END){g_scrollY=0;for(auto&p:g_doc.pages)g_scrollY+=mm2px((float)p.ph)+24;InvalidateRect(hWnd,nullptr,FALSE);}
        return 0;}
    case WM_SETCURSOR:SetCursor(LoadCursor(nullptr,IDC_ARROW));return TRUE;
    }
    return DefWindowProcW(hWnd,msg,wP,lP);
}

// ── Sidebar WndProc ───────────────────────────────────────────────────────────
static LRESULT CALLBACK SidebarProc(HWND hWnd,UINT msg,WPARAM wP,LPARAM lP){
    switch(msg){
    case WM_VSCROLL:{
        RECT rc;GetClientRect(hWnd,&rc);int visH=rc.bottom-rc.top;
        int maxSc=std::max(0,g_sidebarContentH-visH);
        int prev=g_sidebarScrollY;
        switch(LOWORD(wP)){
        case SB_LINEUP:   g_sidebarScrollY-=16;break;
        case SB_LINEDOWN: g_sidebarScrollY+=16;break;
        case SB_PAGEUP:   g_sidebarScrollY-=visH;break;
        case SB_PAGEDOWN: g_sidebarScrollY+=visH;break;
        case SB_THUMBTRACK: g_sidebarScrollY=HIWORD(wP);break;}
        g_sidebarScrollY=std::max(0,std::min(g_sidebarScrollY,maxSc));
        if(g_sidebarScrollY!=prev){repositionSidebarControls();updateSidebarScroll();}
        return 0;}
    case WM_MOUSEWHEEL:{
        int delta=GET_WHEEL_DELTA_WPARAM(wP);
        RECT rc;GetClientRect(hWnd,&rc);int visH=rc.bottom-rc.top;
        int maxSc=std::max(0,g_sidebarContentH-visH);
        int prev=g_sidebarScrollY;
        g_sidebarScrollY=std::max(0,std::min(g_sidebarScrollY-delta/3,maxSc));
        if(g_sidebarScrollY!=prev){repositionSidebarControls();updateSidebarScroll();}
        return 0;}
    case WM_HSCROLL:{
        HWND hTrack=(HWND)lP;
        for(auto&sw:g_sideWidgets){
            if(sw.hCtrl==hTrack&&sw.kind=="slider"){
                int pos=(int)SendMessageW(hTrack,TBM_GETPOS,0,0);
                double val=sw.wMin+pos*(sw.wStep>0?sw.wStep:1.0);
                g_vars[sw.varName]=val;
                reRender();updateSidebarValues();return 0;}}
        return 0;}
    case WM_COMMAND:{
        int notif=HIWORD(wP);HWND hCtrl=(HWND)lP;
        for(auto&sw:g_sideWidgets){
            if(sw.hCtrl!=hCtrl)continue;
            if(sw.kind=="toggle"&&notif==BN_CLICKED){
                LRESULT chk=SendMessageW(sw.hCtrl,BM_GETCHECK,0,0);
                g_vars[sw.varName]=(chk==BST_CHECKED)?1.0:0.0;
                reRender();updateSidebarValues();return 0;}
            if(sw.kind=="input"&&notif==EN_KILLFOCUS){
                std::string txt=getWndStr(sw.hCtrl);
                if(!txt.empty()){
                    try{double v=std::stod(txt);g_vars[sw.varName]=v;
                        reRender();updateSidebarValues();}catch(...){}
                }return 0;}
            if(sw.kind=="button"&&notif==BN_CLICKED){
                if(!sw.setTarget.empty()){
                    g_vars[sw.setTarget]=evalExpr(sw.setExpr);
                    reRender();updateSidebarValues();}
                return 0;}}
        return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:
        SetBkMode((HDC)wP,TRANSPARENT);return(LRESULT)GetSysColorBrush(COLOR_3DFACE);
    case WM_ERASEBKGND:{RECT rc;GetClientRect(hWnd,&rc);
        FillRect((HDC)wP,&rc,GetSysColorBrush(COLOR_3DFACE));return 1;}
    }
    return DefWindowProcW(hWnd,msg,wP,lP);
}

// ── Layout (does NOT rebuild sidebar — only repositions) ──────────────────────
static const int TB_H=42,SB_H=22,SIDEBAR_W=284;
static void doLayout(HWND hWnd){
    RECT rc;GetClientRect(hWnd,&rc);int W=rc.right,H=rc.bottom;
    int sw=(g_hSidebar&&g_sideWidgets.size())?SIDEBAR_W:0;
    MoveWindow(g_hSidebar,W-sw,TB_H,sw,H-TB_H-SB_H,TRUE);
    MoveWindow(g_hCanvas, 0,   TB_H,W-sw,H-TB_H-SB_H,TRUE);
    if(sw){updateSidebarScroll();repositionSidebarControls();}
}

// ── Credits dialog (proper custom wndproc) ────────────────────────────────────
static BOOL g_credDone=FALSE;
static LRESULT CALLBACK CreditsDlgProc(HWND hWnd,UINT msg,WPARAM wP,LPARAM lP){
    if(msg==WM_COMMAND&&(LOWORD(wP)==IDOK||LOWORD(wP)==IDCANCEL)){g_credDone=TRUE;DestroyWindow(hWnd);return 0;}
    if(msg==WM_CLOSE){g_credDone=TRUE;DestroyWindow(hWnd);return 0;}
    if(msg==WM_DESTROY){g_credDone=TRUE;return 0;}
    return DefWindowProcW(hWnd,msg,wP,lP);
}
static void showCredits(HWND parent){
    g_credDone=FALSE;
    HWND hD=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,L"SynCredDlg",
        L"Credits",WS_POPUP|WS_CAPTION|WS_SYSMENU,300,200,360,220,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    LOGFONTW lf={};lf.lfHeight=-16;lf.lfWeight=FW_BOLD;wcscpy(lf.lfFaceName,L"Segoe UI");
    HFONT hB=CreateFontIndirectW(&lf);lf.lfWeight=FW_NORMAL;lf.lfHeight=-14;HFONT hN=CreateFontIndirectW(&lf);
    auto mk=[&](const char*t,int y,HFONT f){
        HWND h=CreateWindowExW(0,L"STATIC",toW(t).c_str(),WS_CHILD|WS_VISIBLE|SS_CENTER,10,y,340,22,hD,nullptr,GetModuleHandle(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);};
    mk("Syntaxia Reader",12,hB);mk("Cursed Entertainment",38,hN);
    mk("Designer:  Farica Kimora",60,hN);mk("\xa9 2026 Cursed Entertainment",82,hN);
    HWND hOK=CreateWindowExW(0,L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,140,120,80,26,hD,(HMENU)IDOK,GetModuleHandle(nullptr),nullptr);
    SendMessageW(hOK,WM_SETFONT,(WPARAM)hN,TRUE);
    ShowWindow(hD,SW_SHOW);EnableWindow(parent,FALSE);
    MSG msg;while(!g_credDone&&GetMessageW(&msg,nullptr,0,0)){if(msg.message==WM_QUIT){PostQuitMessage(0);break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
    EnableWindow(parent,TRUE);if(IsWindow(hD))DestroyWindow(hD);
    DeleteObject(hB);DeleteObject(hN);SetForegroundWindow(parent);
}

// ── File I/O ─────────────────────────────────────────────────────────────────
static bool pickFile(std::string&path){
    wchar_t buf[MAX_PATH]={};if(!path.empty())wcscpy(buf,toW(path).c_str());
    OPENFILENAMEW ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_hwnd;
    ofn.lpstrFilter=L"Syntaxia Document (*.syn)\0*.syn\0All Files\0*.*\0";
    ofn.lpstrFile=buf;ofn.nMaxFile=MAX_PATH;ofn.Flags=OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(!GetOpenFileNameW(&ofn))return false;path=fromW(buf);return true;
}
static void loadFile(const std::string&path){
    std::ifstream f(path,std::ios::binary);
    if(!f){MessageBoxW(g_hwnd,(std::wstring(L"Cannot open:\n")+toW(path)).c_str(),L"SynReader",MB_OK|MB_ICONERROR);return;}
    g_srcText=std::string((std::istreambuf_iterator<char>(f)),{});
    // Pre-pass for modern Structura format only
    bool isModern=g_srcText.find("@page ")==std::string::npos&&
                  g_srcText.find("@textbox ")==std::string::npos&&
                  !isSyntaxiaFormat(g_srcText);
    if(isModern) parseVarPass(g_srcText);
    else{g_vars.clear();g_derives.clear();g_sideWidgets.clear();g_styles.clear();}
    g_doc=parseSyn(g_srcText);
    g_filePath=path;g_scrollX=0;g_scrollY=0;g_focusPage=0;
    std::string title="Syntaxia Reader";
    if(!g_doc.title.empty())title+=" \xe2\x80\x94 "+g_doc.title;
    else{auto sl=path.find_last_of("/\\");title+=" \xe2\x80\x94 "+(sl==std::string::npos?path:path.substr(sl+1));}
    SetWindowTextW(g_hwnd,toW(title).c_str());
    updatePageLabel();
    rebuildSidebar();  // only called here, not on every resize
    doLayout(g_hwnd);
    InvalidateRect(g_hCanvas,nullptr,FALSE);
}

// ── Status bar ────────────────────────────────────────────────────────────────
static void createStatusBar(HWND p){
    g_hStatus=CreateWindowExW(0,STATUSCLASSNAMEW,nullptr,WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,p,nullptr,GetModuleHandle(nullptr),nullptr);}
static void updateStatus(){
    if(!g_hStatus)return;
    std::string s=g_doc.title.empty()?"No document":g_doc.title;
    if(!g_doc.author.empty())s+="  |  "+g_doc.author;
    s+="  |  "+std::to_string((int)g_doc.pages.size())+" page(s)";
    s+="  |  Zoom: "+std::to_string((int)(g_zoom*100))+"%";
    SetWindowTextW(g_hStatus,toW(s).c_str());
}

// ── Main WndProc ──────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hWnd,UINT msg,WPARAM wP,LPARAM lP){
    switch(msg){
    case WM_CREATE:{
        g_hwnd=hWnd;
        g_font    =CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        g_fontBold=CreateFontW(-13,0,0,0,FW_BOLD,  0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

        HMENU hM=CreateMenu();
        {HMENU s=CreatePopupMenu();AppendMenuW(s,MF_STRING,ID_FILE_OPEN,L"Open...\tCtrl+O");AppendMenuW(s,MF_SEPARATOR,0,nullptr);AppendMenuW(s,MF_STRING,ID_FILE_EXIT,L"Exit");AppendMenuW(hM,MF_POPUP,(UINT_PTR)s,L"File");}
        {HMENU s=CreatePopupMenu();AppendMenuW(s,MF_STRING,ID_VIEW_ZOOMIN,L"Zoom In\t+");AppendMenuW(s,MF_STRING,ID_VIEW_ZOOMOUT,L"Zoom Out\t-");AppendMenuW(s,MF_STRING,ID_VIEW_FIT,L"Fit Page\tF");AppendMenuW(hM,MF_POPUP,(UINT_PTR)s,L"View");}
        {HMENU s=CreatePopupMenu();AppendMenuW(s,MF_STRING,ID_HELP_CREDITS,L"Credits");AppendMenuW(hM,MF_POPUP,(UINT_PTR)s,L"Help");}
        SetMenu(hWnd,hM);

        int bx=6;
        auto btn=[&](const char*t,HMENU id,int w){
            HWND h=CreateWindowExW(0,L"BUTTON",toW(t).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,bx,9,w,26,hWnd,id,GetModuleHandle(nullptr),nullptr);
            SendMessageW(h,WM_SETFONT,(WPARAM)g_font,TRUE);bx+=w+4;};
        btn("Open",(HMENU)ID_FILE_OPEN,60);bx+=8;
        btn("Zoom +",(HMENU)ID_VIEW_ZOOMIN,60);btn("Zoom -",(HMENU)ID_VIEW_ZOOMOUT,60);btn("Fit",(HMENU)ID_VIEW_FIT,44);bx+=8;
        g_hPrev=CreateWindowExW(0,L"BUTTON",L"◄",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,bx,9,28,26,hWnd,(HMENU)IDC_PREV_PAGE,GetModuleHandle(nullptr),nullptr);
        SendMessageW(g_hPrev,WM_SETFONT,(WPARAM)g_font,TRUE);bx+=32;
        g_hPageLbl=CreateWindowExW(0,L"STATIC",L"Page 1 / 1",WS_CHILD|WS_VISIBLE|SS_CENTER|SS_CENTERIMAGE,bx,9,110,26,hWnd,(HMENU)IDC_PAGE_LABEL,GetModuleHandle(nullptr),nullptr);
        SendMessageW(g_hPageLbl,WM_SETFONT,(WPARAM)g_font,TRUE);bx+=114;
        g_hNext=CreateWindowExW(0,L"BUTTON",L"►",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,bx,9,28,26,hWnd,(HMENU)IDC_NEXT_PAGE,GetModuleHandle(nullptr),nullptr);
        SendMessageW(g_hNext,WM_SETFONT,(WPARAM)g_font,TRUE);

        // Canvas
        {WNDCLASSEXW wcc={};wcc.cbSize=sizeof(wcc);wcc.lpfnWndProc=CanvasProc;
         wcc.hInstance=GetModuleHandle(nullptr);wcc.hCursor=LoadCursor(nullptr,IDC_ARROW);
         wcc.lpszClassName=L"SynCanvas";RegisterClassExW(&wcc);}
        g_hCanvas=CreateWindowExW(0,L"SynCanvas",nullptr,WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,hWnd,(HMENU)IDC_CANVAS,GetModuleHandle(nullptr),nullptr);

        // Sidebar
        {WNDCLASSEXW wcc={};wcc.cbSize=sizeof(wcc);wcc.lpfnWndProc=SidebarProc;
         wcc.hInstance=GetModuleHandle(nullptr);wcc.hbrBackground=(HBRUSH)(COLOR_3DFACE+1);
         wcc.lpszClassName=L"SynSidebar";RegisterClassExW(&wcc);}
        g_hSidebar=CreateWindowExW(WS_EX_CLIENTEDGE,L"SynSidebar",nullptr,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL,0,0,0,0,hWnd,(HMENU)IDC_SIDEBAR,GetModuleHandle(nullptr),nullptr);

        // Credits dialog class
        {WNDCLASSEXW wcc={};wcc.cbSize=sizeof(wcc);wcc.lpfnWndProc=CreditsDlgProc;
         wcc.hInstance=GetModuleHandle(nullptr);wcc.hbrBackground=(HBRUSH)(COLOR_3DFACE+1);
         wcc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);wcc.lpszClassName=L"SynCredDlg";RegisterClassExW(&wcc);}

        createStatusBar(hWnd);doLayout(hWnd);updatePageLabel();updateStatus();
        break;}
    case WM_SIZE:
        doLayout(hWnd);SendMessageW(g_hStatus,WM_SIZE,0,0);InvalidateRect(g_hCanvas,nullptr,FALSE);
        break;
    case WM_KEYDOWN:SendMessageW(g_hCanvas,WM_KEYDOWN,wP,lP);break;
    case WM_COMMAND:{
        int id=LOWORD(wP);
        if(id==ID_FILE_OPEN){std::string p=g_filePath;if(pickFile(p))loadFile(p);updateStatus();break;}
        if(id==IDC_PREV_PAGE){g_scrollY=pageScrollY(std::max(0,g_focusPage-1));InvalidateRect(g_hCanvas,nullptr,FALSE);break;}
        if(id==IDC_NEXT_PAGE){g_scrollY=pageScrollY(std::min((int)g_doc.pages.size()-1,g_focusPage+1));InvalidateRect(g_hCanvas,nullptr,FALSE);break;}
        if(id==ID_VIEW_ZOOMIN) {g_zoom=std::min(5.f,g_zoom+0.25f);updateStatus();InvalidateRect(g_hCanvas,nullptr,FALSE);break;}
        if(id==ID_VIEW_ZOOMOUT){g_zoom=std::max(0.2f,g_zoom-0.25f);updateStatus();InvalidateRect(g_hCanvas,nullptr,FALSE);break;}
        if(id==ID_VIEW_FIT){RECT rc;GetClientRect(g_hCanvas,&rc);
            if(rc.right>40&&rc.bottom>40)g_zoom=std::min((rc.right-40)/(g_doc.pw*3.f),(rc.bottom-80)/(g_doc.ph*3.f));
            g_scrollX=0;g_scrollY=0;updateStatus();InvalidateRect(g_hCanvas,nullptr,FALSE);break;}
        if(id==ID_HELP_CREDITS){showCredits(hWnd);break;}
        if(id==ID_FILE_EXIT){SendMessageW(hWnd,WM_CLOSE,0,0);break;}
        break;}
    case WM_DROPFILES:{HDROP hDrop=(HDROP)wP;wchar_t buf[MAX_PATH]={};
        if(DragQueryFileW(hDrop,0,buf,MAX_PATH)){std::string p=fromW(buf);loadFile(p);updateStatus();}
        DragFinish(hDrop);break;}
    case WM_CLOSE:   DestroyWindow(hWnd);break;
    case WM_DESTROY: if(g_font)DeleteObject(g_font);if(g_fontBold)DeleteObject(g_fontBold);PostQuitMessage(0);break;
    }
    return DefWindowProcW(hWnd,msg,wP,lP);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR lpCmd,int nShow){
    InitCommonControls();
    WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_3DFACE+1);wc.lpszClassName=L"SynReader";
    wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);

    HWND hw=CreateWindowExW(WS_EX_ACCEPTFILES,L"SynReader",L"Syntaxia Reader",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1200,860,nullptr,nullptr,hInst,nullptr);

    if(lpCmd&&lpCmd[0]){
        std::string arg(lpCmd);
        if(!arg.empty()&&arg.front()=='"'){arg=arg.substr(1);if(arg.back()=='"')arg.pop_back();}
        if(!arg.empty())loadFile(arg);
    }else{std::string p;if(pickFile(p))loadFile(p);}

    ShowWindow(hw,nShow);UpdateWindow(hw);SetFocus(g_hCanvas);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return(int)msg.wParam;
}
