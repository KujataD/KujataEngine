#pragma once

#include "GameModuleHotReloader.h"
#include "../runtime/GameModule.h"
#include "../scene/Scene.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace KujataEngine {

// エディタ全体の実行状態。
// 将来Scene Cloneを入れる場合も、このモードを見てEditScene/PlaySceneを切り替える想定。
enum class EditorMode {
	Edit,
	Play,
	PrefabEdit,
};

/// <summary>
/// Editor全体の状態と実行制御を管理する
/// </summary>
class EditorApplication {
public:
	static KUJATA_API EditorApplication* GetInstance();

	void Initialize();

	void BeginFrame();

	/// <summary>
	/// Editor UI更新とゲームUpdate制御
	/// </summary>
	void Update();

	void Draw();

	void EndFrame();

	void Finalize();

	/// <summary>
	/// プロセスの終了コード。--run のスクリプトで失敗したコマンドがあれば1。
	/// </summary>
	int GetExitCode() const;

	/// <summary>
	/// Playを開始する
	/// </summary>
	void Start();

	/// <summary>
	/// Playを停止してEditへ戻る
	/// </summary>
	void Stop();

	KUJATA_API bool IsPlaying() const;

	bool IsPrefabEditing() const;

	bool OpenPrefabEditMode(const std::filesystem::path& prefabPath);

	bool SavePrefabEditMode();

	void ClosePrefabEditMode(bool saveChanges);

	const std::filesystem::path& GetPrefabEditPath() const;

	EditorMode GetEditorMode() const;

	bool ShouldUpdateGame() const;

	/// <summary>
	/// 現在操作対象にするSceneを設定し、所有権をEditorApplicationへ移す
	/// </summary>
	void SetCurrentScene(std::unique_ptr<Scene> scene);

	bool ReloadGameModule();

	/// <summary>GameModule のプロジェクトを、プロジェクトの中の .cpp / .h に合わせる(GameProjectSync)。</summary>
	void SyncGameProjectFiles();

	Scene* GetCurrentScene() const;

	// 起動時に最初に読み込むシーン名(ProjectSettings/StartupScene.txt)。未設定なら空。
	KUJATA_API std::string GetStartupSceneName() const;
	KUJATA_API void SetStartupSceneName(const std::string& sceneName);

private:
	EditorApplication() = default;
	~EditorApplication() = default;
	EditorApplication(const EditorApplication&) = delete;
	EditorApplication& operator=(const EditorApplication&) = delete;


	void AddConsoleLog(const std::string& message);

	void DestroyCurrentScene();
	void SetCurrentSceneRaw(Scene* scene, GameModuleApi::DestroySceneFunc destroySceneFunc);
	void InitializeCurrentSceneAndImportJson();

	// SceneManager::ChangeSceneで予約された切り替えをフレーム先頭で実行する。
	void ProcessPendingSceneChange();

	/// <summary>
	/// DLL差し替え前に現在SceneをJSONへ退避する
	/// </summary>
	bool SaveCurrentSceneJsonForHotReload();
	void RestoreFallbackSceneAfterHotReloadFailure();

private:
	// 起動時は必ずEdit。Startを押したときだけPlayに移行する。
	EditorMode editorMode_ = EditorMode::Edit;

	// 将来のEditScene/PlayScene差し替え口。DLL由来SceneはDLL側DestroySceneで破棄する。
	Scene* currentScene_ = nullptr;
	GameModuleApi::DestroySceneFunc destroyCurrentSceneFunc_ = nullptr;
	// GameModule DLLの読み込み・HotReload用ビルド・解放。Sceneの作り直しはこちら側で行う。
	GameModuleHotReloader gameModule_;
	Scene* sceneBeforePrefabEdit_ = nullptr;
	GameModuleApi::DestroySceneFunc destroySceneBeforePrefabEditFunc_ = nullptr;
	std::unique_ptr<Scene> prefabEditScene_;
	std::filesystem::path prefabEditPath_;
	std::string playModeSceneJson_;
	std::string playModeSelectedObjectInstanceId_;
};

} // namespace KujataEngine
