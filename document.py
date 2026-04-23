#!/usr/bin/env python3
"""Structura document renderer — Python prototype.

Parses the reactive .syn document format and renders it to the terminal
with all {{expr}} values evaluated at their default variable settings.

Usage:
    python document.py game_store.syn
    python document.py ocean_pressure.syn
    python document.py game_store.syn depth=3500
"""

import re
import sys
import os
import math

# ─── Expression evaluator ────────────────────────────────────────────────────

def _build_ns(store):
    ns = {
        'math': math,
        'π': math.pi,
        'e': math.e,
        '__builtins__': {
            'abs': abs, 'round': round, 'int': int, 'float': float,
            'str': str, 'bool': bool, 'len': len, 'min': min, 'max': max,
            'True': True, 'False': False,
        },
    }
    ns.update({k: v for k, v in store.items() if not k.startswith('__')})
    return ns


def syn_eval(expr, store):
    """Evaluate a Syntaxia expression string against store."""
    expr = expr.strip()
    if not expr:
        return None
    expr = expr.replace('π', str(math.pi))
    # √(expr) or √var
    expr = re.sub(r'√\(([^)]+)\)', r'math.sqrt(\1)', expr)
    expr = re.sub(r'√([a-zA-Z_]\w*)', r'math.sqrt(\1)', expr)
    expr = re.sub(r'√(\d+(?:\.\d+)?)', lambda m: f'math.sqrt({m.group(1)})', expr)
    # && || !
    expr = expr.replace('&&', ' and ').replace('||', ' or ')
    expr = re.sub(r'!(?!=)', ' not ', expr)
    # Ternary: cond ? a : b  →  a if cond else b
    expr = _convert_ternary(expr)
    ns = _build_ns(store)
    try:
        return eval(expr, ns)  # noqa: S307
    except Exception as exc:
        raise RuntimeError(f"eval error in '{expr}': {exc}") from exc


def _convert_ternary(expr):
    """Convert C-style ternary a ? b : c to Python a if b else c (innermost first)."""
    # Handle nested ternaries by repeated innermost substitution.
    # Pattern: non-nested ternary (no ? inside sub-expressions).
    # We rely on Python strings not containing ? so this is safe for our use case.
    pattern = re.compile(r'([^?:]+)\?([^?:]+):([^?:]+)')
    for _ in range(10):
        new = pattern.sub(r'(\2) if (\1) else (\3)', expr)
        if new == expr:
            break
        expr = new
    return expr


def _apply_fmt(val, fmt):
    """Apply a format string (Python spec or C printf-style with prefix/suffix)."""
    if not fmt:
        return fmt_val(val)
    m = re.search(r'%(\.\d+)?([dfsge])', fmt)
    if m:
        py_fmt = (m.group(1) or '') + m.group(2)
        prefix = fmt[:m.start()]
        suffix = fmt[m.end():]
        return prefix + format(float(val), py_fmt) + suffix
    try:
        return format(val, fmt)
    except (TypeError, ValueError):
        return str(val)


def fmt_val(val, fmt=''):
    """Format a value with an optional Python format spec."""
    if fmt:
        try:
            return format(val, fmt)
        except (TypeError, ValueError):
            return str(val)
    if isinstance(val, float):
        if val == int(val):
            return str(int(val))
        return f'{val:.4f}'.rstrip('0').rstrip('.')
    return str(val)


def interpolate(text, store):
    """Replace all {{expr}} and {{expr:fmt}} in text with evaluated values."""
    def replace(m):
        inner = m.group(1)
        if ':' in inner:
            parts = inner.split(':', 1)
            expr, fmt = parts[0].strip(), parts[1].strip()
        else:
            expr, fmt = inner.strip(), ''
        try:
            val = syn_eval(expr, store)
            return fmt_val(val, fmt)
        except RuntimeError:
            return f'{{{{{inner}}}}}'  # leave as-is if eval fails
    return re.sub(r'\{\{([^}]+)\}\}', replace, text)


# ─── Parser ───────────────────────────────────────────────────────────────────

