# MyFirstMasteringEQ

JUCE 製のマスタリング用 EQ プラグイン。Linux では VST3 / CLAP / スタンドアローンをビルドし、
最終的に macOS で AU をビルドして Logic Pro で使えるようにする。

**現在の状態: Step 5 完了** — 1 バンドのピーキング EQ として動作。
3 パラメータ（Frequency / Gain / Q）、ノブ UI、周波数特性グラフを実装済み。
残りは macOS での AU ビルドと Logic での確認（Step 6）。

## パラメータ

| 表示名 | ID | 範囲 | デフォルト | スケール | 表示例 |
|--------|----|------|-----------|----------|--------|
| Frequency | `freq` | 20 Hz – 20 kHz | 1 kHz | 対数（1 kHz が中央） | `1.00 kHz` / `440.0 Hz` |
| Gain | `gain` | -18 – +18 dB | 0 dB | 線形（0.01 dB 刻み） | `6.00 dB` |
| Q | `q` | 0.1 – 10 | 1.0 | 対数（1.0 が中央） | `4.00` |

| 項目 | 内容 |
|------|------|
| JUCE | 9.0.6（CMake の `FetchContent` で自動取得） |
| CLAP | [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)（JUCE 本体には CLAP 出力がないため） |
| C++ | C++17 |
| ビルド | CMake + Ninja |

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

| 形式 | パス |
|------|------|
| VST3 | `build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3` |
| CLAP | `build/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap` |
| Standalone | `build/MyFirstMasteringEQ_artefacts/Release/Standalone/MyFirstMasteringEQ` |

### ホストから見えるようにする

```bash
mkdir -p ~/.vst3 ~/.clap
cp -r build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 ~/.vst3/
cp    build/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap  ~/.clap/
```

## 3. インストール（プラグインを DAW から使えるようにする）

ビルドしただけでは DAW の一覧に出ない。ホストが探すディレクトリに置く必要がある。

### Linux（ユーザー単位・sudo 不要）

```bash
./scripts/install-linux.sh
```

これで以下にコピーされる:

| 形式 | 置き場所 |
|------|---------|
| VST3 | `~/.vst3/MyFirstMasteringEQ.vst3` |
| CLAP | `~/.clap/MyFirstMasteringEQ.clap` |
| Standalone | `~/.local/bin/MyFirstMasteringEQ` |

システム全体に入れたい場合（要 sudo）:

```bash
sudo cp -r build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 /usr/local/lib/vst3/
sudo cp    build/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap /usr/local/lib/clap/
```

ホスト側で再スキャンする（Carla なら再起動、Reaper なら「プラグインを再スキャン」）。

### macOS

```bash
# AU（Logic 用）
cp -R build/MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component \
      ~/Library/Audio/Plug-Ins/Components/
# VST3 / CLAP
cp -R build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
cp -R build/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap \
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

1 バンドのピーキング EQ（biquad）を自前で実装している。

| 項目 | 内容 |
|------|------|
| 係数計算 | `juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter()`（`std::array` 返し＝ヒープ確保なし） |
| フィルタ構造 | Transposed Direct Form II（`Source/PluginProcessor.h` の `Biquad`） |
| チャンネル | 左右で独立した状態を持つ（共有しない） |
| パラメータ取得 | `apvts.getRawParameterValue()` が返す `std::atomic<float>*` を毎ブロック `load()` |
| スムージング | `juce::SmoothedValue`（20 ms）。移動中だけブロック単位で再チューニング |
| レイテンシ | 0 サンプル |
| 0 dB 時 | 係数を厳密な恒等写像に差し替えて素通し（±0.001 dB 未満は無処理） |

`processBlock()` では **メモリ確保・ロック・`std::function` 呼び出しを行わない**。
係数更新は `std::array<float,5>` の代入だけで完結する。

> **0 dB を明示的に素通しにしている理由**: biquad は 0 dB で「数学的には」恒等写像だが、
> FMA（積和融合）が有効な CPU（Apple silicon など）では丸め誤差が残り bit-exact にならない。
> そのため ±0.001 dB 未満では恒等係数に差し替え、全プラットフォームで完全な素通しを保証する。

## 5. テスト

```bash
ctest --test-dir build --output-on-failure
# または直接
./build/MyFirstMasteringEQTests_artefacts/Release/MyFirstMasteringEQTests
```

ホストもオーディオデバイスも使わずに、実際の `processBlock()` を呼んで検証する。

ホスト互換性の検証（任意）:

```bash
pluginval --strictness-level 5 --validate build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3
clap-validator validate build/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap
```

## 6. CMake オプション

| オプション | 既定値 | 意味 |
|-----------|--------|------|
| `MYEQ_BUILD_CLAP` | `ON` | CLAP 形式をビルドする |
| `MYEQ_BUILD_TESTS` | `ON` | ヘッドレステストをビルドする |

出力形式は `CMakeLists.txt` の `MYEQ_PLUGIN_FORMATS` 変数で決まる
（Linux: `VST3 Standalone`、macOS では `AU` が追加される）。

macOS では同じ `CMakeLists.txt` が AU も出力する（`if(APPLE)` で追加）。

## 7. macOS で AU をビルドする

AU は Apple の AudioUnit SDK と Objective-C++ を使うため **Linux ではビルドできない**
（クロスコンパイルも macOS SDK が必須で現実的でない）。macOS 上でビルドする:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"   # Universal（任意）
cmake --build build --parallel
```

