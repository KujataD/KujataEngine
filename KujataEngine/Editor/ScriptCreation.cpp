#include "ScriptCreation.h"

#include "../base/ProjectPath.h"
#include "../scene/ComponentFactory.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

namespace KujataEngine {

namespace ScriptCreation {

namespace {

constexpr const char* kHeaderTemplate = R"TEMPLATE(#pragma once

#include "KujataEngine.h"

// $NAME$
// (このコンポーネントの役割をここに書く)
class $NAME$ : public KujataEngine::Component {
public:
	// 型の名前。シーンのファイルと Add Component の一覧で使う。クラス名と同じにしておく。
	const char* GetTypeName() const override { return "$NAME$"; }

	// Play を始めたときに 1 回呼ばれる。
	void OnPlayStart() override;

	// Play 中に毎フレーム呼ばれる(Edit 中は呼ばれない)。
	void Update() override;

	// Inspector に出し、シーンに保存する項目の登録。
	// ここに書いた項目は、保存・読み込み・Inspector の表示・Undo・参照の解決が自動で行われる。
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		// (変数, ドラッグの速さ, 最小, 最大, ツールチップ)。最小と最大を両方 0 にすると範囲なし。
		// 変数より後ろは後ろから省略できる(例: KUJATA_REGISTER_FLOAT_TIP(speed_) / KUJATA_REGISTER_FLOAT_TIP(speed_, 0.1f, 0.0f, 100.0f))。
		KUJATA_REGISTER_FLOAT_TIP(speed_, 0.1f, 0.0f, 100.0f, "速さ (m/秒)");
		KUJATA_REGISTER_BOOL_TIP(active_, "false にすると Update で何もしない");
		KUJATA_REGISTER_VECTOR3_TIP(offset_, 0.01f, 0.0f, 0.0f, "位置のずれ (m)");
		// 他のオブジェクト。Inspector の欄へ Hierarchy からドラッグして入れる。
		KUJATA_REGISTER_OBJECT_REF(target_);
	}

private:
	// 保存する項目。KUJATA_FIELD_* で宣言し(第 2 引数は既定値)、上の KUJATA_SERIALIZED_FIELDS_BEGIN で登録する。
	KUJATA_FIELD_FLOAT(speed_, 5.0f);
	KUJATA_FIELD_BOOL(active_, true);
	KUJATA_FIELD_VECTOR3(offset_, KujataEngine::Vector3(0.0f, 0.0f, 0.0f));
	KUJATA_FIELD_OBJECT_REF(target_);

	// 保存しない項目(Play 中だけ使う値)。
	// コンポーネントは Play をまたいで使い回されるので、OnPlayStart で必ず初期化する。
	float elapsed_ = 0.0f;
};
)TEMPLATE";

constexpr const char* kSourceTemplate = R"TEMPLATE(#include "$NAME$.h"

using namespace KujataEngine;

void $NAME$::OnPlayStart() {
	elapsed_ = 0.0f;
}

void $NAME$::Update() {
	GameObject* owner = GetOwner();
	if (!owner || !active_) {
		return;
	}

	// 経過時間。1 フレームの秒数は Time::GetDeltaTime()。
	elapsed_ += Time::GetDeltaTime();

	// 自分の位置は owner->GetTransform().translation_ で読み書きする。
	// 例: owner->GetTransform().translation_.x += speed_ * Time::GetDeltaTime();

	// 参照先は Get() で取る。Inspector で入れていなければ nullptr なので、ある時だけ使う
	// (ここで return すると、この後に書いた処理まで止まるので注意)。
	if (GameObject* target = target_.Get()) {
		// 例: const Vector3& targetPosition = target->GetTransform().translation_;
		(void)target;
	}
}

// このコンポーネントをゲームに登録する(GameModule.cpp に書き足さなくてよい)。
KUJATA_REGISTER_GAME_COMPONENT($NAME$);
)TEMPLATE";

Result Fail(std::string message) {
	Result result;
	result.message = std::move(message);
	return result;
}

bool IsIdentifier(const std::string& name) {
	if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) {
		return false;
	}
	return std::all_of(name.begin(), name.end(), [](char c) {
		const unsigned char u = static_cast<unsigned char>(c);
		return u < 0x80 && (std::isalnum(u) || c == '_');
	});
}

bool EndsWith(const std::string& text, const std::string& suffix) {
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// directory が base の中(base 自身を含む)か。
bool IsInside(const std::filesystem::path& directory, const std::filesystem::path& base) {
	const std::filesystem::path relative = directory.lexically_relative(base);
	return !relative.empty() && *relative.begin() != "..";
}

std::string Expand(const char* text, const std::string& className) {
	std::string result = text;
	const std::string key = "$NAME$";
	for (size_t position = result.find(key); position != std::string::npos; position = result.find(key, position + className.size())) {
		result.replace(position, key.size(), className);
	}
	// リポジトリのソースに合わせて CRLF で書く。
	std::string crlf;
	crlf.reserve(result.size() + result.size() / 32);
	for (char c : result) {
		if (c == '\n') {
			crlf += '\r';
		}
		crlf += c;
	}
	return crlf;
}

bool WriteFile(const std::filesystem::path& path, const std::string& text) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}
	file << text;
	return file.good();
}

} // namespace

Result CreateComponentScript(const std::filesystem::path& directory, const std::string& name) {
	if (!IsIdentifier(name)) {
		return Fail("名前には英数字と _ だけが使えます(先頭は数字以外): " + name);
	}
	const std::string className = EndsWith(name, "Component") ? name : name + "Component";

	const std::filesystem::path projectRoot = GetActiveProjectRoot().lexically_normal();
	const std::filesystem::path target = std::filesystem::absolute(directory).lexically_normal();
	if (!IsInside(target, projectRoot)) {
		return Fail("プロジェクトの外には作れません: " + target.string());
	}
	// ここにあるソースはビルドに入らない(GameModule.vcxproj の Exclude と合わせる)。
	for (const std::filesystem::path& excluded : {projectRoot / "Data", projectRoot / "Temp", projectRoot / "GameModule" / "bin"}) {
		if (IsInside(target, excluded)) {
			return Fail("このフォルダのソースはビルドに入りません: " + target.string());
		}
	}

	const std::vector<std::string>& registered = ComponentFactory::GetInstance().GetRegisteredTypeNames();
	if (std::find(registered.begin(), registered.end(), className) != registered.end()) {
		return Fail("同じ名前のコンポーネントがあります: " + className);
	}

	Result result;
	result.className = className;
	result.headerPath = target / (className + ".h");
	result.sourcePath = target / (className + ".cpp");
	std::error_code errorCode;
	if (std::filesystem::exists(result.headerPath, errorCode) || std::filesystem::exists(result.sourcePath, errorCode)) {
		return Fail("同じ名前のファイルがあります: " + result.headerPath.string());
	}

	std::filesystem::create_directories(target, errorCode);
	if (!WriteFile(result.headerPath, Expand(kHeaderTemplate, className)) || !WriteFile(result.sourcePath, Expand(kSourceTemplate, className))) {
		return Fail("ファイルに書き込めません: " + target.string());
	}

	result.succeeded = true;
	result.message = "スクリプトを作りました: " + result.sourcePath.string();
	return result;
}

} // namespace ScriptCreation

} // namespace KujataEngine
