/*
 * SynReader.exe  —  Syntaxia Reader  (Structura runtime v1.0)
 * Renders .syn documents in the terminal. Zero external dependencies.
 *
 * Compile (MinGW):
 *   g++ -std=c++14 -O2 -static-libgcc -static-libstdc++ \
 *       -o SynReader.exe synreader.cpp -luser32
 *
 * Usage:
 *   SynReader.exe document.syn
 *   SynReader.exe physics_demo.syn age=34 smoker=1
 */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <iomanip>
#include "stdlib.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── ANSI / terminal ──────────────────────────────────────────────────────────
static bool g_ansi = false;
static void enableAnsi() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m = 0;
    if (GetConsoleMode(h, &m)) {
        SetConsoleMode(h, m | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
        g_ansi = true;
    }
    SetConsoleOutputCP(65001); // UTF-8
}
static std::string ansi(const char* code) { return g_ansi ? std::string("\033[")+code+"m" : ""; }
static const std::string RST  = "\033[0m";
static const std::string BOLD = "\033[1m";
static const std::string DIM  = "\033[2m";
static const std::string BLINK= "\033[5m";

// VGA 16-color names → ANSI fg code
static std::string vgaFg(const std::string& name) {
    if (!g_ansi) return "";
    static const std::map<std::string,std::string> M = {
        {"BLACK","30"},{"RED","31"},{"GREEN","32"},{"YELLOW","33"},
        {"BLUE","34"},{"MAGENTA","35"},{"CYAN","36"},{"WHITE","37"},
        {"SILVER","37"},{"GRAY","90"},{"GREY","90"},
        {"BRIGHT_RED","91"},{"BRIGHT_GREEN","92"},{"BRIGHT_CYAN","96"},
        {"BRIGHT_WHITE","97"},
    };
    auto it = M.find(name);
    return it != M.end() ? "\033["+it->second+"m" : "";
}
static std::string vgaBg(const std::string& name) {
    if (!g_ansi) return "";
    static const std::map<std::string,std::string> M = {
        {"BLACK","40"},{"RED","41"},{"GREEN","42"},{"YELLOW","43"},
        {"BLUE","44"},{"MAGENTA","45"},{"CYAN","46"},{"WHITE","47"},
        {"SILVER","47"},
    };
    auto it = M.find(name);
    return it != M.end() ? "\033["+it->second+"m" : "";
}

// ─── String helpers ───────────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b-a+1);
}
static std::string toLower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.substr(0,p.size()) == p;
}
static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream ss(s); std::string l;
    while (std::getline(ss, l)) {
        while (!l.empty() && (l.back()=='\r'||l.back()=='\n')) l.pop_back();
        v.push_back(l);
    }
    return v;
}

// ─── Math expression evaluator ────────────────────────────────────────────────
enum class TK { Num, Str, Id, Plus, Minus, Star, Slash, Pct, Pow,
                Lt, Gt, Le, Ge, Eq, Neq, And2, Or2, Bang,
                QMark, Colon, LParen, RParen, Comma,
                KwIf, KwElse, KwAnd, KwOr, KwNot, Sqrt, End };

struct Token {
    TK type; double num; std::string text;
    Token() : type(TK::End), num(0) {}
    Token(TK t, double n, const std::string& s) : type(t), num(n), text(s) {}
};

struct Value {
    bool isStr=false; double num=0; std::string str;
    explicit Value(double d=0): num(d) {}
    explicit Value(std::string s): isStr(true), str(std::move(s)) {}
    bool truthy() const { return isStr ? !str.empty() : num!=0.0; }
    double asNum() const {
        if (!isStr) return num;
        try { return std::stod(str); } catch(...) { return 0; }
    }
    std::string asStr() const {
        if (isStr) return str;
        if (num==std::floor(num) && std::fabs(num)<1e15) {
            std::ostringstream o; o<<(long long)num; return o.str();
        }
        std::ostringstream o;
        o<<std::setprecision(6)<<num;
        return o.str();
    }
};

