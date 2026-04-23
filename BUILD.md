# Building Structura — C++ Renderer & Maker

## What you are building

Two C++ applications:

| App | File | Purpose |
|-----|------|---------|
| **Structura Maker** | `maker/StructuraMaker.exe` | Visual block editor — create `.syn` files without writing code |
| **Structura Viewer** | `renderer/structura-view.exe` | Reactive runtime — opens a `.syn` file as a live interactive document |

---

<@>math_utils.syn@</@>

<<def CalculatePremium>>
    <var name="base" type="float">200.00</var>
    <var name="age_factor" type="float">1.0</var>

    <if condition="user_age > 50">
        <derive name="age_factor">1.8</derive>
        <if condition="smoker == true">
            <derive name="age_factor">age_factor * 2.5</derive>
        </if>
    </else>
        <if condition="user_age < 25">
            <derive name="age_factor">1.3</derive>
        </if>
    </if>

    <var name="monthly" type="float">base * age_factor</var>
    <var name="annual" type="float">monthly * 12.0 * 0.95</var> <print>Monthly Premium: $monthly</print>
    <print>Annual Total (Discounted): $annual</print>
<>/def>>

## Structura Maker (Win32, no dependencies)

The maker is a single-source Win32 application.
It is **already compiled** at `maker/StructuraMaker.exe`.

### Features
- Block-based document editor (Add / reorder / delete elements)
- Element palette: Heading · Paragraph · Divider · Variable · Toggle · Constant · Derive · Math Block · Display · Table
- Context-sensitive properties panel (right pane, updates per element type)
- Live `.syn` preview (bottom pane, updates as you type)
- Open existing `.syn` files (round-trip parser)
- Save / Export `.syn`
- Meta dialog for @title / @author / @description
- Open a file on the command line: `StructuraMaker.exe game_store.syn`

### Rebuild from source

Requirements: any C++ compiler that targets Win32 (MinGW GCC 6+ or MSVC 2019+).
No external libraries required — only Windows system DLLs.

**MinGW:**
```bash
g++ -std=c++14 -mwindows -O2 -o maker/StructuraMaker.exe maker/maker.cpp \
    -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lole32
```

**MSVC (x64 Native Tools Command Prompt):**
```bat
cl /std:c++14 /O2 /W3 maker\maker.cpp /Fe:maker\StructuraMaker.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib comctl32.lib comdlg32.lib gdi32.lib ole32.lib
```

### Source layout
```
maker/
├── maker.cpp          ← entire application (~820 lines)
└── StructuraMaker.exe ← pre-built binary
```

---

## Structura Viewer (Dear ImGui + SDL2)

The viewer is the interactive document runtime — it opens `.syn` files and
renders them as live documents with working sliders, toggles, and reactive
`{{expressions}}`.

---

## Prerequisites (Viewer only)

| Tool | Version | Why |
|------|---------|-----|
| C++ compiler | GCC 11+ / Clang 14+ / MSVC 2022 | C++17 required |
| CMake | 3.22+ | Build system |
| GUI backend (pick one) | see below | Renders the window |
| pkg-config | any | Finds libraries on Linux/macOS |

### GUI backend options (pick one to start)

| Backend | Complexity | Look | Notes |
|---------|-----------|------|-------|
| **Dear ImGui + SDL2** | Low | Modern dark | Fastest to prototype with |
| **Qt 6** | Medium | Native | Best long-term for a real app |
| **SFML + ImGui** | Low | Custom | Good if you want full draw control |
| **wxWidgets** | Medium | Native | Cross-platform native widgets |

**Recommendation for prototype:** Dear ImGui + SDL2.
Install: `vcpkg install imgui[sdl2-binding,opengl3-binding] sdl2`

---

## Project structure

```
syntaxia-structura/
├── renderer/
│   ├── CMakeLists.txt          ← build file
│   ├── main.cpp                ← entry point (skeleton exists)
│   ├── document.hpp / .cpp     ← .syn parser → node tree
│   ├── var_store.hpp / .cpp    ← reactive variable graph
│   ├── expr_eval.hpp / .cpp    ← math expression evaluator
│   ├── renderer.hpp / .cpp     ← walks node tree → draw calls
│   └── gui/
│       ├── imgui_backend.cpp   ← Dear ImGui render target
│       └── widgets.cpp         ← slider, toggle, display widgets
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(structura VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(structura-view
    main.cpp
    document.cpp
    var_store.cpp
    expr_eval.cpp
    renderer.cpp
    gui/imgui_backend.cpp
    gui/widgets.cpp
)

target_link_libraries(structura-view PRIVATE
    SDL2::SDL2
    imgui::imgui
)
```

