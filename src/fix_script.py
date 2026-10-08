#!/usr/bin/env python3
import re

# Read the file
with open('C:/Users/X1/Documents/Codex/2026-10-05/c/outputs/src/home_hub.cpp', 'r') as f:
    content = f.read()

# Define the exact text to replace
old_text = '''   String userDisp = apUserValue.length() ? apUserValue : "No value yet";
    card(12, 222, 296, 22, "User Input", userDisp);

   // Back button (hover support)
   bool backHover = false;
   // Hover state is tracked by checking touch position in handleTap
   int backFill = backHover ? c.surfaceRaised : c.surface;
   int backBorder = backHover ? c.accent : c.surfaceRaised;
   tft.fillRoundRect(12, 250, 65, 14, 5, backFill);
   tft.drawRoundRect(12, 250, 65, 14, 5, backBorder);
   tft.setTextColor(backHover ? c.header : c.text, backFill);
   tft.setTextSize(1);
   tft.drawCentreString("Back", 44, 256, 2);

   card(82, 250, 226, 14, "Refresh");'''

# Define the new text
new_text = '''   String userDisp = apUserValue.length() ? apUserValue : "No value yet";
    card(12, 222, 296, 22, "User Input", userDisp);

   card(12, 250, 65, 14, "Back");
   card(82, 250, 226, 14, "Refresh");'''

# Perform the replacement
new_content = content.replace(old_text, new_text)

# Write back to file
with open('C:/Users/X1/Documents/Codex/2026-10-05/c/outputs/src/home_hub.cpp', 'w') as f:
    f.write(new_content)

print("File updated successfully")
