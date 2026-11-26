# touchstone viewer

A desktop viewer for S-parameter measurements, ie touchstone (.sNp) + spectrum analyzer csvs (some of them, if its not hameg then plox patch your own parsing) 
avaliable noncommercial use, see [License](#license).

## some things that might not be immediately obvious to anyone with a pulse + congnitive skills to comprehend what github is for

shift + drag in a panel selects an x range. The legend then shows the median of each visible curve in that range. 
rightclick on that for context menu

ctrl+click on a curve on any plot = marker in that place with exact values

labels are made from the file names. words that appear in all filenames are dropped e.g.: `800mm_cable_25-AB` and `900mm_cable_3-AB` give `800mm 25` and `900mm 3`.

highlighting marks the frequency ranges in which the curves are ordered by a number in their file names (ie regex).

- `number`: which number of the file name gives the order i.e. auto-regex that assumes you have some sort of numerical order in your filenames (e.g. epses of shit you measured)
- `allowed rank difference`: 0 means each curve must be exactly at its place; 1 means it can be one place off, and so on.
- `curves to leave out`: up to 3 curves that break the order most are left out and drawn dotted.
- `advanced`: if for some reason you need to write your own regex by hand.

files with the same number have no order between them. that does not actually break anything sans that pair

time view shows the time response of each curve, obviously.

you can do time domain gating there: `settings` in the top bar opens the settings panel: the gate (on or off, center and span for each parameter) and, under `Advanced`, the gate method (kernel convolution (ie, as i understand it the way that some siglent and rhode schwartz hardware does things) or multiplication in time (which i have personally seen give wildly misleading results on seemingly normal measurments but that is probably my skill issue), the gate window, the gate mode (bandpass or bandstop), the band edge extension, and the window and zero padding of the time response. the gate band can be dragged in the plot. with the gate on, the time view also obviously shows the gated spectrum of each parameter.

Tse settings that change what a plot shows are stated in the note below the title. that is an intentional design choice to mitigate multiple, severe skill issues. easy to patch out, would not recomend doing that unless you work alone or have god-like communication skills.

you can also save things: `save session` or ctrl+S writes one `.tvsession` file. it holds all loaded measurement files and the state of the window ie everything basically. 

you open a session file like a measurement file

ctrl + `=` makes text and controls larger, ctrl + `-` smaller, ctrl + `0` returns to scale 1.

## command line

```
touchstone_viewer.exe [files or folders ...] [--size WIDTHxHEIGHT] [--scale FACTOR] [--log] [--stress]
```

- `--size`: window size, for example `1500x900`.
- `--scale`: interface scale, for example `1.25`.
- `--log`: writes a log file. Windows: `%LOCALAPPDATA%\Touchstone Viewer\touchstone-viewer.log`. macOS: `~/Library/Logs/touchstone-viewer.log`.
- `--stress`: speed test with generated curves.

## building

CMake 3.24 or higher and GCC or Clang with C++20. GLFW, Dear ImGui, ImPlot and stb_image_write are in `basic/third_party` under their own licenses.

From the folder `basic`:

```
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Presets:

| preset | result |
|---|---|
| `release` | the macOS program |
| `debug` | the same with sanitizers and library checks |
| `windows` | the Windows program, cross-built on macOS or Linux with mingw-w64 |
| `core` | the numerical code and its tests only, without the window; this one also builds on Linux |

The window tests of `ctest` need a display.

## License

[PolyForm Noncommercial License 1.0.0](LICENSE.md). 
