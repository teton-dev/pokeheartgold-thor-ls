// libntr's developer windows > Application (SIM::GUI::PrjMain, weak in libntr): the game's debug window, as the
// Platinum fork's (src/port/sim_gui_prj.cpp, gui_mapjump.cpp) with HGSS's places and more: warps to the towns or any
// map, the trainers on this map (go to one, battle it, mark it beaten or not), a battle against any trainer by number
// or name, a wild battle. The game logic does the work (port_debug_field.h); this is the ImGui window.
#include <nitro/types.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "port_debug_field.h"
#include "port_save_editor.h"

namespace {

int sMapId = 0;
int sX = 0;
int sZ = 0;
int sTrainerId = 1;
char sTrainerFilter[32] = "";
int sSpecies = 25;
int sLevel = 5;
const char *sResult = nullptr; // the last button's outcome

void Done(BOOL ok, const char *what) {
    sResult = ok ? what : "Not now: the player isn't free on the overworld.";
}

bool Contains(const char *text, const char *part) {
    if (part[0] == '\0') {
        return true;
    }
    for (const char *t = text; *t != '\0'; t++) {
        const char *a = t, *b = part;
        while (*a != '\0' && *b != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

void DrawWarps(bool free) {
    ImGui::BeginDisabled(!free);
    const float width = ImGui::GetContentRegionAvail().x;
    const int perRow = width > 480.0f ? 3 : 2;
    const ImVec2 size((width - ImGui::GetStyle().ItemSpacing.x * (perRow - 1)) / perRow, 0.0f);
    for (int town = 0; town < DebugField_TownCount(); town++) {
        if (town % perRow != 0) {
            ImGui::SameLine();
        }
        if (ImGui::Button(DebugField_TownName(town), size)) {
            Done(DebugField_WarpToTown(town), "Warping.");
        }
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Map", &sMapId, 1, 10);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("X", &sX, 1, 8);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Z", &sZ, 1, 8);
    if (ImGui::Button("Go there")) {
        Done(DebugField_WarpTo(sMapId, sX, sZ), "Warping.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Here")) {
        sMapId = DebugField_MapId();
        DebugField_Position(&sX, &sZ);
    }
    ImGui::EndDisabled();
}

void DrawMapTrainers(bool free) {
    static DebugFieldTrainer trainers[64];
    const int count = DebugField_MapTrainers(trainers, 64);
    if (count == 0) {
        ImGui::TextDisabled("No trainers on this map.");
        return;
    }
    if (!ImGui::BeginTable("##maptrainers", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        return;
    }
    ImGui::TableSetupColumn("Trainer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Tile");
    ImGui::TableSetupColumn("Beaten");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (int i = 0; i < count; i++) {
        const DebugFieldTrainer &trainer = trainers[i];
        ImGui::PushID(trainer.objectId);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s%s (#%d)", trainer.name, trainer.isDouble ? ", double" : "", trainer.trainerId);
        ImGui::TableNextColumn();
        ImGui::Text("%d, %d", trainer.x, trainer.z);
        ImGui::TableNextColumn();
        bool beaten = trainer.beaten != FALSE;
        if (ImGui::Checkbox("##beaten", &beaten)) {
            DebugField_SetTrainerBeaten(trainer.trainerId, beaten);
        }
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!free);
        if (ImGui::SmallButton("Go to")) {
            Done(DebugField_GoToTrainer(trainer.objectId), "Warping in front of the trainer.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Battle")) {
            Done(DebugField_BattleTrainer(trainer.trainerId), "Battle starting.");
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Beaten trainers don't challenge you; untick one to battle it again the game's way.");
    ImGui::PopStyleColor();
}

void DrawAnyTrainer(bool free) {
    const int count = DebugField_TrainerCount();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputInt("Trainer number", &sTrainerId, 1, 10)) {
        sTrainerId = sTrainerId < 1 ? 1 : (sTrainerId >= count ? count - 1 : sTrainerId);
    }
    ImGui::Text("%s", DebugField_TrainerName(sTrainerId));
    bool beaten = DebugField_TrainerBeaten(sTrainerId) != FALSE;
    if (ImGui::Checkbox("Beaten", &beaten)) {
        DebugField_SetTrainerBeaten(sTrainerId, beaten);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!free);
    if (ImGui::Button("Battle this trainer")) {
        Done(DebugField_BattleTrainer(sTrainerId), "Battle starting.");
    }
    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Find by name", sTrainerFilter, sizeof(sTrainerFilter));
    if (sTrainerFilter[0] != '\0') {
        int shown = 0;
        for (int id = 1; id < count && shown < 40; id++) {
            const char *name = DebugField_TrainerName(id);
            if (!Contains(name, sTrainerFilter)) {
                continue;
            }
            char label[64];
            snprintf(label, sizeof(label), "#%d %s", id, name);
            if (ImGui::Selectable(label, id == sTrainerId)) {
                sTrainerId = id;
            }
            shown++;
        }
        if (shown == 0) {
            ImGui::TextDisabled("No trainer by that name.");
        }
    }
}

void DrawWild(bool free) {
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputInt("Species", &sSpecies, 1, 10)) {
        sSpecies = sSpecies < 1 ? 1 : (sSpecies > SAVE_ED_MAX_SPECIES ? SAVE_ED_MAX_SPECIES : sSpecies);
    }
    ImGui::SameLine();
    ImGui::Text("%s", SaveEd_SpeciesName(sSpecies));
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputInt("Level", &sLevel, 1, 10)) {
        sLevel = sLevel < 1 ? 1 : (sLevel > 100 ? 100 : sLevel);
    }
    ImGui::BeginDisabled(!free);
    if (ImGui::Button("Wild battle")) {
        Done(DebugField_WildBattle(sSpecies, sLevel), "Battle starting.");
    }
    ImGui::EndDisabled();
}

} // namespace

namespace SIM::GUI {

void PrjMain(bool *openState) {
    // Fits the window, portrait (the two screens stacked) or landscape
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float width = viewport->WorkSize.x - 16.0f < 520.0f ? viewport->WorkSize.x - 16.0f : 520.0f;
    const float height = viewport->WorkSize.y - 16.0f < 560.0f ? viewport->WorkSize.y - 16.0f : 560.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 8.0f, viewport->WorkPos.y + 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    ImGui::Begin("pokeheartgold", openState);
    ImGui::Text("Git rev: %s", SIM_GetProjectGitHash());

    const char *why = DebugField_Unavailable();
    const bool free = why == nullptr;
    if (DebugField_MapId() >= 0) {
        int x, z;
        DebugField_Position(&x, &z);
        ImGui::Text("%s (map %d) at %d, %d", DebugField_MapName(), DebugField_MapId(), x, z);
    }
    if (why != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", why);
        ImGui::PopStyleColor();
    } else if (sResult != nullptr) {
        ImGui::TextDisabled("%s", sResult);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Battles run in Black 2 and White 2's engine when Settings > Gen 5 battles is on.");
    ImGui::PopStyleColor();

    if (ImGui::CollapsingHeader("Warp")) {
        DrawWarps(free);
    }
    if (ImGui::CollapsingHeader("Trainers on this map", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawMapTrainers(free);
    }
    if (ImGui::CollapsingHeader("Battle any trainer")) {
        DrawAnyTrainer(free);
    }
    if (ImGui::CollapsingHeader("Wild battle")) {
        DrawWild(free);
    }
    ImGui::End();
}

} // namespace SIM::GUI
