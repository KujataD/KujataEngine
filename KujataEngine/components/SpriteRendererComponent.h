#pragma once

#include "../2d/SpriteQuad.h"
#include "../scene/Component.h"
#include <array>
#include <string>

namespace KujataEngine {

class Camera;

/// <summary>
/// world空間に2Dスプライトを描画するComponent(Unity 2DのSpriteRenderer相当)。
///
/// UI(Canvas方式)との役割分担:
///   - Sprite方式(このComponent): GameObjectの通常のTransformで配置し、カメラに映る。
///     3Dオブジェクトと同じ空間・同じ深度に並ぶ。ゲーム内の見た目(キャラ、弾、背景)向け。
///     CanvasもRectTransformも不要。
///   - Canvas方式(CanvasComponent + RectTransform + Image/Text/Button): 画面に貼り付く
///     スクリーン空間UI。解像度に追従し、クリック等のUIイベントを受け取る。HUD/メニュー向け。
/// </summary>
/// <remarks>ゲームDLL(GameModule)からランタイム利用できるようKUJATA_APIでエクスポートする。</remarks>
class KUJATA_API SpriteRendererComponent : public Component {
public:
	const char* GetTypeName() const override { return "SpriteRendererComponent"; }
	bool AllowMultiple() const override { return false; }

	/// <summary>
	/// 表示するテクスチャを設定します。assetIdが空でパスの実ファイルが存在すればIDを補完し、
	/// 以後のリネーム/移動に追従します(アセット参照の共通パターン)。
	/// </summary>
	void SetSprite(const std::string& assetId, const std::string& path);

	void SetSpritePath(const std::string& path) { SetSprite("", path); }

	void SetColor(const Vector4& color);
	const Vector4& GetColor() const { return color_; }

	/// <summary>
	/// 1 world単位あたりのテクスチャピクセル数(Unity 2DのPixels Per Unit)。
	/// スプライトの大きさは「テクスチャの解像度 / この値」で自動決定される。
	/// 例: 100x100pxのテクスチャをPPU=100で使うと1x1 world単位になる。
	/// </summary>
	void SetPixelsPerUnit(float pixelsPerUnit);
	float GetPixelsPerUnit() const { return pixelsPerUnit_; }

	/// <summary>テクスチャ解像度とPPUから決まる現在の大きさ(world単位)。</summary>
	const Vector2& GetSize() const { return size_; }

	/// <summary>
	/// 描画順(Unity 2DのOrder in Layer)。小さいほど奥に描かれる。
	/// スプライトは深度を書かないため、スプライト同士の前後はこの値だけで決まる。
	/// </summary>
	void SetSortingOrder(int sortingOrder) { sortingOrder_ = sortingOrder; }
	int GetSortingOrder() const { return sortingOrder_; }

	/// <summary>Sprite2DRendererのパスから呼ばれる実描画。Component::Draw()では描かない。</summary>
	void DrawSprite();

	/// <summary>基準点(0..1)。(0.5,0.5)で中心、(0,0)でテクスチャ左上がTransformの原点。</summary>
	void SetPivot(const Vector2& pivot);
	const Vector2& GetPivot() const { return pivot_; }

	void SetFlip(bool flipX, bool flipY);

	void SetBlendMode(BlendMode blendMode);

	/// <summary>描画に使うカメラ。SceneがビューごとにApplyRenderCameraで設定する。</summary>
	void SetRenderCamera(const Camera* camera) override { camera_ = camera; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;
	void OnAfterReadJson() override;

private:
	/// <summary>GPUリソースの生成と、現在の設定のQuadへの反映。</summary>
	void EnsureQuadInitialized();
	/// <summary>assetId優先で現在のパスへ解決し、テクスチャを読み込む。</summary>
	void EnsureTextureLoaded();
	/// <summary>テクスチャ解像度とPPUからsize_を計算し直す。</summary>
	void UpdateSizeFromTexture();
	void SyncPathBuffer();
	void ApplyToQuad();

	std::string textureAssetId_;
	std::string texturePath_ = "Resources/white1x1.png";
	Vector4 color_ = {1.0f, 1.0f, 1.0f, 1.0f};
	float pixelsPerUnit_ = 100.0f;
	int sortingOrder_ = 0;
	Vector2 pivot_ = {0.5f, 0.5f};
	bool flipX_ = false;
	bool flipY_ = false;
	Vector2 uvOffset_ = {0.0f, 0.0f};
	Vector2 uvScale_ = {1.0f, 1.0f};
	float uvRotation_ = 0.0f;
	int blendModeIndex_ = static_cast<int>(BlendMode::kNormal);

	// 実行時状態(非シリアライズ)
	const Camera* camera_ = nullptr;
	SpriteQuad quad_;
	// テクスチャ解像度 / PPU から求まる大きさ。直接は編集しない。
	Vector2 size_ = {1.0f, 1.0f};
	uint32_t textureIndex_ = 0;
	std::string loadedPath_;
	bool textureResolved_ = false;
	std::array<char, 256> pathBuffer_{};
};

} // namespace KujataEngine