// Tokenise one expression string
static std::vector<Token> tokenise(const std::string& src) {
    std::vector<Token> toks;
    size_t i=0, n=src.size();
    auto peek = [&](){ return i+1<n ? src[i+1] : '\0'; };
    while (i<n) {
        unsigned char c=(unsigned char)src[i];
        if (c==' '||c=='\t'||c=='\r'||c=='\n'){i++;continue;}
        // UTF-8: π = CF 80
        if (c==0xCF && i+1<n && (unsigned char)src[i+1]==0x80){
            toks.push_back(Token(TK::Num,M_PI,"π")); i+=2; continue;
        }
        // UTF-8: √ = E2 88 9A
        if (c==0xE2 && i+2<n && (unsigned char)src[i+1]==0x88 && (unsigned char)src[i+2]==0x9A){
            toks.push_back(Token(TK::Sqrt,0,"√")); i+=3; continue;
        }
        // String literal
        if (c=='"'||c=='\''){
            char q=src[i++]; std::string val;
            while(i<n&&src[i]!=q){
                if(src[i]=='\\'&&i+1<n){i++;val+=src[i];}else val+=src[i];
                i++;
            }
            if(i<n)i++;
            toks.push_back(Token(TK::Str,0,val)); continue;
        }
        // Number
        if (isdigit(c)||(c=='.'&&i+1<n&&isdigit(src[i+1]))){
            std::string num;
            while(i<n&&(isdigit((unsigned char)src[i])||src[i]=='.'
                        ||src[i]=='e'||src[i]=='E'
                        ||((src[i]=='+'||src[i]=='-')&&i>0
                           &&(src[i-1]=='e'||src[i-1]=='E'))))
                num+=src[i++];
            toks.push_back(Token(TK::Num,std::stod(num),num)); continue;
        }
        // Identifier / keyword
        if (isalpha(c)||c=='_'){
            std::string id;
            while(i<n&&(isalnum((unsigned char)src[i])||src[i]=='_'))id+=src[i++];
            TK t=TK::Id;
            if(id=="if"||id=="IF")   t=TK::KwIf;
            else if(id=="else"||id=="ELSE") t=TK::KwElse;
            else if(id=="and")  t=TK::KwAnd;
            else if(id=="or")   t=TK::KwOr;
            else if(id=="not")  t=TK::KwNot;
            else if(id=="true"||id=="True") { toks.push_back(Token(TK::Num,1,id)); continue; }
            else if(id=="false"||id=="False"){ toks.push_back(Token(TK::Num,0,id)); continue; }
            toks.push_back(Token(t,0,id)); continue;
        }
        // Two-char ops
        if(c=='*'&&peek()=='*'){toks.push_back(Token(TK::Pow,0,"**"));i+=2;continue;}
        if(c=='<'&&peek()=='='){toks.push_back(Token(TK::Le, 0,"<="));i+=2;continue;}
        if(c=='>'&&peek()=='='){toks.push_back(Token(TK::Ge, 0,">="));i+=2;continue;}
        if(c=='='&&peek()=='='){toks.push_back(Token(TK::Eq, 0,"=="));i+=2;continue;}
        if(c=='!'&&peek()=='='){toks.push_back(Token(TK::Neq,0,"!="));i+=2;continue;}
        if(c=='&'&&peek()=='&'){toks.push_back(Token(TK::And2,0,"&&"));i+=2;continue;}
        if(c=='|'&&peek()=='|'){toks.push_back(Token(TK::Or2, 0,"||"));i+=2;continue;}
        // Single-char
        static const std::string SC="+-*/%<>!?:(),";
        static const TK TT[]={TK::Plus,TK::Minus,TK::Star,TK::Slash,TK::Pct,
                               TK::Lt,TK::Gt,TK::Bang,TK::QMark,TK::Colon,
                               TK::LParen,TK::RParen,TK::Comma};
        auto p=SC.find((char)c);
        if(p!=std::string::npos){toks.push_back(Token(TT[p],0,std::string(1,(char)c)));i++;continue;}
        i++; // skip unknown
    }
    toks.push_back(Token(TK::End,0,""));
    return toks;
}

// Recursive descent evaluator
struct Eval {
    const std::map<std::string,Value>& vars;
    const std::vector<Token>& toks;
    size_t pos;

    Eval(const std::map<std::string,Value>& v, const std::vector<Token>& t)
        : vars(v), toks(t), pos(0) {}

    const Token& cur() const { return toks[pos]; }
    Token adv() { return toks[pos++]; }
    bool is(TK t) const { return cur().type==t; }

    Value run() { Value v=pyTernary(); return v; }

