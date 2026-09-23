"""Capture a reproducible public generator case for output-led review."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from bachlib.common import DEFAULT_CLI, REPO_ROOT

REQUEST_KEYS = ("form", "seed", "key", "character", "instrument", "bpm", "scale", "bars")


def register(subparsers: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    parser = subparsers.add_parser(
        "output-review", help="capture a fixed-seed MIDI/JSON case with a review manifest"
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--repeat-of", type=Path, metavar="MANIFEST")
    parser.add_argument("--form")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--key")
    parser.add_argument("--character")
    parser.add_argument("--instrument")
    parser.add_argument("--bpm", type=int)
    length = parser.add_mutually_exclusive_group()
    length.add_argument("--scale")
    length.add_argument("--bars", type=int)
    parser.set_defaults(func=run)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _request(args: argparse.Namespace) -> dict[str, Any]:
    if args.repeat_of:
        if any(getattr(args, key) is not None for key in REQUEST_KEYS):
            raise ValueError("--repeat-of cannot be combined with case options")
        payload = json.loads(args.repeat_of.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("repeat manifest is not an object")
        request = payload.get("request")
        if not isinstance(request, dict) or set(request) != set(REQUEST_KEYS):
            raise ValueError("repeat manifest has no complete request")
    else:
        request = {key: getattr(args, key) for key in REQUEST_KEYS}
        if not request["form"] or request["seed"] is None:
            raise ValueError("--form and --seed are required")
        if request["scale"] is None and request["bars"] is None:
            request["scale"] = "short"
    if not isinstance(request["form"], str) or not request["form"]:
        raise ValueError("form must be a nonempty string")
    if type(request["seed"]) is not int or request["seed"] <= 0:
        raise ValueError("seed must be a positive fixed integer")
    for key in ("key", "character", "instrument", "scale"):
        if request[key] is not None and not isinstance(request[key], str):
            raise ValueError(f"{key} must be a string")
    if request["bpm"] is not None and type(request["bpm"]) is not int:
        raise ValueError("bpm must be an integer")
    if request["bars"] is not None and (type(request["bars"]) is not int or request["bars"] <= 0):
        raise ValueError("bars must be positive")
    if request["bars"] is not None and request["scale"] is not None:
        raise ValueError("bars and scale cannot both be set")
    return request


def _resolved_request(request: dict[str, Any], stdout: str) -> dict[str, Any]:
    fields = {}
    for line in stdout.splitlines():
        label, separator, value = line.partition(":")
        if separator and label.strip() in ("Key", "Character", "Instrument", "BPM"):
            fields[label.strip()] = value.strip()
    if not all(fields.get(key) for key in ("Key", "Character", "Instrument", "BPM")):
        raise ValueError("CLI output lacks resolved key, character, instrument, or BPM")
    resolved = dict(request)
    resolved["key"] = fields["Key"]
    resolved["character"] = fields["Character"].lower()
    resolved["instrument"] = fields["Instrument"].lower()
    try:
        resolved["bpm"] = int(fields["BPM"])
    except ValueError as exc:
        raise ValueError("CLI output has an invalid BPM") from exc
    return resolved


def _command(cli: Path, request: dict[str, Any], midi: Path) -> list[str]:
    command = [str(cli), "--form", request["form"], "--seed", str(request["seed"])]
    for key in ("key", "character", "instrument", "bpm", "scale", "bars"):
        if request[key] is not None:
            command.extend((f"--{key}", str(request[key])))
    return [*command, "--generated-json", "-o", str(midi)]


def _check_pair(generated_path: Path, provenance_path: Path) -> int:
    generated = json.loads(generated_path.read_text(encoding="utf-8"))
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    if generated.get("schema_version") != "generated.v1":
        raise ValueError("generated JSON is not generated.v1")
    if provenance.get("schema_version") != "provenance.v1":
        raise ValueError("provenance JSON is not provenance.v1")
    notes, origins = generated.get("notes"), provenance.get("notes")
    if not isinstance(notes, list) or not isinstance(origins, list) or len(notes) != len(origins):
        raise ValueError("note/provenance counts differ")
    if not notes:
        raise ValueError("generated piece contains no notes")
    for index, (note, origin) in enumerate(zip(notes, origins)):
        if not isinstance(note, dict) or not isinstance(origin, dict):
            raise ValueError(f"note {index} is not an object")
        if (
            type(note.get("index")) is not int
            or type(origin.get("index")) is not int
            or note["index"] != index
            or origin["index"] != index
        ):
            raise ValueError(f"note/provenance index mismatch at {index}")
        if origin.get("source") not in ("Material", "Compose", "Ornament"):
            raise ValueError(f"missing note source at {index}")
        span_id = origin.get("span_id")
        if "span_id" not in origin or (
            span_id is not None and (type(span_id) is not int or span_id < 0)
        ):
            raise ValueError(f"invalid span_id at {index}")
        if not isinstance(origin.get("voice_intent"), str) or not origin["voice_intent"]:
            raise ValueError(f"missing note provenance at {index}")
    return len(notes)


def run(args: argparse.Namespace) -> int:
    try:
        request = _request(args)
        cli = args.cli.resolve(strict=True)
        if not cli.is_file():
            raise ValueError(f"CLI is not a file: {cli}")
        output_dir = args.output_dir.resolve()
        output_dir.mkdir(parents=True, exist_ok=False)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"output-review: {exc}", file=sys.stderr)
        return 2

    midi_path = output_dir / "piece.mid"
    command = _command(cli, request, midi_path)
    manifest: dict[str, Any] = {
        "schema_version": "output-review.v1",
        "request": request,
        "submitted_request": request,
        "request_resolution": "submitted_only",
        "command": command,
        "cli_sha256": _sha256(cli),
        "files": {},
        "checks": {},
        "errors": [],
    }
    errors: list[str] = manifest["errors"]
    try:
        process = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, check=False)
        cli_exit_code: int | None = process.returncode
        stdout, stderr = process.stdout, process.stderr
    except OSError as exc:
        cli_exit_code = None
        stdout, stderr = "", str(exc)
    (output_dir / "cli.stdout").write_text(stdout, encoding="utf-8")
    (output_dir / "cli.stderr").write_text(stderr, encoding="utf-8")
    manifest["cli_exit_code"] = cli_exit_code
    if cli_exit_code != 0:
        errors.append(f"bach_cli exited {cli_exit_code}: {stderr.strip()}")

    generated_path = output_dir / "piece.generated.json"
    provenance_path = output_dir / "piece.provenance.json"
    diagnostic_path = output_dir / "piece.diagnostic.json"
    if cli_exit_code != 0 and diagnostic_path.is_file():
        try:
            diagnostic = json.loads(diagnostic_path.read_text(encoding="utf-8"))
            if diagnostic.get("schema_version") == "diagnostic.v1":
                manifest["checks"]["diagnostic_validation"] = diagnostic.get("validation")
            else:
                errors.append("diagnostic JSON has an unexpected schema version")
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"cannot read diagnostic JSON: {exc}")
    if cli_exit_code == 0:
        try:
            manifest["request"] = _resolved_request(request, stdout)
            manifest["request_resolution"] = "resolved"
        except ValueError as exc:
            errors.append(str(exc))
        for path in (midi_path, generated_path, provenance_path):
            if not path.is_file() or path.stat().st_size == 0:
                errors.append(f"missing or empty artifact: {path.name}")
        if generated_path.is_file():
            check = subprocess.run(
                [
                    sys.executable,
                    str(REPO_ROOT / "scripts" / "bach_tools.py"),
                    "validate",
                    str(generated_path),
                ],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            (output_dir / "schema.log").write_text(check.stdout + check.stderr, encoding="utf-8")
            manifest["checks"]["generated_schema_exit_code"] = check.returncode
            if check.returncode != 0:
                errors.append(f"generated schema validation exited {check.returncode}")
        if generated_path.is_file() and provenance_path.is_file():
            try:
                manifest["checks"]["index_parallel_notes"] = _check_pair(
                    generated_path, provenance_path
                )
            except (OSError, ValueError, json.JSONDecodeError) as exc:
                errors.append(str(exc))

    for path in output_dir.iterdir():
        if path.is_file() and path.name != "manifest.json":
            manifest["files"][path.name] = _sha256(path)
    manifest["ok"] = not errors
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(output_dir / "manifest.json")
    for error in errors:
        print(f"output-review: {error}", file=sys.stderr)
    return 0 if not errors else 1
