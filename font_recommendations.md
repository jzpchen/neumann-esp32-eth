# Font Options & Recommendations

This document outlines recommended font upgrades for both the **Web UI** (Google Fonts) and the physical **OLED Display** (Adafruit GFX).

---

## 1. Web UI Font Options

The current Web UI uses **Outfit**. Below are alternative modern web fonts that fit sleek, premium audio interfaces:

| Font Name | Style Type | Visual Vibe | Google Fonts Integration |
| :--- | :--- | :--- | :--- |
| **Inter** | Clean Sans-Serif | Professional, ultra-clean, neutral, and readable. Standard for modern web apps. | `<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;800&display=swap" rel="stylesheet">` |
| **Plus Jakarta Sans** | Geometric | Sophisticated curves, high-end tech aesthetic. Outstanding for titles and large numbers. | `<link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@300;400;600;800&display=swap" rel="stylesheet">` |
| **Sora** | Tech-Geometric | Futuristic, bold geometric shapes. Great for numbers and buttons. | `<link href="https://fonts.googleapis.com/css2?family=Sora:wght@300;400;600;800&display=swap" rel="stylesheet">` |
| **Syne** | Stylized | Artistic, wide, and distinctive. Gives an "exclusive audio hardware" look. | `<link href="https://fonts.googleapis.com/css2?family=Syne:wght@400;600;800&display=swap" rel="stylesheet">` |

### To apply a new font in `html.cpp`:
1. Find `<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">` (Line 10) and replace it with the new link.
2. Update the CSS body font family (Line 33):
   ```css
   body {
       font-family: 'Plus Jakarta Sans', sans-serif;
       ...
   }
   ```

---

## 2. OLED Display Font Options (Adafruit GFX)

Adafruit GFX includes beautiful custom fonts that make the display look like a commercial audio product.

### Option A: Standard Font (Current)
* **Description**: Classic pixel-style block lettering.
* **Pros**: Simple, highly readable, occupies no flash storage space, uses top-left alignment coordinates.
* **Cons**: Looks generic and retro/blocky.

### Option B: FreeSansBold (Recommended for Premium Look)
* **Description**: A clean, anti-aliased sans-serif vector font.
* **Pros**: Smooth curves, looks highly professional.
* **Cons**: Cursor coordinates change from **top-left** to **bottom-left baseline**.
* **Suggested Sizes**:
  * Volume Level (`%.1f`): `FreeSansBold18pt7b`
  * "dB" unit & "L/R" Status: `FreeSans9pt7b` or `FreeSansBold9pt7b`

### Option C: FreeMonoBold (Monospaced)
* **Description**: Fixed-width serif vector font.
* **Pros**: Character widths are identical. The numbers will never "jump" or wiggle left-to-right when scrolling through numbers of different widths (e.g., changing from `11.1` to `88.8`).
* **Cons**: Slightly blockier text shape.
* **Suggested Sizes**: `FreeMonoBold18pt7b`

---

## 3. OLED Font Integration Guide

To use custom fonts in your firmware, you must include the font header and adjust the layout coordinates.

### Code Integration Example:

```cpp
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 1. Draw Large Volume Level (size 1 using 18pt font = ~24px tall)
  display.setFont(&FreeSansBold18pt7b);
  display.setTextSize(1);
  
  // CRITICAL: Custom fonts use baseline coords! 
  // A Y cursor of 8 would render off-screen. We shift Y down by ~26px.
  display.setCursor(20, 28); 
  display.printf("%.1f", vol);

  // 2. Draw "dB" unit using a smaller font
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(102, 28); // Align with volume baseline
  display.print("dB");

  // Reset font back to default (nullptr) for standard status bar drawing
  display.setFont(nullptr); 
  display.setTextSize(1);
  display.setCursor(12, 48);
  display.print("L");
  
  // (Draw speaker graphics using default coordinates...)
  
  display.display();
}
```

### Layout Shifting Guide for Adafruit Custom Fonts:
Custom fonts are drawn with the cursor representing the **bottom-left baseline** of the character, whereas the default system font uses the **top-left corner**.

* For `FreeSansBold18pt7b`: Increase your vertical `Y` cursor by **20 to 24 pixels** to prevent text from being cut off at the top of the display.
* For `FreeSansBold9pt7b`: Increase your vertical `Y` cursor by **10 to 12 pixels**.
