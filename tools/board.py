#!/usr/bin/env python3
"""Talk to a running TamaPoke board over its USB serial console.

Standard library only (no pyserial), so it runs without a venv. macOS and
Linux; the board's USB port must not be open in another program.

  python3 tools/board.py clock            # set the board clock to this computer's local time
  python3 tools/board.py save             # write any pending pet changes to flash now
  python3 tools/board.py status           # HEALTH + STATS
  python3 tools/board.py batlog > run.csv # the battery diary as CSV (#14; read-only, BATLOG?)
  python3 tools/board.py cmd TIME         # any console command; prints the reply up to DONE
  python3 tools/board.py cmd OWNER Name   # the player's name for the speech bubbles (#23)
  python3 tools/board.py cmd BDAY 3 14    # the player's birthday, month then day (#26; BDAY 0 0 clears it)
  python3 tools/board.py --port /dev/cu.usbmodem101 clock

The firmware keeps local wall-clock time as if it were UTC: the clock screen
builds the epoch from the hour and minute you set and shows e/3600 % 24, and
Pet::today() is epoch/86400. So `clock` sends this computer's local time in
that form. (rtcbat.cpp keeps it with settimeofday on the 1.75C; on the original
board it goes to the PCF85063 through gmtime_r and is read back through mktime
with TZ unset.)
"""
import argparse
import calendar
import csv
import glob
import os
import select
import sys
import termios
import time
import unicodedata

BAUD = termios.B115200


