#! /usr/bin/env python3
# -*- coding: utf-8 -*-
# vim:fenc=utf-8

"""
About: Build MEICA VNF binary.
"""

import os
import sys

from comnetsemu.net import Containernet
from mininet.log import info, setLogLevel

PARENT_DIR = os.path.abspath(os.path.join(os.path.curdir, os.pardir))

if __name__ == "__main__":
    if os.geteuid() != 0:
        print("Run this script with sudo.", file=sys.stderr)
        sys.exit(1)
    setLogLevel("error")

    print(
        "* Build VNF executables... Built executables are located in ./build/ directory."
    )
    try:
        net = Containernet(
            xterms=False,
        )

        builder = net.addDockerHost(
            "builder",
            dimage="in-network_bss",
            ip="10.0.1.11/16",
            docker_args={
                "cpuset_cpus": "0",
                "hostname": "builder",
                "volumes": {
                    PARENT_DIR: {"bind": "/in-network_bss", "mode": "rw"},
                },
                "working_dir": "/in-network_bss/emulation",
            },
        )
        net.start()
        ret = builder.cmd("cd /in-network_bss/emulation && make clean && make")
        print(ret)
    finally:
        net.stop()
