/**
 * syntaxia — JS/React package for Syntaxia .syn documents
 *
 * Usage (React):
 *   import { SynDocument, parseSyn, evalExpr } from 'syntaxia';
 *   <SynDocument src={synFileContent} overrides={{ depth: 3500 }} />
 *
 * Usage (Node / plain JS):
 *   const { parseSyn, buildStore } = require('syntaxia');
 *   const { vars, nodes } = parseSyn(src);
 */

'use strict';

// ── Stdlib ────────────────────────────────────────────────────────────────────
const STDLIB = {
  'math_utils.syn': {
    PI: Math.PI, E: Math.E, PHI: 1.61803398874989,
    SQRT2: Math.SQRT2, SQRT3: Math.sqrt(3),
    LN2: Math.LN2, LN10: Math.LN10,
    G: 9.80665, C_LIGHT: 299792458,
    RHO_WATER: 1000.0, RHO_SEA: 1025.0,
    KG_TO_LB: 2.20462, KM_TO_MI: 0.621371, M_TO_FT: 3.28084,
  },
};

// ── Expression evaluator ──────────────────────────────────────────────────────
function evalExpr(expr, vars) {
  let e = String(expr);
  e = e.replace(/π/g, String(Math.PI));
  e = e.replace(/√\(([^)]+)\)/g, (_, x) => `Math.sqrt(${x})`);
  e = e.replace(/√([a-zA-Z_]\w*)/g, (_, v) => `Math.sqrt(${v})`);
  e = e.replace(/√(\d+(?:\.\d+)?)/g, (_, n) => `Math.sqrt(${n})`);
  // Python ternary: x if cond else y  →  (cond ? x : y)
  e = e.replace(/(.+?)\s+if\s+(.+?)\s+else\s+(.+)/g, '($2 ? $1 : $3)');
  const names = Object.keys(vars);
  const vals = names.map(k => vars[k]);
  try {
    // eslint-disable-next-line no-new-func
    return new Function(...names, `"use strict"; return (${e});`)(...vals);
  } catch (_) {
    return null;
  }
}

function fmtVal(v, fmt) {
  if (v === null || v === undefined) return '';
  if (typeof v === 'string') return v;
  if (fmt) {
    const m = fmt.match(/(\D*)\.(\d+)([fde])(.*)/);
    if (m) return m[1] + Number(v).toFixed(parseInt(m[2])) + m[4];
  }
  if (Number.isInteger(v) || Math.abs(v - Math.round(v)) < 1e-9) return String(Math.round(v));
  return parseFloat(v.toFixed(4)).toString();
}

function interpolate(text, vars) {
  return String(text).replace(/\{\{([^}]+)\}\}/g, (_, inner) => {
    const ci = inner.indexOf(':');
    const exprPart = ci === -1 ? inner.trim() : inner.slice(0, ci).trim();
    const fmtPart  = ci === -1 ? '' : inner.slice(ci + 1).trim();
    const val = evalExpr(exprPart, vars);
    return fmtVal(val, fmtPart);
  });
}

