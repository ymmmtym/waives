# AGENTS.md — waives

JUCE 製オーディオプラグインのモノレポ。このファイルは、ここで作業する AI エージェント
向けの実務メモ。使い方の説明は `README.md` に、macOS 実機での確認手順は
`docs/logic-pro-checklist.md` にある。

---

## 1. リポジトリの形

| 項目 | 内容 |
|------|------|
| 構成 | `plugins/<PluginName>/` に 1 プラグイン 1 ディレクトリ |
| 形式 | Linux: VST3 / CLAP / Standalone、macOS: 加えて AU |
| 言語 | C++17 |
| ビルド | CMake 3.22+ / Ninja |
| バージョン | `VERSION` が唯一の源。リリースタグは `v<VERSION>-<sha7>` |

いまあるプラグイン:

| プラグイン | 内容 |
|-----------|------|
| `MyFirstMasteringEQ` | 8 バンド・ピーキング EQ（バンド単位のバイパス / ソロ、特性グラフ） |
| `MyFirstTapeMachine` | テープ・シミュレーター（ワウ / フラッター / サチュレーション / ヒス） |
| `MyFirstTubeAmp` | 真空管アンプ（2 段 / トーン・スタック / サグ / キャビネット） |

```
CMakeLists.txt              # WAIVES_PLUGINS に並んだものだけを configure する
cmake/WaivesPlugin.cmake    # 形式・警告フラグ・テストハーネス・AU コードの共通化
cmake/Dependencies.cmake    # JUCE の取得（.cache/fetchcontent/）
shared/WaivesDsp.h          # RBJ 係数 / biquad / ノイズ（プラグイン共通）
tests/TestUtilities.h       # check() などのヘッドレステスト用ヘルパ
plugins/<Name>/
├── CMakeLists.txt          # そのプラグインの差分だけ
├── Source/                 # PluginProcessor / PluginEditor / 係数計算
├── tests/ProcessorTest.cpp # ヘッドレステスト（ctest に登録される）
└── benchmarks/             # 任意。処理コストの計測（ctest には入れない）
scripts/install-linux.sh    # ビルド済みプラグインを ~/.vst3 などへ
scripts/verify-macos.sh     # macOS 実機で auval / pluginval をまとめて回す
```

---

## 2. ビルドとテスト

```bash
# 依存（Debian/Ubuntu。README の「1. Linux の依存パッケージ」と同じ）
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxi-dev \
  libgl1-mesa-dev libglu1-mesa-dev libegl-dev libcurl4-openssl-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --no-tests=error --output-on-failure
```

`--no-tests=error` は必須。`WAIVES_BUILD_TESTS=OFF` で configure した `build/` を使い
回すとテストが 1 件も登録されず、CTest は**0 件でも成功を返す**。`--no-tests=error` なら
落ちる。実行件数（`100% tests passed, 0 tests failed out of N`）も毎回読む。

- `libfontconfig1-dev` が無いと configure が `juceaide` のリンクで落ちる。
- macOS の AU は**指定したアーキテクチャの分だけ**できる。CI は universal
  （`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`）で作るので、ローカルでも同じものを
  作りたいときはこの指定を付ける。省略すると実行中の Mac のアーキテクチャのみになる。
- JUCE は初回 configure で `.cache/fetchcontent/` に入る（`build/` を消しても再取得しない）。
- 出力は `build/plugins/<Name>/<Target>_artefacts/Release/{VST3,CLAP,Standalone,AU}/`。
- オプション: `-DWAIVES_BUILD_CLAP=OFF`、`-DWAIVES_BUILD_TESTS=OFF`、
  `-DWAIVES_BUILD_BENCHMARKS=OFF`。

**ビルドが本当に走ったかを確認する。** `cmake` が PATH に無いと "command not found" が
stderr に出るだけで、`| grep error` では成功に見える。テストは実行ファイルの
タイムスタンプ（`ls -l --time-style=+%H:%M:%S`）を確かめてから信じる。

