/**
 * Syntaxia Reader — Chrome content script
 * Intercepts .syn file loads and renders them as formatted documents.
 */

(function () {
  'use strict';

  // ── Stdlib constants (mirror of stdlib.hpp) ──────────────────────────────
  const STDLIB = {
    'math_utils.syn': {
      PI: Math.PI, E: Math.E, PHI: 1.61803398874989,
      SQRT2: Math.SQRT2, SQRT3: Math.sqrt(3),
      LN2: Math.LN2, LN10: Math.LN10,
      G: 9.80665, C_LIGHT: 299792458,
      RHO_WATER: 1000, RHO_SEA: 1025,
      KG_TO_LB: 2.20462, KM_TO_MI: 0.621371, M_TO_FT: 3.28084,
    }
  };

  // ── Expression evaluator (safe JS eval via Function) ─────────────────────
  function evalExpr(expr, vars) {
    expr = expr.replace(/π/g, String(Math.PI));
    expr = expr.replace(/√\(([^)]+)\)/g, (_, e) => `Math.sqrt(${e})`);
    expr = expr.replace(/√([a-zA-Z_]\w*)/g, (_, v) => `Math.sqrt(${v})`);
    expr = expr.replace(/√(\d+(?:\.\d+)?)/g, (_, n) => `Math.sqrt(${n})`);
    expr = expr.replace(/&&/g, '&&').replace(/\|\|/g, '||');
    const names = Object.keys(vars);
    const vals  = names.map(k => vars[k]);
    try {
      return new Function(...names, `"use strict"; return (${expr});`)(...vals);
    } catch (_) { return null; }
  }

  function fmtVal(v) {
    if (v === null || v === undefined) return '';
    if (typeof v === 'string') return v;
    if (Number.isInteger(v) || Math.abs(v - Math.round(v)) < 1e-9) return String(Math.round(v));
    return parseFloat(v.toFixed(4)).toString();
  }

  function interpolate(text, vars) {
    return text.replace(/\{\{([^}]+)\}\}/g, (_, inner) => {
      const [exprPart, fmtPart] = inner.includes(':')
        ? [inner.slice(0, inner.indexOf(':')), inner.slice(inner.indexOf(':') + 1)]
        : [inner, ''];
      const val = evalExpr(exprPart.trim(), vars);
      if (val === null) return `{{${inner}}}`;
      if (fmtPart) {
        const m = fmtPart.match(/^(\D*)\.?(\d+)([fde])$/);
        if (m) return m[1] + Number(val).toFixed(parseInt(m[2])) + (m[0].match(/[a-z]$/) ? '' : '');
      }
      return fmtVal(val);
    });
  }

  // ── .syn parser ───────────────────────────────────────────────────────────
  function parseSyn(src) {
    const vars = {};
    const nodes = [];
    const lines = src.split('\n');
    let i = 0;

    // Pre-scan widget map
    const widgetMap = {};
    for (const line of lines) {
      const s = line.trim();
      let m = s.match(/^\[slider:\s*(\w+),\s*(.*)\]$/);
      if (m) { widgetMap[m[1]] = { widget: 'slider', ...parseKV(m[2]) }; continue; }
      m = s.match(/^\[toggle:\s*(\w+),\s*(.*)\]$/);
      if (m) { widgetMap[m[1]] = { widget: 'toggle', ...parseKV(m[2]) }; continue; }
    }

    while (i < lines.length) {
      const raw = lines[i];
      const s = raw.trim();
      i++;
      if (!s || s.startsWith('//') || s.startsWith('#!')) continue;

      const titleM = s.match(/^@title\s+(.*)/);
      if (titleM) { nodes.push({ type: 'title', text: titleM[1] }); continue; }

      const descM = s.match(/^@description\s+(.*)/);
      if (descM) { nodes.push({ type: 'desc', text: descM[1] }); continue; }

      if (s === '---') { nodes.push({ type: 'hr' }); continue; }

      const headM = s.match(/^(#{1,3})\s+(.*)/);
      if (headM) { nodes.push({ type: 'heading', level: headM[1].length, text: headM[2] }); continue; }

      const varM = s.match(/^@var\s+(\w+)\s*=\s*(.*)/);
      if (varM) {
        const name = varM[1], expr = varM[2].replace(/--.*$/, '').trim();
        const v = evalExpr(expr, { ...vars, ...STDLIB.math_utils });
        vars[name] = (v !== null) ? v : 0;
        const wInfo = widgetMap[name] || {};
        if (wInfo.widget === 'toggle') {
          nodes.push({ type: 'toggle', name, label: wInfo.label || name, value: vars[name] });
        } else if (wInfo.widget === 'slider') {
          nodes.push({ type: 'slider', name, label: wInfo.label || name, value: vars[name],
                       min: parseFloat(wInfo.min||0), max: parseFloat(wInfo.max||100),
                       step: parseFloat(wInfo.step||1) });
        }
        continue;
      }

      const constM = s.match(/^@const\s+(\w+)\s*=\s*(.*)/);
      if (constM) {
        const expr = constM[2].replace(/--.*$/, '').trim();
        const v = evalExpr(expr, { ...vars, ...STDLIB.math_utils });
        vars[constM[1]] = (v !== null) ? v : 0;
        continue;
      }

      const derM = s.match(/^@derive\s+(\w+)\s*=\s*(.*)/);
      if (derM) {
        const expr = derM[2].replace(/--.*$/, '').trim();
        const v = evalExpr(expr, { ...vars, ...STDLIB.math_utils });
        vars[derM[1]] = (v !== null) ? v : 0;
        continue;
      }

      if (s.startsWith('[') && s.endsWith(']')) { continue; } // already pre-scanned

      if (s.startsWith('|')) {
        // Table row
        if (!nodes.length || nodes[nodes.length - 1].type !== 'table') {
          nodes.push({ type: 'table', rows: [] });
        }
        const isSep = /^\|[-| :]+\|$/.test(s);
        if (!isSep) {
          const cells = s.split('|').slice(1, -1).map(c => c.trim());
          const tbl = nodes[nodes.length - 1];
          tbl.rows.push({ cells, isHeader: tbl.rows.length === 0 });
        } else {
          if (nodes[nodes.length - 1].rows.length)
            nodes[nodes.length - 1].rows[nodes[nodes.length - 1].rows.length - 1].isHeader = true;
        }
        continue;
      }

      if (s.startsWith('$$')) {
        const mathLines = [];
        while (i < lines.length && lines[i].trim() !== '$$') mathLines.push(lines[i++]);
        i++;
        nodes.push({ type: 'math', content: mathLines.join('\n') });
        continue;
      }

      if (s) nodes.push({ type: 'text', text: s });
    }
    return { vars, nodes };
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

  // ── Renderer ──────────────────────────────────────────────────────────────
  function render(src) {
    const { vars, nodes } = parseSyn(src);
    const html = ['<div class="syn-document">'];

    for (const node of nodes) {
      switch (node.type) {
        case 'title':
          html.push(`<h1 class="syn-title">${esc(node.text)}</h1>`); break;
        case 'desc':
          html.push(`<p class="syn-desc">${esc(node.text)}</p>`); break;
        case 'heading':
          html.push(`<h${node.level} class="syn-h${node.level}">${esc(interpolate(node.text, vars))}</h${node.level}>`); break;
        case 'hr':
          html.push('<hr class="syn-hr">'); break;
        case 'text': {
          const t = interpolate(node.text, vars);
          html.push(`<p class="syn-text">${esc(t)}</p>`); break;
        }
        case 'slider':
          html.push(`<div class="syn-slider"><label>${esc(node.label)}</label>
            <input type="range" min="${node.min}" max="${node.max}" step="${node.step}" value="${node.value}" disabled>
            <span class="syn-val">${fmtVal(node.value)}</span></div>`); break;
        case 'toggle': {
          const chk = node.value ? '☑' : '☐';
          html.push(`<div class="syn-toggle">${chk} ${esc(node.label)}</div>`); break;
        }
        case 'math':
          html.push(`<pre class="syn-math">${esc(node.content)}</pre>`); break;
        case 'table': {
          html.push('<table class="syn-table">');
          for (const row of node.rows) {
            const tag = row.isHeader ? 'th' : 'td';
            html.push('<tr>' + row.cells.map(c =>
              `<${tag}>${esc(interpolate(c, vars))}</${tag}>`
            ).join('') + '</tr>');
          }
          html.push('</table>'); break;
        }
      }
    }

    html.push('</div>');
    return html.join('\n');
  }

  function esc(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  // ── Inject ────────────────────────────────────────────────────────────────
  function inject() {
    const pre = document.querySelector('pre');
    if (!pre) return;
    const src = pre.textContent;
    if (!src.includes('@') && !src.includes('<func') && !src.includes('<structura')) return;

    document.title = 'Syntaxia Reader';
    document.body.innerHTML = '';
    document.body.insertAdjacentHTML('beforeend', render(src));
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', inject);
  } else {
    inject();
  }
})();
