#pragma once
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../vfx/ParticleModel.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace KujataEngine {

class Camera;

/// <summary>
/// 汎用パーティクル。**GPUインスタンシングで1ドローにまとめる**ので、粒を増やしてもGameObjectは増えない。
///
/// 使い方は2通りある。
///   - **持続**  Looping をONにして、Emission Rate ぶん毎秒出し続ける(エンジンの炎、死亡地点の炎)
///   - **単発**  コードから Burst() を呼んで Burst Count ぶん一気に出す(土埃、ヒット、切替の炎)
///
/// **強さ(Strength)を外から掛けられる。** 土埃で「軽い足音」と「巨体の踏みつけ」を
/// 同じPrefabの倍率違いで賄うための仕組みで、粒の数・初速・大きさにまとめて効く。
///
/// 座標系はWorld Spaceが既定。発生源が動いても粒はその場に取り残されるので、
/// 走りながら出す土埃や炎の尾が自然になる。offにすると粒が親に付いて回る。
/// </summary>
class KUJATA_API ParticleSystemComponent : public Component {
public:
	const char* GetTypeName() const override { return "ParticleSystemComponent"; }
	bool AllowMultiple() const override { return true; }

	void Initialize() override;
	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;
	void Draw() override;

	/// <summary>パーティクルは深度を書かない(PipelineType::kParticle)ので、不透明物の後に遠い順で描く。</summary>
	bool IsTransparentDraw() const override { return true; }

	/// <summary>描画に使うカメラ。シーン側が毎フレーム配る(ModelRenderer/Trailと同じ流儀)。</summary>
	void SetRenderCamera(const Camera* camera) override { camera_ = camera; }

	/// <summary>Burst Count ぶん一気に出す。</summary>
	void Burst();
	/// <summary>個数を指定して一気に出す。</summary>
	void Burst(int count);

	/// <summary>
	/// 好きな場所から出す(ワールド座標)。散らし方・広がり・初速・寿命・大きさは設定どおりで、
	/// 基準方向だけを direction にする(長さは見ない。0なら設定の Emit Direction)。
	/// inheritVelocity は全部の粒に足す速度(動いている物から出すしぶきに、その物の勢いを持たせる)。
	/// 水流に沿ったしぶきや、当たった場所の水しぶきのように、発生源が1か所でないものに使う。Strength は掛からない。
	/// </summary>
	void EmitAt(const Vector3& worldPosition, const Vector3& direction, int count = 1, const Vector3& inheritVelocity = {0.0f, 0.0f, 0.0f});

	/// <summary>持続発生のON/OFF。Loopingがoffのときは効かない。</summary>
	void SetEmitting(bool emitting) { emitting_ = emitting; }
	bool IsEmitting() const { return emitting_; }

	/// <summary>
	/// **強さの倍率。** 粒の数・初速・大きさにまとめて掛かる。
	/// 同じPrefabで「軽い接地」と「渾身の踏みつけ」を出し分けるためのもの。1で設定どおり。
	/// </summary>
	void SetStrength(float strength) { strength_ = (strength > 0.0f) ? strength : 0.0f; }
	float GetStrength() const { return strength_; }

	/// <summary>色を上書きする(魂の色を流し込むなど)。アルファは設定値のものを使う。</summary>
	void SetColorOverride(const Vector4& color);
	void ClearColorOverride() { hasColorOverride_ = false; }

	/// <summary>
	/// 粒のテクスチャをSRVインデックスで直接差し替える(ランタイム上書き)。
	/// texturePath_のファイル解決を経由しないので、NoiseTextureComponentのような
	/// メモリ生成テクスチャ(実ファイルを持たない)を貼るのに使う。
	/// まだモデルが作られていなければ何もしない(先にshape_等の設定でモデル生成が必要)。
	/// </summary>
	void SetTextureOverride(uint32_t textureIndex) {
		if (model_) {
			model_->SetTexture(textureIndex);
		}
	}

	/// <summary>生きている粒の数。消えたら片付けたい側が見る。</summary>
	size_t GetAliveCount() const { return particles_.size(); }

private:
	struct Particle {
		Vector3 position;
		Vector3 velocity;
		float age = 0.0f;
		float lifetime = 1.0f;
		float startSize = 1.0f;
		float rotation = 0.0f;
		float rotationSpeed = 0.0f;
	};

	void EnsureModel();
	void SpawnOne();
	// origin から、baseDirection を基準に1粒出す(extraVelocity は速度に足す)。
	void SpawnAt(const Vector3& origin, const Vector3& baseDirection, const Vector3& extraVelocity);
	Vector3 SampleEmitPosition(const Vector3& origin) const;
	Vector3 SampleVelocity(const Vector3& baseDirection) const;
	float RandomRange(float minValue, float maxValue) const;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_INT_NAMED_TIP(shape_, "Shape", 1.0f, 0, 3,
		    "粒の形。0=Plane(ビルボード) / 1=Cube / 2=Triangle / 3=Tetrahedron。\n"
		    "煙や炎はPlane、破片や火花はCube/Tetrahedronが向く。");
		KUJATA_REGISTER_STRING_NAMED_TIP(texturePath_, "Texture", "粒のテクスチャ。白1x1なら単色の粒になる。");
		KUJATA_REGISTER_INT_NAMED_TIP(blendMode_, "Blend Mode", 1.0f, 0, 1,
		    "0=通常 / 1=加算。**炎や魔法の粒は加算、土埃は通常**。加算で土埃を出すと白く光ってしまう。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(looping_, "Looping",
		    "onで毎秒Emission Rateぶん出し続ける。offならBurst()を呼んだときだけ出る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(emissionRate_, "Emission Rate", 0.5f, 0.0f, 500.0f,
		    "Looping時の毎秒の発生数。");
		KUJATA_REGISTER_INT_NAMED_TIP(burstCount_, "Burst Count", 1.0f, 1, 500,
		    "Burst()1回で出す数。Strengthが掛かるので、ここは倍率1のときの数を入れる。");
		KUJATA_REGISTER_INT_NAMED_TIP(emitVolume_, "Emit Volume", 1.0f, 0, 2,
		    "発生位置の散らし方。0=点 / 1=球 / 2=水平の円盤。\n**土埃は円盤**にすると地面に沿って広がる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(emitRadius_, "Emit Radius", 0.01f, 0.0f, 20.0f, "発生位置を散らす半径。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(emitDirection_, "Emit Direction", 0.01f, -100.0f, 100.0f, "粒を飛ばす基準方向(オーナーのローカル)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(spreadAngleDeg_, "Spread Angle", 1.0f, 0.0f, 180.0f,
		    "基準方向からの広がり[度]。0で真っ直ぐ、180で全方向。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lifetimeMin_, "Lifetime Min", 0.01f, 0.02f, 20.0f, "粒の寿命の下限[秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(lifetimeMax_, "Lifetime Max", 0.01f, 0.02f, 20.0f, "粒の寿命の上限[秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(speedMin_, "Speed Min", 0.05f, 0.0f, 100.0f, "初速の下限[m/s]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(speedMax_, "Speed Max", 0.05f, 0.0f, 100.0f, "初速の上限[m/s]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeMin_, "Size Min", 0.01f, 0.001f, 20.0f, "発生時の大きさの下限。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeMax_, "Size Max", 0.01f, 0.001f, 20.0f, "発生時の大きさの上限。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(endSizeScale_, "End Size Scale", 0.01f, 0.0f, 10.0f,
		    "寿命の終わりに大きさが何倍になるか。**1より大きいと煙のように膨らみ、小さいと火花のように痩せる**。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(startColor_, "Start Color", 0.01f, 0.0f, 8.0f, "発生時の色(RGBA)。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(endColor_, "End Color", 0.01f, 0.0f, 8.0f, "消える直前の色(RGBA)。アルファを0にすると自然に消える。");
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(gravity_, "Gravity", 0.05f, -100.0f, 100.0f, "毎秒かかる加速度。煙は弱い上向き、破片は下向きにする。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(drag_, "Drag", 0.05f, 0.0f, 20.0f,
		    "速度の減衰[1/s]。**大きいほど早く失速して「もったり」する**。土埃は強めが合う。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(rotationSpeedDeg_, "Rotation Speed", 1.0f, -720.0f, 720.0f,
		    "粒の自転速度[度/秒]。±のランダムで散らす。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(worldSpace_, "World Space",
		    "onなら粒はワールドに置き去りになる(発生源が動いても付いてこない)。\n"
		    "走りながら出す土埃や炎の尾はこちらが自然。offだと親に追従する。");
	}

	// 見た目。
	KUJATA_FIELD_INT(shape_, 0);
	KUJATA_FIELD_STRING(texturePath_, "Resources/white1x1.png");
	KUJATA_FIELD_INT(blendMode_, 0);

	// 発生。
	KUJATA_FIELD_BOOL(looping_, false);
	KUJATA_FIELD_FLOAT(emissionRate_, 20.0f);
	KUJATA_FIELD_INT(burstCount_, 12);
	KUJATA_FIELD_INT(emitVolume_, 1);
	KUJATA_FIELD_FLOAT(emitRadius_, 0.3f);
	KUJATA_FIELD_VECTOR3(emitDirection_, (KujataEngine::Vector3{0.0f, 1.0f, 0.0f}));
	KUJATA_FIELD_FLOAT(spreadAngleDeg_, 45.0f);

	// 粒の性質。
	KUJATA_FIELD_FLOAT(lifetimeMin_, 0.4f);
	KUJATA_FIELD_FLOAT(lifetimeMax_, 0.9f);
	KUJATA_FIELD_FLOAT(speedMin_, 1.0f);
	KUJATA_FIELD_FLOAT(speedMax_, 2.5f);
	KUJATA_FIELD_FLOAT(sizeMin_, 0.2f);
	KUJATA_FIELD_FLOAT(sizeMax_, 0.45f);
	KUJATA_FIELD_FLOAT(endSizeScale_, 1.8f);
	KUJATA_FIELD_VECTOR4(startColor_, (KujataEngine::Vector4{0.75f, 0.68f, 0.55f, 0.85f}));
	KUJATA_FIELD_VECTOR4(endColor_, (KujataEngine::Vector4{0.6f, 0.55f, 0.45f, 0.0f}));
	KUJATA_FIELD_VECTOR3(gravity_, (KujataEngine::Vector3{0.0f, -1.2f, 0.0f}));
	KUJATA_FIELD_FLOAT(drag_, 1.5f);
	KUJATA_FIELD_FLOAT(rotationSpeedDeg_, 60.0f);
	KUJATA_FIELD_BOOL(worldSpace_, true);

	// --- 実行状態 ---
	std::unique_ptr<ParticleModel> model_;
	const Camera* camera_ = nullptr;
	std::vector<Particle> particles_;
	bool emitting_ = true;
	float emitAccumulator_ = 0.0f;
	float strength_ = 1.0f;
	bool hasColorOverride_ = false;
	Vector4 colorOverride_ = {1.0f, 1.0f, 1.0f, 1.0f};
	// 追従モードで粒を一緒に運ぶための前フレーム位置。
	Vector3 lastOwnerPosition_ = {0.0f, 0.0f, 0.0f};
	mutable std::mt19937 random_{12345u};
};

} // namespace KujataEngine
