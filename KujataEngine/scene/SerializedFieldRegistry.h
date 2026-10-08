#pragma once

#include "../runtime/InspectorUI.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "ObjectRef.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace KujataEngine {

/// <summary>
/// アニメーション可能なfloatチャンネル1つ分。pathはComponent内のチャンネル名
/// (例: "speed", "velocity.x")。Component種別のprefixはAnimator側が付ける。
/// </summary>
struct AnimatableChannel {
	std::string path;
	float* value = nullptr;
	// bool型チャンネル(補間せずカーブ値>=0.5でtrue)。valueとどちらか一方を設定する。
	bool* boolValue = nullptr;
};

/// <summary>
/// 登録された調整項目をInspector表示、JSON保存、JSON読み込みへ流すRegistry。
/// 同じ登録から型情報(キー・型・範囲・説明)も書き出せる(DescribeSchema)。CUI の schema.get が使う。
/// </summary>
class KUJATA_API SerializedFieldRegistry {
public:
	enum class Mode {
		DrawInspector,
		WriteJson,
		ReadJson,
		CollectAnimatables,
		ResolveReferences,
		DescribeSchema,
	};

	/// <summary>型情報の書き出し用Registryを作るときの目印(WriteJson用のコンストラクタと区別するため)。</summary>
	struct DescribeSchemaTag {};

	/// <summary>
	/// Inspector描画用Registryを作成します。
	/// </summary>
	SerializedFieldRegistry() : mode_(Mode::DrawInspector) {}

	/// <summary>
	/// JSON保存用Registryを作成します。
	/// </summary>
	explicit SerializedFieldRegistry(nlohmann::json& json) : mode_(Mode::WriteJson), writeJson_(&json) {}

	/// <summary>
	/// JSON読み込み用Registryを作成します。
	/// </summary>
	explicit SerializedFieldRegistry(const nlohmann::json& json) : mode_(Mode::ReadJson), readJson_(&json) {}

	/// <summary>
	/// アニメーションチャンネル収集用Registryを作成します。
	/// float/Vector3/Vector4フィールドをチャンネルとして列挙します。
	/// </summary>
	explicit SerializedFieldRegistry(std::vector<AnimatableChannel>& channels) : mode_(Mode::CollectAnimatables), channels_(&channels) {}

	/// <summary>
	/// 参照解決用Registryを作成します(instanceId → GameObject* の埋め戻し)。
	/// </summary>
	explicit SerializedFieldRegistry(const IObjectResolver& resolver) : mode_(Mode::ResolveReferences), resolver_(&resolver) {}

	/// <summary>
	/// 型情報の書き出し用Registryを作成します。fieldsは配列になり、登録順に1フィールド1要素が入る。
	/// 要素: {"key", "label", "type"(float/int/uint32/bool/vector3/color/string/object/objectRef/componentRef),
	///        "min", "max"(範囲がある場合だけ), "step"(ドラッグの刻み), "tooltip"(ある場合だけ), "fields"(objectの中身)}
	/// </summary>
	SerializedFieldRegistry(DescribeSchemaTag, nlohmann::json& fields) : mode_(Mode::DescribeSchema), schema_(&fields) {
		*schema_ = nlohmann::json::array();
	}

	void Float(const char* memberName, float& value, float dragSpeed = 0.1f, float minValue = 0.0f, float maxValue = 0.0f, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		FloatNamed(key.c_str(), MakeDisplayName(key).c_str(), value, dragSpeed, minValue, maxValue, tooltip);
	}

