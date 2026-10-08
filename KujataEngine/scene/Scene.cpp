// Sceneの本体: GameObjectの所有と出し入れ、ライフサイクル(Initialize/Play/Finalize)、1フレームの更新順。
// 描画はSceneRendering.cpp、JSON書き出しはSceneSerialization.cpp、
// 物理はScenePhysics、衝突はSceneCollisionSystem、編集用の表示はSceneGizmos/EditorBillboardsにある。
#include "Scene.h"
#include "EditorBillboards.h"
#include "ScenePhysics.h"
#include "../base/FrameProfiler.h"
#include "../base/Time.h"
#include "../runtime/UIEventBus.h"
#include "../runtime/UIInput.h"
#include <algorithm>

namespace KujataEngine {

Scene::~Scene() = default;

void Scene::Initialize() {
	if (initialized_) {
		return;
	}

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->Initialize();
		}
	}

	initialized_ = true;
	EditorBillboards::PrepareScene(*this);
}

void Scene::Update() {
	// 固定の間隔の更新(Component::FixedUpdate)。溜めた時間が 1 回分を超えるたびに呼ぶ。
	// 時間スケールは溜める時間に掛かる(GetDeltaTime)ので、スローでは呼ばれる回数が減り、1 回の dt は変わらない。
	// 1 フレームに回す回数には上限を置き、超えた分は捨てる(重いフレームのあとに大量に回して、さらに重くなるのを防ぐ)。
	{
		constexpr int kMaxFixedStepsPerFrame = 5;
		fixedTimeAccumulator_ += Time::GetDeltaTime();
		int steps = 0;
		while (fixedTimeAccumulator_ >= Time::GetFixedDeltaTime() && steps < kMaxFixedStepsPerFrame) {
			// Update と同じ理由で添字で回す(途中で増えたオブジェクトも安全に扱える)。
			for (size_t index = 0; index < gameObjects_.size(); ++index) {
				GameObject* gameObject = gameObjects_[index].get();
				if (!gameObject || !gameObject->IsActiveInHierarchy()) {
					continue;
				}
				std::vector<std::unique_ptr<Component>>& components = gameObject->GetComponents();
				for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
					Component* component = components[componentIndex].get();
					if (component && component->IsEnabled()) {
						component->FixedUpdate();
					}
				}
			}
			fixedTimeAccumulator_ -= Time::GetFixedDeltaTime();
			++steps;
		}
		if (steps == kMaxFixedStepsPerFrame) {
			fixedTimeAccumulator_ = 0.0f;
		}
	}

	// ゲームロジック(Component::Update)がSetVeloc/移動を行う → 速度積分 → 衝突検出+応答 の順。
	//
	// **添字で回すこと。範囲forは使えない。**
	// Update中に PrefabAsset::Instantiate されると gameObjects_ に push_back されて再確保が起き、
	// 範囲forのイテレータが無効化されて以降のUpdateが解放済みメモリを触る(thisが壊れる)。
	// 要素は unique_ptr なので、再確保で動くのはポインタだけ。GameObject自体は動かないため添字なら安全。
	// size()を毎回読み直すことで、その場で増えた分も安全に扱える(同フレームで1回Updateされる)。
	for (size_t index = 0; index < gameObjects_.size(); ++index) {
		GameObject* gameObject = gameObjects_[index].get();
		if (gameObject && gameObject->IsRoot()) {
			gameObject->UpdateHierarchy();
		}
	}

	ScenePhysics::IntegrateRigidbodies(*this, Time::GetDeltaTime());

	UpdateWorldTransforms();
	{
		FrameProfiler::Scope profile(FrameProfiler::kCollision);
		collisionSystem_.Update(*this);
	}

	// 最後の更新(Component::LateUpdate)。物理と当たり判定で位置が決まった後に呼ぶ。
	// ここで動かした物も、描く前の PrepareFrame でワールド行列が更新されるので、このフレームの描画に間に合う。
	for (size_t index = 0; index < gameObjects_.size(); ++index) {
		GameObject* gameObject = gameObjects_[index].get();
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		std::vector<std::unique_ptr<Component>>& components = gameObject->GetComponents();
		for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
			Component* component = components[componentIndex].get();
			if (component && component->IsEnabled()) {
				component->LateUpdate();
			}
		}
	}
}

void Scene::SetSceneName(const std::string& name) { sceneName_ = name; }

void Scene::Finalize() {
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->Finalize();
		}
	}
	gameObjects_.clear();
	EditorBillboards::ClearCache();
	collisionSystem_.Clear();
	initialized_ = false;
}

void Scene::OnPlayStart() {
	// Play毎にUIイベント購読を張り直す(Unity同様、前回のPlayのリスナーを持ち越さない)。
	// 各ComponentはこのあとのOnPlayStartで購読するため、先にクリアしておく。
	UIEventBus::Clear();
	// UIのフォーカスも持ち越さない。前のシーンのGameObject*を握ったままだと
	// 破棄済みのアドレスと新しいオブジェクトが偶然一致したときに誤選択になる。
	SetUISelected(nullptr);
	// 時間スケールも必ず等速へ戻す。ポーズ(死亡メニュー等)で0にしたままStopされると、
	// **次のPlayが止まったまま始まる**(戻す責任はSetTimeScaleを呼んだ側にあるが、保険をここに置く)。
	Time::SetTimeScale(1.0f);
	// FixedUpdate の溜めも持ち越さない(前の Play の端数で、最初の更新の回数が変わらないように)。
	fixedTimeAccumulator_ = 0.0f;

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->OnPlayStart();
		}
	}
}

