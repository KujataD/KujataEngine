#include "ModelRendererComponent.h"
#include "../3d/Camera.h"
#include "../3d/Model.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"
#include "../scene/SerializedFieldRegistry.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace KujataEngine {

namespace {

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	if (!json.at(key).is_string()) {
		return defaultValue;
	}

	return json.at(key).get<std::string>();
}

ModelRendererComponent::PrimitiveType ReadPrimitiveType(const std::string& primitiveName) {
	if (primitiveName == "Cube") {
		return ModelRendererComponent::PrimitiveType::Cube;
	}

	if (primitiveName == "Sphere") {
		return ModelRendererComponent::PrimitiveType::Sphere;
	}

	if (primitiveName == "Capsule") {
		return ModelRendererComponent::PrimitiveType::Capsule;
	}

	if (primitiveName == "Plane") {
		return ModelRendererComponent::PrimitiveType::Plane;
	}

	if (primitiveName == "Ring") {
		return ModelRendererComponent::PrimitiveType::Ring;
	}

	if (primitiveName == "Model") {
		return ModelRendererComponent::PrimitiveType::Model;
	}

	return ModelRendererComponent::PrimitiveType::Custom;
}

// 旧形式のModel参照("bishop"のような名前のみ)を "Resources/<name>/<name>.obj" 規約でパスへ展開する。
// 既にパス形式(区切りか拡張子を含む)ならそのまま返す。
// Model::CreateFromOBJと同様、規約フォルダに無ければResources直下(Resources/<name>.obj)へフォールバックする。
std::string NormalizeModelPath(const std::string& modelPathOrName) {
	if (modelPathOrName.empty()) {
		return modelPathOrName;
	}
	bool hasSeparator = modelPathOrName.find('/') != std::string::npos || modelPathOrName.find('\\') != std::string::npos;
	bool hasExtension = std::filesystem::path(modelPathOrName).has_extension();
	if (hasSeparator || hasExtension) {
		return modelPathOrName;
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	std::string conventionPath = "Resources/" + modelPathOrName + "/" + modelPathOrName + ".obj";
	if (std::filesystem::exists(assetDatabase.ResolveAssetPath("", conventionPath))) {
		return conventionPath;
	}
	std::string rootPath = "Resources/" + modelPathOrName + ".obj";
	if (std::filesystem::exists(assetDatabase.ResolveAssetPath("", rootPath))) {
		return rootPath;
	}
	return conventionPath;
}

std::string GetBaseColorTexturePath(const MaterialAssetData& material) {
	return MaterialAsset::GetTexturePath(material, MaterialTextureSlot::BaseColor);
}

void SetBaseColorTexturePath(MaterialAssetData& material, const std::string& texturePath) {
	MaterialAsset::SetTexture(material, MaterialTextureSlot::BaseColor, "", texturePath);
}

} // namespace

ModelRendererComponent::ModelRendererComponent() = default;

ModelRendererComponent::ModelRendererComponent(const Camera* camera) : camera_(camera) {}

ModelRendererComponent::ModelRendererComponent(std::unique_ptr<Model> model, const Camera* camera) : model_(std::move(model)), camera_(camera) {}

ModelRendererComponent::~ModelRendererComponent() = default;

void ModelRendererComponent::SetModel(std::unique_ptr<Model> model) {
	model_ = std::move(model);
	primitive_ = PrimitiveType::Custom;
	ApplyMaterialToModel();
}

void ModelRendererComponent::SetRenderCamera(const Camera* camera) {
	camera_ = camera;
}

void ModelRendererComponent::SetPrimitive(PrimitiveType primitive, const std::string& textureFilePath) {
	primitive_ = primitive;
	// TextureはMaterialだけで管理する。Material Assetを参照していない場合のみ、
	// 埋め込みMaterialのBaseColorへ反映する(Unityの Instance Material 上書き相当)。
	if (!textureFilePath.empty() && materialAssetId_.empty() && materialPath_.empty()) {
		SetBaseColorTexturePath(material_, textureFilePath);
	}
	RebuildPrimitiveModel();
}

