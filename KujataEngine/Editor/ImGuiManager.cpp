#include "ImGuiManager.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../../externals/ImGuizmo-1.9/src/ImGuizmo.h"
#include "../../externals/imsearch/imsearch.h"
#include "EditorApplication.h"
#include "EditorConsole.h"
#include "EditorImGuiUtil.h"
#include "EditorSelection.h"
#include "EditorStyle.h"
#include "EditorUndoManager.h"
#include "SceneJsonExporter.h"
#include "../base/DirectXCommon.h"
#include "../base/ProjectPath.h"
#include "../base/WinApp.h"
#include "../runtime/PlayState.h"
#include "../runtime/SceneManager.h"
#include "../scene/ComponentFactory.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace KujataEngine {
namespace {

void AllocateImGuiSrvDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {
	// ImGui 1.92系のDX12バックエンドは、フォントや追加テクスチャ用のSRVをバックエンド側へ要求してくる。
	// KujataEngine側のSRV採番関数を使うことで、Gameウィンドウ用SRVや通常テクスチャと番号が衝突しないようにする。
	DirectXCommon* dxCommon = static_cast<DirectXCommon*>(info->UserData);
	uint32_t srvIndex = dxCommon->AllocateSrvIndex();
	ID3D12DescriptorHeap* srvHeap = dxCommon->GetSrvDescriptorHeap();
	uint32_t descriptorSize = dxCommon->GetDescriptorSizeSRV();

	// CPUハンドルはCreateShaderResourceViewなど、CPU側からDescriptorを書き込む用途で使う。
	*outCpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
	outCpuHandle->ptr += descriptorSize * srvIndex;

	// GPUハンドルはシェーダーやImGui描画時にGPU側から参照する用途で使う。
	*outGpuHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
	outGpuHandle->ptr += descriptorSize * srvIndex;
}

void FreeImGuiSrvDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
	// 現状はSRVヒープをフレーム中に再利用しないため解放処理は不要。
}

// ウィンドウの表示状態をimgui.iniに書くときの名前と、EditorWindowVisibilityのメンバの対応。
// 名前はiniのキーになるので変えないこと(変えると保存済みの状態が読めなくなる)。
struct WindowVisibilityEntry {
	const char* name;
	bool EditorWindowVisibility::* flag;
};

constexpr WindowVisibilityEntry kWindowVisibilityEntries[] = {
    {"Scene", &EditorWindowVisibility::scene},
    {"Game", &EditorWindowVisibility::game},
    {"Hierarchy", &EditorWindowVisibility::hierarchy},
    {"Inspector", &EditorWindowVisibility::inspector},
    {"Project", &EditorWindowVisibility::project},
    {"Console", &EditorWindowVisibility::console},
    {"Performance", &EditorWindowVisibility::performance},
    {"Animation", &EditorWindowVisibility::animation},
    {"ProjectSettings", &EditorWindowVisibility::projectSettings},
};

constexpr const char* kWindowVisibilitySettingsType = "KujataEditor";
constexpr const char* kWindowVisibilitySettingsEntry = "Windows";

// iniの [KujataEditor][Windows] の見出しに来たとき、書き込み先(EditorWindowVisibility)を返す。
void* ReadWindowVisibilityOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
	if (std::strcmp(name, kWindowVisibilitySettingsEntry) != 0) {
		return nullptr;
	}
	return handler->UserData;
}

// "Scene=1" の形の1行を読む。知らないキーは無視する(ウィンドウを減らしても古いiniで落ちないように)。
void ReadWindowVisibilityLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
	EditorWindowVisibility* visibility = static_cast<EditorWindowVisibility*>(entry);
	const char* separator = std::strchr(line, '=');
	if (!visibility || !separator) {
		return;
	}
	const std::string key(line, separator);
	const bool value = std::atoi(separator + 1) != 0;
	for (const WindowVisibilityEntry& item : kWindowVisibilityEntries) {
		if (key == item.name) {
			visibility->*item.flag = value;
			return;
		}
	}
}

void WriteWindowVisibility(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* outBuffer) {
	const EditorWindowVisibility* visibility = static_cast<const EditorWindowVisibility*>(handler->UserData);
	outBuffer->appendf("[%s][%s]\n", handler->TypeName, kWindowVisibilitySettingsEntry);
	for (const WindowVisibilityEntry& item : kWindowVisibilityEntries) {
		outBuffer->appendf("%s=%d\n", item.name, (visibility->*item.flag) ? 1 : 0);
	}
	outBuffer->append("\n");
}

} // namespace

