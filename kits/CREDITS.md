# Melasa

Touch drum kit for phone and tablet. Open `index.html` through a web server (or GitHub Pages); the sampled kits are loaded with `fetch`, so opening the file directly from disk only plays the Synth kit.

- Kit: crash, splash, ride, hi-hat (open + closed, closed chokes open), 3 toms, snare, double kick
- Kits: Black Pearl, Red Zeppelin (acoustic, 5 velocity layers), TR-808, Synth
- Multi-touch: hit the center for louder, the rim for softer
- Tap "Edit layout" to move and resize pads (saved per orientation on the device)
- Desktop keys: Q crash, W splash, E ride, A closed hi-hat, Z open hi-hat, S snare, D/F/G toms, Space kick

Planned: piano.

## Credits

Samples in `kits/` are trimmed and converted to MP3. Their licenses are separate from the app code.

- `kits/black-pearl/`, `kits/red-zeppelin/`: AVL Drumkits by Glen MacArthur, [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/). Source: <https://github.com/studiorack/avl-drumkits> (originally bandshed.net/avldrumkits). As adapted files, they stay under CC BY-SA 3.0.
- `kits/tr-808/`: Roland TR-808 sample set by Michael Fischer (Technopolis, 1994), released free by the author. Via <https://github.com/danigb/samples>.
- Synth kit: generated in code.
