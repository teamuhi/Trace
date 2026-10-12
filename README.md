# Trace

A fast, stable, cross-platform paint application in the spirit of MS Paint, with more features. It is written in C++20 and runs on Windows, Linux and macOS.

## Key features

• Standard paint features

• Resize, canvas size and crop

• Rulers and guides with snapping

• Three modes behind a switcher: Paint, Pixel Art (layers, pixel-perfect drawing, `.tpix` projects) and Flowchart (still being built)

• To be added soon

## Tech stack

| Concern | Choice |
|---|---|
| Language | C++20 |
| Window, input, clipboard, dialogs | [SDL3](https://libsdl.org) |
| UI | [Dear ImGui](https://github.com/ocornut/imgui) (docking branch features) on the SDL3 renderer backend |
| Image I/O | stb_image / stb_image_write, msf_gif (vendored in `third_party/`) |
| Fonts | Inter and JetBrains Mono (SIL OFL, vendored in `third_party/fonts/`, compiled into the program) |
| Build | CMake 3.25+, Ninja, vcpkg (manifest mode) |
| Tests | doctest + CTest |
| CI | GitHub Actions (Windows, Linux, macOS), vcpkg pinned to a baseline commit with binary caching, warnings as errors, release builds uploaded as artifacts |

## Status

**Pixel Art mode (layers and the workspace).** Pixel Art is now a real mode: a layered canvas of up to 1024 x 1024 pixels with its own tools, a Layers panel, and `.tpix` project files. Animation frames are stored in the file and in the document, but the Timeline, onion skin, palette tools, symmetry, tile preview, image conversion and GIF / sprite sheet export come in later versions. See [Pixel Art](#pixel-art). Flowchart is still a placeholder; Paint behaves exactly as before.

**Modes (foundations).** Trace is growing two more modes next to Paint, **Flowchart** and **Pixel Art**: the mode switcher, per-mode workspaces, a shared project file container and an Export command. See [Modes](#modes).

**M6: Polish, then an interface pass.** Toast notifications, tuned release builds and a pinned, cached CI build for Windows, Linux and macOS, followed by icon toolbar buttons, a Theme page with editable colors, background patterns, interface fonts and importable language packs.

**Settings.** File > Settings (Ctrl+,) opens a window with eight categories: General (interface language and font, interface scale, size of a new canvas, undo memory), Theme (appearance, accent color, every interface color), View (pixel grid, rulers, guides and snapping, background patterns, Tools panel, status bar, CPU and RAM readout, how a new canvas is zoomed), Tools (restore the last session's tool settings or start from chosen values, start tool), Pixel Art (default canvas size, the zoom from which the pixel grid shows, pixel-perfect drawing on at start), Scripts (placeholder for a future release), Keybinds (rebind every shortcut, with conflict warnings) and About. Settings are saved to `settings.ini` in the per-user data directory (next to `imgui.ini`; on Windows `%APPDATA%\Trace\Trace`) and a missing or damaged file falls back to the defaults.

### Modes

The **Paint | Flowchart | Pixel Art** switcher sits at the right end of the menu bar (the shown mode is filled with the accent color, in both themes). You can also switch with **Ctrl+Shift+1 / 2 / 3** (rebindable under the Modes group). One mode is shown at a time and each keeps its own document, windows and dock layout, so switching back finds everything as you left it. The last mode is remembered between runs.

- **Paint** is the full image editor described on this page.
- **Pixel Art** is described under [Pixel Art](#pixel-art).
- **Flowchart** is in progress: for now it shows a placeholder window and cannot create or open documents. Its project file (`.tflow`) is already recognised by content, so Open and drag-and-drop explain that the mode is not ready instead of failing with a vague error.
- Switching is refused (the switcher is dimmed) while a tool gesture, a dialog or a prompt is open.
- **Quitting** with unsaved changes in more than one mode asks about each in turn, showing the mode it is asking about; Cancel on any prompt keeps Trace open.
- Shortcuts are scoped to the mode that is shown, so a key such as `R` can mean different things in different modes without ever firing twice. The Keybinds page warns about a conflict only between shortcuts that can be active at the same time.
- **File > Export...** (Ctrl+Shift+E) is for writing a finished picture out of a project mode (PNG today, and later SVG, GIF and sprite sheet). It is separate from Save, so a project is never silently flattened. Paint saves images directly, so it has nothing to export and the item is disabled there.

### Pixel Art

Switch to **Pixel Art** (Ctrl+Shift+3) for a layered canvas meant for sprites and tiles. It keeps its own document, tools (with their own colors and sizes), windows and dock layout, so nothing here changes Paint.

- **Canvas.** File > New makes a transparent 64 x 64 canvas (change it under Settings > Pixel Art); every side is 1 to 1024 pixels, in New, Resize and Canvas Size. A new or opened document is fitted to the window, enlarged in whole steps so every pixel is the same size, and the zoom commands step through whole levels from 100% up. The pixel grid shows from 400% (a setting) when View > Pixel Grid is on; rulers, guides, snapping and the backdrop and transparency patterns work as in Paint.
- **Tools** (Pixel Tools panel, same keys as Paint): Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Fill, Picker and Select. Everything is whole pixels: there is no antialiasing and no gradient.
  - **Pencil** has a **Pixel-perfect** option that removes the extra corner pixel of every L-shaped step, so a freehand line stays one pixel thick (Settings > Pixel Art sets whether Trace starts with it on; it is on by default).
  - **Brush** and **Eraser** stamp a round or a square tip of 1 to 16 pixels (a square stamp is exact; an odd size is centered on the pixel). The eraser always clears to transparency.
  - **Line, Rectangle and Ellipse** take an outline width of 1 to 16 pixels (and the three fill modes).
  - **Fill** is **Contiguous** (the connected area) or **Global** (every matching pixel of the layer), with a tolerance.
- **Layers.** The Layers panel lists the layers top first. Each row has an eye (show or hide), a lock, a thumbnail of the layer and the name (double-click to rename). Click a row to make it the active layer, drag a row to reorder, drag the opacity slider to fade the active layer (it applies when you let go, so a drag is one undo step), and use New, Duplicate, Delete and Merge down (Delete needs a second layer, Merge down a layer below). The Layer menu has the same plus Flatten Image and Move Layer Up / Down; **New layer** is Ctrl+Shift+N. A project can have up to 256 layers, and about 134 million pixels in all its layers and frames together.
  - Drawing on a hidden or a locked layer is refused with a short message ("Layer is hidden" / "Layer is locked"). Layers cannot be added, reordered or changed while a stroke is in progress.
  - Every layer change and every edit is an undo step; undoing an edit on another layer takes you to that layer first.
- **Edit and Image menus** work as in Paint: undo and redo, cut, copy, paste (a floating selection on the active layer), select all, deselect and delete; Resize, Canvas Size and Crop to Selection (limited to 1024 pixels).
- **Files.** Save and Save As write a **`.tpix`** project holding every layer and frame. Open and drag-and-drop take `.tpix` files (a project switches to this mode) and still open ordinary images in Paint. A `.tpix` bigger than 1024 pixels on a side, or one that would need too much memory, is refused with a message; a damaged layer image opens as an empty one and you are told how many. **File > Export** writes the picture you see (all visible layers blended) as a PNG at 1x.
- Coming later: the Timeline with onion skin, Import Image and conversion to pixel art, palette tools, symmetry drawing, a 3 x 3 tile preview, and GIF and sprite sheet export.

### Tools

Pick a tool in the Tools panel, a two-column grid of icons without boxes (the selected one is drawn in the accent color; hover for the name and key), or with its key. Left button paints with the primary color, right button with the secondary (Fill and Picker follow the same rule).

| Tool | Key | Notes |
|---|---|---|
| Pencil | P | 1 px, hard edge |
| Brush | B | size 1-200 px, optional antialiasing, round tip |
| Eraser | E | paints the secondary color, or transparency with "Erase to transparent" |
| Line | L | width 1-50 px, optional antialiasing, Shift snaps to 45 degree steps |
| Rectangle | R | outline, filled, or outline + fill; Shift makes a square |
| Ellipse | O | same modes as Rectangle; Shift makes a circle |
| Fill | F | flood fill with a 0-255 tolerance (compared against the clicked pixel) |
| Picker | K | click or drag to copy a pixel, including its transparency, into the primary color |
| Select | S | drag to select a rectangle, drag inside it to move the pixels; Shift makes a square |

Shapes preview live while you drag and Escape cancels them. `[` and `]` change the brush or outline size, X swaps the colors. A ring under the cursor shows the footprint of the brush and eraser.

### Appearance

- **Theme page** (Settings > Theme): choose System, Light or Dark; pick an accent from eight presets (they recolor both themes); edit any of 16 colors (window, buttons, text, canvas backdrop, rulers, guides, selection and more) for the light and the dark theme separately, with a live preview and a Reset button per color. Everything that is blue by default (title bars, tabs, buttons on hover, sliders) follows the accent, and text on the accent switches between white and black by contrast. Only colors you changed are saved (in `[theme.dark]` and `[theme.light]` of `settings.ini`).
- **Import and export theme** write your changed colors to an ini file and read one back, so a theme can be shared.
- **Backgrounds** (Settings > View): the area around the image and the area behind transparent pixels each take a pattern: solid, checkerboard, grid, dots, diagonal lines or plus marks, with a spacing of 8 to 64 px. Patterns are anchored to the image and follow zoom and pan; the spacing doubles automatically when a huge window would need an excessive number of marks.
- **Fonts** (Settings > General): the classic font (ImGui's scalable default), Inter or JetBrains Mono (both built in, SIL Open Font License), or any `.ttf` / `.otf` file you load, at 10 to 24 px. A font file can add scripts the built-in ones lack, such as CJK. A broken or oversized file (over 32 MB) is refused with a message and Trace falls back to Inter.

### Languages

Settings > General > Language switches the interface language. The whole interface is translatable: menus, tools, dialogs, toasts, error messages, Settings and the shortcut list. English is built in and is the fallback for anything a pack does not translate. A Spanish pack ships in the `languages` folder next to the program.

- **Import language...** validates a pack file and copies it to `languages` in the per-user data folder (next to `settings.ini`); it is then selected. **Export template...** writes a file with every text and its English wording for translators.
- **Pack format:** an ini file with `[meta]` (`code`, `name`, `author`) and `[strings]` (`key = text`). `{0}`, `{1}` are replaced by numbers and names at run time and must appear in a translation exactly as in the English text (a line that differs is ignored, and the Settings page says how many texts a pack covers); `{{` and `}}` are literal braces; `\n` is a line break. Files must be UTF-8 and at most 1 MB.
- Right-to-left scripts are not supported. For scripts the built-in fonts lack, load a font file that has them.

### Notifications

Quiet confirmations appear as toasts in the bottom-right corner and fade out by themselves: "Saved photo.png", "Copied selection", "Cut selection", "Resized to 400 x 300 px", "Canvas is now ...", "Cropped to ...", and minor clipboard problems (such as no image on the clipboard) in the warning color. They never take focus or input, and the app goes back to sleep once the last one has faded. Failures that need a decision, such as a file that cannot be saved or running out of memory, stay as dialogs.

### Image: resize, canvas size and crop

The Image menu changes the size of the whole picture. Each of these is one undo step (an undo step for a size change stores the old image in full, so it uses as much memory as the picture), they commit any pending edit first, and they end the selection (pixels you were moving are dropped where they are first).

- **Resize** (Ctrl+Alt+I) scales the picture by percent or to an exact size in pixels, with "Keep aspect ratio" and a choice of *Sharp pixels* (nearest neighbour) or *Smooth* (bilinear; transparent pixels never tint their neighbours). Sizes from 1 to 16384 pixels per side.
- **Canvas Size** (Ctrl+Alt+C) makes the canvas bigger or smaller without scaling the picture. A 3 x 3 anchor decides where the picture sits; new area is filled with the secondary color or left transparent, and anything that no longer fits is cut off.
- **Crop to Selection** (Ctrl+Shift+X) keeps only the selected rectangle (the part of it that is on the image).
- If there is not enough memory for the new image, Trace says so and leaves the picture untouched.

### Rulers and guides

- **Rulers** run along the top and left of the canvas in image pixels and follow zoom and pan; a marker shows the pointer. Toggle with View > Rulers (Ctrl+R).
- **Guides:** press on a ruler and drag onto the canvas to create one (vertical from the left ruler, horizontal from the top one); a tooltip shows its position. Drag an existing guide to move it, and drop it back on a ruler or outside the image to delete it. Escape cancels the drag. View > Guides (Ctrl+;) hides them and View > Clear Guides removes them all. Guides sit on pixel boundaries (0 and the image size are the edges), up to 128 per direction.
- **Snap to guides** (Ctrl+Shift+;) pulls Line, Rectangle and Ellipse corners, the Select marquee and moved selections onto a guide within a few screen pixels, so a box's edge lands exactly on it. Freehand tools do not snap. Snapping is off while guides are hidden.
- Guides move with the picture when you resize, change the canvas size or crop (and are dropped if they end up outside); they are not part of undo, and opening a file or New clears them. The ruler, guide and snap switches are also on the Settings > View page and are remembered.

### Selection and clipboard

- **Select** (S): drag on the canvas to draw a rectangle (clipped to the image), click to deselect, Escape cancels the drag. The outline is a dashed line in the theme's selection color.
- **Move:** drag inside the selection. The pixels lift off the image, leaving the secondary color behind (or transparency when "Erase to transparent" is on), and can be dragged anywhere, even partly off the canvas, without losing the hidden part. Transparent pixels show what is underneath. Each drag is one undo step; drawing with any other tool, or pressing Deselect, drops the pixels where they are.
- **Select all** (Ctrl+A), **Deselect** (Ctrl+D), **Delete** (clears a selection like the eraser would; discards moved pixels).
- **Cut / Copy / Paste** (Ctrl+X / C / V) use the system clipboard as an image: copies are offered as PNG (keeps transparency) and BMP, and pastes accept either, so images move to and from other programs. A paste lands as a floating selection at the top-left of the visible canvas and switches to the Select tool so you can drag it into place; a paste larger than the canvas is clipped (the canvas does not grow). Ctrl+C/V/X/A are left to a focused text field (a hex color, say).
- Selections are dropped by undo, redo, New and Open. Drawing tools are not clipped to the selection.

### Files

- **Open** (Ctrl+O), **Save** (Ctrl+S), **Save as** (Ctrl+Shift+S) and **New** (Ctrl+N) in the File menu, using the system's native file dialogs. You can also drag an image file onto the window (with several files, the first one opens).
- **Formats:** PNG, JPEG, BMP, TGA and GIF for both opening and saving. Files are recognised by their content, so a misnamed file still opens. Images up to 16384 x 16384 pixels and 512 MB.
- **Transparency:** kept in PNG, TGA and BMP. JPEG has none, so transparent areas are flattened onto white. GIF keeps on/off transparency (pixels at least half transparent become transparent) and at most 256 colors.
- **JPEG quality** (1-100) is asked for when you save a JPEG and remembered; Save then reuses it.
- **Animated GIFs** open on their first frame. Because saving would write a still image over the animation, Trace tells you and asks for a new file name instead of replacing the original.
- **Open and drag-and-drop go by content:** a Trace project file (`.tflow`, `.tpix`) is handed to its mode, and any image opens in Paint, from whichever mode is shown. A `.tpix` opens in Pixel Art; a `.tflow` still reports that Flowchart is not ready, and a project file of a kind this version does not know gets a clear error.
- **Project files** (`.tpix` now, `.tflow` later) keep everything editable, are written the same safe way as images, and are checked on opening: counts, sizes and pictures are validated, anything unusable is skipped, and a hostile or truncated file is refused with a message instead of crashing.
- **Unsaved changes:** New, Open, dropping a file and closing the window ask "Save changes?" first. The window title shows the file name, with `*` while there are unsaved changes.
- **Safe saving:** the image is written to a temporary file and renamed over the target only when complete, so a failed save (full disk, locked file) never damages the existing file. Failures are explained in a dialog; nothing crashes on a broken, truncated or hostile file.
- Known limits: JPEG EXIF rotation is not applied, and only the first frame of an animated GIF is loaded.

### Undo and redo

Ctrl+Z undoes and Ctrl+Y (or Ctrl+Shift+Z) redoes; Edit shows what is available. Every tool gesture (a whole stroke, a shape, a fill) is one step, Escape-cancelled shapes leave no step, and a stroke cut short with Escape stays and can be undone. History stores only the 64 x 64 pixel tiles that changed, so a step costs about the size of the area you edited, and the oldest steps are dropped once the history passes a memory limit (512 MB by default, changeable under Settings > General). A new edit after undoing discards the steps that could have been redone. The newest step is always kept.

### Colors

Primary and secondary swatches (click one to open the RGBA editor with HSV, hex and an alpha bar), a Swap button, and a 28-color palette that includes a transparent swatch: click for primary, right click for secondary. Colors with alpha below 255 composite over the canvas.

### Canvas (from M1)

- **Zoom** from 10% to 3200% with Ctrl+wheel (zooms toward the cursor), the zoom shortcuts below, or the View menu. 100% is one device pixel.
- **Pan** with the middle mouse button, Space + left drag, or the wheel (Shift for horizontal).
- **Checkerboard** behind transparent pixels, a **pixel grid** and hover outline from 800% zoom (View > Pixel Grid toggles the grid).
- **Dirty-rect upload:** only changed regions of the image are sent to the GPU texture.
- **Status bar:** tool, cursor pixel, image size, zoom, and live CPU and RAM usage.

File > New (Ctrl+N) starts a blank canvas (800x600 unless changed in Settings). Light and dark themes follow the OS by default and can be forced from View > Theme; every UI and canvas color comes from the theme tokens. The render loop sleeps while idle.

## Keyboard shortcuts

These are the defaults. Every shortcut can be rebound (two per action) in File > Settings > Keybinds, and Cmd replaces Ctrl on macOS.

### File

| Action | Shortcut |
|---|---|
| New canvas | `Ctrl+N` |
| Open | `Ctrl+O` |
| Save | `Ctrl+S` |
| Save as | `Ctrl+Shift+S` |
| Export | `Ctrl+Shift+E` |
| Settings | `Ctrl+,` |

### Edit

| Action | Shortcut |
|---|---|
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Y` / `Ctrl+Shift+Z` |
| Cut | `Ctrl+X` |
| Copy | `Ctrl+C` |
| Paste | `Ctrl+V` |
| Select all | `Ctrl+A` |
| Deselect | `Ctrl+D` |
| Delete selection | `Delete` |

### Image

| Action | Shortcut |
|---|---|
| Resize image | `Ctrl+Alt+I` |
| Canvas size | `Ctrl+Alt+C` |
| Crop to selection | `Ctrl+Shift+X` |

### View

| Action | Shortcut |
|---|---|
| Zoom in | `Ctrl+=` / `Ctrl+Num+` |
| Zoom out | `Ctrl+-` / `Ctrl+Num-` |
| Fit to window | `Ctrl+0` |
| Actual size | `Ctrl+1` |
| Pixel grid | `Ctrl+'` |
| Rulers | `Ctrl+R` |
| Guides | `Ctrl+;` |
| Snap to guides | `Ctrl+Shift+;` |

### Tools

| Action | Shortcut |
|---|---|
| Pencil | `P` |
| Brush | `B` |
| Eraser | `E` |
| Line | `L` |
| Rectangle | `R` |
| Ellipse | `O` |
| Fill | `F` |
| Picker | `K` |
| Select | `S` |
| Swap colors | `X` |
| Decrease size | `[` |
| Increase size | `]` |

### Modes

| Action | Shortcut |
|---|---|
| Paint mode | `Ctrl+Shift+1` |
| Flowchart mode | `Ctrl+Shift+2` |
| Pixel Art mode | `Ctrl+Shift+3` |

### Pixel Art

These are live in Pixel Art mode only. The tool, size, color and view shortcuts above work there too. Resize, Canvas size and Crop have no shortcut in Pixel Art (use its Image menu).

| Action | Shortcut |
|---|---|
| New layer | `Ctrl+Shift+N` |

Fixed controls that cannot be rebound: mouse wheel scrolls (Shift for horizontal) and Ctrl+wheel zooms, middle-drag or Space + left drag pans, and Escape cancels the current shape or a pending key capture. Single-key shortcuts are ignored while a text field has focus.

## Prerequisites

All platforms need a C++20 compiler, CMake 3.25 or newer, Ninja and vcpkg.

### Windows

1. Install Visual Studio 2022 Build Tools with the "Desktop development with C++" workload.
2. Install CMake (3.25+) and Ninja (for example `winget install Kitware.CMake Ninja-build.Ninja`).
3. Install vcpkg:
   ```
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg
   ```
   Open a new terminal afterwards so `VCPKG_ROOT` is picked up.

### Linux

```
sudo apt-get install clang cmake ninja-build autoconf automake libtool pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libdbus-1-dev libudev-dev \
  libpulse-dev libasound2-dev
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
```

### macOS

```
xcode-select --install
brew install cmake ninja autoconf automake libtool pkg-config
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
```

## Build

Presets are named `<os>-<compiler>-<debug|release>`: `windows-msvc-*`, `linux-clang-*`, `macos-clang-*`. The first configure builds SDL3 and ImGui through vcpkg and takes a few minutes.

On Windows, run these from the **x64 Native Tools Command Prompt for VS 2022** so `cl` is on the path.

```
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Replace the preset name with `linux-clang-release` or `macos-clang-release` on those systems. The executable lands in `build/<preset>/` (`Trace.exe` on Windows).

Release builds use `/O2` with link-time optimization on MSVC, hidden symbols and dead-code stripping with clang. The vcpkg dependencies are pinned by `builtin-baseline` in `vcpkg.json`, which needs a full (not shallow) vcpkg clone.

Useful options: `-DTRACE_BUILD_TESTS=OFF`, `-DTRACE_WARNINGS_AS_ERRORS=ON`, `-DTRACE_SANITIZERS=ON` (ASan and UBSan on non-MSVC compilers).

## Project layout

```
Trace/
├─ CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  .editorconfig
├─ .github/workflows/ci.yml
├─ third_party/        vendored dependencies (msf_gif, the Inter and JetBrains Mono fonts)
├─ languages/          language packs that ship with the program (copied next to the executable)
├─ src/
│  ├─ core/            trace_core: pure C++ (Image, Document with a layer x frame cel grid and a composite, color blending, raster algorithms, undo history with tile, replace and layer/frame structure steps, selection, resize/canvas/crop transforms, guides, dirty rects, view transform with configurable zoom limits, INI parser, keymap with per-mode contexts, string table and language packs, color text, background pattern math). No SDL or ImGui.
│  ├─ io/              trace_io: image loading and saving (stb, msf_gif, animated GIF), the atomic file writer, the chunked project container used by `.tflow` and `.tpix`, the `.tpix` (Pixel Art project) reader and writer, save-target and UTF-8 path helpers. No SDL or ImGui.
│  ├─ tools/           trace_tools: Tool interface, the nine tools (with the pixel art options: pixel-perfect pencil, square stamp, global fill) and the Toolbox that routes gestures. No SDL or ImGui.
│  ├─ platform/        trace_platform: per-OS services (CPU and RAM sampling)
│  └─ app/             Trace executable: main loop and mode switcher, the Workspace interface with the Paint and Pixel Art workspaces (Layers panel, `.tpix` file adapter) and the Flowchart placeholder, theme tokens, canvas view and texture mirror, toasts, tool icons, tools panel, theme and fonts, language manager, native dialogs, settings model and window, ImGui key bindings, file session and file adapters (dialogs, unsaved-changes prompt), clipboard, Resize and Canvas Size dialogs, UI
└─ tests/              doctest unit tests for trace_core and trace_tools
```

## License

MIT. See [LICENSE](LICENSE).
