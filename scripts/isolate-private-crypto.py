#!/usr/bin/env python3
"""Namespace private Mbed TLS globals in a packaged JavaSwitch archive."""
import argparse
import os
import pathlib
import subprocess
import tempfile


def defined_crypto_symbols(nm, archive):
    output = subprocess.check_output(
        [nm, "-g", "--defined-only", str(archive)], text=True
    )
    symbols = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            name = fields[-1]
            if name.startswith("mbedtls_"):
                symbols.add(name)
    return sorted(symbols)


def isolate(archive, nm, objcopy, map_out=None):
    archive = pathlib.Path(archive)
    symbols = defined_crypto_symbols(nm, archive)
    if not symbols:
        return []

    mapping = [(name, "js_private_" + name) for name in symbols]
    # Keep the replacement beside the archive: the checkout may live on an
    # overlay filesystem where the system temporary directory is another mount.
    with tempfile.TemporaryDirectory(
        prefix=".javaswitch-crypto-", dir=archive.parent
    ) as temporary:
        temporary = pathlib.Path(temporary)
        map_path = temporary / "symbols.map"
        output_path = temporary / archive.name
        map_path.write_text("".join(f"{old} {new}\n" for old, new in mapping))
        subprocess.run(
            [objcopy, "--redefine-syms=" + str(map_path), str(archive), str(output_path)],
            check=True,
        )
        # Keep the archive index usable by both GNU ld and SDK consumers.
        os.replace(output_path, archive)

    if map_out:
        pathlib.Path(map_out).write_text(
            "".join(f"{old} {new}\n" for old, new in mapping)
        )
    return symbols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--map-out", type=pathlib.Path)
    args = parser.parse_args()
    symbols = isolate(args.archive, args.nm, args.objcopy, args.map_out)
    print(f"Namespaced {len(symbols)} private Mbed TLS symbols in {args.archive}")


if __name__ == "__main__":
    main()
