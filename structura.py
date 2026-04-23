#!/usr/bin/env python3
"""Structura — Syntaxia (.syn) interpreter  (Phase 3 MVP)"""

import sys
import os
import re
import math

# ─── Tokenizer ────────────────────────────────────────────────────────────────

_RE_IMPORT = re.compile(r'<@>([^@]+)@</@>')
_RE_CONV = re.compile(r'<~>([^<]+)<~>')
_RE_CLOSE = re.compile(r'</([a-zA-Z][a-zA-Z0-9_-]*)>')
_RE_ATTR = re.compile(r'(\w+)="([^"]*)"')


def _scan_open_tag(src, pos):
    """Return (name, attrs_dict, end_pos) for opening tag at src[pos], or None."""
    i = pos + 1
    n = len(src)
    if i >= n or src[i] in ('/', '!', '?', '@', '~'):
        return None
    j = i
    while j < n and (src[j].isalnum() or src[j] in '_-'):
        j += 1
    if j == i:
        return None
    tag = src[i:j]
    i = j
    attrs_start = i
    in_q, qc = False, None
    while i < n:
        c = src[i]
        if in_q:
            if c == qc:
                in_q = False
        else:
            if c in ('"', "'"):
                in_q, qc = True, c
            elif c == '>':
                break
        i += 1
    attrs = dict(_RE_ATTR.findall(src[attrs_start:i]))
    return tag, attrs, i + 1


def tokenize(source):
    """Return a flat list of tokens from a Syntaxia source string."""
    toks = []
    i = 0
    n = len(source)
    while i < n:
        if source[i:i + 3] == '<@>':
            m = _RE_IMPORT.match(source, i)
            if m:
                toks.append(('IMPORT', m.group(1).strip()))
                i = m.end()
                continue
        if source[i:i + 3] == '<~>':
            m = _RE_CONV.match(source, i)
            if m:
                toks.append(('CONV', m.group(1).strip()))
                i = m.end()
                continue
        if source[i:i + 2] == '</':
            m = _RE_CLOSE.match(source, i)
            if m:
                toks.append(('CLOSE', m.group(1)))
                i = m.end()
                continue
        if source[i] == '<':
            result = _scan_open_tag(source, i)
            if result:
                tag, attrs, end = result
                toks.append(('OPEN', tag, attrs))
                i = end
                continue
        end = i
        while end < n and source[end] != '<':
            end += 1
        text = source[i:end].strip()
        if text:
            toks.append(('TEXT', text))
        i = end if end > i else i + 1
    return toks


# ─── Parser ───────────────────────────────────────────────────────────────────

