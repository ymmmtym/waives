# waives

[![build](https://github.com/ymmmtym/waives/actions/workflows/build.yml/badge.svg)](https://github.com/ymmmtym/waives/actions/workflows/build.yml)
[![release](https://github.com/ymmmtym/waives/actions/workflows/release.yml/badge.svg)](https://github.com/ymmmtym/waives/releases)
[![license: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](LICENSE)

JUCE 製オーディオプラグインのモノレポ。プラグインは 1 ディレクトリ 1 本で `plugins/` 以下に並ぶ。
Linux では VST3 / CLAP / スタンドアローンを、macOS では加えて AU をビルドする。

| プラグイン | 内容 | 形式 |
|-----------|------|------|
| [`MyFirstMasteringEQ`](plugins/MyFirstMasteringEQ/) | 4 バンド・ピーキング EQ（マスタリング用） | VST3 / CLAP / Standalone / AU |
| [`MyFirstTapeMachine`](plugins/MyFirstTapeMachine/) | テープ・シミュレーター（サチュレーション / ワウ・フラッター / ヒス） | VST3 / CLAP / Standalone / AU |

```
plugins/<PluginName>/
├── CMakeLists.txt       # そのプラグインの差分だけを書く
├── Source/              # AudioProcessor / PluginEditor / DSP
└── tests/               # ヘッドレステスト（ctest に自動登録）
```

共通部分（JUCE の取得、出力フォーマット、警告フラグ、テスト用ハーネス）は
`CMakeLists.txt` と `cmake/WaivesPlugin.cmake` にまとめてある。

## プラグイン

### MyFirstMasteringEQ — 4 バンド・ピーキング EQ

`plugins/MyFirstMasteringEQ/`。バンドごとの Frequency / Gain / Q と Band Bypass、
バンド選択付きのノブ UI、4 バンドの合成カーブを描く周波数特性グラフ
（ノードをドラッグして操作できる）、10 ms クロスフェード付きのバイパス
（全体 / バンド単位）、バンド単位のソロを実装している。

#### パラメータ

4 バンドのピーキング EQ を**直列**につないでいる。各バンドが Frequency / Gain / Q を持つ。

| バンド | Frequency | Gain | Q | Band Bypass |
|--------|-----------|------|----|-------------|
| 1 | `freq` | `gain` | `q` | `bypass1` |
| 2 | `freq2` | `gain2` | `q2` | `bypass2` |
| 3 | `freq3` | `gain3` | `q3` | `bypass3` |
| 4 | `freq4` | `gain4` | `q4` | `bypass4` |

バンド 1 だけ接尾辞がないのは、**v0.1.0（1 バンド版）の ID をそのまま残している**ため。
既存セッションを開くとバンド 1 に値が入り、バンド 2〜4 は 0 dB なので音は変わらない。

| 表示名 | 範囲 | デフォルト | スケール | 表示例 |
|--------|------|-----------|----------|--------|
| Frequency | 20 Hz – 20 kHz | 1 kHz | 対数（1 kHz が中央） | `1.00 kHz` / `440.0 Hz` |
| Gain | -18 – +18 dB | 0 dB | 線形（0.01 dB 刻み） | `6.00 dB` |
| Q | 0.1 – 10 | 1.0 | 対数（1.0 が中央） | `4.00` |
| Bypass | off / on | off | 2 値 | — |
| Band Bypass | off / on | off | 2 値 | — |

`bypass` は全バンドをまとめて素通しにする。`bypass1`〜`bypass4` は**そのバンドだけ**を
素通しにし、他のバンドはそのまま効き続ける。どちらも切り替えは 10 ms の
クロスフェードなので、クリックノイズは出ない。

**ソロはパラメータではない**。聴き比べのための一時的な状態なので、セッションには
保存されない（開き直すと解除されている）。

#### バンドの選び方

- **1〜4 のボタン**で編集するバンドを選ぶ。3 つのノブは選択中バンドの値に切り替わる
- **グラフのノードをドラッグ**すると、そのバンドの Frequency（横）と Gain（縦）が変わる。
  ノードをクリックするとそのバンドが選択され、ノブも追従する
- **ノードの上でマウスホイール**を回すと Q が変わる（上で狭く、下で広く）
- グラフには 4 バンドそれぞれの細いカーブ（バンドごとに色分け）と、
  その**合計を描く太いカーブ**が出る
- **SOLO** を押すと、選択中のバンドだけが聞こえる。他のバンドは 10 ms でフェードアウトし、
  解除すると元の音量に戻る。バンドを切り替えるとソロもそのバンドに移る
- **BAND OFF** は選択中のバンドだけを素通しにする（全体の Bypass とは別）
- ノブのダブルクリックでデフォルト値に戻る

バンドの色はボタンとグラフで共通（1 = 紫、2 = 緑青、3 = 橙、4 = 桃）。
同じ位置にノードが重なっているときは、選択中のバンドが優先して掴まれる。
Bypass 中・ソロで外れているバンドは、カーブが薄く、ノードが中抜きで描かれる。

#### プリセット

DAW のプリセットメニュー（`getNumPrograms()` / `setCurrentProgram()`）と、
エディタ左上のドロップダウンの両方から選べる。

| # | 名前 | Frequency | Gain | Q | 用途 |
|---|------|-----------|------|----|------|
| 1 | Flat | 1 kHz | 0 dB | 1.0 | 素通し（比較用の起点） |
| 2 | Low end trim | 80 Hz | -2 dB | 0.7 | 低域の膨らみを軽く整理 |
| 3 | Presence | 3 kHz | +1.5 dB | 1.0 | ボーカルの抜けを持ち上げる |
| 4 | Air | 12 kHz | +2 dB | 0.7 | 高域の空気感 |

- プリセットは**バンド 1 の値と、他バンドのゲイン 0 dB 化**をまとめて行う（他バンドの周波数と Q は残す）
- プリセットは Band Bypass をすべて解除する（ソロはそのまま）
- ノブを動かしても音はそのまま変わり、プリセット名は「最後に選んだもの」として残る
- セッションを保存すると選択中のプリセット番号も保存され、開き直すと名前が復元される

### MyFirstTapeMachine — テープ・シミュレーター

`plugins/MyFirstTapeMachine/`。テープの「録音 → 再生」を 1 本の信号経路として組んだもの。
キャラクターを作る 3 つ（サチュレーション、ワウ・フラッター、ヒス）を、それぞれ独立したノブにしてある。

信号経路:

```
入力トリム
  → プリエンファシス（+4 dB @ 3 kHz）
  → 遅延線（ワウ / フラッターで読み出し位置が揺れる）
  → サチュレーション（2 倍にオーバーサンプリング）
  → ヘッドバンプ（+2.5 dB @ 65 Hz）
  → ディエンファシス（-4 dB @ 3 kHz）
  → トーン（ローパス 2〜18 kHz）
  → DC カット
  → ヒス
  → MIX（遅延したドライと混ぜる）
  → 出力トリム
```

- **ワウ / フラッター**は遅延線の読み出し位置を LFO で動かしている。ワウは 0.63 Hz（最大 3 ms）、
  フラッターは 6.3 Hz と 9.7 Hz の 2 成分（最大 0.35 ms）。片方だけだと機械的に聞こえるため
- **サチュレーション**は非対称の tanh。バイアスをかけているので**偶数次倍音**が出る
  （対称なカーブだと奇数次だけになり、ファズっぽくなる）。小信号のゲインは 1 のままなので、
  DRIVE は音量ではなく質を変える
- 倍音が折り返して汚くならないよう、サチュレータだけ 2 倍にオーバーサンプリングしている
- 遅延線の分だけレイテンシーがある（48 kHz で約 3.6 ms）。`setLatencySamples()` で報告するので
  ホストが補正する
- MIX は**遅延したドライ**と混ぜる。遅延前のドライと混ぜると、その差がコムフィルターになるため

#### パラメータ

| 表示名 | ID | 範囲 | デフォルト | スケール | 説明 |
|--------|-----|------|-----------|----------|------|
| Input | `input` | -12 – +12 dB | 0 dB | 線形 | テープに送るレベル |
| Drive | `drive` | 0 – 10 | 2 | 線形 | サチュレーションの強さ。0 でほぼ素通し |
| Tone | `tone` | 2 – 18 kHz | 12 kHz | 対数 | 再生側の高域損失 |
| Wow | `wow` | 0 – 100 % | 25 % | 線形 | 遅い速度むら（最大 3 ms） |
| Flutter | `flutter` | 0 – 100 % | 15 % | 線形 | 細かい速度むら（最大 0.35 ms） |
| Hiss | `hiss` | -96 – -24 dB | -84 dB | 線形 | テープノイズの実効値。-96 dB で無音 |
| Mix | `mix` | 0 – 100 % | 100 % | 線形 | 遅延ドライとのミックス |
| Output | `output` | -12 – +12 dB | 0 dB | 線形 | 出力トリム |
| Bypass | `bypass` | off / on | off | 2 値 | 10 ms クロスフェード付きの素通し |

#### プリセット

| # | 名前 | Drive | Tone | Wow / Flutter | Hiss | 用途 |
|---|------|-------|------|---------------|------|------|
| 1 | Default | 2 | 12 kHz | 25 % / 15 % | -84 dB | 素のテープ |
| 2 | Warm slap | 5 | 9 kHz | 30 % / 20 % | -78 dB | 太く、少しだけ揺らす |
| 3 | Lo-fi cassette | 7 | 4.5 kHz | 60 % / 45 % | -66 dB | カセットの劣化 |
| 4 | Mastering glue | 1.5 | 16 kHz | 8 % / 4 % | -96 dB | マスターバスに薄く |
| 5 | Parallel crush | 9 | 6 kHz | 20 % / 10 % | -72 dB | MIX 40 % で潰す |

ヘッドレステストは、DRIVE を上げると THD が増えること、フルスケール入力でも出力が
1.0 を超えないこと、DRIVE 0 のときにレベルが入力と一致すること（-3.0 dB RMS）を確認している。

## 環境

| 項目 | 内容 |
|------|------|
| JUCE | 9.0.6（CMake の `FetchContent` で自動取得） |
| CLAP | [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)（JUCE 本体には CLAP 出力がないため） |
| C++ | C++17 |
| ビルド | CMake + Ninja |

## すぐ使う（ビルド不要）

[Releases](https://github.com/ymmmtym/waives/releases) にビルド済みのバンドルがある。
main にマージするたびに自動で作られ、タグは `v<バージョン>-<短い SHA>`（例 `v0.1.0-1899315`）。
バージョンはリポジトリ直下の `VERSION` で決まる。

プラグイン × OS ごとに 1 つのアーカイブが付く。

| ファイル | 中身 |
|----------|------|
| `<Plugin>-linux.tar.gz` | VST3 / CLAP / Standalone |
| `<Plugin>-macos.tar.gz` | AU / VST3 / CLAP / Standalone（universal） |
| `SHA256SUMS` | 上記のチェックサム |

`<Plugin>` は `MyFirstMasteringEQ` と `MyFirstTapeMachine`。
展開すると `<Plugin>_artefacts/Release/`、`LICENSE`、`THIRD-PARTY-NOTICES.md` が出てくる。

### Linux

```bash
# 検証に失敗した時点で止める
set -e

# これから展開するアーカイブだけを検証する（両方落としていれば
# `sha256sum -c SHA256SUMS` でまとめて検証してもよい）
grep -F '  MyFirstMasteringEQ-linux.tar.gz' SHA256SUMS | sha256sum -c -

tar -xzf MyFirstMasteringEQ-linux.tar.gz

mkdir -p ~/.vst3 ~/.clap ~/.local/bin
cp -R MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 ~/.vst3/
cp -R MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap ~/.clap/
install -Dm755 MyFirstMasteringEQ_artefacts/Release/Standalone/MyFirstMasteringEQ \
    ~/.local/bin/MyFirstMasteringEQ
```

### macOS

macOS には `sha256sum` が無いので `shasum` を使う。

```bash
# 検証に失敗した時点で止める
set -e

# これから展開するアーカイブだけを検証する（両方落としていれば
# `shasum -a 256 -c SHA256SUMS` でまとめて検証してもよい）
grep -F '  MyFirstMasteringEQ-macos.tar.gz' SHA256SUMS | shasum -a 256 -c -

tar -xzf MyFirstMasteringEQ-macos.tar.gz

mkdir -p ~/Library/Audio/Plug-Ins/{Components,VST3,CLAP}

# バンドル本体をコピーする（AU/ や VST3/ のディレクトリごとではない）
cp -R MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component \
      ~/Library/Audio/Plug-Ins/Components/
cp -R MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
cp -R MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap \
      ~/Library/Audio/Plug-Ins/CLAP/

# スタンドアローンは .app。そのまま起動するか /Applications へ移す
open MyFirstMasteringEQ_artefacts/Release/Standalone/MyFirstMasteringEQ.app
```

配布物はアドホック署名なので、Gatekeeper の隔離属性を外す必要がある（§7 を参照）。

自分でビルドする場合は §1 以降に進む。

## 1. Linux の依存パッケージ

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config git \
  libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxi-dev \
  libgl1-mesa-dev libglu1-mesa-dev libegl-dev \
  libcurl4-openssl-dev
```

`libxi-dev`（XInput2）と EGL は JUCE 9 で新たに必須になった。
Arch / Fedora では `alsa-lib`, `freetype2`, `fontconfig`, `libx11`, `libxcomposite`,
`libxcursor`, `libxext`, `libxinerama`, `libxrandr`, `libxrender`, `libxi`,
`mesa`, `libegl`, `curl` の開発パッケージを入れる。

## 2. ビルド

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

JUCE は初回の configure 時に `.cache/fetchcontent/` へダウンロードされる
（`build/` を消しても再ダウンロードされない）。

### 出力物

プラグインごとに `build/plugins/<PluginName>/<Target>_artefacts/Release/` へ出力される。
`MyFirstMasteringEQ` の場合はこうなる:

| 形式 | パス |
|------|------|
| VST3 | `build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3` |
| CLAP | `build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap` |
| Standalone | `build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/Standalone/MyFirstMasteringEQ` |

特定のプラグインだけビルドすることもできる:

```bash
cmake --build build --target MyFirstMasteringEQ_VST3
```

### ホストから見えるようにする

```bash
mkdir -p ~/.vst3 ~/.clap
cp -r build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 ~/.vst3/
cp    build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap  ~/.clap/
```

## 3. インストール（プラグインを DAW から使えるようにする）

ビルドしただけでは DAW の一覧に出ない。ホストが探すディレクトリに置く必要がある。

### Linux（ユーザー単位・sudo 不要）

```bash
./scripts/install-linux.sh
```

`build/` 以下にある `*_artefacts/Release` をすべて探してインストールするので、
プラグインが増えてもスクリプトの変更は不要。特定のビルドディレクトリを使う場合は
`./scripts/install-linux.sh build-clang` のように渡す。

これで以下にコピーされる（例: `MyFirstMasteringEQ`）:

| 形式 | 置き場所 |
|------|---------|
| VST3 | `~/.vst3/MyFirstMasteringEQ.vst3` |
| CLAP | `~/.clap/MyFirstMasteringEQ.clap` |
| Standalone | `~/.local/bin/MyFirstMasteringEQ` |

システム全体に入れたい場合（要 sudo）:

```bash
sudo cp -r build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 /usr/local/lib/vst3/
sudo cp    build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap /usr/local/lib/clap/
```

ホスト側で再スキャンする（Carla なら再起動、Reaper なら「プラグインを再スキャン」）。

### macOS

```bash
# 形式ごとの置き場所が無い場合は先に作る（初回インストール時）
mkdir -p ~/Library/Audio/Plug-Ins/Components \
         ~/Library/Audio/Plug-Ins/VST3 \
         ~/Library/Audio/Plug-Ins/CLAP

# AU（Logic 用）
cp -R build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component \
      ~/Library/Audio/Plug-Ins/Components/
# VST3 / CLAP
cp -R build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
cp -R build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap \
      ~/Library/Audio/Plug-Ins/CLAP/

# Gatekeeper の隔離属性を外し、ローカル署名を付ける
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component

# AU の検証（aufx = エフェクト, Mfmq = PLUGIN_CODE, Myau = 製造元コード）
auval -v aufx Mfmq Myau
```

Logic は起動時に AU を再スキャンする。出てこない場合は Logic を終了して
`killall -9 AudioComponentRegistrar` してから起動し直す。

### ビルド時に自動コピーさせる

`CMakeLists.txt` の `COPY_PLUGIN_AFTER_BUILD FALSE` を `TRUE` にすると、
ビルドのたびにホストの標準ディレクトリへ自動でコピーされる（開発中は便利）。

## 4. 信号処理

### MyFirstMasteringEQ

ピーキング EQ（biquad）を自前で実装している。

| 項目 | 内容 |
|------|------|
| 係数計算 | `juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter()`（`std::array` 返し＝ヒープ確保なし） |
| フィルタ構造 | Transposed Direct Form II（`Source/PluginProcessor.h` の `Biquad`） |
| チャンネル | 左右で独立した状態を持つ（共有しない） |
| パラメータ取得 | `apvts.getRawParameterValue()` が返す `std::atomic<float>*` を毎ブロック `load()` |
| スムージング | `juce::SmoothedValue`（20 ms）。移動中は 32 サンプル単位で再チューニング（ホストのブロック長に左右されない） |
| レイテンシ | 0 サンプル |
| 0 dB 時 | 係数を厳密な恒等写像に差し替えて素通し（±0.001 dB 未満は無処理） |

`processBlock()` では **メモリ確保・ロック・`std::function` 呼び出しを行わない**。
係数更新は `std::array<float,5>` の代入だけで完結する。

> **0 dB を明示的に素通しにしている理由**: biquad は 0 dB で「数学的には」恒等写像だが、
> FMA（積和融合）が有効な CPU（Apple silicon など）では丸め誤差が残り bit-exact にならない。
> そのため ±0.001 dB 未満では恒等係数に差し替え、全プラットフォームで完全な素通しを保証する。

### MyFirstTapeMachine

| 項目 | 内容 |
|------|------|
| 遅延線 | チャンネルごとに `std::vector<float>`（`prepareToPlay()` で 1 回だけ確保）。線形補間で読み出す |
| ワウ / フラッター | 読み出し位置を LFO で動かす。位相はサンプルごとに進め、1 周期で折り返す |
| サチュレーション | `tanh(a·x + b) - tanh(b)` を a で割った非対称カーブ。2 倍にオーバーサンプリング（`juce::dsp::Oversampling`、IIR ハーフバンド） |
| フィルタ | pre/de-emphasis、ヘッドバンプ、トーン、DC カットはいずれも RBJ の係数式を自前で計算（`TapeElements.h`）。ヒープ確保なし |
| ヒス | xorshift の白色ノイズをチャンネルごとに持つ。値は実効値（RMS）で解釈する |
| スムージング | `juce::SmoothedValue`（20 ms）。トーンの再チューニングは 64 サンプル単位 |
| レイテンシ | 遅延線の平均遅延 + オーバーサンプラーの遅延（48 kHz で約 3.6 ms）を報告 |

`processBlock()` 内で確保するものは無い。オーバーサンプラーも遅延線も `prepareToPlay()` で作る。

## 5. テスト

```bash
ctest --test-dir build --output-on-failure
# または直接
./build/MyFirstMasteringEQTests_artefacts/Release/MyFirstMasteringEQTests
```

ホストもオーディオデバイスも使わずに、実際の `processBlock()` を呼んで検証する。

ホスト互換性の検証（CI でも毎回実行している。詳細は「8. ホスト互換性の自動検証」）:

```bash
pluginval --strictness-level 5 --skip-gui-tests --validate-in-process \
  build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3
clap-validator validate \
  build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap
```

### 処理コストの計測

`ctest` には入れていない手動実行のベンチマークがある（数値はマシン依存なので、
合否ではなく記録として扱う）:

```bash
./build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQBench_artefacts/Release/MyFirstMasteringEQBench
```

1 kHz の正弦波を 10 秒ぶん、ステレオで流したときの実測値
（Linux, x86_64, Release ビルド）。`1 band` と `4 bands` の 2 シナリオを出力する。

| サンプルレート | ブロック | v0.1.0（1 バンド版） | 現行: 1 バンドのみ使用 | 現行: 4 バンドすべて使用 |
|----------------|----------|----------------------|------------------------|--------------------------|
| 44.1 kHz | 64 | 0.155 % | 0.300 % | 0.300 % |
| 44.1 kHz | 512 | 0.160 % | 0.315 % | 0.314 % |
| 48 kHz | 64 | 0.172 % | 0.325 % | 0.324 % |
| 48 kHz | 512 | 0.173 % | 0.345 % | 0.343 % |
| 96 kHz | 512 | 0.352 % | 0.683 % | 0.683 % |
| 192 kHz | 512 | 0.676 % | 1.401 % | 1.364 % |

レイテンシは 0 サンプル（IIR のため）。48 kHz で 1 コアの 0.35 % 未満。

**使っていないバンドもコストは同じ**。0 dB のバンドは恒等フィルタになるが、
サンプルは 4 つの biquad をすべて通るため。バンドごとに分岐して飛ばす最適化は
可能だが、現状の余裕（1 % 未満）では不要と判断した。

**数値は CPU・ビルド環境で変わる**ので、比較するときは同じマシンで測り直すこと。

## 6. CMake オプション

| オプション | 既定値 | 意味 |
|-----------|--------|------|
| `WAIVES_BUILD_CLAP` | `ON` | CLAP 形式をビルドする |
| `WAIVES_BUILD_TESTS` | `ON` | ヘッドレステストをビルドする |
| `WAIVES_BUILD_BENCHMARKS` | `ON` | 手動実行のベンチマークをビルドする（`ctest` には登録しない） |

出力形式は `cmake/WaivesPlugin.cmake` の `waives_add_plugin()` が決める
（Linux: `VST3 Standalone`、macOS では `if(APPLE)` で `AU` が追加される）。
各プラグインは形式を書かなくてよい。

## 7. macOS で AU をビルドする

AU は Apple の AudioUnit SDK と Objective-C++ を使うため **Linux ではビルドできない**
（クロスコンパイルも macOS SDK が必須で現実的でない）。macOS 上でビルドする:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"   # Universal（任意）
cmake --build build --parallel
```

出力: `build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component`

```bash
# Logic から見えるようにする
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component \
      ~/Library/Audio/Plug-Ins/Components/

# Apple 純正のバリデータで確認（aufx = effect, Mfmq = PLUGIN_CODE, Myau = 製造元コード）
auval -v aufx Mfmq Myau
```

### 実機での確認を 1 コマンドで

macOS 上なら、リリースのダウンロードから配置・署名・`auval`・`pluginval` までを
`scripts/verify-macos.sh` がまとめて行う:

```bash
./scripts/verify-macos.sh                 # 最新リリースを使う
./scripts/verify-macos.sh --tarball FILE  # 手元の tar.gz を使う
./scripts/verify-macos.sh --au-only       # AU だけ入れる
./scripts/verify-macos.sh --dry-run       # 何をするかだけ表示する
```

Logic Pro に読み込んでからの確認項目は [docs/logic-pro-checklist.md](docs/logic-pro-checklist.md) にある。

### macOS 実機が無い場合

`.github/workflows/build.yml` が GitHub Actions の macOS ランナーで AU / VST3 / CLAP /
Standalone をビルドし、Artifact としてアップロードする。push 後は
**Actions → build → 最新の実行 → Artifacts → `MyFirstMasteringEQ-macos`** から
ダウンロードできる（手動実行は `workflow_dispatch`）。中身は
`MyFirstMasteringEQ-macos.tar.gz` なので、展開してから配置する
（`upload-artifact` はファイルの実行権限を落とすため、tar で包んでいる）:

```bash
# 展開先はこの後のコマンドが参照するパスに合わせる
mkdir -p build/plugins/MyFirstMasteringEQ
tar -xzf MyFirstMasteringEQ-macos.tar.gz -C build/plugins/MyFirstMasteringEQ
# → build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/ に AU / VST3 / CLAP / Standalone
```

ダウンロードした `.component` は quarantine 属性が付くので、配置前に外す:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component
```

AU の検証は CI でも回している（`auval` と pluginval の AU ロード）。
リリースの macOS バンドルは universal（arm64 + x86_64）で、
`Info.plist` の `AudioComponents` は `aufx Mfmq Myau`。

## 8. Carla / DAW での動作確認

### Carla に読み込む

```bash
sudo apt install carla            # Ubuntu 22.04 系。24.04 では公式サイトの AppImage を使う
carla
```

1. Carla 起動 → **Engine → Engine driver** を `JACK` か `ALSA` にする
2. 左の **Add Plugin → VST3** から
   `build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3` を選ぶ
3. プラグインの GUI が開くので、ノブを回すと周波数特性グラフが追従することを確認
4. Carla の **Patchbay** タブで「音源 → MyFirstMasteringEQ → 出力」と配線する

CLAP 版は `build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap`。
Carla が CLAP 非対応の場合は VST3 を使う。

### 効果を耳で確認する

- **ホワイトノイズ**を流し、Frequency を 100 Hz → 1 kHz → 10 kHz と動かすと、
  持ち上がっている帯域の「シャー」が移動するのが分かる
- **サイン波**（1 kHz）を流し、Gain を +12 dB にすると音量が約 4 倍になる
- Gain を 0 dB に戻すと音が元どおりになる（= 0 dB は素通し）

### ホスト互換性の自動検証

CI（`.github/workflows/_build-plugin.yml`）が毎回実行し、失敗するとジョブが赤くなる:

| 対象 | ツール | 実行環境 | 結果 |
|------|--------|----------|------|
| VST3 | `pluginval --strictness-level 5 --skip-gui-tests` | Linux / macOS | SUCCESS |
| CLAP | `clap-validator` | Linux / macOS | 32 passed / 0 failed / 1 warning（仕様上想定内） |
| AU | `auval -v aufx Mfmq Myau` | macOS | PASS |

ツールは固定バージョン（pluginval 1.0.4 / clap-validator 0.4.1）を GitHub Releases から
取得してキャッシュしている。手元で同じ検証をする場合:

```bash
# pluginval（VST3）
pluginval --strictness-level 5 --skip-gui-tests --validate-in-process \
  build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3

# clap-validator（CLAP）
clap-validator validate \
  build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap
```

どちらも失敗すると非 0 で終了するので、スクリプトから呼んでも判定できる。

### よくあるトラブル

| 症状 | 原因 | 対処 |
|------|------|------|
| Carla の一覧にプラグインが出ない | VST3 の探索パスに入っていない | `~/.vst3/` にシンボリックリンクを張る: `ln -s "$PWD/build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3" ~/.vst3/` |
| 読み込めるが音が出ない | ホスト側の配線（Patchbay）が未接続 | 音源 → プラグイン → 出力を配線する |
| ノブを回すとノイズ | （この実装では起きない想定）スムージングが効いていない | `prepareToPlay` で `SmoothedValue::reset()` を呼んでいるか確認 |
| 音が割れる | 上げすぎ | Gain は ±18 dB まで。マスタリング用途では ±3 dB 程度から始める |
| プラグインが落ちる | ホストとプラグインのバージョン不一致 | `pluginval` で再現するか切り分ける |
| グラフと音が違う | サンプルレート取得の失敗 | グラフは `getCurrentSampleRate()` を使う。0 のときは 48 kHz で描画 |

### エディタの操作

| 操作 | 動作 |
|------|------|
| ノブをドラッグ | 値を変更（Shift で微調整） |
| ノブを**ダブルクリック** | そのパラメータの既定値に戻る |
| Tab / Shift+Tab | ノブ・プリセット・Bypass の間でフォーカス移動 |
| 矢印キー | フォーカス中のノブを 1 ステップ動かす |
| ホバー | パラメータの説明がツールチップで出る |
| SOLO ボタン | 選択中のバンドだけを聴く（保存されない） |
| BAND OFF ボタン | 選択中のバンドだけを素通しにする |
| グラフのノードをドラッグ | そのバンドの Frequency（横）と Gain（縦）を変える |
| グラフのノードをクリック | そのバンドを選択する（ノブが追従する） |
| ノードの上でホイール | そのバンドの Q を変える（上で狭く、下で広く） |
| BYPASS | 10 ms のクロスフェードで素通しに切り替える（A/B 用） |

## 9. メモ

- ヘッドレス環境でスタンドアローンを起動確認する場合: `xvfb-run -a <binary>`
- CLAP の `clap-validator` はパラメータが 0 個のプラグインで `param-conversions`
  テストがバリデータ側のゼロ除算でクラッシュする（Step 3 でパラメータを追加すると解消）。

## 10. プラグインを追加する

既存のプラグインをコピーするのが早い。以下は `MyFirstMasteringEQ` を雛形にする例。

### 1. ディレクトリを作る

```bash
cp -r plugins/MyFirstMasteringEQ plugins/MySecondPlugin
rm -rf plugins/MySecondPlugin/build
```

### 2. ソースを書き換える

- `Source/PluginProcessor.h/.cpp` のクラス名（`MyFirstMasteringEQAudioProcessor`）
- `Source/PluginEditor.h/.cpp`
- `Source/PluginProcessor.cpp` の `createPluginFilter()` が返す型
- パラメータ ID は **プラグインごとに固有**にする（`ParamIDs` 名前空間）

### 3. `plugins/MySecondPlugin/CMakeLists.txt` を直す

```cmake
waives_add_plugin(MySecondPlugin
    COMPANY_NAME      "MyAudioLab"
    BUNDLE_ID         "com.myaudiolab.mysecondplugin"
    MANUFACTURER_CODE Myau        # 製造元コードは共通でよい
    PLUGIN_CODE       Msnd        # ← 必ず他と違う 4 文字にする
    DESCRIPTION       "..."
    CLAP_ID           "com.myaudiolab.mysecondplugin"
    CLAP_FEATURES     audio-effect
    SOURCES           ${MYEQ_SOURCES}
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)

waives_add_plugin_tests(MySecondPluginTests
    TEST_SOURCE  tests/ProcessorTest.cpp
    SOURCES      ${MYEQ_SOURCES}
    INCLUDE_DIRS Source
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)
```

`PLUGIN_CODE` が重複していると configure 時にエラーで止まる（AU の識別子なので重複不可）。

### 4. ルートの `CMakeLists.txt` に登録する

```cmake
set(WAIVES_PLUGINS
    MyFirstMasteringEQ
    MySecondPlugin)      # ← 追加
```

### 5. CI のマトリクスに追加する

`.github/workflows/build.yml` の `matrix.plugin` に `MySecondPlugin` を足すと、
Linux / macOS の両方でビルド・テスト・`auval` が走る。

### 6. テストを書く

`tests/TestUtilities.h` の `check()` / `buffersMatch()` / `fillWithSine()` を再利用できる。
`waives_add_plugin_tests()` が `ctest` に自動登録するので、`ctest --test-dir build` で
全プラグインのテストがまとめて走る。

### 7. ビルドと確認

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./scripts/install-linux.sh
```

## 11. リリースの運用

main に push するたびに、`release` ワークフローがビルド・テスト・検証を通したうえで
GitHub Release を作る。

- タグは `v<VERSION>-<短い SHA>`（例: `v0.1.0-d1a7913`）。`VERSION` はリポジトリ直下の
  `VERSION` ファイルが唯一の出所で、`CMakeLists.txt` と `release.yml` の両方が読む
- 同じコミットで再実行した場合は、新しいリリースを作らず既存のタグに成果物を上書きする
  （`--clobber`）
- 成果物は Linux / macOS の tarball と `SHA256SUMS`。検証手順は「すぐ使う」を参照

### 同じバージョンで複数のリリースが並ぶ件

タグにコミットの SHA が入るので、`VERSION` を上げないまま main が進むと
`v0.1.0-5815984` と `v0.1.0-d1a7913` のように**同じバージョンのリリースが複数**できる。
これは意図した挙動で、次の理由から**古いものも残す**方針にしている。

- 「どのコミットのビルドか」がタグだけで分かるので、不具合の切り分けに使える
- 過去のビルドを再現・再配布できる（AGPL の「対応するソース」の観点でも素直）
- 最新のリリースが GitHub の `latest` になるため、利用者は最新を選べばよい

一覧を整理したくなった場合は、古いものを手動で消す（**不可逆**なので注意）:

```bash
gh release list
gh release delete v0.1.0-5815984 --cleanup-tag   # タグも消す
```

「同じ VERSION のリリースは 1 つだけにする」運用に変える場合は、
`release.yml` の publish ジョブに「同じ VERSION の古いリリースを削除する」ステップを足す。

### バージョンを上げる

1. `VERSION` を書き換える（例: `0.1.0` → `0.2.0`）
2. 同じ PR で README の該当箇所を更新する
3. マージすると `v0.2.0-<sha>` が作られる

## ライセンス

本リポジトリは **AGPL-3.0**（[LICENSE](LICENSE)）。第三者コンポーネントの表示は
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) にまとめてある。

### なぜ AGPL-3.0 なのか

- JUCE 9 は **AGPLv3 と商用 EULA のデュアルライセンス**（[JUCE Licensing FAQ](https://juce.com/get-juce/#licensing-faq)）。
  AGPL を選べば追加費用なしで利用できる。
- 商用 EULA 側は Starter が年間売上 $20,000 まで無料、Indie が $40/月（$300,000 まで）、
  Pro が $175/月（[JUCE 9 EULA 1.2](https://juce.com/legal/juce-9-licence/)）。

### 使う側の義務

- 改変していないバイナリを配布するときは、対応するソースを提供する（本リポジトリが該当）
- 改変版（バイナリを含む）を配布するときは、改変を含む対応するソースを提供し、
  同じ AGPL-3.0 で公開する

プラグインを DAW で使うだけなら、これらの義務は発生しない。

### クローズドソースで配布したい場合

- 著作権者（リポジトリ所有者）は自分のコードを別ライセンスでも提供できる。
  AGPL はあくまで第三者の利用条件を定めるもの
- JUCE 側は AGPL ではなく商用ライセンス（Starter 以上）に切り替える必要がある
- JUCE 9 EULA 2.3 は「フレームワークを、ソース開示を要求するオープンソースライセンスの
  対象にしないこと」を求めている。したがって **AGPL と商用 EULA は併用できない**
  （どちらか一方を選ぶ）

ライセンスを変更する場合は `LICENSE` を差し替え、この節と
`THIRD-PARTY-NOTICES.md` を更新すること。
