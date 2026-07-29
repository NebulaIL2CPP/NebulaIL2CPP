#include "Nebula/UI/SampleMenu.h"

#include <array>
#include <cmath>
#include <string>

#include <imgui.h>

#include "Nebula/Config/Config.h"

namespace {

void DrawBasicControls() {
    static bool enabled = true;
    static bool optionA = true;
    static bool optionB = false;
    static int radio = 0;

    ImGui::SeparatorText("Text and buttons");
    ImGui::Text("Normal text");
    ImGui::TextColored(
        ImVec4(0.25F, 0.85F, 1.0F, 1.0F), "Colored text");
    ImGui::TextWrapped(
        "This page does not depend on any game class. If you can see it, "
        "the EGL hook and Dear ImGui renderer are working.");
    ImGui::BulletText("ARM64 inline hook");
    ImGui::BulletText("OpenGL ES 3 renderer");

    if (ImGui::Button("Button", ImVec2(150.0F, 0.0F))) {
        ImGui::OpenPopup("ButtonPopup");
    }
    ImGui::SameLine();
    ImGui::SmallButton("Small button");
    ImGui::SameLine();
    ImGui::ArrowButton("Arrow", ImGuiDir_Right);

    if (ImGui::BeginPopup("ButtonPopup")) {
        ImGui::Text("Button clicked");
        ImGui::Separator();
        if (ImGui::Selectable("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Selection");
    ImGui::Checkbox("Enabled", &enabled);
    ImGui::Checkbox("Option A", &optionA);
    ImGui::SameLine();
    ImGui::Checkbox("Option B", &optionB);
    ImGui::RadioButton("Mode 1", &radio, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Mode 2", &radio, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Mode 3", &radio, 2);
}

void DrawValueControls() {
    static int integerValue = 50;
    static float floatValue = 0.5F;
    static float angle = 45.0F;
    static int dragInteger = 10;
    static float dragFloat = 1.0F;
    static std::array<char, 128> text{"NebulaIL2CPP"};
    static std::array<float, 3> vector{1.0F, 2.0F, 3.0F};

    ImGui::SeparatorText("Sliders and drag controls");
    ImGui::SliderInt("Integer slider", &integerValue, 0, 100);
    ImGui::SliderFloat(
        "Float slider", &floatValue, 0.0F, 1.0F, "%.3f");
    ImGui::SliderAngle("Angle", &angle, -180.0F, 180.0F);
    ImGui::DragInt("Drag integer", &dragInteger, 1.0F, -100, 100);
    ImGui::DragFloat(
        "Drag float", &dragFloat, 0.01F, -10.0F, 10.0F, "%.2f");
    ImGui::DragFloat3("Vector 3", vector.data(), 0.05F);

    ImGui::SeparatorText("Input");
    ImGui::InputText("Text input", text.data(), text.size());
    ImGui::InputInt("Integer input", &integerValue);
    ImGui::InputFloat("Float input", &floatValue, 0.01F, 0.1F, "%.3f");

    const float progress =
        static_cast<float>(integerValue) / 100.0F;
    ImGui::ProgressBar(
        progress, ImVec2(-1.0F, 0.0F), "Progress");
}

void DrawListsAndColors() {
    static int comboIndex = 0;
    static int listIndex = 0;
    static ImVec4 color{0.18F, 0.62F, 0.95F, 1.0F};
    static const char* items[] = {
        "Player", "Weapon", "World", "Visual"};

    ImGui::SeparatorText("Lists");
    ImGui::Combo(
        "Combo", &comboIndex, items, IM_ARRAYSIZE(items));
    ImGui::ListBox(
        "List box", &listIndex, items, IM_ARRAYSIZE(items), 4);

    ImGui::SeparatorText("Color");
    ImGui::ColorEdit3("RGB", &color.x);
    ImGui::ColorEdit4("RGBA", &color.x);
    ImGui::ColorButton(
        "Color preview", color,
        ImGuiColorEditFlags_AlphaPreviewHalf,
        ImVec2(80.0F, 40.0F));
}

void DrawContainersAndPlots() {
    static bool nodes[3]{true, false, true};
    static float samples[90]{};
    static int sampleOffset = 0;
    static float phase = 0.0F;

    phase += ImGui::GetIO().DeltaTime * 2.0F;
    samples[sampleOffset] =
        0.5F + 0.45F * std::sin(phase);
    sampleOffset = (sampleOffset + 1) % IM_ARRAYSIZE(samples);

    if (ImGui::CollapsingHeader(
            "Tree and child window", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNode("Feature tree")) {
            ImGui::Checkbox("Node A", &nodes[0]);
            ImGui::Checkbox("Node B", &nodes[1]);
            if (ImGui::TreeNode("Nested node")) {
                ImGui::Checkbox("Node C", &nodes[2]);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        ImGui::BeginChild(
            "SampleChild", ImVec2(0.0F, 120.0F),
            ImGuiChildFlags_Borders);
        ImGui::Text("Scrollable child region");
        for (int index = 0; index < 8; ++index) {
            ImGui::Selectable(
                ("Selectable item " + std::to_string(index + 1)).c_str());
        }
        ImGui::EndChild();
    }

    ImGui::SeparatorText("Plots");
    ImGui::PlotLines(
        "Line plot", samples, IM_ARRAYSIZE(samples), sampleOffset,
        nullptr, 0.0F, 1.0F, ImVec2(0.0F, 90.0F));
    ImGui::PlotHistogram(
        "Histogram", samples, IM_ARRAYSIZE(samples), sampleOffset,
        nullptr, 0.0F, 1.0F, ImVec2(0.0F, 90.0F));

    if (ImGui::BeginTable(
            "StatusTable", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Component");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Backend");
        ImGui::TableHeadersRow();

        const char* rows[][3] = {
            {"Overlay", "Ready", "OpenGL ES 3"},
            {"Hook", "Ready", "Dobby"},
            {"Config", "Ready", "JSON"}};
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            for (int column = 0; column < 3; ++column) {
                ImGui::TableSetColumnIndex(column);
                ImGui::TextUnformatted(row[column]);
            }
        }
        ImGui::EndTable();
    }
}

void DrawConfigControls() {
    static bool configBool =
        Nebula::Config::Get().GetBool("sample.bool", false);
    static int configInt =
        Nebula::Config::Get().GetInt("sample.int", 25);
    static float configFloat =
        Nebula::Config::Get().GetFloat("sample.float", 0.5F);
    static bool showModal = false;

    ImGui::Checkbox("Config bool", &configBool);
    ImGui::SliderInt("Config int", &configInt, 0, 100);
    ImGui::SliderFloat("Config float", &configFloat, 0.0F, 1.0F);

    if (ImGui::Button("Save JSON")) {
        auto& config = Nebula::Config::Get();
        config.SetBool("sample.bool", configBool);
        config.SetInt("sample.int", configInt);
        config.SetFloat("sample.float", configFloat);
        config.Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open modal")) {
        showModal = true;
        ImGui::OpenPopup("Nebula modal");
    }

    if (showModal &&
        ImGui::BeginPopupModal(
            "Nebula modal", &showModal,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Modal dialog is working.");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(160.0F, 0.0F))) {
            showModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::BeginDisabled();
    ImGui::Button("Disabled button");
    ImGui::EndDisabled();

    ImGui::TextDisabled("Touch or hold this line for a tooltip.");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Nebula tooltip");
    }
}

} // namespace

namespace Nebula {

void SampleMenu::Draw() {
    if (!ImGui::BeginTabItem("Controls")) {
        return;
    }

    if (ImGui::BeginTabBar("ControlGroups")) {
        if (ImGui::BeginTabItem("Basic")) {
            DrawBasicControls();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Values")) {
            DrawValueControls();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lists")) {
            DrawListsAndColors();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Layout")) {
            DrawContainersAndPlots();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Config")) {
            DrawConfigControls();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndTabItem();
}

} // namespace Nebula
