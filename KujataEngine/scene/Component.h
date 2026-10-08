#pragma once

#include "../runtime/KujataApi.h"
#include "InvokableMethod.h"
#include "SerializedFieldRegistry.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <iosfwd>
#include <string>
#include <vector>

namespace KujataEngine {

class ColliderComponent;
class GameObject;
class SerializedFieldRegistry;
struct AnimatableChannel;
struct Collision;

/// <summary>
/// GameObjectに追加するComponentの基底クラス
/// </summary>
class KUJATA_API Component {
public:
	virtual ~Component();

	virtual void Initialize() {}
	virtual void Update() {}

	/// <summary>
	/// 固定間隔 Time::GetFixedDeltaTime() ごとに呼ぶ。
	/// </summary>
	virtual void FixedUpdate() {}

	/// <summary>
	/// 毎フレーム、全部の Update・物理・当たり判定の後に呼ぶ(カメラの追従など、動いた後の位置を使う処理)。
	/// </summary>
	virtual void LateUpdate() {}
	virtual void Draw() {}

	/// <summary>
	/// 深度を書かない描画(半透明・加算)をするか。true のコンポーネントは、Scene が不透明物をすべて描いた後に、
	/// カメラから遠い順に Draw を呼ぶ(シーンの並び順に関係なく、奥の物が透けて見えるように)。
	/// </summary>
	virtual bool IsTransparentDraw() const { return false; }

	/// <summary>
	/// 半透明(IsTransparentDraw が true)の中での描く順番。小さいほど先に描く(同じ値どうしは遠い順)。既定0。
	/// 水面のように「不透明物の深度を読みたいが、ほかの半透明より奥にある大きな面」は負にして先に描く。
	/// 半透明を描く前に不透明物の深度がコピーされるので、半透明として描けば自作シェーダーが gSceneDepth を読める。
	/// </summary>
	virtual int GetTransparentQueue() const { return 0; }

	/// <summary>
	/// Inspector表示用のComponent名を取得
	/// </summary>
	virtual const char* GetTypeName() const { return "Component"; }

	virtual void DrawInspector();

	/// <summary>
	/// Component固有情報をJSONへ書き出します。
	/// </summary>
	virtual void WriteJson(nlohmann::json& json) const;

	/// <summary>
	/// Component固有情報をJSONから読み込みます。
	/// </summary>
	virtual void ReadJson(const nlohmann::json& json);

	/// <summary>
	/// フィールドの型情報(キー・型・範囲・説明)を fields に書き出す(CUI の schema.get が使う)。
	/// KUJATA_SERIALIZED_FIELDS_BEGIN で登録しているコンポーネントは自動で対応し、trueを返す。
	/// Inspector/JSON を手書きしているコンポーネントは既定のまま false(型情報なし)。
	/// </summary>
	virtual bool DescribeSerializedFields(nlohmann::json& fields) {
		(void)fields;
		return false;
	}

	virtual void OnAfterReadJson() {}

	/// <summary>
	/// アニメーション可能なfloatチャンネルを列挙します。
	/// KUJATA_SERIALIZED_FIELDS使用Componentは自動対応。手書きComponentは個別にoverrideします。
	/// </summary>
	virtual void CollectAnimatableChannels(std::vector<AnimatableChannel>& channels) { (void)channels; }

	/// <summary>
	/// シリアライズ済みのGameObject参照(ObjectRef/ComponentRef)を、instanceIdから実ポインタへ解決します。
	/// Scene構築後(全GameObject生成後)に呼ばれます。参照を持たないComponentは何もしません。
	/// KUJATA_SERIALIZED_FIELDS_BEGIN を使うComponentは自動でoverrideされます。
	/// </summary>
	virtual void ResolveReferences(IObjectResolver& resolver) { (void)resolver; }

	/// <summary>
	/// このComponentが公開する「Inspectorから呼び出せるメソッド」を登録します。
	/// UnityのUnityEvent(Button.onClick等)のメソッド選択肢に相当します。
	/// 例: registry.Add("LoadBTSet", [this]() { LoadBTSet(); });
	/// </summary>
	virtual void RegisterInvokableMethods(InvokableMethodRegistry& registry) { (void)registry; }

	/// <summary>
	/// Component情報を共通形式のJSONとして書き出す
	/// </summary>
	void WriteJson(std::ostream& os, int indent) const;

	/// <summary>
	/// Editor上で削除可能かどうか
	/// </summary>
	virtual bool CanRemove() const { return true; }

	/// <summary>
	/// 同じ種類のComponentを複数追加できるかどうか
	/// </summary>
	virtual bool AllowMultiple() const { return true; }

	virtual void OnPlayStart() {}

	virtual void OnPlayStop() {}

	/// <summary>
	/// 通常Collider同士が接触開始した時に呼ばれます。
	/// </summary>
	virtual void OnCollisionEnter(const Collision& collision) { (void)collision; }

	/// <summary>
	/// 通常Collider同士が接触中の時に呼ばれます。
	/// </summary>
	virtual void OnCollisionStay(const Collision& collision) { (void)collision; }

	/// <summary>
	/// 通常Collider同士が接触終了した時に呼ばれます。
	/// </summary>
	virtual void OnCollisionExit(const Collision& collision) { (void)collision; }

	/// <summary>
	/// Trigger Colliderとの接触開始時に呼ばれます。
	/// </summary>
	virtual void OnTriggerEnter(ColliderComponent* other) { (void)other; }

	/// <summary>
	/// Trigger Colliderとの接触中に呼ばれます。
	/// </summary>
	virtual void OnTriggerStay(ColliderComponent* other) { (void)other; }

	/// <summary>
	/// Trigger Colliderとの接触終了時に呼ばれます。
	/// </summary>
	virtual void OnTriggerExit(ColliderComponent* other) { (void)other; }

	void SetOwner(GameObject* owner) { owner_ = owner; }

	GameObject* GetOwner() const { return owner_; }

	// --- Unity風のComponent検索(定義は循環include回避のためGameObject.h側) ---

	/// <summary>同じGameObjectのT型Componentを返します(無ければnullptr)。</summary>
	template <class T>
	T* GetComponent() const;

	/// <summary>自身と子孫からT型Componentを探します(深さ優先、無ければnullptr)。</summary>
	template <class T>
	T* GetComponentInChildren() const;

	/// <summary>自身と祖先からT型Componentを探します(無ければnullptr)。</summary>
	template <class T>
	T* GetComponentInParent() const;

	void SetEnabled(bool enabled) { enabled_ = enabled; }

	bool IsEnabled() const { return enabled_; }

	virtual bool IsTransformComponent() const { return false; }

	/// <summary>
	/// ColliderComponentなら自身を返す高速ダウンキャスト(dynamic_castの代替)。
	/// Scene::UpdateCollisions等、毎フレーム全Componentを型判定する箇所でRTTIコストを避けるために使う。
	/// </summary>
	virtual ColliderComponent* AsColliderComponent() { return nullptr; }


protected:
	GameObject* owner_ = nullptr;
	bool enabled_ = true;
};

} // namespace KujataEngine
