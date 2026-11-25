# AGENTS.md — JUCE Mastering EQ Plugin

> このファイルはAIコーディングエージェント（Cursor、Windsurf、GitHub Copilot Chat等）向けのプロジェクトルールと開発計画です。
> 人間の開発者がこのファイルを読んでも構いませんが、主な読者はAIエージェントです。

---

## 1. プロジェクト概要

- **プロジェクト名**: MyFirstMasteringEQ
- **目標**: Logic Pro（macOS）で使えるAU形式のマスタリング用EQプラグインをJUCEで開発する
- **最終目標**: Waves Q10のようなマルチバンド・ピーキングEQ
- **最初のマイルストーン**: 1バンドのピーキングEQがLogic Pro上で動作すること
- **開発者の経験**: JUCEおよびプラグイン開発は未経験

---

## 2. 技術スタック

| 項目 | 内容 |
|------|------|
| フレームワーク | JUCE（最新安定版） |
| 言語 | C++17以降 |
| macOSビルド | CMake（Xcode ジェネレータ可） |
| Linuxビルド | CMake + Ninja |
| プラグイン形式 | AU（必須）+ VST3 + CLAP + スタンドアーン |
| テスト用ホスト（macOS） | Logic Pro |
| テスト用ホスト（Linux） | Carla / JUCEスタンドアローン |

---

## 3. エージェントへの行動ルール

### 3.1 段階的な開発
- **1ステップずつ進める**。前のステップが完了して次に進むことを確認してから、次のステップを提示すること。
- すべてのコードを一気に生成しない。現在のステップに必要なファイル・コードのみを提示する。
- 各ステップの終了時に「次のステップに進むための準備」として必要な操作を明示すること。

### 3.2 コード品質
- **リアルタイムスレッド（processBlock内）でメモリ確保（new/delete/malloc）を行わない**。
- **パラメータ変更時にクリックノイズが出ないよう、スムージングを考慮する**。
- コードは**完全な形で提示する**。部分的なスニペットだけで終わらせない。
- ファイル構成（ヘッダー/ソースの分離）を明示する。
- JUCEの標準的なコーディング規約に従う（インデント4スペース、クラス名はUpperCamelCase等）。

### 3.3 設定・手順の明示
- ProjucerやXcodeの設定変更が必要な場合は、**具体的な設定項目と値**を示す。
- ビルド手順、確認手順、よくあるエラーと対処法を各ステップに含める。

### 3.4 プラットフォーム対応
- コードは**プラットフォーム非依存**に保つ。OS固有の処理はJUCEの抽象化APIを使う。
- AUはmacOS専用だが、コード自体はLinux/macOS共通で動くように書く。
- UIはJUCEの標準コンポーネントを使い、OS依存のネイティブUIを使わない。

---

## 4. 段階的な開発計画

### Step 1: JUCEプロジェクトの初期化
- Projucerでの新規プロジェクト作成手順（テンプレート、設定項目）
- XcodeエクスポーターおよびLinux（CMake）エクスポーターの設定
- AU形式の有効化方法
- 初回ビルドと `.component` / `.vst3` ファイルの出力確認
- **完了条件**: バイパス（入力をそのまま出力）するプラグインがLogic/Carlaで認識される

### Step 2: AudioProcessorの基本実装
- `processBlock()` の構造説明
- 入力音声をそのまま通すバイパス処理の実装
- チャンネル数・サンプリングレートの取得
- **完了条件**: プラグインを挿入しても音が変わらない（バイパス動作）

### Step 3: パラメータの定義とUI
- `AudioProcessorValueTreeState` を使った3パラメータの定義:
  - `frequency`: 20.0Hz ~ 20,000.0Hz（対数スケール）
  - `gain`: -18.0dB ~ +18.0dB
  - `q`: 0.1 ~ 10.0
- スライダーとパラメータの紐付け（`SliderAttachment`）
- UIレイアウト（FlexBoxまたは絶対配置）
- **完了条件**: UIのスライダーを動かすとパラメータ値が変更される

### Step 4: ピーキングEQのDSP実装
- `juce::dsp::IIR::Filter` の使い方
- `makePeakFilter()` での係数計算
- リアルタイムでの係数更新（スレッドセーフに、スムージング付き）
- 左右チャンネルへの適用
- **完了条件**: 特定の周波数帯域の音量が増減される