class Parser:
    def __init__(self, tokens):
        self.toks = tokens
        self.pos = 0

    def peek(self):
        return self.toks[self.pos] if self.pos < len(self.toks) else None

    def consume(self):
        t = self.toks[self.pos]
        self.pos += 1
        return t

    def parse_all(self):
        nodes = []
        while self.pos < len(self.toks):
            node = self._parse_node()
            if node is not None:
                nodes.append(node)
        return nodes

    def _parse_node(self):
        t = self.peek()
        if t is None:
            return None
        kind = t[0]
        if kind == 'IMPORT':
            self.consume()
            return ('import', t[1])
        if kind == 'OPEN':
            tag = t[1]
            handlers = {
                'func': self._parse_func,
                'var': self._parse_var,
                'if': self._parse_if,
                'while': self._parse_while,
                'for': self._parse_for,
                'print': self._parse_print,
                'return': self._parse_return,
            }
            if tag in handlers:
                return handlers[tag]()
        if kind in ('CLOSE', 'TEXT', 'CONV'):
            return None
        self.consume()
        return None

    def _parse_func(self):
        t = self.consume()
        name = t[2].get('name', '')
        body = self._body('func')
        return ('func', name, body)

    def _parse_var(self):
        t = self.consume()
        name = t[2].get('name', '')
        vtype = t[2].get('type', 'any')
        nxt = self.peek()
        if nxt and nxt[0] == 'CONV':
            self.consume()
            conv = nxt[1]
            self._expect_close('var')
            return ('var', name, vtype, ('conv', conv))
        if nxt and nxt[0] == 'TEXT':
            self.consume()
            expr = nxt[1]
            self._expect_close('var')
            return ('var', name, vtype, ('expr', expr))
        self._expect_close('var')
        return ('var', name, vtype, ('expr', ''))

    def _parse_if(self):
        t = self.consume()
        cond = t[2].get('condition', 'False')
        body, elifs, else_body = [], [], None
        while self.pos < len(self.toks):
            nxt = self.peek()
            if nxt is None:
                break
            if nxt[0] == 'CLOSE' and nxt[1] == 'if':
                self.consume()
                break
            if nxt[0] == 'OPEN' and nxt[1] == 'elif':
                ec = nxt[2].get('condition', 'False')
                self.consume()
                eb = self._body('elif')
                elifs.append((ec, eb))
            elif nxt[0] == 'OPEN' and nxt[1] == 'else':
                self.consume()
                else_body = self._body('else')
            else:
                node = self._parse_node()
                if node is not None:
                    body.append(node)
                elif nxt == self.peek():
                    self.consume()
        return ('if', cond, body, elifs, else_body)

    def _parse_while(self):
        t = self.consume()
        cond = t[2].get('condition', 'False')
        body = self._body('while')
        return ('while', cond, body)

    def _parse_for(self):
        t = self.consume()
        cond = t[2].get('condition', '')
        body = self._body('for')
        return ('for', cond, body)

    def _parse_print(self):
        self.consume()
        nxt = self.peek()
        text = ''
        if nxt and nxt[0] == 'TEXT':
            self.consume()
            text = nxt[1]
        self._expect_close('print')
        return ('print', text)

    def _parse_return(self):
        self.consume()
        nxt = self.peek()
        expr = ''
        if nxt and nxt[0] == 'TEXT':
            self.consume()
            expr = nxt[1]
        self._expect_close('return')
        return ('return', expr)

    def _body(self, close_tag):
        nodes = []
        while self.pos < len(self.toks):
            nxt = self.peek()
            if nxt is None:
                break
            if nxt[0] == 'CLOSE' and nxt[1] == close_tag:
                self.consume()
                break
            node = self._parse_node()
            if node is not None:
                nodes.append(node)
            elif nxt == self.peek():
                self.consume()
        return nodes

    def _expect_close(self, tag):
        nxt = self.peek()
        if nxt and nxt[0] == 'CLOSE' and nxt[1] == tag:
            self.consume()


# ─── Interpreter ──────────────────────────────────────────────────────────────

class ReturnSignal(Exception):
    def __init__(self, value):
        super().__init__()
        self.value = value