    Value pyTernary() {
        Value v=cTernary();
        if(is(TK::KwIf)){ adv();
            Value c=cTernary();
            if(!is(TK::KwElse)) throw std::runtime_error("expected else");
            adv();
            Value alt=pyTernary();
            return c.truthy()?v:alt;
        }
        return v;
    }
    Value cTernary() {
        Value v=orExpr();
        if(is(TK::QMark)){ adv();
            Value t=pyTernary(); if(!is(TK::Colon)) throw std::runtime_error("expected :");
            adv(); Value f=pyTernary(); return v.truthy()?t:f;
        }
        return v;
    }
    Value orExpr() {
        Value v=andExpr();
        while(is(TK::Or2)||is(TK::KwOr)){adv();Value r=andExpr();v=Value(double(v.truthy()||r.truthy()));}
        return v;
    }
    Value andExpr() {
        Value v=notExpr();
        while(is(TK::And2)||is(TK::KwAnd)){adv();Value r=notExpr();v=Value(double(v.truthy()&&r.truthy()));}
        return v;
    }
    Value notExpr() {
        if(is(TK::Bang)||is(TK::KwNot)){adv();return Value(double(!notExpr().truthy()));}
        return cmpExpr();
    }
    Value cmpExpr() {
        Value v=addExpr();
        while(true){
            if(is(TK::Lt)){adv();Value r=addExpr();v=Value(double(v.asNum()<r.asNum()));}
            else if(is(TK::Gt)){adv();Value r=addExpr();v=Value(double(v.asNum()>r.asNum()));}
            else if(is(TK::Le)){adv();Value r=addExpr();v=Value(double(v.asNum()<=r.asNum()));}
            else if(is(TK::Ge)){adv();Value r=addExpr();v=Value(double(v.asNum()>=r.asNum()));}
            else if(is(TK::Eq)){adv();Value r=addExpr();
                v=Value(double(v.isStr&&r.isStr?v.str==r.str:v.asNum()==r.asNum()));}
            else if(is(TK::Neq)){adv();Value r=addExpr();
                v=Value(double(v.isStr&&r.isStr?v.str!=r.str:v.asNum()!=r.asNum()));}
            else break;
        }
        return v;
    }
    Value addExpr() {
        Value v=mulExpr();
        while(is(TK::Plus)||is(TK::Minus)){
            bool add=is(TK::Plus); adv(); Value r=mulExpr();
            v=Value(add?v.asNum()+r.asNum():v.asNum()-r.asNum());
        }
        return v;
    }
    Value mulExpr() {
        Value v=powExpr();
        while(is(TK::Star)||is(TK::Slash)||is(TK::Pct)){
            TK op=cur().type; adv(); Value r=powExpr();
            if(op==TK::Star) v=Value(v.asNum()*r.asNum());
            else if(op==TK::Slash) v=Value(v.asNum()/r.asNum());
            else v=Value(std::fmod(v.asNum(),r.asNum()));
        }
        return v;
    }
    Value powExpr() {
        Value v=unary();
        if(is(TK::Pow)){adv();Value r=unary();v=Value(std::pow(v.asNum(),r.asNum()));}
        return v;
    }
    Value unary() {
        if(is(TK::Minus)){adv();return Value(-unary().asNum());}
        if(is(TK::Sqrt)){adv();return Value(std::sqrt(primary().asNum()));}
        return primary();
    }
    Value primary() {
        if(is(TK::Num)){double n=cur().num;adv();return Value(n);}
        if(is(TK::Str)){std::string s=cur().text;adv();return Value(s);}
        if(is(TK::LParen)){adv();Value v=pyTernary();
            if(is(TK::RParen))adv(); return v;}
        if(is(TK::Id)){
            std::string nm=cur().text; adv();
            if(is(TK::LParen)){adv(); // function call
                std::vector<Value> args;
                while(!is(TK::RParen)&&!is(TK::End)){
                    args.push_back(pyTernary());
                    if(is(TK::Comma))adv();
                }
                if(is(TK::RParen))adv();
                return callFn(nm,args);
            }
            auto it=vars.find(nm);
            if(it!=vars.end()) return it->second;
            throw std::runtime_error("Undefined: "+nm);
        }
        return Value(0.0);
    }
    Value callFn(const std::string& n, const std::vector<Value>& a){
        auto N=[&](int i){return a.at(i).asNum();};
        if(n=="sqrt"||n=="math.sqrt") return Value(std::sqrt(N(0)));
        if(n=="sin") return Value(std::sin(N(0)));
        if(n=="cos") return Value(std::cos(N(0)));
        if(n=="tan") return Value(std::tan(N(0)));
        if(n=="abs") return Value(std::fabs(N(0)));
        if(n=="floor") return Value(std::floor(N(0)));
        if(n=="ceil")  return Value(std::ceil(N(0)));
        if(n=="round") return Value(std::round(N(0)));
        if(n=="log")   return Value(std::log(N(0)));
        if(n=="exp")   return Value(std::exp(N(0)));
        if(n=="min")   return Value(std::min(N(0),N(1)));
        if(n=="max")   return Value(std::max(N(0),N(1)));
        if(n=="int")   return Value((double)(long long)N(0));
        if(n=="float") return Value(N(0));
        if(n=="str")   return Value(a.at(0).asStr());
        // Unknown: return 0
        return Value(0.0);
    }
};

static Value evalExpr(const std::string& expr, const std::map<std::string,Value>& vars) {
    auto toks = tokenise(expr);
    Eval ev(vars, toks);
    return ev.run();
}

