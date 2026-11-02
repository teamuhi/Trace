# Trace

A fast, cross-platform paint application in the spirit of MS Paint, with more features. Written in C++20; runs on Windows, Linux and macOS.

## Key features

- Paint tools with layers, a Text tool (any installed font), resize, canvas size, crop, rulers and guides
- Endless whiteboard that grows as you draw
- Three modes behind a switcher: **Paint**, **Pixel Art** (layers, animation, `.tpix`) and **Flowchart** (`.tflow`)
- Save As in PNG, JPEG, TIFF, SVG, ICO and more; Flowchart also writes PDF and Word
- Drag pictures and GIFs onto the canvas in any mode
- Light and dark themes with editable colors, language packs, rebindable shortcuts
- Portable builds for Windows and Linux

## Tech stack

<table>
  <thead>
    <tr>
      <th>Concern</th>
      <th>Choice</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Language</td>
      <td>C++20</td>
    </tr>
    <tr>
      <td>Window, input, dialogs</td>
      <td><a href="https://libsdl.org">SDL3</a></td>
    </tr>
    <tr>
      <td>UI</td>
      <td><a href="https://github.com/ocornut/imgui">Dear ImGui</a> (docking) on the SDL3 renderer</td>
    </tr>
    <tr>
      <td>Images and fonts</td>
      <td>stb, msf_gif, nanosvg (vendored in <code>third_party/</code>); Inter and JetBrains Mono (SIL OFL)</td>
    </tr>
    <tr>
      <td>Build</td>
      <td>CMake 3.25+, Ninja, vcpkg</td>
    </tr>
    <tr>
      <td>Tests</td>
      <td>doctest + CTest</td>
    </tr>
    <tr>
      <td>CI</td>
      <td>GitHub Actions on Windows, Linux and macOS; portable packages published on <code>v*</code> tags</td>
    </tr>
  </tbody>
</table>

## Modes

The **Paint | Flowchart | Pixel Art** switcher is at the right of the menu bar (Ctrl+Shift+1 / 2 / 3). Each mode keeps its own document, windows and layout, and the last mode is remembered. Switching is refused while a gesture or dialog is open. **View > Reset Layout** restores the shown mode's windows; **Settings > View > Reset layout** restores all. **File > Export...** (Ctrl+Shift+E) writes a finished picture from Pixel Art or Flowchart; Paint saves images directly.

### Paint

- **Tools:** Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Fill, Picker, Select and Text. Left button paints the primary color, right button the secondary. Right-click Brush or Eraser for tip style, size and hardness. `[` `]` change size, X swaps colors, Escape cancels.
- **Text:** click, type, Ctrl+Enter applies (one undo step), Escape drops it. Size 6-200 px, any installed font and style. Applied text is not editable.
- **Layers:** show/hide, lock, opacity, reorder, duplicate, merge, flatten. Saving to PNG, JPEG, BMP, TGA or GIF flattens them; `.tpix` keeps them.
- **Selection:** drag a rectangle, drag inside it to move pixels. Cut, Copy and Paste use the system clipboard (PNG and BMP).
- **Image menu:** Resize (Ctrl+Alt+I), Canvas Size (Ctrl+Alt+C), Crop to Selection (Ctrl+Shift+X); each is one undo step.
- **Rulers and guides:** drag from a ruler to make a guide, drop it back to delete; Snap to guides pulls shape corners and selections onto them.
- **Canvas:** zoom 10%-3200%, pan with middle-drag, Space + drag or the wheel; pixel grid from 800%; the status bar shows tool, pixel, size, zoom, CPU and RAM.
- **Undo:** Ctrl+Z / Ctrl+Y. History stores only changed tiles and drops the oldest steps past a memory limit (512 MB, in Settings).
- **Whiteboard:** File > New Whiteboard starts an endless board on white or black paper that grows as you draw (up to 16384 px per side). Saved as `.tboard`; export with File > Export (area, border, size, background; PNG or JPEG).

### Flowchart

- **Shapes:** rectangle, rounded rectangle, ellipse, diamond, parallelogram, terminator, document, database, hexagon, circle, sticky note, text, image, container, and a polygon (3-32 sides).
- **Connectors:** drag from a shape's edge dot. Curved, elbow or straight, with arrowheads and labels. Drag the diamond handle to bend one by hand.
- **Editing:** select, move (snaps to the grid and other shapes), resize, copy / paste / duplicate, undo / redo, group, align, distribute, order. Copy also puts a PNG on the system clipboard.
- **Style:** fill, line, text color (or **Auto**, which follows the theme), line width, dash, font size and alignment in Properties.
- **Auto layout** (Ctrl+Shift+L) arranges shapes along the connectors. **File > New from Template:** Process, Decision Tree, Swimlane.
- **Files:** `.tflow` keeps everything editable. Export to PNG (x1, x2, x4) or SVG with a chosen background and padding.