ImGuiManager* ImGuiManager::GetInstance() {
	static ImGuiManager instance;
	return &instance;
}

void ImGuiManager::Initialize() {
#ifdef USE_IMGUI

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	WinApp* winApp = WinApp::GetInstance();

	// ImGui初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// Dockingを有効化する。これによりDockSpaceや各ウィンドウのドッキングが使える。
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// キーボード操作を有効化して、エディタUIとして最低限扱いやすくする。
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// レイアウト(Dock配置・ウィンドウの位置と表示状態)はエンジンのフォルダのimgui.iniに保存する。
	// 既定の相対パスのままだと、起動したときのカレントフォルダごとに別のiniができて配置が戻らない。
	iniFilePath_ = (GetEngineRoot() / "imgui.ini").string();
	io.IniFilename = iniFilePath_.c_str();
	RegisterWindowVisibilitySettings();

	// フォントを既定より少し大きく、きれいなサンサリフ(Segoe UI)にする。
	// 日本語グリフは存在すれば別フォントからマージする。無ければImGui既定フォントにフォールバック。
	{
		const float fontSize = 18.0f;
		const char* latinFontPath = "C:/Windows/Fonts/segoeui.ttf";
		if (std::filesystem::exists(latinFontPath)) {
			io.Fonts->AddFontFromFileTTF(latinFontPath, fontSize);

			const char* japaneseFontPaths[] = {
			    "C:/Windows/Fonts/YuGothR.ttc",
			    "C:/Windows/Fonts/meiryo.ttc",
			    "C:/Windows/Fonts/msgothic.ttc",
			};
			for (const char* japaneseFontPath : japaneseFontPaths) {
				if (std::filesystem::exists(japaneseFontPath)) {
					ImFontConfig mergeConfig;
					mergeConfig.MergeMode = true;
					io.Fonts->AddFontFromFileTTF(japaneseFontPath, fontSize, &mergeConfig, io.Fonts->GetGlyphRangesJapanese());
					break;
				}
			}
		}
	}

	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
	// ImSearch(AddComponent等の検索)のコンテキストをImGuiコンテキスト生成直後に作る。
	ImSearch::CreateContext();

	EditorStyle::Apply();
	ImGui_ImplWin32_Init(winApp->GetHwnd());

	// 1.92系のDX12バックエンドはInitInfoで初期化する。
	// 旧式の初期化ではフォントアトラス用テクスチャが正しく作られず、assertの原因になる。
	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = dxCommon->GetDevice();
	initInfo.CommandQueue = dxCommon->GetCommandQueue();
	initInfo.NumFramesInFlight = dxCommon->GetSwapChainBufferCount();
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	initInfo.SrvDescriptorHeap = dxCommon->GetSrvDescriptorHeap();
	initInfo.UserData = dxCommon;
	initInfo.SrvDescriptorAllocFn = AllocateImGuiSrvDescriptor;
	initInfo.SrvDescriptorFreeFn = FreeImGuiSrvDescriptor;
	ImGui_ImplDX12_Init(&initInfo);

	// Dock配置はimgui.iniから復元する。保存が無いときだけ、最初のEditorDockSpace::Drawで初期配置を組む。
	ClearConsoleLogs();
	AddConsoleLog("Console initialized.");
#endif // USE_IMGUI
}

void ImGuiManager::AddConsoleLog(const std::string& message) { EditorConsole::GetInstance()->AddLog(message); }

void ImGuiManager::ClearConsoleLogs() { EditorConsole::GetInstance()->ClearLogs(); }

void ImGuiManager::HandleEditorShortcuts() {
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	//io.Fonts->AddFontFromFileTTF(
	//	"C:/Windows/Fonts/Arial.ttf", 
	//	24.0f, 
	//	nullptr, 
	//	io.Fonts->GetGlyphRangesJapanese()
	//);

	io.FontGlobalScale = 1.0f;

	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (scene && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		bool redo = io.KeyShift;
		if (redo) {
			if (EditorUndoManager::GetInstance()->Redo(*scene)) {
				AddConsoleLog("[Undo] Redo.");
			}
		} else {
			if (EditorUndoManager::GetInstance()->Undo(*scene)) {
				AddConsoleLog("[Undo] Undo.");
			}
		}
		return;
	}
	if (scene && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
		if (EditorUndoManager::GetInstance()->Redo(*scene)) {
			AddConsoleLog("[Undo] Redo.");
		}
		return;
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
		if (EditorApplication::GetInstance()->IsPrefabEditing()) {
			EditorApplication::GetInstance()->SavePrefabEditMode();
			return;
		}
		ExportCurrentSceneJson();
	}
	if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
		EditorApplication::GetInstance()->ReloadGameModule();
	}
