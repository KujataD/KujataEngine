#pragma once

#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// シーンのファイルの一覧と作成。
/// Project Settings の Scenes と CUI の scene.* コマンドは、どちらもここを呼ぶ。
/// シーンは Data/SceneJson/<名前>/<名前>.scene.json。開くのは ChangeScene、起動シーンは EditorApplication::SetStartupSceneName。
/// </summary>
namespace SceneEditing {

struct Result {
	bool succeeded = false;
	std::string message;
};

/// <summary>シーンの名前の一覧(名前順)。</summary>
std::vector<std::string> ListScenes();

bool Exists(const std::string& name);

/// <summary>空のシーンのファイルを作る。名前が空・ファイル名に使えない文字を含む・既にある場合は失敗。</summary>
Result CreateScene(const std::string& name);

} // namespace SceneEditing

} // namespace KujataEngine
