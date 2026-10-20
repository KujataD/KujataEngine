// エディタの CUI のコマンド: マテリアルと、マテリアルで選ぶ自作シェーダー
// (保存と反映は SaveMaterialAsset、ひな形は CreateCustomShaderFile。どちらも Material の Inspector と同じもの)。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../AssetDatabase.h"
#include "../MaterialInspector.h"
#include "../../3d/GraphicsPipeline.h"
#include "../../assets/MaterialAsset.h"
#include "../../base/ProjectPath.h"
#include "../../components/ModelRendererComponent.h"
#include "../../scene/GameObject.h"
#include "../../scene/Scene.h"
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

// "Materials/Toon.material.json" のような Data 基準の相対パス、または絶対パスでマテリアルを探す。
bool ResolveMaterialPath(const std::string& text, std::filesystem::path& outPath, std::string& error) {
	if (text.empty()) {
		error = "マテリアルのパスを指定してください(material.list で一覧)。";
		return false;
	}
	std::filesystem::path path(text);
	if (path.is_relative()) {
		path = GetProjectDataRoot() / path;
	}
	std::error_code errorCode;
	if (!MaterialAsset::IsMaterialFile(path) || !std::filesystem::exists(path, errorCode)) {
		error = "マテリアルが見つかりません: " + text + "(*.material.json を Data からの相対パスで指定。material.list で一覧)";
		return false;
	}
	outPath = path;
	return true;
}

json DescribeMaterial(const std::filesystem::path& path, const MaterialAssetData& material) {
	json value;
	std::error_code errorCode;
	value["path"] = std::filesystem::relative(path, GetProjectDataRoot(), errorCode).generic_string();
	json fields = json::object();
	MaterialAsset::WriteJsonObject(fields, material);
	value["fields"] = fields;
	return value;
}

EditorCommandResult CommandMaterialList(const EditorCommandArgs&) {
	json materials = json::array();
	const std::filesystem::path dataRoot = GetProjectDataRoot();
	std::error_code errorCode;
	for (auto it = std::filesystem::recursive_directory_iterator(dataRoot, errorCode); !errorCode && it != std::filesystem::recursive_directory_iterator(); it.increment(errorCode)) {
		if (it->is_regular_file() && MaterialAsset::IsMaterialFile(it->path())) {
			materials.push_back(std::filesystem::relative(it->path(), dataRoot, errorCode).generic_string());
		}
	}
	return EditorCommandResult::Success(materials);
}

EditorCommandResult CommandMaterialCreate(const EditorCommandArgs& args) {
	const std::string name = MaterialAsset::SanitizeName(args.Count() > 0 ? args.Get(0) : "New Material");
	const std::filesystem::path directory = GetProjectDataRoot() / "Materials";
	std::error_code errorCode;
	std::filesystem::create_directories(directory, errorCode);
	const std::filesystem::path path = directory / (name + ".material.json");
	if (std::filesystem::exists(path, errorCode)) {
		return EditorCommandResult::Failure("同じ名前のマテリアルがあります: " + path.generic_string());
	}
	std::string message;
	if (!MaterialAsset::CreateDefaultFile(path, message)) {
		return EditorCommandResult::Failure(message);
	}
	AssetDatabase::GetInstance().GetOrCreateAssetId(path);
	MaterialAssetData material = MaterialAsset::CreateDefault();
	MaterialAsset::Load(path, material, message);
	return EditorCommandResult::Success(DescribeMaterial(path, material));
}

EditorCommandResult CommandMaterialGet(const EditorCommandArgs& args) {
	std::filesystem::path path;
	std::string error;
	if (!ResolveMaterialPath(args.Get(0), path, error)) {
		return EditorCommandResult::Failure(error);
	}
	MaterialAssetData material = MaterialAsset::CreateDefault();
	if (!MaterialAsset::Load(path, material, error)) {
		return EditorCommandResult::Failure(error);
	}
	return EditorCommandResult::Success(DescribeMaterial(path, material));
}