#endif // USE_IMGUI
}

void ImGuiManager::ExportCurrentSceneJson() {
	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		AddConsoleLog("[Editor] Scene JSON export failed: No Scene.");
		return;
	}
	// Play 中のシーンは、動いた後の値と実行中に作ったオブジェクトを含むので保存しない(CUI の scene.save と同じ)。
	// Play 中の変更は Stop で Play 前に戻る。
	if (EditorApplication::GetInstance()->IsPlaying()) {
		EditorConsole::GetInstance()->AddLog("[Editor] Play 中は保存できません(Stop してから保存してください)。", EditorLogLevel::Warning);
		return;
	}

	// Project Windowの表示ルートはプロジェクトのフォルダだが、シーンJSONの読み書きはData配下が基準。
	// プロジェクトのフォルダを渡すと <プロジェクト>/SceneJson へ書き出してしまい、Importerが読まない場所に保存される。
	SceneJsonExporter::ExportResult exportResult = SceneJsonExporter::ExportScene(*scene, GetProjectDataRoot());
	if (exportResult.succeeded) {
		AddConsoleLog("[Editor] Scene JSON exported: " + exportResult.outputDirectory.string());
		projectWindow_.Refresh();
		return;
	}

	AddConsoleLog("[Editor] Scene JSON export failed: " + exportResult.message);
}

bool ImGuiManager::ShowWindow(const std::string& name, bool visible) {
	// Project Settings のページ名なら、そのページを開く。
	if (ProjectSettingsWindow::Page page; ProjectSettingsWindow::TryParsePage(name, page)) {
		if (visible) {
			projectSettingsWindow_.SetPage(page);
		}
		return ShowWindow("ProjectSettings", visible);
	}
	for (const WindowVisibilityEntry& item : kWindowVisibilityEntries) {
		if (name != item.name) {
			continue;
		}
		windowVisibility_.*item.flag = visible;
#ifdef USE_IMGUI
		if (visible) {
			// 同じノードに重なったタブの後ろにあっても、前に出して見えるようにする。
			ImGui::SetWindowFocus(item.flag == &EditorWindowVisibility::projectSettings ? ProjectSettingsWindow::GetWindowTitle() : item.name);
		}
#endif // USE_IMGUI
		return true;
	}
	return false;
}

std::vector<std::pair<std::string, bool>> ImGuiManager::GetWindowVisibilities() const {
	std::vector<std::pair<std::string, bool>> result;
	for (const WindowVisibilityEntry& item : kWindowVisibilityEntries) {
		result.emplace_back(item.name, windowVisibility_.*item.flag);
	}
	return result;
}

void ImGuiManager::RegisterWindowVisibilitySettings() {
#ifdef USE_IMGUI
	ImGuiSettingsHandler handler;
	handler.TypeName = kWindowVisibilitySettingsType;
	handler.TypeHash = ImHashStr(kWindowVisibilitySettingsType);
	handler.ReadOpenFn = ReadWindowVisibilityOpen;
	handler.ReadLineFn = ReadWindowVisibilityLine;
	handler.WriteAllFn = WriteWindowVisibility;
	handler.UserData = &windowVisibility_;
	ImGui::AddSettingsHandler(&handler);
#endif // USE_IMGUI
}

