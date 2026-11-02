# Part 1: Decision Tree / Code Outline for `heat_index.m`

1. **Start.** Clear the workspace and the Command Window.
2. **Ask for temperature units.** Prompt: "F for Fahrenheit, C for Celsius."
   - Is the answer F or C (upper or lower case)?
     - **No:** print "Invalid choice" and ask again.
     - **Yes:** save it as an upper-case letter.
3. **Ask for the air temperature** in the chosen units.
4. **Ask for relative humidity.**
   - First, explain that RH is entered as a **percent** (55, not 0.55).
   - Is 0 ≤ RH ≤ 100?
     - **No:** print an error and ask again.
     - **Yes:** continue.
5. **Convert to Fahrenheit if needed.**
   - Units = C? → T(°F) = T(°C) × 9/5 + 32
   - Units = F? → use T as entered.
6. **Calculate the simple (Steadman) heat index:**
   HI = 0.5 × [T + 61.0 + (T − 68.0) × 1.2 + RH × 0.094]
7. **Is (HI_simple + T) / 2 ≥ 80 °F?**
   - **No:** final HI = HI_simple, adjustment = 0. Go to step 9.
   - **Yes:** use the full Rothfusz regression:
     HI = −42.379 + 2.04901523T + 10.14333127RH − 0.22475541T·RH − 0.00683783T² − 0.05481717RH² + 0.00122874T²·RH + 0.00085282T·RH² − 0.00000199T²·RH²
8. **Check whether an adjustment factor applies** (Rothfusz branch only).
   - RH < 13 % **and** 80 ≤ T ≤ 112 °F?
     → **subtract** [(13 − RH)/4] × √{[17 − |T − 95|]/17}
   - Otherwise, RH > 85 % **and** 80 ≤ T ≤ 87 °F?
     → **add** [(RH − 85)/10] × [(87 − T)/5]
   - Otherwise → adjustment = 0.
9. **Convert the final HI to Celsius:** HI(°C) = (HI(°F) − 32) × 5/9
10. **Display the results:** the inputs, the method used, the adjustment factor, and
    **one line with HI in °F and °C to two decimal places.**
11. **End.**

## Suggested test cases (one per screenshot)

| Units | Temperature | RH (%) | Path exercised | Expected output |
|---|---|---|---|---|
| F | 90 | 60 | Rothfusz, no adjustment | 99.68 °F, 37.60 °C |
| C | 32 | 70 | Celsius conversion + Rothfusz | 104.74 °F, 40.41 °C |
| F | 85 | 95 | Rothfusz + high-humidity adjustment (+0.40) | 104.61 °F, 40.34 °C |
| F | 100 | 8 | Rothfusz + low-humidity adjustment (−1.05) | 93.41 °F, 34.12 °C |
| F | 70 | 50 | Simple formula only | 69.05 °F, 20.58 °C |

Check: the NWS heat index chart lists 100 °F for 90 °F at 60 % RH, which matches 99.68 °F.
