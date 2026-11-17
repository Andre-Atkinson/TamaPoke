#!/usr/bin/env python3
"""Send sprite files to the board's sprite storage over USB, only the ones you name.

Standard library only (it talks through tools/board.py's console, no pyserial). macOS and
Linux; the board's USB port must not be open in another program.

  python3 tools/send_sd.py ps151.bin            # one shiny sprite (dex number 151)
  python3 tools/send_sd.py ps025.bin ps133.bin  # several, in this order
  python3 tools/send_sd.py --check ps151.bin    # would it fit? (read-only: sends no file)
  python3 tools/send_sd.py --ls                 # what the board holds, and the room left
  python3 tools/send_sd.py --port /dev/cu.usbmodem101 ps151.bin

A plain name is a file in tools/sdcard/mons/ (tools/pack_pmd.py makes them); a name with a
/ in it is a path, so a shell glob works too (tools/sdcard/mons/ps00*.bin). Each file goes
to the board as mons/<name>: pNNN.bin is dex number NNN's sprite, psNNN.bin its shiny one
and thumbs.bin the gallery's thumbnails. The firmware reads no other name, so no other
name is sent.

Before sending anything it reads the board's free space (SDINFO) and what it already holds
(LS), and sends nothing unless every file fits. A file the board already has is replaced:
PUT deletes the old copy first, so its space counts as free. The board has no command that
deletes a file, so a file sent stays until the sprite storage is flashed again.

The 1.75C keeps its sprites in the LittleFS `storage` partition, which holds the 151
normal sprites and thumbs.bin with about 8.5 MB to spare, room for some shiny sprites
(PORTING.md, "Sprite storage trade-off"). The original 1.75 keeps them on its microSD card.
"""
import argparse
import os
import re
import shlex
import sys
import termios
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)  # board.py and dex_data.py sit next to this script

from board import Console, NoDone, NoReply, find_port  # noqa: E402
from dex_data import DEX  # noqa: E402

MONS = os.path.join(TOOLS, 'sdcard', 'mons')
DEX_MAX = max(n for n, *_ in DEX)
PUT_MAX = 4 * 1024 * 1024  # sdmon.cpp: PUT refuses an empty file and one over 4 MB
CHUNK = 2048               # sdmon.cpp: PUT reads the data in 2048-byte blocks, acking each with '#'
FS_BLOCK = 4096            # tools/build_spritefs.sh: the 1.75C's LittleFS block size
FS_SPARE = 16 * FS_BLOCK   # left free, so the file system still has room for its own writes
# Seconds to wait for PUT's OK, for each block's '#' and for the DONE after the last block.
# The firmware gives up on a file after 5 s without data (Serial.setTimeout(5000) in sdmon.cpp)
# and then prints ERR: RESYNC_TIMEOUT waits for that ERR after a failure, so that the board is
# listening for commands again before this script lets go of the port.
OK_TIMEOUT, ACK_TIMEOUT, DONE_TIMEOUT, RESYNC_TIMEOUT = 10, 15, 15, 7
LS_TIMEOUT = 15            # LS lists every file in mons/: about 300 lines at most

NAME = re.compile(r'(p|ps|s|)(\d{3})\.bin')


class BoardError(Exception):
    """The board's answer can't be used (no storage, or a reply that came through damaged)."""


def kb(n):
    return f'{n / 1024:,.0f} KB'


def magic_for(name):
    """The first four bytes of a file the firmware reads as mons/<name>, or None if it reads
    no file of that name (sdmon.cpp: PmdMon::load, SdMon::load, SdThumbs::load)."""
    if name == 'thumbs.bin':
        return b'TPTH'
    m = NAME.fullmatch(name)
    if not m or not 1 <= int(m.group(2)) <= DEX_MAX:
        return None
    return b'TPK2' if m.group(1).startswith('p') else b'TPK1'