	void FloatNamed(const char* jsonKey, const char* label, float& value, float dragSpeed = 0.1f, float minValue = 0.0f, float maxValue = 0.0f, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "float", tooltip, minValue, maxValue, dragSpeed);
			return;
		}
		if (mode_ == Mode::CollectAnimatables) {
			channels_->push_back({pathPrefix_ + jsonKey, &value});
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			bool changed = InspectorUI::DragFloat(label, &value, dragSpeed, minValue, maxValue);
			InspectorUI::AnimationFieldHook(ChannelKey(jsonKey).c_str(), &value, 1, changed);
			// ツールチップは最後に出す。AnimationFieldHookのBeginPopupContextItemが
			// 「直前のアイテム」を参照するので、その判定を先に済ませておく。
			InspectorUI::ItemTooltip(tooltip);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = value;
			return;
		}
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_number()) {
			return;
		}
		value = readJson_->at(jsonKey).get<float>();
		if (minValue < maxValue) {
			value = std::clamp(value, minValue, maxValue);
		}
	}

	void Int(const char* memberName, int& value, float dragSpeed = 1.0f, int minValue = 0, int maxValue = 0, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		IntNamed(key.c_str(), MakeDisplayName(key).c_str(), value, dragSpeed, minValue, maxValue, tooltip);
	}

	void IntNamed(const char* jsonKey, const char* label, int& value, float dragSpeed = 1.0f, int minValue = 0, int maxValue = 0, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "int", tooltip, minValue, maxValue, dragSpeed);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			InspectorUI::DragInt(label, &value, dragSpeed, minValue, maxValue);
			InspectorUI::ItemTooltip(tooltip);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = value;
			return;
		}
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_number_integer()) {
			return;
		}
		value = readJson_->at(jsonKey).get<int>();
		if (minValue < maxValue) {
			value = std::clamp(value, minValue, maxValue);
		}
	}

	void UInt32(const char* memberName, uint32_t& value, float dragSpeed = 1.0f, uint32_t minValue = 0, uint32_t maxValue = 0, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		UInt32Named(key.c_str(), MakeDisplayName(key).c_str(), value, dragSpeed, minValue, maxValue, tooltip);
	}

	void UInt32Named(const char* jsonKey, const char* label, uint32_t& value, float dragSpeed = 1.0f, uint32_t minValue = 0, uint32_t maxValue = 0, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "uint32", tooltip, static_cast<double>(minValue), static_cast<double>(maxValue), dragSpeed);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			int intValue = static_cast<int>(std::min<uint32_t>(value, static_cast<uint32_t>(0x7fffffff)));
			int intMin = static_cast<int>(std::min<uint32_t>(minValue, static_cast<uint32_t>(0x7fffffff)));
			int intMax = static_cast<int>(std::min<uint32_t>(maxValue, static_cast<uint32_t>(0x7fffffff)));
			if (InspectorUI::DragInt(label, &intValue, dragSpeed, intMin, intMax)) {
				if (intValue < 0) {
					value = 0;
				} else {
					value = static_cast<uint32_t>(intValue);
				}
			}
			InspectorUI::ItemTooltip(tooltip);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = value;
			return;
		}
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_number_unsigned() && !readJson_->at(jsonKey).is_number_integer()) {
			return;
		}
		int64_t readValue = readJson_->at(jsonKey).get<int64_t>();
		if (readValue < 0) {
			return;
		}
		value = static_cast<uint32_t>(readValue);
		if (minValue < maxValue) {
			value = std::clamp(value, minValue, maxValue);
		}
	}

	void Bool(const char* memberName, bool& value, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		BoolNamed(key.c_str(), MakeDisplayName(key).c_str(), value, tooltip);
	}

	void BoolNamed(const char* jsonKey, const char* label, bool& value, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "bool", tooltip);
			return;
		}
		if (mode_ == Mode::CollectAnimatables) {
			AnimatableChannel channel;
			channel.path = pathPrefix_ + jsonKey;
			channel.boolValue = &value;
			channels_->push_back(channel);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			bool changed = InspectorUI::Checkbox(label, &value);
			// bool成分1つをアニメーション録画へ接続する(値は0/1のfloatとして扱う)。
			float animValue = value ? 1.0f : 0.0f;
			InspectorUI::AnimationFieldHook(ChannelKey(jsonKey).c_str(), &animValue, 1, changed);
			InspectorUI::ItemTooltip(tooltip);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = value;
			return;
		}
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_boolean()) {
			return;
		}
		value = readJson_->at(jsonKey).get<bool>();
	}

	// ラベル見出しの下に X/Y/Z の3チェックボックスを横並びで描く(Unityのconstraints風)。
	// JSONは各軸を個別キーで保存/読込する。
	void BoolAxes(const char* label, const char* keyX, bool& x, const char* keyY, bool& y, const char* keyZ, bool& z) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(keyX, (std::string(label) + " X").c_str(), "bool", nullptr);
			AddSchema(keyY, (std::string(label) + " Y").c_str(), "bool", nullptr);
			AddSchema(keyZ, (std::string(label) + " Z").c_str(), "bool", nullptr);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			InspectorUI::TextUnformatted(label);
			// 表示は "X"/"Y"/"Z"、IDはキーで一意化(##で不可視化)。
			std::string labelX = std::string("X##") + keyX;
			std::string labelY = std::string("Y##") + keyY;
			std::string labelZ = std::string("Z##") + keyZ;
			InspectorUI::Checkbox(labelX.c_str(), &x);
			InspectorUI::SameLine();
			InspectorUI::Checkbox(labelY.c_str(), &y);
			InspectorUI::SameLine();
			InspectorUI::Checkbox(labelZ.c_str(), &z);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[keyX] = x;
			(*writeJson_)[keyY] = y;
			(*writeJson_)[keyZ] = z;
			return;
		}
		if (HasReadableKey(keyX) && readJson_->at(keyX).is_boolean()) {
			x = readJson_->at(keyX).get<bool>();
		}
		if (HasReadableKey(keyY) && readJson_->at(keyY).is_boolean()) {
			y = readJson_->at(keyY).get<bool>();
		}
		if (HasReadableKey(keyZ) && readJson_->at(keyZ).is_boolean()) {
			z = readJson_->at(keyZ).get<bool>();
		}
	}

	void Vector3Field(const char* memberName, Vector3& value, float dragSpeed = 0.01f, float minValue = 0.0f, float maxValue = 0.0f, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		Vector3Named(key.c_str(), MakeDisplayName(key).c_str(), value, dragSpeed, minValue, maxValue, tooltip);
	}

	void Vector3Named(const char* jsonKey, const char* label, Vector3& value, float dragSpeed = 0.01f, float minValue = 0.0f, float maxValue = 0.0f, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "vector3", tooltip, minValue, maxValue, dragSpeed);
			return;
		}
		if (mode_ == Mode::CollectAnimatables) {
			std::string basePath = pathPrefix_ + jsonKey;
			channels_->push_back({basePath + ".x", &value.x});
			channels_->push_back({basePath + ".y", &value.y});
			channels_->push_back({basePath + ".z", &value.z});
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			bool changed = InspectorUI::DragFloat3(label, &value.x, dragSpeed, minValue, maxValue);
			InspectorUI::AnimationFieldHook(ChannelKey(jsonKey).c_str(), &value.x, 3, changed);
			InspectorUI::ItemTooltip(tooltip);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = {value.x, value.y, value.z};
			return;
		}
		if (!CanReadArray(jsonKey, 3)) {
			return;
		}
		const nlohmann::json& values = readJson_->at(jsonKey);
		if (values.at(0).is_number()) {
			value.x = values.at(0).get<float>();
		}
		if (values.at(1).is_number()) {
			value.y = values.at(1).get<float>();
		}
		if (values.at(2).is_number()) {
			value.z = values.at(2).get<float>();
		}
	}

	void Vector4Field(const char* memberName, Vector4& value, float dragSpeed = 0.01f, float minValue = 0.0f, float maxValue = 0.0f, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		Vector4Named(key.c_str(), MakeDisplayName(key).c_str(), value, dragSpeed, minValue, maxValue, tooltip);
	}

	void Vector4Named(const char* jsonKey, const char* label, Vector4& value, float dragSpeed = 0.01f, float minValue = 0.0f, float maxValue = 0.0f, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			// Inspectorでは色(RGBA)として編集している。範囲は使っていないので出さない。
			AddSchema(jsonKey, label, "color", tooltip);
			return;
		}
		if (mode_ == Mode::CollectAnimatables) {
			std::string basePath = pathPrefix_ + jsonKey;
			channels_->push_back({basePath + ".x", &value.x});
			channels_->push_back({basePath + ".y", &value.y});
			channels_->push_back({basePath + ".z", &value.z});
			channels_->push_back({basePath + ".w", &value.w});
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			float values[4] = {value.x, value.y, value.z, value.w};
			bool changed = InspectorUI::ColorEdit4(label, values);
			if (changed) {
				value = {values[0], values[1], values[2], values[3]};
			}
			InspectorUI::AnimationFieldHook(ChannelKey(jsonKey).c_str(), &value.x, 4, changed);
			InspectorUI::ItemTooltip(tooltip);
			(void)dragSpeed;
			(void)minValue;
			(void)maxValue;
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = {value.x, value.y, value.z, value.w};
			return;
		}
		if (!CanReadArray(jsonKey, 4)) {
			return;
		}
		const nlohmann::json& values = readJson_->at(jsonKey);
		if (values.at(0).is_number()) {
			value.x = values.at(0).get<float>();
		}
		if (values.at(1).is_number()) {
			value.y = values.at(1).get<float>();
		}
		if (values.at(2).is_number()) {
			value.z = values.at(2).get<float>();
		}
		if (values.at(3).is_number()) {
			value.w = values.at(3).get<float>();
		}
	}

	void String(const char* memberName, std::string& value, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		StringNamed(key.c_str(), MakeDisplayName(key).c_str(), value, tooltip);
	}

	void StringNamed(const char* jsonKey, const char* label, std::string& value, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "string", tooltip);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			std::array<char, 256> buffer{};
			size_t copyLength = (std::min)(value.size(), buffer.size() - 1);
			std::copy_n(value.data(), copyLength, buffer.data());
			buffer[copyLength] = '\0';
			if (InspectorUI::InputText(label, buffer.data(), buffer.size())) {
				value = buffer.data();
			}
			InspectorUI::ItemTooltip(tooltip);
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = value;
			return;
		}
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_string()) {
			return;
		}
		value = readJson_->at(jsonKey).get<std::string>();
	}

	template <class TObject>
	void Object(const char* memberName, TObject& value, const char* tooltip = nullptr) {
		std::string key = MakeJsonKey(memberName);
		ObjectNamed(key.c_str(), MakeDisplayName(key).c_str(), value, tooltip);
	}

	template <class TObject>
	void ObjectNamed(const char* jsonKey, const char* label, TObject& value, const char* tooltip = nullptr) {
		if (mode_ == Mode::DescribeSchema) {
			nlohmann::json childFields;
			SerializedFieldRegistry childRegistry(DescribeSchemaTag{}, childFields);
			value.RegisterSerializedFields(childRegistry);
			nlohmann::json& entry = AddSchema(jsonKey, label, "object", tooltip);
			entry["fields"] = childFields;
			return;
		}
		if (mode_ == Mode::CollectAnimatables) {
			SerializedFieldRegistry childRegistry(*channels_);
			childRegistry.pathPrefix_ = pathPrefix_ + jsonKey + ".";
			value.RegisterSerializedFields(childRegistry);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			InspectorUI::TextUnformatted(label);
			// 見出し自体にも説明を出せるようにする(脚のグループ単位の補足など)。
			InspectorUI::ItemTooltip(tooltip);
			// ImGuiはラベル文字列をIDに使うため、同じ構造体を複数並べると同名ラベルがID衝突する。
			// 要素ごとにIDスコープを切って避ける。
			InspectorUI::PushId(jsonKey);
			SerializedFieldRegistry childRegistry;
			// 録画のチャンネル名もCollectAnimatablesと同じ "leg0.hipYawDeg" 形式に揃える。
			childRegistry.pathPrefix_ = pathPrefix_ + jsonKey + ".";
			value.RegisterSerializedFields(childRegistry);
			InspectorUI::PopId();
			return;
		}
		if (mode_ == Mode::WriteJson) {
			nlohmann::json childJson = nlohmann::json::object();
			SerializedFieldRegistry childRegistry(childJson);
			value.RegisterSerializedFields(childRegistry);
			(*writeJson_)[jsonKey] = childJson;
			return;
		}
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_object()) {
			return;
		}
		SerializedFieldRegistry childRegistry(readJson_->at(jsonKey));
		value.RegisterSerializedFields(childRegistry);
	}

	/// <summary>
	/// GameObject参照フィールド(型なし)。UnityのInspectorのオブジェクト代入欄に相当。
	/// </summary>
	void ObjectRefField(const char* memberName, ObjectRef& ref) {
		std::string key = MakeJsonKey(memberName);
		ObjectRefNamed(key.c_str(), MakeDisplayName(key).c_str(), ref);
	}

	void ObjectRefNamed(const char* jsonKey, const char* label, ObjectRef& ref) {
		if (mode_ == Mode::DescribeSchema) {
			// JSON(field.set)では参照先の instanceId の文字列として書く。
			AddSchema(jsonKey, label, "objectRef", nullptr);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			DrawObjectField(label, ref.value, [&ref]() { ref.Clear(); }, [&ref](GameObject* dropped) { ref.Assign(dropped); });
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = ref.targetInstanceId;
			return;
		}
		if (mode_ == Mode::ReadJson) {
			ref.value = nullptr;
			ReadInstanceId(jsonKey, ref.targetInstanceId);
			return;
		}
		if (mode_ == Mode::ResolveReferences && resolver_) {
			ref.Resolve(*resolver_);
			return;
		}
	}

	/// <summary>
	/// 型付き参照フィールド(Unityの public T target; 相当)。型が一致するGameObjectのみ受理する。
	/// </summary>
	template <class T>
	void ComponentRefField(const char* memberName, ComponentRef<T>& ref) {
		std::string key = MakeJsonKey(memberName);
		ComponentRefNamed(key.c_str(), MakeDisplayName(key).c_str(), ref);
	}

	template <class T>
	void ComponentRefNamed(const char* jsonKey, const char* label, ComponentRef<T>& ref) {
		if (mode_ == Mode::DescribeSchema) {
			AddSchema(jsonKey, label, "componentRef", nullptr);
			return;
		}
		if (mode_ == Mode::DrawInspector) {
			DrawObjectField(label, ref.owner, [&ref]() { ref.Clear(); }, [&ref](GameObject* dropped) { ref.Assign(dropped); });
			return;
		}
		if (mode_ == Mode::WriteJson) {
			(*writeJson_)[jsonKey] = ref.targetInstanceId;
			return;
		}
		if (mode_ == Mode::ReadJson) {
			ref.owner = nullptr;
			ref.component = nullptr;
			ReadInstanceId(jsonKey, ref.targetInstanceId);
			return;
		}
		if (mode_ == Mode::ResolveReferences && resolver_) {
			ref.Resolve(*resolver_);
			return;
		}
	}

	static std::string MakeJsonKey(const char* memberName) {
		std::string key = memberName;
		size_t dotPosition = key.find_last_of('.');
		if (dotPosition != std::string::npos) {
			key = key.substr(dotPosition + 1);
		}
		while (!key.empty() && key.back() == '_') {
			key.pop_back();
		}
		return key;
	}

	static std::string MakeDisplayName(const std::string& key) {
		std::string label;
		label.reserve(key.size() + 4);

		for (size_t index = 0; index < key.size(); ++index) {
			char character = key[index];
			if (character == '_') {
				label.push_back(' ');
				continue;
			}

			bool shouldInsertSpace = false;
			if (index > 0 && std::isupper(static_cast<unsigned char>(character)) != 0) {
				char previous = key[index - 1];
				if (previous != '_' && std::islower(static_cast<unsigned char>(previous)) != 0) {
					shouldInsertSpace = true;
				}
			}
			if (shouldInsertSpace) {
				label.push_back(' ');
			}

			if (label.empty() || (!label.empty() && label.back() == ' ')) {
				label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
			} else {
				label.push_back(character);
			}
		}

		return label;
	}

