#! /usr/bin/env python3
# -*- coding: utf-8 -*-

"""
About: Python script for distributed MEICA processing.
       This script is loaded and called by the fast path implemented in ./meica_vnf.cpp
"""

import pickle
import struct
import sys
import typing

sys.path.insert(0, "../")

from pyfastbss_core import pyfbss

# This is used as the default payload size for each chunk.
MEICA_IP_TOTAL_LEN: typing.Final = 1400  # bytes
EXTRACTION_BASE: typing.Final = 2


def run_meica_dist(
    X_bytes: bytes,
    uW_bytes: bytes,
    iter_num: int,
    max_rounds: int,
) -> bytes:
    """Run distributed MEICA on bytes_in and return the calculated result.

    :param x_bytes: X matrix in bytes.
    :param uW_bytes: uW in bytes.
    :param iter_num: Current iteration number.
    :param max_rounds: Maximal allowed iteration rounds to run meica_dist.

    :return bytes_out: Return has_final_result + new_iter_num + uW_next
    """
    X = pickle.loads(X_bytes)
    uXs = pyfbss.meica_generate_uxs(X, ext_multi_ica=EXTRACTION_BASE)

    # Get the initial uW to iterate.
    if iter_num == 0:
        assert len(uW_bytes) == 0
        uW_prev = pyfbss.generate_initial_matrix_B(X)
        iter_start_index = 0
    else:
        assert len(uW_bytes) != 0
        # uW_prev should be contained in the result chunks.
        uW_prev = pickle.loads(uW_bytes)
        iter_start_index = iter_num

    # Set to True if break_by_tol or all iterations have finished.
    has_final_result = False
    break_by_tol = False
    round_num = 0

    for index in range(iter_start_index, len(uXs), 1):
        uW_next, break_by_tol = pyfbss.meica_dist_get_uw(uXs[index], uW_prev)
        uW_prev = uW_next
        round_num += 1

        if break_by_tol or (index == len(uXs) - 1):
            # Fast-break by tolerance or all iterations have finished.
            has_final_result = True
            break

        if round_num == max_rounds:
            # Already run out of allowed compute rounds. So interactions break
            # in the middle, current uW_next need to be passed to the next
            # computing node.
            has_final_result = False
            break

    if has_final_result:
        new_iter_num = len(uXs)
        result_data = uW_next
    else:
        new_iter_num = index + 1
        result_data = uW_next

    # WARN: Use bytearray if there is performance issues.
    bytes_out = struct.pack("!BB", int(has_final_result), new_iter_num) + pickle.dumps(
        result_data
    )
    return bytes_out
