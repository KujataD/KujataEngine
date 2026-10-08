# CLAUDE.md — KujataEngine 開発コンテキスト

このファイルは AI アシスタント(Claude Code 等)にプロジェクトの前提を共有するためのもの。
人間向けの概要は [ReadMe.md](ReadMe.md) を参照。
**このファイルと ReadMe.md はエンジン共通の内容だけを書く**(全ゲームのリポジトリで同じ内容に保ち、エンジン更新の取り込みで衝突させないため)。ゲーム固有の情報は `DirectXGame/README.md` に書く。

## プロジェクト概要

- **KujataEngine**: DirectX 12 製の自作ゲームエンジン(Unity 風エディタ内蔵)。
- 作者は学生(専攻: **ゲームAI**)。応答・コメント・ドキュメントは日本語で書くこと。
- **1リポジトリ=1ゲーム**。エンジンは `KujataEngine/`、外部ライブラリは `externals/`、ゲームは `DirectXGame/` にフォルダで分けてある。このリポジトリのゲームについては **`DirectXGame/README.md` を読むこと**。
- 構成: `KujataEngine.sln` → exe(エンジン/エディタ、`KujataEngine/KujataEngine.vcxproj`)+ `GameModule` DLL(ゲームロジック、`DirectXGame/GameModule/`、ホットリロード対応)。

## リポジトリの運用(エンジンとゲーム)

- エンジンの本家はリポジトリ **KujataEngine**(`DirectXGame/` は空のテンプレート)。新しいゲームは KujataEngine を git clone し、元のリポジトリを remote `engine` として残して作る。ハブ(プロジェクト管理アプリ)は作らない。
- エンジンの更新は、ゲーム側で `git fetch engine` → `git merge engine/main` で取り込む。
- 衝突させないためのルール:
  - **ゲームのリポジトリでは `KujataEngine/` と `externals/` を気軽に変えない。** 直す必要があれば別コミットにして、KujataEngine へ cherry-pick で持ち帰る。
  - **KujataEngine では `DirectXGame/`(テンプレート)を変えない。** 変えた場合、各ゲームへは手で反映する。
  - `KujataEngine.sln` はゲームごとに違ってよい(ゲームが使う追加ライブラリのプロジェクトを載せるため)。
- エンジンの新機能(決定論+リプレイ等)は KujataEngine で開発し、`Sandbox/`(git 管理外)で試す。
- **コミットに Claude 名義を入れない**(`Co-Authored-By: Claude` などのアトリビューション行を付けない)。PR 本文も同じ。

## ビルドと検証

```bash
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" KujataEngine.sln -p:Configuration=Debug -p:Platform=x64 -m -v:m -nologo
```

- **ユニットテストは運用しない方針**(Tests プロジェクトは削除済み)。検証は「ビルド成功+実機起動」で行う。
- **エディタの操作・確認は CUI を使う**(`Tools/kujata.cmd`。設計と一覧は [editor-automation.md](editor-automation.md))。スクリーンショットの座標クリックや OS へのキー注入より先にこちらを使う。
  - AI からは、コマンドを標準入力で渡して `-Json` で受け取るのが確実: `printf '%s\n' 'scene.list' 'object.get MonsterBall' | Tools/kujata.cmd -Json`
  - 返事の `logs`(実行中に出たログ)と `state` を毎回確かめ、エディタ側で問題が起きていないかを見る。
  - 見た目の確認は `view.screenshot <scene|game|editor> <パス>` で撮って画像を読む(ウィンドウの撮影より確実。隠れたビューも撮れる)。エディタの UI を撮るときは先に `window.show <ウィンドウ名>` で前に出す。
  - ログは `log.tail 50 error`、または `log.file` の JSON Lines を読む。変更の前後比較は `state.dump` の差分で見る。
  - 起動からの確認を 1 回で済ませるときは `KujataEngine.exe --run <ファイル> --exit`(終了コードで成否が分かる)。
  - 新しいエディタ機能を作ったら、その操作もコマンドとして登録する(`Editor/Commands/` の分野ごとのファイル。共通の関数は `EditorCommandUtil`)。UI にしかない操作を増やさない。処理は UI とコマンドの両方から呼べる関数に置く(例: `Editor/PrefabEditing`・`Editor/AnimationEditing`)。
  - **KujataEngine で `prefab.create` / `animation.createClip` / `scene.save` などを試すと、`DirectXGame/Data` にファイルができる**(テンプレートを変えない決まりに反する)。試した後は消してからコミットする。
  - 新しく書くログは重さを明示して出す(`EditorConsole::AddLog(message, EditorLogLevel::Error)` など)。
  - フィールドを書き換える前に `schema.get <型名>` で型・範囲・説明を確かめる。新しいコンポーネントは `KUJATA_SERIALIZED_FIELDS_BEGIN` で登録する(手書きの DrawInspector/WriteJson だと型情報が推測になる)。ゲームのコンポーネントは `script.create`(Project の Create → Script)でひな形から作り、`.cpp` 末尾の `KUJATA_REGISTER_GAME_COMPONENT(型名);` で登録する(`GameModule.cpp` には書かない)。`GameModule.vcxproj` のソースの一覧はエディタ(`GameProjectSync`)が書き直すので、手で編集しない。
