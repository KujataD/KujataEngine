#pragma once

#include "../3d/Model.h"
#include "../3d/WorldTransform.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/IMaterialTarget.h"
#include "../scene/SerializedFieldRegistry.h"
#include <memory>
#include <string>

namespace KujataEngine {

class Camera;
class Scene;

/// <summary>
/// 海面。付けるだけで、細かく分割した板(格子)が波打つ海になる(風のタクト / A Short Hike 風。ローポリ・ドット絵向け)。
///
/// 見た目(EngineData/shader/Custom/Ocean.hlsl):
///   - 平らな海の色(陰は Lighting の分だけ)に、ワールド座標に貼った泡の輪と、少しずらした暗い輪を重ねる
///   - 模様を読む位置を sin を重ねた式で揺らし、ゆっくり流す
///   - 奥の不透明物までの距離で、浅瀬の色・透け具合・岸の泡を決める(半透明として描き、不透明物の深度を読む)
///   - 頂点は正弦波を最大4つ足して上下させ、波の山を白くする
///
/// 高さ:
///   - **同じ波の式を C++ でも計算する**(GetSurfaceHeight)。描かれている三角形の上の高さを返すので、見た目の海面とぴったり合う
///   - 波の時間はゲームの時間(Time::GetDeltaTime。ヒットストップ・スローで波も止まる)。Play 中でないときは実時間で動かす
///   - OceanComponent::TryGetSurfaceHeight(scene, x, z, height) … その位置の海面の高さ(海の外なら false)
///   - FloatOnWaterComponent … 付けた物を海面に浮かべる
///
/// Follow Camera を ON にすると、板がカメラの真下へついて来る(果てのない海。遠くは Fog で空に溶かす)。
/// そのときは高さの問い合わせも、どこでも海の上として答える。
/// **回転は Y 軸まわりだけ、拡大は X・Z だけにする**(傾けると波が板に沿わない。波の高さは Wave Height で決める)。
/// </summary>
class KUJATA_API OceanComponent : public Component, public IMaterialTarget {
public:
	static constexpr int kWaveCount = 4;

