# Plan D: Bitmap-Icon Rendering Fidelity

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** Improve the visual quality of Hemisphere bitmap icons (LOGIC_ICON, ZAP_ICON, CV_ICON, etc.) when rendered on the NT 256x64 grayscale screen.

**Architecture:** Hemisphere's `drawBitmap8` writes one column-major 8-pixel-tall byte per column directly to screen memory. Current shim implementation in `shim/src/graphics.cpp::drawBitmap8` reproduces this faithfully, but the resulting 12-pixel-wide logic-gate symbols look abstract at NT screen density. Plan D evaluates and (if warranted) implements one of three improvement paths.

**Tech Stack:** C++11, NT API `NT_drawShapeI`/`NT_drawShapeF` primitives for line/circle/rect drawing.

**Pre-req:** Plan C complete (Tier 1 applets shipping).

---

## Decision: which path?

Three candidate approaches. Decide in Task 1 before committing to one.

| Path | Effort | Visual gain | Maintenance cost |
|------|--------|-------------|------------------|
| A: leave as-is | 0 | none | none |
| B: auto-vectorize bitmaps | small (~50 LOC) | low (lines still blocky) | none |
| C: hand-craft replacements for each icon | medium (~10 icons) | high | per-icon update if Phazerville adds icons |

Recommend Path B as default. If Path B output is still illegible after Task 2, fall back to Path C for the icons that matter most.

---

## Task 1: Inventory icons and pick path

**Files:** none modified; investigation only.

- [ ] **Step 1: List all icon bitmaps the shim references**

```bash
grep -rn "gfxBitmap\|gfxIcon" vendor/O_C-Phazerville/software/src/applets/{Logic,AttenuateOffset,Slew,Calculate,Burst}.h
```

Record the full set of LOGIC_ICON, NOTE_ICON, CV_ICON, DOWN_BTN_ICON, ZAP_ICON, etc.

- [ ] **Step 2: Render a screenshot reference**

With Logic applet loaded on NT, switch through each LOGIC_ICON op (AND, OR, XOR, NAND, NOR, XNOR, -CV-). Photograph each.

- [ ] **Step 3: Decide**

If photographs are legible, declare Plan D done (Path A). Otherwise proceed to Task 2 with Path B.

---

## Task 2: Auto-vectorize bitmap drawing (Path B)

**Files:**
- Modify: `shim/src/graphics.cpp` (replace `drawBitmap8`)
- Modify: `shim/include/hem_graphics.h` (no signature change, just impl)

- [ ] **Step 1: Replace `drawBitmap8` to coalesce runs into rectangles**

In `shim/src/graphics.cpp`, change `Graphics::drawBitmap8` to scan each column and emit a rectangle per contiguous run of set bits, then merge with adjacent columns producing identical run patterns into wider rectangles. Pseudocode:

```cpp
void Graphics::drawBitmap8(int x, int y, int w, const uint8_t* data) {
    int col = 0;
    while (col < w) {
        uint8_t pattern = data[col];
        int run_w = 1;
        while (col + run_w < w && data[col + run_w] == pattern) ++run_w;
        // For each contiguous run of 1-bits in `pattern`, emit one rect of size run_w x run_len.
        int row = 0;
        while (row < 8) {
            if (pattern & (1u << row)) {
                int run_h = 1;
                while (row + run_h < 8 && (pattern & (1u << (row + run_h)))) ++run_h;
                NT_drawShapeI(kNT_box, x + col, y + row,
                              x + col + run_w - 1, y + row + run_h - 1, 15);
                row += run_h;
            } else {
                ++row;
            }
        }
        col += run_w;
    }
}
```

Verify `kNT_box` is the filled-rectangle shape; check `vendor/distingNT_API/include/distingnt/api.h` enum names if uncertain.

- [ ] **Step 2: Build**

```bash
make arm
```

- [ ] **Step 3: Hardware test**

Deploy Logic.o (or any applet using gfxBitmap). Confirm icon shapes match Path A baseline but with smoother edges if NT_drawShape anti-aliases.

- [ ] **Step 4: Pass criterion**

Icons readable; no regression in shape correctness.

- [ ] **Step 5: Commit**

```bash
git add shim/src/graphics.cpp
git commit -m "shim: vectorize drawBitmap8 via NT_drawShape rectangles"
```

---

## Task 3 (optional): Hand-craft replacement icons (Path C)

Only execute if Path B output is still illegible.

**Files:**
- Create: `shim/include/icon_overrides.h` with `inline void draw_logic_and(int x, int y)` etc.
- Modify: `shim/src/graphics.cpp::drawBitmap8` to detect known bitmap pointers and call the override.

- [ ] **Step 1: Identify the high-value icons**

LOGIC_ICON[0..5] (6 logic gates) most prominently visible. Skip rare ones.

- [ ] **Step 2: For each icon, write a function using NT_drawShapeI primitives**

Example AND gate:

```cpp
inline void draw_logic_and(int x, int y) {
    // Vertical input lines
    NT_drawShapeI(kNT_box, x, y + 1, x + 1, y + 2, 15);
    NT_drawShapeI(kNT_box, x, y + 5, x + 1, y + 6, 15);
    // Body rectangle
    NT_drawShapeI(kNT_box, x + 2, y + 1, x + 7, y + 6, 15);
    // Rounded right end (approximated)
    NT_drawShapeI(kNT_line, x + 8, y + 2, x + 8, y + 5, 15);
    // Output line
    NT_drawShapeI(kNT_line, x + 9, y + 3, x + 11, y + 3, 15);
}
```

- [ ] **Step 3: Pointer-comparison dispatch in drawBitmap8**

```cpp
if (data == LOGIC_ICON[0]) { draw_logic_and(x, y); return; }
// ... for each known icon
```

Falls through to default rect-coalesce if no match.

- [ ] **Step 4: Build, deploy, verify** each replaced icon.

- [ ] **Step 5: Commit** per icon group, in chunks.

---

## Out of scope

- Animated icons
- Color customization (NT screen is 4-bit grayscale; current rendering uses solid white at intensity 15)
- Per-applet custom icon overrides (icons are part of vendored Hemisphere source; don't fork)

## Risk register

- **NT_drawShapeI integer convention**: pixel coords inclusive? Check `vendor/distingNT_API/examples/` for canonical usage. If exclusive, off-by-one in Task 2 Step 1.
- **Anti-aliasing surprise**: `NT_drawShapeF` anti-aliases; `NT_drawShapeI` may not. Path B uses integer variant for performance; visual gain may be marginal.
- **Pointer-comparison fragility (Path C)**: relies on icon arrays having stable addresses. Phazerville source uses `static const`; addresses stable per-applet but differ across applets if duplicated definitions exist. Verify by grep.
