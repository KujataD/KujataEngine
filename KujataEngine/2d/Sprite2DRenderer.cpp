#include "Sprite2DRenderer.h"

#include "../components/SpriteRendererComponent.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace KujataEngine {

void DrawSceneSprites(Scene& scene, Camera* camera) {
	if (!camera) {
		return;
	}

	// 有効なSpriteRendererを集める。階層順ではなくSorting Orderで描くのがSprite方式の要点。
	std::vector<SpriteRendererComponent*> sprites;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			SpriteRendererComponent* sprite = dynamic_cast<SpriteRendererComponent*>(component.get());
			if (sprite && sprite->IsEnabled()) {
				sprite->SetRenderCamera(camera);
				sprites.push_back(sprite);
			}
		}
	}
	if (sprites.empty()) {
		return;
	}

	// Sorting Orderの小さい順(奥→手前)。同値はシーン内の順序を保つ(stable)。
	std::stable_sort(sprites.begin(), sprites.end(),
	                 [](const SpriteRendererComponent* a, const SpriteRendererComponent* b) { return a->GetSortingOrder() < b->GetSortingOrder(); });

	for (SpriteRendererComponent* sprite : sprites) {
		sprite->DrawSprite();
	}
}

} // namespace KujataEngine
