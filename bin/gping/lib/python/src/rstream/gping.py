#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

"""
rstream-gping - (https://rstream.io/) - interactive ping tool using rstream primitives

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-gping [options]
  rstream-gping (-h|--help)
  rstream-gping --version

example:
  rstream-gping
  rstream-gping -s 1 --period 250 --max-ping 10

options:
  -h --help             show this screen
  --version             show version
  --uri=ARG             URI [default: 127.0.0.1:6003]
  -s --sessions=ARG     number of sessions to run simultaneously [default: 1]
  -j --jobs=ARG         number of threads to run simultaneously (0 = auto) [default: 0]
  -t --timeout=ARG      maximum amount of time in milliseconds that command will run [default: 10000]
  --period=ARG          time in milliseconds that nperf waits between successive executions attempts (0 = do not wait) [default: 250]
  --max-ping=ARG        maximum number of ping sent per execution (0 = infinite) [default: 50]
  --ping-size=ARG       ping size in bytes expressed as a power of 2 [default: 4]
  --protocol=ARG        protocol to use [default: websocket]

valid protocols: websocket, plain
"""

from collections import deque
import asciichartpy
import docopt
import rstream.common
import rstream.core
import rstream.io
import rstream.nperf
import rstream.version
import os
import signal
import subprocess
import sys

def format_time_us(time, precision = 3):
    unit = "us"
    if time > 1000.0:
        unit = "ms"
        time /= 1000.0
        if time > 1000.0:
            unit = "s"
            time /= 1000.0
    tmp = "{:." + str(precision) + "f}"
    return tmp.format(time) + " " + unit
    
def format_sample(sample):
    if sample == None:
        return ["-", "-", "-", "-", "-", "-"]
    else:
        return [str(sample.type), sample.size, format_time_us(sample.min_us), format_time_us(sample.max_us), format_time_us(sample.mean_us), format_time_us(sample.stdev_us)]
    
def main():
    version = "rstream-gping " + rstream.version.version()
    args = docopt.docopt(__doc__, version = version)
    terminal_size = os.get_terminal_size()
    client_config = rstream.nperf.client_config()
    client_config.address = rstream.io.address(args["--uri"])
    settings = rstream.nperf.settings_client()
    settings.common.buffer_size = 131072
    settings.common.timeouts_open_close_ms = 0
    settings.common.timeouts_max_time_ms = int(args["--timeout"])
    settings.common.protocol = rstream.nperf.protocol.names[args["--protocol"]]
    settings.execution_count = 0
    settings.max_ping = int(args["--max-ping"])
    settings.period_metrics_ms = 10
    settings.period_ms = int(args["--period"])
    settings.ping_buffer_size = int(args["--ping-size"])
    settings.sessions = int(args["--sessions"])
    settings.max_data_bytes = 0
    settings.retry = True
    options = 2 # ping mode
    jobs = int(args["--jobs"])
    serie = deque([], maxlen = terminal_size.columns - 10)
    client = rstream.nperf.client(client_config, settings, options, jobs)
    connection = handshake = ping = None
    def signal_handler_sigint(*args):
        client.stop()
    def signal_handler_sigwinch(*args):
        nonlocal terminal_size
        terminal_size = os.get_terminal_size()
        nonlocal serie
        serie = deque(serie, maxlen = terminal_size.columns - 10)
    def display(data):
        nonlocal connection
        nonlocal handshake
        nonlocal ping
        if type(data.data) == rstream.nperf.sample:
            if data.data.type == rstream.nperf.sample_type.connection:
                connection = data.data
            elif data.data.type == rstream.nperf.sample_type.handshake:
                handshake = data.data
            elif data.data.type == rstream.nperf.sample_type.ping:
                if not data.final:
                    return
                if data.options != 2:
                    return
                ping = data.data
        elif type(data.data) == rstream.common.error_code:
            connection = handshake = ping = None
        print("\033c")
        width = 10
        print()
        print("\tgping by rstream - (https://rstream.io/)")
        print()
        keys = ["type : ", "size : ", "min : ", "max : ", "mean : ", "stdev : "]
        values_connection = format_sample(connection)
        values_handshake = format_sample(handshake)
        values_ping = format_sample(ping)
        for i in range(0, len(keys)):
            print(f"{keys[i]:>12}{values_connection[i]:<12} {keys[i]:>12}{values_handshake[i]:<12} {keys[i]:>12}{values_ping[i]:<12}")
        error = "error : "
        error_msg = error_msg = data.data.message().lower() if type(data.data) == rstream.common.error_code else "-"
        print()
        print(f"{error:>12}{error_msg:<12}")
        print()
        asciichart_config = {
            'colors': [
                asciichartpy.red if type(data.data) == rstream.common.error_code else asciichartpy.green
            ],
            'offset': 3,
            'height': min(30, terminal_size.lines)
        }
        if type(data.data) == rstream.nperf.sample and data.data.type == rstream.nperf.sample_type.ping:
            serie.append(data.data.mean_us)
        elif type(data.data) == rstream.common.error_code:
            serie.append(float("nan"))
        if len(serie) > 0:
            try:
                print(asciichartpy.plot(series=[list(serie)], cfg=asciichart_config))
            except:
                pass
    signal.signal(signal.SIGINT, signal_handler_sigint)
    signal.signal(signal.SIGWINCH, signal_handler_sigwinch)
    client.start()
    error = rstream.common.error_code()
    while True:
        data = client.get()
        if type(data) == rstream.nperf.metrics:
            display(data)
        elif type(data) == rstream.common.error_code:
            error = data
            break
    client.stop()
    if error.value() != 0:
        raise SystemExit(error.message().lower())
    
if __name__ == "__main__":
    main()
