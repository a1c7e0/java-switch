#!/usr/bin/env python3
"""Write source identity without a timestamp so unchanged builds stay stable."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
if subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip():
    revision += '-dirty'
output = root / 'build/build-info.h'
content = '#define JS_BUILD_REVISION "' + revision + '"\n'
if not output.exists() or output.read_text() != content:
    output.write_text(content)
