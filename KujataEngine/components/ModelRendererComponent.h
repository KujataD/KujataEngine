#pragma once

#include "../assets/MaterialAsset.h"
#include "../scene/Component.h"
#include "../scene/IMaterialTarget.h"
#include "../scene/IRaycastTarget.h"
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {

class Camera;
class Model;

/// <summary>
/// ModelをGameObjectのTransformで描画するComponent
/// </summary>
/// <remarks>ゲームDLL(GameModule)からランタイム生成できるようKUJATA_APIでエクスポートする。</remarks>
class KUJATA_API ModelRendererComponent : public Component, public IRaycastTarget, public IMaterialTarget {
public:
	enum class PrimitiveType {
		Custom,
		Cube,
		Sphere,
		Capsule,
		Plane,
		// XZ平面に寝た円環。衝撃波・魔法陣・攻撃予告円のような「地面の輪」用。
		Ring,
		Model,
	};

	ModelRendererComponent();
	explicit ModelRendererComponent(const Camera* camera);
	ModelRendererComponent(std::unique_ptr<Model> model, const Camera* camera);
	~ModelRendererComponent() override;

	const char* GetTypeName() const override { return "ModelRendererComponent"; }
	bool AllowMultiple() const override { return false; }


	void SetModel(std::unique_ptr<Model> model);

	void SetRenderCamera(const Camera* camera) override;

	/// <summary>
	/// Builtin形状を設定します。textureFilePathは、Material Assetを参照していない場合のみ
	/// 埋め込みMaterialのBaseColorへ反映されます(Texture/ColorはMaterialだけで管理します)。
	/// </summary>
	void SetPrimitive(PrimitiveType primitive, const std::string& textureFilePath);

	/// <summary>
	/// 表示するModelをパス(プロジェクト相対)で設定します。存在するファイルならassetIdを補完し、
	/// 以後のリネーム/移動に追従します。旧形式の名前だけの指定("bishop")は
	/// "Resources/&lt;name&gt;/&lt;name&gt;.obj" 規約でパスへ展開されます。
	/// </summary>
	void SetModelPath(const std::string& modelPathOrName);

	const std::string& GetModelPath() const { return modelPath_; }

	void SetMaterialAsset(const std::string& materialAssetId, const std::string& materialPath);

	void SetMaterialPath(const std::string& materialPath);

	/// <summary>
	/// ProjectWindowからドロップされたMaterial Assetを適用します。
	/// </summary>
	bool ApplyMaterialAsset(const std::string& materialPath) override;

	bool UsesMaterialAsset(const std::string& materialPath) const override;

	/// <summary>
	/// サブメッシュ別UVトランスフォーム。パーツのマテリアル/テクスチャは.mtl由来のものを維持したまま、
	/// UVだけを個別に調整する(MultiMeshモデル用)。
	/// </summary>
	void SetSubMeshUVTransform(size_t subMeshIndex, const Vector2& offset, const Vector2& scale, float rotation);

	PrimitiveType GetPrimitive() const { return primitive_; }

	const std::string& GetMaterialAssetId() const { return materialAssetId_; }

	const std::string& GetMaterialPath() const { return materialPath_; }

	const Model* GetModel() const { return model_.get(); }

	const Model* GetRayCastModel() const override { return model_.get(); }

	void Update() override;

	void Draw() override;

	/// <summary>深度を書かないマテリアル(半透明・加算)なら true。Scene が不透明物の後に遠い順で描く。</summary>
	bool IsTransparentDraw() const override { return !material_.depthWrite; }

	/// <summary>
	/// 自作シェーダーへ渡す、このオブジェクトだけの値(gShaderParams.userValue)。同じマテリアルでも1つずつ見た目を変えたいときに使う
	/// (例: 泡のシェーダーの色相のずれ)。意味は各シェーダーが決める。
	/// </summary>
	void SetShaderUserValue(float value) { shaderUserValue_ = value; }
	float GetShaderUserValue() const { return shaderUserValue_; }

	/// <summary>
	/// アニメーション可能チャンネル(emissiveIntensity/emissiveColor.r,g,b)を公開する。
	/// AnimationWindowでカーブを打つと発光の明滅などが作れる。
	/// </summary>
	void CollectAnimatableChannels(std::vector<AnimatableChannel>& channels) override;

	/// <summary>
	/// ランタイム発光上書き(被弾フラッシュ等の演出用)。マテリアルアセットには影響しない。
	/// Clearするまで毎フレームこの値がモデルへ適用される。
	/// </summary>
	void SetEmissiveOverride(const Vector3& color, float intensity) {
		emissiveOverrideColor_ = color;
		emissiveOverrideIntensity_ = intensity;
		emissiveOverrideActive_ = true;
	}
	void ClearEmissiveOverride() { emissiveOverrideActive_ = false; }
	bool HasEmissiveOverride() const { return emissiveOverrideActive_; }

	/// <summary>
	/// ランタイム色上書き(衝撃波の減衰など、同じMaterialアセットを使う個体ごとに濃さを変えたい場合)。
	/// **Materialアセット自体は書き換えない。** 上書きしないと濃さがアセット共有になり、
	/// 同時に複数出ている演出が互いの濃さを奪い合う。
	/// </summary>
	void SetColorOverride(const Vector4& color) {
		colorOverride_ = color;
		colorOverrideActive_ = true;
	}
	void ClearColorOverride() { colorOverrideActive_ = false; }

	/// <summary>
	/// 現在のModelのテクスチャを、指定SRVインデックスへ直接差し替える(ランタイム上書き)。
	/// Materialアセットのpath解決を経由しないので、NoiseTextureComponentのような
	/// メモリ生成テクスチャ(実ファイルを持たない)を貼るのに使う。毎フレーム呼ぶ必要はない
	/// (Model::Updateはテクスチャを触らないので、一度呼べば次のマテリアル変更まで保持される)。
	/// </summary>
	void SetTextureOverride(uint32_t textureIndex);

	/// <summary>
	/// エミッションマップ(自己発光の分布)を、指定SRVインデックスへ直接差し替える(ランタイム上書き)。
	/// **これを指定しないと発光は面全体へ一様に乗る**ので、霧のように模様のある物を光らせると
	/// 模様が白飛びで潰れる。BaseColorと同じ手続きテクスチャを入れれば、濃い所だけが光る。
	/// </summary>
	void SetEmissiveTextureOverride(uint32_t textureIndex);

	/// <summary>
	/// UVタイリング(繰り返し回数)を直接差し替える(ランタイム上書き)。
	/// プリミティブのUVは常に0..1固定でTransform.scaleでは伸びないため、
	/// 巨大なオブジェクト(地面など)に手続きテクスチャを敷くと模様が1枚だけ間延びして見える。
	/// tilingを1より大きくすると、そのぶんテクスチャが繰り返し敷き詰められる。
	/// </summary>
	void SetUVTilingOverride(float tiling);

	/// <summary>
	/// ワールド座標でのタイリング(トライプラナー)を差し替える(ランタイム上書き)。
	/// 値は1ワールドユニットあたりの繰り返し数で、0でメッシュのUV貼りへ戻る。
	/// **プリミティブをTransformで引き伸ばすと面ごとに模様の伸び方が変わる**ため、
	/// 手続きテクスチャを箱や球へ貼るときはこちらを使う(SetUVTilingOverrideは面数固定)。
	/// </summary>
	void SetTriplanarScaleOverride(float scale);

	/// <summary>
	/// UVトランスフォーム(スクロール/タイリング/回転)を直接差し替える(ランタイム上書き)。
	/// **毎フレーム動かす演出はこちらを使う**(SetSubMeshUVTransformはInspectorに出る
	/// シリアライズ対象を書き換えてしまい、Play中の値が編集値として見えてしまう)。
	/// 適用順はMaterialと同じ Scale → RotateZ → Translate。
	/// </summary>
	void SetUVTransformOverride(const Vector2& offset, const Vector2& scale, float rotation);
	/// <summary>現在のMaterialのBase Color(上書き前の既定値)。演出側が「既定を基準に薄める」ために使う。</summary>
	const Vector4& GetBaseColor() const { return material_.baseColor; }
	bool HasColorOverride() const { return colorOverrideActive_; }

	void DrawInspector() override;

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

private:
	/// <summary>サブメッシュ別のUVトランスフォーム値。既定(offset 0・scale 1・rotation 0)は「変換なし」。</summary>
	struct SubMeshUVTransform {
		Vector2 offset = {0.0f, 0.0f};
		Vector2 scale = {1.0f, 1.0f};
		float rotation = 0.0f; // ラジアン

		bool IsIdentity() const { return offset.x == 0.0f && offset.y == 0.0f && scale.x == 1.0f && scale.y == 1.0f && rotation == 0.0f; }
	};

	void RebuildPrimitiveModel();
	std::filesystem::path ResolveModelFilePath() const;
	void ApplyMaterialToModel();
	void ApplySubMeshUVTransforms();
	/// <summary>
	/// 参照中のMaterial Assetを読み込み、失敗時はComponent内Materialを返します。
	/// </summary>
	MaterialAssetData GetActiveMaterial() const;
	std::filesystem::path ResolveMaterialPath() const;
	const char* GetPrimitiveName() const;

	std::unique_ptr<Model> model_;
	const Camera* camera_ = nullptr;
	PrimitiveType primitive_ = PrimitiveType::Custom;
	// Modelアセットへの参照。assetId優先・path(プロジェクト相対)はfallback。
	// 旧シーン互換のため名前だけの値("player"等)も保持しうる(使用時に規約パスへ展開)。
	std::string modelAssetId_;
	std::string modelPath_ = "Resources/block/block.obj";
	std::string materialAssetId_;
	std::string materialPath_;
	MaterialAssetData material_ = MaterialAsset::CreateDefault();
	// サブメッシュ別UVトランスフォーム(index=サブメッシュ順)。マテリアル/テクスチャは.mtl由来を維持する。
	std::vector<SubMeshUVTransform> subMeshUVTransforms_;
	bool billboardEnabled_ = false;
	// 両面描画(背面カリングなし)。バリア球など内側からも見せたいものに使う。
	bool doubleSided_ = false;
	// 自作シェーダーへ渡す、このオブジェクトだけの値(gShaderParams.userValue)。
	float shaderUserValue_ = 0.0f;
	int billboardFaceMode_ = 0;
	float cameraLocalZ_ = 1.0f;
	// ランタイム発光上書き(演出用の一時値。シリアライズしない)。
	Vector4 colorOverride_ = {1.0f, 1.0f, 1.0f, 1.0f};
	bool colorOverrideActive_ = false;
	Vector3 emissiveOverrideColor_ = {0.0f, 0.0f, 0.0f};
	float emissiveOverrideIntensity_ = 1.0f;
	bool emissiveOverrideActive_ = false;
};

} // namespace KujataEngine
