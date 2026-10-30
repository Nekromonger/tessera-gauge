# Needle gauge

A sensor as a dial with a needle: what it measures on top, a half-round dial with a needle in the middle, and the value
with its unit under it.

The filled part of the arc follows the needle, in the screen's accent colour or in green, amber and red by where the
value stands: red when high (power, CO2) or red when low (a battery).

## Set up

1. In Tessera, open **Plugins**, choose **Add with a link**, and paste `https://github.com/Nekromonger/tessera-gauge`.
2. Tick your screens. Each screen builds once with the plugin.
3. In **Layout**, add the tile **Needle gauge** to a page, at least one column wide and two rows high, and choose the
   sensor in its inspector.
4. Set **Lowest value** and **Highest value** to the range of the dial (0 and 100 for a battery in percent).
5. Optional: give it a **Title**, choose the **Decimals** and the **Colours**.

## Good to know

- Works with sensors, number and input_number entities whose state is a number.
- A value outside the range keeps the needle at the end of the dial; the number under it is the real value.
- On a tile too small for the dial it shows the title and the value only.
- Works on every board, the CYD included.
