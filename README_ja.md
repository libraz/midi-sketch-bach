# midi-sketch-bach

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/midi-sketch-bach/ci.yml?branch=main&label=CI)](https://github.com/libraz/midi-sketch-bach/actions)
[![codecov](https://codecov.io/gh/libraz/midi-sketch-bach/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/midi-sketch-bach)
[![Live Demo](https://img.shields.io/badge/demo-bach.midi--sketch.libraz.net-6b21a8)](https://bach.midi-sketch.libraz.net/ja/)
[![License](https://img.shields.io/badge/license-AGPL--3.0%20%2F%20Commercial-green)](LICENSE)

> **アルファ版** ── 開発中のプロジェクトです。機能やAPIは変更される可能性があります。ビルド済みバイナリは準備中です。

**🎹 今すぐ試す: [bach.midi-sketch.libraz.net](https://bach.midi-sketch.libraz.net/ja/)** ── ブラウザ上でバッハ様式の楽曲を生成・再生できます（WebAssembly）。[ドキュメント](https://bach.midi-sketch.libraz.net/ja/docs/getting-started)と[対位法講座](https://bach.midi-sketch.libraz.net/ja/docs/counterpoint/intervals)も同サイトにあります。

J.S.バッハの器楽作品に特化したMIDIジェネレーター。

バッハの器楽音楽 ── オルガン・フーガ、無伴奏弦楽組曲、室内ソナタ ── は、対位法・和声論理・形式構造の驚異的な建築の上に成り立っています。本プロジェクトはこれら器楽作品のみを対象とし、その構造原理 ── 厳格な声部進行、チェロ組曲の和声的流れ、オルガン・フーガの多声的テクスチャ ── を可能な限り再現し、演奏可能なMIDIとして出力することを試みるプロジェクトです。

対位法規則・声部進行・楽曲構造など、まだ多くの既知の問題が残っています。フィードバックや貢献を歓迎します。

ポップス/現代音楽ジェネレーター [midi-sketch](https://github.com/libraz/midi-sketch) の開発知見を活かして構築されています。**CLIツール**・**JavaScript/WASMライブラリ**・**インタラクティブWebデモ**として利用できます。

## 生成できる楽曲形式

**オルガン作品** ── 対位法駆動・多声部：

| 形式 | 範とする作品 | 声部数 |
|------|-------------|--------|
| 前奏曲とフーガ | BWV 532, 548 | 3 |
| フーガ | 厳格な3声フーガ | 3 |
| トリオ・ソナタ | BWV 525-530 | 3 |
| コラール前奏曲 | BWV 599-650（オルゲルビュッヒライン） | 3 |
| トッカータとフーガ | BWV 565 | 3 |
| パッサカリア | BWV 582 | 3 |
| 幻想曲とフーガ | BWV 537, 542 | 3 |

**弦楽作品** ── 独奏弦の音色を想定した和声駆動の書法です。シャコンヌは
不変の低音を軸に複数声部で実現します：

| 形式 | 範とする作品 | 楽器 |
|------|-------------|------|
| チェロ前奏曲 | BWV 1007（組曲第1番） | チェロ |
| シャコンヌ | BWV 1004（パルティータ第2番） | ヴァイオリン |

**鍵盤作品** ── 変奏曲形式：

| 形式 | 範とする作品 | 声部数 |
|------|-------------|--------|
| ゴルトベルク変奏曲 | BWV 988 | 3 |

`Goldberg Variations` は BWV 988 の楽譜再現ではなく、圧縮した構造モデルです。
専用の32音からなるアリア低音句を4小節単位へ写像し、`--scale full` では
アリア、30変奏（9つのカノンと2旋律のクォドリベット枠）、終端コーダを伴う
アリア再提示の全128小節を生成します。

フーガは提示部・真正/変格応答・対主題を構成します。以降の技法は形式と長さに
よって異なります。`fugue` と `prelude_and_fugue` はどの長さでも中間提示と
ストレッタを持ちます。嬉遊部は展開部に余地がある場合に入り、展開部に提示が2つ以上
あれば属音保続音も入ります。`toccata_and_fugue` と
`fantasia_and_fugue` のフーガ部は提示部・ストレッタ・終止で、間は嬉遊部ではなく
音型でつなぎます。32小節未満ではストレッタを持たず、24小節未満では提示部の
入りが2回だけで、低音声部は入りません。
最終スコア validator は出力ノート上の平行5度・平行8度と声部交差を監査し、
固定素材に由来する検出結果も報告します。

## クイックスタート

### CLI

```bash
make build
./build/bin/bach_cli                                        # ハ長調のフーガ
./build/bin/bach_cli --form fugue --key g_minor --seed 42
./build/bin/bach_cli --form chaconne --scale full
./build/bin/bach_cli --form cello_prelude --bpm 120 -o prelude.mid
```

### JavaScript / WASM

```typescript
import { init, BachGenerator } from '@libraz/midi-sketch-bach';

await init();
const bach = new BachGenerator();
bach.generate({ form: 'fugue', key: 0, isMinor: true, seed: 42 });

const midi = bach.getMidi();     // Uint8Array
const events = bach.getEvents(); // パース済みJSONノートデータ
bach.destroy();
```

### Webデモ

ホスト版: **[bach.midi-sketch.libraz.net](https://bach.midi-sketch.libraz.net/ja/)** ── ローカルで動かす場合:

```bash
make demo   # http://localhost:8080/demo/
```

## CLIオプション

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `--form FORM` | 楽曲形式 | `fugue` |
| `--key KEY` | 調（例: `g_minor`, `D_major`） | `C_major` |
| `--character CH` | `severe`, `playful`, `noble`, `restless` | `severe` |
| `--instrument INST` | `organ`, `harpsichord`, `piano`, `violin`, `cello`（形式ごとの受理楽器は下表） | 形式の既定 |
| `--scale SCALE` | `short`, `medium`, `long`, `full` | `short` |
| `--bars N` | 目標小節数（`--scale`を上書き） | - |
| `--bpm N` | テンポ（40-200） | `100` |
| `--seed N` | 乱数シード（0 = ランダム） | `0` |
| `--json` | JSONイベントデータ出力 | - |
| `--generated-json` | generated.v1 + provenance.v1 JSON出力 | - |
| `--free-counterpoint` | 実験的: パッサカリアの副次対旋律をスコア付き探索で生成（デフォルト無効）。他の形式は利用不可の診断を返す | - |
| `-o FILE` | 出力ファイルパス | `output.mid` |

形式ごとに受理される楽器（これ以外の組み合わせは拒否されます）:

| 形式 | 楽器（先頭が既定） |
|------|-------------------|
| `fugue`, `prelude_and_fugue`, `trio_sonata`, `chorale_prelude`, `toccata_and_fugue`, `passacaglia`, `fantasia_and_fugue` | `organ` |
| `cello_prelude` | `cello` |
| `chaconne` | `violin` |
| `goldberg_variations` | `harpsichord`, `piano` |

## ビルド

```bash
make build          # C++ CLI
make test           # C++テスト実行（ctest 980件以上）
yarn test           # JS/WASMテスト実行（vitest 210件以上）
make quality-gate   # フォーマット + ビルド + テスト
make wasm           # WASM + JSバインディング
```

必要環境: C++17コンパイラ、CMake 3.15+。WASMビルドにはEmscriptenが必要です。

## ライセンス

[AGPL-3.0](LICENSE) / [商用](LICENSE-COMMERCIAL) デュアルライセンス。AGPL-3.0 の条件下では自由に利用・改変・再配布できます。クローズドソース製品やプロプライエタリな SaaS への組み込みには商用ライセンスが必要です。商用利用のお問い合わせ: libraz@libraz.net
