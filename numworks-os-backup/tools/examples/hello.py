# ============================================================
# hello.py — NumWorks OS starter script
# Demonstrates display and basic Python
# ============================================================
import display
import math

display.fill(display.BLACK)

# Title
display.text("NumWorks OS", 80, 20, display.CYAN, display.BLACK)
display.text("Python Demo", 80, 32, display.GRAY, display.BLACK)

# Draw a simple sine wave
for x in range(320):
    y = int(120 + 40 * math.sin(x * 0.05))
    display.pixel(x, y, display.GREEN)

# Bottom message
display.text("Press any key to exit", 30, 220, display.WHITE, display.BLACK)
