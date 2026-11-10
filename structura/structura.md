# Structura — Reactive Document Syntax Specification

## Concept

A `.syn` file is a **live document** — not a script, not a webpage.
It mixes prose, math, and UI elements into a single reactive surface.
Every value is live. Every expression updates automatically.

---

## Document Directives

```
@title  My Document
@author Name
```

---

## Variable Declarations

```
@var   name = default_value       -- mutable, bindable to sliders/inputs
@const name = value               -- immutable constant
@derive name = expression         -- computed; updates when dependencies change
```

Expressions support:
- Arithmetic: `+ - * / % **`
- Math constants: `π`
- Math functions: `√(x)`, `sin(x)`, `cos(x)`, `log(x)`, `abs(x)`
- Comparisons: `< > <= >= == !=`
- Logical: `&& || !`
- Ternary: `condition ? value_if_true : value_if_false`
- Type conversion: `int(x)`, `float(x)`

---

## Inline Reactive Expressions

Anywhere in body text:

```
{{expression}}           -- evaluate and insert result
{{expression:.2f}}       -- evaluate with Python format spec
{{expression:unit}}      -- evaluate and append unit string
```

---

## UI Widgets

```
[slider: var_name, min=X, max=Y, step=Z, label="Text"]
[input:  var_name, type=number|text, label="Text"]
[toggle: var_name, label="Text"]
[button: label="Text", set var_name = expression]
[display: expression, label="Text", format="fmt"]
[plot: x_var, y_expr, x_min=X, x_max=Y, label="Title"]
```

Widgets bind directly to `@var` names. Changing a slider immediately
propagates through the dependency graph and re-renders all `{{expr}}`
that depend on it.

---

## Math Blocks

Display-mode math — not evaluated, just rendered:

```
$$
  P(d) = P₀ + ρ₀ · g · d
  ρ(d) = ρ₀ · (1 + P / K)
$$
```

---

## Structure

```
# Heading 1
## Heading 2
### Heading 3

---           (horizontal rule / section separator)

Regular paragraph text with {{inline expressions}}.

| Col A | Col B       |
|-------|-------------|
| Base  | {{base}}    |
| Tax   | {{tax:.2f}} |
```

---

## Reactive Model

```
@var  → initial value, user-adjustable via widgets
@const → fixed, never recomputed
@derive → expression re-evaluated in topological order when any dep changes
```

Dependency graph:

```
@var depth = 0
@const rho0 = 1025
@derive P = rho0 * 9.81 * depth        -- depends on: depth, rho0
@derive rho = rho0 * (1 + P / 2.2e9)  -- depends on: rho0, P
```

Change `depth` → recompute `P` → recompute `rho` → update all `{{P}}`, `{{rho}}`
in the rendered document.

---

## Stories (choose your own adventure)

A Structura document can be a game book: pages are scenes, choices are buttons on the page,
and `@var`s are the player's state (battery, inventory, score). It's still a reactive document:
taking a choice changes variables, and the whole document follows them.

```
@mode book                        -- show one scene at a time; only choices turn the pages
@scene name                       -- a new scene starts here, on a page of its own
[choice: "Text", goto=name, set var = expr, if=cond]
[if: expr]  ...  [else]  ...  [endif]
```

**`@scene name`**: everything after it, up to the next `@scene`, is that scene. A long scene runs
onto more pages and stays one scene. The first scene shares its page with `@title` / `@author`,
and it's where the story starts.

**`[choice: ...]`**: a button on the page. Every part after the label is optional:

| Part | What it does |
|------|--------------|
| `goto=name` | go to that scene |
| `set var = expr` | change a variable; several are allowed, applied in order, each one seeing the ones before it |
| `if=cond` | the choice only shows while `cond` is true |

The label can hold `{{expressions}}`. The parts are separated by commas, so an expression inside a
choice can't contain one (use `a > b ? a : b`, not `max(a, b)`).

**`[if: expr]` ... `[else]` ... `[endif]`**: the lines in between show only while `expr` is true
(the `[else]` part while it's false). They nest, and they can hold text, headings, tables and
choices. Declare `@var`s at the top, not inside an `[if:]`.

**Reading** (SynReader): click a choice, or press its number (1-9). F5 starts the story again with
every variable at its first value. In book mode the page arrows are off, and the arrow keys only
scroll the current scene. Without `@mode book` the scenes are ordinary pages, and a choice scrolls
to its scene.

```syn
@title The Cellar
@mode book
@var hasKey = 0

@scene start
# The Hallway
A drawer, and stairs going down.
[choice: "Search the drawer", goto=drawer]
[choice: "Go down to the cellar", goto=cellar]

@scene drawer
[if: hasKey == 0]
A rusty key lies inside!
[choice: "Take it", set hasKey = 1, goto=start]
[else]
The drawer is empty.
[endif]
[choice: "Back", goto=start]

@scene cellar
[if: hasKey]
The key fits. YOU WIN!
[else]
A locked door. You need a key.
[choice: "Go back up", goto=start]
[endif]
```

A whole story with several endings: `examples/spider_queen.syn`.

Stories are Structura, not Syntaxia: the `<func>` / `<var>` / `<if>` script tags don't take part.
So far only SynReader plays them. StructuraMaker, StructArch, the npm package, the pip package
and the browser extension show a story's text but not its choices or scenes.

---

## Full Example

```syn
@title Ohm's Law Explorer

# Ohm's Law  — V = IR

@var voltage = 12
@var resistance = 100

@derive current = voltage / resistance
@derive power   = voltage * current

[slider: voltage,    min=1,   max=50,  step=1,   label="Voltage (V)"]
[slider: resistance, min=10,  max=500, step=10,  label="Resistance (Ω)"]

With V = **{{voltage}} V** and R = **{{resistance}} Ω**:

$$
  I = V / R = {{voltage}} / {{resistance}}
  P = V × I = {{voltage}} × {{current:.4f}}
$$

- Current:  **{{current:.4f}} A**  ({{current * 1000:.2f}} mA)
- Power:    **{{power:.3f}} W**
```