// Auto-format a Value for print output
static std::string fmtVal(const Value& v, const std::string& fmt="") {
    if(v.isStr) return v.str;
    if(!fmt.empty()){
        // C printf %: find %...f/d/s/g
        auto p=fmt.find('%');
        if(p!=std::string::npos){
            char buf[128];
            std::string prefix=fmt.substr(0,p);
            std::string fspec=fmt.substr(p);
            // find end of format spec
            size_t e=p+1; while(e<fmt.size()&&!isalpha(fmt[e]))e++;
            std::string suffix=e<fmt.size()?fmt.substr(e+1):"";
            fspec=fmt.substr(p,e-p+1);
            snprintf(buf,sizeof(buf),fspec.c_str(),v.num);
            return prefix+buf+suffix;
        }
        // Python .Nf style
        if(!fmt.empty()&&fmt[0]=='.'){
            std::ostringstream o;
            int prec=std::stoi(fmt.substr(1,fmt.size()-1));
            char t=fmt.back();
            if(t=='f') o<<std::fixed<<std::setprecision(prec)<<v.num;
            else o<<std::setprecision(prec)<<v.num;
            return o.str();
        }
    }
    // Auto
    if(v.num==std::floor(v.num)&&std::fabs(v.num)<1e15){
        std::ostringstream o; o<<(long long)v.num; return o.str();
    }
    char buf[64]; snprintf(buf,sizeof(buf),"%.4f",v.num);
    std::string s(buf);
    auto dot=s.find('.');
    if(dot!=std::string::npos){
        auto last=s.find_last_not_of('0');
        if(last>dot) s=s.substr(0,last+1); else s=s.substr(0,dot);
    }
    return s;
}

// ─── .strss style engine ──────────────────────────────────────────────────────
struct StyleRule {
    std::string condition;   // "" = always; else expression
    std::string fg, bg;
    bool blink=false;
};
struct StyleDef {
    std::string fg, bg;
    bool blink=false;
};

struct StyleSheet {
    std::vector<StyleRule>            rules;
    std::map<std::string,StyleDef>   ids;   // #Header etc.
    std::string globalFg, globalBg;

    void parse(const std::string& src) {
        // Very lightweight .strss parser
        std::istringstream ss(src); std::string line;
        std::string currentId, currentCond;
        bool inBlock=false;
        StyleDef cur; StyleRule curRule;
        while(std::getline(ss,line)){
            std::string s=trim(line);
            if(s.empty()||startsWith(s,"/*")) continue;

            // [Condition: expr]
            if(s[0]=='['){
                auto c=s.find(':');
                if(c!=std::string::npos){
                    std::string key=trim(s.substr(1,c-1));
                    std::string val=trim(s.substr(c+1));
                    if(val.back()==']') val.pop_back();
                    if(toLower(key)=="condition") currentCond=trim(val);
                }
                continue;
            }
            // #Id or block
            if(s[0]=='#' && s.back()=='{'){
                currentId=trim(s.substr(1,s.size()-2)); cur={}; inBlock=true; continue;
            }
            if(s=="{") { inBlock=true; continue; }
            if(s=="}") {
                if(!currentCond.empty()){
                    StyleRule r; r.condition=currentCond;
                    r.fg=cur.fg; r.bg=cur.bg; r.blink=cur.blink;
                    rules.push_back(r); currentCond="";
                } else if(!currentId.empty()){
                    if(currentId=="Global"){globalFg=cur.fg;globalBg=cur.bg;}
                    else ids[currentId]=cur;
                    currentId="";
                }
                cur={}; inBlock=false; continue;
            }
            if(!inBlock) continue;
            // key: value;
            auto colon=s.find(':');
            if(colon==std::string::npos) continue;
            std::string key=trim(s.substr(0,colon));
            std::string val=trim(s.substr(colon+1));
            if(!val.empty()&&val.back()==';') val.pop_back();
            val=trim(val);
            // Strip /* comment */
            auto cm=val.find("/*");
            if(cm!=std::string::npos) val=trim(val.substr(0,cm));

            std::string kl=toLower(key);
            if(kl=="glyph"||kl=="color"||kl=="colour") cur.fg=val;
            else if(kl=="surface"||kl=="background") cur.bg=val;
            else if(kl=="signal"&&toLower(val)=="blink") cur.blink=true;
        }
    }

