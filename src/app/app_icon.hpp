#pragma once

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace trace {

// The application icon (src/assets/icons/app_icon.png, built into the program). The same picture is the Windows
// exe icon (src/platform/windows/trace.rc), so Explorer, the taskbar and the title bar all agree.

// Sets the window's title bar / taskbar icon with several sizes (the picture, halved down to 16 px) so the system
// picks the sharpest one for the display scale. Returns false, leaving the default icon, when decoding or SDL fails.
bool setWindowIcon(SDL_Window* window);

// The texture of the icon for the About page. initAppIcon gives it the renderer; the texture is made on first use
// (null when that fails or there is no renderer). releaseAppIcon must run before the renderer is destroyed.
void initAppIcon(SDL_Renderer* renderer) noexcept;
SDL_Texture* appIconTexture();
void releaseAppIcon() noexcept;

} // namespace trace
