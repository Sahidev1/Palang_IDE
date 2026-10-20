# Phases 4–5 — Inline symbolic math

Design for Phase 4 (§1–2: symbol shortcuts and palette) and Phase 5
(§3–7: linear syntax, file format, rendering, code generation, types).
Moved out of PLAN.md, where it was §6. Rules that apply to every phase are
in ARCHITECTURE.md.

## 1. Symbol shortcuts
- Typing `_Name` followed by a non-letter expands to the symbol: `_Sigma(` → `Σ(`, `_sigma` → `σ`, `_sum` → `∑`, `_int` → `∫`, `_leq` → `≤`, `_to` → `→`.
- **Case-sensitive** (`_Sigma` Σ vs `_sigma` σ).
- **Expansion happens only inside math regions** so normal code (`my_pi`, `_private`) is never touched.
- **Ctrl+Z right after an expansion** restores the typed text.
- **Completion popup** while typing (`_Si…` → list of Σ Sigma, σ sigma, ∼ sim) via a GtkSourceView completion provider.
- **Letters and operators are separate entries:** Σ (U+03A3, Greek letter: a name like a function `Σ(x)`) vs ∑ (U+2211, summation operator). This matters for code generation.
- **Open decision:** `_` is also the natural subscript marker (`x_i`). Rule proposal: `_` + a *known symbol name* = shortcut, otherwise subscript. Alternative: use `\` as the shortcut prefix (Julia/LaTeX style). Decide before Phase 4.

## 2. Symbol palette (virtual keyboard)
- `GtkPopover` with a `GtkFlowBox` grid, opened by Ctrl+M or a toolbar button.
- Tabs: Greek · Operators (∑ ∏ ∫ ∂ ∇ √ ±) · Relations (≤ ≥ ≠ ≈ ≡ ∝) · Sets & logic (∈ ∉ ⊂ ∪ ∩ ∀ ∃ ¬ ∧ ∨) · Arrows (→ ⇒ ↔ ↦) · Structures (fraction, power, subscript, root, matrix templates).
- Hover shows name + shortcut ("Σ — `_Sigma`") so users learn to type them.
- Search box ("integral" → ∫ ∬ ∮), recently-used row.
- **Symbol table in `data/symbols.json`** (name, glyph, kind = letter/operator/relation…, ASCII name for codegen), loaded with json-glib into a `GHashTable`. Start with about 200 curated entries; users can extend it in `~/.config/palang/symbols.json`. A larger set can be generated from Julia's completion table / LaTeX `unicode-math`.

## 3. Linear input syntax
Borrow from Microsoft's **UnicodeMath** linear format:
- `_` subscript, `^` superscript, `/` fraction, `( )` grouping, `√( )` root, `_sum_(i=1)^n`.
- Example: `∑_(i=1)^n x_i^2 / n`.
- A small hand-written parser in `math/palang-math-parser.c` (recursive descent, works on UTF-8 via `g_utf8_*` functions) → our own math AST (a tree of `PalangMathNode` structs). This AST feeds both rendering and codegen.

## 4. File format (in-file markers)
Everything lives in the source file; the comment prefix comes from GtkSourceView language metadata (`line-comment-start`).

```python
# @math Σ(x) = 1 + x  @f:3fa9c2 @c:81d0e4
def Σ(x):
    return 1 + x
# @end
```

- The formula is stored in **Unicode linear form**, so it's readable in any editor, on GitHub and in diffs.
- `@f` = hash of the normalized formula + target language + generator version; `@c` = hash of the generated code.
  - Formula hash mismatch → regenerate.
  - Code hash mismatch → code was edited outside the formula (e.g. in another editor) → warn, offer "regenerate" or "keep edit".
- The file always compiles/runs anywhere; other tools just see comments + normal code.

## 5. Display in the editor
- The `@math` marker line, the code, and `@end` get the `invisible` tag.
- A `TextChildAnchor` at the block start holds the **rendered formula widget** plus a status icon: ✓ up to date · ⟳ generating · ⚠ failed/stale.
- Click the formula → edit mode (shows the raw linear text); leaving edit mode re-renders and triggers generation.
- "Show generated code" toggle per block and globally (also on hover).
- **Rendering stages:**
  1. Unicode text with Pango sub/superscript markup (cheap, enough at first).
  2. A real 2D layout (fractions, stacked limits, roots): our own layout engine drawn with GSK/Cairo from the math AST, or MicroTeX via FFI.

## 6. Formula → code translation
Runs when the user **finishes** an expression (leaves the block / Enter / command), never per keystroke. If the hash is unchanged, nothing happens.

```
formula changed
  → Tier 1: parser → AST → deterministic codegen → write hidden code (auto)
  → parse failed / ambiguous → Tier 2: AI backend → show diff → user accepts