出力: `build/MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component`

```bash
# Logic から見えるようにする
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R build/MyFirstMasteringEQ_artefacts/Release/AU/MyFirstMasteringEQ.component \
      ~/Library/Audio/Plug-Ins/Components/

# Apple 純正のバリデータで確認（aufx = effect, Mfmq = PLUGIN_CODE, Myau = 製造元コード）
auval -v aufx Mfmq Myau
```

### macOS 実機が無い場合

`.github/workflows/build.yml` が GitHub Actions の macOS ランナーで AU / VST3 / CLAP /
Standalone をビルドし、Artifact としてアップロードする。push 後は
**Actions → build → 最新の実行 → Artifacts → `MyFirstMasteringEQ-macos`** から
ダウンロードできる（手動実行は `workflow_dispatch`）。

ダウンロードした `.component` は quarantine 属性が付くので、配置前に外す:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component
```

## 8. Carla / DAW での動作確認

### Carla に読み込む

```bash
sudo apt install carla            # Ubuntu 22.04 系。24.04 では公式サイトの AppImage を使う
carla
```

1. Carla 起動 → **Engine → Engine driver** を `JACK` か `ALSA` にする
2. 左の **Add Plugin → VST3** から
   `build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3` を選ぶ
3. プラグインの GUI が開くので、ノブを回すと周波数特性グラフが追従することを確認
4. Carla の **Patchbay** タブで「音源 → MyFirstMasteringEQ → 出力」と配線する

CLAP 版は `build/MyFirstMasteringEQ_artefacts/Release/CLAP/MyFirstMasteringEQ.clap`。
Carla が CLAP 非対応の場合は VST3 を使う。

### 効果を耳で確認する

- **ホワイトノイズ**を流し、Frequency を 100 Hz → 1 kHz → 10 kHz と動かすと、
  持ち上がっている帯域の「シャー」が移動するのが分かる
- **サイン波**（1 kHz）を流し、Gain を +12 dB にすると音量が約 4 倍になる
- Gain を 0 dB に戻すと音が元どおりになる（= 0 dB は素通し）

### ホスト互換性の自動検証

このリポジトリでは以下の 2 つで検証済み（Linux, JUCE 9.0.6）:

| ツール | 結果 |
|--------|------|
| `pluginval --strictness-level 5`（VST3） | SUCCESS（エディタ・オートメーション含む） |
| `clap-validator`（CLAP） | 32 passed / 0 failed / 1 warning（仕様上想定内） |

### よくあるトラブル

| 症状 | 原因 | 対処 |
|------|------|------|
| Carla の一覧にプラグインが出ない | VST3 の探索パスに入っていない | `~/.vst3/` にシンボリックリンクを張る: `ln -s "$PWD/build/MyFirstMasteringEQ_artefacts/Release/VST3/MyFirstMasteringEQ.vst3" ~/.vst3/` |
| 読み込めるが音が出ない | ホスト側の配線（Patchbay）が未接続 | 音源 → プラグイン → 出力を配線する |
| ノブを回すとノイズ | （この実装では起きない想定）スムージングが効いていない | `prepareToPlay` で `SmoothedValue::reset()` を呼んでいるか確認 |
| 音が割れる | 上げすぎ | Gain は ±18 dB まで。マスタリング用途では ±3 dB 程度から始める |
| プラグインが落ちる | ホストとプラグインのバージョン不一致 | `pluginval` で再現するか切り分ける |
| グラフと音が違う | サンプルレート取得の失敗 | グラフは `getCurrentSampleRate()` を使う。0 のときは 48 kHz で描画 |

## 9. メモ

- ヘッドレス環境でスタンドアローンを起動確認する場合: `xvfb-run -a <binary>`
- CLAP の `clap-validator` はパラメータが 0 個のプラグインで `param-conversions`
  テストがバリデータ側のゼロ除算でクラッシュする（Step 3 でパラメータを追加すると解消）。
