// Sceneの描画まわり(Volume適用・ビュー描画・スクリーン空間UI)。
// 何を描くかの中身はGameObject/Component・SceneGizmos・EditorBillboards・UIレンダラーにあり、
// ここは描く順番とビューごとの出し分けだけを決める。
#include "Scene.h"
#include "EditorBillboards.h"
#include "SceneGizmos.h"
#include "../3d/Camera.h"
#include "../2d/Sprite2DRenderer.h"
#include "../2d/UICanvasRenderer.h"
#include "../base/DirectXCommon.h"
#include "../input/Input.h"
#include "../postprocess/PostProcess.h"
#include "../postprocess/VolumeStack.h"
#include "../runtime/PlayState.h"
#include "../runtime/SelectionProvider.h"

#include <algorithm>
#include <vector>

namespace KujataEngine {

namespace {

// 不透明物を先にすべて描き、半透明(深度を書かないもの)をカメラから遠い順に描く。
// 半透明は奥の物を隠せないので、先に描かれた物の上にしか重ならない。遠い順に描けば、手前の半透明ほど後から重なる。
// 距離は2乗のまま比べる(並び順が分かればよいので平方根は要らない)。同じ距離ならシーンの並び順のまま。
void DrawGameObjectsSorted(const std::vector<std::unique_ptr<GameObject>>& gameObjects, const Camera* camera) {
	// 毎フレームの確保を避けるため使い回す(描画は1スレッドなのでstaticでよい)。
	static std::vector<Component*> transparentComponents;
	static std::vector<std::pair<float, Component*>> sorted;
	// 描く順番: GetTransparentQueue の小さい順 → 同じなら遠い順。
	transparentComponents.clear();

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects) {
		if (gameObject && gameObject->IsRoot()) {
			gameObject->DrawHierarchy(&transparentComponents);
		}
	}

	// 半透明の自作シェーダーが「奥にある不透明物までの距離」を読めるように、ここまでの深度を写しておく。
	if (!transparentComponents.empty()) {
		DirectXCommon::GetInstance()->CaptureSceneDepth();
	}

	sorted.clear();
	for (Component* component : transparentComponents) {
		float distanceSquared = 0.0f;
		if (camera && component->GetOwner()) {
			const Vector3 position = component->GetOwner()->GetTransform().GetWorldPosition();
			const Vector3 delta = {position.x - camera->translation_.x, position.y - camera->translation_.y, position.z - camera->translation_.z};
			distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
		}
		sorted.emplace_back(distanceSquared, component);
	}
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
		const int queueA = a.second->GetTransparentQueue();
		const int queueB = b.second->GetTransparentQueue();
		return queueA != queueB ? queueA < queueB : a.first > b.first;
	});
	for (const auto& entry : sorted) {
		entry.second->Draw();
	}
}

} // namespace

void Scene::RefreshEditorBillboards() { EditorBillboards::PrepareScene(*this); }

void Scene::Draw() {
	UpdateWorldTransforms();

	DrawGameObjectsSorted(gameObjects_, GetEditorCamera());

	EditorBillboards::Draw(*this);
	SceneGizmos::DrawSelectedColliders(*this);
	SceneGizmos::DrawSelectedCameraFrustum(*this);
	SceneGizmos::DrawGrid(Vector4(0.4f, 0.4f, 0.4f, 1.0f));
}

void Scene::PrepareFrame() { UpdateWorldTransforms(); }

void Scene::ApplyVolumes(const Camera* camera) {
	// カメラが無いビュー(まだシーンカメラが揃っていない等)は原点基準で解決しておく。
	// Global Volumeしか無いシーンではカメラ位置は結果に影響しない。
	Vector3 cameraPosition = camera ? camera->translation_ : Vector3{0.0f, 0.0f, 0.0f};
	VolumeResolveResult resolved = VolumeStack::Resolve(*this, cameraPosition);
	PostProcess::GetInstance()->SetActiveProfile(resolved.profile);
}

void Scene::RenderView(Camera* camera, bool drawEditorOverlays) {
	// モデルのカメラは派生側でApplyRenderCamera済みの想定。ここでは半透明の並べ替えにだけ使う。
	DrawGameObjectsSorted(gameObjects_, camera);

	// 編集用オーバーレイはSceneビューのみ(呼び出し側のdrawEditorOverlaysで制御)。
	if (drawEditorOverlays) {
		EditorBillboards::Draw(*this);
		SceneGizmos::DrawSelectedColliders(*this);
		SceneGizmos::DrawSelectedCameraFrustum(*this);
		SceneGizmos::DrawSelectedLightRanges(*this);
		SceneGizmos::DrawGrid(Vector4(0.4f, 0.4f, 0.4f, 1.0f));
	}

	// 全Collider可視化モードがONなら、Sceneビュー/Gameビュー問わず全Colliderを描く。
	// **F1の受け付けはここで行う。**
	// Scene::Update は Play 中しか回らないので、Update側にトグルを置くと
	// 「止めて配置を直している最中に全部を見る」ことができない(調整したい時ほど使えない)。
	if (drawEditorOverlays && Input::GetKeyTrigger(DIK_F1)) {
		ToggleShowAllColliders();
	}
	if (SceneGizmos::IsShowAllColliders()) {
		SceneGizmos::DrawAllColliders(*this);
	}

	// world空間2Dスプライト(Sprite方式)。3Dメッシュの後・スクリーン空間UIの前に、
	// Sorting Order順でまとめて描く(深度を書かないので描画順が前後関係になる)。
	DrawSceneSprites(*this, camera);

	// World Space Canvas。ワールドに置かれたUIなので、選択状態に関わらず常に描く。
	{
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		// レイアウトはUIの座標(Gameの出力の大きさ)で決める。クリック判定(UpdateUIEventSystem)と同じ基準にするため。
		DrawSceneWorldCanvases(*this, camera, static_cast<float>(dxCommon->GetGameOutputWidth()), static_cast<float>(dxCommon->GetGameOutputHeight()));
	}

	// Screen Space CanvasはここでHDRシーンRTへは描かない。フォグ/ブルーム/トーンマップの影響を
	// 受けないよう、ポストプロセス後のLDR RTへRenderScreenSpaceUIで描く。
}

void Scene::RenderScreenSpaceUI(float targetWidth, float targetHeight, bool drawEditorOverlays) {
	// drawEditorOverlays==true はSceneビュー、false はGameビュー。
	// Gameビューは常にUIを描く。SceneビューはUI(Canvasまたはその子孫)を選択中のときだけ描く。
	bool drawUI = !drawEditorOverlays;
	if (drawEditorOverlays) {
		// UI編集モード中は選択に関わらず常にCanvasを描く。それ以外はUI(またはその子孫)選択時のみ。
		drawUI = IsSceneViewUIEditMode() || IsUIRelatedObject(GetSelectionProvider().GetSelectedGameObject());
	}
	if (drawUI) {
		DrawSceneCanvases(*this, targetWidth, targetHeight);
	}
}

void Scene::SetShowAllColliders(bool show) { SceneGizmos::SetShowAllColliders(show); }

bool Scene::IsShowAllColliders() { return SceneGizmos::IsShowAllColliders(); }

void Scene::ToggleShowAllColliders() { SceneGizmos::SetShowAllColliders(!SceneGizmos::IsShowAllColliders()); }

} // namespace KujataEngine