---

## 3. プラグインを足すとき

1. `plugins/<Name>/CMakeLists.txt` に `waives_add_plugin()` と
   `waives_add_plugin_tests()` を書く。`MANUFACTURER_CODE` / `PLUGIN_CODE` は
   4 文字で、プラグイン間で重複すると configure が止める。
   **`CLAP_ID`（と `CLAP_FEATURES`）を必ず書く。** `waives_add_plugin()` は `CLAP_ID`
   が無いと CLAP をビルドしないのに、CI の clap-validator は CLAP バンドルを無条件に
   検証するので、書き忘れると CI だけが落ちる。
2. ルート `CMakeLists.txt` の `WAIVES_PLUGINS` に名前を足す。
3. `.github/workflows/build.yml` と `release.yml` の `matrix.plugin` に足す。
4. `README.md` の表・プラグイン節・「4. 信号処理」の表を更新する。
5. `tests/ProcessorTest.cpp` を書く（下記 5 章）。

形式（VST3 / CLAP / Standalone、macOS では AU）は `waives_add_plugin()` が決めるので、
プラグイン側で `FORMATS` を書かない。

---

## 4. コードのルール

### リアルタイム安全

- `processBlock()` の中で **new / delete / malloc / ロック / `std::function` 呼び出しをしない**。
  バッファ・遅延線・オーバーサンプラーは `prepareToPlay()` で作る。
- ホストが `prepareToPlay()` より長いブロックを渡すことがあるので、内部バッファは
  固定サイズのチャンクに切って処理する。
- 係数は `std::array<float,5>` の代入で更新する（ヒープを使わない）。

### 音

- パラメータ変更は `juce::SmoothedValue`（20 ms が標準）で追従させ、クリックを出さない。
  フィルタ係数の再計算は 32〜64 サンプル単位に間引く。
- **バイパスはレイテンシ整合させる**。`processBlockBypassed()` を自前でオーバーライドし、
  「遅延させただけの入力」へクロスフェードする。JUCE 既定の実装はレイテンシを守らない。
- `juce::dsp::Oversampling` は `useIntegerLatency = true` で作り、報告レイテンシを整数にする。
- サンプルレート依存の係数を UI スレッドで作らない（`prepareToPlay()` で作る）。
- 0 dB / off のときに「数学的には恒等」でも bit-exact にならない場合がある（FMA）。
  素通しを保証したいところは恒等係数に差し替える。このとき**フィルタの状態
  （`z1` / `z2`）もリセットする**。係数だけ差し替えると、前の音の残りが次の出力に
  足される（`MyFirstMasteringEQ` の `Biquad::reset()` がその例）。

### パラメータ

- `AudioProcessorValueTreeState` を使い、`getRawParameterValue()` の
  `std::atomic<float>*` をキャッシュして毎ブロック `load()` する。
- **パラメータ ID は保存済みセッションと automation の互換そのもの。** 既存 ID を
  改名しない。EQ のバンド 1 だけ接尾辞が無いのは v0.1.0 の ID を残しているため。
- 表示文字列は `AudioParameterFloat` の `stringFromValue` で単位まで作る。

### UI

- エディタはホストがいつ壊してもおかしくない。`Timer`・非同期コールバック・
  `PopupMenu::showMenuAsync` は **`juce::Component::SafePointer`** を捕まえる
  （`this` を生で捕まえない）。
- 禁止なのは**音声処理中のバッファ確保**。つまり `processBlock()` の中（やオーディオ
  スレッドから間接的に呼ばれる場所）で `juce::AudioBuffer::setSize()` を呼ぶこと。
  `prepareToPlay()` の中では自由に `setSize()` してよい。ホストはブロックサイズを
  変えて `prepareToPlay()` を**何度でも呼び直す**ので、そのたびに内部バッファを
  作り直すのが正しい。