	const char* GetTypeName() const override { return "OceanComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;
	void Draw() override;

	/// <summary>
	/// 海は常に半透明の側で描く(不透明物を描き終えた後の深度を読んで、岸の泡・浅瀬の色を出すため)。
	/// 深度は書くので、後から描く半透明(しぶき等)とも正しく前後する。ほかの半透明より先に描く。
	/// </summary>
	bool IsTransparentDraw() const override { return true; }
	int GetTransparentQueue() const override { return -100; }

	bool ApplyMaterialAsset(const std::string& materialPath) override;
	bool UsesMaterialAsset(const std::string& materialPath) const override;

	/// <summary>描画に使うカメラ。Sceneが各ビューの描画前に配る(ModelRendererComponentと同じ扱い)。</summary>
	void SetRenderCamera(const Camera* camera) override { camera_ = camera; }

	/// <summary>
	/// ワールド座標 (x, z) の海面の高さ(ワールドの Y)を返す。描かれている三角形の上の高さなので、見た目と一致する。
	/// 海の板の外なら false(outHeight は変えない)。Follow Camera が ON なら、どこでも海の上として答える。
	/// </summary>
	bool GetSurfaceHeight(float x, float z, float& outHeight) const;

	/// <summary>
	/// シーンの海(有効な OceanComponent)から、ワールド座標 (x, z) の海面の高さを探す。重なっていれば高い方。
	/// どの海の上でもなければ false。浮かぶ物・水しぶき・着水の判定などに使う。
	/// </summary>
	static bool TryGetSurfaceHeight(const Scene& scene, float x, float z, float& outHeight);

	/// <summary>
	/// 波の式そのもの(このオブジェクトの位置を含まない、波だけの上下[m])。worldXZ はワールドの xz。
	/// シェーダーの WaveHeight と同じ式(変えるときは両方直す)。
	/// </summary>
	float EvaluateWaves(const Vector2& worldXZ) const;

	/// <summary>波の時間[秒](シェーダーの gShaderParams.time と同じ値)。</summary>
	float GetWaveTime() const { return waveTime_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		// --- 形 ---
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeX_, "Size X", 0.1f, 0.1f, 10000.0f, "海の広さ(X方向[m])。このオブジェクトの位置が中心。Follow Camera なら見える範囲。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeZ_, "Size Z", 0.1f, 0.1f, 10000.0f, "海の広さ(Z方向[m])。");
		KUJATA_REGISTER_INT_NAMED_TIP(divisions_, "Divisions", 1.0f, 1, 512,
		    "長い方の辺を何マスに分けるか(短い方は同じマスの大きさになるように決める)。少ないほどローポリ。1マスは波長の1/3以下がおすすめ。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(followCamera_, "Follow Camera",
		    "板をカメラの真下へついて来させる(果てのない海)。波と模様はその場に留まる。遠くは Fog で空の色に溶かすとよい。");
		// --- 色 ---
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(waterColor_, "Water Color", 0.01f, 0.0f, 1.0f, "沖(深いところ)の海の色。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(shallowColor_, "Shallow Color", 0.01f, 0.0f, 1.0f, "浅瀬の色(海の下に物があるところ)。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(foamColor_, "Foam Color", 0.01f, 0.0f, 1.0f, "泡の色(模様・波の山・岸)。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(ringColor_, "Ring Color", 0.01f, 0.0f, 1.0f, "泡の下に敷く暗い輪の色。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lighting_, "Lighting", 0.01f, 0.0f, 1.0f,
		    "陰の強さ。0=べた塗り(風のタクト)、1=トゥーンの陰そのまま(面ごとの陰がはっきり出る)。");
		// --- 泡の模様 ---
		KUJATA_REGISTER_FLOAT_NAMED_TIP(patternSize_, "Pattern Size", 0.05f, 0.0f, 1000.0f,
		    "泡の輪の模様の大きさ[m](1マス)。0で模様なし。Material に BaseColor テクスチャがあれば、その赤を模様にする(1枚がこの大きさ)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(foamAmount_, "Foam Amount", 0.01f, 0.0f, 1.0f, "泡の輪の量(マスに輪がある割合)。0で模様なし。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(foamWidth_, "Foam Width", 0.005f, 0.0f, 1.0f, "輪の線の太さ(マスの大きさに対する割合)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(ringOffset_, "Ring Offset", 0.01f, 0.0f, 100.0f, "暗い輪をどれだけずらすか[m]。0で暗い輪なし。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(distortStrength_, "Distort Strength", 0.01f, 0.0f, 100.0f, "模様の揺らぎの強さ[m]。0で揺らさない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(distortLength_, "Distort Length", 0.05f, 0.01f, 1000.0f, "揺らぎの波長[m](大きいほどゆったりうねる)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(driftSpeed_, "Drift Speed", 0.01f, -100.0f, 100.0f, "模様が流れる速さ[m/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(driftDirection_, "Drift Direction", 1.0f, -360.0f, 360.0f, "模様が流れる向き[度](0=+X、90=+Z)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(foamLevel_, "Crest Foam", 0.01f, 0.0f, 1.0f,
		    "波の山のどこから上を泡にするか。0=山の泡なし、0.85=山のてっぺんだけ、0.5=半分より上。");
		// --- 深さ(岸・浅瀬) ---
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shoreFoamDistance_, "Shore Foam", 0.01f, 0.0f, 100.0f,
		    "岸の泡の幅[m]。海の下の物(地面・岩・浮かぶ物)が水面からこの距離より近いところを泡にする。0で岸の泡なし。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(depthColorDistance_, "Shallow Depth", 0.05f, 0.0f, 1000.0f,
		    "この深さ[m]より浅いところを、段をつけて浅瀬の色にする。0で深さを使わない(どこも沖の色)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(shallowAlpha_, "Shallow Alpha", 0.01f, 0.0f, 1.0f, "浅瀬の不透明度(小さいほど海の下が透ける)。沖は常に不透明。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(timeScale_, "Time Scale", 0.01f, 0.0f, 10.0f, "波と模様の進む速さの倍率(0で止まる)。");
		// --- 波 ---
		registry.FloatNamed("wave1Direction", "Wave1 Direction", waves_[0].x, 1.0f, -360.0f, 360.0f, "波1の進む向き[度](0=+X、90=+Z)。");
		registry.FloatNamed("wave1Length", "Wave1 Length", waves_[0].y, 0.05f, 0.0f, 1000.0f, "波1の波長[m](山から次の山まで)。0でこの波を使わない。");
		registry.FloatNamed("wave1Height", "Wave1 Height", waves_[0].z, 0.01f, 0.0f, 100.0f, "波1の高さ[m](平らな面から山の頂点まで)。");
		registry.FloatNamed("wave1Speed", "Wave1 Speed", waves_[0].w, 0.05f, -100.0f, 100.0f, "波1の進む速さ[m/秒]。");
		registry.FloatNamed("wave2Direction", "Wave2 Direction", waves_[1].x, 1.0f, -360.0f, 360.0f, "波2の進む向き[度]。波1と向きを変えると自然になる。");
		registry.FloatNamed("wave2Length", "Wave2 Length", waves_[1].y, 0.05f, 0.0f, 1000.0f, "波2の波長[m]。0でこの波を使わない。");
		registry.FloatNamed("wave2Height", "Wave2 Height", waves_[1].z, 0.01f, 0.0f, 100.0f, "波2の高さ[m]。");
		registry.FloatNamed("wave2Speed", "Wave2 Speed", waves_[1].w, 0.05f, -100.0f, 100.0f, "波2の進む速さ[m/秒]。");
		registry.FloatNamed("wave3Direction", "Wave3 Direction", waves_[2].x, 1.0f, -360.0f, 360.0f, "波3の進む向き[度]。");
		registry.FloatNamed("wave3Length", "Wave3 Length", waves_[2].y, 0.05f, 0.0f, 1000.0f, "波3の波長[m]。0でこの波を使わない。");
		registry.FloatNamed("wave3Height", "Wave3 Height", waves_[2].z, 0.01f, 0.0f, 100.0f, "波3の高さ[m]。");
		registry.FloatNamed("wave3Speed", "Wave3 Speed", waves_[2].w, 0.05f, -100.0f, 100.0f, "波3の進む速さ[m/秒]。");
		registry.FloatNamed("wave4Direction", "Wave4 Direction", waves_[3].x, 1.0f, -360.0f, 360.0f, "波4の進む向き[度]。");
		registry.FloatNamed("wave4Length", "Wave4 Length", waves_[3].y, 0.05f, 0.0f, 1000.0f, "波4の波長[m]。0でこの波を使わない(既定は使わない)。");
		registry.FloatNamed("wave4Height", "Wave4 Height", waves_[3].z, 0.01f, 0.0f, 100.0f, "波4の高さ[m]。");
		registry.FloatNamed("wave4Speed", "Wave4 Speed", waves_[3].w, 0.05f, -100.0f, 100.0f, "波4の進む速さ[m/秒]。");
		KUJATA_REGISTER_STRING_NAMED_TIP(materialPath_, "Material",
		    "見た目の元になるマテリアル(Project からドラッグでも設定できる)。色は掛け算、トゥーンの段・Flat Shading が効き、"
		    "BaseColor テクスチャは泡の模様になる。Shader が空なら海のシェーダーを使う。");
	}

	KUJATA_FIELD_FLOAT(sizeX_, 40.0f);
	KUJATA_FIELD_FLOAT(sizeZ_, 40.0f);
	KUJATA_FIELD_INT(divisions_, 40);
	KUJATA_FIELD_BOOL(followCamera_, false);
	Vector4 waterColor_ = {0.12f, 0.42f, 0.78f, 1.0f};
	Vector4 shallowColor_ = {0.3f, 0.78f, 0.85f, 1.0f};
	Vector4 foamColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
	Vector4 ringColor_ = {0.08f, 0.3f, 0.62f, 1.0f};
	KUJATA_FIELD_FLOAT(lighting_, 0.35f);
	KUJATA_FIELD_FLOAT(patternSize_, 4.0f);
	KUJATA_FIELD_FLOAT(foamAmount_, 0.45f);
	KUJATA_FIELD_FLOAT(foamWidth_, 0.08f);
	KUJATA_FIELD_FLOAT(ringOffset_, 0.25f);
	KUJATA_FIELD_FLOAT(distortStrength_, 0.35f);
	KUJATA_FIELD_FLOAT(distortLength_, 6.0f);
	KUJATA_FIELD_FLOAT(driftSpeed_, 0.3f);
	KUJATA_FIELD_FLOAT(driftDirection_, 0.0f);
	KUJATA_FIELD_FLOAT(foamLevel_, 0.9f);
	KUJATA_FIELD_FLOAT(shoreFoamDistance_, 0.4f);
	KUJATA_FIELD_FLOAT(depthColorDistance_, 3.0f);
	KUJATA_FIELD_FLOAT(shallowAlpha_, 0.75f);
	KUJATA_FIELD_FLOAT(timeScale_, 1.0f);
	// 波(向き[度], 波長[m], 高さ[m], 速さ[m/s])。シェーダーの objectParams[0..3] へそのまま渡す。
	// 風のタクト風に、うねりは控えめ(見た目の主役は泡の模様)。
	Vector4 waves_[kWaveCount] = {
	    {0.0f, 9.0f, 0.12f, 1.6f},
	    {60.0f, 5.5f, 0.08f, 1.2f},
	    {150.0f, 3.5f, 0.04f, 1.0f},
	    {0.0f, 0.0f, 0.0f, 1.0f},
	};
	KUJATA_FIELD_STRING(materialPath_, "");

	/// <summary>格子のマスの数(長い辺が Divisions マス)。</summary>
	void GetGridDivisions(uint32_t& outX, uint32_t& outZ) const;
	/// <summary>形(格子)が無いか、広さ・分割が変わっていれば作り直す。</summary>
	void EnsureModel();
	/// <summary>マテリアル(未設定なら既定の海の見た目)をモデルへ反映する。</summary>
	void ApplyMaterial();
	/// <summary>シェーダーへ渡す値(objectParams)を組み立てる。</summary>
	void BuildShaderParams(Vector4 (&outParams)[11]) const;

	// --- 実行時 ---
	const Camera* camera_ = nullptr;
	std::unique_ptr<Model> model_;
	// 描くときの位置(このオブジェクトの子として置く。Follow Camera のときはカメラの真下へ、マスの大きさ単位でずらす)。
	WorldTransform drawTransform_;
	bool drawTransformReady_ = false;
	float builtSizeX_ = 0.0f;
	float builtSizeZ_ = 0.0f;
	uint32_t builtDivisionsX_ = 0;
	uint32_t builtDivisionsZ_ = 0;
	std::string appliedMaterialPath_;
	bool materialDirty_ = true;
	// マテリアルの BaseColor テクスチャを泡の模様に使うか(マテリアルにテクスチャが設定されているとき)。
	bool usePatternTexture_ = false;
	// 波の時間。Play 中は Update がゲームの時間で進め、それ以外は Draw がフレームに1回、実時間で進める。
	float waveTime_ = 0.0f;
	bool playing_ = false;
	uint64_t advancedFrame_ = ~0ull;
};

} // namespace KujataEngine