    // Resolve active style given current variable state
    StyleDef resolve(const std::map<std::string,Value>& vars,
                     const std::string& id="") const {
        StyleDef base;
        base.fg=globalFg; base.bg=globalBg;
        // Apply matching conditions
        for(auto& r:rules){
            if(r.condition.empty()){base.fg=r.fg;base.bg=r.bg;base.blink=r.blink;continue;}
            try{
                Value v=evalExpr(r.condition,vars);
                if(v.truthy()){base.fg=r.fg;base.bg=r.bg;base.blink=r.blink;}
            }catch(...){}
        }
        // Apply id-specific style on top
        if(!id.empty()){
            auto it=ids.find(id);
            if(it!=ids.end()){
                if(!it->second.fg.empty()) base.fg=it->second.fg;
                if(!it->second.bg.empty()) base.bg=it->second.bg;
                if(it->second.blink) base.blink=true;
            }
        }
        return base;
    }

    std::string applyAnsi(const StyleDef& sd) const {
        std::string s;
        if(!sd.fg.empty()) s+=vgaFg(sd.fg);
        if(!sd.bg.empty()) s+=vgaBg(sd.bg);
        if(sd.blink&&g_ansi) s+="\033[5m";
        return s;
    }
};

// ─── .syn tag parser ──────────────────────────────────────────────────────────
struct Attr { std::map<std::string,std::string> m; };

static Attr parseAttrs(const std::string& s){
    Attr a;
    size_t i=0,n=s.size();
    while(i<n){
        while(i<n&&s[i]==' ')i++;
        // read key
        size_t ks=i;
        while(i<n&&s[i]!='='&&s[i]!=' ')i++;
        if(i>=n) break;
        std::string key=s.substr(ks,i-ks);
        if(s[i]!='='){i++;continue;}
        i++; // skip =
        char q=s[i]; i++;
        size_t vs=i;
        while(i<n&&s[i]!=q)i++;
        std::string val=s.substr(vs,i-vs);
        if(i<n)i++;
        a.m[key]=val;
    }
    return a;
}

// Parse a tag opening: returns (tagname, attrs, isSelfClose, isClose)
struct TagInfo { std::string name; Attr attrs; bool selfClose=false; bool close=false; };
static TagInfo parseTag(const std::string& raw){
    TagInfo t;
    std::string s=trim(raw);
    if(s.empty()||s[0]!='<') return t;
    s=s.substr(1);
    if(s.back()=='>') s.pop_back();
    if(!s.empty()&&s.back()=='/'){ s.pop_back(); t.selfClose=true; }
    if(!s.empty()&&s[0]=='/'){ s=s.substr(1); t.close=true; }
    // Skip HTML comments (<!-- ... -->) and processing instructions
    if(!s.empty()&&s[0]=='!') return TagInfo();
    // handle </@>
    if(s=="@>") { t.close=true; t.name="@"; return t; }
    // tag name
    size_t i=0;
    while(i<s.size()&&s[i]!=' ')i++;
    t.name=s.substr(0,i);
    if(i<s.size()) t.attrs=parseAttrs(trim(s.substr(i)));
    return t;
}

// Tokenise .syn source into a flat list of tokens:
// ("open", tagname, attrs, selfClose) | ("close", tagname) | ("text", text)
struct SynTok {
    std::string kind, name, text;
    Attr attrs;
    bool selfClose;
    SynTok() : selfClose(false) {}
    SynTok(const std::string& k, const std::string& n, const std::string& t)
        : kind(k), name(n), text(t), selfClose(false) {}
};

static std::vector<SynTok> tokeniseSyn(const std::string& src){
    std::vector<SynTok> out;
    size_t i=0,n=src.size();
    while(i<n){
        // Import: <@>filename@</@> or <@>filename</@>
        if(i+2<n&&src[i]=='<'&&src[i+1]=='@'&&src[i+2]=='>'){
            size_t end=src.find("</@>",i+3);
            if(end==std::string::npos) end=src.find("</@>",i+3);
            if(end!=std::string::npos){
                std::string fname=trim(src.substr(i+3,end-i-3));
                if(!fname.empty()&&fname.back()=='@') fname.pop_back();
                fname=trim(fname);
                SynTok t; t.kind="import"; t.name=fname;
                out.push_back(t); i=end+4; continue;
            }
        }
        // Type-cast <~>type==expr<~>
        if(i+2<n&&src[i]=='<'&&src[i+1]=='~'&&src[i+2]=='>'){
            size_t end=src.find("<~>",i+3);
            if(end!=std::string::npos){
                std::string inner=trim(src.substr(i+3,end-i-3));
                SynTok t; t.kind="cast"; t.text=inner;
                out.push_back(t); i=end+3; continue;
            }
        }
        // Type-cast <>type==expr<>
        if(i+1<n&&src[i]=='<'&&src[i+1]=='>'){
            size_t end=src.find("<>",i+2);
            if(end!=std::string::npos){
                std::string inner=trim(src.substr(i+2,end-i-2));
                SynTok t; t.kind="cast"; t.text=inner;
                out.push_back(t); i=end+2; continue;
            }
        }
        if(src[i]=='<'){
            size_t end=src.find('>',i);
            if(end==std::string::npos){SynTok _t("text","",std::string(1,src[i++]));out.push_back(_t);continue;}
            std::string raw=src.substr(i,end-i+1);
            TagInfo ti=parseTag(raw);
            if(!ti.name.empty()){
                SynTok t;
                t.kind=ti.close?"close":"open";
                t.name=ti.name; t.attrs=ti.attrs; t.selfClose=ti.selfClose;
                out.push_back(t);
            }
            i=end+1; continue;
        }
        // Text
        size_t end=src.find('<',i);
        std::string txt=src.substr(i, end==std::string::npos?std::string::npos:end-i);
        if(!trim(txt).empty()){SynTok _t("text","",txt);out.push_back(_t);}
        if(end==std::string::npos) break;
        i=end;
    }
    return out;
}

