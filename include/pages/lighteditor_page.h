#pragma once 

#include "../SKSEMenuFramework.h"
#include "../logger.hpp"
#include "../config.h"
#include "../LightData.h"
#include "..//LightManager.h"
#include "../Utility.h"
#include <format>

// note static in a header means each .cpp gets its own seperate vector 

inline vector<RE::NiPointer<RE::BSLight>> relightLights = {};
//vanilla, elfx, lux ect
inline vector<RE::NiPointer<RE::BSLight>> pluginLights = {};

void __stdcall RenderLightEditor();


inline auto lightbulbIcon = FontAwesome::UnicodeToUtf8(0xf0eb);

inline auto palletIcon = FontAwesome::UnicodeToUtf8(0xf53f);

inline auto coordinatesIcon = FontAwesome::UnicodeToUtf8(0xf601);

inline auto editorIcon = FontAwesome::UnicodeToUtf8(0xf044);

inline auto trashIcon = FontAwesome::UnicodeToUtf8(0xf1f8);

inline auto plusIcon = FontAwesome::UnicodeToUtf8(0xf055);

inline auto flagIcon = FontAwesome::UnicodeToUtf8(0xf024);


struct LightGroupData
{
    // All lights in the scene, keyed by their index in the global `lights` vector.
    std::unordered_map<std::string, std::vector<int>> byConfigPath;

    // configPath groups that belong to a named menuCategory.
    std::unordered_map<std::string, std::vector<std::string>> byCategory;

    // configPath groups with no menuCategory.
    std::vector<std::string> uncategorized;

    // configPaths in alphabetical display order (by resolved menu name).
    std::vector<std::string> sortedConfigPaths;

    // How many lights share the same resolved menu name (used to disambiguate
    // with a mesh path suffix).
    std::unordered_map<std::string, int> menuNameCounts;
};



inline void DrawExternalEmittanceSelector(
    LightConfig& config,
    RE::TESObjectREFR* selectedRef,
    const std::vector<std::pair<std::string, RE::TESRegion*>>& regionList,
    bool& showEmittanceWindow)
{
    if (ImGuiMCP::Button("External Emittance")) {
        showEmittanceWindow = !showEmittanceWindow;
    }

    if (showEmittanceWindow) {
        if (ImGuiMCP::Begin("External Emittance", &showEmittanceWindow,
            ImGuiMCP::ImGuiWindowFlags_NoCollapse |
            ImGuiMCP::ImGuiWindowFlags_NoDocking)) {

            if (ImGuiMCP::Selectable("None", config.emittanceRegion == nullptr)) {
                config.emittanceRegion = nullptr;
                config.externalEmittance.clear();

                if (config.isPluginLight && selectedRef) {
                    LightData::SetPluginLightEmittanceSource(selectedRef, nullptr);
                }
            }

            for (auto& [editorID, region] : regionList) {
                bool selected = config.emittanceRegion == region;

                if (ImGuiMCP::Selectable(editorID.c_str(), selected)) {
                    config.emittanceRegion = region;
                    config.externalEmittance = editorID;

                    if (region) {
                        UpdateRegionEmittance(region->emittanceColor, region);
                    }

                    if (config.isPluginLight && selectedRef) {
                        if (auto* form = RE::TESForm::LookupByEditorID(editorID)) {
                            LightData::SetPluginLightEmittanceSource(selectedRef, form);
                        }
                    }

                    showEmittanceWindow = false;
                }
            }
        }
        ImGuiMCP::End();
    }

    if (ImGuiMCP::IsItemHovered()) {
        ImGuiMCP::BeginTooltip();

        ImGuiMCP::Text("Current region:");

        ImGuiMCP::SameLine();
        ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s",
            config.externalEmittance.c_str());


        ImGuiMCP::Text("Select a region used for external emittance "
            "(Change light colors based on time of day, good for window lights)");

        ImGuiMCP::EndTooltip();
    }
}

