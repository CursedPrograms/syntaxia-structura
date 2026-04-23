#pragma once
#include <memory>
#include "document.hpp"
#include "expr_eval.hpp"

// ─── RenderTarget ─────────────────────────────────────────────────────────────
//
// Abstract output surface.  Concrete backends: terminal, SDL2 window, PDF.
//
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    virtual void beginDocument(const std::string& title) = 0;
    virtual void endDocument() = 0;

    virtual void heading(int level, const std::string& text) = 0;
    virtual void paragraph(const std::string& text) = 0;
    virtual void hrule() = 0;
    virtual void mathBlock(const std::string& raw) = 0;
    virtual void tableBegin() = 0;
    virtual void tableRow(const std::vector<std::string>& cells, bool isHeader) = 0;
    virtual void tableEnd() = 0;

    // Widgets — the target decides how to render them (GUI vs. static)
    virtual void widgetSlider(const std::string& label, double value,
                              double min, double max, double step) = 0;
    virtual void widgetDisplay(const std::string& label,
                               const std::string& formatted) = 0;
    virtual void widgetInput(const std::string& label,
                             const std::string& value) = 0;
};

// ─── Renderer ─────────────────────────────────────────────────────────────────
//
// Walks Document::nodes(), resolves reactive expressions via ExprEvaluator,
// and dispatches draw calls to a RenderTarget.
//
// Usage:
//   auto doc = Document::load("ocean_pressure.syn");
//   TerminalTarget out;
//   Renderer r(out);
//   r.render(*doc);
//
//   // User drags slider → update var → re-render changed nodes:
//   doc->varStore().set("depth", 3500.0);
//   r.renderDirty(*doc);   // only nodes whose deps changed
//
class Renderer {
public:
    explicit Renderer(RenderTarget& target);

    // Full document render.
    void render(Document& doc);

    // Partial re-render: only nodes that depend on changed vars.
    // Call after VarStore::set() to get reactive updates.
    void renderDirty(Document& doc);

private:
    RenderTarget&                   target_;
    std::unique_ptr<ExprEvaluator>  eval_;

    void renderNode(DocNode& node);
    std::string resolveSpan(const TextSpan& span) const;
    std::string formatValue(double val, const std::string& fmt) const;

    // Track which document regions depend on which vars for dirty tracking.
    // region index → set of var names
    std::vector<std::pair<size_t, std::string>> regionDeps_;
    std::set<std::string>                       dirtyVars_;
};

// ─── TerminalTarget ───────────────────────────────────────────────────────────
//
// Simple ANSI terminal renderer.  Widgets print their current value.
//
class TerminalTarget : public RenderTarget {
public:
    void beginDocument(const std::string& title) override;
    void endDocument() override;
    void heading(int level, const std::string& text) override;
    void paragraph(const std::string& text) override;
    void hrule() override;
    void mathBlock(const std::string& raw) override;
    void tableBegin() override;
    void tableRow(const std::vector<std::string>& cells, bool isHeader) override;
    void tableEnd() override;
    void widgetSlider(const std::string& label, double value,
                      double min, double max, double step) override;
    void widgetDisplay(const std::string& label,
                       const std::string& formatted) override;
    void widgetInput(const std::string& label,
                     const std::string& value) override;
};