def parse_document(source):
    """Parse a .syn source into (meta, var_decls, nodes).

    meta      : dict of @title, @author etc.
    var_decls : list of (kind, name, expr) — 'var'/'const'/'derive'
    nodes     : list of (type, data) render nodes
    """
    meta = {}
    var_decls = []
    nodes = []

    lines = source.splitlines()
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i]
        stripped = line.strip()

        # Meta directives
        if stripped.startswith('@title '):
            meta['title'] = stripped[7:].strip()
            i += 1; continue
        if stripped.startswith('@author '):
            meta['author'] = stripped[8:].strip()
            i += 1; continue
        if stripped.startswith('@description '):
            meta['description'] = stripped[13:].strip()
            i += 1; continue

        # Variable declarations
        if stripped.startswith('@var ') or stripped.startswith('@const ') or stripped.startswith('@derive '):
            kind, rest = stripped.split(None, 1)
            kind = kind.lstrip('@')
            if '=' in rest:
                name, expr = rest.split('=', 1)
                # Strip inline comments (-- ...)
                expr = re.sub(r'\s*--.*$', '', expr).strip()
                var_decls.append((kind, name.strip(), expr.strip()))
            i += 1; continue

        # Math block $$...$$
        if stripped == '$$':
            math_lines = []
            i += 1
            while i < n and lines[i].strip() != '$$':
                math_lines.append(lines[i])
                i += 1
            nodes.append(('math', '\n'.join(math_lines)))
            i += 1; continue

        # Horizontal rule
        if stripped == '---':
            nodes.append(('hr', None))
            i += 1; continue

        # Headings
        m = re.match(r'^(#{1,3})\s+(.*)', stripped)
        if m:
            nodes.append(('heading', len(m.group(1)), m.group(2).strip()))
            i += 1; continue

        # Table
        if stripped.startswith('|'):
            table_rows = []
            while i < n and lines[i].strip().startswith('|'):
                row = lines[i].strip()
                # Skip separator rows like |---|---|
                if re.match(r'^\|[-| :]+\|$', row):
                    if table_rows:
                        table_rows[-1] = (table_rows[-1][0], True)  # mark prev as header
                    i += 1
                    continue
                cells = [c.strip() for c in row.strip('|').split('|')]
                table_rows.append((cells, False))
                i += 1
            nodes.append(('table', table_rows))
            continue

        # Widget line
        if stripped.startswith('[') and stripped.endswith(']'):
            nodes.append(('widget', _parse_widget(stripped)))
            i += 1; continue

        # Empty line
        if not stripped:
            nodes.append(('blank', None))
            i += 1; continue

        # Paragraph / text
        nodes.append(('text', stripped))
        i += 1

    return meta, var_decls, nodes


def _parse_widget(s):
    """Parse [kind: varname, key=val, ...] into a dict."""
    inner = s[1:-1]
    if ':' in inner:
        kind_part, rest = inner.split(':', 1)
    else:
        kind_part, rest = inner, ''
    widget = {'kind': kind_part.strip()}
    parts = [p.strip() for p in rest.split(',') if p.strip()]
    if parts:
        widget['target'] = parts[0]
    for part in parts[1:]:
        if '=' in part:
            k, v = part.split('=', 1)
            k, v = k.strip(), v.strip().strip('"').strip("'")
            try:
                widget[k] = float(v) if '.' in v else int(v)
            except ValueError:
                widget[k] = v
    return widget


# ─── Variable resolution ─────────────────────────────────────────────────────

def build_store(var_decls, overrides=None):
    """Evaluate all var/const/derive declarations, return the variable store.

    Derives are evaluated in multiple passes to resolve dependency order.
    """
    store = {}

    # First pass: set vars and consts
    for kind, name, expr in var_decls:
        if kind in ('var', 'const'):
            try:
                store[name] = syn_eval(expr, store)
            except RuntimeError:
                store[name] = 0.0

    # Apply CLI overrides to @var only
    if overrides:
        for name, val in overrides.items():
            if name in store:
                store[name] = val

    # Multi-pass derive evaluation (handles dependency chains)
    derives = [(name, expr) for kind, name, expr in var_decls if kind == 'derive']
    for _ in range(len(derives) + 2):
        changed = False
        for name, expr in derives:
            try:
                new_val = syn_eval(expr, store)
                if store.get(name) != new_val:
                    store[name] = new_val
                    changed = True
            except RuntimeError:
                pass
        if not changed:
            break

    return store


# ─── Renderer ─────────────────────────────────────────────────────────────────

BOLD  = '\033[1m'
DIM   = '\033[2m'
CYAN  = '\033[96m'
GREEN = '\033[92m'
RESET = '\033[0m'
LINE  = '─' * 60


def _md_bold(text):
    """Convert **text** and *text* to ANSI bold/dim for terminal."""
    text = re.sub(r'\*\*(.+?)\*\*', BOLD + r'\1' + RESET, text)
    text = re.sub(r'\*(.+?)\*', DIM + r'\1' + RESET, text)
    return text


