# Trace

A fast, stable, cross-platform paint application in the spirit of MS Paint, with more features. It is written in C++20 and runs on Windows, Linux and macOS.

## Key features

• Standard paint features, including a Text tool (type in Inter, JetBrains Mono or any font installed on the system, such as Arial or Calibri, with its styles, at any size, previewed live)

• Resize, canvas size and crop

• Redesigned look: pill mode switcher, card-style sidebars, icon-only tool buttons with tooltips, the app icon as window and executable icon
• Flowchart connectors can be bent by hand (drag the handle, double-click it to reset) and text has horizontal and vertical alignment
• Endless whiteboard in Paint (File > New Whiteboard): a canvas that grows as you draw, white or black paper, saved as `.tboard` and exported with a border, area and size you choose
• Drag pictures (and GIFs) from your desktop onto the canvas in any mode: they land as movable objects instead of replacing the document; an animated GIF becomes a layer with one frame per GIF frame in Pixel Art
• Layers in Paint: a Layers panel with show/hide, lock, opacity, reorder, duplicate, merge down and flatten

• Rulers and guides with snapping

• Three modes behind a switcher: Paint, Pixel Art (layers, pixel-perfect drawing, `.tpix` projects) and Flowchart (shapes, connectors, text, images, arrange, auto layout, templates, `.tflow` files and PNG / SVG / PDF / Word export), and Save As offers PNG, JPEG, TIFF, SVG, ICO and more image types in every mode

• To be added soon

## Tech stack

