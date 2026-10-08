# エディタ操作の CUI 化 — 設計

AI(Claude Code 等)と人間の両方が、同じコマンドでエディタを操作・確認できるようにするための設計文書。

- 対象: KujataEngine のエディタ(`KujataEngine/Editor/`)と、起動・ログまわり。
- 状態: **Step 1〜4 を実装済み**(CUI の土台 / スクリーンショット・状態の書き出し・ログ / ログの重さ・プレハブ・アニメーション / 型情報)。Step 5 以降は未着手(§6)。
- 関連: [CLAUDE.md](CLAUDE.md) の「現在の方針」②(決定論+リプレイ)、[determinism.md](determinism.md) の Step 4(入力の記録・再生)

---

## 1. なぜ必要か

今のエディタは「人間が画面を見てマウスで操作する」ことしか想定していない。
AI に作業させると、次のところで詰まる(2026-09 の作業で実際に起きたこと)。

| 困りごと | 今の回避策 | 問題 |
|---|---|---|
| オブジェクトを選べない | スクリーンショットから座標を割り出してクリック | レイアウトが変わると同じ座標では押せない |
| 結果を確かめられない | ウィンドウを撮影して画像を目で見る | 値の確認ができない。見落としやすい |
| キーが押せない | OS へのキー注入 | Release では届かない |
| ログが読めない | ファイルの `logs/` だけ | エディタの Console の中身は外から読めない |
| 起動確認が手作業 | 起動 → 何秒か待つ → 強制終了 を毎回組み立てる | 待ち時間の勘に頼る。失敗を終了コードで判定できない |

人間にとっても似た不便がある: Undo の履歴に何をしたかが出ない、Console を絞り込めない、
シーン JSON の git 差分が ID の揺れで読みにくい、不具合を他人の PC で再現できない。

## 2. なぜバッチではなく CUI か

最初は「コマンドライン引数で起動して、処理を流して終了する」バッチ実行を中心に考えたが、やめた。
バッチでは**エディタ側の問題を見落とす**。

- **毎回まっさらな状態から始まる。** 選択の食い違い、Undo の積み方、ホットリロード後に残る古い状態、
  Play/Stop の繰り返しによる状態の持ち越しなど、**使い続けて初めて出る不具合**が再現しない。
- **エディタの処理を通らない近道ができやすい。** バッチ専用の経路を作ると、人間が通る処理と別物になり、
  「バッチでは通るのにエディタでは壊れる」が起きる。
- **様子を見て次の手を決められない。** AI は「状態を見る → 操作する → 結果を見る」を繰り返して作業する。一方的に流すだけでは合わない。

そこで、**起動中のエディタに 1 つずつコマンドを送り、結果を受け取る CUI** を中心にする。
バッチは「起動したエディタに、同じコマンドを順に流すだけ」のものとして残す(まっさらな状態からの再現確認に使う)。

## 3. 構成

```
 起動中のエディタ(UI も普段どおり動いている)
   └ コマンド層 EditorCommandRegistry
        └ 実行はメインスレッドのフレームの頭(EditorApplication::Update の先頭)。
          人間の UI 操作と同じ処理(EditorApplication / EditorUndoManager / SceneJsonImporter)を呼ぶ
        ↑ EditorCommandServer(受付の窓口。1 件ずつ順に実行する)
 ├ Console の入力欄 : エディタの Console ウィンドウで直接打つ(人間)
 ├ kujata CLI      : ターミナルから Tools/kujata.cmd <コマンド>。引数なしで対話モード(人間・AI)
 │                   標準入力をつなぐと 1 行 1 コマンドで順に実行(引用符が崩れないので AI はこちら)
 │                   名前付きパイプ \\.\pipe\KujataEditor 経由
 ├ MCP サーバー    : (未実装)Claude Code から同じコマンドをツールとして呼ぶ
 └ スクリプト実行  : --run <ファイル> [--exit] = 起動したエディタに同じコマンドを 1 行ずつ流す
```

**人間も AI も同じ CUI を使う。** 人間は Console の入力欄か `kujata` の対話モード、AI は `kujata <コマンド>` を 1 回ずつ呼ぶ。
どちらも同じコマンド層を通るので、名前・結果・エラー文が揃う。

### 守る決まり