inline void restoreLightToDefaults(RE::NiPointer<RE::NiLight> light) {
    if (!light || !light.get()) {
        logger::error("Selected light is null, cannot restore defaults");
        return;

    }

    const std::string lightName = light->name.c_str();

    LightConfig backupCfg;

    auto itDefault = LightData::defaultConfigs.find(light->unk138);
    if (itDefault == LightData::defaultConfigs.end()) {
        logger::warn("no config found to restore defaults from");
        return;
    }
    else {
        backupCfg = itDefault->second;
    }

    auto& lightData = light->GetLightRuntimeData();

    auto itCfg = LightData::configIDToJsonCfg.find(lightData.unk138);
    if (itCfg == LightData::configIDToJsonCfg.end()) {
        logger::warn("No JSON config entry found for light ID {}", lightData.unk138);
        return;
    }
    auto& cfg = itCfg->second;

    cfg.position = backupCfg.position;


    auto ref = LightData::GetRefFromLight(light.get());

    if (!ref) {
        logger::warn("no ref for configID {} cant restore defaults", light->unk138);
        return;
    }

    cfg.diffuseColor[0] = backupCfg.diffuseColor[0];
    cfg.diffuseColor[1] = backupCfg.diffuseColor[1];
    cfg.diffuseColor[2] = backupCfg.diffuseColor[2];

    lightData.radius = LightData::getNiPointLightRadius(backupCfg, ref->GetScale());
    lightData.fade = backupCfg.brightness;
    LightData::setNiPointLightAmbientAndDiffuse(light.get(), backupCfg);
    LightData::setNiPointLightPos(light.get(), backupCfg);

    //update the parent or sometimes it doesent work.
    if (auto* parent = light->parent) {
        RE::NiUpdateData updateData{};
        updateData.time = 0.0f;
        updateData.flags = RE::NiUpdateData::Flag::kDirty;
        parent->UpdateTransformAndBounds(updateData);
    }

    cfg.startingFade = backupCfg.startingFade;
    cfg.flickerIntensity = backupCfg.flickerIntensity;
    cfg.flickersPerSecond = backupCfg.flickersPerSecond;
    cfg.flickerAmplitude = backupCfg.flickerAmplitude;
    cfg.flags = backupCfg.flags;
    cfg.menuName = backupCfg.menuName;
    cfg.menuCategory = backupCfg.menuCategory;
    cfg.externalEmittance = backupCfg.externalEmittance;
    cfg.emittanceRegion = backupCfg.emittanceRegion;

    // Propagate to active lights in the shader node
    auto* ssNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
    if (!ssNode) return;

    auto& rt = ssNode->GetRuntimeData();

    auto updateLightList = [&](auto& lightList) {
        for (auto& currentLight : lightList) {
            if (!currentLight) continue;

            auto& activeData = currentLight->light->GetLightRuntimeData();

            if (activeData.unk138 == lightData.unk138) {
                activeData = lightData;
                currentLight->light->local.translate = light->local.translate;
                currentLight->light->worldBound.center = light->local.translate;

                if (globals::islInstalled) {
                    if (auto* isl = Overlay::Get(currentLight->light.get())) {
                        isl->cutoffOverride = backupCfg.cutoffOverride;
                        isl->size = backupCfg.size;
                    }
                }
            }
        }
        };

    updateLightList(rt.activeLights);
    updateLightList(rt.activeShadowLights);

    logger::info("Restored '{}' to default config", lightName);
}

// ---------------------------------------------------------------------------
// Per-plugin-group bulk actions (Default All / Save All), added 2026-11-08.
// The single-light Save / Default buttons in RenderLightEditor() and the
// per-group buttons in RenderLightList() below run exactly the same code
// through these two helpers, so a group action can never drift from what
// clicking Save / Default on one light does.
// ---------------------------------------------------------------------------

// Save one light, exactly as the old per-light Save button did:
// refresh the LightConfig from the live light, update the runtime caches,
// then write it out. For a plugin light with no JSON file yet this creates
// one (saveNewConfiguration); otherwise it overwrites the existing entry
// (saveConfiguration). Returns false (and logs why) when it cannot save.
inline bool SaveSingleLight(const RE::NiPointer<RE::BSLight>& bsLight)
{
    if (!bsLight || !bsLight->light) {
        logger::error("no ni light from bslight when saving template");
        return false;
    }

    auto niLight = bsLight->light.get();

    std::string lightNameRL = niLight->name.c_str();

    if (lightNameRL.empty()) {
        logger::debug("Nilight name empty when saving template");
        return false;
    }

    // strip whichever prefix applies ("RL" or "PL")
    auto lightName = removePrefix(lightNameRL, "RL");

    LightConfig cfg;

    if (LightData::foundConfigForLightByConfigID(niLight)) {

        auto& baseConfig = LightData::configIDToJsonCfg[niLight->unk138];

        LightData::updateConfigFromLight(cfg, baseConfig, niLight);

        if (!LightData::updateRuntimeConfigCaches(cfg)) {
            logger::warn("Failed to update runtime config caches for '{}'", lightName);
        }

        // Plugin light: create a new JSON config if one doesn't already exist
        if (baseConfig.isPluginLight) {

            bool configExists =
                !baseConfig.configPath.empty() &&
                std::filesystem::exists(baseConfig.configPath);

            if (!configExists) {
                logger::info(
                    "Plugin light '{}' has no existing config file. Creating new configuration.",
                    lightName
                );
                saveNewConfiguration(cfg);
                logger::info("created new json file for {} at path {} ", baseConfig.menuName, cfg.configPath);
                return true;
            }
            else {
                // Existing plugin config - save normally
                saveConfiguration(cfg);
                logger::info("Saving file for {} at path {} ", baseConfig.menuName, cfg.configPath);
                return true;
            }
        }
        else {
            // Existing Relight / normal light
            if (!cfg.configPath.empty()) {
                saveConfiguration(cfg);
                return true;
            }
            else {
                logger::warn(
                    "Config for '{}' has no configPath, cannot save",
                    lightName
                );
                return false;
            }
        }
    }
    else {
        logger::warn("No config found for light '{}'", lightName);
        return false;
    }
}

