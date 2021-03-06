#include <iostream>
#include <array>
#include <utility>
#include <pxr/imaging/garch/glApi.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/gprim.h>
#include "Gui.h"
#include "Editor.h"
#include "LayerEditor.h"
#include "FileBrowser.h"
#include "PropertyEditor.h"
#include "ModalDialogs.h"
#include "StageOutliner.h"
#include "Timeline.h"
#include "ContentBrowser.h"
#include "PrimSpecEditor.h"
#include "Constants.h"
#include "Commands.h"

// Get usd known file format extensions and returns then prefixed with a dot and in a vector
static const std::vector<std::string> GetUsdValidExtensions() {
    const auto usdExtensions = SdfFileFormat::FindAllFileFormatExtensions();
    std::vector<std::string> validExtensions;
    auto addDot = [](const std::string &str) { return "." + str; };
    std::transform(usdExtensions.cbegin(), usdExtensions.cend(), std::back_inserter(validExtensions), addDot);
    return std::move(validExtensions);
}

/// Modal dialog used to create a new layer
struct CreateUsdFileModalDialog : public ModalDialog {

    CreateUsdFileModalDialog(Editor &editor) : editor(editor), createStage(true){};

    void Draw() override {
        DrawFileBrowser();
        auto filePath = GetFileBrowserFilePath();

        if (FilePathExists()) {
            ImGui::TextColored(ImVec4(1.0f, 0.1f, 0.1f, 1.0f), "Overwrite: ");
        } else {
            ImGui::Checkbox("Open as stage", &createStage);
        } // ... could add other messages like permission denied, or incorrect extension
        ImGui::Text("%s", filePath.c_str());
        DrawOkCancelModal([&]() {
            if (!filePath.empty()) {
                if (createStage) {
                    editor.CreateStage(filePath);
                } else {
                    editor.CreateLayer(filePath);
                }
            }
        });
    }

    const char *DialogId() const override { return "Create usd file"; }
    Editor &editor;
    bool createStage = true;
};

/// Modal dialog to open a layer
struct OpenUsdFileModalDialog : public ModalDialog {

    OpenUsdFileModalDialog(Editor &editor) : editor(editor) { SetValidExtensions(GetUsdValidExtensions()); };
    ~OpenUsdFileModalDialog() override {}
    void Draw() override {
        DrawFileBrowser();

        if (FilePathExists()) {
            ImGui::Checkbox("Open as stage", &openAsStage);
        } else {
            ImGui::Text("Not found: ");
        }
        auto filePath = GetFileBrowserFilePath();
        ImGui::Text("%s", filePath.c_str());
        DrawOkCancelModal([&]() {
            if (!filePath.empty() && FilePathExists()) {
                if (openAsStage) {
                    editor.ImportStage(filePath);
                } else {
                    editor.ImportLayer(filePath);
                }
            }
        });
    }

    const char *DialogId() const override { return "Open layer"; }
    Editor &editor;
    bool openAsStage = true;
};

struct SaveLayerAs : public ModalDialog {

    SaveLayerAs(Editor &editor) : editor(editor){};
    ~SaveLayerAs() override {}
    void Draw() override {
        DrawFileBrowser();
        auto filePath = GetFileBrowserFilePath();
        if (FilePathExists()) {
            ImGui::TextColored(ImVec4(1.0f, 0.1f, 0.1f, 1.0f), "Overwrite: ");
        } else {
            ImGui::Text("Save to: ");
        }
        ImGui::Text("%s", filePath.c_str());
        DrawOkCancelModal([&]() { // On Ok ->
            if (!filePath.empty() && !FilePathExists()) {
                editor.SaveCurrentLayerAs(filePath);
            }
        });
    }

    const char *DialogId() const override { return "Save layer as"; }
    Editor &editor;
};


static void BeginBackgoundDock() {
    // Setup dockspace using experimental imgui branch
    static bool alwaysOpened = true;
    static ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_None;
    static ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", &alwaysOpened, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceid = ImGui::GetID("dockspace");
    ImGui::DockSpace(dockspaceid, ImVec2(0.0f, 0.0f), dockFlags);
}

static void EndBackgroundDock() {
    ImGui::End();
}


/// Call back for dropping a file in the ui
/// TODO Drop callback should popup a modal dialog with the different options available
void Editor::DropCallback(GLFWwindow *window, int count, const char **paths) {
    void *userPointer = glfwGetWindowUserPointer(window);
    if (userPointer) {
        Editor *editor = static_cast<Editor *>(userPointer);
        // TODO: Create a task, add a callback
        if (editor && count) {
            for (int i = 0; i < count; ++i) {
                // make a drop event ?
                if (ArchGetFileLength(paths[i]) == 0) {
                    // if the file is empty, this is considered a new file
                    editor->CreateStage(std::string(paths[i]));
                } else {
                    editor->ImportLayer(std::string(paths[i]));
                }
            }
        }
    }
}