1. **名前は 1 つに揃える。** Inspector の表示名・JSON のキー・コマンドで使う名前を同じにする。
   人間が「MonsterBall の Rotator の speed を上げて」と言えば、そのまま AI のコマンドになる。
2. **オブジェクトはパスでも指定できる。** instanceId(`go_...`)に加えて `Stage/Enemies/Guardian` のような階層パスを受け付ける。
   同名が複数あるときは**曖昧だとエラーを返す**(黙って先頭を選ばない)。
3. **失敗の理由は文章で返す。** 人間が読んで直せる文にする。
4. **変更は必ず Undo できる。** 変更系のコマンドは実行前に Undo のスナップショットを取り、ラベルに `[CUI]` と中身を入れる。
5. **エディタの様子を毎回返す。** 返り値に「そのコマンドの実行中に出たログ」と「実行後のエディタの状態」を必ず含める(§4)。
   これで AI も人間も、1 手ごとにエディタ側で何か起きていないかを確かめられる。
6. **エディタの機能もコマンドとして公開する。** UI の部品には描画と入力の受け取りだけを残し、中の処理はコマンドに寄せる
   (例: ギズモはドラッグ中の見た目だけを持ち、手を離した時点で `field.set` 相当の処理を 1 回呼ぶ)。今あるものは少しずつ移す。

## 4. コマンドの形と返り値

### 入力

1 行のテキスト。人間が打つ形と AI が送る形は同じ。

```
field.set MonsterBall RotatorComponent speed 0.05
object.get "Stage/Enemies/Guardian"
wait 60
```

- 空白で区切る。空白を含む名前は `"..."` で囲む。
- `field.set` の値は残りの部分を JSON として読む(`0.05` / `true` / `"文字列"` / `[1, 2, 3]`)。JSON として読めなければ文字列として扱う。
- コンポーネントは型名で指定する。同じ型が複数あるときは `ColliderComponent#1` のように番号を付ける(0 始まり)。

### 返り値(JSON 1 行)

```json
{
  "ok": true,
  "command": "field.set MonsterBall RotatorComponent speed 0.05",
  "result": { "object": "MonsterBall", "component": "RotatorComponent", "key": "speed", "value": 0.05 },
  "error": "",
  "logs": ["...このコマンドの実行中に Console に出たログ..."],
  "state": { "mode": "Edit", "scene": "SampleScene", "selection": "MonsterBall", "undoTop": "[CUI] field.set MonsterBall/RotatorComponent.speed", "frame": 1234 }
}
```

返事の `text` は人間向けに整形した結果(Console と `kujata` はこれを表示する)。
`kujata -Json` は返事の JSON をそのまま出す(AI・スクリプト向け)。失敗すると終了コード 1。

## 5. コマンド一覧(Step 1〜4 で実装したもの)