// ─── AST ─────────────────────────────────────────────────────────────────────
struct Node {
    std::string kind;   // func,var,if,loop,print,call,layout,import,text,cast
    std::string name, type, expr, styleId, castTarget;
    std::vector<Node> body, alt; // body=then/loop body, alt=else body
    std::map<std::string,std::string> attrs;
};

// Forward declaration
static std::vector<Node> parseBody(const std::vector<SynTok>& toks,
                                    size_t& pos, const std::string& closeTag);

static Node parseFuncBody(const std::vector<SynTok>& toks, size_t& pos) {
    // parse body nodes until </func>
    Node n; n.kind="func_body";
    n.body=parseBody(toks,pos,"func");
    return n;
}

static std::vector<Node> parseBody(const std::vector<SynTok>& toks,
                                    size_t& pos, const std::string& closeTag){
    std::vector<Node> nodes;
    while(pos<toks.size()){
        const auto& t=toks[pos];
        if(t.kind=="close"&&t.name==closeTag) { pos++; break; }
        // <else> is an open tag used as a separator — stop body, let if-parser handle it
        if(t.kind=="open"&&t.name=="else")    { break; }
        if(t.kind=="close")                    { pos++; continue; } // skip unexpected close

        if(t.kind=="import"){
            Node n; n.kind="import"; n.name=t.name;
            nodes.push_back(n); pos++; continue;
        }
        if(t.kind=="cast"){
            // <>type==expr<>
            Node n; n.kind="cast"; n.expr=t.text;
            auto eq=n.expr.find("==");
            if(eq!=std::string::npos){n.castTarget=trim(n.expr.substr(0,eq));n.expr=trim(n.expr.substr(eq+2));}
            nodes.push_back(n); pos++; continue;
        }
        if(t.kind=="text"){ pos++; continue; } // standalone text: ignore in logic

        if(t.kind!="open"){ pos++; continue; }

        const std::string& tag=t.name;
        Attr attrs=t.attrs;
        pos++;

        if(tag=="var"||tag=="const"){
            Node n; n.kind="var";
            n.name=attrs.m.count("name")?attrs.m.at("name"):"";
            n.type=attrs.m.count("type")?attrs.m.at("type"):"float";
            // body: collect text or cast until </var>
            std::string body_txt;
            while(pos<toks.size()){
                const auto& bt=toks[pos];
                if(bt.kind=="close"&&bt.name==tag){pos++;break;}
                if(bt.kind=="text") body_txt+=bt.text;
                else if(bt.kind=="cast"){
                    n.castTarget=trim(bt.text.substr(0,bt.text.find("==")));
                    body_txt=trim(bt.text.substr(bt.text.find("==")+2));
                }
                pos++;
            }
            n.expr=trim(body_txt);
            // Strip inline comment
            auto cm=n.expr.find("--");
            if(cm!=std::string::npos) n.expr=trim(n.expr.substr(0,cm));
            nodes.push_back(n); continue;
        }
        if(tag=="if"){
            Node n; n.kind="if";
            n.expr=attrs.m.count("condition")?attrs.m.at("condition"):"false";
            // Parse the then-body; stops at </if> OR <else>
            n.body=parseBody(toks,pos,"if");
            // If parseBody stopped at <else> (not at </if>), parse the else-body
            if(pos<toks.size()&&toks[pos].kind=="open"&&toks[pos].name=="else"){
                pos++; // consume <else>
                // Else-body runs until </if> (the outer closing tag)
                n.alt=parseBody(toks,pos,"if");
            }
            nodes.push_back(n); continue;
        }
        if(tag=="loop"||tag=="while"){
            Node n; n.kind="loop";
            if(attrs.m.count("count"))     n.expr=attrs.m.at("count");
            if(attrs.m.count("while"))   { n.expr=attrs.m.at("while");     n.castTarget="while"; }
            if(attrs.m.count("condition")){ n.expr=attrs.m.at("condition"); n.castTarget="while"; }
            if(tag=="while")             { n.castTarget="while"; }
            n.body=parseBody(toks,pos,tag); // closes </while> or </loop>
            nodes.push_back(n); continue;
        }
        if(tag=="print"){
            Node n; n.kind="print";
            if(attrs.m.count("style")) n.styleId=attrs.m.at("style");
            // collect text until </print>
            std::string txt;
            while(pos<toks.size()){
                const auto& bt=toks[pos];
                if(bt.kind=="close"&&bt.name=="print"){pos++;break;}
                if(bt.kind=="text") txt+=bt.text;
                pos++;
            }
            n.expr=trim(txt);
            nodes.push_back(n); continue;
        }
        if(tag=="layout"){
            Node n; n.kind="layout"; n.attrs=attrs.m;
            std::string txt;
            while(pos<toks.size()){
                const auto& bt=toks[pos];
                if(bt.kind=="close"&&bt.name=="layout"){pos++;break;}
                if(bt.kind=="text") txt+=bt.text;
                pos++;
            }
            n.expr=trim(txt);
            nodes.push_back(n); continue;
        }
        if(tag=="call"||t.selfClose){
            if(tag=="call"){
                Node n; n.kind="call";
                n.name=attrs.m.count("method")?attrs.m.at("method"):"";
                nodes.push_back(n);
            }
            continue;
        }
        // Unknown tag — skip only if it has a matching close tag nearby, otherwise ignore
        // (prevents eating the rest of the document on unrecognised tags)
        {
            size_t save=pos;
            bool found=false;
            for(size_t k=pos;k<toks.size()&&k<pos+4;k++){
                if(toks[k].kind=="close"&&toks[k].name==tag){found=true;break;}
            }
            if(found) parseBody(toks,pos,tag);
            else (void)save; // leave pos where it is
        }
    }
    return nodes;
}

