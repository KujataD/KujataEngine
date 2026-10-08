// エディタの CUI のコマンド: スクリプト(ゲームのコンポーネントのひな形)とフォルダ。
// 設計と一覧は .claude/editor-automation.md。
#include "EditorCommandUtil.h"
#include "../../base/ProjectPath.h"
#include "../EditorApplication.h"
#include "../EditorConsole.h"
#include "../FolderCreation.h"
#include "../GameProjectSync.h"
#include "../../base/StringUtil.h"
#include "../ScriptCreation.h"
#include <string>

namespace KujataEngine {

namespace {

using nlohmann::json;

EditorCommandResult CommandScriptCreate(const EditorCommandArgs& args) {
	if (args.Count() < 1) {
		return EditorCommandResult::Failure("使い方: script.create <名前> [フォルダ(プロジェクト基準。既定は GameComponents)]");
	}
	const std::string folder = (args.Count() > 1) ? args.Get(1) : "GameComponents";
	const ScriptCreation::Result created = ScriptCreation::CreateComponentScript(GetActiveProjectRoot() / folder, args.Get(0));
	if (!created.succeeded) {
		return EditorCommandResult::Failure(created.message);
	}
	EditorConsole::GetInstance()->AddLog("[Script] " + created.message, EditorLogLevel::Info);

	json result;
	result["className"] = created.className;
	result["header"] = created.headerPath.generic_string();
	result["source"] = created.sourcePath.generic_string();
	// Project ウィンドウの Create > Script と同じく、すぐ Add Component に出るようビルドし直す。
	result["reloaded"] = EditorApplication::GetInstance()->ReloadGameModule();
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandFolderCreate(const EditorCommandArgs& args) {
	if (args.Count() < 1) {
		return EditorCommandResult::Failure("使い方: folder.create <パス(プロジェクト基準。例 GameComponents/Player)>");
	}
	// 最後の名前だけを作る(途中のフォルダが無ければ一緒に作られる)。
	const std::filesystem::path path(StringUtil::ToWString(args.Get(0)));
	const FolderCreation::Result created =
	    FolderCreation::CreateFolder(GetActiveProjectRoot() / path.parent_path(), StringUtil::ToString(path.filename().wstring()));
	if (!created.succeeded) {
		return EditorCommandResult::Failure(created.message);
	}
	return EditorCommandResult::Success(created.message);
}

EditorCommandResult CommandProjectSync(const EditorCommandArgs&) {
	const GameProjectSync::Result synced = GameProjectSync::Sync();
	if (!synced.succeeded) {
		return EditorCommandResult::Failure(synced.message);
	}
	json result;
	result["changed"] = synced.changed;
	result["sources"] = synced.sourceCount;
	result["message"] = synced.message;
	return EditorCommandResult::Success(result);
}

} // namespace

void RegisterScriptCommands(EditorCommandRegistry& registry) {
	registry.Register("script.create", "script.create <名前> [フォルダ]",
	                  "コンポーネントのひな形(.h / .cpp)を作り、GameModule をビルドし直す(Project の Create > Script と同じ)", CommandScriptCreate);
	registry.Register("folder.create", "folder.create <パス>", "プロジェクトの中にフォルダを作る(Project の Create > Folder と同じ)", CommandFolderCreate);
	registry.Register("project.sync", "project.sync",
	                  "GameModule.vcxproj をワイルドカードの形に保ち、.filters にプロジェクトの中の .cpp / .h を並べ直す(スクリプトの作成・DLL の読み直し・起動のときにも自動で行う)", CommandProjectSync);
}

} // namespace KujataEngine