| 分類 | コマンド | 内容 |
|---|---|---|
| 案内 | `help [コマンド]` | コマンド一覧・使い方 |
| 参照 | `state` | エディタの状態(モード・シーン・選択・Undo の先頭・フレーム番号) |
| | `scene.list` | 全オブジェクトのパス・instanceId・有効/無効・コンポーネント |
| | `object.get <オブジェクト>` | コンポーネントごとの全フィールド値 |
| | `component.types` | 追加できるコンポーネントの型名 |
| | `schema.get [型名]` / `schema.get <オブジェクト> <型名>` | フィールドの型・範囲・説明・初期値(§5.5)。引数なしで一覧 |
| | `log.tail [件数] [info\|warning\|error]` | Console の最近のログ。重さを付けるとそれ以上のものだけ |
| | `log.file` | 今回の起動のログファイル(JSON Lines)の場所 |
| | `state.dump [ファイル]` | エディタの状態と全オブジェクトの全フィールド(差分を取って変化を確かめる用) |
| 確認 | `view.screenshot <scene\|game\|editor> [ファイル]` | ビューの描画結果、またはエディタ全体を PNG に保存する(§5.1) |
| | `window.show [ウィンドウ名] [true\|false]` | ウィンドウを開いて前面に出す / 閉じる。引数なしで一覧。`InputActions` / `Scenes` / `Rendering` は Project Settings のそのページを開く |
| プレハブ | `prefab.list` | プレハブファイルの一覧(`prefab.instantiate` にそのまま渡せる Data 基準のパス) |
| | `prefab.create <オブジェクト>` | 子階層ごとプレハブとして保存し、インスタンスにする(Hierarchy の Create Prefab) |
| | `prefab.instantiate <パス> [親]` | プレハブを配置して選択する(Project からのドラッグ&ドロップ) |
| | `prefab.apply` / `prefab.revert` / `prefab.unpack <インスタンス>` | Inspector の Apply / Revert / Unpack。子を指定してもルート単位で行う |
| | `prefab.open <インスタンス\|*.prefab.json>` / `prefab.save` / `prefab.close [true\|false]` | プレハブ編集モードを開く / 保存する / 閉じる |
| アニメーション | `animation.info <オブジェクト>` | Animator のクリップ・トラック・キー |
| | `animation.channels <オブジェクト>` | キーを打てるトラック(チャンネル)の一覧 |
| | `animation.createClip <オブジェクト> <名前>` | 新しいクリップを `Data/Animations/` に作って持たせる |
| | `animation.addKey <オブジェクト> <トラック> <秒> [値]` / `animation.removeKey ...` | キーを打つ(値を省略すると今の値) / 消す |
| | `animation.save <オブジェクト>` | クリップをファイルへ保存する(Save Clip) |
| マテリアル | `material.list` / `material.create [名前]` | マテリアルの一覧(Data 基準のパス) / `Data/Materials/` に作る |
| | `material.get <パス>` / `material.set <パス> <キー> <値>` | 全フィールド(キー名はファイルと同じ) / 1 つ書き換えて保存し、使っているオブジェクトへ反映する(Material の Inspector と同じ処理。Undo 不可) |
| 表示 | `view.colliders [on\|off]` | 全 Collider の形を線で描く(F1 と同じ。省略すると切り替え) |
| 自作シェーダー | `shader.list` | Data 配下の .hlsl と、コンパイルの成否・エラー |
| | `shader.create [名前]` / `shader.reload` | ひな形を `Data/Shaders/` に作る(Inspector の New) / すべてコンパイルし直す(保存すれば自動でも読み直す) |
| 操作 | `action.list` | アクションの一覧(割り当てと今の値・待っているコマンドの数) |
| | `action.set <アクション名> <値> [y の値]` | アクションの値をコマンドで流す(キーを押すのと同じ扱い。Play 中の次の更新で反映) |
| | `action.reload` / `action.device [on\|off]` | `Data/ProjectSettings/InputActions.json` を読み直す / キーボード・パッドを読むかどうか |
| | `action.add <名前> [button\|axis1d\|axis2d]` / `action.remove <名前>` / `action.rename <名前> <新しい名前>` | アクションを足す / 消す / 名前を変える(Project Settings の Input Actions と同じ処理。その場で保存) |
| | `action.type <名前> <種類>` / `action.bind <名前> <割り当て>` / `action.unbind <名前> <割り当て>` | 種類を変える(合わない割り当ては外れる) / 割り当てを足す / 外す |
| | `action.bindings` | 種類ごとに使える割り当ての名前 |
| スクリプト | `script.create <名前> [フォルダ]` | コンポーネントのひな形(.h / .cpp)を作り、GameModule をビルドし直す(Project の Create → Script と同じ)。フォルダはプロジェクト基準、既定は `GameComponents` |
| | `project.sync` | プロジェクトの中の .cpp / .h を `GameModule.vcxproj` と `.filters` に並べ直す(変わったときだけ書く。スクリプトの作成・DLL の読み直し・起動のときにも自動で行う) |
| | `folder.create <パス>` | プロジェクトの中にフォルダを作る(Project の Create → Folder と同じ。例 `folder.create GameComponents/Player`) |
| 選択 | `select <オブジェクト>` / `select none` | Hierarchy の選択を変える |
| 編集 | `object.create <名前> [親]` | 空のオブジェクトを作る |
| | `object.delete <オブジェクト>` | 子ごと削除する |
| | `object.rename <オブジェクト> <新しい名前>` | 名前を変える |
| | `object.active <オブジェクト> <true/false>` | 有効/無効を切り替える |
| | `component.add <オブジェクト> <型名>` | コンポーネントを足す |
| | `component.remove <オブジェクト> <型名[#番号]>` | コンポーネントを外す |
| | `field.set <オブジェクト> <型名[#番号]> <キー> <値>` | フィールドを書き換える(キー `enabled` でコンポーネントの有効/無効) |
| | `undo` / `redo` | Undo / Redo |
| 実行 | `play.start` / `play.stop` | Play / Stop |
| | `wait <フレーム数>` | 指定フレーム進むのを待ってから返す(Play 中の変化を見るため。待っている間のログも返す) |
| | `scene.save` | シーンを保存する(Ctrl+S と同じ) |
| | `scene.create <名前>` / `scene.open <名前>` | 空のシーンを作って開く(Project Settings の Scenes の Create Scene と同じ) / シーンを開く(保存していない変更は捨てる)。どちらも切り替わってから返事をする |
| | `scene.startup [名前]` | 起動シーンを変える。名前を省くと、今の起動シーンとシーンの一覧 |
| | `module.reload` | GameModule をビルドし直して差し替える(Reload DLL と同じ) |
| | `quit` | エディタを終了する |

