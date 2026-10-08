#include "GameProjectSync.h"

#include "../base/ProjectPath.h"
#include "../base/StringUtil.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace KujataEngine {

namespace GameProjectSync {

namespace {

struct SourceFile {
	// vcxproj から見たパス(例 "..\GameComponents\Player\PlayerComponent.cpp"、"..\GameModule\GameModule.cpp")。
	std::string include;
	// フィルター(例 "GameComponents\Player")。GameModule フォルダ直下は空(プロジェクト直下に出す)。
	std::string filter;
	bool isHeader = false;
};

std::string ToUtf8(const std::filesystem::path& path) { return StringUtil::ToString(path.wstring()); }

// "/" を "\" にそろえる(vcxproj の書き方に合わせる)。
std::string ToBackslash(std::string text) {
	std::replace(text.begin(), text.end(), '/', '\\');
	return text;
}

std::vector<SourceFile> CollectSources(const std::filesystem::path& projectRoot, const std::filesystem::path& moduleDir) {
	// ここにあるソースは入れない(ビルドの生成物・データ)。
	const std::set<std::filesystem::path> excluded = {projectRoot / "Data", projectRoot / "Temp", moduleDir / "bin", moduleDir / "Temp"};

	std::vector<SourceFile> sources;
	std::error_code errorCode;
	std::filesystem::recursive_directory_iterator it(projectRoot, std::filesystem::directory_options::skip_permission_denied, errorCode);
	for (; !errorCode && it != std::filesystem::recursive_directory_iterator(); it.increment(errorCode)) {
		const std::filesystem::path path = it->path().lexically_normal();
		if (it->is_directory(errorCode)) {
			const std::string name = ToUtf8(path.filename());
			if (excluded.contains(path) || (!name.empty() && name.front() == '.')) {
				it.disable_recursion_pending();
			}
			continue;
		}
		const std::filesystem::path extension = path.extension();
		const bool isHeader = extension == ".h" || extension == ".hpp";
		if (!isHeader && extension != ".cpp") {
			continue;
		}

		SourceFile source;
		source.isHeader = isHeader;
		// vcxproj のワイルドカード(..\**\*.cpp)が出す形と同じにする(GameModule 直下も "..\GameModule\..." になる)。
		// 形が違うと、.filters のフォルダ分けが Visual Studio に効かない。
		source.include = "..\\" + ToBackslash(ToUtf8(path.lexically_relative(projectRoot)));
		const std::filesystem::path folder = path.parent_path().lexically_relative(projectRoot);
		if (path.parent_path() != moduleDir && folder != ".") {
			source.filter = ToBackslash(ToUtf8(folder));
		}
		sources.push_back(std::move(source));
	}
	// 並びを決めておく(実行ごとに同じ内容を書き、差分を出さないため)。
	std::sort(sources.begin(), sources.end(), [](const SourceFile& a, const SourceFile& b) { return a.include < b.include; });
	return sources;
}

struct TextFile {
	std::string text;
	bool bom = false;
	std::string newline = "\r\n";
};

bool ReadTextFile(const std::filesystem::path& path, TextFile& out) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	out.text = buffer.str();
	if (out.text.rfind("\xEF\xBB\xBF", 0) == 0) {
		out.bom = true;
		out.text.erase(0, 3);
	}
	out.newline = (out.text.find("\r\n") != std::string::npos || out.text.find('\n') == std::string::npos) ? "\r\n" : "\n";
	return true;
}

// 中身が同じなら書かない。書いたら true。
bool WriteIfChanged(const std::filesystem::path& path, const TextFile& original, const std::string& text) {
	if (text == original.text) {
		return false;
	}
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (original.bom) {
		file << "\xEF\xBB\xBF";
	}
	file << text;
	return true;
}

// 既存の、ソースだけを並べた <ItemGroup> と、空の <ItemGroup> を取り除き、取り除いた最初の場所を返す。
size_t RemoveSourceItemGroups(std::string& text) {
	size_t insertAt = std::string::npos;
	size_t position = 0;
	while ((position = text.find("<ItemGroup>", position)) != std::string::npos) {
		const size_t end = text.find("</ItemGroup>", position);
		if (end == std::string::npos) {
			break;
		}
		const size_t blockEnd = end + std::string("</ItemGroup>").size();
		const std::string block = text.substr(position, blockEnd - position);
		const bool hasSources = block.find("<ClCompile Include") != std::string::npos || block.find("<ClInclude Include") != std::string::npos;
		const bool hasOthers = block.find("<ProjectConfiguration") != std::string::npos || block.find("<ProjectReference") != std::string::npos ||
		                       block.find("<None ") != std::string::npos || block.find("<Filter ") != std::string::npos;
		const std::string inner = block.substr(std::string("<ItemGroup>").size(), end - position - std::string("<ItemGroup>").size());
		const bool isEmpty = inner.find_first_not_of(" \t\r\n") == std::string::npos;
		if (!isEmpty && (!hasSources || hasOthers)) {
			position = blockEnd;
			continue;
		}
		// 行頭の字下げと、後ろの改行も一緒に消す。
		size_t lineStart = text.rfind('\n', position);
		lineStart = (lineStart == std::string::npos) ? position : lineStart + 1;
		size_t lineEnd = blockEnd;
		if (text.compare(lineEnd, 2, "\r\n") == 0) {
			lineEnd += 2;
		} else if (lineEnd < text.size() && text[lineEnd] == '\n') {
			lineEnd += 1;
		}
		text.erase(lineStart, lineEnd - lineStart);
		if (insertAt == std::string::npos) {
			insertAt = lineStart;
		}
		position = lineStart;
	}
	return insertAt;
}

// vcxproj にはファイルを並べず、ワイルドカードで全部入れる(中身はいつも同じなので、ファイルを足しても vcxproj は変わらず、git で衝突しない)。
// 入れないフォルダは CollectSources と同じにする(Data・Temp・GameModule の bin と Temp・"." で始まるフォルダ)。
std::string BuildItemGroups(const std::string& nl) {
	// 除外もワイルドカードと同じ "..\" から書く(bin\** のように書くと、..\GameModule\bin\... に当たらない)。
	const std::string exclude = "..\\Data\\**;..\\Temp\\**;..\\GameModule\\bin\\**;..\\GameModule\\Temp\\**;..\\**\\.*\\**";
	std::string text = "  <ItemGroup>" + nl;
	text += "    <!-- ソースはワイルドカードで入れる(エディタの GameProjectSync が書く。手で編集しない)。 -->" + nl;
	text += "    <ClCompile Include=\"..\\**\\*.cpp\" Exclude=\"" + exclude + "\" />" + nl;
	text += "    <ClInclude Include=\"..\\**\\*.h;..\\**\\*.hpp\" Exclude=\"" + exclude + "\" />" + nl;
	text += "  </ItemGroup>" + nl;
	return text;
}

// フィルター名から、実行ごとに変わらない GUID を作る(FNV-1a)。
std::string MakeFilterGuid(const std::string& name) {
	uint64_t hashA = 14695981039346656037ull;
	uint64_t hashB = 1099511628211ull;
	for (unsigned char c : name) {
		hashA = (hashA ^ c) * 1099511628211ull;
		hashB = (hashB ^ c) * 14695981039346656037ull + 0x9E3779B97F4A7C15ull;
	}
	char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "{%08X-%04X-%04X-%04X-%012llX}", static_cast<uint32_t>(hashA >> 32), static_cast<uint32_t>((hashA >> 16) & 0xFFFF),
	              static_cast<uint32_t>(hashA & 0xFFFF), static_cast<uint32_t>(hashB >> 48), static_cast<unsigned long long>(hashB & 0xFFFFFFFFFFFFull));
	return buffer;
}