def load_files(args):
    """[(arg, board name, bytes)] for the files named; exits listing every problem, before
    anything talks to the board, when there is one."""
    files, problems = [], []
    seen = set()
    for arg in args:
        path = arg if '/' in arg else os.path.join(MONS, arg)
        name = os.path.basename(path)
        magic = magic_for(name)
        if magic is None:
            problems.append(f'{arg}: the board reads no file called {name!r} (only pNNN.bin, '
                            f'psNNN.bin, NNN.bin and sNNN.bin for NNN 001 to {DEX_MAX:03d}, '
                            'and thumbs.bin)')
            continue
        if name in seen:
            problems.append(f'{arg}: {name} is named twice')
            continue
        seen.add(name)
        try:
            with open(path, 'rb') as fh:
                data = fh.read(PUT_MAX + 1)
        except FileNotFoundError:
            problems.append(f'{arg}: no such file' if '/' in arg else
                            f'{arg}: no such file in tools/sdcard/mons/ (tools/pack_pmd.py makes it)')
            continue
        except OSError as e:
            problems.append(f'{arg}: {e.strerror}')
            continue
        if len(data) > PUT_MAX:
            problems.append(f'{arg}: over {kb(PUT_MAX)}, more than PUT takes')
        elif data[:4] != magic:
            problems.append(f'{arg}: not a sprite file of its kind (it should start with '
                            f'{magic.decode()})')
        else:
            files.append((arg, name, data))
    if problems:
        sys.exit('send_sd.py: nothing sent:\n  ' + '\n  '.join(problems))
    return files


def parse_sdinfo(lines):
    """(total, used) in bytes from the reply to SDINFO, or None."""
    for text in lines:
        m = re.fullmatch(r'total=(\d+) used=(\d+)', text)
        if m:
            return int(m.group(1)), int(m.group(2))
    return None


def parse_ls(lines):
    """{name: size} from the reply to LS. The firmware prints one 'name size' line per file
    in mons/; other lines (the HEALTH heartbeat) are skipped."""
    have = {}
    for text in lines:
        m = re.fullmatch(r'(\S+\.\w+) (\d+)', text)
        if m:
            have[m.group(1).rsplit('/', 1)[-1]] = int(m.group(2))
    return have


def storage(con):
    """(total, used, {name: size}) from SDINFO and LS."""
    info = parse_sdinfo(con.command('SDINFO'))
    if info is None:
        raise BoardError('no total=/used= line in the reply to SDINFO; try again')
    total, used = info
    if total == 0:
        raise BoardError('the board has no sprite storage mounted (SDINFO says total=0): the '
                         "1.75C's sprite image is not flashed (tools/flash_175c.sh --fs-only), "
                         'or the original 1.75 has no microSD card')
    have = parse_ls(con.command('LS', timeout=LS_TIMEOUT))
    # A byte lost on the way can shorten a number. The files in mons/ alone take at least the
    # sum of their sizes, so a used= below that (or above total=) came through damaged.
    if used > total or sum(have.values()) > used:
        raise BoardError(f'the replies to SDINFO (total={total} used={used}) and LS disagree; '
                         'a line came through damaged: try again')
    return total, used, have