void ModelRendererComponent::SetModelPath(const std::string& modelPathOrName) {
	std::string normalizedPath = NormalizeModelPath(modelPathOrName);
	modelAssetId_.clear();
	modelPath_ = normalizedPath;
	if (normalizedPath.empty()) {
		return;
	}

	// 存在するファイルならassetIdを補完し、リネーム/移動に追従できる参照へ揃える。
	// Editor外(FallbackAssetResolver)ではIDが取れないため、その場合はパスをそのまま保持する。
	IAssetResolver& assetDatabase = GetAssetResolver();
	std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", normalizedPath);
	modelAssetId_ = assetDatabase.GetOrCreateAssetId(resolvedPath);
	if (!modelAssetId_.empty()) {
		modelPath_ = assetDatabase.MakeProjectRelativePath(resolvedPath);
	}
}

std::filesystem::path ModelRendererComponent::ResolveModelFilePath() const {
	if (modelAssetId_.empty() && modelPath_.empty()) {
		return {};
	}
	return GetAssetResolver().ResolveAssetPath(modelAssetId_, NormalizeModelPath(modelPath_));
}

void ModelRendererComponent::SetMaterialAsset(const std::string& materialAssetId, const std::string& materialPath) {
	materialAssetId_ = materialAssetId;
	materialPath_ = materialPath;

	std::filesystem::path resolvedPath = ResolveMaterialPath();
	if (!resolvedPath.empty()) {
		std::string message;
		MaterialAssetData loadedMaterial{};
		if (MaterialAsset::Load(resolvedPath, loadedMaterial, message)) {
			material_ = loadedMaterial;
		}
	}

	ApplyMaterialToModel();
}