void Scene::OnPlayStop() {
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->OnPlayStop();
		}
	}
	collisionSystem_.Clear();
}

GameObject* Scene::CreateGameObject(const std::string& name) { return AddGameObject(std::make_unique<GameObject>(name)); }

GameObject* Scene::CreateEditorEntity() { return CreateGameObject("Entity"); }

void Scene::OnEditorComponentAdded(GameObject* gameObject, Component* component) {
	(void)gameObject;
	EditorBillboards::PrepareComponent(component);
}

GameObject* Scene::AddGameObject(std::unique_ptr<GameObject> gameObject) {
	GameObject* raw = gameObject.get();
	if (raw) {
		raw->SetScene(this);
	}
	gameObjects_.push_back(std::move(gameObject));

	if (initialized_ && raw) {
		raw->Initialize();
		for (const std::unique_ptr<Component>& component : raw->GetComponents()) {
			EditorBillboards::PrepareComponent(component.get());
		}
	}

	return raw;
}

void Scene::RemoveGameObjectHierarchy(GameObject* gameObject) {
	if (!gameObject) {
		return;
	}

	std::vector<GameObject*> removeTargets;
	removeTargets.push_back(gameObject);
	for (size_t index = 0; index < removeTargets.size(); ++index) {
		GameObject* current = removeTargets[index];
		if (!current) {
			continue;
		}
		for (GameObject* child : current->GetChildren()) {
			if (child) {
				removeTargets.push_back(child);
			}
		}
	}

	for (GameObject* target : removeTargets) {
		if (target) {
			target->Finalize();
		}
	}

	auto shouldRemove = [&removeTargets](const std::unique_ptr<GameObject>& current) {
		if (!current) {
			return false;
		}
		return std::find(removeTargets.begin(), removeTargets.end(), current.get()) != removeTargets.end();
	};
	gameObjects_.erase(std::remove_if(gameObjects_.begin(), gameObjects_.end(), shouldRemove), gameObjects_.end());
}

bool Scene::MoveGameObjectOrder(GameObject* dragged, GameObject* target, bool insertAfter) {
	if (!dragged || !target || dragged == target) {
		return false;
	}
	// 自分の子孫の隣には移動できない(自分を自分の中へ入れることになるため)。
	if (target->IsDescendantOf(dragged)) {
		return false;
	}

	GameObject* newParent = target->GetParent();

	// 1. 兄弟にするため、必要なら親を変更(ワールド位置を維持)。
	if (dragged->GetParent() != newParent) {
		if (!dragged->SetParent(newParent, true)) {
			return false;
		}
	}

	// 2. ランタイムの子表示順(children_)を更新。ルート同士(newParent==nullptr)は対象外。
	if (newParent) {
		newParent->ReorderChild(dragged, target, insertAfter);
	}

	// 3. 永続化順(gameObjects_の並び)を、draggedの所有ptrがtargetの直前/直後へ来るよう移動。
	//    保存はgameObjects_順で書き出されるため、これでルート順・子順の両方が保存に反映される。
	std::vector<std::unique_ptr<GameObject>>::iterator draggedIt =
	    std::find_if(gameObjects_.begin(), gameObjects_.end(),
	        [dragged](const std::unique_ptr<GameObject>& current) { return current.get() == dragged; });
	if (draggedIt == gameObjects_.end()) {
		return true;
	}
	std::unique_ptr<GameObject> ownedDragged = std::move(*draggedIt);
	gameObjects_.erase(draggedIt);

	std::vector<std::unique_ptr<GameObject>>::iterator targetIt =
	    std::find_if(gameObjects_.begin(), gameObjects_.end(),
	        [target](const std::unique_ptr<GameObject>& current) { return current.get() == target; });
	if (targetIt == gameObjects_.end()) {
		gameObjects_.push_back(std::move(ownedDragged));
		return true;
	}
	if (insertAfter) {
		++targetIt;
	}
	gameObjects_.insert(targetIt, std::move(ownedDragged));
	return true;
}

GameObject* Scene::FindGameObjectByName(const std::string& name) const {
	if (name.empty()) {
		return nullptr;
	}

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (!gameObject) {
			continue;
		}
		if (gameObject->GetName() == name) {
			return gameObject.get();
		}
	}
	return nullptr;
}

GameObject* Scene::FindGameObjectByInstanceId(const std::string& instanceId) const {
	if (instanceId.empty()) {
		return nullptr;
	}

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (!gameObject) {
			continue;
		}
		if (gameObject->GetInstanceId() == instanceId) {
			return gameObject.get();
		}
	}

	return nullptr;
}

void Scene::UpdateWorldTransforms() {
	// ここも添字で回す(Update同様、途中で生成されても壊れないように)。
	for (size_t index = 0; index < gameObjects_.size(); ++index) {
		GameObject* gameObject = gameObjects_[index].get();
		if (gameObject && gameObject->IsRoot()) {
			gameObject->UpdateWorldTransformHierarchy();
		}
	}
}

} // namespace KujataEngine
