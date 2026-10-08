#include "ProjectSettingsWindow.h"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#include "../3d/Camera.h"
#include "../base/ProjectPath.h"
#include "../components/VolumeComponent.h"
#include "../postprocess/PostProcess.h"
#include "../postprocess/VolumeStack.h"
#include "../runtime/InputActionSystem.h"
#include "../runtime/SceneManager.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "EditorApplication.h"
#include "EditorConsole.h"
#include "EditorSelection.h"
#include "InputActionEditing.h"
#include "SceneEditing.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#endif // USE_IMGUI

namespace KujataEngine {

namespace {

struct PageEntry {
	ProjectSettingsWindow::Page page;
	const char* label;
	// CUI の window.show で使う名前。
	const char* key;
};

constexpr PageEntry kPages[] = {
    {ProjectSettingsWindow::Page::InputActions, "Input Actions", "InputActions"},
    {ProjectSettingsWindow::Page::Scenes, "Scenes", "Scenes"},
    {ProjectSettingsWindow::Page::Rendering, "Rendering", "Rendering"},
};

#ifdef USE_IMGUI

constexpr const char* kTypeLabels[] = {"Button", "Axis 1D", "Axis 2D"};
constexpr InputActionType kTypes[] = {InputActionType::Button, InputActionType::Axis1D, InputActionType::Axis2D};

const char* GetTypeLabel(InputActionType type) {
	for (size_t i = 0; i < std::size(kTypes); ++i) {
		if (kTypes[i] == type) {
			return kTypeLabels[i];
		}
	}
	return kTypeLabels[0];
}

// 失敗したときだけ Console に出す(成功のたびに出すとログが埋まる)。
void Report(const InputActionEditing::Result& result) {
	if (!result.succeeded) {
		EditorConsole::GetInstance()->AddLog("[InputAction] " + result.message, EditorLogLevel::Warning);
	}
}

// 割り当ての名前の一覧から1つ選ぶコンボ。選び直したら true。
bool NameCombo(const char* id, const std::string& current, const std::vector<std::string>& names, std::string& outSelected) {
	bool changed = false;
	if (ImGui::BeginCombo(id, InputActionAsset::GetBindingLabel(current).c_str(), ImGuiComboFlags_HeightLarge)) {
		for (const std::string& name : names) {
			const bool selected = (name == current);
			if (ImGui::Selectable(InputActionAsset::GetBindingLabel(name).c_str(), selected) && !selected) {
				outSelected = name;
				changed = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

// まだ付けていない割り当てのうち、選択肢の先頭。
std::string FindUnusedBinding(const InputActionDef& action) {
	for (const std::string& name : InputActionAsset::GetBindingNames(action.type)) {
		if (std::find(action.bindings.begin(), action.bindings.end(), name) == action.bindings.end()) {
			return name;
		}
	}
	return {};
}

// 割り当て1つの行。軸1D の "A:D" はキーを2つ選ぶ。
void DrawBindingRow(const InputActionDef& action, size_t index) {
	const std::string& binding = action.bindings[index];
	ImGui::PushID(static_cast<int>(index));

	const float removeWidth = ImGui::GetFrameHeight();
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float width = ImGui::GetContentRegionAvail().x - removeWidth - spacing;

	std::string selected;
	const size_t separator = binding.find(':');
	if (action.type == InputActionType::Axis1D && separator != std::string::npos) {
		const std::vector<std::string> buttons = InputActionAsset::GetBindingNames(InputActionType::Button);
		const std::string negative = binding.substr(0, separator);
		const std::string positive = binding.substr(separator + 1);
		const float half = (width - spacing) * 0.5f;
		ImGui::SetNextItemWidth(half);
		if (NameCombo("##Negative", negative, buttons, selected)) {
			Report(InputActionEditing::SetBinding(action.name, index, selected + ":" + positive));
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(half);
		if (NameCombo("##Positive", positive, buttons, selected)) {
			Report(InputActionEditing::SetBinding(action.name, index, negative + ":" + selected));
		}
	} else {
		ImGui::SetNextItemWidth(width);
		if (NameCombo("##Binding", binding, InputActionAsset::GetBindingNames(action.type), selected)) {
			Report(InputActionEditing::SetBinding(action.name, index, selected));
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("x", ImVec2(removeWidth, 0.0f))) {
		Report(InputActionEditing::RemoveBinding(action.name, binding));
	}
	ImGui::PopID();
}

#endif // USE_IMGUI

} // namespace

bool ProjectSettingsWindow::TryParsePage(const std::string& name, Page& outPage) {
	for (const PageEntry& entry : kPages) {
		if (name == entry.key || name == entry.label) {
			outPage = entry.page;
			return true;
		}
	}
	return false;
}

void ProjectSettingsWindow::SetPage(Page page) { page_ = page; }

void ProjectSettingsWindow::Draw(bool* pOpen) {
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(760.0f, 480.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(GetWindowTitle(), pOpen)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginChild("Pages", ImVec2(150.0f, 0.0f), ImGuiChildFlags_Borders)) {
		for (const PageEntry& entry : kPages) {
			if (ImGui::Selectable(entry.label, page_ == entry.page)) {
				page_ = entry.page;
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	if (ImGui::BeginChild("Page", ImVec2(0.0f, 0.0f))) {
		switch (page_) {
		case Page::InputActions:
			DrawInputActionsPage();
			break;
		case Page::Scenes:
			DrawScenesPage();
			break;
		case Page::Rendering:
			DrawRenderingPage();
			break;
		}
	}
	ImGui::EndChild();

	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

void ProjectSettingsWindow::DrawInputActionsPage() {
#ifdef USE_IMGUI
	// 編集すると定義が置き換わるので、写しを回す。
	const std::vector<InputActionDef> actions = InputActionSystem::GetInstance()->GetDefinitions();

	for (size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex) {
		const InputActionDef& action = actions[actionIndex];
		ImGui::PushID(action.name.c_str());

		const bool open = ImGui::CollapsingHeader(action.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::BeginPopupContextItem()) {
			if (ImGui::MenuItem("Move Up", nullptr, false, actionIndex > 0)) {
				Report(InputActionEditing::MoveAction(action.name, -1));
			}
			if (ImGui::MenuItem("Move Down", nullptr, false, actionIndex + 1 < actions.size())) {
				Report(InputActionEditing::MoveAction(action.name, 1));
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Delete")) {
				Report(InputActionEditing::RemoveAction(action.name));
			}
			ImGui::EndPopup();
		}

		if (open) {
			ImGui::Indent();

			// 名前は入力を終えたときに変える(1文字ごとに保存しない)。入力中はメンバーのバッファを渡し続ける。
			if (renameTarget_ != action.name) {
				char nameBuffer[64] = {};
				strncpy_s(nameBuffer, action.name.c_str(), _TRUNCATE);
				ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
				if (ImGui::IsItemActivated()) {
					renameTarget_ = action.name;
					strncpy_s(renameBuffer_, action.name.c_str(), _TRUNCATE);
				}
			} else {
				ImGui::InputText("Name", renameBuffer_, sizeof(renameBuffer_));
				if (ImGui::IsItemDeactivated()) {
					renameTarget_.clear();
					if (action.name != renameBuffer_) {
						Report(InputActionEditing::RenameAction(action.name, renameBuffer_));
					}
				}
			}

			if (ImGui::BeginCombo("Type", GetTypeLabel(action.type))) {
				for (size_t i = 0; i < std::size(kTypes); ++i) {
					if (ImGui::Selectable(kTypeLabels[i], kTypes[i] == action.type) && kTypes[i] != action.type) {
						Report(InputActionEditing::SetType(action.name, kTypes[i]));
					}
				}
				ImGui::EndCombo();
			}

			ImGui::TextUnformatted("Bindings");
			for (size_t i = 0; i < action.bindings.size(); ++i) {
				DrawBindingRow(action, i);
			}

			if (action.type == InputActionType::Axis1D) {
				if (ImGui::Button("+ Trigger")) {
					Report(InputActionEditing::AddBinding(action.name, FindUnusedBinding(action)));
				}
				ImGui::SameLine();
				if (ImGui::Button("+ Keys (- / +)")) {
					Report(InputActionEditing::AddBinding(action.name, "A:D"));
				}
			} else if (ImGui::Button("+ Binding")) {
				Report(InputActionEditing::AddBinding(action.name, FindUnusedBinding(action)));
			}

			ImGui::Unindent();
			ImGui::Spacing();
		}
		ImGui::PopID();
	}

	ImGui::Separator();
	ImGui::SetNextItemWidth(200.0f);
	const bool entered = ImGui::InputTextWithHint("##NewAction", "New action name", newActionName_, sizeof(newActionName_), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if ((ImGui::Button("Add Action") || entered) && newActionName_[0] != '\0') {
		const InputActionEditing::Result result = InputActionEditing::AddAction(newActionName_, InputActionType::Button);
		Report(result);
		if (result.succeeded) {
			newActionName_[0] = '\0';
		}
	}
#endif // USE_IMGUI
}

void ProjectSettingsWindow::DrawScenesPage() {
#ifdef USE_IMGUI
	const std::string startupScene = EditorApplication::GetInstance()->GetStartupSceneName();

	// ラジオ = 起動シーン、名前 = 開く。
	for (const std::string& sceneName : SceneEditing::ListScenes()) {
		ImGui::PushID(sceneName.c_str());
		if (ImGui::RadioButton("##startup", sceneName == startupScene)) {
			EditorApplication::GetInstance()->SetStartupSceneName(sceneName);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Startup scene");
		}
		ImGui::SameLine();
		if (ImGui::Selectable(sceneName.c_str())) {
			ChangeScene(sceneName);
		}
		ImGui::PopID();
	}

	ImGui::Separator();
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##NewSceneName", "New scene name", newSceneName_, sizeof(newSceneName_));
	ImGui::SameLine();
	if (ImGui::Button("Create Scene") && newSceneName_[0] != '\0') {
		const std::string sceneName = newSceneName_;
		const SceneEditing::Result result = SceneEditing::CreateScene(sceneName);
		// 既にある名前なら、作らずにそのシーンを開く。
		if (result.succeeded || SceneEditing::Exists(sceneName)) {
			ChangeScene(sceneName);
			newSceneName_[0] = '\0';
		} else {
			EditorConsole::GetInstance()->AddLog(result.message, EditorLogLevel::Error);
		}
	}
#endif // USE_IMGUI
}

void ProjectSettingsWindow::DrawRenderingPage() {
#ifdef USE_IMGUI
	// 今フレームに効いているポストエフェクトの確認用。値の編集はシーン上の Volume(Inspector)で行う。
	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		ImGui::TextDisabled("No scene");
		return;
	}

	// Scene ビューのカメラ位置で解決する(Local Volume の効き具合が見えるように)。
	Camera* sceneCamera = scene->GetSceneViewCamera();
	const Vector3 cameraPosition = sceneCamera ? sceneCamera->translation_ : Vector3{0.0f, 0.0f, 0.0f};
	const VolumeResolveResult resolved = VolumeStack::Resolve(*scene, cameraPosition);

	ImGui::SeparatorText("Volumes");
	if (resolved.contributors.empty()) {
		ImGui::TextDisabled("None");
		if (ImGui::Button("Create Global Volume")) {
			EditorSelection::GetInstance()->SetSelectedGameObject(CreateGlobalVolumeObject(*scene));
		}
	} else {
		for (VolumeComponent* volume : resolved.contributors) {
			GameObject* owner = volume->GetOwner();
			const char* name = owner ? owner->GetName().c_str() : "(no owner)";
			const float weight = VolumeStack::ComputeWeight(*volume, cameraPosition);
			ImGui::BulletText("%s  [%s]  priority=%.1f  weight=%.2f", name, volume->IsGlobal() ? "Global" : "Local", volume->GetPriority(), weight);
		}
	}

	ImGui::SeparatorText("Resolved");
	// 解決結果の写しなので、編集してもどこにも反映されない。常に Disabled で描く。
	VolumeProfileData preview = resolved.profile;
	for (const VolumeEffectDescriptor& descriptor : GetVolumeEffectDescriptors()) {
		ImGui::PushID(descriptor.name);
		const bool overriding = preview.IsOverriding(descriptor.id);
		// 表示名が変わっても開閉状態を保つよう、ID は ### の後ろで固定する。
		const std::string header = std::string(descriptor.name) + (overriding ? "" : "  (default)") + "###Effect";
		if (ImGui::CollapsingHeader(header.c_str())) {
			ImGui::Indent();
			ImGui::BeginDisabled(true);
			descriptor.drawInspector(preview);
			ImGui::EndDisabled();
			ImGui::Unindent();
		}
		ImGui::PopID();
	}

	PostProcess* postProcess = PostProcess::GetInstance();
	if (postProcess->GetFadeAmount() > 0.0f) {
		ImGui::Separator();
		ImGui::Text("Fade: %.2f", postProcess->GetFadeAmount());
	}
#endif // USE_IMGUI
}

} // namespace KujataEngine