void ModelRendererComponent::SetMaterialPath(const std::string& materialPath) {
	if (materialPath.empty()) {
		return;
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", materialPath);
	std::string assetId = assetDatabase.GetOrCreateAssetId(resolvedPath);
	std::string storedPath = assetDatabase.MakeProjectRelativePath(resolvedPath);
	SetMaterialAsset(assetId, storedPath);
}

bool ModelRendererComponent::ApplyMaterialAsset(const std::string& materialPath) {
	SetMaterialPath(materialPath);
	return true;
}

bool ModelRendererComponent::UsesMaterialAsset(const std::string& materialPath) const {
	if (materialAssetId_.empty() && materialPath_.empty()) {
		return false;
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	std::filesystem::path targetPath = assetDatabase.ResolveAssetPath("", materialPath);
	std::filesystem::path currentPath = ResolveMaterialPath();
	if (targetPath.empty() || currentPath.empty()) {
		return false;
	}

	return targetPath.lexically_normal() == currentPath.lexically_normal();
}

void ModelRendererComponent::SetSubMeshUVTransform(size_t subMeshIndex, const Vector2& offset, const Vector2& scale, float rotation) {
	if (subMeshIndex >= subMeshUVTransforms_.size()) {
		subMeshUVTransforms_.resize(subMeshIndex + 1);
	}
	subMeshUVTransforms_[subMeshIndex] = {offset, scale, rotation};
	if (model_) {
		model_->SetSubMeshUVTransform(subMeshIndex, offset, scale, rotation);
	}
}

void ModelRendererComponent::Update() {
	if (!model_) {
		return;
	}

	// 色の上書き(衝撃波の減衰など)。個体ごとの濃さなのでMaterialアセットには触らない。
	if (colorOverrideActive_) {
		model_->SetColor(colorOverride_);
	}

	// ランタイム上書き(被弾フラッシュ等)が有効なら最優先で適用する(Emissionチェックに関係なく光らせる)。
	if (emissiveOverrideActive_) {
		model_->SetEmissive(emissiveOverrideColor_, emissiveOverrideIntensity_, true);
		model_->SetEmissiveBloom(material_.bloomIntensity, material_.bloomThreshold, material_.bloomSoftKnee);
		return;
	}

	// AnimatorComponentがmaterial_のemissiveチャンネルを書き換えるため、毎フレームモデルへ反映する。
	// 適用条件はApplyMaterialToModelの色/テクスチャと同じ(パーツ別マテリアル尊重時は触らない)。
	bool hasMaterialAsset = !materialAssetId_.empty() || !materialPath_.empty();
	bool isMultiMesh = model_->GetSubMeshCount() > 1;
	if (hasMaterialAsset || !isMultiMesh) {
		model_->SetEmissive(material_.emissiveColor, material_.emissiveIntensity, material_.emissiveEnabled);
		model_->SetEmissiveBloom(material_.bloomIntensity, material_.bloomThreshold, material_.bloomSoftKnee);
	}
}

void ModelRendererComponent::SetTextureOverride(uint32_t textureIndex) {
	if (!model_) {
		return;
	}
	model_->SetTexture(textureIndex);
}

void ModelRendererComponent::SetEmissiveTextureOverride(uint32_t textureIndex) {
	if (!model_) {
		return;
	}
	model_->SetEmissiveTexture(textureIndex);
}

void ModelRendererComponent::SetUVTilingOverride(float tiling) { SetUVTransformOverride({0.0f, 0.0f}, {tiling, tiling}, 0.0f); }

void ModelRendererComponent::SetTriplanarScaleOverride(float scale) {
	if (!model_) {
		return;
	}
	model_->SetTriplanarScale(scale);
}

void ModelRendererComponent::SetUVTransformOverride(const Vector2& offset, const Vector2& scale, float rotation) {
	if (!model_) {
		return;
	}
	model_->SetUVTransform(offset, scale, rotation);
}

void ModelRendererComponent::CollectAnimatableChannels(std::vector<AnimatableChannel>& channels) {
	// 発光の明滅(ランプ/チャージ演出等)をAnimationWindowのカーブで作れるように公開する。
	channels.push_back({"emissiveIntensity", &material_.emissiveIntensity, nullptr});
	channels.push_back({"emissiveColor.r", &material_.emissiveColor.x, nullptr});
	channels.push_back({"emissiveColor.g", &material_.emissiveColor.y, nullptr});
	channels.push_back({"emissiveColor.b", &material_.emissiveColor.z, nullptr});
}

void ModelRendererComponent::Draw() {
	GameObject* owner = GetOwner();
	if (!owner || !model_ || !camera_) {
		return;
	}

	if (billboardEnabled_) {
		// 親階層に追従し、カメラを向くビルボード行列でmatWorld_を更新する。
		// scale_を直接いじらないため、毎フレーム累積してNaN化する不具合は起きない。
		owner->GetTransform().UpdateBillboardMatrix(*camera_, cameraLocalZ_, billboardFaceMode_ == 1);
	} else {
		owner->GetTransform().UpdateMatrix(*camera_);
	}

	model_->SetDoubleSided(doubleSided_);
	model_->SetShaderUserValue(shaderUserValue_);
	model_->Draw(owner->GetTransform(), *camera_);
}

void ModelRendererComponent::DrawInspector() {
#ifdef USE_IMGUI
	int primitiveIndex = 0;
	switch (primitive_) {
	case KujataEngine::ModelRendererComponent::PrimitiveType::Custom:
		break;
	case KujataEngine::ModelRendererComponent::PrimitiveType::Cube:
		primitiveIndex = 1;
		break;
	case KujataEngine::ModelRendererComponent::PrimitiveType::Sphere:
		primitiveIndex = 2;
		break;
	case KujataEngine::ModelRendererComponent::PrimitiveType::Capsule:
		primitiveIndex = 3;
		break;
	case KujataEngine::ModelRendererComponent::PrimitiveType::Plane:
		primitiveIndex = 4;
		break;
	case KujataEngine::ModelRendererComponent::PrimitiveType::Ring:
		primitiveIndex = 5;
		break;
	case KujataEngine::ModelRendererComponent::PrimitiveType::Model:
		primitiveIndex = 6;
		break;
	default:
		break;
	}

	const char* primitiveItems[] = {"Custom", "Cube", "Sphere", "Capsule", "Plane", "Ring", "Model"};
	constexpr int primitiveItemCount = static_cast<int>(sizeof(primitiveItems) / sizeof(primitiveItems[0]));
	if (InspectorUI::Combo("Primitive", &primitiveIndex, primitiveItems, primitiveItemCount)) {
		PrimitiveType selectedPrimitive = PrimitiveType::Custom;
		if (primitiveIndex == 1) {
			selectedPrimitive = PrimitiveType::Cube;
		} else if (primitiveIndex == 2) {
			selectedPrimitive = PrimitiveType::Sphere;
		} else if (primitiveIndex == 3) {
			selectedPrimitive = PrimitiveType::Capsule;
		} else if (primitiveIndex == 4) {
			selectedPrimitive = PrimitiveType::Plane;
		} else if (primitiveIndex == 5) {
			selectedPrimitive = PrimitiveType::Ring;
		} else if (primitiveIndex == 6) {
			selectedPrimitive = PrimitiveType::Model;
		}
		SetPrimitive(selectedPrimitive, GetBaseColorTexturePath(material_));
	}

	// Texture/ColorはMaterialだけで管理するため、この欄には出さない(Material Inspectorで編集する)。

	// ModelはProject(エクスプローラー)からModelファイルをドロップして差し替える。
	std::array<char, 256> modelDropBuffer{};
	if (InspectorUI::ModelAssetField("Model", modelPath_.c_str(), modelDropBuffer.data(), modelDropBuffer.size())) {
		std::string droppedPath = modelDropBuffer.data();
		if (!droppedPath.empty()) {
			SetModelPath(droppedPath);
			SetPrimitive(PrimitiveType::Model, GetBaseColorTexturePath(material_));
		}
	}

	InspectorUI::TextUnformatted("Material");
	if (materialPath_.empty()) {
		InspectorUI::TextDisabled("Embedded Material");
	} else {
		InspectorUI::TextUnformatted(materialPath_.c_str());
	}

	// サブメッシュ別UVトランスフォーム(MultiMesh時のみ)。マテリアル/テクスチャは.mtl由来のまま、
	// パーツごとにUVだけを調整できる。
	if (model_ && model_->GetSubMeshCount() > 1) {
		InspectorUI::TextUnformatted("--- Sub Mesh UV Transform ---");
		size_t subMeshCount = model_->GetSubMeshCount();
		if (subMeshUVTransforms_.size() < subMeshCount) {
			subMeshUVTransforms_.resize(subMeshCount);
		}
		for (size_t index = 0; index < subMeshCount; ++index) {
			SubMeshUVTransform& uvTransform = subMeshUVTransforms_[index];
			std::string indexText = std::to_string(index);
			bool uvChanged = false;
			uvChanged |= InspectorUI::DragFloat2(("UV Offset " + indexText).c_str(), &uvTransform.offset.x, 0.01f);
			uvChanged |= InspectorUI::DragFloat2(("UV Scale " + indexText).c_str(), &uvTransform.scale.x, 0.01f);
			uvChanged |= InspectorUI::DragFloat(("UV Rotation " + indexText).c_str(), &uvTransform.rotation, 0.01f);
			if (uvChanged) {
				model_->SetSubMeshUVTransform(index, uvTransform.offset, uvTransform.scale, uvTransform.rotation);
			}
		}
	}

	InspectorUI::Checkbox("Double Sided", &doubleSided_);

	InspectorUI::DragFloat("Shader User Value", &shaderUserValue_, 0.01f);
	InspectorUI::ItemTooltip("自作シェーダーへ渡す、このオブジェクトだけの値(gShaderParams.userValue)。同じマテリアルでも1つずつ見た目を変えたいときに使う(泡の色相のずれなど)。");


	InspectorUI::TextUnformatted("--- Billboard ---");
	InspectorUI::Checkbox("Billboard Enabled", &billboardEnabled_);

	if (billboardEnabled_) {
		int faceModeIndex = billboardFaceMode_;
		const char* faceModeItems[] = {"Front", "Back"};
		if (InspectorUI::Combo("Face Mode", &faceModeIndex, faceModeItems, 2)) {
			billboardFaceMode_ = faceModeIndex;
		}

		// カメラの視線方向へのオフセット量。正で手前、負で奥。0で親位置のまま。
		InspectorUI::DragFloat("Camera Local Z", &cameraLocalZ_, 0.01f, -5.0f, 5.0f);
	}
#endif // USE_IMGUI
}

void ModelRendererComponent::WriteJson(nlohmann::json& json) const {
	json["primitive"] = GetPrimitiveName();
	json["modelAssetId"] = modelAssetId_;
	json["modelPath"] = modelPath_;
	json["materialAssetId"] = materialAssetId_;
	json["materialPath"] = materialPath_;
	json["billboardEnabled"] = billboardEnabled_;
	json["doubleSided"] = doubleSided_;
	json["shaderUserValue"] = shaderUserValue_;
	json["billboardFaceMode"] = billboardFaceMode_;
	json["cameraLocalZ"] = cameraLocalZ_;

	nlohmann::json materialJson;
	MaterialAsset::WriteJsonObject(materialJson, material_);
	json["material"] = materialJson;

	// サブメッシュ別UVトランスフォーム(既定値のみなら省略してJSONを汚さない)。
	bool hasNonIdentityUVTransform = false;
	for (const SubMeshUVTransform& uvTransform : subMeshUVTransforms_) {
		if (!uvTransform.IsIdentity()) {
			hasNonIdentityUVTransform = true;
			break;
		}
	}
	if (hasNonIdentityUVTransform) {
		nlohmann::json uvTransformsJson = nlohmann::json::array();
		for (const SubMeshUVTransform& uvTransform : subMeshUVTransforms_) {
			nlohmann::json uvJson;
			uvJson["offset"] = {uvTransform.offset.x, uvTransform.offset.y};
			uvJson["scale"] = {uvTransform.scale.x, uvTransform.scale.y};
			uvJson["rotation"] = uvTransform.rotation;
			uvTransformsJson.push_back(uvJson);
		}
		json["subMeshUVTransforms"] = uvTransformsJson;
	}
}

void ModelRendererComponent::ReadJson(const nlohmann::json& json) {
	std::string primitiveName = ReadString(json, "primitive", GetPrimitiveName());
	modelAssetId_ = ReadString(json, "modelAssetId", "");
	std::string storedModelPath = ReadString(json, "modelPath", modelPath_);
	if (modelAssetId_.empty()) {
		// 旧データ(ID無し・名前だけの参照を含む): 規約パスへ展開しIDを補完する(次回保存時に永続化)。
		SetModelPath(storedModelPath);
	} else {
		modelPath_ = storedModelPath;
	}
	materialAssetId_ = ReadString(json, "materialAssetId", materialAssetId_);
	materialPath_ = ReadString(json, "materialPath", materialPath_);

	if (json.contains("billboardEnabled") && json.at("billboardEnabled").is_boolean()) {
		billboardEnabled_ = json.at("billboardEnabled").get<bool>();
	}
	if (json.contains("shaderUserValue") && json.at("shaderUserValue").is_number()) {
		shaderUserValue_ = json.at("shaderUserValue").get<float>();
	}
	if (json.contains("doubleSided") && json.at("doubleSided").is_boolean()) {
		doubleSided_ = json.at("doubleSided").get<bool>();
	}

	if (json.contains("billboardFaceMode") && json.at("billboardFaceMode").is_number_integer()) {
		billboardFaceMode_ = json.at("billboardFaceMode").get<int>();
	}

	if (json.contains("cameraLocalZ") && json.at("cameraLocalZ").is_number()) {
		cameraLocalZ_ = json.at("cameraLocalZ").get<float>();
	}

	if (json.contains("material") && json.at("material").is_object()) {
		material_ = MaterialAsset::ReadJsonObject(json.at("material"), material_);
	} else {
		// 旧シーン互換: Texture/ColorがMaterialへ移行する前の "texture" を取り込む。
		std::string texturePath = ReadString(json, "texture", GetBaseColorTexturePath(material_));
		SetBaseColorTexturePath(material_, texturePath);
	}

	subMeshUVTransforms_.clear();
	if (json.contains("subMeshUVTransforms") && json.at("subMeshUVTransforms").is_array()) {
		for (const nlohmann::json& uvJson : json.at("subMeshUVTransforms")) {
			SubMeshUVTransform uvTransform;
			if (uvJson.is_object()) {
				if (uvJson.contains("offset") && uvJson.at("offset").is_array() && uvJson.at("offset").size() >= 2) {
					uvTransform.offset = {uvJson.at("offset")[0].get<float>(), uvJson.at("offset")[1].get<float>()};
				}
				if (uvJson.contains("scale") && uvJson.at("scale").is_array() && uvJson.at("scale").size() >= 2) {
					uvTransform.scale = {uvJson.at("scale")[0].get<float>(), uvJson.at("scale")[1].get<float>()};
				}
				if (uvJson.contains("rotation") && uvJson.at("rotation").is_number()) {
					uvTransform.rotation = uvJson.at("rotation").get<float>();
				}
			}
			subMeshUVTransforms_.push_back(uvTransform);
		}
	}

	SetPrimitive(ReadPrimitiveType(primitiveName), GetBaseColorTexturePath(material_));
}

void ModelRendererComponent::RebuildPrimitiveModel() {
	MaterialAssetData activeMaterial = GetActiveMaterial();
	material_ = activeMaterial;
	std::string texturePath = MaterialAsset::ResolveTexturePath(activeMaterial, MaterialTextureSlot::BaseColor).string();

	if (primitive_ == PrimitiveType::Cube) {
		model_.reset(Model::CreateCube(texturePath, ShaderModel::kBlingPhongReflection));
		ApplyMaterialToModel();
		return;
	}

	if (primitive_ == PrimitiveType::Sphere) {
		model_.reset(Model::CreateSphere(texturePath, ShaderModel::kBlingPhongReflection));
		ApplyMaterialToModel();
		return;
	}

	if (primitive_ == PrimitiveType::Capsule) {
		model_.reset(Model::CreateCapsule(texturePath, ShaderModel::kBlingPhongReflection));
		ApplyMaterialToModel();
		return;
	}

	if (primitive_ == PrimitiveType::Plane) {
		model_.reset(Model::CreatePlane(texturePath, ShaderModel::kBlingPhongReflection));
		ApplyMaterialToModel();
		return;
	}

	if (primitive_ == PrimitiveType::Ring) {
		// 輪は土煙や魔法陣など「面で見せる」用途なので、既定はライティング無し(Unlit)。
		// マテリアルのShader Modelで上書きされる。
		model_.reset(Model::CreateRing(texturePath, ShaderModel::kNone));
		ApplyMaterialToModel();
		return;
	}

	if (primitive_ == PrimitiveType::Model) {
		// assetId優先で現在のパスへ解決してから読み込む(移動済みアセットは.meta経由で新パスが返る)。
		std::filesystem::path modelFilePath = ResolveModelFilePath();
		Model* loadedModel = nullptr;
		if (!modelFilePath.empty()) {
			loadedModel = Model::TryCreateFromFile(modelFilePath.generic_string(), ShaderModel::kBlingPhongReflection);
		}
		model_.reset(loadedModel);
		ApplyMaterialToModel();
	}
}

void ModelRendererComponent::ApplyMaterialToModel() {
	if (!model_) {
		return;
	}

	// シェーダー方式(ライティング)はMaterialの選択を常に全サブメッシュへ反映する。
	model_->SetShaderModel(static_cast<ShaderModel>(material_.shaderModel));
	// 合成方法と深度書き込みもMaterialの選択をそのまま全サブメッシュへ反映する。
	model_->SetBlendMode(static_cast<BlendMode>(material_.blendMode));
	model_->SetDepthWrite(material_.depthWrite);
	// トゥーン・ローポリ調もシェーダー方式と同じく、常に全サブメッシュへ反映する(パーツ別マテリアルでも見た目をそろえる)。
	model_->SetStylize(material_.toonSteps, material_.toonSmoothness, material_.flatShading, material_.pointSampling);
	// 自作シェーダーとそのパラメータも、シェーダー方式と同じく常に全サブメッシュへ反映する。
	model_->SetCustomShader(MaterialAsset::ResolveCustomShader(material_));
	model_->SetShaderParams(material_.shaderParams);

	// 色/テクスチャの上書き方針(方式B):
	//  - Material Assetを参照している場合は、Unityの共有Material同様に全サブメッシュを一括上書き。
	//  - 参照なし(埋め込み)でも、単一メッシュ(プリミティブや単一OBJ)は従来通りbaseColor/textureを適用。
	//  - 参照なし かつ MultiMesh のときだけ、モデル側のパーツ別マテリアル/テクスチャを尊重する。
	bool hasMaterialAsset = !materialAssetId_.empty() || !materialPath_.empty();
	bool isMultiMesh = model_->GetSubMeshCount() > 1;
	if (hasMaterialAsset || !isMultiMesh) {
		model_->SetColor(material_.baseColor);
		model_->SetTexture(MaterialAsset::ResolveTextureIndex(material_, MaterialTextureSlot::BaseColor));
		model_->SetEmissiveTexture(MaterialAsset::ResolveTextureIndex(material_, MaterialTextureSlot::Emissive));
		model_->SetEmissive(material_.emissiveColor, material_.emissiveIntensity, material_.emissiveEnabled);
		model_->SetEmissiveBloom(material_.bloomIntensity, material_.bloomThreshold, material_.bloomSoftKnee);
		model_->SetUVTransform(material_.uvOffset, material_.uvScale, material_.uvRotation);
	}

	// サブメッシュ別UVトランスフォームは最後に適用し、全体Materialの一括UVより優先させる。
	ApplySubMeshUVTransforms();
}

void ModelRendererComponent::ApplySubMeshUVTransforms() {
	if (!model_) {
		return;
	}

	size_t subMeshCount = model_->GetSubMeshCount();
	for (size_t index = 0; index < subMeshUVTransforms_.size() && index < subMeshCount; ++index) {
		const SubMeshUVTransform& uvTransform = subMeshUVTransforms_[index];
		// 既定値のパーツは触らない(全体Materialが設定した一括UVを上書きしないため)。
		if (uvTransform.IsIdentity()) {
			continue;
		}
		model_->SetSubMeshUVTransform(index, uvTransform.offset, uvTransform.scale, uvTransform.rotation);
	}
}

MaterialAssetData ModelRendererComponent::GetActiveMaterial() const {
	std::filesystem::path resolvedPath = ResolveMaterialPath();
	if (!resolvedPath.empty()) {
		std::string message;
		MaterialAssetData loadedMaterial{};
		if (MaterialAsset::Load(resolvedPath, loadedMaterial, message)) {
			return loadedMaterial;
		}
	}

	return material_;
}

std::filesystem::path ModelRendererComponent::ResolveMaterialPath() const {
	if (materialAssetId_.empty() && materialPath_.empty()) {
		return {};
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	return assetDatabase.ResolveAssetPath(materialAssetId_, materialPath_);
}

const char* ModelRendererComponent::GetPrimitiveName() const {
	if (primitive_ == PrimitiveType::Cube) {
		return "Cube";
	}

	if (primitive_ == PrimitiveType::Sphere) {
		return "Sphere";
	}

	if (primitive_ == PrimitiveType::Capsule) {
		return "Capsule";
	}

	if (primitive_ == PrimitiveType::Plane) {
		return "Plane";
	}

	if (primitive_ == PrimitiveType::Ring) {
		return "Ring";
	}

	if (primitive_ == PrimitiveType::Model) {
		return "Model";
	}

	return "Custom";
}

} // namespace KujataEngine
