# Melasa

Touch drums and piano for phone and tablet.

**Android:** download the APK from the latest [release](https://github.com/contactvirgilio/Melasa/releases). Each `v*` tag builds a new APK with GitHub Actions (Capacitor wrapper, `.github/workflows/android.yml`).

**Web:** serve the folder over HTTPS (e.g. GitHub Pages). It installs to the home screen and works offline. Opening `index.html` straight from disk only plays the Synth kit.

- Kit: crash, splash, ride, hi-hat (open + closed, closed chokes open), 3 toms, snare, double kick
- Kits: Black Pearl, Red Zeppelin (acoustic, 5 velocity layers), TR-808, Synth
- Pianos (separate screen, pick from the same menu): Grand, Upright, Honky-tonk. 2 octaves in landscape, 1 in portrait, octave shift, sustain. Press nearer the front of a key for louder.
- Multi-touch: hit the center for louder, the rim for softer
- Each kit and piano has its own skin (Auto). Pick a different one from the Skin menu (under "Layout" for drums, in the toolbar for piano)
- Tap "Layout" to move and resize pads (saved per orientation on the device)
- Desktop keys (piano): A W S E D F T G Y H U J K = one octave from C
- Desktop keys (drums): Q crash, W splash, E ride, A closed hi-hat, Z open hi-hat, S snare, D/F/G toms, Space kick

## Credits

Samples in `kits/` and `pianos/` are trimmed and converted to MP3. Their licenses are separate from the app code.

- `kits/black-pearl/`, `kits/red-zeppelin/`: AVL Drumkits by Glen MacArthur, [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/). Source: <https://github.com/studiorack/avl-drumkits> (originally bandshed.net/avldrumkits). As adapted files, they stay under CC BY-SA 3.0.
- `kits/tr-808/`: Roland TR-808 sample set by Michael Fischer (Technopolis, 1994), released free by the author. Via <https://github.com/danigb/samples>.
- `pianos/grand/`: Splendid Grand Piano (Steinway), public domain. Source: <https://github.com/sfzinstruments/SplendidGrandPiano>
- `pianos/upright/`: Upright Piano, Yamaha, from the Versilian Community Sample Library, CC0. Source: <https://github.com/sgossner/VCSL>
- `pianos/honky/`: Old Piano (Francis Bacon), CC0. Source: <https://freepats.zenvoid.org/Piano/honky-tonk-piano.html>
- All pianos via <https://github.com/danigb/samples>; one velocity layer, about every third note, trimmed to 5 seconds.
- Synth kit: generated in code.
