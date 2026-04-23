"""Tests for the Structura Syntaxia interpreter."""

import io
import math
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from structura import Interpreter, Parser, tokenize  # noqa: E402

SCRIPTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run_syn(filename):
    """Run a .syn file, return captured stdout as a stripped string."""
    path = os.path.join(SCRIPTS_DIR, filename)
    captured = io.StringIO()
    old_out = sys.stdout
    sys.stdout = captured
    try:
        Interpreter().run(path)
    finally:
        sys.stdout = old_out
    return captured.getvalue().strip()


# ─── Script output tests ──────────────────────────────────────────────────────

def test_counter():
    lines = run_syn('counter.syn').splitlines()
    assert lines == [
        'Counter: 0',
        'Counter: 1',
        'Counter: 2',
        'Counter: 3',
        'Counter: 4',
    ]


def test_helloworld():
    assert 'Hello' in run_syn('helloworld.syn')


def test_math_demo():
    output = run_syn('math_demo.syn')
    assert 'Sine of angle' in output


def test_physics_demo():
    output = run_syn('physics_demo.syn')
    assert 'Distance traveled: 171.5' in output
    assert 'Half of' in output
    assert 'Square root of 49: 7' in output
    assert 'Distance as integer: 171' in output


def test_example_syn():
    output = run_syn('example.syn')
    assert 'Square root of 49: 7' in output
    assert 'Distance as integer: 171' in output


def test_func_syn():
    output = run_syn('func.syn')
    assert 'Sum: 30' in output
    assert 'Product: 200' in output
    assert 'Sum is greater than 25' in output


def test_imports_syn():
    output = run_syn('imports.syn')
    assert 'Angle in radians' in output
    assert 'Square root of 25: 5' in output


# ─── Tokenizer tests ──────────────────────────────────────────────────────────

def test_tokenize_basic():
    toks = tokenize('<func name="hello"><print>Hi</print></func>')
    assert ('OPEN', 'func', {'name': 'hello'}) in toks
    assert ('OPEN', 'print', {}) in toks
    assert ('TEXT', 'Hi') in toks
    assert ('CLOSE', 'func') in toks


def test_tokenize_gt_in_condition():
    src = '<if condition="distance > 0 && speed > 0"><print>yes</print></if>'
    toks = tokenize(src)
    if_tok = next(t for t in toks if t[0] == 'OPEN' and t[1] == 'if')
    assert if_tok[2]['condition'] == 'distance > 0 && speed > 0'


def test_tokenize_import():
    toks = tokenize('<@>math_utils.syn@</@>')
    assert toks == [('IMPORT', 'math_utils.syn')]


def test_tokenize_conv():
    toks = tokenize('<~>int==distance<~>')
    assert toks == [('CONV', 'int==distance')]


# ─── Expression evaluation tests ─────────────────────────────────────────────

def test_eval_pi():
    result = Interpreter()._eval('π / 2', {})
    assert abs(result - math.pi / 2) < 1e-9


def test_eval_sqrt_literal():
    assert Interpreter()._eval('√49', {}) == 7.0


def test_eval_sqrt_var():
    assert Interpreter()._eval('√x', {'x': 16}) == 4.0


def test_eval_logical_and():
    assert Interpreter()._eval('1 > 0 && 2 > 1', {}) is True


def test_eval_logical_or():
    assert Interpreter()._eval('1 > 2 || 2 > 1', {}) is True


def test_eval_not():
    assert Interpreter()._eval('!0', {}) is True


def test_eval_not_equal():
    assert Interpreter()._eval('3 != 4', {}) is True


# ─── Type coercion tests ──────────────────────────────────────────────────────

def test_coerce_int():
    val = Interpreter()._coerce(3.7, 'int')
    assert val == 3
    assert isinstance(val, int)


def test_coerce_float():
    val = Interpreter()._coerce('2.5', 'float')
    assert val == 2.5


def test_coerce_str():
    assert Interpreter()._coerce(42, 'str') == '42'


# ─── Parser tests ─────────────────────────────────────────────────────────────

def test_parse_func():
    toks = tokenize('<func name="f"><print>hi</print></func>')
    nodes = Parser(toks).parse_all()
    assert nodes[0][0] == 'func'
    assert nodes[0][1] == 'f'


def test_parse_var_expr():
    toks = tokenize('<var name="x" type="int">42</var>')
    nodes = Parser(toks).parse_all()
    assert nodes[0] == ('var', 'x', 'int', ('expr', '42'))


def test_parse_var_conv():
    toks = tokenize('<var name="n" type="int"><~>int==3.9<~></var>')
    nodes = Parser(toks).parse_all()
    assert nodes[0] == ('var', 'n', 'int', ('conv', 'int==3.9'))


def test_parse_while():
    src = '<while condition="i < 3"><print>i</print></while>'
    toks = tokenize(src)
    nodes = Parser(toks).parse_all()
    assert nodes[0][0] == 'while'
    assert nodes[0][1] == 'i < 3'


# ─── Format value tests ───────────────────────────────────────────────────────

def test_fval_whole_float():
    assert Interpreter._fval(7.0) == '7'


def test_fval_decimal_float():
    assert Interpreter._fval(1.5) == '1.5'


def test_fval_four_dp():
    assert Interpreter._fval(1.5707963) == '1.5708'


def test_fval_int():
    assert Interpreter._fval(42) == '42'
