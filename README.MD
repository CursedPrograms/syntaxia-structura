[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  
  <br>
<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>
  <br>

<div align="center">
    <img alt="Git" src="https://img.shields.io/badge/git%20-%23323330.svg?&style=for-the-badge&logo=git&logoColor=white"/>
  <img alt="PowerShell" src="https://img.shields.io/badge/PowerShell-%23323330.svg?&style=for-the-badge&logo=powershell&logoColor=white"/>
  <img alt="Shell" src="https://img.shields.io/badge/Shell-%23323330.svg?&style=for-the-badge&logo=gnu-bash&logoColor=white"/>
  <img alt="Batch" src="https://img.shields.io/badge/Batch-%23323330.svg?&style=for-the-badge&logo=windows&logoColor=white"/>
  </div>
  <br>

# Syntaxia & Structura

## Language Overview

Syntaxia & Structura is a hybrid markup/programming language combining HTML-like structure with Python-style semantics and C-style operators.

## Core Syntax Rules

1. **Mandatory HTML-like tags** for all code blocks
2. **Python-style indentation** for internal blocks
3. **C-style operators**: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&`, `&&`, `|`, `||`, `!`
4. **Math symbols**: `π` (Pi), `√` (square root)
5. **Explicit type conversion**: `<>target_type==source_value<>`
6. **Script references/imports**: `<@>other_script.syn</@>`

## Tags Reference

| Tag | Purpose |
|-----|---------|
| `<@>filename.syn</@>` | Import/using another Syntaxia script |
| `<func name="...">...</func>` | Define a function |
| `<var name="..." type="...">value</var>` | Define variable |
| `<if condition="...">...</if>` | Conditional block |
| `<loop type="..." count="...">...</loop>` | Loop block |
| `<print>expression</print>` | Output to console or UI |
| `<>target_type==source_value<>` | Explicit type conversion |

## Example

```xml
<@>math_utils.syn</@>

<func name="physics_demo">
    <var name="speed" type="float">9.8</var>
    <var name="time" type="float">5</var>
    <var name="distance" type="float">speed * time + 0.5 * 9.8 * time * time</var>

    <if condition="distance > 0 && speed > 0">
        <print>Distance traveled: distance</print>
    </if>

    <print>Half of π: π / 2</print>
    <print>Square root of 49: √49</print>

    <var name="distance_int" type="int"><>int==distance<></var>
    <print>Distance as integer: distance_int</print>
</func>
<br>
<div align="center">
© Cursed Entertainment 2026
</div>
<br>
<div align="center">
<a href="https://cursed-entertainment.itch.io/" target="_blank">
    <img src="https://github.com/CursedPrograms/cursedentertainment/raw/main/images/logos/logo-wide-grey.png"
        alt="CursedEntertainment Logo" style="width:250px;">
</a>
</div>