Editor::Editor() : _viewport(UsdStageRefPtr(), _selection) {
    ExecuteAfterDraw<EditorSetDataPointer>(this); // This is specialized to execute here, not after the draw
}

Editor::~Editor(){}

void Editor::SetCurrentStage(UsdStageCache::Id current) {
    SetCurrentStage(_stageCache.Find(current));
}

void Editor::SetCurrentStage(UsdStageRefPtr stage) {
    _currentStage = stage;
    // NOTE: We set the default layer to the current stage root
    // this might have side effects
    if (!GetCurrentLayer() && _currentStage) {
        SetCurrentLayer(_currentStage->GetRootLayer());
    }
    // TODO multiple viewport management
    _viewport.SetCurrentStage(stage);
}

void Editor::SetCurrentLayer(SdfLayerRefPtr layer) {
    if (!layer)
        return;
    if (!_layerHistory.empty()) {
        if (GetCurrentLayer() != layer) {
            if (_layerHistoryPointer < _layerHistory.size() - 1) {
                _layerHistory.resize(_layerHistoryPointer + 1);
            }
            _layerHistory.push_back(layer);
            _layerHistoryPointer = _layerHistory.size() - 1;
        }
    } else {
        _layerHistory.push_back(layer);
        _layerHistoryPointer = _layerHistory.size() - 1;
    }
}

void Editor::SetCurrentEditTarget(SdfLayerHandle layer) {
    if (GetCurrentStage()) {
        GetCurrentStage()->SetEditTarget(UsdEditTarget(layer));
    }
}

SdfLayerRefPtr Editor::GetCurrentLayer() {
    return _layerHistory.empty() ? SdfLayerRefPtr() : _layerHistory[_layerHistoryPointer];
}

void Editor::SetPreviousLayer() {
    if (_layerHistoryPointer > 0) {
        _layerHistoryPointer--;
    }
}


void Editor::SetNextLayer() {
    if (_layerHistoryPointer < _layerHistory.size()-1) {
        _layerHistoryPointer++;
    }
}


void Editor::UseLayer(SdfLayerRefPtr layer) {
    if (layer) {
        if (_layers.find(layer) == _layers.end()) {
            _layers.emplace(layer);
        }
        SetCurrentLayer(layer);
        _showContentBrowser = true;
        _showLayerEditor = true;
    }
}


void Editor::CreateLayer(const std::string &path) {
    auto newLayer = SdfLayer::CreateNew(path);
    UseLayer(newLayer);
}

void Editor::ImportLayer(const std::string &path) {
    auto newLayer = SdfLayer::FindOrOpen(path);
    UseLayer(newLayer);
}

//
void Editor::ImportStage(const std::string &path) {
    auto newStage = UsdStage::Open(path);
    if (newStage) {
        _stageCache.Insert(newStage);
        SetCurrentStage(newStage);
        _showContentBrowser = true;
        _showViewport = true;
    }
}

void Editor::SaveCurrentLayerAs(const std::string &path) {
    auto newLayer = SdfLayer::CreateNew(path);
    if (newLayer && GetCurrentLayer()) {
        newLayer->TransferContent(GetCurrentLayer());
        newLayer->Save();
        UseLayer(newLayer);
    }
}

void Editor::CreateStage(const std::string &path) {
    auto usdaFormat = SdfFileFormat::FindByExtension("usda");
    auto layer = SdfLayer::New(usdaFormat, path);
    if (layer) {
        auto newStage = UsdStage::Open(layer);
        if (newStage) {
            _stageCache.Insert(newStage);
            SetCurrentStage(newStage);
            _showContentBrowser = true;
            _showViewport = true;
        }
    }
}

Viewport & Editor::GetViewport() {
    return _viewport;
}

void Editor::HydraRender() {
    _viewport.Update();
    _viewport.Render();
}

