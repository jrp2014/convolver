#!/usr/bin/env python3
"""Inspect published downstream packages without saving or executing downloads.

This checks provenance evidence, not convolution correctness. Run with --check
to require exact agreement with the recorded archive and member hashes.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tarfile
import urllib.request
import zipfile


MAX_BYTES = 32 * 1024 * 1024


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def binary_header(data):
    if data[:2] == b"MZ":
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe : pe + 4] != b"PE\0\0":
            raise ValueError("MZ member does not contain a PE signature")
        machine = struct.unpack_from("<H", data, pe + 4)[0]
        magic = struct.unpack_from("<H", data, pe + 24)[0]
        return {
            "format": "PE",
            "bits": {0x10B: 32, 0x20B: 64}.get(magic),
            "machine": f"0x{machine:04x}",
            "machine_name": {0x014C: "I386", 0x8664: "AMD64"}.get(machine),
        }
    if data[:4] == b"\x7fELF":
        endian = {1: "<", 2: ">"}[data[5]]
        machine = struct.unpack_from(endian + "H", data, 18)[0]
        return {
            "format": "ELF",
            "bits": {1: 32, 2: 64}.get(data[4]),
            "machine": machine,
            "machine_name": {3: "I386", 62: "AMD64", 183: "AARCH64"}.get(machine),
        }
    raise ValueError("Member has neither a PE nor an ELF header")


def inspect(package):
    request = urllib.request.Request(
        package["url"], headers={"User-Agent": "Convolver-Provenance-Inspector/1.0"}
    )
    with urllib.request.urlopen(request, timeout=45) as response:
        archive = response.read(MAX_BYTES + 1)
        final_url = response.geturl()
    if len(archive) > MAX_BYTES:
        raise ValueError("Archive exceeds the 32 MiB inspection limit")

    member = package["member"]
    if package["archive_format"] == "zip":
        with zipfile.ZipFile(io.BytesIO(archive)) as container:
            info = container.getinfo(member["path"])
            if info.file_size > MAX_BYTES:
                raise ValueError("Member exceeds the 32 MiB inspection limit")
            binary = container.read(info)
            names = container.namelist()
    elif package["archive_format"] == "tar.gz":
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as container:
            info = container.getmember(member["path"])
            if not info.isfile() or info.size > MAX_BYTES:
                raise ValueError("Member is not a regular file within the size limit")
            stream = container.extractfile(info)
            if stream is None:
                raise ValueError("Cannot read archive member")
            binary = stream.read(MAX_BYTES + 1)
            names = container.getnames()
    else:
        raise ValueError("Unsupported archive format")

    header = binary_header(binary)
    markers = {text: text.encode("ascii") in binary for text in member["markers"]}
    archive_hash = sha256(archive)
    member_hash = sha256(binary)
    checks = {
        "archive_sha256_matches": archive_hash == package["sha256"],
        "archive_size_matches": len(archive) == package["bytes"],
        "member_sha256_matches": member_hash == member["sha256"],
        "member_size_matches": len(binary) == member["bytes"],
        "binary_header_matches": header == member["binary_header"],
        "all_markers_present": all(markers.values()),
    }
    return {
        "id": package["id"],
        "url": package["url"],
        "final_url": final_url,
        "archive_sha256": archive_hash,
        "archive_bytes": len(archive),
        "member_path": member["path"],
        "member_sha256": member_hash,
        "member_bytes": len(binary),
        "binary_header": header,
        "markers": markers,
        "source_or_licence_filename_candidates": [
            name
            for name in names
            if any(
                token in name.lower()
                for token in ("source", "license", "licence", "copying", ".cpp", ".hpp")
            )
        ],
        "checks": checks,
        "matches_recorded_evidence": all(checks.values()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--evidence",
        type=Path,
        default=Path(__file__).resolve().with_name("downstream-evidence.json"),
    )
    parser.add_argument("--package", action="append", help="Inspect only this package id")
    parser.add_argument("--check", action="store_true", help="Exit nonzero on any mismatch")
    args = parser.parse_args()
    evidence = json.loads(args.evidence.read_text(encoding="utf-8"))
    packages = evidence["inspected_packages"]
    if args.package:
        unknown = set(args.package) - {p["id"] for p in packages}
        if unknown:
            parser.error("Unknown package ids: " + ", ".join(sorted(unknown)))
        packages = [p for p in packages if p["id"] in args.package]
    results = []
    for package in packages:
        try:
            results.append(inspect(package))
        except Exception as error:
            results.append({"id": package["id"], "error": str(error)})
    json.dump(results, sys.stdout, indent=2)
    print()
    errors = any("error" in result for result in results)
    mismatch = any(not r.get("matches_recorded_evidence", False) for r in results)
    return int(errors or (args.check and mismatch))


if __name__ == "__main__":
    raise SystemExit(main())
