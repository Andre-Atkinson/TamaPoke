#!/usr/bin/env python3
"""Talk to a running TamaPoke board over its USB serial console.

Standard library only (no pyserial), so it runs without a venv. macOS and
Linux; the board's USB port must not be open in another program.

  python3 tools/board.py clock            # set the board clock to this computer's local time
  python3 tools/board.py save             # write any pending pet changes to flash now
  python3 tools/board.py status           # HEALTH + STATS
  python3 tools/board.py cmd TIME         # any console command; prints the reply up to DONE
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
import glob
import os
import select
import sys
import termios
import time

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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port')
    sub = ap.add_subparsers(dest='what', required=True)
    sub.add_parser('clock', help="set the board clock to this computer's local time")
    sub.add_parser('save', help='write pending pet changes to flash now')
    sub.add_parser('status', help='print HEALTH and STATS')
    c = sub.add_parser('cmd', help='send any console command')
    c.add_argument('line', nargs='+')
    args = ap.parse_args(argv)

    try:
        run(args)
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
            after = parse_rtc(con.command(f'TIME {target}'))
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
        elif args.what == 'status':
            for cmd in ('HEALTH', 'STATS'):
                for text in con.command(cmd):
                    print(text)
        else:
            # Print as lines arrive; some commands (LOCK?, unknown ones) never
            # print DONE, so a quiet second after output is a normal end.
            con.command(' '.join(args.line), timeout=10, idle=1.0,
                        on_line=lambda t: print(t, flush=True))
    finally:
        con.close()


if __name__ == '__main__':
    main()