// マテリアルのキーを 1 つ書き換える(material.set と material.instance.set で共通)。
// 値は行末までを JSON として読む(field.set と同じ)。読めなければ文字列として扱う。outChange には key / before / after が入る。
bool SetMaterialField(MaterialAssetData& material, const std::string& key, const std::string& rawValue, json& outChange, std::string& error) {
	json value = json::parse(rawValue, nullptr, false);
	if (value.is_discarded()) {
		value = rawValue;
	}

	// 今の全フィールドを JSON にして 1 つだけ差し替え、読み戻す(保存形式と同じ手順なので、キー名はファイルと同じ)。
	json fields = json::object();
	MaterialAsset::WriteJsonObject(fields, material);
	if (key == "textures" || key == "name" || !fields.contains(key)) {
		std::string keys;
		for (auto it = fields.begin(); it != fields.end(); ++it) {
			if (it.key() != "textures" && it.key() != "name") {
				keys += " " + it.key();
			}
		}
		error = "書き換えられないキーです: " + key + "。書き換えられるキー:" + keys;
		return false;
	}
	const json before = fields[key];
	const bool sameKind = (before.is_number() && value.is_number()) || (before.is_boolean() && value.is_boolean()) ||
	                      (before.is_string() && value.is_string()) || (before.is_array() && value.is_array() && value.size() == before.size());
	if (!sameKind) {
		error = key + " は " + before.dump() + " と同じ形の値で指定してください。";
		return false;
	}
	// 整数の欄(shaderModel / toonSteps など)に 4.5 のような小数を入れても読み飛ばされないよう、整数にそろえる。
	if (before.is_number_integer() && value.is_number_float()) {
		value = static_cast<int>(value.get<double>());
	}
	fields[key] = value;
	material = MaterialAsset::ReadJsonObject(fields, material);

	outChange["key"] = key;
	outChange["before"] = before;
	outChange["after"] = value;
	return true;
}

EditorCommandResult CommandMaterialSet(const EditorCommandArgs& args) {
	if (args.Count() < 3) {
		return EditorCommandResult::Failure("使い方: material.set <マテリアルのパス> <キー> <値>(例: material.set Materials/Toon.material.json toonSteps 4)");
	}
	std::filesystem::path path;
	std::string error;
	if (!ResolveMaterialPath(args.Get(0), path, error)) {
		return EditorCommandResult::Failure(error);
	}
	MaterialAssetData material = MaterialAsset::CreateDefault();
	if (!MaterialAsset::Load(path, material, error)) {
		return EditorCommandResult::Failure(error);
	}
	json change;
	if (!SetMaterialField(material, args.Get(1), args.RestFrom(2), change, error)) {
		return EditorCommandResult::Failure(error);
	}

	if (!SaveMaterialAsset(path, material, error)) {
		return EditorCommandResult::Failure(error);
	}
	json result = DescribeMaterial(path, material);
	result.update(change);
	return EditorCommandResult::Success(result);
}

// オブジェクトの ModelRendererComponent(マテリアルのインスタンスを持てるもの)を探す。
ModelRendererComponent* ResolveModelRenderer(const std::string& objectSpec, std::string& error) {
	Scene* scene = GetScene();
	if (!scene) {
		error = "シーンがありません。";
		return nullptr;
	}
	GameObject* gameObject = ResolveObject(*scene, objectSpec, error);
	if (!gameObject) {
		return nullptr;
	}
	return dynamic_cast<ModelRendererComponent*>(ResolveComponent(*gameObject, "ModelRendererComponent", error));
}

json DescribeMaterialInstance(const ModelRendererComponent& renderer) {
	json value;
	value["instanced"] = renderer.HasMaterialInstance();
	value["materialPath"] = renderer.GetMaterialPath();
	json fields = json::object();
	MaterialAsset::WriteJsonObject(fields, renderer.GetMaterial());
	value["fields"] = fields;
	return value;
}

