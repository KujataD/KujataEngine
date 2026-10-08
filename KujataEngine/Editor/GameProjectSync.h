#pragma once

#include <string>

namespace KujataEngine {

/// <summary>
/// ゲームのプロジェクト(`GameModule/GameModule.vcxproj` と `.filters`)を、プロジェクトの中の .cpp / .h に合わせる。
/// - vcxproj: ソースはワイルドカード(`..\**\*.cpp` など)で入れ、ファイルを 1 つずつは書かない。
///   ファイルを足しても vcxproj が変わらないので、git で衝突しない。Visual Studio が 1 つずつの一覧に書き直しても、ここで元に戻す。
/// - .filters: Solution Explorer のフォルダ表示用に、ファイルを 1 つずつ書く(フォルダと同じ階層)。人ごとに作るので git に入れない。
/// Data・Temp・GameModule/bin の中は入れない。
/// 中身が変わったときだけ書く(Visual Studio の「再読み込み」を余計に出さないため)。
/// スクリプトの作成・DLL の読み直し・エディタの起動のときに呼ばれる。CUI は project.sync。
/// </summary>
namespace GameProjectSync {

struct Result {
	bool succeeded = false;
	// ファイルを書き直したか。
	bool changed = false;
	int sourceCount = 0;
	std::string message;
};

Result Sync();

} // namespace GameProjectSync

} // namespace KujataEngine