class Interpreter:
    def __init__(self):
        self.functions = {}
        self.global_env = {}
        self._main_funcs = []
        self._script_dir = '.'

    def run(self, path):
        self._script_dir = os.path.dirname(os.path.abspath(path))
        self._main_funcs = []
        for node in self._load(path):
            self._exec_top(node, is_main=True)
        for name in self._main_funcs:
            self._call(name)

    def _load(self, path):
        try:
            with open(path, encoding='utf-8') as f:
                src = f.read()
        except FileNotFoundError:
            print(f"[structura] warning: cannot find '{path}'", file=sys.stderr)
            return []
        return Parser(tokenize(src)).parse_all()

    def _exec_top(self, node, is_main=False):
        if node[0] == 'import':
            self._do_import(node[1])
        elif node[0] == 'func':
            _, name, body = node
            self.functions[name] = body
            if is_main:
                self._main_funcs.append(name)

    def _do_import(self, filename):
        path = os.path.join(self._script_dir, filename)
        for node in self._load(path):
            self._exec_top(node, is_main=False)

    def _call(self, name):
        if name not in self.functions:
            raise RuntimeError(f"undefined function '{name}'")
        env = dict(self.global_env)
        try:
            self._block(self.functions[name], env)
        except ReturnSignal:
            pass

    def _block(self, nodes, env):
        for node in nodes:
            self._exec(node, env)

    def _exec(self, node, env):
        kind = node[0]
        if kind == 'var':
            _, name, vtype, val_node = node
            env[name] = self._coerce(self._eval_val(val_node, env), vtype)
        elif kind == 'print':
            print(self._fmt_print(node[1], env))
        elif kind == 'if':
            _, cond, body, elifs, else_body = node
            if self._eval(cond, env):
                self._block(body, env)
            else:
                for ec, eb in elifs:
                    if self._eval(ec, env):
                        self._block(eb, env)
                        break
                else:
                    if else_body:
                        self._block(else_body, env)
        elif kind == 'while':
            _, cond, body = node
            while self._eval(cond, env):
                self._block(body, env)
        elif kind == 'for':
            _, cond, body = node
            while self._eval(cond, env):
                self._block(body, env)
        elif kind == 'return':
            val = self._eval(node[1], env) if node[1] else None
            raise ReturnSignal(val)

    def _eval_val(self, val_node, env):
        if val_node[0] == 'conv':
            parts = val_node[1].split('==', 1)
            target = parts[0].strip()
            expr = parts[1].strip() if len(parts) > 1 else ''
            return self._coerce(self._eval(expr, env), target)
        return self._eval(val_node[1], env)

    def _eval(self, expr, env):
        expr = expr.strip()
        if not expr:
            return None
        expr = expr.replace('π', str(math.pi))
        expr = re.sub(r'√(\d+(?:\.\d+)?)', lambda m: f'math.sqrt({m.group(1)})', expr)
        expr = re.sub(r'√([a-zA-Z_]\w*)', lambda m: f'math.sqrt({m.group(1)})', expr)
        expr = expr.replace('&&', ' and ')
        expr = expr.replace('||', ' or ')
        expr = re.sub(r'!(?!=)', ' not ', expr)
        ns = {
            'math': math,
            '__builtins__': {
                'abs': abs, 'round': round, 'int': int, 'float': float,
                'str': str, 'bool': bool, 'len': len, 'min': min, 'max': max,
            },
        }
        ns.update({k: v for k, v in env.items() if not k.startswith('__')})
        try:
            return eval(expr, ns)
        except Exception as exc:
            raise RuntimeError(f"eval error: '{expr}' — {exc}") from exc

    def _coerce(self, val, type_str):
        if val is None:
            return val
        try:
            if type_str == 'int':
                return int(val)
            if type_str == 'float':
                return float(val)
            if type_str in ('str', 'string'):
                return str(val)
            if type_str == 'bool':
                return bool(val)
        except (TypeError, ValueError):
            pass
        return val

    def _fmt_print(self, body, env):
        body = body.strip()
        if not body:
            return ''
        if ': ' in body:
            idx = body.rfind(': ')
            label = body[:idx + 2]
            expr_part = body[idx + 2:]
            try:
                val = self._eval(expr_part, env)
                return label + self._fval(val)
            except RuntimeError:
                pass
        try:
            return self._fval(self._eval(body, env))
        except RuntimeError:
            return body

    @staticmethod
    def _fval(val):
        if isinstance(val, float):
            if val == int(val):
                return str(int(val))
            return f'{val:.4f}'.rstrip('0').rstrip('.')
        return str(val)


# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

    args = sys.argv[1:]
    if not args or args[0] in ('-h', '--help'):
        print('usage: structura run <file.syn>')
        print('       structura <file.syn>')
        sys.exit(0)

    path = args[1] if len(args) >= 2 and args[0] == 'run' else args[0]

    if not os.path.exists(path):
        print(f'error: file not found: {path}', file=sys.stderr)
        sys.exit(1)

    Interpreter().run(path)


if __name__ == '__main__':
    main()