- **Component 等の共有ヘッダ(ABI)を変更したら、必ず .sln 経由で exe と GameModule を同時に再ビルド**すること。片方だけ古いと起動時にエントリポイントエラーで落ちる。
- Release 確認時は Rebuild 禁止(自動 Play で確認可。マウスは効くがキー注入は届かない)。
- **ビルドや確認のとき、エンジン(exe)が起動中ならプロセスを終了してよい**(確認は不要)。起動中だと exe を上書きできず LNK1168 でリンクが失敗する。
- 生成物は `build/` と各プロジェクトの `Temp/` に集約。VS からのデバッグ実行はカレントが `KujataEngine/` になるので、`logs/` はそこに出る。`imgui.ini`(エディタのレイアウトとウィンドウの表示状態)はカレントに関係なく常に `KujataEngine/imgui.ini`(git 管理外)。遊んでもらう用の配布フォルダは `Tools/MakeGameBuild.ps1`(Release をビルドしてから実行)。
- **assimp のライブラリは Git LFS で管理**(`externals/assimp/lib/Debug/assimp-vc143-mdd.lib` と `assimp-vc143-mtd.pdb`、`lib/Release/assimp-vc143-md.lib`。Debug 用 lib は 67MB)。`.gitignore` の `Debug/` `Release/` 規則にかかるので、差し替えるときは `git add -f` が要る。LFS の実体が落ちていない(ポインタのままの)状態だと LNK1104 / LNK4099(警告がエラー扱い)でビルドが通らない。

## ディレクトリ構成(エンジンは約 4 万行)

| パス | 役割 |
|---|---|
| `KujataEngine/` | エンジン一式(`KujataEngine.vcxproj`・`main.cpp`)。**ゲーム固有のものを置かない** |
| `KujataEngine/EngineData` | エンジンが自前で持つデータ(シェーダー、既定テクスチャ `white1x1.png`) |
| `KujataEngine/scene` | コンポーネント基盤の心臓部(Component / GameObject / ComponentFactory / SerializedFieldRegistry / ObjectRef) |
| `KujataEngine/runtime` | EngineContext / GameModuleLoader(DLL 境界)/ SceneManager / PlayState / TagRegistry |
| `KujataEngine/components` | 組み込みコンポーネント(Transform / Collider / Rigidbody / Camera / ライト / UI / Particle 等) |
| `KujataEngine/Editor` | ImGui エディタ一式(Inspector / Hierarchy / Scene・Game ビュー / AnimationWindow) |
| `KujataEngine/3d` `/2d` `/base` | 描画・D3D12 基盤(DirectXCommon / TextureManager 等) |
| `KujataEngine/postprocess` | PostEffectPipeline / Volume(HDR / Bloom / Fog) |
| `KujataEngine/shapes` `/math` | コライダー形状・数学 |
| `externals/` | 外部ライブラリ(imgui / assimp / DirectXTex 等)— 読解・変更の対象外 |
| `DirectXGame/` | このリポジトリのゲーム。**フォルダごと持ち出せば切り離せる** |
| `DirectXGame/GameModule` | ゲーム DLL のプロジェクト(`GameModule.cpp` がコンポーネント登録の入口) |
| `DirectXGame/GameComponents` | ゲーム側ロジック |
| `DirectXGame/Data` | シーン・プレハブ・リソース・`ProjectSettings` |
| `DirectXGame/Game.props` | ゲーム固有のビルド設定(exe 名 `KujataExeName`)。エンジンと GameModule の両方の vcxproj が読む |
| `Sandbox/` | 使い捨ての試作プロジェクト(git 管理外。`--project` で開く) |

## 機能を足すときの方針

- **なるべく今ある機能を使う。** 新しい仕組みを作る前に、既存のコンポーネント・関数・シェーダーの組み合わせでできないかを先に探す(例: 当たり判定は既存の Collider コンポーネントと `ShapeUtil` の判定関数を使い、足すのは足りない部分だけにする)。
- **足すときは、人間が簡単に使えるようにする。**
  - コードを書かずに、コンポーネントを付けて Inspector で設定するだけで使える
  - 既定値のままでもそれらしく動く
  - 各設定にツールチップを付ける(`KUJATA_REGISTER_*_TIP`)
  - 確かめるための表示(当たり判定の形など)を Inspector のチェックで出せる
  - ReadMe に使い方を書く

