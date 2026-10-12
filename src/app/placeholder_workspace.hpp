#pragma once

#include <string>

#include "app/file_adapter.hpp"
#include "app/file_session.hpp"
#include "app/workspace.hpp"

namespace trace {

// The file side of a mode that has no document yet: nothing to save, and nothing but images to open (which
// belong to Paint and are routed there).
class PlaceholderFileAdapter final : public FileAdapter {
public:
    explicit PlaceholderFileAdapter(Mode mode) : mode_(mode) {}

    bool hasDocument() const override { return false; }
    bool isModified() const override { return false; }
    void markSaved() override {}
    std::string newBlank() override { return {}; }
    LoadOutcome load(const std::filesystem::path& path) override;
    std::span<const FileFormat> saveFormats() const override { return {}; }
    std::optional<int> formatForPath(const std::filesystem::path&) const override { return std::nullopt; }
    std::string save(const std::filesystem::path&, int, const SaveOptions&) override { return {}; }
    std::span<const FileFilter> openFilters() const override;

private:
    Mode mode_;
};

// Flowchart and Pixel Art are still being built. Until they are, each has a dockspace of its own (so the
// mode switcher, the saved layout and the exit guard already work) holding one window that says so.
class PlaceholderWorkspace final : public Workspace {
public:
    PlaceholderWorkspace(WorkspaceContext& context, Mode mode);

    Mode mode() const noexcept override { return mode_; }
    void init(SDL_Window* window, SDL_Renderer* renderer) override;

    ImGuiID dockSpaceId() const override;
    void buildDefaultLayout(ImGuiID dockId, ImVec2 size) override;
    void drawWindows() override;
    void drawDialogs() override { files_.drawModals(); }

    std::string statusLeft() const override;
    bool busy() const override { return files_.busy(); }

    FileSession& fileSession() override { return files_; }

private:
    // "Title###Id": the visible title follows the language, the id (and so the saved layout) never changes.
    std::string windowName() const;

    Mode mode_;
    PlaceholderFileAdapter adapter_;
    FileSession files_;
};

} // namespace trace
