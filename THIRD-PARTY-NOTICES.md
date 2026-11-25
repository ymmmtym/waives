# Third-party notices

このリポジトリ、およびここから生成したバイナリには次のソフトウェアが含まれる。
バイナリを配布するときは、このファイルも一緒に配布すること（MIT ライセンスの条件）。

## JUCE 9

- 入手元: <https://github.com/juce-framework/JUCE>
- ライセンス: **AGPLv3 または商用 EULA のデュアルライセンス**
- 本リポジトリでの選択: AGPLv3（[JUCE Licensing FAQ](https://juce.com/get-juce/#licensing-faq)）
- 著作権: Raw Material Software Limited
- 取得方法: CMake の `FetchContent` でビルド時に取得する。リポジトリには同梱していない
- ビルド済みバイナリには JUCE のコードが静的リンクされる。AGPLv3 の条件により、
  バイナリを配布する場合は対応するソース（本リポジトリ）を提供する必要がある

商用 EULA で利用する場合の条件は [JUCE 9 EULA](https://juce.com/legal/juce-9-licence/) を参照。
EULA 2.3 は、フレームワークをソース開示を要求するオープンソースライセンスの対象に
することを禁じているため、AGPL と商用 EULA は併用できない（どちらか一方を選ぶ）。

## CLAP

- 入手元: <https://github.com/free-audio/clap>
- 用途: プラグイン形式の仕様とヘッダー
- ライセンス: MIT

```text
MIT License

Copyright (c) 2021 Alexandre BIQUE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## clap-juce-extensions

- 入手元: <https://github.com/free-audio/clap-juce-extensions>
- 用途: JUCE プロジェクトに CLAP 出力を追加する
- 固定コミット: `3837f4a6ed2ab0ba46aba69ae5b7041c7c64ce74`
- ライセンス: MIT

```text
Copyright 2019-2020, Paul Walker

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## GitHub Actions

CI とリリースのワークフローでは `actions/checkout`、`actions/cache`、
`actions/upload-artifact`、`actions/download-artifact`（いずれも MIT）を使用している。
これらはビルド環境で動くもので、配布物には含まれない。