## コードの書き方

**YAGNI・KISS・DRY・SOLID(主に S)の原則に従って書く。ただし、可読性を何よりも優先する。原則は厳密に守らなくてよい。**

- **YAGNI**: 今使わない機能・設定・抽象化は作らない(「いつか使うかも」では足さない)。
- **KISS**: いちばん単純に書ける方法を選ぶ。
- **DRY**: 同じ処理や同じ値を何か所にも書かない。ただし、まとめるとかえって読みにくくなるなら、重複を残してよい。
- **S(単一責任)**: 1 つのクラス・関数には 1 つの役割を持たせる。
- 原則どうし、または原則と読みやすさがぶつかったら、読みやすい方を選ぶ。

## 重要な規約・罠(要点のみ)

- **アセット参照は 2 層**: assetId(`.meta`)+パス fallback。`.meta` は git 管理必須。ID はセット時に自動補完する。
- **半透明は深度を書かない**ので、Scene が不透明物をすべて描いた後に、カメラから遠い順に描く(シーンの並び順は関係ない)。深度を書かない描画をするコンポーネントを新しく作ったら、`Component::IsTransparentDraw` で true を返すこと(返さないと、後から描かれる奥の物に上書きされて消える)。
- **自作シェーダー**(マテリアルの `shaderPath`)は `EngineData/shader/Object3dCustom.hlsli` を include して `PSMain`(必須)/`VSMain`(省略可)を書く。定数のレジスタ(b0〜b5・t0/t2・s0/s1)は `GraphicsPipeline::CreateObject3dRootSignature` と `Object3dPixel.hlsli` / `Object3dVertex.hlsli` / `Object3d.hlsli` で一致させる。コンパイルに失敗してもエンジンは止まらず、直前に成功した版(無ければ標準)で描く。
- **自作シェーダーの `gSceneDepth`(t3)は、半透明の描画中だけ本物**。Scene が不透明物を描き終えたところで深度をコピーし(`DirectXCommon::CaptureSceneDepth`)、それ以外の描画では白(=いちばん遠い)が入る。不透明物の深度を読みたいコンポーネントは `IsTransparentDraw` を true にする(深度は書いてもよい。先に描くなら `GetTransparentQueue` を負に)。
- **シェーダーと並びを合わせる C++ の構造体**: `ShaderParamsData`(3d/Model.h)↔ `ShaderParams`、`CameraForGPU`(3d/Camera.h)↔ `Camera`(どちらも EngineData/shader/Object3d.hlsli)。
- **海の波の式は 2 か所にある**: `EngineData/shader/Custom/Ocean.hlsl` の `WaveHeight`(見た目)と `OceanComponent::EvaluateWaves`(高さの問い合わせ)。片方だけ変えると、浮かぶ物と海面がずれる。
- **操作は必ずアクション層を通す**: ゲームのコードから `Input::`(生のキー・パッド)を直接読まない。`InputActionSystem::GetInstance()->GetActions()` の `ActionState`(`Held` / `Pressed` / `Axis2D` 等)を読む。生の入力を読むのは `InputActionSystem::CollectFromDevices` の中だけで、そこから `ActionCommand` を `CommandQueue` へ積み、更新の頭で実行してアクションの値を更新する(キーボード・AI・CUI・将来のリプレイで入口の形をそろえるため)。デバッグカメラなど Presentation 層は今までどおり `Input::` でよい。
- **Play の状態持ち越し**: コンポーネントは使い回されるので、非シリアライズ状態は `OnPlayStart` で必ず初期化する。
- GameModule DLL からエンジン側シンボルを使うには `KUJATA_API` エクスポートが必要(未エクスポートだとリンク不可)。
- テクスチャ/フォントの読み込みは描画パス外(Prepare)で行うこと。日本語パスでテクスチャ読込が死ぬ罠あり。
- **Scene ビューと Game ビューは同じフレームで両方描く**(GPU が実際に描くのはフレームの最後)。描くたびに書き換える GPU のバッファ(カメラの行列を入れる定数・インスタンスのバッファ・毎フレームの頂点)は、**ビューごとに別のものを持つ**(`DirectXCommon::GetRenderViewIndex()` で選ぶ。例: `WorldTransform`・`LineRenderer`・`ParticleModel`・`SplineRendererComponent`)。1つを共有すると、先に描いたビューも後のビューの中身(別のカメラ)で描かれて、Game ビューを開いているときだけ Scene ビューでずれて見える。
- **Game の描画先は画面より小さいことがある**(カメラの `pixelSize` によるドット絵化)。ビューポートを `WinApp::kWindowWidth` 固定で書かず、`DirectXCommon::GetCurrentTargetWidth/Height`(今の描画先)を使う。UI の座標(クリック判定・Canvas のレイアウト)は `GetGameOutputWidth/Height`、3D を描く先の大きさは `GetGameRenderWidth/Height`。
- **エンジンとゲームの境界**: ゲーム固有の設定は `DirectXGame/Game.props`(exe 名)と `DirectXGame/Data/ProjectSettings/Project.json`(ウィンドウタイトル・背景色)に置き、`KujataEngine/` には書かない。
- **パスの起点は2つ**(`base/ProjectPath.h`): エンジンの持ち物は `GetEngineRoot()`(= `KujataEngine/`)/ `GetEngineDataRoot()`、プロジェクトの持ち物(GameModule・Data・Temp)は `GetActiveProjectRoot()` / `GetProjectDataRoot()` を使う。どちらも起動時に一度だけ決まり、キャッシュされる。
- **開くプロジェクトの決まり方**: 起動引数 `--project <フォルダ>` → 無ければエンジンの隣の `DirectXGame/` → それも無ければ(配布先)エンジンのフォルダ(= exe の隣)。
- GameModule.dll はプロジェクトの `GameModule/bin/<構成>/` を優先し、無いとき(配布先)だけ exe の隣を読む。
- エンジンのソースは外部ライブラリを `"../../externals/imgui/imgui.h"` のような相対パスでインクルードしている。`KujataEngine/` と `externals/` の位置関係(同じ階層に並ぶ)を変えないこと。
- ゲームコードからエンジンのヘッダは `"components/ImageComponent.h"` のようにインクルードパス基準で書く(相対パスは使わない)。
- `SampleScene`(エンジン側)は `Resources/plane/plane.gltf` と `resources/white1x1.png` を名前で読むので、プロジェクトの Data にこの2つが無いと起動しない(エンジンがプロジェクトのアセット名を知っている既知の設計問題)。