// ─── Interpreter ─────────────────────────────────────────────────────────────
struct Interpreter {
    std::map<std::string,Value>        vars;
    std::map<std::string,std::vector<Node>> funcs;
    StyleSheet                          sheet;
    int                                 callDepth=0;

    void coerce(Value& v, const std::string& type) {
        if(type=="int"||type=="INT")   v=Value((double)(long long)v.asNum());
        else if(type=="float"||type=="FLOAT") v=Value(v.asNum());
        else if(type=="bool")          v=Value(double(v.truthy()?1:0));
        else if(type=="string"||type=="str") v=Value(v.asStr());
    }

    Value eval(const std::string& expr) {
        if(trim(expr).empty()) return Value(0.0);
        try{ return evalExpr(expr, vars); }
        catch(const std::exception& e){
            // non-fatal: return 0
            return Value(0.0);
        }
    }

    // Print a line with style applied
    void printLine(const std::string& raw, const std::string& styleId="") {
        std::string out;
        // Split "label: expression" — evalExpr so undefined vars throw → fall back to raw
        auto ci=raw.rfind(": ");
        if(ci!=std::string::npos){
            std::string label=raw.substr(0,ci+2);
            std::string exprPart=trim(raw.substr(ci+2));
            try{
                Value v=evalExpr(exprPart,vars);
                out=label+fmtVal(v);
            }catch(...){out=raw;}
        } else {
            // Without a label:expr pattern, only evaluate if the text
            // looks like a pure identifier or simple numeric expression.
            // Separator lines (----), uppercase headings, etc. print raw.
            std::string t=trim(raw);
            bool all_id=!t.empty();
            for(char c:t) if(!isalnum((unsigned char)c)&&c!='_'){all_id=false;break;}
            bool starts_var=!t.empty()&&(islower((unsigned char)t[0])||t[0]=='_');
            bool starts_num=!t.empty()&&(isdigit((unsigned char)t[0])||t[0]=='(');
            if((all_id&&starts_var)||starts_num){
                try{Value v=evalExpr(t,vars);out=fmtVal(v);}catch(...){out=raw;}
            } else {
                out=raw;
            }
        }
        // Apply style
        auto sd=sheet.resolve(vars, styleId);
        std::string pre=sheet.applyAnsi(sd);
        std::string suf=(!pre.empty()&&g_ansi)?"\033[0m":"";
        std::cout<<pre<<out<<suf<<"\n";
    }