// Reset one light to defaults, exactly as the old per-light Default button
// did. Returns false when restoreLightToDefaults() would bail out early
// (no default config, no JSON config entry, or no ref), so a group action
// can count real failures instead of counting a no-op as a success.
// NOTE: like the single-light Default button, this only resets the
// in-memory config and the live light - it does NOT write any JSON file.
// Use SaveSingleLight() / Save All afterwards to persist it.
inline bool RestoreSingleLightToDefaults(const RE::NiPointer<RE::BSLight>& bsLight)
{
    if (!bsLight || !bsLight->light) {
        logger::warn("Default clicked but no light selected");
        return false;
    }

    auto* niLight = bsLight->light.get();

    if (LightData::defaultConfigs.find(niLight->unk138) == LightData::defaultConfigs.end()) {
        logger::warn("no config found to restore defaults from");
        return false;
    }
    if (LightData::configIDToJsonCfg.find(niLight->GetLightRuntimeData().unk138) == LightData::configIDToJsonCfg.end()) {
        logger::warn("No JSON config entry found for light ID {}", niLight->GetLightRuntimeData().unk138);
        return false;
    }
    if (!LightData::GetRefFromLight(niLight)) {
        logger::warn("no ref for configID {} cant restore defaults", niLight->unk138);
        return false;
    }

    restoreLightToDefaults(bsLight->light);
    logger::info("Restored defaults for '{}'", niLight->name.c_str());
    return true;
}

// Last bulk-action result per group (menuCategory), shown on the group row
// after a Default All / Save All click, e.g. "Saved 3/3 (0 failed)".
inline std::unordered_map<std::string, std::string> groupBulkResult;



inline int getLightConfigID(const RE::NiPointer<RE::BSLight>& l) {
    if (!l || !l->light) return -1;
    return l->light->GetLightRuntimeData().unk138;  // runtime configID key
}

inline void refreshLight(
    const RE::NiPointer<RE::BSLight>& activeLight,
    std::vector<RE::NiPointer<RE::BSLight>>& refreshedLights,
    std::unordered_map<int, int>& keyToIndex,
    std::unordered_set<int>& seen,
    std::string_view prefix)
{
    if (!activeLight || !activeLight->light || !activeLight->light.get()) return;

    std::string_view name{ activeLight->light->name.c_str() };

    if (name.size() < prefix.size() || name.substr(0, prefix.size()) != prefix) {
        return;
    }

    int configID = getLightConfigID(activeLight);

    if (configID == 0 && prefix == "ol") {
        RE::NiLight* niLight = activeLight->light.get();
        auto* ref = niLight->GetUserData();

        if (ref) {
            auto* baseObject = ref->GetBaseObject();

            if (baseObject && baseObject->formType == RE::FormType::Light) {
                auto* lightTemplate = static_cast<RE::TESObjectLIGH*>(baseObject);

                if (!lightTemplate && activeLight->unk060 == 4) {
                    logger::warn("torch  ref base object is not a tesobjectLIGH");
                }

                std::string edid = clib_util::editorID::get_editorID(baseObject);
                const RE::TESFile* refOriginFile = ref->GetDescriptionOwnerFile();
                std::string modName = refOriginFile ? refOriginFile->fileName : "";

                LightConfig cfg;
                CreateConfigFromPluginLight(cfg, niLight, lightTemplate, ref, edid, modName, false);

                niLight->unk138 = cfg.configID;
                configID = cfg.configID;

                logger::info("Created config for 'ol' (object light) light: {} (ID: {})", cfg.menuName, cfg.configID);
            }
        }
        else {
            logger::warn("no reference for light {}", niLight->name.c_str());
        }
    }

    // If still no key, return (skip this light)
    if (configID <= 0) return;

    seen.insert(configID);

    auto it = LightData::configIDToJsonCfg.find(configID);
    if (it == LightData::configIDToJsonCfg.end()) {
        logger::debug("light :{} (config ID={}) has no json cfg entry", name, configID);
    }

    auto itIdx = keyToIndex.find(configID);
    if (itIdx == keyToIndex.end()) {
        refreshedLights.push_back(activeLight);
        keyToIndex[configID] = (int)refreshedLights.size() - 1;
    }
    else {
        refreshedLights[itIdx->second] = activeLight;
    }
}

inline void refreshAllLights(
    int& selectedIndex,
    std::vector<RE::NiPointer<RE::BSLight>>& lights,
    std::string_view prefix)
{
    auto* ssNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
    if (!ssNode) {
        logger::warn("ShadowSceneNode[0] is null!");
        return;
    }
    auto& rt = ssNode->GetRuntimeData();

    int selectedConfigID = -1;
    if (selectedIndex >= 0 && selectedIndex < (int)lights.size()) {
        selectedConfigID = getLightConfigID(lights[selectedIndex]);
    }

    std::unordered_map<int, int> keyToIndex;
    keyToIndex.reserve(lights.size());
    for (int i = 0; i < (int)lights.size(); ++i) {
        int key = getLightConfigID(lights[i]);
        if (key >= 0) keyToIndex[key] = i;
    }

    std::unordered_set<int> seen;
    seen.reserve(256);

    for (auto& activeLight : rt.activeLights) {
        refreshLight(activeLight, lights, keyToIndex, seen, prefix);
    }
    for (auto& activeShadowLight : rt.activeShadowLights) {
        refreshLight(activeShadowLight, lights, keyToIndex, seen, prefix);
    }

    lights.erase(std::remove_if(lights.begin(), lights.end(),
        [&](const RE::NiPointer<RE::BSLight>& l) {
            int key = getLightConfigID(l);
            return key < 0 || (seen.find(key) == seen.end());
        }),
        lights.end());

    selectedIndex = -1;
    if (selectedConfigID >= 0) {
        for (int i = 0; i < (int)lights.size(); ++i) {
            if (getLightConfigID(lights[i]) == selectedConfigID) {
                selectedIndex = i;
                break;
            }
        }
    }
}