def find_port(explicit=None):
    if explicit:
        return explicit
    ports = sorted(glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*'))
    if len(ports) == 1:
        return ports[0]
    if not ports:
        sys.exit('No board found. Plug it in with a USB-C data cable, or pass --port.')
    sys.exit(f'Several serial ports ({", ".join(ports)}); pass --port.')


def local_wallclock_epoch(now=None):
    """This computer's local wall-clock time, expressed as a UTC epoch."""
    return calendar.timegm(time.localtime(now))


class NoReply(TimeoutError):
    """The board printed nothing at all (an unknown command, or not TamaPoke)."""


class Console:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                                                     # iflag: raw
        attrs[1] = 0                                                     # oflag: raw
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL         # cflag
        attrs[3] = 0                                                     # lflag: no echo/canon
        attrs[4] = attrs[5] = BAUD
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buf = b''

    def close(self):
        os.close(self.fd)

    def _read(self, timeout):
        if select.select([self.fd], [], [], timeout)[0]:
            try:
                self.buf += os.read(self.fd, 4096)
            except BlockingIOError:
                pass

    def drain(self, seconds=0.3):
        """Discard whatever the board printed before we asked anything."""
        end = time.time() + seconds
        while time.time() < end:
            self._read(0.05)
        self.buf = b''

    def command(self, line, timeout=3.0, on_line=None, idle=None):
        """Send one console line; return the reply lines up to (not incl.) DONE.

        on_line(text) is called as each line arrives. With idle set, a reply
        that has started and then goes quiet for that long also ends the call
        (for commands that never print DONE)."""
        os.write(self.fd, line.encode('utf-8') + b'\n')
        end = time.time() + timeout
        last = time.time()
        lines = []
        while time.time() < end:
            self._read(0.1)
            while b'\n' in self.buf:
                raw, self.buf = self.buf.split(b'\n', 1)
                text = raw.decode('utf-8', 'replace').strip()
                if text == 'DONE':
                    return lines
                if text:
                    lines.append(text)
                    last = time.time()
                    if on_line:
                        on_line(text)
            if idle and lines and time.time() - last >= idle:
                return lines
        if not lines:
            raise NoReply(f'no reply to {line!r} within {timeout:.0f} s (unknown command?)')
        raise TimeoutError(f'no DONE after {line!r} within {timeout:.0f} s; got {lines!r}')


def parse_rtc(lines):
    """The epoch from a 'rtc=<n>' reply line, or None."""
    for text in lines:
        if text.startswith('rtc='):
            try:
                return int(text[4:])
            except ValueError:
                return None
    return None


# The battery diary (#14, batdiary.h): BATLOG? answers a "BATLOG rows=<n> cap=<n> unsaved=<n>
# period_min=<n>" line, this header and one row per line, oldest first.
BATLOG_HEADER = 'epoch,event,pct,mv,usb,chg,on_min,sleep_min'
BATLOG_CSV = ('time', 'event', 'battery_pct', 'battery_mv', 'state', 'screen_on_min', 'asleep_min')
BATLOG_TIMEOUT = 10  # s: the dump is about 4 KB, and the reply may follow a ~1 s NVS write


class BatlogError(ValueError):
    """A BATLOG? reply that is not the whole diary."""


def parse_batlog(lines):
    """The rows of a BATLOG? reply (dicts, oldest first) and its first line's fields.

    Lines before the BATLOG line (a heartbeat that came first) are skipped. A reply cut
    short (fewer rows than it announces) or one that is not a diary raises BatlogError."""
    start = next((i for i, t in enumerate(lines) if t.startswith('BATLOG ')), None)
    if start is None:
        raise BatlogError(f'no BATLOG line in the reply: {lines!r}')
    info = dict(f.split('=', 1) for f in lines[start].split()[1:] if '=' in f)
    if start + 1 >= len(lines) or lines[start + 1] != BATLOG_HEADER:
        raise BatlogError('the reply has no battery diary header')
    rows = []
    for text in lines[start + 2:]:
        f = text.split(',')
        try:
            if len(f) != 8:
                raise ValueError
            epoch, pct, mv, usb, chg, on, asleep = (int(f[0]), int(f[2]), int(f[3]), int(f[4]),
                                                    int(f[5]), int(f[6]), int(f[7]))
        except ValueError:
            raise BatlogError(f'not a diary row: {text!r}') from None
        rows.append({'epoch': epoch, 'event': f[1], 'pct': pct, 'mv': mv, 'usb': usb == 1,
                     'charging': chg == 1, 'on_min': on, 'asleep_min': asleep})
    want = info.get('rows', '')
    if not want.isdigit() or int(want) != len(rows):
        raise BatlogError(f'the dump was cut short: got {len(rows)} of {want or "?"} rows; try again')
    return rows, info


def batlog_state(row):
    """discharging, charging, full (on USB, the charger done) or 'no battery'."""
    if row['pct'] < 0:
        return 'no battery'
    if row['usb']:
        return 'charging' if row['charging'] else 'full'
    return 'discharging'


def batlog_csv_row(row):
    """A diary row as the CSV columns in BATLOG_CSV. The board keeps local wall-clock time as
    if it were UTC (see the top of this file), so gmtime gives the board's own date and time;
    an empty time is a row written while the board's clock was not set."""
    when = time.strftime('%Y-%m-%d %H:%M', time.gmtime(row['epoch'])) if row['epoch'] else ''
    return [when, row['event'], row['pct'] if row['pct'] >= 0 else '', row['mv'] or '',
            batlog_state(row), row['on_min'], row['asleep_min']]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port')
    sub = ap.add_subparsers(dest='what', required=True)
    sub.add_parser('clock', help="set the board clock to this computer's local time")
    sub.add_parser('save', help='write pending pet changes to flash now')
    sub.add_parser('status', help='print HEALTH and STATS')
    sub.add_parser('batlog', help='print the battery diary as CSV (read-only)')
    c = sub.add_parser('cmd', help='send any console command')
    c.add_argument('line', nargs='+')
    args = ap.parse_args(argv)

    try:
        run(args)
    except NoReply as e:
        sys.exit(f'board.py: {e}')
    except BatlogError as e:
        sys.exit(f'board.py: battery diary: {e}')
    except (TimeoutError, OSError, termios.error) as e:
        sys.exit(f'board.py: {e}. Is the board plugged in, awake, and the port free '
                 '(not open in another program)?')


def run(args):
    con = Console(find_port(args.port))
    try:
        con.drain()
        if args.what == 'clock':
            before = parse_rtc(con.command('TIME'))
            target = local_wallclock_epoch()
            reply = con.command(f'TIME {target}')
            # A clock that did not take the value says why (an ERR line) and still answers
            # rtc= with what it reads; the pet's saved time is then left alone.
            refused = next((t for t in reply if t.startswith('ERR')), None)
            if refused:
                sys.exit(f'clock not set; the board said: {refused}')
            after = parse_rtc(reply)
            if after is None or abs(after - target) > 2:
                sys.exit(f'clock not set: board answered rtc={after}, wanted {target}')
            drift = '' if before is None else f' (was {before - target:+d} s)'
            print(f'Board clock set to {time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(after))}{drift}')
            if before and before // 86400 > target // 86400:
                print('note: that moved the clock back across midnight; care already counted '
                      'for the later day will not count again until the clock passes it.')
        elif args.what == 'save':
            for text in con.command('SAVE'):
                print(text)
        elif args.what == 'batlog':
            try:
                reply = con.command('BATLOG?', timeout=BATLOG_TIMEOUT)
            except NoReply:
                raise NoReply('no reply to BATLOG?: the firmware on the board is older than the '
                              'battery diary (#14); update it first') from None
            rows, info = parse_batlog(reply)
            out = csv.writer(sys.stdout, lineterminator='\n')
            out.writerow(BATLOG_CSV)
            for row in rows:
                out.writerow(batlog_csv_row(row))
            note = f'{len(rows)} rows (the board keeps the newest {info.get("cap", "?")})'
            if info.get('unsaved', '0') != '0':
                note += (f'; the newest {info["unsaved"]} are not in flash yet (they are written '
                         'once the screen is dark) and a power-off now would lose them')
            print(note, file=sys.stderr)
        elif args.what == 'status':
            for cmd in ('HEALTH', 'STATS'):
                for text in con.command(cmd):
                    print(text)
        else:
            # Print as lines arrive. LOCK? never prints DONE, so 3 s of quiet
            # after some output ends the call; 3 s leaves room for a reply that
            # follows an unsolicited line (the HEALTH heartbeat) after a ~1 s
            # NVS write. A command with no reply at all (unknown) times out.
            # NFC: a letter typed as a base letter plus a combining accent goes as the
            # one precomposed letter the firmware knows (OWNER <name>, owner.h)
            line = unicodedata.normalize('NFC', ' '.join(args.line))
            con.command(line, timeout=10, idle=3.0, on_line=lambda t: print(t, flush=True))
    finally:
        con.close()


if __name__ == '__main__':
    main()