def render(meta, var_decls, nodes, store):
    """Render the document to stdout."""
    title = meta.get('title', 'Untitled')
    print()
    print(f'{BOLD}{CYAN}{"═" * 60}{RESET}')
    print(f'{BOLD}{CYAN}  {title}{RESET}')
    if 'author' in meta:
        print(f'{DIM}  by {meta["author"]}{RESET}')
    if 'description' in meta:
        print(f'{DIM}  {meta["description"]}{RESET}')
    print(f'{BOLD}{CYAN}{"═" * 60}{RESET}')
    print()

    for node in nodes:
        kind = node[0]

        if kind == 'blank':
            print()

        elif kind == 'hr':
            print(f'{DIM}{LINE}{RESET}')
            print()

        elif kind == 'heading':
            _, level, text = node
            text = interpolate(text, store)
            text = _md_bold(text)
            prefix = '#' * level + ' '
            size = [BOLD + CYAN, BOLD, '']
            print(f'{size[level - 1]}{prefix}{text}{RESET}')
            print()

        elif kind == 'text':
            _, text = node
            text = interpolate(text, store)
            text = _md_bold(text)
            print(text)

        elif kind == 'math':
            _, raw = node
            print(f'{DIM}  ┌── math ─────────────────────────────{RESET}')
            for line in raw.splitlines():
                interp = interpolate(line, store)
                print(f'{DIM}  │  {interp}{RESET}')
            print(f'{DIM}  └────────────────────────────────────{RESET}')
            print()

        elif kind == 'table':
            _, rows = node
            _render_table(rows, store)
            print()

        elif kind == 'widget':
            _, w = node
            _render_widget(w, store)

    print()


def _render_table(rows, store):
    col_widths = []
    rendered = []
    for cells, is_header in rows:
        row_rendered = [interpolate(_md_bold(c), store) for c in cells]
        rendered.append((row_rendered, is_header))
        for j, cell in enumerate(row_rendered):
            visible = re.sub(r'\033\[[0-9;]*m', '', cell)
            while len(col_widths) <= j:
                col_widths.append(0)
            col_widths[j] = max(col_widths[j], len(visible))

    for cells, is_header in rendered:
        parts = []
        for j, cell in enumerate(cells):
            visible_len = len(re.sub(r'\033\[[0-9;]*m', '', cell))
            pad = col_widths[j] - visible_len
            parts.append(cell + ' ' * pad)
        row_str = '  ' + ' │ '.join(parts)
        if is_header:
            print(f'{BOLD}{row_str}{RESET}')
            sep = '  ' + '─┼─'.join('─' * w for w in col_widths)
            print(f'{DIM}{sep}{RESET}')
        else:
            print(row_str)


def _render_widget(w, store):
    kind = w.get('kind', '')
    label = w.get('label', w.get('target', ''))

    if kind == 'slider':
        var = w.get('target', '')
        val = store.get(var, 0)
        mn, mx = w.get('min', 0), w.get('max', 100)
        bar_len = 30
        frac = (val - mn) / (mx - mn) if mx != mn else 0
        filled = int(frac * bar_len)
        bar = '█' * filled + '░' * (bar_len - filled)
        print(f'{GREEN}  [{bar}] {fmt_val(val)}{RESET}  {DIM}{label}{RESET}')

    elif kind == 'display':
        expr = w.get('expr', w.get('target', ''))
        fmt = w.get('format', w.get('fmt', ''))
        try:
            val = syn_eval(expr, store)
            text = _apply_fmt(val, fmt)
        except Exception:
            text = str(store.get(expr, expr))
        print(f'{BOLD}{CYAN}  ▶ {label}: {text}{RESET}')

    elif kind == 'toggle':
        var = w.get('target', '')
        val = store.get(var, 0)
        box = '☑' if val else '☐'
        style = BOLD + GREEN if val else DIM
        print(f'  {style}{box}  {label}{RESET}')

    elif kind == 'input':
        var = w.get('target', '')
        val = store.get(var, '')
        print(f'{DIM}  [INPUT] {label}: {val}{RESET}')


# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

    args = sys.argv[1:]
    if not args or args[0] in ('-h', '--help'):
        print('usage: document.py <file.syn> [var=value ...]')
        sys.exit(0)

    path = args[0]
    if not os.path.exists(path):
        print(f'error: file not found: {path}', file=sys.stderr)
        sys.exit(1)

    # Parse CLI overrides: depth=3500 base_price=40 etc.
    overrides = {}
    for arg in args[1:]:
        if '=' in arg:
            k, v = arg.split('=', 1)
            try:
                overrides[k.strip()] = float(v.strip())
            except ValueError:
                pass

    with open(path, encoding='utf-8') as f:
        source = f.read()

    meta, var_decls, nodes = parse_document(source)
    store = build_store(var_decls, overrides)
    render(meta, var_decls, nodes, store)


if __name__ == '__main__':
    main()
