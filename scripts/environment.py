#!/usr/bin/env python3
"""Record reproducibility facts; unavailable observations stay explicit."""
from datetime import datetime, timezone
import json
import platform
import subprocess

commands = [['uname','-sm'],['clang','--version'],['cmake','--version'],['python3','--version'],
            ['pkg-config','--modversion','gtk4'],['git','-C','vendor/llama.cpp','rev-parse','HEAD'],['df','-h','.']]
if platform.system() == 'Darwin':
    commands += [['sw_vers'],['sysctl','-n','hw.memsize','hw.logicalcpu'],['pmset','-g','batt']]
report = {'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(), 'commands': []}
for cmd in commands:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        report['commands'].append({'argv': cmd, 'returncode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr})
    except (OSError, subprocess.TimeoutExpired) as e:
        report['commands'].append({'argv': cmd, 'unavailable': str(e)})
print(json.dumps(report, indent=2))