Build:
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
./build/structura-view game_store.syn
```

---

## Implementation order

### Step 1 — ExprEvaluator  (`expr_eval.cpp`)

This is the foundation. Everything depends on it.

Implement a **recursive-descent parser** for this grammar:

```
expr     → ternary
ternary  → or ('?' or ':' or)*
or       → and ('||' and)*
and      → eq ('&&' eq)*
eq       → rel (('=='|'!=') rel)*
rel      → add (('<'|'>'|'<='|'>=') add)*
add      → mul (('+' | '-') mul)*
mul      → pow (('*' | '/' | '%') pow)*
pow      → unary ('**' unary)*
unary    → ('-' | '!' | '√') unary | primary
primary  → NUMBER | STRING | 'π' | IDENT | call | '(' expr ')'
call     → IDENT '(' expr (',' expr)* ')'
```

Built-in functions to register: `sin cos tan sqrt log exp abs floor ceil round min max`

Constants to pre-load: `π = 3.14159265358979`

Key method signatures:
```cpp
double ExprEvaluator::evalNum(const std::string& expr) const;
std::string ExprEvaluator::evalStr(const std::string& expr) const;
std::set<std::string> ExprEvaluator::dependencies(const std::string& expr) const;
```

Test it first in isolation before touching the rest.

---

### Step 2 — VarStore  (`var_store.cpp`)

```cpp
void VarStore::declareVar(const std::string& name, double defaultVal) {
    store_[name] = {Kind::Var, defaultVal, "", {}};
}

void VarStore::declareDerive(const std::string& name,
                              const std::string& expr,
                              const std::set<std::string>& deps) {
    store_[name] = {Kind::Derive, 0.0, expr, deps};
    // Add back-edges: for each dep d, d.writes.insert(name)
    for (auto& d : deps)
        if (store_.count(d)) store_[d].writes.insert(name);
}

void VarStore::set(const std::string& name, double val) {
    if (isConst(name)) throw std::runtime_error("cannot set const");
    store_[name].value = val;
    notify(name, val);
    recompute(name);  // propagate downstream
}
```

Recompute uses topological order:
```cpp
void VarStore::recompute(const std::string& changed) {
    // BFS/DFS from changed through the writes graph
    // For each reached Derive node, re-evaluate its expr
    auto order = topologicalOrder();  // cached or rebuilt
    ExprEvaluator eval(*this);
    for (auto& name : order) {
        auto& e = store_[name];
        if (e.kind == Kind::Derive) {
            e.value = eval.evalNum(e.expr);
            notify(name, e.value);
        }
    }
}
```

Topological sort — standard DFS:
```cpp
std::vector<std::string> VarStore::topologicalOrder() const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    std::function<void(const std::string&)> dfs = [&](const std::string& n) {
        if (visited.count(n)) return;
        visited.insert(n);
        for (auto& dep : store_.at(n).reads) dfs(dep);
        result.push_back(n);
    };
    for (auto& [name, _] : store_) dfs(name);
    return result;
}
```

---

### Step 3 — Document parser  (`document.cpp`)

Parse `.syn` line by line. Map each line pattern to a node type:

```cpp
std::unique_ptr<Document> Document::parse(const std::string& source) {
    auto doc = std::make_unique<Document>();
    std::istringstream ss(source);
    std::string line;
    while (std::getline(ss, line)) {
        auto stripped = trim(line);

        if (startsWith(stripped, "@title "))
            { doc->title_ = stripped.substr(7); continue; }

        if (startsWith(stripped, "@var "))
            { parseVarDecl(stripped, *doc, VarDeclNode::Kind::Var); continue; }

        if (startsWith(stripped, "@const "))
            { parseVarDecl(stripped, *doc, VarDeclNode::Kind::Const); continue; }

        if (startsWith(stripped, "@derive "))
            { parseVarDecl(stripped, *doc, VarDeclNode::Kind::Derive); continue; }

        if (stripped == "$$")
            { parseMathBlock(ss, *doc); continue; }

        if (stripped == "---")
            { doc->nodes_.push_back(std::make_unique<HRuleNode>()); continue; }

        if (stripped[0] == '#')
            { doc->nodes_.push_back(parseHeading(stripped)); continue; }

        if (stripped[0] == '[')
            { doc->nodes_.push_back(parseWidget(stripped)); continue; }

        if (stripped[0] == '|')
            { parseTable(ss, stripped, *doc); continue; }

        if (!stripped.empty())
            { doc->nodes_.push_back(parseParagraph(stripped)); continue; }
    }
    return doc;
}
```

`TextSpan::parse` — splits `"Total: **{{total:.2f}}**"` into `[Literal("Total: **"), Reactive("total", ".2f"), Literal("**")]`.

---

### Step 4 — Renderer  (`renderer.cpp`)

The renderer walks nodes and dispatches to the `RenderTarget`.

For each `TextSpan`, resolve it:
```cpp
std::string Renderer::resolveSpan(const TextSpan& span) const {
    std::string out;
    for (auto& chunk : span.chunks) {
        if (auto* lit = std::get_if<TextSpan::Literal>(&chunk))
            out += lit->text;
        else if (auto* rx = std::get_if<TextSpan::Reactive>(&chunk)) {
            double val = eval_->evalNum(rx->expr);
            out += formatValue(val, rx->fmt);
        }
    }
    return out;
}
```

For the dirty-rerender path, track which document regions (node indices)
reference which variables. When `VarStore::set()` fires a callback,
mark those regions dirty and re-render only them.

---

### Step 5 — GUI backend  (`gui/imgui_backend.cpp`)

With Dear ImGui + SDL2:

```cpp
// In the render loop:
while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) { ImGui_ImplSDL2_ProcessEvent(&e); }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin(doc->title().c_str());
    renderer.render(*doc);   // renders each node as ImGui calls
    ImGui::End();

    ImGui::Render();
    SDL_GL_SwapWindow(window);
}
```

Widget implementations:
```cpp
// Slider widget
void ImGuiTarget::widgetSlider(const std::string& varName,
                                double val, double min, double max) {
    float v = (float)val;
    if (ImGui::SliderFloat(varName.c_str(), &v, min, max))
        doc->varStore().set(varName, (double)v);  // triggers reactive update
}