void ImGuiManager::DrawEditor() {
#ifdef USE_IMGUI
	// Editor UIを構成する各ウィンドウを毎フレーム描画する。
	// 実際の配置はDockSpace側が管理するため、ここでは各ウィンドウを出すだけでよい。
	HandleEditorShortcuts();
	dockSpace_.Draw([this]() { DrawMainMenuBar(); }, [this]() { DrawToolbar(); });

	// Windowメニューの表示フラグに応じて各ウィンドウを描画する。
	// 各ウィンドウのBeginにp_openを渡すことで、閉じるボタン[x]でもフラグが下りる。
	if (windowVisibility_.scene) {
		sceneView_.Draw(&windowVisibility_.scene);
	} else {
		// 非表示のビューは描画パスをスキップさせる。
		SetSceneViewVisible(false);
	}
	if (windowVisibility_.game) {
		gameView_.Draw(&windowVisibility_.game);
	} else {
		SetGameViewVisible(false);
	}
	if (windowVisibility_.hierarchy) {
		hierarchyWindow_.Draw(&windowVisibility_.hierarchy);
	}
	if (windowVisibility_.inspector) {
		inspectorWindow_.Draw(projectWindow_, &windowVisibility_.inspector);
	}
	// ProjectはDockBuilderでHierarchyとInspectorの間に初期配置される。
	if (windowVisibility_.project) {
		projectWindow_.Draw(&windowVisibility_.project);
	}
	if (windowVisibility_.performance) {
		performanceWindow_.Draw(&windowVisibility_.performance);
	}
	if (windowVisibility_.console) {
		EditorConsole::GetInstance()->Draw(&windowVisibility_.console);
	}
	if (windowVisibility_.animation) {
		animationWindow_.Draw(&windowVisibility_.animation);
	} else {
		// 非表示中は録画/プレビュー/Add Keyframeコンテキストを解除しておく。
		animationWindow_.NotifyHidden();
	}
	if (windowVisibility_.projectSettings) {
		projectSettingsWindow_.Draw(&windowVisibility_.projectSettings);
	}

	// 表示状態が変わったら(Windowメニュー・閉じるボタン)その場でiniへ書く。
	// ImGuiはウィンドウの移動などでしか保存せず、しかも5秒おきにまとめて書くので、
	// 知らせないと表示状態だけ変えた分が残らず、直後に落ちた(止めた)ときも失われる。
	// 起動直後の1回はiniから読んだ値との差分で保存が走るが、同じ内容を書くだけなので害はない。
	if (windowVisibility_ != savedWindowVisibility_) {
		savedWindowVisibility_ = windowVisibility_;
		ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
	}
#endif // USE_IMGUI
}

namespace {
#ifdef USE_IMGUI
// メニューバー内の再生コントロール用アイコンボタン。
// isTriangle=true で再生(▶ 三角)、false で編集(■ 四角)を描く。activeなら強調表示。クリックでtrue。
bool DrawModeIconButton(const char* id, bool active, bool isTriangle) {
	const float height = ImGui::GetFrameHeight();
	ImGui::InvisibleButton(id, ImVec2(height, height));
	const bool clicked = ImGui::IsItemClicked();

	const ImVec2 pMin = ImGui::GetItemRectMin();
	const ImVec2 pMax = ImGui::GetItemRectMax();

	ImU32 color;
	if (active) {
		color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
	} else if (ImGui::IsItemHovered()) {
		color = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
	} else {
		color = ImGui::GetColorU32(ImGuiCol_Text);
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float pad = height * 0.28f;
	if (isTriangle) {
		const ImVec2 a(pMin.x + pad, pMin.y + pad);
		const ImVec2 b(pMin.x + pad, pMax.y - pad);
		const ImVec2 c(pMax.x - pad, (pMin.y + pMax.y) * 0.5f);
		drawList->AddTriangleFilled(a, b, c, color);
	} else {
		drawList->AddRectFilled(ImVec2(pMin.x + pad, pMin.y + pad), ImVec2(pMax.x - pad, pMax.y - pad), color);
	}
	return clicked;
}
#endif // USE_IMGUI
} // namespace

void ImGuiManager::DrawMainMenuBar() {
#ifdef USE_IMGUI
	EditorApplication* app = EditorApplication::GetInstance();
	Scene* scene = app->GetCurrentScene();
	const bool hasScene = scene != nullptr;

	if (ImGui::BeginMenu("File")) {
		// Play 中はシーンを保存できない(ExportCurrentSceneJson)。プレハブの編集中は、プレハブの保存になる。
		if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !app->IsPlaying() || app->IsPrefabEditing())) {
			if (app->IsPrefabEditing()) {
				app->SavePrefabEditMode();
			} else {
				ExportCurrentSceneJson();
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Exit")) {
			PostQuitMessage(0);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, hasScene)) {
			if (EditorUndoManager::GetInstance()->Undo(*scene)) {
				AddConsoleLog("[Undo] Undo.");
			}
		}
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, hasScene)) {
			if (EditorUndoManager::GetInstance()->Redo(*scene)) {
				AddConsoleLog("[Undo] Redo.");
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Project Settings...")) {
			ShowWindow("ProjectSettings", true);
		}
		ImGui::EndMenu();
	}

	// GameObjectの生成メニューはHierarchyの右クリック(Create)に一本化した。
	// 生成先の親を選んでから作れるHierarchy側が本来の導線で、2箇所に置くと挙動差が生まれるため。

	if (ImGui::BeginMenu("Build")) {
		// Prefab編集中はReloadを受け付けない(EditorApplication側で拒否される)ためグレーアウトする。
		// Play中は自動でEditへ戻してからReloadされるので、そのまま実行できる。
		const bool canReloadGameModule = !app->IsPrefabEditing();
		if (ImGui::MenuItem("Reload DLL", "Ctrl+Shift+R", false, canReloadGameModule)) {
			app->ReloadGameModule();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window")) {
		ImGui::MenuItem("Scene", nullptr, &windowVisibility_.scene);
		ImGui::MenuItem("Game", nullptr, &windowVisibility_.game);
		ImGui::MenuItem("Hierarchy", nullptr, &windowVisibility_.hierarchy);
		ImGui::MenuItem("Inspector", nullptr, &windowVisibility_.inspector);
		ImGui::MenuItem("Project", nullptr, &windowVisibility_.project);
		ImGui::MenuItem("Console", nullptr, &windowVisibility_.console);
		ImGui::MenuItem("Performance", nullptr, &windowVisibility_.performance);
		ImGui::MenuItem("Animation", nullptr, &windowVisibility_.animation);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout")) {
			dockSpace_.ResetLayout();
		}
		ImGui::EndMenu();
	}
