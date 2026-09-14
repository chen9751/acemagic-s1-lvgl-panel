"""Exercise actual command execution against a fake busctl, without Bluetooth."""
import ctypes
import os
from pathlib import Path
import sys
import tempfile

lib = ctypes.CDLL(sys.argv[1])
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    log = root / 'calls'
    script = root / 'busctl'
    script.write_text('''#!/usr/bin/env python3
import os, sys
from pathlib import Path
args = sys.argv[1:]
scenario = os.environ.get('S1_TEST_SCENARIO', 'normal')
device = '/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF'
if 'tree' in args:
    print(device)
elif 'get-property' in args:
    if args[-1] == 'Connected':
        print('b ' + ('false' if scenario == 'disconnected' else 'true'))
    elif scenario == 'control_only':
        sys.exit(1)
    else:
        print('o "' + device + '/player' + ('1' if scenario == 'reconnect' else '0') + '"')
elif 'GetAll' in args:
    print('a{sv} 1 "Status" s "playing"')
else:
    with open(os.environ['S1_TEST_LOG'], 'a') as f:
        f.write(' '.join(args) + '\\n')
    if 'org.bluez.MediaPlayer1' in args and scenario in ('unsupported', 'timeout'):
        print('Not supported' if scenario == 'unsupported' else 'Connection timed out')
        sys.exit(1)
''')
    script.chmod(0o755)
    os.environ['PATH'] = directory + os.pathsep + os.environ['PATH']
    os.environ['S1_TEST_LOG'] = str(log)
    def action(scenario, command):
        os.environ['S1_TEST_SCENARIO'] = scenario
        log.write_text('')
        result = lib.bluez_media_control(command)
        return result, log.read_text().splitlines()
    lib.bluez_media_init()
    result, calls = action('normal', 1)
    assert result == 0 and len(calls) == 1 and calls[0].endswith('MediaPlayer1 Pause')
    result, calls = action('reconnect', 2)
    assert result == 0 and '/player1 ' in calls[0] and calls[0].endswith('Next')
    result, calls = action('control_only', 0)
    assert result == 0 and len(calls) == 1 and calls[0].endswith('MediaControl1 Previous')
    result, calls = action('unsupported', 2)
    assert result == 0 and len(calls) == 2 and calls[1].endswith('MediaControl1 Next')
    result, calls = action('timeout', 2)
    assert result != 0 and len(calls) == 1, 'Do not duplicate Next on an ambiguous timeout'
    result, calls = action('disconnected', 1)
    assert result != 0 and not calls
    assert lib.bluez_media_control(99) != 0
print('PASS: real subprocess discovery, addressed player, AVRCP fallback, reconnect and failure paths')
