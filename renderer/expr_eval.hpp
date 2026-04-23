#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include "var_store.hpp"

// ─── AST nodes ───────────────────────────────────────────────────────────────

struct Expr { virtual ~Expr() = default; };

struct NumberLit   : Expr { double value; };
struct StringLit   : Expr { std::string value; };
struct VarRef      : Expr { std::string name; };
struct BinaryOp    : Expr {
    char op;   // + - * / % **
    std::unique_ptr<Expr> lhs, rhs;
};
struct UnaryOp     : Expr { char op; std::unique_ptr<Expr> operand; };
struct CallExpr    : Expr {
    std::string func;
    std::vector<std::unique_ptr<Expr>> args;
};
struct TernaryExpr : Expr {
    std::unique_ptr<Expr> cond, then_, else_;
};

// ─── ExprEvaluator ───────────────────────────────────────────────────────────
//
// Parses and evaluates Syntaxia expressions.
// Reads variable values from VarStore at evaluation time.
//
// Supported:
//   Literals:     42  3.14  "text"
//   Constants:    π  (≈ 3.14159…)
//   Functions:    √(x)  sin(x)  cos(x)  tan(x)  log(x)  exp(x)  abs(x)
//                 floor(x)  ceil(x)  round(x)  min(a,b)  max(a,b)
//   Operators:    + - * / % ** (power)
//   Comparisons:  < > <= >= == !=
//   Logical:      && || !
//   Ternary:      cond ? a : b
//   Grouping:     (expr)
//
class ExprEvaluator {
public:
    explicit ExprEvaluator(const VarStore& store);

    // Parse expr string into an AST.  Throws on syntax error.
    std::unique_ptr<Expr> parse(const std::string& expr) const;

    // Evaluate a pre-parsed AST.  Returns double or string (via variant).
    double evalNum(const Expr& node) const;
    std::string evalStr(const Expr& node) const;

    // One-shot: parse + eval.
    double evalNum(const std::string& expr) const;
    std::string evalStr(const std::string& expr) const;

    // Return the set of variable names referenced in expr.
    std::set<std::string> dependencies(const std::string& expr) const;

private:
    const VarStore& store_;

    // Recursive-descent parser state
    struct ParseState {
        const char* src;
        size_t      pos;
        size_t      len;
    };

    std::unique_ptr<Expr> parseExpr(ParseState& s) const;
    std::unique_ptr<Expr> parseTernary(ParseState& s) const;
    std::unique_ptr<Expr> parseOr(ParseState& s) const;
    std::unique_ptr<Expr> parseAnd(ParseState& s) const;
    std::unique_ptr<Expr> parseEquality(ParseState& s) const;
    std::unique_ptr<Expr> parseRelational(ParseState& s) const;
    std::unique_ptr<Expr> parseAddSub(ParseState& s) const;
    std::unique_ptr<Expr> parseMulDiv(ParseState& s) const;
    std::unique_ptr<Expr> parsePower(ParseState& s) const;
    std::unique_ptr<Expr> parseUnary(ParseState& s) const;
    std::unique_ptr<Expr> parsePrimary(ParseState& s) const;
    std::unique_ptr<Expr> parseCall(const std::string& name, ParseState& s) const;

    void skipWS(ParseState& s) const;
    bool match(ParseState& s, const char* tok) const;
    char peek(const ParseState& s) const;
};