// 今の .filters から、フィルター名と GUID の組を読む(GUID を変えないため)。
std::map<std::string, std::string> ReadFilterGuids(const std::string& text) {
	std::map<std::string, std::string> guids;
	const std::string open = "<Filter Include=\"";
	size_t position = 0;
	while ((position = text.find(open, position)) != std::string::npos) {
		const size_t nameStart = position + open.size();
		const size_t nameEnd = text.find('"', nameStart);
		const size_t guidStart = text.find("<UniqueIdentifier>", nameEnd);
		const size_t guidEnd = text.find("</UniqueIdentifier>", guidStart);
		if (nameEnd == std::string::npos || guidStart == std::string::npos || guidEnd == std::string::npos) {
			break;
		}
		const size_t valueStart = guidStart + std::string("<UniqueIdentifier>").size();
		guids[text.substr(nameStart, nameEnd - nameStart)] = text.substr(valueStart, guidEnd - valueStart);
		position = guidEnd;
	}
	return guids;
}

std::string BuildFilters(const std::vector<SourceFile>& sources, const std::map<std::string, std::string>& oldGuids, const std::string& nl) {
	// 親のフィルターも作る(GameComponents\Player なら GameComponents も)。
	std::set<std::string> filters;
	for (const SourceFile& source : sources) {
		for (std::string filter = source.filter; !filter.empty();) {
			filters.insert(filter);
			const size_t separator = filter.rfind('\\');
			filter = (separator == std::string::npos) ? std::string() : filter.substr(0, separator);
		}
	}

	std::string text = "<?xml version=\"1.0\" encoding=\"utf-8\"?>" + nl;
	text += "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">" + nl;
	text += "  <ItemGroup>" + nl;
	for (const std::string& filter : filters) {
		const auto old = oldGuids.find(filter);
		text += "    <Filter Include=\"" + filter + "\">" + nl;
		text += "      <UniqueIdentifier>" + (old != oldGuids.end() ? old->second : MakeFilterGuid(filter)) + "</UniqueIdentifier>" + nl;
		text += "    </Filter>" + nl;
	}
	text += "  </ItemGroup>" + nl;
	for (const bool headers : {false, true}) {
		const char* element = headers ? "ClInclude" : "ClCompile";
		text += "  <ItemGroup>" + nl;
		for (const SourceFile& source : sources) {
			if (source.isHeader != headers) {
				continue;
			}
			if (source.filter.empty()) {
				text += std::string("    <") + element + " Include=\"" + source.include + "\" />" + nl;
			} else {
				text += std::string("    <") + element + " Include=\"" + source.include + "\">" + nl;
				text += "      <Filter>" + source.filter + "</Filter>" + nl;
				text += std::string("    </") + element + ">" + nl;
			}
		}
		text += "  </ItemGroup>" + nl;
	}
	text += "</Project>" + nl;
	return text;
}

} // namespace