#endif // USE_IMGUI
}

void ImGuiManager::DrawToolbar() {
#ifdef USE_IMGUI
	EditorApplication* app = EditorApplication::GetInstance();

	// 縦位置を揃えるため、テキスト系はフレーム内でセンタリングする。
	ImGui::AlignTextToFramePadding();

	// --- 再生・編集コントロール。Edit=四角/Start=三角のアイコンで表す ---
	const bool isPlaying = app->IsPlaying();
	// Edit(四角): Playを止めて編集へ戻る。編集中は強調表示。
	if (DrawModeIconButton("##EditModeButton", !isPlaying, false)) {
		app->Stop();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Edit");
	}
	ImGui::SameLine();
	// Start(三角): Playを開始。再生中は強調表示。
	if (DrawModeIconButton("##StartModeButton", isPlaying, true)) {
		app->Start();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Start");
	}
	ImGui::SameLine();

	// Reload DLLはメニューバーの Build へ移動した(ツールバーは再生コントロールとモード表示に絞る)。

	if (app->IsPrefabEditing()) {
		if (ImGui::Button("Save Prefab")) {
			app->SavePrefabEditMode();
		}
		ImGui::SameLine();
		if (ImGui::Button("Back")) {
			app->ClosePrefabEditMode(false);
		}
		ImGui::SameLine();
		ImGui::Text("Mode: PrefabEdit (%s)", app->GetPrefabEditPath().filename().string().c_str());
	} else if (isPlaying) {
		ImGui::TextUnformatted("Mode: Play");
	} else {
		ImGui::TextUnformatted("Mode: Edit");
	}
#endif // USE_IMGUI
}

void ImGuiManager::Begin() {
#ifdef USE_IMGUI

	// エンジンのフレーム先頭処理に対応
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
#endif // USE_IMGUI
}

void ImGuiManager::End() {
#ifdef USE_IMGUI

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	// Project Windowで要求されたモデルサムネイルを、ImGui本体の描画前にOffscreenへ描く。
	projectWindow_.RenderModelPreviews();
	ImGui::Render();
	// ImGuiで作ったUIをDirectX12のCommandListに描画命令として積む
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon->GetCommandList());
#endif // USE_IMGUI
}

void ImGuiManager::Finalize() {
#ifdef USE_IMGUI

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	// ImSearchコンテキストはImGuiコンテキスト破棄の前に解放する。
	ImSearch::DestroyContext();
	ImGui::DestroyContext();
#endif // USE_IMGUI
}

} // namespace KujataEngine
