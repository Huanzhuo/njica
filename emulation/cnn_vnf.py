#! /usr/bin/env python3
# -*- coding: utf-8 -*-

"""
About: Python script for distributed CNN processing.
       This script is loaded and called by the fast path implemented in ./cnn_vnf.cpp
"""

import pickle
import struct
import sys
import typing

sys.path.insert(0, "../")


def run_cnn_dist(
    X_bytes: bytes,
) -> bytes:
    """Run distributed CNN on bytes_in and return the calculated result."""
    X = pickle.loads(X_bytes)

    # TODO: <He> Process the X data with the fancy neural network.
    result_data = X

    # MARK: Metadata could be added here to mark the processing status of the
    # data.
    bytes_out = pickle.dumps(result_data)
    return bytes_out
