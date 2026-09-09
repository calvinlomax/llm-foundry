#!/usr/bin/env python3
"""Verify normal launch executes only the existing GUI, even from a path with spaces."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='cinder launcher ') as directory:
    root = Path(directory)
    launcher = root / 'cinder.command'
    shutil.copy2(source, launcher)
    gui = root / 'build/release/foundry-gui'
    gui.parent.mkdir(parents=True)
    gui.write_text('#!/bin/bash\nprintf "GUI launched\\n%s\\n%s\\n%s\\n" "$PWD" "$FOUNDRY_HOME" "$FOUNDRY_PROJECT_DIR"\n')
    gui.chmod(0o755)
    result = subprocess.run(['/bin/bash', str(launcher)], cwd='/', text=True,
                            capture_output=True, timeout=10, check=True)
    assert result.stdout.splitlines() == ['GUI launched', str(root), str(root / 'models/registry'), str(root)]
    assert not result.stderr, result.stderr
print('Clean launcher and paths containing spaces passed')
