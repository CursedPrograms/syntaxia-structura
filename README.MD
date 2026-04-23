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
  <img alt="C++" src="https://img.shields.io/badge/C++-%23323330.svg?&style=for-the-badge&logo=cplusplus&logoColor=white"/>
  <img alt="Win32" src="https://img.shields.io/badge/Win32-%23323330.svg?&style=for-the-badge&logo=windows&logoColor=white"/>
  <img alt="Git" src="https://img.shields.io/badge/git-%23323330.svg?&style=for-the-badge&logo=git&logoColor=white"/>
</div>
<br>

# Syntaxia & Structura

A deterministic, reactive, math-driven document system with a visual page-layout editor and an Acrobat-style document reader. Documents are saved as `.syn` files — a hybrid markup format where layout, logic, and styling coexist in one file. Styling is always embedded; there is no separate `.strss` file.

---

## Applications

### StructArch.exe — Page Layout Editor

An InDesign-style desktop publishing tool for creating `.syn` documents.

**Features**
- Physical page canvas — A4, A3, A5, Letter, Legal, Tabloid
- Draw text boxes by clicking and dragging on any page
- Select tool — click to select, drag to move, right-click or Delete to remove
- Multi-page support — add and delete pages from the left panel
- Properties panel — text content, font size, alignment (L/C/R), text colour, background colour, variable binding
- HSV colour wheel picker with live hex `#RRGGBB` input
- Zoom (`Ctrl+scroll` or toolbar buttons), fit-to-page
- Saves and opens `.syn` files
- Shortcuts: `Ctrl+S` save · `Ctrl+O` open · `Ctrl+N` new · `Delete` remove box · `T` text-box tool

### SynReader.exe — Visual Document Reader

An Acrobat-style print-preview reader for `.syn` documents.

**Features**
- Opens with a file picker if launched without arguments
- Drag a `.syn` file directly onto the window
- Pages rendered as physical white sheets on a dark slate background
- Handles both StructArch layout format and modern Structura document format
- Modern format (headings, paragraphs, tables, math blocks) is automatically reflowed onto pages
- Zoom and scroll; `◀ ▶` page navigation; `Home` / `End` keys jump to first/last page
- Status bar shows document title, author, page count, and current zoom

---

## .syn File Format

`.syn` files are self-contained — layout, logic, and styling are all embedded.

### StructArch layout format

```
@title   My Document
@author  Farica Kimora
@pagesize A4

@page 0
@textbox id=tb1 x=20 y=20 w=170 h=40 color=#000000 hasbg=0 size=14 align=1
Section Title
@end
@textbox id=tb2 x=20 y=70 w=170 h=60 size=11 align=0 color=#333333
Body text. Supports word-wrap.
@end
@endpage
```

### Modern Structura document format

```
@title   Ocean Pressure Simulator
@author  Cursed Entertainment

# Physics Model

$$
  P(d) = P₀ + ρ · g · d
$$

@var    depth    = 0
@const  g        = 9.81
@derive pressure = 1025 * g * depth
```

### Page sizes

| Name    | Width × Height |
|---------|----------------|
| A4      | 210 × 297 mm   |
| A3      | 297 × 420 mm   |
| A5      | 148 × 210 mm   |
| Letter  | 216 × 279 mm   |
| Legal   | 216 × 356 mm   |
| Tabloid | 279 × 432 mm   |

---

## Build

Requires MinGW (GCC 6+).

```bat
build.bat
```

Or manually:

```bat
g++ -std=c++14 -mwindows -O2 -static-libgcc -static-libstdc++ ^
    -o StructArch.exe maker\structarch.cpp ^
    -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32 -lshell32

g++ -std=c++14 -mwindows -O2 -static-libgcc -static-libstdc++ ^
    -o SynReader.exe renderer\synreader.cpp ^
    -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32 -lshell32
```

Both executables are fully self-contained — no external DLL dependencies.

---

## Project Structure

```
syntaxia-structura/
├── StructArch.exe        — page layout editor
├── SynReader.exe         — visual reader
├── build.bat             — build script
├── maker/
│   ├── structarch.cpp    — StructArch source
│   └── maker.cpp         — block editor source (legacy)
├── renderer/
│   ├── synreader.cpp     — SynReader source
│   └── stdlib.hpp        — embedded standard library
├── examples/             — sample .syn documents
├── extension/            — Chrome browser extension
├── npm/                  — Node.js / React library
└── structura/            — language specification
```

---

## Browser & Web

**Chrome Extension** — navigate to a `.syn` file in Chrome and it renders inline.

**npm Package** — `<SynDocument src={content} />` React component for web embedding.

---

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