- 編集系のコマンドは Play 中も使える(Inspector と同じ。Stop すると Play 前の状態に戻る)。
- `field.set` は、そのコンポーネントの全フィールドを `WriteJson` で書き出し、1 つだけ差し替えて読み込ませる(`SceneJsonImporter::ApplyComponentProperties`。シーンの読み込みと同じ ReadJson → OnAfterReadJson → 参照の解決を、そのコンポーネントだけに行う)。シーン全体を JSON にして読み戻すことはしない。
  参照フィールド(ObjectRef)も JSON 上の instanceId として書き換えられる。

### 5.1 スクリーンショット(view.screenshot)

- `scene` / `game`: ポスト処理(フォグ・ブルーム・トーンマップ)と画面の UI まで描いた、そのビューの描画結果。
  **タブが隠れているビューも、撮るときだけ描かせる**(同じ場所に Scene と Game がタブで重なっていても撮れる)。大きさは描画先の大きさ。
- `editor`: ImGui まで描いたエディタ全体(バックバッファ)。UI の見た目を確かめる用。隠れたタブは写らないので、先に `window.show` で前に出す。
- 流れ: コマンドが予約 → 描画の途中で読み出し用バッファへコピー → 次のフレームの頭で PNG に保存して返事(`EditorScreenshot`)。
  `DirectXCommon::PostDraw` が毎フレーム GPU の完了を待つので、1 フレーム遅れで確実に読める。
- 保存先の既定は `<プロジェクト>/Temp/Screenshots/<対象>_<日時>.png`(git 管理外)。相対パスはプロジェクトのフォルダ基準。

### 5.2 ログ

- Console のログと、エンジンの `Logger::Log`(シェーダーのコンパイル等)を、`<エンジン>/logs/editor_<日時>.jsonl` に 1 行 1 件の JSON で残す(`EditorLog`)。
  1 行: `{"time", "level", "source"("Console"/"Engine"), "category"(先頭の [..]), "message"}`
- 重さ(info / warning / error)は、今のログが重さを持たないので**文面から判定**している(「失敗」「error」等)。完全ではない。
- Console は重さごとに色分け(警告は黄・エラーは赤)し、件数付きのチェックと文字列で絞り込める。

### 5.3 エディタ機能のコマンド化(UI と同じ処理を呼ぶ)

§3 の決まり 6 のとおり、UI のボタンにあった処理を共通の関数に移し、UI とコマンドの両方から呼ぶようにした。

| 共通の処理 | UI 側 | コマンド |
|---|---|---|
| `Editor/PrefabEditing`(作成・配置・Apply・Revert・Unpack。Undo・選択の移し替え・ログまで) | Hierarchy の Create Prefab とドロップ、Inspector の Apply / Revert / Unpack | `prefab.*` |
| `Editor/AnimationEditing`(クリップ作成・キーの追加と削除・チャンネル一覧) | Animation ウィンドウの Create / Add Key | `animation.*` |
| `SaveMaterialAsset`(`Editor/MaterialInspector.h`。保存・シーンの使用箇所への反映・Inspector の表示の更新) | Material の Inspector での編集 | `material.set` |