private:
	/// <summary>
	/// 型情報を1件足す。範囲は min < max のときだけ出す(ReadJson と同じく、それ以外は「制限なし」の意味)。
	/// </summary>
	nlohmann::json& AddSchema(const char* jsonKey, const char* label, const char* type, const char* tooltip, double minValue = 0.0, double maxValue = 0.0, float dragSpeed = 0.0f) {
		nlohmann::json entry;
		entry["key"] = jsonKey;
		entry["label"] = label;
		entry["type"] = type;
		if (minValue < maxValue) {
			entry["min"] = minValue;
			entry["max"] = maxValue;
		}
		if (dragSpeed > 0.0f) {
			entry["step"] = dragSpeed;
		}
		if (tooltip && tooltip[0] != '\0') {
			entry["tooltip"] = tooltip;
		}
		schema_->push_back(entry);
		return schema_->back();
	}

	/// <summary>
	/// 録画用のチャンネル名。ネストしたObjectの中では "leg0.hipYawDeg" のように親のキーが前置され、
	/// CollectAnimatablesが列挙するpathと一致する。トップレベルでは jsonKey そのまま。
	/// </summary>
	std::string ChannelKey(const char* jsonKey) const { return pathPrefix_ + jsonKey; }

	bool HasReadableKey(const char* jsonKey) const {
		if (!readJson_) {
			return false;
		}
		return readJson_->contains(jsonKey);
	}

	bool CanReadArray(const char* jsonKey, size_t requiredSize) const {
		if (!HasReadableKey(jsonKey)) {
			return false;
		}
		if (!readJson_->at(jsonKey).is_array()) {
			return false;
		}
		return readJson_->at(jsonKey).size() >= requiredSize;
	}

	void ReadInstanceId(const char* jsonKey, std::string& outInstanceId) {
		if (!HasReadableKey(jsonKey)) {
			return;
		}
		if (!readJson_->at(jsonKey).is_string()) {
			return;
		}
		outInstanceId = readJson_->at(jsonKey).get<std::string>();
	}

	// オブジェクト参照フィールドのUI描画をここへ集約する。
	// 参照モデル(ObjectRef/ComponentRef)側は「どう表示/ドロップ処理するか」を知らずに済む。
	template <class OnClear, class OnDropped>
	void DrawObjectField(const char* label, const GameObject* current, OnClear onClear, OnDropped onDropped) {
		std::string currentName = GameObjectDisplayName(current);
		void* dropped = nullptr;
		bool cleared = false;
		if (!InspectorUI::ObjectField(label, currentName.c_str(), &dropped, &cleared)) {
			return;
		}
		if (cleared) {
			onClear();
			return;
		}
		if (dropped) {
			onDropped(static_cast<GameObject*>(dropped));
		}
	}

	Mode mode_ = Mode::DrawInspector;
	nlohmann::json* writeJson_ = nullptr;
	const nlohmann::json* readJson_ = nullptr;
	std::vector<AnimatableChannel>* channels_ = nullptr;
	std::string pathPrefix_;
	const IObjectResolver* resolver_ = nullptr;
	nlohmann::json* schema_ = nullptr;
};

} // namespace KujataEngine

