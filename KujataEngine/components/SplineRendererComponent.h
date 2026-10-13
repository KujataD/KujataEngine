#pragma once

#include "../3d/Model.h"
#include "../base/DirectXCommon.h"
#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/IMaterialTarget.h"
#include "../scene/SerializedFieldRegistry.h"
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {

class Camera;

/// <summary>
/// 点の列をなめらかな曲線(Catmull-Rom)でつなぎ、その曲線に沿ったチューブ(筒)かリボン(帯)を描く。
/// 放水の水流(水弾をつなぐ)・紫電(泡と泡をつなぐ)・ロープ・レーザーなど、**毎フレーム形が変わる線状の物**に使う。
///
/// 点の渡し方は2通り:
///   - ゲームのコードから SetPoints で渡す(ワールド座標)。水弾の位置を毎フレーム渡すなど
///   - 渡していなければ、**子オブジェクトの位置**を並び順に点として使う(エディタで形を作って確かめるとき)
///
/// 仕組み:
///   1. 点と点の間を Subdivisions 個に分けて、曲線上の位置を求める(点を必ず通る Catmull-Rom。間隔が不揃いでも暴れにくい centripetal 版)
///   2. チューブ: 曲線に沿って、断面の円(Sides 角形)を並べてつなぐ。断面がねじれないよう、向きを前の断面から少しずつ回して決める
///      リボン: 曲線に沿って、カメラを向く帯を張る(TrailRendererComponent と同じ作り方)
///   3. 頂点をワールド座標のまま作り、単位行列で描く(このオブジェクト自身の Transform は形に影響しない)
///
/// UV は u=曲線に沿った向き(0=最初の点)、v=断面の周(チューブ)または幅(リボン)の向き(0〜1)。
/// UV Per Unit が 0 なら u は全体で 0〜1、0 より大きければ 1 ユニットあたりその回数だけ繰り返す(流れる模様に使う)。
/// 自作シェーダーには曲線の全長も gShaderParams.curveLength で渡すので、UV Per Unit = 1(u = 根元からの距離)にしておけば、
/// 模様は距離で(長さが変わっても速さと間隔が変わらない)、先端の処理は u / curveLength で(根元0〜先端1)決められる。
/// </summary>
class KUJATA_API SplineRendererComponent : public Component, public IMaterialTarget {
public:
	enum class Shape {
		Tube = 0,   // 筒。どこから見ても太さが同じ(水流など)
		Ribbon = 1, // カメラを向く帯。細い物・光る物に(紫電など)
	};

	const char* GetTypeName() const override { return "SplineRendererComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Draw() override;

	/// <summary>深度を書かないマテリアル(半透明・加算)なら true。Scene が不透明物の後に遠い順で描く。</summary>
	bool IsTransparentDraw() const override { return transparent_; }

	bool ApplyMaterialAsset(const std::string& materialPath) override;
	bool UsesMaterialAsset(const std::string& materialPath) const override;

	/// <summary>描画に使うカメラ。Sceneが各ビューの描画前に配る(ModelRendererComponentと同じ扱い)。</summary>
	void SetRenderCamera(const Camera* camera) override { camera_ = camera; }

	/// <summary>
	/// 曲線が通る点をワールド座標で渡す(2点以上で描かれる)。widthScales を渡すと、点ごとに太さを掛ける(点と同じ数)。
	/// 渡した点は ClearPoints を呼ぶまで使われ、子オブジェクトの位置は使わなくなる。
	/// </summary>
	void SetPoints(const std::vector<Vector3>& points, const std::vector<float>* widthScales = nullptr);

	/// <summary>SetPoints で渡した点を捨てる(子オブジェクトの位置に戻る。子が無ければ何も描かない)。</summary>
	void ClearPoints();

	/// <summary>
	/// 自作シェーダーへ渡す、この線だけの値(gShaderParams.userValue。既定0)。マテリアルの Shader Params は同じマテリアルの線で
	/// 共通なので、線ごとに見た目を変えたいとき(例: 先端が物に当たっている線だけ先端を欠けさせない)に使う。意味は各シェーダーが決める。
	/// </summary>
	void SetShaderUserValue(float value) { shaderUserValue_ = value; }
	float GetShaderUserValue() const { return shaderUserValue_; }

	/// <summary>
	/// 自作シェーダーへ渡す、この線だけの float4(gShaderParams.objectParams[0..11])。先頭から count 個。
	/// マテリアルを作らずに、コンポーネントの設定だけで見た目を決めたいときに使う(例: WaterSprayComponent の水流)。
	/// </summary>
	void SetShaderObjectParams(const Vector4* values, size_t count);

	/// <summary>
	/// Material を使わないときの自作シェーダー(engine:Custom/Water.hlsl など)。Material を設定すればそちらが優先。
	/// </summary>
	void SetShaderPath(const std::string& shaderPath);

	/// <summary>今使っている点の数(SetPoints の点、または子オブジェクトの数)。</summary>
	size_t GetPointCount() const { return controlPoints_.size(); }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_INT_NAMED_TIP(shape_, "Shape", 1.0f, 0, 1, "0=Tube(筒。水流など) / 1=Ribbon(カメラを向く帯。紫電など)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(startWidth_, "Start Width", 0.01f, 0.0f, 100.0f, "最初の点での太さ(チューブは直径、リボンは幅)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(endWidth_, "End Width", 0.01f, 0.0f, 100.0f, "最後の点での太さ。途中はなめらかに変わる。");
		KUJATA_REGISTER_INT_NAMED_TIP(sides_, "Sides", 1.0f, 3, 32, "チューブの断面の角の数。少ないほどローポリ(6前後がおすすめ)。");
		KUJATA_REGISTER_INT_NAMED_TIP(subdivisions_, "Subdivisions", 1.0f, 0, 16,
		    "点と点の間を何分割して曲線にするか。0で折れ線(紫電のカクカクした線はこれ)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(caps_, "Caps", "チューブの両端をふさぐ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(uvPerUnit_, "UV Per Unit", 0.01f, 0.0f, 100.0f,
		    "u 方向の繰り返し。0で全体が 0〜1、0より大きいと 1 ユニットあたりその回数(模様を流すとき)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(materialPath_, "Material", "描くのに使うマテリアル(Project からドラッグでも設定できる)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(shaderPath_, "Shader",
		    "Material を使わないときの自作シェーダー(例: engine:Custom/Water.hlsl)。Material を設定すればそちらが優先。");
	}

	KUJATA_FIELD_INT(shape_, 0);
	KUJATA_FIELD_FLOAT(startWidth_, 0.3f);
	KUJATA_FIELD_FLOAT(endWidth_, 0.3f);
	KUJATA_FIELD_INT(sides_, 6);
	KUJATA_FIELD_INT(subdivisions_, 4);
	KUJATA_FIELD_BOOL(caps_, true);
	KUJATA_FIELD_FLOAT(uvPerUnit_, 0.0f);
	KUJATA_FIELD_STRING(materialPath_, "");
	KUJATA_FIELD_STRING(shaderPath_, "");

	// 曲線上の1点(組み立ての途中で使う)。
	struct Sample {
		Vector3 position;
		Vector3 tangent;
		float width = 0.0f;
		float u = 0.0f;
	};

	/// <summary>今フレームの点(SetPoints の点、または子オブジェクトの位置)を controlPoints_ へ集める。</summary>
	void GatherControlPoints();
	/// <summary>点の列から曲線上の点を並べる。</summary>
	void BuildSamples();
	/// <summary>チューブ/リボンの頂点を vertices_ へ組み立てる。</summary>
	void BuildTube();
	void BuildRibbon(const Camera& camera);
	/// <summary>マテリアルを読み直して、ビューごとのモデルへ反映する。</summary>
	void ApplyMaterial(Model& model);
	/// <summary>このビューのモデルを、頂点数が足りるように用意する。</summary>
	Model* EnsureModel(uint32_t viewIndex, size_t vertexCount);

	// --- 実行時 ---
	const Camera* camera_ = nullptr;
	// SetPoints で渡された点(空で codePointsSet_ が false なら子オブジェクトの位置を使う)。
	std::vector<Vector3> codePoints_;
	std::vector<float> codeWidthScales_;
	bool codePointsSet_ = false;
	std::vector<Vector3> controlPoints_;
	std::vector<float> controlWidthScales_;
	std::vector<Sample> samples_;
	std::vector<VertexData> vertices_;
	// ビュー(Scene/Game)ごとのモデル。リボンはカメラを向くので、ビューごとに形が違う。
	std::unique_ptr<Model> models_[DirectXCommon::kRenderViewCount];
	uint64_t builtFrame_[DirectXCommon::kRenderViewCount] = {~0ull, ~0ull};
	// モデルへ反映したマテリアルのパス(変わったら読み直す)。
	std::string appliedMaterialPath_[DirectXCommon::kRenderViewCount];
	bool materialDirty_[DirectXCommon::kRenderViewCount] = {true, true};
	bool transparent_ = false;
	// 曲線の全長(自作シェーダーへ gShaderParams.curveLength として渡す)。
	float curveLength_ = 0.0f;
	// 自作シェーダーへ gShaderParams.userValue として渡す値。
	float shaderUserValue_ = 0.0f;
	// 自作シェーダーへ gShaderParams.objectParams として渡す値(コンポーネントから毎フレーム入れる)。
	Vector4 shaderObjectParams_[12] = {};
	size_t shaderObjectParamCount_ = 0;
	// ApplyMaterial したときの Shader(変わったら読み直す)。
	std::string appliedShaderPath_;
	// 頂点は既にワールド座標なので、描画は常に単位行列で行う。
	WorldTransform identityTransform_;
	bool identityReady_ = false;
};

} // namespace KujataEngine