// timing is a real issue, ive tried 4 or 5 different ways anjd lights dont intiialize 
// properly and this was the way that i ended up on
inline void RefreshNonRuntimeSettings(LightConfig cfg)
{

    auto* ssNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
    if (!ssNode) {
        logger::warn("ShadowSceneNode[0] is null cant reinitialize lights");
        return;
    }

    auto addLight = [&](const RE::NiPointer<RE::NiLight>& light) {
        if (!light)
            return;

        auto ref = light->GetUserData();
        if (!ref) {
            logger::warn("no ref for configID {} cant refresh non runtime settings", light->unk138);
            return;
        }

        // delay
        SKSE::GetTaskInterface()->AddTask([cfg, ref]() mutable {

            const RE::FormID formID = ref->GetFormID();

            auto loadedModel = ref->Get3D();
            if (!loadedModel) {
                logger::error("no loaded model for ref, cant refresh non runtime settings");
                return;
            }

            RE::NiNode* root = loadedModel->AsNode();
            if (!root) {
                logger::error("no attachable node for ref cant refresh non runtime settings");
                return;
            }

            auto baseObj = ref->GetBaseObject();
            if (!baseObj) {
                logger::error("no base object for ref cant refresh non runtime settings");
                return;
            }

            const auto bm = baseObj->As<RE::TESModel>();
            if (!bm) {
                logger::error("base object is not TESModel cant refresh non runtime settings");
                return;
            }

            auto currentModel = std::string(bm->GetModel());

            auto meshName = extractMeshName(currentModel);

            bool attachedDebugMarker = false;

            RE::NiLight* niLight = LightManager::AttachLight(
                cfg,
                root,
                ref,
                meshName,
                formID,
                attachedDebugMarker);

            if (!niLight) {
                logger::error("no niLight cant refresh non runtime settings");
                return;
            }

            UpdateRefRootTransforms(ref);

            });

        };

    for (const auto& l : ssNode->activeLights) {
        if (!l || !l->light)
            continue;

        //skip non relight lights
        auto name = std::string_view(l->light->name.c_str());
        if (name.size() < 2 || name[0] != 'R' || name[1] != 'L')
            continue;

        // find config of current light
        auto it2 = LightData::configIDToJsonCfg.find(l->light->unk138);

        if (it2 == LightData::configIDToJsonCfg.end()) {
            logger::warn("configID {} not found in config map cant refresh lights", cfg.configID);
            return;
        }

        //if current light does not == selected light config path and json index, skip
        // this is because multiple configs can have same json index and config path but have different config ids.
        if (it2->second.configPath != cfg.configPath || it2->second.jsonIndex != cfg.jsonIndex)
            continue;

        // remove any matches 
        ssNode->RemoveLight(l->light.get());

        // add anew light
        addLight(l->light);
    }

    for (const auto& l : ssNode->activeShadowLights) {
        if (!l || !l->light)
            continue;

        auto name = std::string_view(l->light->name.c_str());
        if (name.size() < 2 || name[0] != 'R' || name[1] != 'L')
            continue;

        auto it3 = LightData::configIDToJsonCfg.find(l->light->unk138);

        if (it3 == LightData::configIDToJsonCfg.end()) {
            logger::warn("configID {} not found in config map cant refresh lights", cfg.configID);
            return;
        }

        if (it3->second.configPath != cfg.configPath || it3->second.jsonIndex != cfg.jsonIndex)
            continue;

        ssNode->RemoveLight(l->light.get());

        addLight(l->light);
    }

    // reset tri light cache otherwise light flcker prevention mode wont let the light on.
    globals::secondAfterCellFullyLoaded.store(false);
    LightData::ResetTriLightCache();
}

inline std::string ResolveMenuName(const LightConfig& cfg)
{
    if (!cfg.menuName.empty())
        return cfg.menuName;
    if (!cfg.meshPaths.empty())
        return cfg.meshPaths[0];
    return "Unknown Light";
}

inline bool DeleteSelectedLightTemplate(int& selectedIndex, std::vector<RE::NiPointer<RE::BSLight>>& lights)
{
    if (selectedIndex < 0 || selectedIndex >= lights.size())
        return false;

    auto selectedLight = lights[selectedIndex];

    if (!selectedLight || !selectedLight->light)
        return false;

    auto niLight = selectedLight->light.get();

    auto it = LightData::configIDToJsonCfg.find(niLight->unk138);
    if (it == LightData::configIDToJsonCfg.end())
        return false;

    const std::string deletedPath = it->second.configPath;

    if (deletedPath.empty())
        return false;

    if (!std::filesystem::remove(deletedPath))
        return false;

    auto removeByConfigPath = [&](auto& map)
        {
            for (auto itMap = map.begin(); itMap != map.end();) {
                auto& vec = itMap->second;

                vec.erase(
                    std::remove_if(
                        vec.begin(),
                        vec.end(),
                        [&](const LightConfig& c)
                        {
                            return c.configPath == deletedPath;
                        }),
                    vec.end());

                if (vec.empty()) {
                    itMap = map.erase(itMap);
                }
                else {
                    ++itMap;
                }
            }
        };

    for (auto itCfg = LightData::configIDToJsonCfg.begin();
        itCfg != LightData::configIDToJsonCfg.end();)
    {
        if (itCfg->second.configPath == deletedPath) {
            itCfg = LightData::configIDToJsonCfg.erase(itCfg);
        }
        else {
            ++itCfg;
        }
    }

    removeByConfigPath(LightData::meshPathToJsonCfg);
    removeByConfigPath(LightData::meshPathToJsonCfgExteriors);
    removeByConfigPath(LightData::refFormIDToJsonCfg);
    removeByConfigPath(LightData::refFormIDToJsonCfgExteriors);

    logger::info("Deleted light template '{}'", deletedPath);

    selectedIndex = -1;

    return true;
}

