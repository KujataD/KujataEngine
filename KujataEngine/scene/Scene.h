#pragma once

#include "../runtime/KujataApi.h"
#include "GameObject.h"
#include "SceneCollisionSystem.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {

class Camera;

class Scene {
public:
	KUJATA_API virtual ~Scene();
	KUJATA_API virtual void Initialize();
	KUJATA_API virtual void Update();
	KUJATA_API virtual void Draw();

	KUJATA_API virtual void Finalize();

	/// <summary>
	/// EditからPlayへ入る直前の処理
	/// </summary>
	KUJATA_API virtual void OnPlayStart();

	/// <summary>
	/// PlayからEditへ戻る時の処理
	/// </summary>
	KUJATA_API virtual void OnPlayStop();

	/// <summary>
	/// Editor保存時に使うScene名を取得。
	/// SetSceneName()でオーバーライドされていればそれを、無ければ派生の既定名を返す。
	/// この名前が SceneJson/<name>/<name>.scene.json のロードパスを決める。
	/// </summary>
	virtual const char* GetSceneName() const { return sceneName_.empty() ? GetDefaultSceneName() : sceneName_.c_str(); }

	/// <summary>
	/// 派生クラスが返す既定のScene名(SetSceneName未設定時に使われる)。
	/// </summary>
	virtual const char* GetDefaultSceneName() const { return "Scene"; }

	/// <summary>
	/// ロード対象のScene名を上書きする。ChangeScene時に生成直後・Initialize前に設定する。
	/// </summary>
	KUJATA_API void SetSceneName(const std::string& name);

	virtual Camera* GetEditorCamera() { return nullptr; }

	// --- Scene/Game 2画面描画用API ---
	/// <summary>
	/// 毎フレーム1回の準備(入力反映・カメラ同期・ライト・ワールド行列など)。ビュー描画の前に呼ぶ。
	/// </summary>
	KUJATA_API virtual void PrepareFrame();

	/// <summary>
	/// 1ビューぶんの描画。cameraでモデルを描き、drawEditorOverlaysでグリッド/アイコン/フラスタム等を足す。
	/// </summary>
	KUJATA_API virtual void RenderView(Camera* camera, bool drawEditorOverlays);

	/// <summary>
	/// シーン上のVolumeComponentを解決し、このビューで使うポストエフェクト設定をPostProcessへ適用する。
	/// Local Volumeの内外判定にcameraの位置を使うため、ポスト処理を走らせるビューごとに呼ぶこと。
	/// Volumeが1つも無いシーンでは既定値(ポスト無し相当)が適用される。
	/// </summary>
	KUJATA_API void ApplyVolumes(const Camera* camera);

	/// <summary>
	/// Screen Space Canvas(スクリーン空間UI)の描画。
	/// フォグ/ブルーム/トーンマップの影響を受けないよう、ポストプロセスの後にLDR RTへ描く。
	/// targetWidth/Heightは描画先(Resolve RT/バックバッファ)のピクセルサイズ。
	/// drawEditorOverlays==true はSceneビュー(UI選択中/UI編集モードのみ描画)、false はGameビュー(常に描画)。
	/// </summary>
	KUJATA_API virtual void RenderScreenSpaceUI(float targetWidth, float targetHeight, bool drawEditorOverlays);

	/// <summary>
	/// Sceneビュー(デバッグカメラ)のカメラ。既定はGetEditorCamera。
	/// </summary>
	virtual Camera* GetSceneViewCamera() { return GetEditorCamera(); }

	/// <summary>
	/// Gameビュー(メインカメラ)のカメラ。2画面対応シーンのみ非nullを返す。
	/// </summary>
	virtual Camera* GetGameViewCamera() { return nullptr; }

	/// <summary>
	/// Editor用ビルボード(アイコン)のModel/Transformを準備する。描画パス外から呼ぶこと。
	/// ランタイム生成されたComponentのアイコンも確実に表示させるため冪等に何度でも呼べる。
	/// </summary>
	KUJATA_API void RefreshEditorBillboards();

	/// <summary>
	/// GameObjectを作成し、所有権をSceneへ持たせる
	/// </summary>
	KUJATA_API GameObject* CreateGameObject(const std::string& name = "GameObject");

	/// <summary>
	/// EditorのHierarchyから空Objectを作成する
	/// </summary>
	KUJATA_API virtual GameObject* CreateEditorEntity();

	// Cube/Sphere/CapsuleはEditor/PrimitiveObjectFactoryが生成する。
	// Collider付きで作るためScene派生ごとの差分が不要になり、形状の追加も1関数で済む。

	/// <summary>
	/// Editor上でComponent追加後にScene固有の依存を補完する
	/// </summary>
	KUJATA_API virtual void OnEditorComponentAdded(GameObject* gameObject, Component* component);

	/// <summary>
	/// SceneへGameObjectを追加し、所有権をSceneへ移す
	/// </summary>
	KUJATA_API GameObject* AddGameObject(std::unique_ptr<GameObject> gameObject);

	KUJATA_API void RemoveGameObjectHierarchy(GameObject* gameObject);

	/// <summary>
	/// Hierarchyのドラッグ&ドロップ並び替え用。draggedをtargetの兄弟にし、直前/直後へ移動する。
	/// 必要なら親を変更(ワールド位置維持)し、ランタイム表示順(children_)と
	/// 永続化順(gameObjects_)の両方を更新する。
	/// </summary>
	KUJATA_API bool MoveGameObjectOrder(GameObject* dragged, GameObject* target, bool insertAfter);

	KUJATA_API GameObject* FindGameObjectByInstanceId(const std::string& instanceId) const;

	/// <summary>
	/// 名前が一致する最初のGameObjectを返します(無ければnullptr)。
	/// </summary>
	KUJATA_API GameObject* FindGameObjectByName(const std::string& name) const;

	/// <summary>
	/// Scene内GameObjectのワールド行列を親子順に更新
	/// </summary>
	KUJATA_API void UpdateWorldTransforms();

	std::vector<std::unique_ptr<GameObject>>& GetGameObjects() { return gameObjects_; }

	const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return gameObjects_; }

	KUJATA_API std::string ToJson() const;

	/// <summary>
	/// ゲーム中(Gameビュー)に全GameObjectのColliderをワイヤーフレーム表示するデバッグモードの切り替え。
	/// Sceneビューは従来通り選択オブジェクトのみ表示する。
	/// </summary>
	KUJATA_API static void SetShowAllColliders(bool show);
	KUJATA_API static bool IsShowAllColliders();
	KUJATA_API static void ToggleShowAllColliders();

protected:
	/// <summary>
	/// Sceneが所有するGameObject一覧
	/// 将来はHierarchy/Serialize/Scene Cloneの対象になる。
	/// </summary>
	std::vector<std::unique_ptr<GameObject>> gameObjects_;

private:
	bool initialized_ = false;
	SceneCollisionSystem collisionSystem_;
	// FixedUpdate にまだ回していない時間(秒)。Play の開始で 0 に戻す。
	float fixedTimeAccumulator_ = 0.0f;

	// SetSceneName()で設定されるロード対象名。空なら GetDefaultSceneName() を使う。
	std::string sceneName_;
};

} // namespace KujataEngine
