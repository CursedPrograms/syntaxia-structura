#pragma once
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "var_store.hpp"

// ─── Document node types ─────────────────────────────────────────────────────

// A span of text, possibly containing {{expr}} interpolations.
struct TextSpan {
    struct Literal  { std::string text; };
    struct Reactive { std::string expr; std::string fmt; };  // {{expr:fmt}}
    using Chunk = std::variant<Literal, Reactive>;

    std::vector<Chunk> chunks;

    static TextSpan parse(const std::string& raw);  // splits on {{...}}
};

// ─── Block nodes (the document tree) ─────────────────────────────────────────

struct DocNode { virtual ~DocNode() = default; };

struct HeadingNode : DocNode {
    int       level;   // 1–3
    TextSpan  text;
};

struct ParagraphNode : DocNode {
    TextSpan  text;
};

struct HRuleNode : DocNode {};

struct MathBlockNode : DocNode {
    std::string raw;   // raw display math (rendered typeset, not evaluated)
};

struct TableRow {
    std::vector<TextSpan> cells;
    bool isHeader = false;
};

struct TableNode : DocNode {
    std::vector<TableRow> rows;
};

struct VarDeclNode : DocNode {
    enum class Kind { Var, Const, Derive };
    Kind        kind;
    std::string name;
    std::string expr;
};

// UI widget kinds
struct WidgetNode : DocNode {
    enum class Kind { Slider, Input, Toggle, Button, Display, Plot };
    Kind        kind;
    std::string targetVar;   // @var name being bound
    std::string expr;        // for Display/Button/Plot
    std::string label;
    std::string fmt;
    double      min = 0, max = 100, step = 1;
};

// ─── Document ─────────────────────────────────────────────────────────────────
//
// Owns the parsed node tree and a VarStore.
// The Renderer traverses Document::nodes() and renders each block.
//
class Document {
public:
    // Parse a .syn source string.  Populates nodes + varStore.
    static std::unique_ptr<Document> parse(const std::string& source);

    // Load a .syn file from disk.
    static std::unique_ptr<Document> load(const std::string& path);

    const std::vector<std::unique_ptr<DocNode>>& nodes() const { return nodes_; }
    VarStore& varStore() { return vars_; }
    const VarStore& varStore() const { return vars_; }

    const std::string& title() const   { return title_; }
    const std::string& author() const  { return author_; }

private:
    std::string                             title_;
    std::string                             author_;
    std::vector<std::unique_ptr<DocNode>>   nodes_;
    VarStore                                vars_;
};