inline LightGroupData BuildCatagorizedLights(
    const std::vector<RE::NiPointer<RE::BSLight>>& lights)
{
    LightGroupData out;

    // --- group indices by config path -----------------------------------
    for (int i = 0; i < static_cast<int>(lights.size()); ++i)
    {
        auto& light = lights[i];
        if (!light || !light->light)
            continue;

        auto configID = light->light->GetLightRuntimeData().unk138;
        auto it = LightData::configIDToJsonCfg.find(configID);
        if (it == LightData::configIDToJsonCfg.end())
            continue;

        const std::string& path =
            it->second.configPath.empty() ? "Unknown Config"
            : it->second.configPath;

        out.byConfigPath[path].push_back(i);
    }

    // --- count duplicate menu names -------------------------------------
    for (auto& [configPath, indices] : out.byConfigPath)
    {
        if (indices.empty())
            continue;



        auto& firstLight = lights[indices.front()];
        if (!firstLight || !firstLight->light)
            continue;

        auto configID = firstLight->light->GetLightRuntimeData().unk138;
        auto it = LightData::configIDToJsonCfg.find(configID);
        if (it == LightData::configIDToJsonCfg.end())
            continue;

        out.menuNameCounts[ResolveMenuName(it->second)]++;
    }

    // --- sort config paths by resolved display name ---------------------
    for (auto& [path, _] : out.byConfigPath)
        out.sortedConfigPaths.push_back(path);

    std::sort(
        out.sortedConfigPaths.begin(),
        out.sortedConfigPaths.end(),
        [&](const std::string& a, const std::string& b)
        {
            auto ResolveName = [&](const std::string& configPath) -> std::string
                {
                    auto& indices = out.byConfigPath[configPath];
                    if (indices.empty())
                        return {};

                    auto& light = lights[indices.front()];
                    if (!light || !light->light)
                        return {};

                    auto configID = light->light->GetLightRuntimeData().unk138;
                    auto it = LightData::configIDToJsonCfg.find(configID);
                    if (it == LightData::configIDToJsonCfg.end())
                        return {};

                    return ResolveMenuName(it->second);
                };

            return compareLightNames(
                ResolveName(a).c_str(),
                ResolveName(b).c_str());
        });

    // --- bucket into categories -----------------------------------------
    for (const auto& configPath : out.sortedConfigPaths)
    {
        auto& indices = out.byConfigPath[configPath];
        if (indices.empty())
            continue;

        auto& light = lights[indices.front()];
        if (!light || !light->light)
            continue;

        auto configID = light->light->GetLightRuntimeData().unk138;
        auto it = LightData::configIDToJsonCfg.find(configID);
        if (it == LightData::configIDToJsonCfg.end())
            continue;

        const std::string& category = it->second.menuCategory;
        if (category.empty())
            out.uncategorized.push_back(configPath);
        else
            out.byCategory[category].push_back(configPath);
    }

    return out;
}

inline void DrawCatagoryGroup(
    const std::string& configPath,
    const LightGroupData& groupData,
    const std::vector<RE::NiPointer<RE::BSLight>>& lights,
    int& selectedIndex)
{
    auto pathIt = groupData.byConfigPath.find(configPath);
    if (pathIt == groupData.byConfigPath.end() || pathIt->second.empty())
        return;

    const auto& indices = pathIt->second;
    int          firstIdx = indices.front();

    auto& firstLight = lights[firstIdx];
    if (!firstLight || !firstLight->light)
        return;

    auto firstConfigID = firstLight->light->GetLightRuntimeData().unk138;
    auto cfgIt = LightData::configIDToJsonCfg.find(firstConfigID);
    if (cfgIt == LightData::configIDToJsonCfg.end())
        return;

    // Resolve the group's display name, disambiguating with mesh path when
    // multiple groups share the same menu name.
    std::string groupMenuName = ResolveMenuName(cfgIt->second);
    if (groupMenuName == "Unknown Light" && cfgIt->second.meshPaths.empty())
        groupMenuName = "Unknown Light";

    auto nameIt = groupData.menuNameCounts.find(groupMenuName);
    if (nameIt != groupData.menuNameCounts.end() &&
        nameIt->second > 1 &&
        !cfgIt->second.meshPaths.empty())
    {
        groupMenuName += " (" + cfgIt->second.meshPaths[0] + ")";
    }

    if (cfgIt->second.isPluginLight)
    {
        ImGuiMCP::PushID(configPath.c_str());

        for (int i : indices)
        {
            auto& light = lights[i];

            if (!light || !light->light)
                continue;

            auto configID =
                light->light->GetLightRuntimeData().unk138;

            auto it =
                LightData::configIDToJsonCfg.find(configID);

            if (it == LightData::configIDToJsonCfg.end())
                continue;

            std::string itemName =
                ResolveMenuName(it->second);

            ImGuiMCP::PushID(i);

            if (ImGuiMCP::Selectable(
                itemName.c_str(),
                i == selectedIndex))
            {
                selectedIndex = i;
            }

            ImGuiMCP::PopID();
        }

        ImGuiMCP::PopID();

        return;
    }

    const bool multiEntry =
        CountJsonEntriesInFile(cfgIt->second.configPath) > 1;

    if (multiEntry)
    {
        // Collapsible group showing each light individually
        std::string header =
            groupMenuName + " (" + std::to_string(indices.size()) + ")";

        ImGuiMCP::PushID(configPath.c_str());

        if (ImGuiMCP::TreeNode(header.c_str()))
        {
            for (int i : indices)
            {
                auto& light = lights[i];
                if (!light || !light->light)
                    continue;

                auto configID = light->light->GetLightRuntimeData().unk138;
                auto it = LightData::configIDToJsonCfg.find(configID);

                std::string itemName = (it != LightData::configIDToJsonCfg.end())
                    ? ResolveMenuName(it->second)
                    : "Unknown Light";

                if (it != LightData::configIDToJsonCfg.end())
                {
                    auto countIt = groupData.menuNameCounts.find(itemName);
                    if (countIt != groupData.menuNameCounts.end() &&
                        countIt->second > 1 &&
                        !it->second.meshPaths.empty())
                    {
                        itemName += " (" + it->second.meshPaths[0] + ")";
                    }
                }

                ImGuiMCP::PushID(i);
                if (ImGuiMCP::Selectable(itemName.c_str(), i == selectedIndex))
                    selectedIndex = i;
                ImGuiMCP::PopID();
            }

            ImGuiMCP::TreePop();
        }

        ImGuiMCP::PopID();
    }
    else
    {
        // Single entry � just a flat selectable
        ImGuiMCP::PushID(firstIdx);
        if (ImGuiMCP::Selectable(groupMenuName.c_str(), firstIdx == selectedIndex))
            selectedIndex = firstIdx;
        ImGuiMCP::PopID();
    }
}

