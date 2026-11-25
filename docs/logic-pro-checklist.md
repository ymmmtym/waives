# Logic Pro での確認手順（#19）

AU を **実際の Logic Pro に読み込んで**確認するためのチェックリスト。
自動でできる部分は `./scripts/verify-macos.sh` が担当するので、
ここに残るのは「人が耳と目で確かめる」項目だけ。

結果は [issue #19](https://github.com/ymmmtym/waives/issues/19) に貼ってください。
最後にテンプレートがあります。

## 0. 前提

- macOS 12 以降、Logic Pro 10.7 以降
- リポジトリを clone 済み（スクリプトを実行するため）

```bash
git clone https://github.com/ymmmtym/waives.git
cd waives

# 最新リリースを落としてきて、署名・配置・auval・pluginval まで一気にやる
./scripts/verify-macos.sh
```

`auval passed` と出れば、Logic のプラグインスキャンを通る状態になっている。
`auval failed` の場合は、その出力を issue に貼れば原因を追える。

> 手元でビルドしたものを使う場合は `--tarball` で渡す:
> `./scripts/verify-macos.sh --tarball build/plugins/MyFirstMasteringEQ/MyFirstMasteringEQ-macos.tar.gz`

## 1. 環境を記録する

| 項目 | 値 |
|------|-----|
| macOS のバージョン | （例: 14.6.1） |
| Mac の機種 / チップ | （例: MacBook Pro M1 Pro） |
| Logic Pro のバージョン | （例: 11.0.1） |
| プロジェクトのサンプルレート | （例: 48 kHz） |
| I/O バッファサイズ | （例: 128） |

## 2. プラグインが見つかる

1. Logic Pro を起動 → 新規プロジェクト（空のプロジェクト）
2. ステレオのオーディオトラックを 1 本作り、**Stereo Out** を選ぶ
3. Stereo Out の **Audio FX** スロットをクリック
4. **Audio Units → MyAudioLab → MyFirstMasteringEQ** が出る

- [ ] 一覧に出てくる
- [ ] 選ぶとエディタが開く（4 バンドのノブ・グラフ・SOLO / BAND OFF が見える）

出てこない場合は、Logic を終了して
`killall -9 AudioComponentRegistrar` → Logic を再起動。
それでも出ないときは Logic の **環境設定 → Plug-in Manager** で
MyFirstMasteringEQ を選び **Reset & Rescan Selection**。

## 3. 音が変わる

1. テスト音源（ループでも可）を再生する
2. バンド 1 の **GAIN** を +12 dB まで上げる
3. **FREQ** を 1 kHz あたりにする

- [ ] 明らかに音量・音色が変わる
- [ ] GAIN を 0 dB に戻すと元の音に戻る（bit-exact な素通し）
- [ ] ノブを動かしている最中にノイズ（ジッパーノイズ）が出ない

## 4. グラフとノブが連動する

- [ ] ノブを動かすとグラフのカーブが追従する
- [ ] グラフのノードをドラッグすると、ノブと音が追従する
- [ ] バンド 2〜4 のボタンで選択を切り替えると、ノブがそのバンドの値になる
- [ ] バンドごとに色が違う（1 紫 / 2 緑青 / 3 橙 / 4 桃）

## 5. ソロとバンド単位バイパス

- [ ] **SOLO** を押すと、選択中バンド以外の帯域が消える（10 ms で滑らかに）
- [ ] SOLO を解除すると元の音に戻る
- [ ] バンドを切り替えるとソロもそのバンドに移る
- [ ] **BAND OFF** を押すと、そのバンドの効果だけが消える（他のバンドは効いたまま）
- [ ] どの切り替えでもクリックノイズが出ない

## 6. 全体バイパス

- [ ] エディタ右上の **BYPASS** で素の音に戻る（切り替えでクリックしない）
- [ ] Logic 側のプラグインバイパス（スロットの電源ボタン）とも独立して動く

## 7. オートメーション

1. Stereo Out の MyFirstMasteringEQ を開く
2. バンド 2 を選び、**GAIN** を右クリック → **オートメーションを表示**
3. トラックのオートメーションを有効にして、ゲインを描く

- [ ] パラメータ一覧に `Frequency` / `Gain` / `Q` / `Band Bypass` がバンドごとに出る（41 個）
- [ ] 描いたオートメーションどおりに音が動く
- [ ] 再生中にノブが動く（ホスト側の値が UI に反映される）

## 8. セッションの往復

1. 4 バンドに適当な値を入れ、**SOLO をオンにしたまま**保存する
2. Logic を終了して開き直す

- [ ] 全バンドの Frequency / Gain / Q / Band Bypass が復元される
- [ ] プリセット名（左上のドロップダウン）が復元される
- [ ] **ソロは解除されている**（保存対象ではないため）

## 9. サンプルレートとバッファ

Logic の **環境設定 → Audio → Devices** でサンプルレートを変えながら確認する。

| サンプルレート | 音が破綻しない | グラフのノード位置が正しい |
|----------------|----------------|----------------------------|
| 44.1 kHz | [ ] | [ ] |
| 48 kHz | [ ] | [ ] |
| 96 kHz | [ ] | [ ] |
| 192 kHz | [ ] | [ ] |

- [ ] I/O バッファ 32 / 64 / 128 で音切れしない
- [ ] レイテンシ補償が 0 サンプルのまま（IIR なので遅延しない）

## 10. 負荷

- [ ] Stereo Out に 1 インスタンス挿して、CPU メーターがほとんど動かない
- [ ] 4 バンドすべて +12 dB にしても音切れしない

## 11. 後片付け

```bash
rm -rf ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component
rm -rf ~/Library/Audio/Plug-Ins/VST3/MyFirstMasteringEQ.vst3
rm -rf ~/Library/Audio/Plug-Ins/CLAP/MyFirstMasteringEQ.clap
rm -rf ~/Applications/MyFirstMasteringEQ.app /Applications/MyFirstMasteringEQ.app
killall -9 AudioComponentRegistrar
```

## 報告テンプレート（#19 に貼る）

```markdown
### 結果

- auval: pass / fail
- pluginval (AU): pass / fail / 未実行
- Logic での読み込み: OK / NG

### 環境

- macOS:
- Mac:
- Logic Pro:
- サンプルレート / バッファ:

### チェックリスト

- [ ] 2. プラグインが見つかる
- [ ] 3. 音が変わる
- [ ] 4. グラフとノブが連動する
- [ ] 5. ソロとバンド単位バイパス
- [ ] 6. 全体バイパス
- [ ] 7. オートメーション
- [ ] 8. セッションの往復
- [ ] 9. サンプルレートとバッファ
- [ ] 10. 負荷

### 気づいたこと

（不具合があれば、再現手順・期待した動作・実際の動作・スクリーンショット）
```