EditorCommandResult CommandMaterialInstanceSet(const EditorCommandArgs& args) {
	if (args.Count() < 3) {
		return EditorCommandResult::Failure("使い方: material.instance.set <オブジェクト> <キー> <値>(例: material.instance.set Boss/Shell baseColor [1,0,0,1])");
	}
	std::string error;
	ModelRendererComponent* renderer = ResolveModelRenderer(args.Get(0), error);
	if (!renderer) {
		return EditorCommandResult::Failure(error);
	}
	MaterialAssetData material = renderer->GetMaterial();
	json change;
	if (!SetMaterialField(material, args.Get(1), args.RestFrom(2), change, error)) {
		return EditorCommandResult::Failure(error);
	}
	renderer->SetMaterialInstance(material);
	json result = DescribeMaterialInstance(*renderer);
	result.update(change);
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandMaterialInstanceClear(const EditorCommandArgs& args) {
	std::string error;
	ModelRendererComponent* renderer = ResolveModelRenderer(args.Get(0), error);
	if (!renderer) {
		return EditorCommandResult::Failure(error);
	}
	renderer->ClearMaterialInstance();
	return EditorCommandResult::Success(DescribeMaterialInstance(*renderer));
}

// 自作シェーダーの一覧: Data 配下の .hlsl(とエンジン同梱の engine:〜)と、登録済み(どれかのマテリアルが使った)もののコンパイルの状態。
json DescribeShaders() {
	json shaders = json::array();
	const std::vector<CustomShaderInfo> infos = GraphicsPipeline::GetInstance()->GetCustomShaderInfos();
	std::error_code errorCode;
	for (const std::string& file : ListCustomShaderFiles()) {
		json entry;
		entry["path"] = file;
		entry["loaded"] = false;
		const std::filesystem::path absolutePath = MaterialAsset::ResolveShaderPath(file);
		for (const CustomShaderInfo& info : infos) {
			if (std::filesystem::equivalent(info.path, absolutePath, errorCode)) {
				entry["loaded"] = true;
				entry["usable"] = info.usable;
				entry["vertexShader"] = info.hasVertexShader ? "VSMain" : "標準";
				entry["error"] = info.error;
			}
		}
		shaders.push_back(entry);
	}
	return shaders;
}

EditorCommandResult CommandShaderList(const EditorCommandArgs&) { return EditorCommandResult::Success(DescribeShaders()); }

EditorCommandResult CommandShaderCreate(const EditorCommandArgs& args) {
	std::string relativePath;
	std::string message;
	if (!CreateCustomShaderFile(args.Count() > 0 ? args.Get(0) : "NewShader", relativePath, message)) {
		return EditorCommandResult::Failure(message);
	}
	json result;
	result["path"] = relativePath;
	result["hint"] = "material.set <マテリアル> shaderPath \"" + relativePath + "\" で使う";
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandShaderReload(const EditorCommandArgs&) {
	GraphicsPipeline::GetInstance()->ReloadChangedCustomShaders(true);
	return EditorCommandResult::Success(DescribeShaders());
}

} // namespace

void RegisterMaterialCommands(EditorCommandRegistry& registry) {
	registry.Register("material.list", "material.list", "マテリアルファイルの一覧(Data からの相対パス)", CommandMaterialList);
	registry.Register("material.create", "material.create [名前]", "Data/Materials/<名前>.material.json を作る", CommandMaterialCreate);
	registry.Register("material.get", "material.get <マテリアルのパス>", "マテリアルの全フィールド(キー名はファイルと同じ)", CommandMaterialGet);
	registry.Register("material.set", "material.set <マテリアルのパス> <キー> <値>",
	                  "マテリアルのフィールドを書き換えて保存し、使っているオブジェクトへ反映する(Undo 不可。例: material.set Materials/Toon.material.json shaderModel 8)",
	                  CommandMaterialSet);
	registry.Register("material.instance.set", "material.instance.set <オブジェクト> <キー> <値>",
	                  "そのオブジェクトの ModelRendererComponent だけのマテリアル(インスタンス)にして 1 つ書き換える。アセットとほかのオブジェクトは変わらない(保存されず、Play を止めると戻る。Undo 不可)",
	                  CommandMaterialInstanceSet);
	registry.Register("material.instance.clear", "material.instance.clear <オブジェクト>", "インスタンスをやめて、参照しているアセットのマテリアルへ戻す",
	                  CommandMaterialInstanceClear);
	registry.Register("shader.list", "shader.list", "自作シェーダー(Data 配下の .hlsl)の一覧と、コンパイルの成否・エラー", CommandShaderList);
	registry.Register("shader.create", "shader.create [名前]", "自作シェーダーのひな形を Data/Shaders/<名前>.hlsl に作る(Material の Inspector の New と同じ)", CommandShaderCreate);
	registry.Register("shader.reload", "shader.reload", "自作シェーダーをすべてコンパイルし直す(保存すれば自動でも読み直す)", CommandShaderReload);
}

} // namespace KujataEngine