inline void RenderLightList(
    const std::vector<RE::NiPointer<RE::BSLight>>& lights,
    int& selectedIndex,
    const char* headerLabel)
{
    if (!ImGuiMCP::CollapsingHeader(headerLabel))
        return;

    ImGuiMCP::PushID(headerLabel);

    LightGroupData groupData = BuildCatagorizedLights(lights);

    constexpr float maxListHeight = 450.0f;
    constexpr float categoryHeight = 24.0f;
    constexpr float itemHeight = 24.0f;

    float desiredHeight = 0.0f;
    desiredHeight += static_cast<float>(groupData.byCategory.size()) * categoryHeight;
    desiredHeight += static_cast<float>(groupData.uncategorized.size()) * itemHeight;

    constexpr float minListHeight = 175.0f;
    desiredHeight = std::clamp(desiredHeight, minListHeight, maxListHeight);

    if (ImGuiMCP::BeginChild("LightListChild", ImGuiMCP::ImVec2(0, desiredHeight), true))
    {
        std::vector<std::string> sortedCategories;
        sortedCategories.reserve(groupData.byCategory.size());
        for (auto& [cat, _] : groupData.byCategory)
            sortedCategories.push_back(cat);

        std::sort(sortedCategories.begin(), sortedCategories.end(),
            [](const std::string& a, const std::string& b)
            {
                return compareLightNames(a.c_str(), b.c_str());
            });

        for (const auto& category : sortedCategories)
        {
            // All light indices in this group. For Plugin Lights one
            // menuCategory is exactly one plugin file (e.g. "Skyrim.esm"):
            // CreateConfigFromPluginLight() sets menuCategory = modName.
            std::vector<int> categoryIndices;
            for (const auto& configPath : groupData.byCategory.at(category)) {
                auto pathIt = groupData.byConfigPath.find(configPath);
                if (pathIt != groupData.byConfigPath.end())
                    categoryIndices.insert(categoryIndices.end(), pathIt->second.begin(), pathIt->second.end());
            }

            bool groupOpen = ImGuiMCP::TreeNode(category.c_str());

            // Bulk buttons on the group header row itself, scoped to THIS
            // group only - never to the whole Plugin Lights list.
            // "##<category>" keeps the visible label short ("Default All" /
            // "Save All") while giving every group's buttons a unique ID.
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button((std::string("Default All##") + category).c_str())) {
                int okCount = 0;
                int failCount = 0;
                for (int idx : categoryIndices) {
                    if (idx < 0 || idx >= static_cast<int>(lights.size())) {
                        ++failCount;
                        continue;
                    }
                    if (RestoreSingleLightToDefaults(lights[idx]))
                        ++okCount;
                    else
                        ++failCount;
                }
                groupBulkResult[category] = std::format("Reset {}/{} ({} failed)", okCount, static_cast<int>(categoryIndices.size()), failCount);
                logger::info("Default All for '{}': reset {}/{} lights ({} failed)", category, okCount, static_cast<int>(categoryIndices.size()), failCount);
            }
            if (ImGuiMCP::IsItemHovered()) {
                std::string tip = std::format("Reset all {} lights in '{}' to defaults (in-memory only - use Save All to write them to disk)", static_cast<int>(categoryIndices.size()), category);
                ImGuiMCP::SetTooltip("%s", tip.c_str());
            }

            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button((std::string("Save All##") + category).c_str())) {
                int okCount = 0;
                int failCount = 0;
                for (int idx : categoryIndices) {
                    if (idx < 0 || idx >= static_cast<int>(lights.size())) {
                        ++failCount;
                        continue;
                    }
                    if (SaveSingleLight(lights[idx]))
                        ++okCount;
                    else
                        ++failCount;
                }
                groupBulkResult[category] = std::format("Saved {}/{} ({} failed)", okCount, static_cast<int>(categoryIndices.size()), failCount);
                logger::info("Save All for '{}': saved {}/{} lights ({} failed)", category, okCount, static_cast<int>(categoryIndices.size()), failCount);
            }
            if (ImGuiMCP::IsItemHovered()) {
                std::string tip = std::format("Save all {} lights in '{}' to their JSON configs", static_cast<int>(categoryIndices.size()), category);
                ImGuiMCP::SetTooltip("%s", tip.c_str());
            }

            auto resultIt = groupBulkResult.find(category);
            if (resultIt != groupBulkResult.end()) {
                ImGuiMCP::SameLine();
                ImGuiMCP::Text("%s", resultIt->second.c_str());
            }

            if (groupOpen)
            {
                for (const auto& configPath : groupData.byCategory.at(category))
                {
                    DrawCatagoryGroup(configPath, groupData, lights, selectedIndex);
                }
                ImGuiMCP::TreePop();
            }
        }

        for (const auto& configPath : groupData.uncategorized)
        {
            DrawCatagoryGroup(configPath, groupData, lights, selectedIndex);
        }
    }
    ImGuiMCP::EndChild();

    ImGuiMCP::PopID();
}

