#pragma once

#include "../3d/Model.h"
#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {

class Camera;

/// <summary>
/// 通った跡に帯(リボン)を残す汎用トレイル。UnityのTrail Rendererに相当する。
/// 魔法弾・剣の軌跡・落下物の煙など、**動くものなら何にでも付けられる**。
///
/// 仕組み:
///   1. 毎フレーム自分のワールド位置を見て、前回の記録から Min Distance 以上動いていたら点を1つ足す
///   2. 各点は Lifetime 秒かけて歳を取り、寿命が切れたものから捨てる
///   3. 残っている点を古い順に並べ、隣り合う点の向きとカメラ方向から「横ベクトル」を作り、
///      左右へ Width ぶん開いた頂点を並べて帯を張る(常にカメラを向く板になる)
///   4. 出来た頂点を Model::UpdateDynamicVertices で毎フレーム流し込む
///
/// 頂点は**ワールド座標で作り、描画も単位行列で行う**。
/// そのため親が動いても・回っても、すでに置いた帯はその場に残る(これがトレイルの本質)。
///
/// UVは u=帯の進行方向(0=先頭の新しい側 → 1=末尾の古い側) / v=帯の幅方向(0〜1)。
/// マテリアルのShader Modelを Trail にすると、uに沿って薄くなり末尾が自然に消える。
///
/// プールで使い回すオブジェクト(魔法弾など)に付ける場合、ワープしたときに
/// 前の場所から線が伸びないよう、**Teleport Distance 以上飛んだら自動で履歴を捨てる**。
/// </summary>
class KUJATA_API TrailRendererComponent : public Component {
public:
	const char* GetTypeName() const override { return "TrailRendererComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;
	void Draw() override;

	/// <summary>深度を書かないマテリアル(半透明・加算)なら true。Scene が不透明物の後に遠い順で描く。</summary>
	bool IsTransparentDraw() const override { return transparent_; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;

	/// <summary>描画に使うカメラ。Sceneが各ビューの描画前に配る(ModelRendererComponentと同じ扱い)。</summary>
	void SetRenderCamera(const Camera* camera) override { camera_ = camera; }

	/// <summary>履歴を全て捨てて帯を消す。ワープさせる直前や、使い回しの開始時に呼ぶ。</summary>
	void Clear() { points_.clear(); }

	/// <summary>点の記録を止める/再開する。止めても既にある帯は寿命で自然に消える。</summary>
	void SetEmitting(bool emitting) { emitting_ = emitting; }
	bool IsEmitting() const { return emitting_; }

private:
	/// <summary>記録した通過点。</summary>
	struct TrailPoint {
		Vector3 position;
		// 記録してからの経過秒。Lifetimeに達したら捨てる。
		float age = 0.0f;
	};

	/// <summary>寿命切れを捨て、必要なら今の位置を記録する。</summary>
	void UpdatePoints(float deltaTime);
	/// <summary>点列から帯の頂点を組み立てる。</summary>
	void BuildRibbon();
	/// <summary>Materialアセットを読み直してモデルへ反映する。</summary>
	void ApplyMaterial();
	/// <summary>モデル(動的メッシュ)を必要に応じて作る。</summary>
	void EnsureModel();

private:
	// --- 設定(保存対象) ---
	// 点が消えるまでの秒数。長いほど尾が長く残る。
	float lifetime_ = 0.35f;
	// これ以上動いたら点を1つ記録する。小さいほど滑らかだが頂点が増える。
	float minDistance_ = 0.08f;
	// 先頭(新しい側)の帯の幅。
	float startWidth_ = 0.35f;
	// 末尾(古い側)の帯の幅。0にすると先細りになる。
	float endWidth_ = 0.0f;
	// 記録する点の上限。ここに達すると古い点から捨てる。
	int maxPoints_ = 48;
	// これ以上離れた場所へ移動したら、瞬間移動とみなして履歴を捨てる(プール再利用対策)。
	float teleportDistance_ = 5.0f;
	// 点の記録を行うか。
	bool emitting_ = true;
	// 帯に使うMaterialアセット。
	std::string materialAssetId_;
	std::string materialPath_;

	// --- 実行時 ---
	std::vector<TrailPoint> points_;
	std::unique_ptr<Model> model_;
	const Camera* camera_ = nullptr;
	// 帯は既にワールド座標で組み立ててあるので、描画は常に単位行列で行う。
	WorldTransform identityTransform_;
	bool identityReady_ = false;
	// 組み立て用の作業バッファ(毎フレームの確保を避けるため使い回す)。
	std::vector<VertexData> vertices_;
	// マテリアルの読み直しが必要か。
	bool materialDirty_ = true;
	// 今のマテリアルが深度を書かないか(IsTransparentDraw)。ApplyMaterialで決まる。
	bool transparent_ = false;
};

} // namespace KujataEngine