```

**Tier 1, deterministic (no AI, free, offline):**
- Start with **our own C codegen** from the AST (one small emitter per target language): arithmetic, function definitions, sums/products as loops/iterators, powers, roots, common functions — for Python, Rust, C/C++, JavaScript.
- Later, for symbolic work (derivatives, integrals, simplification): a **SymPy sidecar** — a small Python helper (`python3-sympy` from apt) started with `GSubprocess`, talking JSON over stdin/stdout; use SymPy's printers (`pycode`, `ccode`, `rust_code`, `jscode`, …).

**Tier 2, AI (context-dependent cases):**
- Is `x` a scalar or an array? Is `xy` a product or one name? Which existing variables/functions to use? Unusual notation.
- Sends formula + surrounding code through the **same provider layer** as autocomplete (API key or local Ollama).
- **Claude Code works here:** speed doesn't matter for this step, so the IDE can run the user's own installed `claude -p "…"` (their own login) as an optional backend.
- **AI output is never accepted silently**: the user approves a diff once, because they won't see the code later.

**Naming rules per project:** keep Unicode identifiers (`Σ`, fine in Python/Rust) or convert them to ASCII (`Σ` → `Sigma`) for C/C++ or strict style guides. Operators (∑, ∫) always become code, never identifiers.

**Example:**
| Formula | Python | Rust |
|---|---|---|
| `Σ(x) = 1 + x` | `def Σ(x): return 1 + x` | `fn sigma(x: f64) -> f64 { 1.0 + x }` |
| `∑_(i=1)^n x_i^2` | `sum(x[i-1]**2 for i in range(1, n+1))` | `(1..=n).map(\|i\| x[i-1].powi(2)).sum::<f64>()` |

(The `i-1` comes from the index rule in §7: math counts from 1, arrays from 0.)

## 7. Functions, scope and types

Formulas can go anywhere a comment can, and Python/JavaScript are easy because they don't declare types. C, C++ and Rust need types, and a formula doesn't say what they are. This section collects what the generator must handle.

**Two kinds of block:**
| Kind | Example | Generates |
|---|---|---|
| Function definition | `f(x) = x^2 + 1` | `def f(x): return x**2 + 1` / `double f(double x) { return x*x + 1.0; }` |
| Expression / assignment, inside a function | `E = 1/2 m v^2` | `E = 0.5 * m * v**2` / `double E = 0.5 * m * v * v;` |

```python
def kinetic(m, v):
    # @math E = 1/2 m v^2   @f:…
    E = 0.5 * m * v**2
    # @end
    return E
```

**Scope rules:**
- **Indentation** comes from the marker line (tabs or spaces as in the file). In Python, wrong indentation breaks the program.
- **Assign or declare:** for `y = …` the generator must know whether `y` already exists (`y = …;`) or is new (`double y = …;`, Rust `let` / `let mut`).
- **Unknown names:** a formula using `v` where no variable `v` exists shows ⚠ instead of generating code that can't compile.
- **Name collisions:** a function-defining formula `f(x) = …` in a file that already has an `f` shows ⚠.
- **C has no nested functions:** function-defining blocks must be at the top level in C files (Python and Rust allow them nested).

**Indexing: math counts from 1, code from 0.** In `∑_(i=1)^n x_i`, `x_1` is the first element, i.e. `x[0]`. Written naively, the code skips the first element and reads past the end (an IndexError in Python, a memory error in C). Default: **math index 1 = array index 0** (formulas from books and papers are 1-based); override per block with `@index 0` when the formula is already 0-based. The rule is part of the formula hash.

**Imports and headers:** generated code may need `import math`, `#include <math.h>` (plus `-lm` when linking), or a Rust `use`. The block's status shows ⚠ "needs `import math`" with a one-click fix that adds it at the top of the file (outside the hidden block, so it's visible). Palang never edits build files silently.

**Typing traps:**
- **Integer division in C:** `1/2 * x` is always 0, so literals become `0.5` or `1.0/2.0`.
- **Rust doesn't mix types:** `1 + x` with `x: f64` doesn't compile; it needs `1.0 + x`.
- **Scalar or array?** Is `x` in `x_i` an array, and is `x` a Python list or a NumPy array?
- **Return type:** is `f(n) = n!` an integer or a real number?

**How types get decided** (the steps work together, in this order):
1. **A default number type per project:** `double` (C/C++), `f64` (Rust). Literals are written to match (`1.0`, not `1`).
2. **Hints from the notation:** `x_i` means indexable; the variable of `∑_(i=1)^n` is an integer; `n!` suggests an integer; in Python, NumPy when the file imports it.
3. **Types of existing variables from the language server (Phase 2):** inside a function, `clangd` / `rust-analyzer` already know `m` is a `double`. This is deterministic and needs no AI, which is one more reason Phase 2 comes before Phase 5.
4. **Optional annotations in the marker** when the default is wrong, e.g. `# @math f(n) = n!   @types n:int f:int64`. Types are part of the formula hash, so changing them regenerates the code.
5. **Ask when unsure:** ⚠ with a quick choice ("`x` is: number / array / matrix"); the answer is saved as an annotation. AI is the last fallback, behind the usual approval.
6. **Check the result:** language-server errors in the hidden code appear as ⚠ on the formula, so a type error shows without opening the code.

**Scope for Phase 5:** start with **Python** (untyped, so most of this disappears), then C and Rust. Support both block kinds from the start.

## 8. Verification

- **Phase 4:** `_Sigma(` → `Σ(` inside a math block, untouched outside; Ctrl+Z reverts; palette inserts symbols; custom symbols from the user's `symbols.json` appear.
- **Phase 5:** write `Σ(x) = 1 + x` → generated code hidden, file runs with `python3`; the file opened in another editor shows comment + code; editing the code externally → ⚠ stale on reopen; AI fallback shows a diff before inserting.