### Pixel Art

- **Canvas:** transparent 64 x 64 by default, up to 1024 x 1024; whole-pixel tools only (Pencil with pixel-perfect, Brush, Eraser, Line, Rectangle, Ellipse, Fill, Picker, Select).
- **Layers and animation:** up to 256 layers; a Timeline with up to 999 frames, per-frame duration, Play / Loop (F5).
- **Palette:** click for primary, right-click for secondary; Add, Load / Save (`.gpl`, `.hex`), From canvas, Sort, Replace color.
- **Symmetry:** up to 8 mirror lines (Ctrl+Alt+H / V) applied to strokes, shapes and fills.
- **Tile preview** (Ctrl+Alt+T) shows the picture 3 x 3; **onion skin** (Ctrl+Alt+O) shows neighbouring frames.
- **Files:** `.tpix` holds all layers, frames and the palette. Export a PNG, animated GIF or sprite sheet at x1-x32.

## Files

- **Open / Save / Save as / New** use native dialogs. PNG, JPEG, BMP, TGA and GIF open and save; files are recognised by content.
- **Save as** also writes TIFF, ICO, QOI, PPM and SVG (Flowchart adds PDF and Word), which cannot be opened again. In Pixel Art and Flowchart these are copies; only the project type renames the document.
- **JPEG** has no transparency (flattened onto white); its quality is asked on save.
- **Dropped pictures** land on the canvas in any mode as a floating selection (Paint, Pixel Art) or an image shape (Flowchart). Hold **Shift** to open one as a document. Project files always open as documents; `.tpix` opens in Pixel Art, `.tflow` in Flowchart, `.tboard` in Paint.
- **Animated GIFs** open on their first frame; dropped on Pixel Art, one becomes a new layer with one frame per GIF frame.
- **Safe saving:** written to a temporary file, then renamed, so a failed save never damages the original. Damaged files are repaired or refused with a message; unsaved changes are asked about before New, Open and close.

## Settings

File > Settings (Ctrl+,), saved to `settings.ini` in the user data folder (`%APPDATA%\Trace\Trace` on Windows).

- **General:** interface scale, font and size, language, new canvas size, undo memory.
- **Theme:** System / Light / Dark, eight accents, editable colors per theme, import and export.
- **View:** panels, rulers, guides, background patterns, new canvas zoom.
- **Tools**, **Pixel Art** and **Flowchart:** start values; **Keybinds:** rebind any shortcut (two per action).
- Every number is a plain text box with its range shown beside it; press Enter, Tab or click away to apply.

**Languages:** pick one under General. Import a pack (an ini file with `[meta]` and `[strings]`) or export a template to translate. English is built in and is the fallback; a Spanish pack ships with the program.

## Keyboard shortcuts

Defaults; Cmd replaces Ctrl on macOS. Every action can be rebound in Settings > Keybinds.

