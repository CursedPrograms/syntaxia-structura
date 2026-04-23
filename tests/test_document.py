"""Tests for the Structura reactive document renderer (document.py)."""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from document import (  # noqa: E402
    build_store, parse_document, syn_eval, interpolate, _apply_fmt,
)

SCRIPTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_syn(name):
    with open(os.path.join(SCRIPTS_DIR, name), encoding='utf-8') as f:
        return f.read()


# ─── syn_eval ────────────────────────────────────────────────────────────────

def test_eval_number():
    assert syn_eval('42', {}) == 42


def test_eval_pi():
    import math
    assert abs(syn_eval('π', {}) - math.pi) < 1e-9


def test_eval_sqrt():
    assert syn_eval('√(16)', {}) == 4.0
    assert syn_eval('√(49)', {}) == 7.0


def test_eval_sqrt_var():
    assert syn_eval('√x', {'x': 25}) == 5.0


def test_eval_arithmetic():
    assert syn_eval('2 + 3 * 4', {}) == 14


def test_eval_var_lookup():
    assert syn_eval('base + dlc', {'base': 60, 'dlc': 15}) == 75


def test_eval_logical_and():
    assert syn_eval('1 > 0 && 2 > 1', {}) is True


def test_eval_logical_or():
    assert syn_eval('0 > 1 || 1 > 0', {}) is True


def test_eval_ternary():
    assert syn_eval('1 > 0 ? "yes" : "no"', {}) == 'yes'
    assert syn_eval('0 > 1 ? "yes" : "no"', {}) == 'no'


# ─── interpolate ─────────────────────────────────────────────────────────────

def test_interpolate_literal():
    assert interpolate('hello world', {}) == 'hello world'


def test_interpolate_var():
    assert interpolate('Total: {{price}}', {'price': 99}) == 'Total: 99'


def test_interpolate_fmt():
    store = {'tax': 7.6}
    result = interpolate('Tax: {{tax:.2f}}', store)
    assert result == 'Tax: 7.60'


def test_interpolate_expr():
    store = {'a': 10, 'b': 5}
    assert interpolate('Sum: {{a + b}}', store) == 'Sum: 15'


def test_interpolate_unknown_unchanged():
    result = interpolate('{{unknown_var}}', {})
    assert 'unknown_var' in result


# ─── parse_document ───────────────────────────────────────────────────────────

def test_parse_meta():
    src = '@title My Doc\n@author Alice\n'
    meta, _, _ = parse_document(src)
    assert meta['title'] == 'My Doc'
    assert meta['author'] == 'Alice'


def test_parse_var_decl():
    src = '@var x = 10\n@const g = 9.81\n@derive y = x * 2\n'
    _, decls, _ = parse_document(src)
    kinds = {name: kind for kind, name, _ in decls}
    assert kinds['x'] == 'var'
    assert kinds['g'] == 'const'
    assert kinds['y'] == 'derive'


def test_parse_heading_nodes():
    src = '# Title\n## Sub\n'
    _, _, nodes = parse_document(src)
    heading_nodes = [n for n in nodes if n[0] == 'heading']
    assert heading_nodes[0][1] == 1
    assert heading_nodes[1][1] == 2


def test_parse_table():
    src = '| A | B |\n|---|---|\n| 1 | 2 |\n'
    _, _, nodes = parse_document(src)
    table = next(n for n in nodes if n[0] == 'table')
    rows = table[1]
    assert rows[0][1] is True   # header row
    assert rows[1][0] == ['1', '2']


# ─── build_store ─────────────────────────────────────────────────────────────

def test_store_var_default():
    decls = [('var', 'x', '10')]
    store = build_store(decls)
    assert store['x'] == 10


def test_store_const():
    decls = [('const', 'g', '9.81')]
    store = build_store(decls)
    assert abs(store['g'] - 9.81) < 1e-9


def test_store_derive_simple():
    decls = [('var', 'a', '5'), ('var', 'b', '3'), ('derive', 'c', 'a + b')]
    store = build_store(decls)
    assert store['c'] == 8


def test_store_derive_chain():
    decls = [
        ('var', 'x', '4'),
        ('derive', 'y', 'x * 2'),
        ('derive', 'z', 'y + 1'),
    ]
    store = build_store(decls)
    assert store['y'] == 8
    assert store['z'] == 9


def test_store_override():
    decls = [('var', 'depth', '0')]
    store = build_store(decls, overrides={'depth': 1000.0})
    assert store['depth'] == 1000.0


# ─── _apply_fmt ──────────────────────────────────────────────────────────────

def test_apply_fmt_printf():
    assert _apply_fmt(3.14159, '%.2f') == '3.14'


def test_apply_fmt_prefix_suffix():
    assert _apply_fmt(102.6, '$%.2f') == '$102.60'
    assert _apply_fmt(348.332, '%.3f atm') == '348.332 atm'


def test_apply_fmt_python():
    assert _apply_fmt(1.5708, '.4f') == '1.5708'


# ─── Full document integration ────────────────────────────────────────────────

def test_game_store_defaults():
    src = load_syn('game_store.syn')
    meta, decls, nodes = parse_document(src)
    store = build_store(decls)
    assert store['base_price'] == 60
    assert store['subtotal'] == 95       # 60+15+20
    assert abs(store['tax'] - 7.6) < 0.01
    assert abs(store['total'] - 102.6) < 0.01


def test_game_store_override():
    src = load_syn('game_store.syn')
    _, decls, _ = parse_document(src)
    store = build_store(decls, overrides={'base_price': 40, 'dlc1': 5, 'dlc2': 5})
    assert store['subtotal'] == 50
    assert abs(store['total'] - 54.0) < 0.01


def test_ocean_surface():
    src = load_syn('ocean_pressure.syn')
    _, decls, _ = parse_document(src)
    store = build_store(decls, overrides={'depth': 0})
    assert abs(store['P'] - 101325) < 1
    assert abs(store['P'] / 101325 - 1.0) < 0.001


def test_ocean_deep():
    src = load_syn('ocean_pressure.syn')
    _, decls, _ = parse_document(src)
    store = build_store(decls, overrides={'depth': 3500})
    assert store['P'] > 1e7
    assert store['rho'] > 1025
    assert store['sound_speed'] > 1400


def test_ocean_zone_labels():
    src = load_syn('ocean_pressure.syn')
    _, decls, _ = parse_document(src)
    for depth, expected in [
        (100,   'Sunlight'),
        (500,   'Twilight'),
        (2000,  'Midnight'),
        (5000,  'Abyssal'),
        (8000,  'Hadal'),
    ]:
        store = build_store(decls, overrides={'depth': float(depth)})
        assert expected in store['zone_label'], \
            f"depth={depth}: expected '{expected}' in '{store['zone_label']}'"