- エディタ側の `juce::Component::setSize()` は別物で、コンストラクタ / `resized()` で
  呼んでよい。
- `ComboBox` は「すでに選ばれている項目」のクリックを報告しない。プリセットの
  再選択が要るなら `TextButton` + `PopupMenu` にする。
- 色は `PluginEditor.cpp` の先頭にまとめた定数を使い、OS 依存のネイティブ UI を使わない。

### スタイル

- JUCE の規約に従う（4 スペース、`UpperCamelCase`、`camelCase` のメンバ）。
- 警告フラグは `cmake/WaivesPlugin.cmake` が全ターゲットに付ける（`-Wall -Wextra
  -Wshadow -Wfloat-equal -Wsign-conversion` など）。新しい警告を増やさない。
- コメントは「なぜ」だけを書く。コードを読めば分かること、変更履歴、PR の説明は書かない。

---

## 5. テスト

`tests/ProcessorTest.cpp` が本体。ホストもオーディオデバイスも使わず、実物の
`AudioProcessor` の `processBlock()` を呼ぶ。フレームワークは使わない。

```cpp
waives::test::check (condition, "what the check proves");   // 1 行出力、失敗で exit 1
```

- モックを使わない。実コードパスを通す。
- 数値は「壊れたら落ちる」境界で書く（例: バイパスは 1e-6、レベルは ±0.5 dB）。
- 状態を持つ API に注意する。例: メーターは**読むまでピークを保持する**ので、
  測る前に一度読んで捨てる。
- バイパスのテストは経路を明示する。パラメータ経由とホスト経由
  （`processBlockBypassed()`）は別のコードなので、パラメータは
  `hostBypass ? 0 : 1` にしたうえで、ホスト経由では `processBlockBypassed()` を直接呼ぶ。
- 時間がかかる確認（pluginval / clap-validator / auval）は CI と
  `scripts/verify-macos.sh` の仕事。テストには入れない。

---

## 6. PR とリリース

- 1 PR = 1 テーマ（1 プラグイン、1 修正）。CI は **プラグイン × プラットフォーム**の
  マトリクスで、VST3 は pluginval、CLAP は clap-validator、AU は auval + pluginval まで回す。
- マージは squash。タイトルは `... (#N)` の形（既存の履歴に合わせる）。
- バージョンを上げたい PR で `VERSION` を書き換える。main への push で
  `release.yml` が `v<VERSION>-<sha7>` のタグと Release を作る。
- CodeRabbit は **base が main でない PR を自動レビューしない**。スタックした PR は
  `@coderabbitai review` とコメントして手動で走らせる。

---

## 7. 実際に踏んだ穴

| 症状 | 原因 | 対処 |
|------|------|------|
| configure が `Failed to build juceaide` で落ちる | `libfontconfig1-dev` が無い | README の依存を入れる |
| バイパスすると音が早く鳴る（二重に聞こえる） | `processBlockBypassed()` がレイテンシを守らない | 自前でオーバーライドし、遅延ドライへクロスフェード |
| バイパスで音量が変わる | トリムを遅延線に書く前 / 後に掛けていた | 遅延線は生の入力。トリムは wet 経路だけに掛ける |
| エフェクトの効き方がサンプルレートで変わる | 基本レート用の係数をオーバーサンプリング後に使った | 非線形の前後は基本レートで処理する |
| メーターが OUTPUT トリムを無視する | トリム前のサンプルを渡していた | 最終出力を渡す |
| エディタを閉じた直後に落ちる | 非同期コールバックが `this` を捕まえていた | `Component::SafePointer` |
| プリセットを選び直せない | `ComboBox` が既選択項目のクリックを報告しない | `TextButton` + `PopupMenu` |
| CI の AU 検証がたまに落ちる | `AudioComponentRegistrar` が古い登録を持っている | registrar を再起動してリトライ（workflow が実施） |