Result Sync() {
	Result result;
	const std::filesystem::path projectRoot = GetActiveProjectRoot().lexically_normal();
	const std::filesystem::path moduleDir = projectRoot / "GameModule";
	const std::filesystem::path projectPath = moduleDir / "GameModule.vcxproj";
	const std::filesystem::path filtersPath = moduleDir / "GameModule.vcxproj.filters";

	TextFile project;
	if (!ReadTextFile(projectPath, project)) {
		// 配布先など、プロジェクトが無いときは何もしない。
		result.succeeded = true;
		result.message = "GameModule.vcxproj がありません: " + ToUtf8(projectPath);
		return result;
	}

	const std::vector<SourceFile> sources = CollectSources(projectRoot, moduleDir);
	result.sourceCount = static_cast<int>(sources.size());

	std::string projectText = project.text;
	size_t insertAt = RemoveSourceItemGroups(projectText);
	if (insertAt == std::string::npos) {
		insertAt = projectText.find("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"");
	}
	if (insertAt == std::string::npos) {
		result.message = "GameModule.vcxproj の形が想定と違うため、書き直しませんでした";
		return result;
	}
	projectText.insert(insertAt, BuildItemGroups(project.newline));

	TextFile filters;
	ReadTextFile(filtersPath, filters);
	filters.bom = true;
	const std::string filtersText = BuildFilters(sources, ReadFilterGuids(filters.text), project.newline);

	const bool projectChanged = WriteIfChanged(projectPath, project, projectText);
	const bool filtersChanged = WriteIfChanged(filtersPath, filters, filtersText);
	result.succeeded = true;
	result.changed = projectChanged || filtersChanged;
	result.message = result.changed ? "GameModule のプロジェクトを更新しました(ソース " + std::to_string(result.sourceCount) + " 個)"
	                                : "GameModule のプロジェクトは最新です";
	return result;
}

} // namespace GameProjectSync

} // namespace KujataEngine
