
#include <array>
#include <pxr/usd/usd/stage.h>
#include "Gui.h"
#include "ContentBrowser.h"
#include "LayerEditor.h" // for DrawLayerMenuItems
#include "Commands.h"
#include "Constants.h"

PXR_NAMESPACE_USING_DIRECTIVE

void DrawStageCache(UsdStageCache &cache, UsdStageCache::Id *selectedStage = nullptr, const ImVec2 &listSize = ImVec2(0, -10)) {
    ImGui::PushItemWidth(-1);
    if (ImGui::BeginListBox("##DrawStageCache", listSize)) {
        auto allStages = cache.GetAllStages();
        for (auto stage : allStages) {
            bool selected = selectedStage && *selectedStage == cache.GetId(stage);
            ImGui::PushID(stage->GetRootLayer()->GetRealPath().c_str());
            if (ImGui::Selectable(stage->GetRootLayer()->GetDisplayName().c_str(), selected)) {
                if (selectedStage)
                    *selectedStage = cache.GetId(stage);
            }
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > TimeBeforeTooltip) {
                ImGui::SetTooltip("%s", stage->GetRootLayer()->GetRealPath().c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
}

template <typename SdfLayerSetT>
void DrawLayerSet(SdfLayerSetT &layerSet, SdfLayerHandle *selectedLayer, const ImVec2 &listSize = ImVec2(0, -10)) {
    ImGui::PushItemWidth(-1);

    // Sort the layer set
    std::vector<SdfLayerHandle> sortedSet(layerSet.begin(), layerSet.end());
    std::sort(sortedSet.begin(), sortedSet.end(),
              [](const auto &t1, const auto &t2) { return t1->GetDisplayName() < t2->GetDisplayName(); });
    static ImGuiTextFilter filter;
    filter.Draw();
    if (ImGui::BeginListBox("##DrawLayerSet", listSize)) { // TODO: anonymous different per type ??
        for (const auto &layer : sortedSet) {
            if (!layer)
                continue;
            bool selected = selectedLayer && *selectedLayer == layer;
            std::string layerName = std::string(layer->IsDirty() ? ICON_FA_SAVE " " : "  ") +
                                    (layer->GetAssetName() != "" ? layer->GetAssetName() : layer->GetIdentifier());

            if (filter.PassFilter(layerName.c_str())) {
                ImGui::PushID(layer->GetUniqueIdentifier());
                if (ImGui::Selectable(layerName.c_str(), selected)) {
                    if (selectedLayer)
                        *selectedLayer = layer;
                }
                if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 2) {
                    ImGui::SetTooltip("%s\n%s", layer->GetRealPath().c_str(), layer->GetIdentifier().c_str());
                    auto assetInfo = layer->GetAssetInfo();
                    if (!assetInfo.IsEmpty()) {
                        if (assetInfo.CanCast<VtDictionary>()) {
                            auto assetInfoDict = assetInfo.Get<VtDictionary>();
                            TF_FOR_ALL(keyValue, assetInfoDict) {
                                std::stringstream ss;
                                ss << keyValue->second;
                                ImGui::SetTooltip("%s %s", keyValue->first.c_str(), ss.str().c_str());
                            }
                        }
                    }
                }

                if (ImGui::BeginPopupContextItem()) {
                    DrawLayerActionPopupMenu(layer);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
}

void DrawContentBrowser(Editor &editor) {

    if (ImGui::BeginTabBar("theatertabbar")) {
        if (ImGui::BeginTabItem("Stages")) {
            UsdStageCache::Id selected;
            DrawStageCache(editor.GetStageCache(), &selected);
            if (selected != UsdStageCache::Id()) {
                editor.SetCurrentStage(selected);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Layers")) {
            SdfLayerHandle selected(editor.GetCurrentLayer());
            auto layers = SdfLayer::GetLoadedLayers();
            DrawLayerSet(layers, &selected);
            if (selected != editor.GetCurrentLayer()) {
                editor.SetCurrentLayer(selected);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