// Toggle widget
void ImGuiTarget::widgetToggle(const std::string& varName, bool checked) {
    bool v = checked;
    if (ImGui::Checkbox(varName.c_str(), &v))
        doc->varStore().set(varName, v ? 1.0 : 0.0);
}
```

---

## The reactive loop in full

```
User drags slider
    │
    ▼
SDL2 mouse event
    │
    ▼
ImGui::SliderFloat returns new value
    │
    ▼
VarStore::set("depth", 3500.0)
    │
    ▼
VarStore::recompute("depth")
    │  topological walk through dependency graph
    ▼
re-evaluates:  P → rho → sound_speed → zone_label
    │
    ▼
each re-eval fires notify(name, newVal)
    │
    ▼
Renderer::renderDirty()
    │  only nodes whose TextSpans reference changed vars
    ▼
ImGui draws updated text in the same frame
    │
    ▼
SDL2 SwapBuffers → user sees new values instantly
```

---

## What already exists

| File | Status | Notes |
|------|--------|-------|
| `maker/maker.cpp` | ✅ complete source | Win32 visual editor |
| `maker/StructuraMaker.exe` | ✅ compiled binary | run directly |
| `renderer/var_store.hpp` | ✅ complete interface | implement `var_store.cpp` |
| `renderer/expr_eval.hpp` | ✅ complete interface | implement `expr_eval.cpp` |
| `renderer/document.hpp` | ✅ complete interface | implement `document.cpp` |
| `renderer/renderer.hpp` | ✅ complete interface | implement `renderer.cpp` |
| `renderer/main.cpp` | ✅ skeleton | fill in event loop |
| `document.py` | ✅ Python prototype | use as reference / test oracle |
| `structura.py` | ✅ script interpreter | runs old `.syn` scripting format |

---

## Suggested implementation milestones

| Milestone | Deliverable |
|-----------|-------------|
| M0 | ✅ **Maker** — `StructuraMaker.exe` already compiled and working |
| M1 | `ExprEvaluator` passes unit tests (π, √, &&, ternary) |
| M2 | `VarStore` with dependency graph + topological sort |
| M3 | `Document::parse` loads all `.syn` demo files |
| M4 | `TerminalTarget` renders a `.syn` doc to stdout (match `document.py` output) |
| M5 | SDL2 window opens, ImGui renders headings + paragraphs |
| M6 | Sliders and toggles work, values update reactively |
| M7 | Tables and math blocks render |
| M8 | `structura-view game_store.syn` fully interactive |

---

## Quick-start: terminal backend first

Before touching SDL2 / ImGui, implement `TerminalTarget` and verify the
renderer correctly walks all node types. The Python prototype in
`document.py` is the reference — match its output before adding GUI.
