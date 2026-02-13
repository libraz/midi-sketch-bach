# midi-sketch-bach

> **アルファ版** ── 開発中のプロジェクトです。機能やAPIは変更される可能性があります。デモおよびビルド済みバイナリは準備中です。

J.S.バッハの器楽作品に特化したMIDIジェネレーター。

バッハの器楽音楽 ── オルガン・フーガ、無伴奏弦楽組曲、室内ソナタ ── は、対位法・和声論理・形式構造の驚異的な建築の上に成り立っています。本プロジェクトはこれら器楽作品のみを対象とし、その構造原理 ── 厳格な声部進行、チェロ組曲の和声的流れ、オルガン・フーガの多声的テクスチャ ── を可能な限り再現し、演奏可能なMIDIとして出力することを試みるプロジェクトです。

対位法規則・声部進行・楽曲構造など、まだ多くの既知の問題が残っています。フィードバックや貢献を歓迎します。

ポップス/現代音楽ジェネレーター [midi-sketch](https://github.com/libraz/midi-sketch) の開発知見を活かして構築されています。**CLIツール**・**JavaScript/WASMライブラリ**・**インタラクティブWebデモ**として利用できます。

## 生成できる楽曲形式

**オルガン作品** ── 対位法駆動・多声部：

| 形式 | 範とする作品 | 声部数 |
|------|-------------|--------|
| 前奏曲とフーガ | BWV 532, 548 | 2-5 |
| フーガ | 厳格な3声フーガ | 2-5 |
| トリオ・ソナタ | BWV 525-530 | 3 |
| コラール前奏曲 | BWV 599-650（オルゲルビュッヒライン） | 3-4 |
| トッカータとフーガ | BWV 565 | 3-4 |
| パッサカリア | BWV 582 | 3-4 |
| 幻想曲とフーガ | BWV 537, 542 | 3-4 |

**無伴奏弦楽作品** ── 和声駆動・単旋律：

| 形式 | 範とする作品 | 楽器 |
|------|-------------|------|
| チェロ前奏曲 | BWV 1007（組曲第1番） | チェロ |
| シャコンヌ | BWV 1004（パルティータ第2番） | ヴァイオリン |

**鍵盤作品** ── 変奏曲形式：

| 形式 | 範とする作品 | 声部数 |
|------|-------------|--------|
| ゴルトベルク変奏曲 | BWV 988 | 2-3 |

フーガでは提示部・真正/変格応答・対主題・嬉遊部・ストレッタを正しく構成し、平行5度・平行8度は禁則として排除、声部交差も解決します。

## クイックスタート

### CLI

```bash
make build
./build/bin/bach_cli                                        # ハ長調の前奏曲とフーガ
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

```bash
make demo   # http://localhost:8080/demo/
```

## CLIオプション

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `--form FORM` | 楽曲形式 | `prelude_and_fugue` |
| `--key KEY` | 調（例: `g_minor`, `D_major`） | `C_major` |
| `--voices N` | 声部数（2-5、オルガンのみ） | `3` |
| `--character CH` | `severe`, `playful`, `noble`, `restless` | 自動 |
| `--instrument INST` | `organ`, `harpsichord`, `piano`, `violin`, `cello`, `guitar` | 自動 |
| `--scale SCALE` | `short`, `medium`, `long`, `full` | `short` |
| `--bars N` | 目標小節数（`--scale`を上書き） | - |
| `--bpm N` | テンポ（40-200） | `72` |
| `--seed N` | 乱数シード（0 = ランダム） | `0` |
| `--json` | JSONイベントデータ出力 | - |
| `--analyze` | 分析メタデータを含める | - |
| `-o FILE` | 出力ファイルパス | `output.mid` |

## ビルド

```bash
make build          # C++ CLI
make test           # テスト実行（C++ 1100件以上 / JS 18件）
make quality-gate   # フォーマット + ビルド + テスト
make wasm           # WASM + JSバインディング
```

必要環境: C++17コンパイラ、CMake 3.15+。WASMビルドにはEmscriptenが必要です。

## ライセンス

[Apache-2.0](LICENSE) / [商用](LICENSE-COMMERCIAL) デュアルライセンス。商用利用のお問い合わせ: libraz@libraz.net
