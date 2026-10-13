#pragma once

#include "Scene.h"
#include <string>

namespace KujataEngine {

class Camera;
class CameraComponent;
class DebugCameraComponent;

/// <summary>
/// 現在のmain.cppにあったサンプル用Scene
/// </summary>
class SampleScene : public Scene {
public:
	KUJATA_API void Initialize() override;

	/// <summary>
	/// Play中だけ進める更新処理
	/// </summary>
	KUJATA_API void Update() override;

	/// <summary>
	/// Edit/Play共通で行う描画処理
	/// </summary>
	KUJATA_API void Draw() override;

	/// <summary>
	/// 既定のScene名。SetSceneName未設定時に使われる(基底のGetSceneNameが参照)。
	/// </summary>
	const char* GetDefaultSceneName() const override { return "SampleScene"; }

	KUJATA_API Camera* GetEditorCamera() override;

	// --- Scene/Game 2画面描画 ---
	KUJATA_API void PrepareFrame() override;
	KUJATA_API void RenderView(Camera* camera, bool drawEditorOverlays) override;
	KUJATA_API Camera* GetSceneViewCamera() override;
	KUJATA_API Camera* GetGameViewCamera() override;

	/// <summary>
	/// Editor上でComponent追加後にScene固有の依存を補完する
	/// </summary>
	KUJATA_API void OnEditorComponentAdded(GameObject* gameObject, Component* component) override;

private:
	void UpdateSceneView();
	void EnsureSceneServiceObjects();
	GameObject* FindGameObjectByName(const std::string& name);
	Camera* GetCurrentViewCamera();
	void ApplySceneLights();
	void ApplyRenderCamera(const Camera* camera);

private:
	CameraComponent* gameCameraComponent_ = nullptr;
	CameraComponent* editorCameraComponent_ = nullptr;
	DebugCameraComponent* editorDebugCameraComponent_ = nullptr;
	CameraComponent* currentViewCameraComponent_ = nullptr;
};

} // namespace KujataEngine
