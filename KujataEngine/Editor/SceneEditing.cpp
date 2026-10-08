#include "SceneEditing.h"

#include "../base/ProjectPath.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace KujataEngine {

namespace SceneEditing {

namespace {

std::filesystem::path GetSceneRoot() { return GetProjectDataRoot() / "SceneJson"; }

std::filesystem::path GetSceneFile(const std::string& name) { return GetSceneRoot() / name / (name + ".scene.json"); }

// 空白を許すと、CUI(空白区切り)から指定できなくなる。
bool IsValidName(const std::string& name) {
	return !name.empty() && name.find_first_of(" \t\r\n/\\:*?\"<>|") == std::string::npos;
}

} // namespace

std::vector<std::string> ListScenes() {
	std::vector<std::string> names;
	std::error_code errorCode;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(GetSceneRoot(), errorCode)) {
		const std::string name = entry.path().filename().string();
		if (entry.is_directory() && std::filesystem::exists(GetSceneFile(name))) {
			names.push_back(name);
		}
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool Exists(const std::string& name) { return IsValidName(name) && std::filesystem::exists(GetSceneFile(name)); }

Result CreateScene(const std::string& name) {
	if (!IsValidName(name)) {
		return {false, "シーンの名前が空か、使えない文字(空白・/ \\ : * ? \" < > |)を含んでいます: " + name};
	}
	if (Exists(name)) {
		return {false, "シーンは既にあります: " + name};
	}

	std::error_code errorCode;
	std::filesystem::create_directories(GetSceneRoot() / name, errorCode);
	std::ofstream ofs(GetSceneFile(name));
	if (!ofs) {
		return {false, "シーンのファイルを作れませんでした: " + GetSceneFile(name).string()};
	}
	ofs << "{\n  \"assetType\": \"Scene\",\n  \"name\": \"" << name << "\",\n  \"gameObjects\": []\n}\n";
	return {true, "シーンを作りました: " + name};
}

} // namespace SceneEditing

} // namespace KujataEngine