void Editor::DrawMainMenuBar() {

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_FA_FILE " New")) {
                DrawModalDialog<CreateUsdFileModalDialog>(*this);
            }
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open")) {
                 DrawModalDialog<OpenUsdFileModalDialog>(*this);
            }
            ImGui::Separator();
            const bool hasLayer = GetCurrentLayer() != SdfLayerRefPtr();
            if (ImGui::MenuItem(ICON_FA_SAVE " Save layer", "CTRL+S", false, hasLayer)) {
                GetCurrentLayer()->Save(true);
            }
            if (ImGui::MenuItem(ICON_FA_SAVE " Save layer as", "CTRL+F", false, hasLayer)) {
                DrawModalDialog<SaveLayerAs>(*this);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                _shutdownRequested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "CTRL+Z")) {
                ExecuteAfterDraw<UndoCommand>();
            }
            if (ImGui::MenuItem("Redo", "CTRL+R")) {
                ExecuteAfterDraw<RedoCommand>();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "CTRL+X", false, false)) {
            }
            if (ImGui::MenuItem("Copy", "CTRL+C", false, false)) {
            }
            if (ImGui::MenuItem("Paste", "CTRL+V", false, false)) {
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Debug window", nullptr, &_showDebugWindow);
            ImGui::MenuItem("Property editor", nullptr, &_showPropertyEditor);
            ImGui::MenuItem("Stage outliner", nullptr, &_showOutliner);
            ImGui::MenuItem("Timeline", nullptr, &_showTimeline);
            ImGui::MenuItem("Content browser", nullptr, &_showContentBrowser);
            ImGui::MenuItem("Layer editor", nullptr, &_showLayerEditor);
            ImGui::MenuItem("Viewport", nullptr, &_showViewport);
            ImGui::MenuItem("SdfPrim editor", nullptr, &_showPrimSpecEditor);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void Editor::Draw() {

    NewFrame();

    // Dock
    BeginBackgoundDock();

    // Main Menu bar
    DrawMainMenuBar();

    if (_showViewport) {
        ImGui::Begin("Viewport", &_showViewport);
        ImVec2 wsize = ImGui::GetWindowSize();
        GetViewport().SetSize(wsize.x, wsize.y - ViewportBorderSize); // for the next render
        GetViewport().Draw();
        ImGui::End();
    }

    if (_showDebugWindow) {
        ImGui::Begin("Debug window", &_showDebugWindow);
        // DrawDebugInfo();
        ImGui::Text("\xee\x81\x99" " %.3f ms/frame  (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

    if (_showPropertyEditor) {
        ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_MenuBar;
        ImGui::Begin("Property editor", &_showPropertyEditor, windowFlags);
        if (GetCurrentStage()) {
            auto prim = GetCurrentStage()->GetPrimAtPath(GetSelectedPath(_selection));
            DrawUsdPrimProperties(prim, GetViewport().GetCurrentTimeCode());
        }
        ImGui::End();
    }

    if (_showOutliner) {
        ImGui::Begin("Stage outliner", &_showOutliner);
        DrawStageOutliner(GetCurrentStage(), _selection);
        ImGui::End();
    }

    if (_showTimeline) {
        ImGui::Begin("Timeline", &_showTimeline);
        UsdTimeCode tc = GetViewport().GetCurrentTimeCode();
        DrawTimeline(GetCurrentStage(), tc);
        GetViewport().SetCurrentTimeCode(tc);
        ImGui::End();
    }

    if (_showLayerEditor) {
        auto rootLayer = GetCurrentLayer();

        const std::string title(
            "Layer editor" + (rootLayer ? (" - " + rootLayer->GetDisplayName() + (rootLayer->IsDirty() ? " *" : " ")) : "") +
            "###Layer editor");

        ImGui::Begin(title.c_str(), &_showLayerEditor);
        DrawLayerEditor(rootLayer, GetSelectedPrimSpec());
        ImGui::End();
    }

    if (_showContentBrowser) {
        ImGui::Begin("Content browser", &_showContentBrowser);
        DrawContentBrowser(*this);
        ImGui::End();
    }

    if (_showPrimSpecEditor) {
        ImGui::Begin("SdfPrim editor", &_showPrimSpecEditor);
        if (GetSelectedPrimSpec()) {
            DrawPrimSpecEditor(GetSelectedPrimSpec());
        } else {
            DrawLayerHeader(GetCurrentLayer());
        }
        ImGui::End();
    }

    DrawCurrentModal();

    ///////////////////////
    // The following piece of code should give birth to a more refined "shortcut" system
    //
    ImGuiIO &io = ImGui::GetIO();
    static bool UndoCommandpressedOnce = true;
    if (io.KeysDown[GLFW_KEY_LEFT_CONTROL] && io.KeysDown[GLFW_KEY_Z]) {
        if (UndoCommandpressedOnce) {
            ExecuteAfterDraw<UndoCommand>();
            UndoCommandpressedOnce = false;
        }
    } else {
        UndoCommandpressedOnce = true;
    }
    static bool RedoCommandpressedOnce = true;
    if (io.KeysDown[GLFW_KEY_LEFT_CONTROL] && io.KeysDown[GLFW_KEY_R]) {
        if (RedoCommandpressedOnce) {
            ExecuteAfterDraw<RedoCommand>();
            RedoCommandpressedOnce = false;
        }
    } else {
        RedoCommandpressedOnce = true;
    }

    /////////////////

    EndBackgroundDock();
    ImGui::Render();
}