- プレハブの Revert / Unpack は、UI から行っても Undo できるようになった(以前は Undo を取っていなかった)。
- アニメーションのキーはクリップ(シーンとは別のファイル)のメモリ上の変更なので、**シーンの Undo では戻らない**。`animation.save` で保存する。
- `animation.createClip` と Animation ウィンドウの Create は、同じ名前のクリップがあると失敗する(以前は上書きしていた)。

### 5.4 ログの重さ

- `EditorConsole::AddLog(message, level)` / `EditorLog::Write(source, message, level)` で重さを明示して出せる。
  CUI・プレハブ・アニメーションのログは明示している。
- 重さを付けずに出した古いログ(`AddLog(message)`・`ImGuiManager::AddConsoleLog`・エンジンの `Logger::Log`)は、今までどおり文面から推測する。

### 5.5 型情報(schema.get)

- `SerializedFieldRegistry` に `Mode::DescribeSchema` を足した。Inspector の表示・JSON の読み書きと**同じ登録**から、
  キー・表示名・型・範囲(min/max)・ドラッグの刻み・説明(ツールチップ)を書き出す。登録が 1 か所なので、Inspector と食い違わない。
- `KUJATA_SERIALIZED_FIELDS_BEGIN` で登録しているコンポーネントは `Component::DescribeSerializedFields` が自動で対応する(`source: "registry"`)。
- Inspector と JSON を**手書きしているコンポーネント**(Transform・ModelRenderer・ライト・コライダー・UI など 18 個)は、
  今の値の JSON から型だけを推測する(`source: "inferred"`。範囲・説明は出ない)。正確にするには登録簿へ移す必要がある(§6)。
- `field.set` は型情報に範囲があれば確かめ、範囲外なら範囲を添えて失敗にする(以前は読み込み時に黙って丸めていた)。

## 6. これからの Step

| Step | 内容 |
|---|---|
| 1(済) | コマンド層、名前付きパイプ、Console の入力欄、`kujata` CLI、`--run` / `--exit` |
|  | 実装中に CUI で見つかって直した既存の不具合: 折りたたまれた親の子を選ぶと Hierarchy が選択を外す / Windows のメッセージを 1 フレーム 1 件しか処理せず入力が遅れる / 最初の編集の Undo ラベルが "Initial" のまま |
| 2(済) | `view.screenshot`、`window.show`、`state.dump`、ログの JSON Lines 出力と Console の色分け・絞り込み、完了を待つコマンドの仕組み(`EditorCommandResult::poll`) |
| 3(済) | ログの重さの明示、プレハブ(`prefab.*`)とアニメーション(`animation.*`)のコマンド化。処理を `PrefabEditing` / `AnimationEditing` に移して UI と共通にした |
|  | 残り: シーンの切り替え(Project Settings の Scenes)・マテリアルの編集・UI 編集モードのコマンド化、古いログ(ホットリロード等)への重さの付与 |
| 4(済) | 型情報: `SerializedFieldRegistry` の `Mode::DescribeSchema` と `schema.get`、`field.set` の範囲チェック |
|  | 残り: 手書きのコンポーネント(18 個)を登録簿へ移す(移せば型・範囲・説明が正確になり、Inspector と JSON の手書きも減る) |
| 5 | MCP サーバー(`kujata` と同じパイプを使う)、コマンドパレット(Ctrl+P)、Undo の履歴ウィンドウ、CUI で変えたオブジェクトの強調表示 |
| 6 | 固定 ID(今は `GenerateInstanceId()` が時刻と乱数で作るので差分がぶれる)と入力の記録・再生。決定論(determinism.md)と一緒に進める |

### CUI でも確かめられないもの

- **UI の部品そのものの不具合**(ボタンの当たり判定、ドラッグ、ドッキング)はコマンドを通らないので見つからない。
  Dear ImGui Test Engine(ウィジェットのラベルで UI を操作する)を検討する。**ライセンスが ImGui 本体と別**なので事前に確認が要る。
- **使い心地・見た目**は人間が判断する。CUI で「壊れていない」ことを確かめてから人間に渡す。

## 7. 決めていないこと

- MCP サーバーを何で書くか(PowerShell / Python / C++)。
- 複数のエディタを同時に起動したときのパイプ名(今は固定の 1 本なので、2 つ目のエディタは CUI を受け付けない)。
- 配布ビルド(エディタ UI なし)で CUI をどこまで使えるようにするか(今はエディタのビルドだけ)。