| Concern | Choice |
|---|---|
| Language | C++20 |
| Window, input, clipboard, dialogs | [SDL3](https://libsdl.org) |
| UI | [Dear ImGui](https://github.com/ocornut/imgui) (docking branch features) on the SDL3 renderer backend |
| Image I/O | stb_image / stb_image_write, msf_gif (vendored in `third_party/`) |
| Text drawing | stb_truetype (draws the Text tool's glyphs) |
| Fonts | Inter and JetBrains Mono (SIL OFL, vendored in `third_party/fonts/`, compiled into the program) |
| Icons | SVG files in `src/assets/icons/`, compiled into the program and rasterized with nanosvg (vendored in `third_party/nanosvg/`) |
| Build | CMake 3.25+, Ninja, vcpkg (manifest mode) |
| Tests | doctest + CTest |
| CI | GitHub Actions (Windows, Linux, macOS), vcpkg pinned to a baseline commit with binary caching, warnings as errors, release builds uploaded as artifacts |

## Status

**Endless whiteboard and dropped pictures.** Paint has a new kind of document, the **whiteboard**, and every mode accepts pictures dragged in from outside. See [Whiteboard](#whiteboard-paint) and **Dropped pictures** under **Files**. Dragging a picture onto the window no longer opens it as a new document: it is placed on the canvas (hold **Shift** while dropping to open it the old way; project files still open).

**Layers in Paint.** Paint now has a **Layers panel** (right side; View > Layers Panel shows or hides it, also under Settings > View) and a **Layer menu**, the same ones Pixel Art uses: an eye and a lock per layer, thumbnails, rename (double-click), drag to reorder, an opacity slider for the active layer, and New, Duplicate, Delete and Merge down (Flatten Image and Move Layer Up / Down are in the menu). A picture starts as one layer and looks and saves exactly as before; with several layers, saving to PNG, JPEG, BMP, TGA or GIF writes them **flattened** (layers are not kept in those files, so save a Pixel Art `.tpix` project if you need them back). Drawing, Cut, Paste and Delete on a hidden or locked layer are refused with a short message. Above the bottom layer the Eraser and Delete (and a lifted selection) always leave **transparency**, whatever the "Erase to transparent" option says, so they never cover the layers underneath. Layer commands wait while a stroke or a text box is open. New Layer's shortcut (Ctrl+Shift+N) works in Pixel Art only; in Paint use the panel's New button or the Layer menu. An older saved window layout is rebuilt once to make room for the panel.

**Pixel Art mode (layers and the workspace).** Pixel Art is now a real mode: a layered canvas of up to 1024 x 1024 pixels with its own tools, a Layers panel, and `.tpix` project files. Animation frames are stored in the file and in the document, the Palette window and symmetry drawing are in; tile preview, onion skin, the Timeline (frames and playback) and GIF / sprite sheet export are in; image conversion comes in a later version. See [Pixel Art](#pixel-art). Paint behaves exactly as before.

**Modes (foundations).** Trace is growing two more modes next to Paint, **Flowchart** and **Pixel Art**: the mode switcher, per-mode workspaces, a shared project file container and an Export command. See [Modes](#modes).

**M6: Polish, then an interface pass.** Toast notifications, tuned release builds and a pinned, cached CI build for Windows, Linux and macOS, followed by icon toolbar buttons, a Theme page with editable colors, background patterns, interface fonts and importable language packs.

**Settings.** File > Settings (Ctrl+,) opens a window (large category icons, a titled header and ruled section headings on every page; labels on the left, controls on the right, and every number is a plain text box with its allowed range shown beside it: type a value and press Enter, Tab or click away, and it is clamped to the range) with nine categories: General (interface language and font, interface scale, size of a new canvas, undo memory), Theme (appearance, accent color, every interface color), View (pixel grid, rulers, guides and snapping, background patterns, Tools and Layers panels, status bar, CPU and RAM readout, how a new canvas is zoomed), Tools (restore the last session's tool settings or start from chosen values, start tool), Pixel Art (default canvas size, the zoom from which the pixel grid shows, pixel-perfect drawing on at start), Flowchart (grid size, show grid, snap to grid and smart guides, and the route and arrowheads a new connector starts with), Scripts (placeholder for a future release), Keybinds (rebind every shortcut, with conflict warnings) and About. Settings are saved to `settings.ini` in the per-user data directory (next to `imgui.ini`; on Windows `%APPDATA%\Trace\Trace`) and a missing or damaged file falls back to the defaults.

### Modes

The **Paint | Flowchart | Pixel Art** switcher sits at the right end of the menu bar (the shown mode is filled with the accent color, in both themes). You can also switch with **Ctrl+Shift+1 / 2 / 3** (rebindable under the Modes group). One mode is shown at a time and each keeps its own document, windows and dock layout, so switching back finds everything as you left it. The last mode is remembered between runs.

- **Paint** is the full image editor described on this page.
- **Pixel Art** is described under [Pixel Art](#pixel-art).
- **Flowchart** is described under [Flowchart](#flowchart).
- Switching is refused (the switcher is dimmed) while a tool gesture, a dialog or a prompt is open.
- **Window layout.** Panels dock around the canvas (drop them on the arrows at its sides or on the window edges); the canvas itself never takes a panel into its tab, so a panel dropped on its middle is refused instead of vanishing behind it. **View > Reset Layout** puts the shown mode's windows back in their default places and shows the panels you closed with their X; **Settings > View > Reset layout** does the same for every mode. A saved layout that hid a panel behind the canvas is rebuilt once at start.
- **Quitting** with unsaved changes in more than one mode asks about each in turn, showing the mode it is asking about; Cancel on any prompt keeps Trace open.
- Shortcuts are scoped to the mode that is shown, so a key such as `R` can mean different things in different modes without ever firing twice. The Keybinds page warns about a conflict only between shortcuts that can be active at the same time.
- **File > Export...** (Ctrl+Shift+E) is for writing a finished picture out of a project mode (PNG, animated GIF and sprite sheet in Pixel Art, PNG and SVG in Flowchart). It is separate from Save, so a project is never silently flattened. Paint saves images directly, so it has nothing to export and the item is disabled there.

### Flowchart

Switch to **Flowchart** (Ctrl+Shift+2) for a free board of flowcharts. It keeps its own document, windows (Shapes on the left, the board, Properties on the right) and dock layout. Charts are saved as `.tflow` files (File > Save, Save As, Open, or drag a file onto the window) and exported with File > Export, or with Save As in any of PNG, JPEG, BMP, TGA, GIF, TIFF, ICO, QOI, PPM, SVG, PDF and Word (see **Files** under Paint).

- **Shapes.** Rectangle, rounded rectangle, ellipse, diamond (decision), parallelogram (input / output), terminator, document, database, hexagon, circle, sticky note, text, image, container (a swimlane with a title band) and **polygon**, a regular shape whose number of sides (3 to 32) you set with the Sides slider in the Shapes window before placing it, or in Properties afterwards. Pick one in the Shapes window or with its shortcut, then click to place a default-sized shape or drag to size it (Shift keeps the proportions). Double-click a shape or press Enter to type inside it; Ctrl+Enter or a click elsewhere commits and Escape reverts.
- **Connectors.** Hover a shape to see a dot on each side, then drag from a dot to another shape (or use the Connector tool and drag from anywhere on a shape). While you drag, the dots of the shape under the pointer move onto its outline, where the connector ends, and the one it will lock to is highlighted: drop near a dot to take it, or anywhere on the shape to lock to its nearest dot, so a connector always ends in a dot; **dropping on an empty spot opens a picker that creates a connected shape there**. Connectors are curved, elbow (right angles) or straight, with an arrowhead, an open arrow, a dot or nothing at each end, an optional label, and follow their shapes when moved. Select a connector to drag its ends onto other shapes or free points. **Labels:** double-click a connector (or select it and press Enter) to type a label. Drag the label to move it along the line; drag it clear of the line to put it above or below instead of on it. In Properties, three buttons place the label on, above or below the line (above and below are relative to the text, and sit clear of the line), **Turn with the line** makes the text follow the direction of the line (it never reads upside down), and a slider sets how far along the line it sits. The route is picked with three buttons (curved, elbow, straight) in Properties, and the Arrange > Connectors menu sets the route of the selected connectors, or of every connector when none is selected, in one step. The route, arrowheads and line color of new connectors are set in Properties with nothing selected. **Manual routing:** select a connector and drag the diamond handle to bend it (a straight line bows, a curve reshapes, the middle segment of an elbow slides); the bend follows the shapes when they move, is saved in `.tflow` and shows in exports. Double-click the handle, right-click the connector > Reset route, or use the Properties button to return to automatic routing.
- **Selecting and editing.** Click, Shift or Ctrl click to add, drag a rubber band, drag to move (snapped to the grid and to the edges and centers of other shapes, shown as guide lines; hold Alt to move freely, Shift to keep one axis), eight handles to resize, arrow keys to nudge by 1 (Shift: 10). Copy, Cut, Paste, Duplicate (Ctrl+D), Select all and Delete work; an image on the system clipboard pastes as an image shape. **Copy and Cut also put a picture of the selected shapes on the system clipboard** (a transparent PNG at x2, with the light theme's automatic colors), so you can paste them into **Paint** or **Pixel Art** (or another program) as pixels; the shapes become a raster and are no longer editable there. Pasting back into Flowchart inserts the editable shapes, not the picture, as long as nothing else was copied in between. Only shapes can be copied this way (a connector alone cannot), and a very large selection is refused like a too-large export. Undo and Redo cover every change, and one drag is one step.
- **Style.** Properties edits fill, line, text color (each can be **Auto**, which follows the theme and so reads well in light and dark; a **Color code** row of eight preset colors colors shapes and connectors with one click, and Arrange > Connectors > Color by Source Shape paints each connector in the color of the shape it leaves), line width and dash, corner radius, font size, alignment, and for connectors the route, arrowheads and label, for one or many shapes at once.
- **Images.** The Image tool (or pasting) brings a picture in as a shape, stored at full size and shown at up to 400 units.
- **Arrange.** The Arrange menu and the Properties window bring forward, send backward, to front, to back, group and ungroup, align (left, center, right, top, middle, bottom), distribute horizontally and vertically (3 or more shapes) and match width or height (the Properties window labels each row and says how many shapes it needs). A group is selected as one (double-click to pick a member); groups are one level deep. A container moves its contents with it; dropping a shape inside a container makes it part of it, and deleting a container with contents asks whether to delete the contents too.
- **View.** Wheel pans (Shift for sideways), Ctrl+wheel zooms (5% to 800%), middle-drag or Space + drag pans, Fit shows everything. View > Show Grid, Snap to Grid and Smart Guides toggle those aids (not saved yet). The board, grid, handles and automatic colors are theme colors under Settings > Theme > Flowchart.

- **Auto layout.** Arrange > Auto layout (Ctrl+Shift+L) arranges the shapes in layers along the connectors: the two or more selected shapes among themselves, or the whole board when fewer are selected. Arrange > Layout Direction picks top to bottom or left to right. Loops in the chart are handled (the connector that closes a loop points back), unconnected parts are placed side by side, a container or a group moves as one block, and the connectors among the arranged shapes re-route themselves. It is one undo step; running it again changes nothing.
- **Templates.** File > New from Template starts a new board from **Process** (start, three steps, end), **Decision Tree** (a decision with Yes and No branches that rejoin) or **Swimlane** (three lanes with a step in each, connected across them). It asks about unsaved changes first, like New.

- **Files.** A `.tflow` project keeps everything editable: shapes with their text, styles (including Auto colors), polygon sides, containers and groups, pictures at full size, connectors with their routes, arrowheads and label placement. It is written the same safe way as images and checked on opening: anything unusable is skipped (a notice says how many items were repaired) and a hostile or truncated file is refused with a message instead of crashing. Unused pictures are not stored.
- **Export.** File > Export... (Ctrl+Shift+E) opens a dialog: **PNG** at x1, x2 or x4, or **SVG**; the background (white, transparent or the board color); all of the chart or only the selected shapes; and the padding around it. Auto colors follow the background you pick (a white or transparent export uses the light theme's ink and paper, so a chart made on the dark board still prints well). PNG is drawn off-screen from the chart's own bounds (not the view) and refused above 16384 pixels per side; SVG is standalone text with real text, paths and embedded pictures, but its line breaks are estimated, so they can differ a little from the board.
- **Settings.** Settings > Flowchart sets the grid size, whether the grid shows, snapping and smart guides, and the route and arrowheads a new connector starts with. The View menu toggles and the Properties window (with nothing selected) change the same values, and they are saved.

### Pixel Art

Switch to **Pixel Art** (Ctrl+Shift+3) for a layered canvas meant for sprites and tiles. It keeps its own document, tools (with their own colors and sizes), windows and dock layout, so nothing here changes Paint.

- **Canvas.** File > New makes a transparent 64 x 64 canvas (change it under Settings > Pixel Art); every side is 1 to 1024 pixels, in New, Resize and Canvas Size. A new or opened document is fitted to the window, enlarged in whole steps so every pixel is the same size, and the zoom commands step through whole levels from 100% up. The pixel grid shows from 400% (a setting) when View > Pixel Grid is on; rulers, guides, snapping and the backdrop and transparency patterns work as in Paint.
- **Tools** (Pixel Tools panel, same keys as Paint, without Text): Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Fill, Picker and Select. Everything is whole pixels: there is no antialiasing and no gradient.
  - **Pencil** has a **Pixel-perfect** option that removes the extra corner pixel of every L-shaped step, so a freehand line stays one pixel thick (Settings > Pixel Art sets whether Trace starts with it on; it is on by default).
  - **Brush** and **Eraser** stamp a round or a square tip of 1 to 16 pixels (a square stamp is exact; an odd size is centered on the pixel). The eraser always clears to transparency.
  - **Line, Rectangle and Ellipse** take an outline width of 1 to 16 pixels (and the three fill modes).
  - **Fill** is **Contiguous** (the connected area) or **Global** (every matching pixel of the layer), with a tolerance.
- **Layers.** The Layers panel lists the layers top first. Each row has an eye (show or hide), a lock, a thumbnail of the layer and the name (double-click to rename). Click a row to make it the active layer, drag a row to reorder, drag the opacity slider to fade the active layer (it applies when you let go, so a drag is one undo step), and use New, Duplicate, Delete and Merge down (Delete needs a second layer, Merge down a layer below). The Layer menu has the same plus Flatten Image and Move Layer Up / Down; **New layer** is Ctrl+Shift+N. A project can have up to 256 layers, and about 134 million pixels in all its layers and frames together. The panel groups an **Active layer** card (opacity slider, Visible and Locked toggles) above one icon-only row for New, Duplicate, Delete and Merge down (names as tooltips).
  - Drawing on a hidden or a locked layer is refused with a short message ("Layer is hidden" / "Layer is locked"). Layers cannot be added, reordered or changed while a stroke is in progress.
  - Every layer change and every edit is an undo step; undoing an edit on another layer takes you to that layer first.
- **Timeline and animation.** The Timeline window (bottom; View > Timeline Panel shows or hides it) has one cell per frame, left to right, scrolling sideways (up to 999 frames). Each cell shows the frame number, a thumbnail of the whole picture in that frame and its **duration in milliseconds**: drag the number, or double-click it and type (1 to 60000 ms; it applies when you let go, so one drag is one undo step). Click a cell to show that frame, drag a cell to move the frame (a line shows where it lands). Above the cells: **Play / Pause**, **Loop** (on: start again after the last frame; off: stop on the last frame), **New**, **Duplicate** and **Delete** frame, "Frame 3 / 12", and the speed of the shown frame in fps (1000 divided by its duration). A new frame is empty and goes after the active one; Duplicate copies every layer of the active frame; the last frame cannot be deleted. The **Frame menu** has New, Duplicate and Delete Frame, Next and Previous Frame, Play / Pause and Onion Skin. Shortcuts: **New frame** Alt+N, **Next frame** `.`, **Previous frame** `,` (they wrap around while Loop is on), **Play animation** F5. Playback uses each frame's own duration, never adds an undo step and never marks the project as changed; it pauses by itself when you edit, undo or redo, use a layer or frame command, start a tool gesture, open or create a file, or leave Pixel Art mode, and it stays on the frame it reached. While it plays, drawing is refused with "Pause the animation to draw". Frame changes wait while a stroke is in progress, and a new frame is refused (with a message) at 999 frames or when it would use too much memory. Trace only redraws continuously while an animation plays; idle it uses no CPU as before.
- **Edit and Image menus** work as in Paint: undo and redo, cut, copy, paste (a floating selection on the active layer), select all, deselect and delete; Resize, Canvas Size and Crop to Selection (limited to 1024 pixels). **Copy** with a selection takes that part of the active layer; with **no selection it takes the whole picture** (every visible layer blended, the shown frame), so it can be pasted straight into Paint.
- **Files.** Save and Save As write a **`.tpix`** project holding every layer and frame. Open and drag-and-drop take `.tpix` files (a project switches to this mode) and still open ordinary images in Paint. A `.tpix` bigger than 1024 pixels on a side, or one that would need too much memory, is refused with a message; a damaged layer image opens as an empty one and you are told how many. **File > Export...** (see **Export** below) writes a PNG, an animated GIF or a sprite sheet. **Save As** also lists the picture types (PNG, JPEG, BMP, TGA, GIF, TIFF, ICO, QOI, PPM, SVG) as copies of the shown frame; see **Files** under Paint.
- **Palette.** The Palette window (right side, below Layers; View > Palette Panel shows or hides it) replaces the fixed color strip. Click a swatch to set the primary color, right-click for the secondary color, drag to reorder. Buttons: **Add** the current color (duplicates, transparent colors and a 257th color are refused), **Remove**, **Load...** and **Save...** (GIMP `.gpl` or plain `.hex`, one `RRGGBB` per line; up to 256 colors and 1 MB, bad lines are skipped), **From canvas** (the colors used by the visible layers, most frequent first), **Sort** by hue or brightness, and **Replace color...**, which swaps one exact color for another in the active cel, the active layer (all frames) or the whole project as one undo step (locked layers are skipped). The palette is saved inside the `.tpix` project and comes back on Open; a new project starts with the default 27 colors. Palette edits themselves cannot be undone, but they count as unsaved changes.
- **Symmetry.** A list of up to 8 symmetry items, each drawn on the canvas and applied to every stroke, shape and fill. In the Pixel Tools panel, **+ Mirror X** and **+ Mirror Y** add an item (the buttons are disabled when the list is full or would make more than 64 copies); below them a rounded **Symmetry list** panel shows the items in order, each row with its name ("Mirror X", "Mirror Y", "Mirror X (2)"), a three-dot menu and a red trash button; **All** in the panel header removes every item. Double-click a name (or choose **Rename** in the menu) to give the item your own name of up to 32 characters: Enter or clicking away keeps it, Escape cancels, and an empty name brings back the automatic one; long names are cut with an ellipsis (the tooltip shows them whole), and a switched-off item is dimmed. Each row shows its fields as labeled pills, side by side when the panel is wide and one per line (label on the left, value and unit in an aligned column) when it is narrow: **posX** and **posY** (a point the mirror line passes through) and **Angle** (a mirror's flip direction in whole degrees: 0 is a vertical line, 90 a horizontal one, 45 and 135 the diagonals; only multiples of 45 are exact on the pixel grid). Drag a value left or right to change it (it moves live on the canvas), or double-click it (or Ctrl+click) to type a number. The menu has Enable / Disable, Duplicate, Center, Move up, Move down and a **Units: px / %** choice that switches posX and posY for the whole list between pixels (0.5 steps, so 31.5 is a pixel center) and percent of the canvas (50 is the middle). The copies are built by applying the enabled items in list order, each to every copy made so far: Mirror X + Mirror Y give four copies, two Mirror X lines at different positions give four. On the canvas each mirror is a dashed line with a round grip; the hovered item is drawn thicker. Drag a grip to move the item's point freely, or drag a dashed line to slide it across (half-pixel steps, Escape puts it back, double-click centers that item). Ctrl+Alt+H / Ctrl+Alt+V add a Mirror X / Y if there is none, otherwise switch all of them off, or on again. Pencil, Brush, Eraser, Line, Rectangle, Ellipse and Fill are mirrored (Select and Picker are not). A translucent stroke blends once where copies overlap, and Escape on a shape removes all its copies. The list is not saved between runs; a new or opened project puts every item back in the middle. You cannot start a stroke within a few pixels of an enabled line or grip (disable or move the item to draw there).
- **Tile preview.** View > Tile preview (Ctrl+Alt+T) shows the picture 3 x 3, so you can check that it repeats without seams. The eight neighbouring copies are drawn faded toward the *Tile preview fade* color (Settings > Theme) and you keep painting on the middle copy only; the fade strength is Settings > Pixel Art > *Fade of the neighbouring copies* (0 to 100%, default 50%). The zoom and fit behave as before and the copies may run past the window.
- **Onion skin.** With onion skin on (Ctrl+Alt+O), the frames next to the active one are drawn faintly behind it, earlier frames tinted with *Onion skin, earlier frames* and later frames with *Onion skin, later frames* (Settings > Theme); the farther a frame is the fainter it is. Settings > Pixel Art sets how many earlier and later frames show (0 to 4 each, default 1), the opacity of the nearest one (5 to 100%, default 40%) and whether the first and last frames count as neighbours of each other (default off). The ghosts are refreshed when you finish a stroke or change the animation, not while you draw, and nothing is drawn for a one-frame animation.
- **Export.** File > Export... (Ctrl+Shift+E) opens a dialog: **PNG** of the current frame, an **animated GIF** or a **sprite sheet** (PNG), all with an integer **scale** from x1 to x32. GIF and sprite sheet take a frame range (First and Last, default the whole animation); the GIF takes an alpha threshold (1 to 255, default 128, or untick *Keep transparency* to make every pixel opaque); the sprite sheet takes a layout (Row, Column, or Grid with a number of columns) and padding (0 to 64 px). The dialog shows the size of the result and the number of frames as you change the options, and *Export* stays disabled with the reason when the result would pass 16384 px on a side. Export asks for the file name next; the options you chose are remembered until Trace closes. The dialog opens as PNG for a one-frame document.
- Coming later: Import Image and conversion to pixel art.

### Whiteboard (Paint)

**File > New Whiteboard** starts an endless board. It begins as a 2048 x 2048 transparent area on white (or black) paper and **grows by itself** as you draw: pressing the mouse near an edge adds room on that side (at least 512 px at a time, never more than the memory budget of 128 million pixels in all layers, and 16384 px on a side). Each growth is one undo step, and what you drew keeps its place on the screen.

- **Paper.** White or black: Settings > View > *New whiteboards* sets the default for new boards, and **View > Whiteboard Paper** changes the current one. Changing the paper also swaps the pen (primary) color and the background (secondary) color when they matched the old paper, so you never draw white on white. The Eraser, Delete and a lifted selection always leave transparency on a whiteboard.
- **View.** No page border or checkerboard; pan with the wheel, middle-drag or Space + drag, zoom from 2% to 3200%. The rulers and the status bar count from the board's origin (where the first board was started), so coordinates stay the same however far it grows. **Fit** shows everything drawn (or the middle of an empty board).
- **Fill** needs a closed area on a whiteboard: a fill that would flood to the edge of the board is refused with a message. **Image > Resize / Canvas Size / Crop** are disabled (the board sizes itself).
- **Limit.** When the board reaches its limit you are told ("cannot grow beyond ..."); drawing inside it keeps working and anything beyond is clipped. Pasting or dropping a picture also grows the board to hold it.
- **Files.** Save and Save As write a **`.tboard`** project (all layers, the origin and the paper); Open and drag-and-drop take `.tboard` files. Save As also lists the picture types, which export the whole allocated board; use **File > Export...** for a proper picture.
- **Export (File > Export...).** *Area:* everything drawn, the current view, the whole board, the selection or a custom rectangle (corner and size in board coordinates). *Border:* the same distance on every side or each side on its own (0 to 16384 px), and it may reach past what is drawn or even past the board, where it is filled with the background. *Size:* a percentage (5% to 800%), a width, a height or a longest side in pixels, with smooth or hard-edged scaling. *Background:* the board paper, white, black or transparent. *File type:* PNG or JPEG (a transparent background in JPEG becomes white, and the dialog says so). The dialog shows the size of the result and the area it comes from as you change the options, and keeps *Export* disabled with the reason when the picture would be empty or too large (16384 px on a side, 128 million pixels). Visible layers are blended with their opacity; the options are remembered until Trace closes.

### Tools

Pick a tool in the Tools panel, a grid of large icon-only buttons (the selected one sits on an accent tile; hover for the name and key), or with its key. Docked at the top or bottom the panel is one compact row of tools with the Options, Colors and Palette cards beside it, and it keeps a sensible size for each dock position when you move it. **Right-click Brush or Eraser** (or press their Options button) for the brush style menu: Round, Square or Soft tips, size, hardness (Soft, Paint only) and Antialias. **Escape** drops the selection. Left button paints with the primary color, right button with the secondary (Fill and Picker follow the same rule).

| Tool | Key | Notes |
|---|---|---|
| Pencil | P | 1 px, hard edge |
| Brush | B | size 1-200 px, optional antialiasing; right-click for the style menu (Round, Square, Soft with hardness 1-100) |
| Eraser | E | paints the secondary color, or transparency with "Erase to transparent" (always transparency above the bottom layer) |
| Line | L | width 1-50 px, optional antialiasing, Shift snaps to 45 degree steps |
| Rectangle | R | outline, filled, or outline + fill; Shift makes a square |
| Ellipse | O | same modes as Rectangle; Shift makes a circle |
| Fill | F | flood fill with a 0-255 tolerance (compared against the clicked pixel) |
| Picker | K | click or drag to copy a pixel, including its transparency, into the primary color |
| Select | S | drag to select a rectangle, drag inside it to move the pixels; Shift makes a square |
| Text | T | click to place text, type in the box that opens (size 6-200 px, any installed font and style, optional antialiasing, left / center / right alignment of several-line text), Ctrl+Enter applies |

**Text.** Click where the text should start (its top-left corner) and type in the small box that opens below it; the text shows live on the canvas in the primary color (right click: secondary) with a dashed outline. Enter starts a new line, Tab inserts a tab, and the text can be up to 4096 bytes. **Ctrl+Enter**, clicking anywhere else (another spot on the canvas opens a new box there), choosing another tool, saving or switching mode applies it as one undo step; **Escape** drops it. Set the size (6-200 px, a number field you drag left or right or double-click to type; `[` and `]` change it), the font (a searchable list: the built-in Inter and JetBrains Mono first, then every installed font, found in the background, with a Style list for families that have Bold, Italic, Light and so on), Antialias and the colors before you click: clicking in the Tools panel while a box is open applies the text first. The start size and font are in Settings > Tools. Text is drawn into the active layer only, clipped to the image, and a locked or hidden layer refuses the click. Characters the font lacks (CJK, emoji) show as a replacement box, and text is not editable once applied. Pixel Art mode does not have the tool.

Shapes preview live while you drag and Escape cancels them. `[` and `]` change the brush or outline size, X swaps the colors. A ring under the cursor shows the footprint of the brush and eraser.

### Appearance

- **Theme page** (Settings > Theme): choose System, Light or Dark; pick an accent from eight presets (they recolor both themes); edit any of 16 colors (window, buttons, text, canvas backdrop, rulers, guides, selection and more) for the light and the dark theme separately, with a live preview and a Reset button per color. Everything that is blue by default (title bars, tabs, buttons on hover, sliders) follows the accent, and text on the accent switches between white and black by contrast. Only colors you changed are saved (in `[theme.dark]` and `[theme.light]` of `settings.ini`).
- **Import and export theme** write your changed colors to an ini file and read one back, so a theme can be shared.
- **Backgrounds** (Settings > View): the area around the image and the area behind transparent pixels each take a pattern: solid, checkerboard, grid, dots, diagonal lines or plus marks, with a spacing of 8 to 64 px. Patterns are anchored to the image and follow zoom and pan; the spacing doubles automatically when a huge window would need an excessive number of marks.
- **Fonts** (Settings > General): the classic font (ImGui's scalable default), Inter or JetBrains Mono (both built in, SIL Open Font License), or any `.ttf` / `.otf` file you load, at 10 to 24 px. A font file can add scripts the built-in ones lack, such as CJK. A broken or oversized file (over 32 MB) is refused with a message and Trace falls back to Inter.

- **Icons**: every tool, mode, panel button and menu entry that has an icon draws it from an SVG file in `src/assets/icons/`. The files are built into the program (nothing is read from disk at run time) and each one is rasterized once per size, at the exact pixel size so lines stay sharp at any interface scale, then tinted with the theme's text color, so one file serves the light and dark themes, the selected state on the accent and the disabled look. To add or change an icon, drop an SVG in that folder and reconfigure; its file name in CamelCase becomes the `Icon` value (`pencil_tool.svg` is `Icon::PencilTool`). Embedded bitmaps, text, gradients and filters in an SVG are not drawn, and `<style>` class rules are copied onto the elements that use them.

### Languages

Settings > General > Language switches the interface language. The whole interface is translatable: menus, tools, dialogs, toasts, error messages, Settings and the shortcut list. English is built in and is the fallback for anything a pack does not translate. A Spanish pack ships in the `languages` folder next to the program.

- **Import language...** validates a pack file and copies it to `languages` in the per-user data folder (next to `settings.ini`); it is then selected. **Export template...** writes a file with every text and its English wording for translators.
- **Pack format:** an ini file with `[meta]` (`code`, `name`, `author`) and `[strings]` (`key = text`). `{0}`, `{1}` are replaced by numbers and names at run time and must appear in a translation exactly as in the English text (a line that differs is ignored, and the Settings page says how many texts a pack covers); `{{` and `}}` are literal braces; `\n` is a line break. Files must be UTF-8 and at most 1 MB.
- Right-to-left scripts are not supported. For scripts the built-in fonts lack, load a font file that has them.

### Notifications

Quiet confirmations appear as toasts at the top center of the canvas and fade out by themselves: "Saved photo.png", "Copied selection", "Cut selection", "Resized to 400 x 300 px", "Canvas is now ...", "Cropped to ...", and minor clipboard problems (such as no image on the clipboard) in the warning color. They never take focus or input, and the app goes back to sleep once the last one has faded. Failures that need a decision, such as a file that cannot be saved or running out of memory, stay as dialogs.

### Image: resize, canvas size and crop

The Image menu changes the size of the whole picture. Each of these is one undo step (an undo step for a size change stores the old image in full, so it uses as much memory as the picture), they commit any pending edit first, and they end the selection (pixels you were moving are dropped where they are first).

- **Resize** (Ctrl+Alt+I) scales the picture by percent or to an exact size in pixels, with "Keep aspect ratio" and a choice of *Sharp pixels* (nearest neighbour) or *Smooth* (bilinear; transparent pixels never tint their neighbours). Sizes from 1 to 16384 pixels per side.
- **Canvas Size** (Ctrl+Alt+C) makes the canvas bigger or smaller without scaling the picture. A 3 x 3 anchor decides where the picture sits (a small preview shows the old picture on the new canvas); new area is filled with the secondary color or left transparent, and anything that no longer fits is cut off. Every dialog (Resize, Canvas Size, Export, Replace Color, the save prompts) shares one layout: Enter presses the highlighted button, Escape cancels, and the destructive choice is red.
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
- **Cut / Copy / Paste** (Ctrl+X / C / V) use the system clipboard as an image: copies are offered as PNG (keeps transparency) and BMP, and pastes accept either, so images move to and from other programs. A paste lands as a floating selection at the top-left of the visible canvas and switches to the Select tool so you can drag it into place; a paste larger than the canvas is clipped (the canvas does not grow, except on a whiteboard, which does). Ctrl+C/V/X/A are left to a focused text field (a hex color, say).
- Selections are dropped by undo, redo, New and Open. Drawing tools are not clipped to the selection.

### Files

- **Open** (Ctrl+O), **Save** (Ctrl+S), **Save as** (Ctrl+Shift+S) and **New** (Ctrl+N) in the File menu, using the system's native file dialogs. You can also drag image files onto the window: they are **placed on the canvas** (see **Dropped pictures** below).
- **Formats:** PNG, JPEG, BMP, TGA and GIF for both opening and saving. Files are recognised by their content, so a misnamed file still opens. Images up to 16384 x 16384 pixels and 512 MB.
- **Save as types:** every mode's Save As dialog ("Save as type") lists the picture formats **PNG, JPEG (`.jpg`, `.jpeg`, `.jpe`, `.jfif`), BMP, TGA, GIF, TIFF (`.tif`, `.tiff`), ICO, QOI, PPM and SVG**; the Flowchart also lists **PDF** and **Word (`.docx`)**. The extension you type wins; a name without one gets the extension of the selected type. TIFF is uncompressed RGBA with alpha; ICO is one 32-bit PNG entry (a picture over 256 pixels is shrunk to fit and centred on a square); QOI is lossless with alpha; PPM has no alpha (flattened onto white). Paint's SVG embeds the picture. TIFF, ICO, QOI, PPM, SVG, PDF and Word can be written but not opened again (open the original project, or PNG, instead).
- **Save as in Pixel Art and Flowchart** also lists the mode's own project type first (`.tpix`, `.tflow`); that is the only entry that changes the document's name and clears its unsaved changes. Every picture or page type is an **export of a copy**: the project keeps its name and its `*`, and closing or opening something afterwards still asks to save it. Pixel Art writes the shown frame at 1:1 (GIF writes the whole animation, SVG draws one rectangle per run of same-colored pixels, so it stays sharp at any size; use File > Export for other scales and sprite sheets). Flowchart writes the whole chart using the background and padding last chosen in File > Export (white and 24 by default) at that dialog's scale (x2 by default); SVG is the vector drawing, PDF and Word put the picture at its natural size on a page (Word: A4, landscape for a wide chart, shrunk to fit).
- **Transparency:** kept in PNG, TGA and BMP. JPEG has none, so transparent areas are flattened onto white. GIF keeps on/off transparency (pixels at least half transparent become transparent) and at most 256 colors.
- **JPEG quality** (1-100) is asked for when you save a JPEG and remembered; Save then reuses it.
- **Dropped pictures.** Drag one or more image files (PNG, JPEG, BMP, TGA, GIF) from your desktop or a file manager onto the canvas in **any mode**. A picture lands as a movable object centered on the drop point: in Paint and Pixel Art a floating selection on the active layer (the Select tool is chosen so you can drag it straight away; a hidden or locked layer refuses it), in Flowchart an image shape. In Paint a picture larger than the canvas is clipped and you are told (on a whiteboard the board grows instead); several files in one drop are offset a little from each other. Hold **Shift** while dropping to open a picture as a document instead (the first file only). Project files (`.tflow`, `.tpix`, `.tboard`) always open as documents. Drops are refused with a message while a dialog is open or a stroke is in progress.
- **Animated GIFs** open on their first frame, and a dropped GIF contributes its first frame in Paint and Flowchart (with a message). In **Pixel Art**, dropping an animated GIF creates a **new layer above the active one** holding one GIF frame per timeline frame, starting at the active frame (frames are added when the timeline is shorter, with the GIF's delays; existing frames keep their durations); the whole import is one undo step. Frames beyond the 999-frame limit, or that would not fit the memory budget (32 million pixels of GIF in all), are left out and you are told; a GIF larger than the canvas is clipped.
- **Animated GIFs** (opening) Because saving would write a still image over the animation, Trace tells you and asks for a new file name instead of replacing the original.
- **Open and drag-and-drop go by content:** a Trace project file (`.tflow`, `.tpix`) is handed to its mode, and any image opens in Paint, from whichever mode is shown. A `.tpix` opens in Pixel Art; a `.tflow` opens in Flowchart; a `.tboard` opens in Paint, and a project file of a kind this version does not know gets a clear error.
- **Project files** (`.tpix` and `.tflow`) keep everything editable, are written the same safe way as images, and are checked on opening: counts, sizes and pictures are validated, anything unusable is skipped, and a hostile or truncated file is refused with a message instead of crashing.
- **Unsaved changes:** New, Open, dropping a file and closing the window ask "Save changes?" first. The window title shows the file name, with `*` while there are unsaved changes.
- **Safe saving:** the image is written to a temporary file and renamed over the target only when complete, so a failed save (full disk, locked file) never damages the existing file. Failures are explained in a dialog; nothing crashes on a broken, truncated or hostile file.
- Known limits: JPEG EXIF rotation is not applied, and File > Open loads only the first frame of an animated GIF (a GIF dropped on Pixel Art imports all of them).

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
| Text | `T` |
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
| Mirror X | `Ctrl+Alt+H` |
| Mirror Y | `Ctrl+Alt+V` |
| New frame | `Alt+N` |
| Next frame | `.` |
| Previous frame | `,` |
| Play animation | `F5` |
| Onion skin | `Ctrl+Alt+O` |
| Tile preview | `Ctrl+Alt+T` |

### Flowchart

These are live in Flowchart mode only. The edit and view shortcuts above (Copy, Paste, Select all, Delete, zoom) work there too.

| Action | Shortcut |
|---|---|
| Flow: Select | `V` |
| Flow: Hand | `H` |
| Flow: Connector | `C` |
| Flow: Text | `T` |
| Flow: Sticky note | `N` |
| Flow: Image | `I` |
| Shape: Rectangle | `R` |
| Shape: Ellipse | `O` |
| Shape: Diamond | `D` |
| Shape: Container | `W` |
| Duplicate | `Ctrl+D` |
| Bring forward | `Ctrl+]` |
| Send backward | `Ctrl+[` |
| Bring to front | `Ctrl+Shift+]` |
| Send to back | `Ctrl+Shift+[` |
| Group | `Ctrl+G` |
| Ungroup | `Ctrl+Shift+G` |
| Auto layout | `Ctrl+Shift+L` |

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
├─ third_party/        vendored dependencies (msf_gif, nanosvg, the Inter and JetBrains Mono fonts)
├─ docs/               plans and status for the Pixel Art and Flowchart modes
├─ languages/          language packs that ship with the program (copied next to the executable)
├─ src/
│  ├─ assets/icons/    the SVG icons (embedded into the executable at configure time)
│  ├─ core/            trace_core: pure C++ (Image, Document with a layer x frame cel grid and a composite, color blending, raster algorithms, the text rasterizer, undo history with tile, replace and layer/frame structure steps, selection, resize/canvas/crop transforms, guides, dirty rects, the SVG icon rasterizer (nanosvg), view transform with configurable zoom limits, INI parser, keymap with per-mode contexts, string table and language packs, color text, background pattern math). No SDL or ImGui.
│  ├─ flow/            trace_flow: pure C++ flowchart library (FlowDocument model with nodes, edges, groups and containers; snapshot undo history; outlines, connector routing, hit tests and snapping; layout, auto layout and templates; the `.tflow` text format; the SVG exporter). Used by the Flowchart workspace; also holds the align / distribute / match-size commands
│  ├─ io/              trace_io: image loading and saving (stb, msf_gif, animated GIF, and the TIFF / ICO / QOI / PPM / SVG / PDF / DOCX writers in `image_extra.cpp`), the atomic file writer, the chunked project container used by `.tflow` and `.tpix`, the `.tpix` (Pixel Art project) and `.tboard` (whiteboard) readers and writers, save-target and UTF-8 path helpers. No SDL or ImGui.
│  ├─ tools/           trace_tools: Tool interface, the ten tools (Text included, drawn with stb_truetype; with the pixel art options: pixel-perfect pencil, square stamp, global fill) and the Toolbox that routes gestures. No SDL or ImGui.
│  ├─ platform/        trace_platform: per-OS services (CPU and RAM sampling)
│  └─ app/             Trace executable: main loop and mode switcher, the Workspace interface with the Paint, Pixel Art (Layers panel, `.tpix` file adapter) and Flowchart (board view, Shapes and Properties panels) workspaces, theme tokens, canvas view and texture mirror, toasts, SVG icon cache and icon widgets (icon menu items and buttons), tool icons, tools panel, theme and fonts, language manager, native dialogs, settings model and window, ImGui key bindings, file session and file adapters (dialogs, unsaved-changes prompt), clipboard, Resize and Canvas Size dialogs, UI
└─ tests/              doctest unit tests for trace_core and trace_tools
```

## License

MIT. See [LICENSE](LICENSE).
