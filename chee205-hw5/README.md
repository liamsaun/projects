# CHEE 205 Homework 5: Heat Index in MATLAB

| File | Purpose |
|---|---|
| `heat_index.m` | **Required script (Part 2).** Asks for units, temperature, and RH, then prints the WPC heat index in °F and °C on one line to two decimal places. |
| `decision_tree.md` | **Part 1** outline, plus test cases with their expected outputs for the three screenshots. |
| `heat_index_table.m` | Optional "Diving Deeper" script: 35–115 °F × 5–100 % RH grid, output file, and 3-D surface plot. |
| `heat_index_results.xlsx` | Data file produced by the optional script. The *Results* sheet has 180 rows with T, RH, HI before adjustment, adjustment factor, final HI (°F/°C), and the method used. The *HI Grid (F)* sheet has the same results as a matrix. |
| `heat_index_results.csv` | The same results as plain text. |
| `heat_index_surface.png` | Preview of the surface plot. Rerun the optional script in MATLAB for the final figure. |

## How to run
1. Put the `.m` files in your MATLAB folder (or upload them to MATLAB Online).
2. Type `heat_index` in the Command Window and answer the prompts. Take a screenshot for each test case.
3. (Optional) Type `heat_index_table`. This writes the CSV, XLSX, and PNG files to the current folder.

The scripts were tested in GNU Octave 8.4. The results match the NWS heat index chart.