## 現在の方針(2026-09 時点)

1. **アーキテクチャは OOP+コンポーネントを維持**。ECS への全面書き換えはしない(DOD/ECS の学習は別リポジトリの小品で行う)。
2. エンジン独自の強みとして**決定論+リプレイ**を柱にする(固定タイムステップ+入力列→状態。GOAP/NN エージェントの評価・高速学習・再現デバッグ基盤にするため)。**設計は [.claude/determinism.md](determinism.md) が一次情報**。決定論まわりを触る前に必ず読むこと(層の線引き・Step の順序・守るべき規約がそこにある)。ティック・アクション層・コマンドの列・リプレイの詳細は [.claude/tick-replay.md](tick-replay.md)。
3. **コードリーディングと図解では、変更後のコードだけを読む**(変更前との比較はしない)。
4. 決定論化の実装では、全体の読解はせず、必要な箇所(更新ループ、rand の呼び出し箇所、deltaTime / 実時間への依存、イテレーション順が不定なコンテナ)だけを的を絞って調べる。

## ドキュメント運用ルール

- **CLAUDE.md(本ファイル)**: AI と共有する前提・規約・現在の方針(エンジン共通)。方針転換・構成変更・新しい罠の発見時に**その場で更新**する。セッション限りの詳細は書かない。**AI への指示・作業の好みは Claude のメモリ機能に保存せず、ここに書く**(メモリはリポジトリに残らず、他の環境や人と共有できないため)。
- **ReadMe.md**: 人間向けの概要・ビルド手順・操作方法(エンジン共通)。**仕様の追加・修正・変更をしたら、毎回 ReadMe.md も更新する**(同じコミットに含める)。**ReadMe はこれ1つに一本化**し、`KujataEngine/` などソースのフォルダに ReadMe を作らない(例外は下の `DirectXGame/README.md`)。
- **DirectXGame/README.md**: そのゲーム固有の説明・依存・設定の置き場所。
- **`docs/` は作らない。** 設計書・図解・調査メモなど AI(Claude)が作るドキュメントは、すべて `.claude/` に置く(例: 決定論の設計 `.claude/determinism.md`)。コードリーディングの図解成果物も `.claude/` に置く。全クラスの関係図は `.claude/class-map.html`(ソースから `python Tools/class_map/build.py` で作り直す。クラスを増やした・関係を変えたら作り直す)。
- コードから自明なこと(クラス一覧・過去の修正履歴)はどのドキュメントにも書かない。git log と実コードを一次情報とする。