// ── Document parser ───────────────────────────────────────────────────────────
function parseSyn(src) {
  const meta = {};
  const decls = [];
  const nodes = [];
  const widgetMap = {};

  // Pre-scan widgets
  for (const line of src.split('\n')) {
    const s = line.trim();
    let m = s.match(/^\[slider:\s*(\w+),\s*(.*)\]$/);
    if (m) { widgetMap[m[1]] = { widget: 'slider', ...parseKV(m[2]) }; continue; }
    m = s.match(/^\[toggle:\s*(\w+),\s*(.*)\]$/);
    if (m) { widgetMap[m[1]] = { widget: 'toggle', ...parseKV(m[2]) }; continue; }
  }

  const lines = src.split('\n');
  let i = 0;
  let inMath = false; const mathBuf = [];

  while (i < lines.length) {
    const raw = lines[i]; const s = raw.trim(); i++;
    if (!s) { nodes.push({ type: 'blank' }); continue; }

    if (s === '$$') {
      if (inMath) { nodes.push({ type: 'math', content: mathBuf.join('\n') }); mathBuf.length = 0; inMath = false; }
      else inMath = true;
      continue;
    }
    if (inMath) { mathBuf.push(s); continue; }

    let m;
    if ((m = s.match(/^@title\s+(.*)/)))       { meta.title = m[1]; nodes.push({ type: 'title', text: m[1] }); continue; }
    if ((m = s.match(/^@author\s+(.*)/)))       { meta.author = m[1]; continue; }
    if ((m = s.match(/^@description\s+(.*)/)))  { meta.description = m[1]; continue; }
    if ((m = s.match(/^@var\s+(\w+)\s*=\s*(.*)/))) {
      const expr = m[2].replace(/--.*$/, '').trim();
      decls.push({ kind: 'var', name: m[1], expr });
      const w = widgetMap[m[1]];
      if (w) nodes.push({ type: w.widget, name: m[1], label: w.label || m[1],
                           min: parseFloat(w.min||0), max: parseFloat(w.max||100), step: parseFloat(w.step||1) });
      continue;
    }
    if ((m = s.match(/^@const\s+(\w+)\s*=\s*(.*)/))) {
      decls.push({ kind: 'const', name: m[1], expr: m[2].replace(/--.*$/, '').trim() }); continue;
    }
    if ((m = s.match(/^@derive\s+(\w+)\s*=\s*(.*)/))) {
      decls.push({ kind: 'derive', name: m[1], expr: m[2].replace(/--.*$/, '').trim() }); continue;
    }
    if (s === '---') { nodes.push({ type: 'hr' }); continue; }
    if ((m = s.match(/^(#{1,3})\s+(.*)/))) { nodes.push({ type: 'heading', level: m[1].length, text: m[2] }); continue; }
    if (s.startsWith('[') && s.endsWith(']')) continue; // widget lines already consumed
    if (s.startsWith('|')) {
      if (!/^\|[-| :]+\|$/.test(s)) {
        const cells = s.split('|').slice(1, -1).map(c => c.trim());
        const last = nodes[nodes.length - 1];
        if (last && last.type === 'table') last.rows.push({ cells, isHeader: last.rows.length === 0 });
        else nodes.push({ type: 'table', rows: [{ cells, isHeader: false }] });
      } else {
        const last = nodes[nodes.length - 1];
        if (last && last.type === 'table' && last.rows.length) last.rows[last.rows.length - 1].isHeader = true;
      }
      continue;
    }
    if (s) nodes.push({ type: 'text', text: s });
  }

  return { meta, decls, nodes };
}

function parseKV(str) {
  const out = {};
  for (const part of str.split(',')) {
    const eq = part.indexOf('=');
    if (eq !== -1) {
      const k = part.slice(0, eq).trim();
      const v = part.slice(eq + 1).trim().replace(/^["']|["']$/g, '');
      out[k] = v;
    }
  }
  return out;
}

// ── Variable store (reactive) ─────────────────────────────────────────────────
function buildStore(decls, overrides = {}) {
  const store = {};
  // Collect stdlib
  Object.assign(store, STDLIB['math_utils.syn']);

  for (const d of decls) {
    if (d.kind === 'var' || d.kind === 'const') {
      const v = evalExpr(d.expr, store);
      store[d.name] = v !== null ? v : 0;
    }
  }
  // Apply overrides
  Object.assign(store, overrides);

  // Multi-pass derive evaluation
  const derives = decls.filter(d => d.kind === 'derive');
  for (let pass = 0; pass < derives.length + 2; pass++) {
    let changed = false;
    for (const d of derives) {
      const v = evalExpr(d.expr, store);
      if (v !== null && v !== store[d.name]) { store[d.name] = v; changed = true; }
    }
    if (!changed) break;
  }
  return store;
}

// ── React component (optional, loaded only if React is present) ───────────────
let SynDocument = null;

if (typeof require !== 'undefined') {
  try {
    const React = require('react');
    const { useState, useCallback } = React;

    SynDocument = function SynDocument({ src, overrides = {}, className = '' }) {
      const { meta, decls, nodes } = parseSyn(src);
      const [vars, setVars] = useState(() => buildStore(decls, overrides));

      const setVar = useCallback((name, value) => {
        setVars(prev => {
          const next = { ...prev, [name]: value };
          // Re-evaluate derives
          for (const d of decls.filter(d => d.kind === 'derive')) {
            const v = evalExpr(d.expr, next);
            if (v !== null) next[d.name] = v;
          }
          return next;
        });
      }, [decls]);

      return React.createElement('div', { className: `syn-document ${className}` },
        nodes.map((node, idx) => renderNode(node, vars, setVar, idx))
      );
    };

    function renderNode(node, vars, setVar, key) {
      const R = React.createElement;
      switch (node.type) {
        case 'title':   return R('h1', { key, className: 'syn-title' }, node.text);
        case 'heading': return R(`h${node.level}`, { key, className: `syn-h${node.level}` }, interpolate(node.text, vars));
        case 'text':    return R('p',  { key, className: 'syn-text'  }, interpolate(node.text, vars));
        case 'hr':      return R('hr', { key, className: 'syn-hr'    });
        case 'math':    return R('pre',{ key, className: 'syn-math'  }, node.content);
        case 'blank':   return R('br', { key });
        case 'slider':  return R('div', { key, className: 'syn-slider' },
          R('label', null, node.label),
          R('input', { type: 'range', min: node.min, max: node.max, step: node.step,
                       value: vars[node.name] || node.min,
                       onChange: e => setVar(node.name, parseFloat(e.target.value)) }),
          R('span', { className: 'syn-val' }, fmtVal(vars[node.name]))
        );
        case 'toggle':  return R('div', { key, className: 'syn-toggle' },
          R('input', { type: 'checkbox', checked: !!vars[node.name],
                       onChange: e => setVar(node.name, e.target.checked ? 1 : 0) }),
          ' ', node.label
        );
        case 'table':   return R('table', { key, className: 'syn-table' },
          R('tbody', null, node.rows.map((row, ri) =>
            R('tr', { key: ri }, row.cells.map((c, ci) => {
              const Tag = row.isHeader ? 'th' : 'td';
              return R(Tag, { key: ci }, interpolate(c, vars));
            }))
          ))
        );
        default: return null;
      }
    }
  } catch (_) { /* React not available — SynDocument stays null */ }
}

module.exports = { parseSyn, buildStore, evalExpr, fmtVal, interpolate, STDLIB, SynDocument };
