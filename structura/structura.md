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
