#pragma once
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Value type for all reactive variables (numeric).
// String-valued derives are stored as a separate parallel map.
using Value = double;
using ChangeCallback = std::function<void(const std::string& name, Value newVal)>;

// ─── VarStore ────────────────────────────────────────────────────────────────
//
// Holds @var, @const, and @derive declarations.
// Maintains a dependency graph (DAG) so that when any mutable variable
// changes, all downstream derives are recomputed in topological order.
//
class VarStore {
public:
    // ── Registration ─────────────────────────────────────────────────────────

    // Register a mutable variable with its default value.
    void declareVar(const std::string& name, Value defaultValue);

    // Register an immutable constant.
    void declareConst(const std::string& name, Value value);

    // Register a derived expression.  deps = names of vars/derives it reads.
    // The expression string is stored; evaluation is delegated to ExprEvaluator.
    void declareDerive(const std::string& name,
                       const std::string& expr,
                       const std::set<std::string>& deps);

    // ── Read / Write ──────────────────────────────────────────────────────────

    Value       get(const std::string& name) const;
    bool        has(const std::string& name) const;
    bool        isConst(const std::string& name) const;

    // Update a @var.  Triggers reactive recomputation of all dependents.
    void set(const std::string& name, Value newValue);

    // ── Reactivity ───────────────────────────────────────────────────────────

    // Subscribe to any variable change (including derived updates).
    void subscribe(ChangeCallback cb);

    // Force full recomputation of all derives (e.g. after document load).
    void recomputeAll();

    // ── Dependency graph query ────────────────────────────────────────────────

    // Returns names that must be recomputed when `name` changes.
    const std::set<std::string>& downstream(const std::string& name) const;

    // Returns names that `name` reads.
    const std::set<std::string>& upstream(const std::string& name) const;

    // Topologically sorted list of all derives (evaluation order).
    std::vector<std::string> evalOrder() const;

private:
    enum class Kind { Var, Const, Derive };

    struct Entry {
        Kind        kind;
        Value       value   = 0.0;
        std::string expr;                      // non-empty for Derive
        std::set<std::string> reads;           // upstream deps
        std::set<std::string> writes;          // downstream deps (back-edges)
    };

    std::unordered_map<std::string, Entry> store_;
    std::vector<ChangeCallback>            listeners_;

    void recompute(const std::string& name);  // recompute + propagate
    void notify(const std::string& name, Value val);

    // Throws if a dependency cycle is detected.
    void checkCycle(const std::string& name,
                    std::set<std::string>& visited,
                    std::set<std::string>& inStack) const;
};