#define KUJATA_FIELD_FLOAT(name, defaultValue) float name = defaultValue
#define KUJATA_FIELD_INT(name, defaultValue) int name = defaultValue
#define KUJATA_FIELD_UINT32(name, defaultValue) uint32_t name = defaultValue
#define KUJATA_FIELD_BOOL(name, defaultValue) bool name = defaultValue
#define KUJATA_FIELD_VECTOR3(name, defaultValue) KujataEngine::Vector3 name = defaultValue
#define KUJATA_FIELD_VECTOR4(name, defaultValue) KujataEngine::Vector4 name = defaultValue
#define KUJATA_FIELD_STRING(name, defaultValue) std::string name = defaultValue
#define KUJATA_FIELD_OBJECT(type, name) type name{}
#define KUJATA_FIELD_OBJECT_REF(name) KujataEngine::ObjectRef name{}
#define KUJATA_FIELD_COMPONENT_REF(type, name) KujataEngine::ComponentRef<type> name{}

#define KUJATA_SERIALIZED_FIELDS_BEGIN() \
public: \
	void DrawInspector() override { \
		KujataEngine::SerializedFieldRegistry registry; \
		RegisterSerializedFields(registry); \
	} \
	void WriteJson(nlohmann::json& json) const override { \
		KujataEngine::SerializedFieldRegistry registry(json); \
		auto& mutableSelf = const_cast<std::remove_const_t<std::remove_reference_t<decltype(*this)>>&>(*this); \
		mutableSelf.RegisterSerializedFields(registry); \
	} \
	void ReadJson(const nlohmann::json& json) override { \
		KujataEngine::SerializedFieldRegistry registry(json); \
		RegisterSerializedFields(registry); \
	} \
	void CollectAnimatableChannels(std::vector<KujataEngine::AnimatableChannel>& channels) override { \
		KujataEngine::SerializedFieldRegistry registry(channels); \
		RegisterSerializedFields(registry); \
	} \
	void ResolveReferences(KujataEngine::IObjectResolver& resolver) override { \
		KujataEngine::SerializedFieldRegistry registry(resolver); \
		RegisterSerializedFields(registry); \
	} \
	bool DescribeSerializedFields(nlohmann::json& fields) override { \
		KujataEngine::SerializedFieldRegistry registry(KujataEngine::SerializedFieldRegistry::DescribeSchemaTag{}, fields); \
		RegisterSerializedFields(registry); \
		return true; \
	} \
