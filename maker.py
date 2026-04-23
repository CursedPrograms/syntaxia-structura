#!/usr/bin/env python3
"""
Structura Maker — visual .syn document builder

Block-based editor: add, reorder, and configure elements via UI.
Generates and exports .syn files without writing code.

Usage:
    python maker.py
    python maker.py existing_file.syn
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import sys
import os
import re

# ─── Colour palette ──────────────────────────────────────────────────────────

C = {
    'bg':        '#1e1e2e',
    'panel':     '#181825',
    'surface':   '#313244',
    'surface2':  '#45475a',
    'text':      '#cdd6f4',
    'subtext':   '#a6adc8',
    'accent':    '#89b4fa',
    'green':     '#a6e3a1',
    'red':       '#f38ba8',
    'yellow':    '#f9e2af',
    'entry_bg':  '#1e1e2e',
    'entry_fg':  '#cdd6f4',
    'select_bg': '#45475a',
    'btn':       '#313244',
    'btn_act':   '#45475a',
}

# ─── Element catalogue ────────────────────────────────────────────────────────

PALETTE = [
    ('heading',  '# Heading',        C['accent']),
    ('text',     '¶ Paragraph',       C['text']),
    ('hr',       '— Divider',         C['subtext']),
    ('var',      '◈ Variable',        C['green']),
    ('toggle',   '☑ Toggle',          C['yellow']),
    ('const',    '■ Constant',        C['subtext']),
    ('derive',   '⇒ Derive',          C['accent']),
    ('math',     '∑ Math Block',      C['yellow']),
    ('display',  '▶ Display',         C['accent']),
    ('table',    '▦ Table',           C['green']),
]

LABEL_MAP = {k: v for k, v, _ in PALETTE}


def default_element(etype):
    d = {
        'heading': {'type': 'heading', 'level': '1', 'text': 'New Section'},
        'text':    {'type': 'text',    'content': 'Enter text here.'},
        'hr':      {'type': 'hr'},
        'var':     {'type': 'var',   'name': 'my_var', 'value': '0',
                    'widget': 'slider', 'min': '0', 'max': '100',
                    'step': '1', 'label': 'My Variable'},
        'toggle':  {'type': 'toggle', 'name': 'include_x', 'value': '1',
                    'label': 'Include X'},
        'const':   {'type': 'const', 'name': 'MY_K', 'value': '1.0'},
        'derive':  {'type': 'derive', 'name': 'result', 'expr': 'a + b'},
        'math':    {'type': 'math',   'content': 'y = f(x)'},
        'display': {'type': 'display', 'expr': 'result',
                    'label': 'Result', 'format': '%.2f'},
        'table':   {'type': 'table',
                    'headers': 'Item, Price, Total',
                    'rows': '{{item}}, {{price}}, {{total}}'},
    }
    return dict(d.get(etype, {'type': etype}))


# ─── .syn generator ───────────────────────────────────────────────────────────

def element_to_syn(el):
    t = el.get('type')
    lines = []

    if t == 'heading':
        prefix = '#' * int(el.get('level', 1))
        lines.append(f"{prefix} {el.get('text', '')}")

    elif t == 'text':
        lines.append(el.get('content', ''))

    elif t == 'hr':
        lines.append('---')

    elif t == 'var':
        name  = el.get('name', 'x')
        val   = el.get('value', '0')
        wtype = el.get('widget', 'slider')
        label = el.get('label', name)
        lines.append(f'@var {name} = {val}')
        if wtype == 'slider':
            mn, mx, st = el.get('min', '0'), el.get('max', '100'), el.get('step', '1')
            lines.append(f'[slider: {name}, min={mn}, max={mx}, step={st}, label="{label}"]')
        elif wtype == 'input':
            lines.append(f'[input: {name}, label="{label}"]')

    elif t == 'toggle':
        name  = el.get('name', 'flag')
        val   = el.get('value', '1')
        label = el.get('label', name)
        lines.append(f'@var {name} = {val}')
        lines.append(f'[toggle: {name}, label="{label}"]')

    elif t == 'const':
        name = el.get('name', 'K')
        val  = el.get('value', '1')
        lines.append(f'@const {name} = {val}')

    elif t == 'derive':
        name = el.get('name', 'result')
        expr = el.get('expr', '0')
        lines.append(f'@derive {name} = {expr}')

    elif t == 'math':
        lines.append('$$')
        for ln in el.get('content', '').splitlines():
            lines.append(f'  {ln}')
        lines.append('$$')

    elif t == 'display':
        expr  = el.get('expr', 'x')
        label = el.get('label', 'Value')
        fmt   = el.get('format', '')
        if fmt:
            lines.append(f'[display: {expr}, label="{label}", format="{fmt}"]')
        else:
            lines.append(f'[display: {expr}, label="{label}"]')

    elif t == 'table':
        headers = [h.strip() for h in el.get('headers', 'A, B').split(',')]
        sep     = ['---'] * len(headers)
        lines.append('| ' + ' | '.join(headers) + ' |')
        lines.append('| ' + ' | '.join(sep) + ' |')
        for row_src in el.get('rows', '').splitlines():
            if row_src.strip():
                cells = [c.strip() for c in row_src.split(',')]
                lines.append('| ' + ' | '.join(cells) + ' |')

    return '\n'.join(lines)


def document_to_syn(meta, elements):
    parts = []
    if meta.get('title'):
        parts.append(f"@title {meta['title']}")
    if meta.get('author'):
        parts.append(f"@author {meta['author']}")
    if meta.get('description'):
        parts.append(f"@description {meta['description']}")
    if parts:
        parts.append('')

    for el in elements:
        syn = element_to_syn(el)
        if syn:
            parts.append(syn)
            parts.append('')

    return '\n'.join(parts)


# ─── .syn loader (basic round-trip) ──────────────────────────────────────────

def syn_to_elements(source):
    """Best-effort parse of an existing .syn file back into element dicts."""
    meta = {}
    elements = []
    lines = source.splitlines()
    i, n = 0, len(lines)

    # Pre-scan: build a map of varName → widget info from [slider:] / [toggle:] lines
    widget_map = {}
    for line in lines:
        s = line.strip()
        m = re.match(r'\[slider:\s*(\w+),\s*(.*)\]', s)
        if m:
            info = {'widget': 'slider', 'min': '0', 'max': '100', 'step': '1', 'label': m.group(1)}
            for kv in m.group(2).split(','):
                kv = kv.strip()
                if '=' in kv:
                    k, v = kv.split('=', 1)
                    info[k.strip()] = v.strip().strip('"').strip("'")
            widget_map[m.group(1)] = info
            continue
        m2 = re.match(r'\[toggle:\s*(\w+),\s*(.*)\]', s)
        if m2:
            info = {'widget': 'toggle', 'label': m2.group(1)}
            for kv in m2.group(2).split(','):
                kv = kv.strip()
                if '=' in kv:
                    k, v = kv.split('=', 1)
                    info[k.strip()] = v.strip().strip('"').strip("'")
            widget_map[m2.group(1)] = info

    while i < n:
        raw = lines[i]
        s = raw.strip()

        if s.startswith('@title '):
            meta['title'] = s[7:].strip(); i += 1; continue
        if s.startswith('@author '):
            meta['author'] = s[8:].strip(); i += 1; continue
        if s.startswith('@description '):
            meta['description'] = s[13:].strip(); i += 1; continue

        if s.startswith('@const ') and '=' in s:
            _, rest = s.split(None, 1)
            name, val = rest.split('=', 1)
            elements.append({'type': 'const', 'name': name.strip(),
                              'value': re.sub(r'\s*--.*$', '', val).strip()})
            i += 1; continue

        if s.startswith('@derive ') and '=' in s:
            _, rest = s.split(None, 1)
            name, expr = rest.split('=', 1)
            elements.append({'type': 'derive', 'name': name.strip(),
                              'expr': re.sub(r'\s*--.*$', '', expr).strip()})
            i += 1; continue

        if s.startswith('@var ') and '=' in s:
            _, rest = s.split(None, 1)
            name, val = rest.split('=', 1)
            name = name.strip()
            val = re.sub(r'\s*--.*$', '', val).strip()
            winfo = widget_map.get(name, {})
            if winfo.get('widget') == 'toggle':
                elements.append({'type': 'toggle', 'name': name,
                                 'value': val, 'label': winfo.get('label', name)})
            else:
                el = {'type': 'var', 'name': name, 'value': val,
                      'widget': winfo.get('widget', 'none'),
                      'label': winfo.get('label', name),
                      'min': winfo.get('min', '0'),
                      'max': winfo.get('max', '100'),
                      'step': winfo.get('step', '1')}
                elements.append(el)
            i += 1; continue

        if s == '$$':
            math_lines = []
            i += 1
            while i < n and lines[i].strip() != '$$':
                math_lines.append(lines[i]); i += 1
            elements.append({'type': 'math',
                              'content': '\n'.join(l.strip() for l in math_lines)})
            i += 1; continue

        if s == '---':
            elements.append({'type': 'hr'}); i += 1; continue

        m = re.match(r'^(#{1,3})\s+(.*)', s)
        if m:
            elements.append({'type': 'heading', 'level': str(len(m.group(1))),
                             'text': m.group(2).strip()})
            i += 1; continue

        if s.startswith('[') and s.endswith(']'):
            inner = s[1:-1]
            # Slider/toggle widgets already consumed via widget_map → skip
            if re.match(r'(slider|toggle):', inner):
                i += 1; continue
            if inner.startswith('display:'):
                rest = inner[8:].strip()
                parts = [p.strip() for p in rest.split(',')]
                el = {'type': 'display', 'expr': parts[0] if parts else 'x',
                      'label': 'Value', 'format': ''}
                for p in parts[1:]:
                    if p.startswith('label='):
                        el['label'] = p.split('=', 1)[1].strip().strip('"')
                    elif p.startswith('format='):
                        el['format'] = p.split('=', 1)[1].strip().strip('"')
                elements.append(el)
            i += 1; continue

        if s.startswith('|'):
            headers, rows_raw = [], []
            while i < n and lines[i].strip().startswith('|'):
                row = lines[i].strip()
                if re.match(r'^\|[-| :]+\|$', row):
                    i += 1; continue
                cells = [c.strip() for c in row.strip('|').split('|')]
                if not headers:
                    headers = cells
                else:
                    rows_raw.append(', '.join(cells))
                i += 1
            elements.append({'type': 'table',
                             'headers': ', '.join(headers),
                             'rows': '\n'.join(rows_raw)})
            continue

        if s:
            elements.append({'type': 'text', 'content': s})
        i += 1

    return meta, elements


# ─── Main application ─────────────────────────────────────────────────────────

class StructuraMaker:
    def __init__(self, root):
        self.root = root
        self.root.title('Structura Maker')
        self.root.geometry('1180x760')
        self.root.configure(bg=C['bg'])
        self.root.minsize(900, 600)

        self.meta = {'title': 'Untitled', 'author': '', 'description': ''}
        self.elements = []
        self.selected_idx = None
        self._current_path = None
        self._prop_vars = {}    # tkinter variables for the properties form
        self._prop_frame_content = None

        self._apply_style()
        self._build_ui()
        self._new_document()

        if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
            self._open_path(sys.argv[1])

    # ── Style ────────────────────────────────────────────────────────────────

    def _apply_style(self):
        s = ttk.Style(self.root)
        s.theme_use('clam')
        s.configure('.', background=C['bg'], foreground=C['text'],
                    font=('Segoe UI', 10))
        s.configure('TFrame', background=C['bg'])
        s.configure('Panel.TFrame', background=C['panel'])
        s.configure('TLabel', background=C['bg'], foreground=C['text'])
        s.configure('Panel.TLabel', background=C['panel'], foreground=C['text'])
        s.configure('Sub.TLabel', background=C['panel'], foreground=C['subtext'],
                    font=('Segoe UI', 9))
        s.configure('TButton', background=C['btn'], foreground=C['text'],
                    borderwidth=0, focuscolor=C['accent'],
                    font=('Segoe UI', 10))
        s.map('TButton',
              background=[('active', C['btn_act']), ('pressed', C['surface2'])],
              foreground=[('active', C['accent'])])
        s.configure('Accent.TButton', background=C['accent'],
                    foreground=C['bg'], font=('Segoe UI', 10, 'bold'))
        s.map('Accent.TButton', background=[('active', C['surface2'])])
        s.configure('TEntry', fieldbackground=C['entry_bg'],
                    foreground=C['entry_fg'], insertcolor=C['accent'],
                    borderwidth=1, relief='flat')
        s.configure('TCombobox', fieldbackground=C['entry_bg'],
                    foreground=C['entry_fg'], selectbackground=C['surface'])
        s.configure('TSeparator', background=C['surface2'])
        s.configure('TScrollbar', background=C['surface'],
                    troughcolor=C['panel'], borderwidth=0, arrowsize=12)

    # ── UI layout ────────────────────────────────────────────────────────────

    def _build_ui(self):
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)
        self.root.rowconfigure(2, weight=0)

        self._build_toolbar()

        paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        paned.grid(row=1, column=0, sticky='nsew', padx=6, pady=(0, 4))

        left = ttk.Frame(paned, style='Panel.TFrame', width=340)
        right = ttk.Frame(paned, style='Panel.TFrame', width=340)
        paned.add(left,  weight=2)
        paned.add(right, weight=1)

        self._build_doc_panel(left)
        self._build_props_panel(right)
        self._build_preview()

    def _build_toolbar(self):
        tb = ttk.Frame(self.root, style='Panel.TFrame')
        tb.grid(row=0, column=0, sticky='ew', padx=0, pady=0)
        tb.columnconfigure(99, weight=1)

        def btn(parent, label, cmd, col, style='TButton'):
            b = ttk.Button(parent, text=label, command=cmd, style=style)
            b.grid(row=0, column=col, padx=(6 if col == 0 else 2, 2), pady=6)
            return b

        btn(tb, '⊕ New',    self._new_document,  0)
        btn(tb, '⊘ Open',   self._open,           1)
        btn(tb, '↓ Save',   self._save,           2)
        ttk.Separator(tb, orient='vertical').grid(row=0, column=3, sticky='ns',
                                                   padx=6, pady=4)
        self._add_menu_btn = btn(tb, '+ Add ▾',  self._show_add_menu, 4)
        ttk.Separator(tb, orient='vertical').grid(row=0, column=5, sticky='ns',
                                                   padx=6, pady=4)
        btn(tb, '↑',        self._move_up,        6)
        btn(tb, '↓',        self._move_down,      7)
        btn(tb, '✕ Delete', self._delete_element, 8)
        ttk.Separator(tb, orient='vertical').grid(row=0, column=9, sticky='ns',
                                                   padx=6, pady=4)
        btn(tb, '⚙ Meta',   self._edit_meta,     10)
        btn(tb, '▶ Export .syn', self._export, 11, style='Accent.TButton')

    def _show_add_menu(self):
        menu = tk.Menu(self.root, tearoff=0, bg=C['panel'], fg=C['text'],
                       activebackground=C['surface2'], activeforeground=C['accent'],
                       bd=0, relief='flat', font=('Segoe UI', 10))
        for etype, label, color in PALETTE:
            menu.add_command(
                label=label,
                command=lambda e=etype: self._add_element(e),
                foreground=color,
            )
        btn = self._add_menu_btn
        x = btn.winfo_rootx()
        y = btn.winfo_rooty() + btn.winfo_height()
        menu.post(x, y)

    def _build_doc_panel(self, parent):
        parent.columnconfigure(0, weight=1)
        parent.rowconfigure(1, weight=1)

        hdr = ttk.Label(parent, text='DOCUMENT STRUCTURE',
                        style='Sub.TLabel', font=('Segoe UI', 9, 'bold'))
        hdr.grid(row=0, column=0, sticky='w', padx=10, pady=(10, 4))

        frame = ttk.Frame(parent, style='Panel.TFrame')
        frame.grid(row=1, column=0, sticky='nsew', padx=6, pady=(0, 6))
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        self._listbox = tk.Listbox(
            frame,
            bg=C['panel'], fg=C['text'],
            selectbackground=C['surface'], selectforeground=C['accent'],
            activestyle='none', bd=0, highlightthickness=0,
            font=('Segoe UI', 10), relief='flat',
        )
        sb = ttk.Scrollbar(frame, orient='vertical',
                           command=self._listbox.yview)
        self._listbox.configure(yscrollcommand=sb.set)
        self._listbox.grid(row=0, column=0, sticky='nsew')
        sb.grid(row=0, column=1, sticky='ns')
        self._listbox.bind('<<ListboxSelect>>', self._on_list_select)
        self._listbox.bind('<Double-Button-1>', lambda e: None)

    def _build_props_panel(self, parent):
        parent.columnconfigure(0, weight=1)
        parent.rowconfigure(1, weight=1)

        hdr = ttk.Label(parent, text='ELEMENT PROPERTIES',
                        style='Sub.TLabel', font=('Segoe UI', 9, 'bold'))
        hdr.grid(row=0, column=0, sticky='w', padx=10, pady=(10, 4))

        canvas = tk.Canvas(parent, bg=C['panel'], highlightthickness=0, bd=0)
        sb = ttk.Scrollbar(parent, orient='vertical', command=canvas.yview)
        canvas.configure(yscrollcommand=sb.set)
        canvas.grid(row=1, column=0, sticky='nsew', padx=6, pady=(0, 6))
        sb.grid(row=1, column=1, sticky='ns', pady=(0, 6))

        self._props_canvas = canvas
        self._props_scroll = sb

        self._props_outer = ttk.Frame(canvas, style='Panel.TFrame')
        self._props_win = canvas.create_window(
            (0, 0), window=self._props_outer, anchor='nw')
        self._props_outer.bind(
            '<Configure>',
            lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',
                    lambda e: canvas.itemconfig(
                        self._props_win, width=e.width))

        self._show_empty_props()

    def _build_preview(self):
        frame = ttk.Frame(self.root, style='Panel.TFrame')
        frame.grid(row=2, column=0, sticky='ew', padx=6, pady=(0, 6))
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(0, weight=1)

        lbl = ttk.Label(frame, text='.SYN PREVIEW',
                        style='Sub.TLabel', font=('Segoe UI', 9, 'bold'))
        lbl.grid(row=0, column=0, sticky='nw', padx=(10, 6), pady=6)

        self._preview_text = tk.Text(
            frame, height=8, wrap='none',
            bg=C['entry_bg'], fg=C['subtext'],
            insertbackground=C['accent'],
            font=('Consolas', 9), bd=0, relief='flat',
            selectbackground=C['surface'],
        )
        sb_y = ttk.Scrollbar(frame, orient='vertical',
                              command=self._preview_text.yview)
        sb_x = ttk.Scrollbar(frame, orient='horizontal',
                              command=self._preview_text.xview)
        self._preview_text.configure(yscrollcommand=sb_y.set,
                                     xscrollcommand=sb_x.set)
        self._preview_text.grid(row=0, column=1, sticky='nsew', pady=4)
        sb_y.grid(row=0, column=2, sticky='ns', pady=4)
        sb_x.grid(row=1, column=1, sticky='ew', padx=0)
        self.root.rowconfigure(2, weight=0, minsize=160)

    # ── Document management ───────────────────────────────────────────────────

    def _new_document(self):
        self.meta = {'title': 'Untitled Document', 'author': '', 'description': ''}
        self.elements = [
            {'type': 'heading', 'level': '1', 'text': 'My Document'},
            {'type': 'text', 'content': 'Start building your document.'},
        ]
        self._current_path = None
        self.root.title('Structura Maker — Untitled')
        self._refresh_list()
        self._update_preview()

    def _refresh_list(self, keep_sel=None):
        self._listbox.delete(0, tk.END)
        for i, el in enumerate(self.elements):
            self._listbox.insert(tk.END, self._element_label(el))
        if keep_sel is not None and keep_sel < len(self.elements):
            self._listbox.selection_set(keep_sel)
            self._listbox.see(keep_sel)
            self.selected_idx = keep_sel
            self._show_props(self.elements[keep_sel])
        elif self.elements:
            self._listbox.selection_set(0)
            self.selected_idx = 0
            self._show_props(self.elements[0])
        self._update_preview()

    def _element_label(self, el):
        t = el.get('type', '?')
        icon = LABEL_MAP.get(t, t).split()[0]
        if t == 'heading':
            return f"  {icon} {'#' * int(el.get('level', 1))} {el.get('text', '')[:40]}"
        if t == 'text':
            return f"  {icon}  {el.get('content', '')[:45]}…" if len(el.get('content', '')) > 45 else f"  {icon}  {el.get('content', '')}"
        if t == 'hr':
            return '  — ─────────────────────────────────'
        if t == 'var':
            return f"  ◈  @var {el.get('name', '?')} = {el.get('value', '?')}"
        if t == 'toggle':
            return f"  ☑  @var {el.get('name', '?')} = {el.get('value', '?')}"
        if t == 'const':
            return f"  ■  @const {el.get('name', '?')} = {el.get('value', '?')}"
        if t == 'derive':
            return f"  ⇒  @derive {el.get('name', '?')} = {el.get('expr', '?')[:30]}"
        if t == 'math':
            return f"  ∑  $$ {el.get('content', '')[:35]}…"
        if t == 'display':
            return f"  ▶  display({el.get('expr', '?')})"
        if t == 'table':
            return f"  ▦  table [ {el.get('headers', '')[:30]} ]"
        return f'  {icon} {t}'

    def _on_list_select(self, _event=None):
        sel = self._listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        self.selected_idx = idx
        self._show_props(self.elements[idx])

    # ── Properties panel ─────────────────────────────────────────────────────

    def _clear_props(self):
        for w in self._props_outer.winfo_children():
            w.destroy()
        self._prop_vars = {}

    def _show_empty_props(self):
        self._clear_props()
        ttk.Label(self._props_outer,
                  text='Select an element\nto edit its properties.',
                  style='Sub.TLabel', justify='center').pack(pady=30)

    def _lbl(self, parent, text, row):
        ttk.Label(parent, text=text, style='Panel.TLabel',
                  font=('Segoe UI', 9)).grid(
            row=row, column=0, sticky='w', padx=(12, 6), pady=(6, 0))

    def _entry(self, parent, key, row, default=''):
        v = tk.StringVar(value=default)
        self._prop_vars[key] = v
        e = ttk.Entry(parent, textvariable=v)
        e.grid(row=row, column=1, sticky='ew', padx=(0, 12), pady=(6, 0))
        e.bind('<KeyRelease>', self._on_prop_change)
        return v

    def _text_widget(self, parent, key, row, height=4, default=''):
        v_holder = [None]
        t = tk.Text(parent, height=height, wrap='word',
                    bg=C['entry_bg'], fg=C['entry_fg'],
                    insertbackground=C['accent'],
                    font=('Consolas', 10), bd=1, relief='flat',
                    selectbackground=C['surface'])
        t.grid(row=row, column=0, columnspan=2, sticky='ew',
               padx=12, pady=(6, 0))
        t.insert('1.0', default)
        t.bind('<KeyRelease>', self._on_prop_change)
        self._prop_vars[key] = t
        return t

    def _combo(self, parent, key, row, values, default):
        v = tk.StringVar(value=default)
        self._prop_vars[key] = v
        c = ttk.Combobox(parent, textvariable=v, values=values,
                         state='readonly', font=('Segoe UI', 10))
        c.grid(row=row, column=1, sticky='ew', padx=(0, 12), pady=(6, 0))
        c.bind('<<ComboboxSelected>>', self._on_prop_change)
        return v

    def _radio_row(self, parent, key, row, options, default):
        v = tk.StringVar(value=default)
        self._prop_vars[key] = v
        f = ttk.Frame(parent, style='Panel.TFrame')
        f.grid(row=row, column=1, sticky='w', padx=(0, 12), pady=(6, 0))
        for val, label in options:
            rb = tk.Radiobutton(f, text=label, variable=v, value=val,
                                bg=C['panel'], fg=C['text'],
                                selectcolor=C['surface'],
                                activebackground=C['panel'],
                                activeforeground=C['accent'],
                                font=('Segoe UI', 10),
                                command=self._on_prop_change)
            rb.pack(side='left', padx=4)
        return v

    def _section(self, parent, title, row):
        ttk.Label(parent, text=title, style='Sub.TLabel',
                  font=('Segoe UI', 8, 'bold')).grid(
            row=row, column=0, columnspan=2,
            sticky='w', padx=12, pady=(12, 2))

    def _show_props(self, el):
        self._clear_props()
        t = el.get('type', '')
        f = self._props_outer
        f.columnconfigure(1, weight=1)

        etype_label = LABEL_MAP.get(t, t)
        ttk.Label(f, text=etype_label, style='Panel.TLabel',
                  font=('Segoe UI', 11, 'bold'),
                  foreground=C['accent']).grid(
            row=0, column=0, columnspan=2, sticky='w', padx=12, pady=(12, 0))

        if t == 'heading':
            self._lbl(f, 'Level', 1)
            self._radio_row(f, 'level', 1,
                            [('1', 'H1'), ('2', 'H2'), ('3', 'H3')],
                            el.get('level', '1'))
            self._lbl(f, 'Text', 2)
            self._entry(f, 'text', 2, el.get('text', ''))

        elif t == 'text':
            self._lbl(f, 'Content', 1)
            self._text_widget(f, 'content', 2, height=5,
                              default=el.get('content', ''))

        elif t == 'hr':
            ttk.Label(f, text='No properties.', style='Sub.TLabel').grid(
                row=1, column=0, columnspan=2, padx=12, pady=8)

        elif t == 'var':
            self._section(f, 'VARIABLE', 1)
            self._lbl(f, 'Name', 2);  self._entry(f, 'name',  2, el.get('name', ''))
            self._lbl(f, 'Default', 3); self._entry(f, 'value', 3, el.get('value', ''))
            self._section(f, 'WIDGET', 4)
            wv = self._combo(f, 'widget', 4,
                             ['slider', 'input', 'none'], el.get('widget', 'slider'))
            self._lbl(f, 'Label', 5);  self._entry(f, 'label', 5, el.get('label', ''))
            self._section(f, 'SLIDER RANGE', 6)
            self._lbl(f, 'Min', 7);  self._entry(f, 'min',  7, el.get('min', '0'))
            self._lbl(f, 'Max', 8);  self._entry(f, 'max',  8, el.get('max', '100'))
            self._lbl(f, 'Step', 9); self._entry(f, 'step', 9, el.get('step', '1'))

        elif t == 'toggle':
            self._section(f, 'TOGGLE', 1)
            self._lbl(f, 'Name', 2);    self._entry(f, 'name',  2, el.get('name', ''))
            self._lbl(f, 'Default', 3)
            self._radio_row(f, 'value', 3,
                            [('1', '☑ On'), ('0', '☐ Off')],
                            el.get('value', '1'))
            self._lbl(f, 'Label', 4);   self._entry(f, 'label', 4, el.get('label', ''))

        elif t == 'const':
            self._lbl(f, 'Name', 1);  self._entry(f, 'name',  1, el.get('name', ''))
            self._lbl(f, 'Value', 2); self._entry(f, 'value', 2, el.get('value', ''))

        elif t == 'derive':
            self._lbl(f, 'Name', 1);       self._entry(f, 'name', 1, el.get('name', ''))
            self._lbl(f, 'Expression', 2); self._entry(f, 'expr', 2, el.get('expr', ''))
            ttk.Label(f, text='Supports: π  √()  &&  ||  ? :  sin cos sqrt',
                      style='Sub.TLabel').grid(
                row=3, column=0, columnspan=2, sticky='w', padx=12, pady=(4, 0))

        elif t == 'math':
            self._lbl(f, 'Equations', 1)
            self._text_widget(f, 'content', 2, height=6,
                              default=el.get('content', ''))
            ttk.Label(f, text='One equation per line. Not evaluated — display only.',
                      style='Sub.TLabel').grid(
                row=3, column=0, columnspan=2, sticky='w', padx=12, pady=(4, 0))

        elif t == 'display':
            self._lbl(f, 'Expression', 1); self._entry(f, 'expr',   1, el.get('expr', ''))
            self._lbl(f, 'Label', 2);      self._entry(f, 'label',  2, el.get('label', ''))
            self._lbl(f, 'Format', 3);     self._entry(f, 'format', 3, el.get('format', ''))
            ttk.Label(f, text='Format examples:  %.2f   $%.2f   %.1f m/s',
                      style='Sub.TLabel').grid(
                row=4, column=0, columnspan=2, sticky='w', padx=12, pady=(4, 0))

        elif t == 'table':
            self._lbl(f, 'Column headers', 1)
            self._entry(f, 'headers', 1, el.get('headers', 'A, B, C'))
            ttk.Label(f, text='Comma-separated', style='Sub.TLabel').grid(
                row=2, column=0, columnspan=2, sticky='w', padx=12, pady=(2, 0))
            self._lbl(f, 'Rows', 3)
            self._text_widget(f, 'rows', 4, height=5, default=el.get('rows', ''))
            ttk.Label(f, text='One row per line, cells comma-separated.\nUse {{expr}} for live values.',
                      style='Sub.TLabel').grid(
                row=5, column=0, columnspan=2, sticky='w', padx=12, pady=(2, 0))

        # Apply button
        ttk.Separator(f, orient='horizontal').grid(
            row=50, column=0, columnspan=2, sticky='ew', padx=12, pady=12)
        ttk.Button(f, text='Apply Changes', style='Accent.TButton',
                   command=self._apply_props).grid(
            row=51, column=0, columnspan=2, padx=12, pady=(0, 12), sticky='ew')

    def _on_prop_change(self, _event=None):
        self._apply_props(silent=True)

    def _apply_props(self, silent=False):
        if self.selected_idx is None:
            return
        el = self.elements[self.selected_idx]
        for key, var in self._prop_vars.items():
            if isinstance(var, tk.Text):
                el[key] = var.get('1.0', 'end-1c')
            else:
                el[key] = var.get()
        self._refresh_list_labels()
        self._update_preview()

    def _refresh_list_labels(self):
        for i, el in enumerate(self.elements):
            self._listbox.delete(i)
            self._listbox.insert(i, self._element_label(el))
        if self.selected_idx is not None:
            self._listbox.selection_set(self.selected_idx)

    # ── Element operations ────────────────────────────────────────────────────

    def _add_element(self, etype):
        el = default_element(etype)
        idx = self.selected_idx
        if idx is None:
            self.elements.append(el)
            new_idx = len(self.elements) - 1
        else:
            self.elements.insert(idx + 1, el)
            new_idx = idx + 1
        self.selected_idx = new_idx
        self._refresh_list(keep_sel=new_idx)

    def _move_up(self):
        i = self.selected_idx
        if i is None or i == 0:
            return
        self.elements[i], self.elements[i - 1] = self.elements[i - 1], self.elements[i]
        self._refresh_list(keep_sel=i - 1)

    def _move_down(self):
        i = self.selected_idx
        if i is None or i >= len(self.elements) - 1:
            return
        self.elements[i], self.elements[i + 1] = self.elements[i + 1], self.elements[i]
        self._refresh_list(keep_sel=i + 1)

    def _delete_element(self):
        i = self.selected_idx
        if i is None or not self.elements:
            return
        self.elements.pop(i)
        new_sel = min(i, len(self.elements) - 1) if self.elements else None
        self.selected_idx = new_sel
        self._refresh_list(keep_sel=new_sel)

    # ── Meta dialog ───────────────────────────────────────────────────────────

    def _edit_meta(self):
        dlg = tk.Toplevel(self.root)
        dlg.title('Document Meta')
        dlg.configure(bg=C['panel'])
        dlg.resizable(False, False)
        dlg.transient(self.root)
        dlg.grab_set()

        fields = [('title', 'Title'), ('author', 'Author'), ('description', 'Description')]
        vars_ = {}
        for row, (key, label) in enumerate(fields):
            ttk.Label(dlg, text=label, style='Panel.TLabel').grid(
                row=row, column=0, padx=12, pady=8, sticky='w')
            v = tk.StringVar(value=self.meta.get(key, ''))
            vars_[key] = v
            ttk.Entry(dlg, textvariable=v, width=40).grid(
                row=row, column=1, padx=(0, 12), pady=8)

        def save():
            for key, v in vars_.items():
                self.meta[key] = v.get()
            self._update_preview()
            dlg.destroy()

        ttk.Button(dlg, text='Save', style='Accent.TButton',
                   command=save).grid(
            row=len(fields), column=0, columnspan=2,
            padx=12, pady=12, sticky='ew')
        dlg.wait_window()

    # ── Preview ───────────────────────────────────────────────────────────────

    def _update_preview(self):
        syn = document_to_syn(self.meta, self.elements)
        self._preview_text.configure(state='normal')
        self._preview_text.delete('1.0', tk.END)
        self._preview_text.insert('1.0', syn)
        self._preview_text.configure(state='disabled')

    # ── File I/O ──────────────────────────────────────────────────────────────

    def _save(self):
        if not self._current_path:
            path = filedialog.asksaveasfilename(
                defaultextension='.syn',
                filetypes=[('Syntaxia Document', '*.syn'), ('All Files', '*.*')],
            )
            if not path:
                return
            self._current_path = path

        syn = document_to_syn(self.meta, self.elements)
        with open(self._current_path, 'w', encoding='utf-8') as f:
            f.write(syn)
        self.root.title(f'Structura Maker — {os.path.basename(self._current_path)}')

    def _open(self):
        path = filedialog.askopenfilename(
            filetypes=[('Syntaxia Document', '*.syn'), ('All Files', '*.*')],
        )
        if path:
            self._open_path(path)

    def _open_path(self, path):
        try:
            with open(path, encoding='utf-8') as f:
                source = f.read()
            meta, elements = syn_to_elements(source)
            self.meta = meta
            self.elements = elements
            self._current_path = path
            self.root.title(f'Structura Maker — {os.path.basename(path)}')
            self._refresh_list()
        except Exception as exc:
            messagebox.showerror('Open Failed', str(exc))

    def _export(self):
        path = filedialog.asksaveasfilename(
            defaultextension='.syn',
            filetypes=[('Syntaxia Document', '*.syn'), ('All Files', '*.*')],
            initialfile=self.meta.get('title', 'document')
                             .lower().replace(' ', '_') + '.syn',
        )
        if not path:
            return
        syn = document_to_syn(self.meta, self.elements)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(syn)
        messagebox.showinfo('Exported', f'Saved to:\n{path}')


# ─── Entry point ──────────────────────────────────────────────────────────────

def main():
    root = tk.Tk()
    app = StructuraMaker(root)
    root.mainloop()


if __name__ == '__main__':
    main()
