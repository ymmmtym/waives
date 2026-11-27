# Logic Pro での確認手順（#19）

AU を **実際の Logic Pro に読み込んで**確認するためのチェックリスト。
自動でできる部分は `./scripts/verify-macos.sh` が担当するので、
ここに残るのは「人が耳と目で確かめる」項目だけ。

結果は [issue #19](https://github.com/ymmmtym/waives/issues/19) に貼ってください。
最後にテンプレートがあります。

## 0. 前提

- macOS 12 以降、Logic Pro 10.7 以降
- リポジトリを clone 済み（スクリプトを実行するため）

先に、同じプラグインが既に入っていないかを確認してバックアップしておく
（スクリプトは同じ名前のバンドルを上書きするため）:

```bash
for p in ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component \
         ~/Library/Audio/Plug-Ins/VST3/MyFirstMasteringEQ.vst3 \
         ~/Library/Audio/Plug-Ins/CLAP/MyFirstMasteringEQ.clap \
         ~/Applications/MyFirstMasteringEQ.app /Applications/MyFirstMasteringEQ.app; do
    if [ -e "$p" ]; then
        # 途中で失敗した .bak を残さない。失敗したら検証そのものを中止する
        rm -rf "$p.bak" || { echo "cannot clear $p.bak" >&2; exit 1; }
        if ! cp -R "$p" "$p.bak"; then
            rm -rf "$p.bak" || echo "warning: delete $p.bak by hand" >&2
            echo "backup failed: $p" >&2
            exit 1
        fi
    fi
done
```

バックアップに失敗した状態で先に進むと、後片付けで元のバンドルを消して
中途半端なコピーを戻すことになる。`cp` が失敗したら、書きかけの `.bak` を消して
ここで止める。

```bash
git clone https://github.com/ymmmtym/waives.git
cd waives

# 最新リリースを落としてきて、署名・配置・auval・pluginval まで一気にやる
./scripts/verify-macos.sh
```

`auval passed` と出れば、Logic のプラグインスキャンを通る状態になっている。
`auval failed` の場合は、その出力を issue に貼れば原因を追える。

> 手元でビルドしたものを使う場合は、まず tar にまとめてから `--tarball` で渡す:
>
> ```bash
> cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
>       -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
> cmake --build build --parallel
> tar -czf MyFirstMasteringEQ-macos.tar.gz \
>     -C build/plugins/MyFirstMasteringEQ MyFirstMasteringEQ_artefacts
> ./scripts/verify-macos.sh --tarball MyFirstMasteringEQ-macos.tar.gz
> ```
>
> リリースの tar.gz と同じ中身（`MyFirstMasteringEQ_artefacts/Release/` 以下）になる。

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
- [ ] 選ぶとエディタが開く（8 バンドのノブ・グラフ・SOLO / BAND OFF が見える）

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

- [ ] 2 つ以上のバンドにブースト／カットを入れてから **SOLO** を押すと、
      選択中バンドの効果だけが残る（他バンドのブースト／カットが外れる。音は消えない）
- [ ] SOLO を解除すると元の音に戻る（切り替えは 10 ms で滑らかに）
- [ ] バンドを切り替えるとソロもそのバンドに移る
- [ ] **BAND OFF** を押すと、そのバンドの効果だけが消える（他のバンドは効いたまま）
- [ ] どの切り替えでもクリックノイズが出ない

## 6. 全体バイパス

- [ ] エディタ右上の **BYPASS** で素の音に戻る（切り替えでクリックしない）
- [ ] Logic 側のプラグインバイパス（スロットの電源ボタン）とも独立して動く

## 7. オートメーション

1. 音源を再生している**オーディオトラック**の Audio FX にも MyFirstMasteringEQ を挿す
2. バンド 2 を選び、**GAIN** を右クリック → **オートメーションを表示**
3. そのトラックのオートメーションを有効にして、ゲインを描く

Stereo Out で試す場合は、先に **Track → Show Master Track**（`⌘⇧M`）で
マスタートラックを表示しておくと、同じ手順でオートメーションを描ける。

- [ ] パラメータ一覧に `Frequency` / `Gain` / `Q` / `Band Bypass` がバンドごとに出る
      （4 × 8 バンド + 全体 Bypass = 33 個）
- [ ] 描いたオートメーションどおりに音が動く
- [ ] 再生中にノブが動く（ホスト側の値が UI に反映される）

## 8. セッションの往復

1. 8 バンドに適当な値を入れ、**SOLO をオンにしたまま**保存する
2. Logic を終了して開き直す

- [ ] 全バンドの Frequency / Gain / Q / Band Bypass が復元される
- [ ] プリセット名（左上のドロップダウン）が復元される
- [ ] **ソロは解除されている**（保存対象ではないため）

## 9. サンプルレートとバッファ

プロジェクトのサンプルレートは **File → Project Settings → Audio → Sample Rate** で変える
（**環境設定 → Audio → Devices** のほうは I/O バッファとオーディオデバイスの設定）。

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
- [ ] 8 バンドすべて +12 dB にしても音切れしない

## 11. 後片付け

スクリプトは Standalone を `/Applications` か `~/Applications` の**書き込めるほう**に
だけ置く。消す前に、どちらに入ったかを確認する:

```bash
ls -d /Applications/MyFirstMasteringEQ.app ~/Applications/MyFirstMasteringEQ.app 2>/dev/null
```

そのうえで、実際に存在するものだけを消す:

```bash
rm -rf ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component
rm -rf ~/Library/Audio/Plug-Ins/VST3/MyFirstMasteringEQ.vst3
rm -rf ~/Library/Audio/Plug-Ins/CLAP/MyFirstMasteringEQ.clap
# 上の ls で出てきたパスだけを消す
rm -rf ~/Applications/MyFirstMasteringEQ.app
killall -9 AudioComponentRegistrar || true
```

検証前から入っていたものは、先頭で作った `.bak` から戻す:

```bash
for p in ~/Library/Audio/Plug-Ins/Components/MyFirstMasteringEQ.component \
         ~/Library/Audio/Plug-Ins/VST3/MyFirstMasteringEQ.vst3 \
         ~/Library/Audio/Plug-Ins/CLAP/MyFirstMasteringEQ.clap \
         ~/Applications/MyFirstMasteringEQ.app /Applications/MyFirstMasteringEQ.app; do
    [ -e "$p.bak" ] && rm -rf "$p" && mv "$p.bak" "$p"
done
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