private: \
	void RegisterSerializedFields(KujataEngine::SerializedFieldRegistry& registry)

// --- ツールチップ付きの登録 ---
// 末尾に説明文(日本語可)を足した _TIP 版。Inspectorでその項目にカーソルを重ねると出る。
// 既存の非TIP版は tooltip=nullptr になるだけなので、混在させてよい。
//
//   KUJATA_REGISTER_FLOAT_TIP(stepThreshold_, 0.01f, 0.05f, 20.0f,
//       "足が定位置からこれだけ水平にズレたら踏み出す。大きいほど大股でのっしり歩く");
//
// 変数より後ろの引数は後ろから省略できる(省略した分は登録の関数の初期値になる)。
//   KUJATA_REGISTER_FLOAT_TIP(slowScale_);                    // 刻み・範囲・説明すべて省略
//   KUJATA_REGISTER_FLOAT_TIP(slowScale_, 0.01f, 0.0f, 1.0f); // 説明だけ省略
// 初期値: 刻みは float 0.1 / int・uint32 1 / Vector3・Vector4 0.01、範囲は 最小 = 最大 = 0(範囲なし)、説明はなし。
// 途中の引数だけを省略することはできない(C++ の初期値は後ろから順に省略する決まりのため)。
// 範囲を省略すると負の値なども入るので、範囲と説明はなるべく書くこと。
// ##__VA_ARGS__ は、後ろの引数が無いときに直前のカンマを消す(MSVC の従来/準拠どちらのプリプロセッサでも効く)。
#define KUJATA_REGISTER_FLOAT_TIP(member, ...) registry.Float(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_FLOAT_NAMED_TIP(member, label, ...) registry.FloatNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_INT_TIP(member, ...) registry.Int(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_INT_NAMED_TIP(member, label, ...) registry.IntNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_UINT32_TIP(member, ...) registry.UInt32(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_UINT32_NAMED_TIP(member, label, ...) registry.UInt32Named(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_BOOL_TIP(member, ...) registry.Bool(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_BOOL_NAMED_TIP(member, label, ...) registry.BoolNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR3_TIP(member, ...) registry.Vector3Field(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR3_NAMED_TIP(member, label, ...) registry.Vector3Named(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR4_TIP(member, ...) registry.Vector4Field(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR4_NAMED_TIP(member, label, ...) registry.Vector4Named(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_STRING_TIP(member, ...) registry.String(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_STRING_NAMED_TIP(member, label, ...) registry.StringNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_OBJECT_TIP(member, ...) registry.Object(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_OBJECT_NAMED_TIP(member, label, ...) registry.ObjectNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)