struct ActiveLightSelection {
    std::vector<RE::NiPointer<RE::BSLight>>* list = nullptr;
    int* index = nullptr;

    bool valid() const {
        return list && index && *index >= 0 && *index < (int)list->size();
    }

    RE::NiPointer<RE::BSLight> get() const {
        return valid() ? (*list)[*index] : RE::NiPointer<RE::BSLight>{};
    }
};

inline ActiveLightSelection ResolveActiveSelection(
    std::vector<RE::NiPointer<RE::BSLight>>& relightLights, int& relightSelectedIndex,
    std::vector<RE::NiPointer<RE::BSLight>>& pluginLights, int& pluginSelectedIndex)
{
    if (relightSelectedIndex >= 0 && relightSelectedIndex < (int)relightLights.size())
        return { &relightLights, &relightSelectedIndex };

    if (pluginSelectedIndex >= 0 && pluginSelectedIndex < (int)pluginLights.size())
        return { &pluginLights, &pluginSelectedIndex };

    return {};
}

inline void RenderRelightFlags(uint32_t& flags)
{
    static bool showFlagWindow = false;

    if (ImGuiMCP::Button((std::string("Flags ") + flagIcon).c_str())) {
        showFlagWindow = !showFlagWindow;
    }

    if (showFlagWindow) {
        ImGuiMCP::Begin(
            "Flag Window",
            &showFlagWindow,
            ImGuiMCP::ImGuiWindowFlags_::ImGuiWindowFlags_None
        );

        auto FlagCheckbox = [](const char* label, uint32_t& flags, RELIGHT_FLAGS flag, const char* tooltip)
            {
                bool checked = (flags & static_cast<uint32_t>(flag)) != 0;

                if (ImGuiMCP::Checkbox(label, &checked)) {
                    if (checked) {
                        flags |= static_cast<uint32_t>(flag);
                    }
                    else {
                        flags &= ~static_cast<uint32_t>(flag);
                    }
                }

                if (ImGuiMCP::IsItemHovered()) {
                    ImGuiMCP::SetTooltip("%s", tooltip);
                }
            };

        FlagCheckbox("Candle", flags, RELIGHT_FLAGS::kCandle,
            "Important for light flicker prevention. Also allows the light to merge with any other "
            "light created from a relight json file which also has the Candle flag.");

        FlagCheckbox("Chandelier", flags, RELIGHT_FLAGS::kChandelier,
            "Important for light flicker prevention.");

        FlagCheckbox("Fire", flags, RELIGHT_FLAGS::kFire,
            "Important for light flicker prevention. Also allows the light to merge with any other "
            "light created from a relight json file which also has the Fire flag.");

        FlagCheckbox("Giant Campfire", flags, RELIGHT_FLAGS::kGiantCampfire,
            "Allows merging with any other light created by a relight json config with the Fire flag (needed).");

        FlagCheckbox("Other", flags, RELIGHT_FLAGS::kOther,
            "Allows merging with any other light created by a relight json config with the Other flag.");

        FlagCheckbox("Increased Merge Distance", flags, RELIGHT_FLAGS::kIncreasedMergeDistance,
            "Grants a significantly larger merge distance to lights. Currently used for ruin candles.");

        FlagCheckbox("Increased Menu XYZ Scale", flags, RELIGHT_FLAGS::kIncreasedMenuXYZScale,
            "Increases the in-game menu XYZ position slider range from 250 to 1250, for positioning "
            "lights on larger objects.");

        FlagCheckbox("No Merging", flags, RELIGHT_FLAGS::kNoMerging,
            "Light sources of this type will never merge.");

        FlagCheckbox("Outdoor", flags, RELIGHT_FLAGS::kOutdoor,
            "Light source is to be applied outdoors only. Lets you have interior/exterior lighting "
            "separated -- e.g. an Outdoor-flagged lantern vs a regular one with no flags.");

        FlagCheckbox("Pulse", flags, RELIGHT_FLAGS::kPulse,
            "Light will pulse instead of flicker, good for flower lights ect.");

        if (globals::islInstalled) {
            // --- Extended Flags ---
            ImGuiMCP::SeparatorText("Community Shaders Flags");

            FlagCheckbox(
                "Inverse Square",
                flags,
                RELIGHT_FLAGS::kInverseSquare,
                "Uses inverse square falloff for attenuation, must refresh light for changes to take effect."
            );

            FlagCheckbox(
                "Linear",
                flags,
                RELIGHT_FLAGS::kLinear,
                "Applies linear lighting flag, must refresh light for changes to take effect"
            );
        }

        ImGuiMCP::End();
    }
}