def new_cost(size):
    """The most space a new file of `size` bytes takes on LittleFS: its data in whole blocks,
    each giving up to 64 bytes to the file's block list, and one more block. (A FAT microSD
    card's clusters can be bigger, but the card is many times the size of all the sprites.)"""
    return (-(-size // (FS_BLOCK - 64)) + 1) * FS_BLOCK


def old_freed(size):
    """The least space the board's own copy of `size` bytes gives back: its data in whole
    blocks."""
    return -(-size // FS_BLOCK) * FS_BLOCK


def plan(files, have):
    """(net, peak): the space the files take once all are sent (negative when they free some),
    and the most they take at any point on the way, in bytes."""
    net = peak = 0
    for _, name, data in files:
        net += new_cost(len(data)) - (old_freed(have[name]) if name in have else 0)
        peak = max(peak, net)
    return net, peak


def wait_for(con, wanted, timeout):
    """The first line among `wanted` the board prints within `timeout` s, or None. Other lines
    (the HEALTH heartbeat, a 'cargado ...' after the previous file) are skipped."""
    end = time.time() + timeout
    while True:
        text = con.read_line(max(0.0, end - time.time()))
        if text is None or text in wanted:
            return text


def put(con, name, data):
    """Send one file with PUT (sdmon.cpp's sdSerialCommand). None once the board has it
    whole, else what went wrong."""
    size = len(data)

    def failed(why, got):
        # Unless the board said ERR itself, it may still be waiting for data: let it give up
        # (5 s) and say ERR, so that it doesn't take the next command as part of the file.
        if got != 'ERR':
            wait_for(con, ('ERR',), RESYNC_TIMEOUT)
        return why

    con.write(f'PUT mons/{name} {size}\n'.encode())
    got = wait_for(con, ('OK', 'ERR'), OK_TIMEOUT)
    if got != 'OK':
        return failed('the board refused it (ERR)' if got else 'no answer to PUT', got)
    for off in range(0, size, CHUNK):
        con.write(data[off:off + CHUNK], timeout=ACK_TIMEOUT)
        got = wait_for(con, ('#', 'ERR'), ACK_TIMEOUT)
        if got != '#':
            said = 'the board said ERR' if got else 'no answer'
            return failed(f'it stopped after {kb(off)} of {kb(size)} ({said})', got)
    got = wait_for(con, ('DONE', 'ERR'), DONE_TIMEOUT)
    if got != 'DONE':
        return failed('the board said ERR after the last block' if got
                      else 'no DONE after the last block', got)
    return None


def unusable(name):
    """What the board shows while mons/<name> is only part of a file (scene.cpp's drawPet():
    the firmware does not fall back to the normal sprite when a shiny file is broken)."""
    if name == 'thumbs.bin':
        return 'the gallery shows no thumbnails'
    if not name.startswith('p'):
        return 'the board cannot use it'
    who = "that species' shiny form" if name.startswith('ps') else 'that species'
    return (f'{who} shows a "?" and "No sprites" where its sprite goes (the nine starter-line '
            'species show their small built-in sprite)')


def send(con, files, port):
    for i, (arg, name, data) in enumerate(files):
        started = time.time()
        try:
            why = put(con, name, data)
        except OSError as e:
            why = str(e)
        if why is None:
            print(f'{name}: sent, {kb(len(data))} in {time.time() - started:.1f} s', flush=True)
            continue
        print(f'{name}: FAILED: {why}', flush=True)
        rest = [a for a, _, _ in files[i:]]
        again = shlex.join(['python3', 'tools/send_sd.py', *(['--port', port] if port else []), *rest])
        msg = (f'{name} did not arrive whole. PUT deletes the copy the board had before it '
               'starts, so the board may now hold only part of it, and it cannot use a partial '
               f'file: until it is sent whole, {unusable(name)}. Send it again (PUT replaces '
               f'it):\n  {again}')
        if len(rest) > 1:
            msg = f'Not sent: {", ".join(n for _, n, _ in files[i + 1:])}.\n{msg}'
        sys.exit(msg)
    print(f'All {len(files)} sent.' if len(files) > 1 else 'Sent.')


def run(args, files):
    con = Console(find_port(args.port))
    try:
        con.drain()
        total, used, have = storage(con)
        free = total - used
        if args.ls:
            for name in sorted(have):
                print(f'{name} {have[name]}')
            print(f'{len(have)} file{"" if len(have) == 1 else "s"} in mons/; '
                  f'{kb(free)} free of {kb(total)}')
            return
        print(f'Free on the board: {kb(free)} of {kb(total)}.')
        for _, name, data in files:
            old = f"replaces the board's copy ({kb(have[name])})" if name in have else 'new'
            print(f'  {name:12} {kb(len(data)):>9}  {old}')
        net, peak = plan(files, have)
        room = free - FS_SPARE
        if peak > room:
            sys.exit(f'send_sd.py: not enough room, nothing sent. These take up to {kb(peak)} and '
                     f'the board has {kb(max(room, 0))} to give ({kb(free)} free, less '
                     f'{kb(FS_SPARE)} the file system keeps). Name fewer files; --check says '
                     'whether they fit without sending anything.')
        print(f'They fit: about {kb(free - net)} will be free afterwards.')
        if args.check:
            print('Nothing sent (--check).')
            return
        send(con, files, args.port)
    finally:
        con.close()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 epilog='Examples: send_sd.py ps151.bin; send_sd.py --check '
                                        'ps151.bin; send_sd.py --ls')
    ap.add_argument('--port', help='the serial port (default: the only /dev/cu.usbmodem* or '
                                   '/dev/ttyACM*)')
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument('--ls', action='store_true',
                      help='list the files the board holds and the room left (read-only)')
    mode.add_argument('--check', action='store_true',
                      help='say whether the files fit, and send nothing (read-only)')
    ap.add_argument('files', nargs='*', metavar='FILE',
                    help='a name in tools/sdcard/mons/ (ps151.bin) or a path')
    args = ap.parse_args(argv)
    if args.ls and args.files:
        ap.error('--ls takes no file names')
    if not args.ls and not args.files:
        ap.error('name the files to send, for example: send_sd.py ps151.bin '
                 '(a shiny Mew). See --help.')

    files = [] if args.ls else load_files(args.files)
    try:
        run(args, files)
    except (NoReply, BoardError) as e:
        sys.exit(f'send_sd.py: {e}')
    except NoDone as e:
        sys.exit(f'send_sd.py: {e}; try again')
    except (OSError, termios.error) as e:
        sys.exit(f'send_sd.py: {e}. Is the board plugged in, awake, and the port free '
                 '(not open in another program)?')


if __name__ == '__main__':
    main()