    void execNode(const Node& n){
        if(n.kind=="var"){
            Value v;
            if(!n.castTarget.empty()){
                v=eval(n.expr); coerce(v,n.castTarget);
            } else {
                v=eval(n.expr);
                coerce(v,n.type);
            }
            vars[n.name]=v;
        }
        else if(n.kind=="if"){
            Value cond=eval(n.expr);
            if(cond.truthy()) execute(n.body);
            else              execute(n.alt);
        }
        else if(n.kind=="loop"){
            if(n.castTarget=="while"){
                int guard=0;
                while(eval(n.expr).truthy()&&guard++<100000) execute(n.body);
            } else {
                Value cnt=eval(n.expr);
                int c=(int)cnt.asNum();
                for(int i=0;i<c;i++) execute(n.body);
            }
        }
        else if(n.kind=="print"){
            printLine(n.expr, n.styleId);
        }
        else if(n.kind=="layout"){
            // For terminal: just print centered or raw
            std::string align="left";
            if(n.attrs.count("align")) align=toLower(n.attrs.at("align"));
            std::string txt=trim(n.expr);
            if(align=="center"){
                int w=60;
                int pad=(w-(int)txt.size())/2;
                if(pad>0) txt=std::string(pad,' ')+txt;
            }
            printLine(txt, "");
        }
        else if(n.kind=="call"){
            auto it=funcs.find(n.name);
            if(it!=funcs.end()&&callDepth<32){callDepth++;execute(it->second);callDepth--;}
        }
        else if(n.kind=="import"){
            loadImport(n.name);
        }
    }

    void execute(const std::vector<Node>& nodes){
        for(auto& n:nodes) execNode(n);
    }

    void loadImport(const std::string& fname){
        std::string src;
        // Check embedded stdlib first
        auto it=STDLIB.find(fname);
        if(it!=STDLIB.end()) src=it->second;
        else {
            std::ifstream f(fname);
            if(!f){ std::cerr<<"[import not found: "<<fname<<"]\n"; return; }
            src=std::string((std::istreambuf_iterator<char>(f)),{});
        }
        runSource(src);
    }

    void runSource(const std::string& src);
};

static void buildDocument(Interpreter& interp, const std::string& src,
                          bool isImport=false){
    // Extract <style> block
    {
        auto ss=src.find("<style>");
        auto se=src.find("</style>");
        if(ss!=std::string::npos&&se!=std::string::npos)
            interp.sheet.parse(src.substr(ss+7,se-ss-7));
    }
    auto toks=tokeniseSyn(src);
    size_t pos=0;
    std::vector<std::string> mainFuncs;  // functions defined in THIS file
    bool hadExplicitCall=false;

    while(pos<toks.size()){
        const auto& t=toks[pos];
        if(t.kind=="import"){ interp.loadImport(t.name); pos++; continue; }
        if(t.kind=="open"&&t.name=="func"){
            std::string fname=t.attrs.m.count("name")?t.attrs.m.at("name"):"main";
            pos++;
            interp.funcs[fname]=parseBody(toks,pos,"func");
            if(!isImport) mainFuncs.push_back(fname);
            continue;
        }
        if(t.kind=="open"&&(t.name=="call"||t.name=="call/")){
            std::string mname=t.attrs.m.count("method")?t.attrs.m.at("method"):"";
            if(!mname.empty()){
                hadExplicitCall=true;
                auto it=interp.funcs.find(mname);
                if(it!=interp.funcs.end()){ interp.callDepth=0; interp.execute(it->second); }
            }
            pos++; continue;
        }
        pos++;
    }
    // Auto-call: if no explicit <call> tags, run all functions defined in this file
    if(!hadExplicitCall&&!isImport){
        for(auto& fname:mainFuncs){
            auto it=interp.funcs.find(fname);
            if(it!=interp.funcs.end()){ interp.callDepth=0; interp.execute(it->second); }
        }
    }
}

void Interpreter::runSource(const std::string& src){
    buildDocument(*this, src, true); // imported — don't auto-call
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv){
    enableAnsi();

    if(argc<2){
        std::cout<<"Syntaxia Reader v1.0 — Structura Runtime\n";
        std::cout<<"Usage: SynReader.exe <file.syn> [var=value ...]\n";
        return 0;
    }

    std::string path=argv[1];
    std::ifstream f(path);
    if(!f){ std::cerr<<"Error: cannot open '"<<path<<"'\n"; return 1; }
    std::string src((std::istreambuf_iterator<char>(f)),{});

    Interpreter interp;

    // CLI overrides: age=34 smoker=1 etc.
    for(int i=2;i<argc;i++){
        std::string arg=argv[i];
        auto eq=arg.find('=');
        if(eq!=std::string::npos){
            std::string k=arg.substr(0,eq);
            std::string v=arg.substr(eq+1);
            try{ interp.vars[k]=Value(std::stod(v)); }
            catch(...){ interp.vars[k]=Value(v); }
        }
    }

    buildDocument(interp, src);
    return 0;
}