<table>
  <thead>
    <tr>
      <th>Group</th>
      <th>Shortcuts</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>File</td>
      <td>New <code>Ctrl+N</code>, Open <code>Ctrl+O</code>, Save <code>Ctrl+S</code>, Save as <code>Ctrl+Shift+S</code>, Export <code>Ctrl+Shift+E</code>, Settings <code>Ctrl+,</code></td>
    </tr>
    <tr>
      <td>Edit</td>
      <td>Undo <code>Ctrl+Z</code>, Redo <code>Ctrl+Y</code> / <code>Ctrl+Shift+Z</code>, Cut <code>Ctrl+X</code>, Copy <code>Ctrl+C</code>, Paste <code>Ctrl+V</code>, Select all <code>Ctrl+A</code>, Deselect <code>Ctrl+D</code>, Delete <code>Delete</code></td>
    </tr>
    <tr>
      <td>Image</td>
      <td>Resize <code>Ctrl+Alt+I</code>, Canvas size <code>Ctrl+Alt+C</code>, Crop <code>Ctrl+Shift+X</code></td>
    </tr>
    <tr>
      <td>View</td>
      <td>Zoom in <code>Ctrl+=</code>, Zoom out <code>Ctrl+-</code>, Fit <code>Ctrl+0</code>, Actual size <code>Ctrl+1</code>, Pixel grid <code>Ctrl+'</code>, Rulers <code>Ctrl+R</code>, Guides <code>Ctrl+;</code>, Snap to guides <code>Ctrl+Shift+;</code></td>
    </tr>
    <tr>
      <td>Tools</td>
      <td>Pencil <code>P</code>, Brush <code>B</code>, Eraser <code>E</code>, Line <code>L</code>, Rectangle <code>R</code>, Ellipse <code>O</code>, Fill <code>F</code>, Picker <code>K</code>, Select <code>S</code>, Text <code>T</code>, Swap colors <code>X</code>, Size <code>[</code> / <code>]</code></td>
    </tr>
    <tr>
      <td>Modes</td>
      <td>Paint <code>Ctrl+Shift+1</code>, Flowchart <code>Ctrl+Shift+2</code>, Pixel Art <code>Ctrl+Shift+3</code></td>
    </tr>
    <tr>
      <td>Pixel Art</td>
      <td>New layer <code>Ctrl+Shift+N</code>, Mirror X / Y <code>Ctrl+Alt+H</code> / <code>V</code>, New frame <code>Alt+N</code>, Next / Previous frame <code>.</code> / <code>,</code>, Play <code>F5</code>, Onion skin <code>Ctrl+Alt+O</code>, Tile preview <code>Ctrl+Alt+T</code></td>
    </tr>
    <tr>
      <td>Flowchart</td>
      <td>Select <code>V</code>, Hand <code>H</code>, Connector <code>C</code>, Text <code>T</code>, Sticky note <code>N</code>, Image <code>I</code>, Rectangle <code>R</code>, Ellipse <code>O</code>, Diamond <code>D</code>, Container <code>W</code>, Duplicate <code>Ctrl+D</code>, Order <code>Ctrl+]</code> / <code>[</code> (add Shift for front / back), Group <code>Ctrl+G</code>, Ungroup <code>Ctrl+Shift+G</code>, Auto layout <code>Ctrl+Shift+L</code></td>
    </tr>
  </tbody>
</table>

Fixed: wheel scrolls, Ctrl+wheel zooms, middle-drag or Space + drag pans, Escape cancels.

## Downloads and portable mode

Each GitHub release has a zip per platform. The Windows and Linux zips are **portable**: unzip anywhere and run `Trace.exe` (or `Trace`). While `portable.txt` sits next to the program, settings, layout and imported languages are kept in a `data` folder beside it; delete the file to use the user profile instead. If the folder is read-only, Trace falls back to the user profile. The macOS zip is the `Trace.app` bundle and is not portable.

To build the folder yourself: `cmake -DBUILD_DIR=build/windows-msvc-release -DOUT_DIR=Trace-portable -P cmake/PackagePortable.cmake`. Pushing a tag such as `v0.1.0` builds all three platforms and publishes the release.

## Build

All platforms need a C++20 compiler, CMake 3.25+, Ninja and vcpkg (set `VCPKG_ROOT`).

- **Windows:** Visual Studio 2022 Build Tools ("Desktop development with C++"), then `winget install Kitware.CMake Ninja-build.Ninja`. Build from the **x64 Native Tools Command Prompt**.
- **Linux:** `sudo apt-get install clang cmake ninja-build autoconf autoconf-archive automake libtool libltdl-dev pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libdbus-1-dev libudev-dev libpulse-dev libasound2-dev`
- **macOS:** `xcode-select --install`, then `brew install cmake ninja autoconf autoconf-archive automake libtool pkg-config`.
- **vcpkg** (needs a full clone): `git clone https://github.com/microsoft/vcpkg` and run its `bootstrap` script.

```
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Presets are `windows-msvc-*`, `linux-clang-*` and `macos-clang-*` (`debug` or `release`). The first configure builds SDL3 and ImGui and takes a few minutes. Options: `-DTRACE_BUILD_TESTS=OFF`, `-DTRACE_WARNINGS_AS_ERRORS=ON`, `-DTRACE_SANITIZERS=ON`.

## Project layout

```
Trace/
├─ .github/workflows/ci.yml   build, test, package; releases on v* tags
├─ cmake/        PackagePortable.cmake
├─ third_party/  vendored dependencies and fonts
├─ languages/    language packs
├─ docs/         plans for the Pixel Art and Flowchart modes
├─ src/
│  ├─ core/      documents, layers, raster, undo, selection, keymap, strings (no SDL or ImGui)
│  ├─ flow/      flowchart model, routing, layout, `.tflow`, SVG
│  ├─ io/        image and project I/O, atomic writer (no SDL or ImGui)
│  ├─ tools/     the tools and the Toolbox (no SDL or ImGui)
│  ├─ platform/  per-OS services
│  ├─ assets/    SVG icons
│  └─ app/       the executable: main loop, workspaces, panels, settings, dialogs
└─ tests/        doctest unit tests
```

## License

MIT. See [LICENSE](LICENSE).