// 説明なしの版も、変数より後ろの引数を後ろから省略できる(KUJATA_REGISTER_FLOAT(speed_) など)。
#define KUJATA_REGISTER_FLOAT(member, ...) registry.Float(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_FLOAT_NAMED(member, label, ...) registry.FloatNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_INT(member, ...) registry.Int(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_INT_NAMED(member, label, ...) registry.IntNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_UINT32(member, ...) registry.UInt32(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_UINT32_NAMED(member, label, ...) registry.UInt32Named(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_BOOL(member) registry.Bool(#member, member)
#define KUJATA_REGISTER_BOOL_NAMED(member, label) registry.BoolNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member)
#define KUJATA_REGISTER_BOOL_AXES(label, memberX, memberY, memberZ)                                    \
	registry.BoolAxes(label, KujataEngine::SerializedFieldRegistry::MakeJsonKey(#memberX).c_str(), memberX, \
	                  KujataEngine::SerializedFieldRegistry::MakeJsonKey(#memberY).c_str(), memberY,        \
	                  KujataEngine::SerializedFieldRegistry::MakeJsonKey(#memberZ).c_str(), memberZ)
#define KUJATA_REGISTER_VECTOR3(member, ...) registry.Vector3Field(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR3_NAMED(member, label, ...) registry.Vector3Named(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR4(member, ...) registry.Vector4Field(#member, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_VECTOR4_NAMED(member, label, ...) registry.Vector4Named(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member, ##__VA_ARGS__)
#define KUJATA_REGISTER_STRING(member) registry.String(#member, member)
#define KUJATA_REGISTER_STRING_NAMED(member, label) registry.StringNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member)
#define KUJATA_REGISTER_OBJECT(member) registry.Object(#member, member)
#define KUJATA_REGISTER_OBJECT_NAMED(member, label) registry.ObjectNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member)
#define KUJATA_REGISTER_OBJECT_REF(member) registry.ObjectRefField(#member, member)
#define KUJATA_REGISTER_OBJECT_REF_NAMED(member, label) registry.ObjectRefNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member)
#define KUJATA_REGISTER_COMPONENT_REF(member) registry.ComponentRefField(#member, member)
#define KUJATA_REGISTER_COMPONENT_REF_NAMED(member, label) registry.ComponentRefNamed(KujataEngine::SerializedFieldRegistry::MakeJsonKey(#member).c_str(), label, member)