### Step 5: 周波数特性グラフの表示
- 簡易的な周波数応答曲線の描画（`paint()` での描画）
- パラメータ変更時の再描画
- **完了条件**: UI上にEQカーブがリアルタイムで表示される

### Step 6: Logicでの動作確認とデバッグ
- Logic Proへの読み込み手順
- 実際の音源でEQの効果を確認する方法
- よくあるトラブル（プラグインが認識されない、音が出ない等）の対処
- **完了条件**: Logic Pro上でマスタリング用途の音源に対してEQが効果的に機能する

---

## 5. パラメータ仕様

| パラメータ名 | ID | 範囲 | デフォルト | スケール | UIラベル |
|-------------|-----|------|----------|---------|---------|
| Frequency | `freq` | 20.0 ~ 20000.0 | 1000.0 | 対数 | Hz |
| Gain | `gain` | -18.0 ~ +18.0 | 0.0 | 線形 | dB |
| Q | `q` | 0.1 ~ 10.0 | 1.0 | 対数 | Q |

---

## 6. ディレクトリ構成（モノレポ）

複数のプラグインを 1 リポジトリで開発する。**1 プラグイン = 1 ディレクトリ**。

```
waives/
├── CMakeLists.txt                   # WAIVES_PLUGINS に登録されたプラグインを add_subdirectory
├── cmake/
│   ├── Dependencies.cmake           # JUCE / clap-juce-extensions（共通）
│   └── WaivesPlugin.cmake           # waives_add_plugin() / waives_add_plugin_tests()
├── plugins/
│   └── MyFirstMasteringEQ/
│       ├── CMakeLists.txt           # そのプラグイン固有の設定だけ
│       ├── Source/
│       │   ├── PluginProcessor.h/.cpp
│       │   ├── PluginEditor.h/.cpp
│       │   └── FrequencyResponseCurve.h/.cpp
│       └── tests/ProcessorTest.cpp  # ヘッドレステスト（ctest に自動登録）
├── tests/TestUtilities.h            # 全プラグイン共通のテストヘルパー
├── scripts/install-linux.sh         # ビルド済みプラグインを一括インストール
├── .github/workflows/build.yml      # plugin × OS のマトリクス
├── AGENTS.md
└── README.md
```

### プラグインを追加するときのルール

1. `plugins/<Name>/` を作り、`CMakeLists.txt` で `waives_add_plugin()` を呼ぶ
2. **`PLUGIN_CODE` は 4 文字で全体で一意**（重複すると configure がエラーで止まる）。
   `auval -v aufx <PLUGIN_CODE> <MANUFACTURER_CODE>` で使われる
3. `BUNDLE_ID` / `CLAP_ID` も一意にする
4. パラメータ ID はプラグイン内で一意（プラグイン間で衝突しても実害はないが、揃えておく）
5. ルート `CMakeLists.txt` の `WAIVES_PLUGINS` と CI の `matrix.plugin` に追加する
6. 出力形式（VST3 / CLAP / Standalone / AU）は共通ヘルパーが決めるので書かない

### プラグイン間で共有したいコード

DSP や UI 部品を複数プラグインで共有するときは `shared/` を作り、
そこに `waives_shared` のような INTERFACE / STATIC ライブラリを定義して
各プラグインの `LINK_LIBRARIES` から参照する（現時点では未作成）。

---

## 7. よくあるエラーと対処法（事前共有）

| 症状 | 原因 | 対処 |
|------|------|------|
| Logicでプラグインが認識されない | AUスキャンが走っていない、またはバリデーション失敗 | Logicのプラグインマネージャーで再スキャン、または `auvaltool` で検証 |
| 音が出ない | 処理関数が空、またはチャンネル指定ミス | `processBlock` でバッファに書き込んでいるか確認 |
| ノブを回すとノイズが出る | 係数更新にスムージングがない | 係数更新を別スレッドで行うか、線形補間を入れる |
| Linuxでビルドエラー | JUCEの依存ライブラリが不足 | `apt install libasound2-dev libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libxdamage-dev libgl1-mesa-dev` |
| macOSで署名エラー | 開発者ID未設定 | XcodeのSigning設定を「Sign to Run Locally」に変更 |

---

## 8. 将来の拡張（参考）

- 4バンド / 8バンドのマルチバンドEQ化
- オーバーサンプリング（2x, 4x, 8x）の導入
- スペクトラムアナライザーの表示
- プリセット管理システム
- 非線形サチュレーション（アナログの色付け）
- バイパス・A/B比較機能

---

*最終更新: 2026-11-24*