inline void RenderTESLightFlags(std::uint32_t& flags)
{
    static bool showFlagWindow = false;

    if (ImGuiMCP::Button((std::string("Flags ") + flagIcon).c_str())) {
        showFlagWindow = !showFlagWindow;
    }

    if (showFlagWindow) {
        ImGuiMCP::Begin(
            "TES Light Flags",
            &showFlagWindow,
            ImGuiMCP::ImGuiWindowFlags_::ImGuiWindowFlags_None
        );

        auto FlagCheckbox =
            [](const char* label,
                std::uint32_t& flags,
                uint32_t flag,
                const char* tooltip)
            {
                const auto flagValue = static_cast<std::uint32_t>(flag);
                bool checked = (flags & flagValue) != 0;

                if (ImGuiMCP::Checkbox(label, &checked)) {
                    if (checked) {
                        flags |= flagValue;
                    }
                    else {
                        flags &= ~flagValue;
                    }
                }

                if (ImGuiMCP::IsItemHovered()) {
                    ImGuiMCP::SetTooltip("%s", tooltip);
                }
            };

        FlagCheckbox(
            "Dynamic",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic),
            "Light is dynamic."
        );

        FlagCheckbox(
            "Can Carry",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kCanCarry),
            "Light can be carried."
        );

        FlagCheckbox(
            "Negative",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative),
            "Light has negative lighting."
        );

        FlagCheckbox(
            "Deep Copy",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDeepCopy),
            "Light data is deep copied."
        );

        FlagCheckbox(
            "Off By Default",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOffByDefault),
            "Light is off by default."
        );

        ImGuiMCP::BeginDisabled(true);

        FlagCheckbox(
            "Flicker",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker),
            "Light flickers."
        );

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "Flicker is controlled via the flicker settings menu"
            );
        }

        FlagCheckbox(
            "Flicker Slow",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow),
            "Light uses the slow flicker behavior."
        );

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "Flicker is controlled via the flicker settings menu"
            );
        }

        FlagCheckbox(
            "Pulse",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse),
            "Light pulses instead of flickering."
        );

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "Flicker is controlled via the flicker settings menu"
            );
        }

        FlagCheckbox(
            "Pulse Slow",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow),
            "Light uses the slow pulse behavior."
        );

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "Flicker is controlled via the flicker settings menu"
            );
        }

        ImGuiMCP::EndDisabled();

        FlagCheckbox(
            "Spotlight",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kSpotlight),
            "Light is treated as a spotlight."
        );

        FlagCheckbox(
            "Spot Shadow",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kSpotShadow),
            "Light casts a spotlight shadow."
        );

        FlagCheckbox(
            "Hemi Shadow",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow),
            "Light uses hemispherical shadowing."
        );

        FlagCheckbox(
            "Omni Shadow",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow),
            "Light casts an omnidirectional shadow."
        );

        FlagCheckbox(
            "Portal Strict",
            flags,
            static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict),
            "Light uses strict portal behavior this should be on for shadow lights, i personally have it on for every light."
        );

        if (globals::islInstalled) {
            // --- Extended Flags ---
            ImGuiMCP::SeparatorText("Community Shaders Flags");

            FlagCheckbox(
                "Inverse Square",
                flags,
                static_cast<uint32_t>(TES_LIGHT_FLAGS_EXT::kInverseSquare),
                "Uses inverse square falloff for attenuation"
            );

            FlagCheckbox(
                "Linear",
                flags,
                static_cast<uint32_t>(TES_LIGHT_FLAGS_EXT::kLinear),
                "Applies Linear Lighting flag ."
            );
        }
        ImGuiMCP::End();
    }
}

inline std::vector<std::string> GetAllConfigKeys()
{
    std::vector<std::string> result;
    result.reserve(256);

    std::unordered_set<std::string> seen;

    auto collect = [&](const auto& map)
        {
            for (const auto& [key, _] : map) {
                if (seen.insert(key).second) {
                    result.push_back(key);
                }
            }
        };

    collect(LightData::meshPathToJsonCfg);
    collect(LightData::meshPathToJsonCfgExteriors);

    return result;
}


inline void BuildRegionList(std::vector<std::pair<std::string, RE::TESRegion*>>& out)
{
    out.clear();

    auto& regions = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESRegion>();

    for (auto* region : regions) {
        if (!region) {
            continue;
        }

        auto editorID = clib_util::editorID::get_editorID(region);
        if (editorID.empty()) {
            continue;
        }

        // filter: only FX* or Weather*
        if (!editorID.starts_with("FX") &&
            !editorID.starts_with("Weather")) {
            continue;
        }

        out.emplace_back(editorID, region);
    }

    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
}