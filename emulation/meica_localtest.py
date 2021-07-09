#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
About: Test the distributed MEICA locally (without any networking).
"""

import argparse
import csv
import math
import sys
import time

import numpy as np

sys.path.insert(0, "../")

from pyfastbss_core import pyfbss
from pyfastbss_testbed import pyfbss_tb

import meica_host
import utils


def get_source_data(duration, source_num):
    S, A, X = pyfbss_tb.generate_matrix_S_A_X(
        "/in-network_bss/google_dataset/32000_wav_factory", duration, source_num
    )
    return (S, A, X)


def get_chunk_num(duration, source_num):
    print("* Run tests for chunk number.")
    _, _, X = get_source_data(duration, source_num)

    data_size = len(X.tobytes())
    chunk_num = math.ceil(float(data_size) / meica_host.MAX_CHUNK_NUM)
    print(
        f"* Duration: {duration}, source number: {source_num}, chunk number: {chunk_num}."
    )


# TODO (Zuo): Compare different compression method for X matrix.


def run_meica_dist(X, uXs: list, uW_prev):
    for i in range(len(uXs)):
        uW_next, break_by_tol = pyfbss.meica_dist_get_uw(uXs[i], uW_prev)
        uW_prev = uW_next
        if break_by_tol:
            break

    hat_S = pyfbss.meica_dist_get_hat_s(uW_next, X)
    return hat_S


def time_meica(duration, source_num, number=50):
    print("* Measure the processing latency of centralized and distributed MEICA.")
    print(
        f"* Duration: {duration}, source number: {source_num}, number of execution: {number}."
    )

    latencies = list()
    for _ in range(number):
        _, _, X = get_source_data(duration, source_num)
        start = time.time()
        _ = pyfbss.meica(X, ext_multi_ica=2)
        latencies.append(time.time() - start)
    avg_latency = np.average(latencies)
    print(f"- Average latency of centralized: {avg_latency}")

    uXs = pyfbss.meica_generate_uxs(X, ext_multi_ica=2)
    uW_prev = pyfbss.generate_initial_matrix_B(uXs[0])
    latencies = list()
    for _ in range(number):
        S, A, X = get_source_data(duration, source_num)
        start = time.time()
        run_meica_dist(X, uXs, uW_prev)
        latencies.append(time.time() - start)

    avg_latency = np.average(latencies)
    print(f"- Average latency of distributed: {avg_latency}")


def time_meica_dist(duration, source_num):
    print("* Measure the processing time of each iteration in distributed MEICA")
    print(f"* Duration: {duration}, source number: {source_num}.")
    _, _, X = get_source_data(duration, source_num)
    uXs = pyfbss.meica_generate_uxs(X, ext_multi_ica=2)
    uW_prev = pyfbss.generate_initial_matrix_B(uXs[0])
    latencies = list()
    for i in range(len(uXs)):
        start = time.time()
        uW_next, break_by_tol = pyfbss.meica_dist_get_uw(uXs[i], uW_prev)
        uW_prev = uW_next
        latencies.append(time.time() - start)
        if break_by_tol:
            break

    start = time.time()
    pyfbss.meica_dist_get_hat_s(uW_next, X)
    latencies.append(time.time() - start)
    latencies = list(map(lambda x: x * 1000, latencies))
    for i, t in enumerate(latencies[:-1]):
        print(f"- Iteration number: {i}, latency: {t} ms")
    print(f"- Latency of last step: {latencies[-1]} ms")


def evaluate_sdr(duration, source_num, number=5):
    print("* Evaluate the estimation of centralized and distributed MEICA.")
    print(
        f"* Duration: {duration}, source number: {source_num}, number of execution: {number}."
    )
    eval_meica_results = list()
    eval_meica_dist_results = list()
    for _ in range(number):
        S, A, X = get_source_data(duration, source_num)

        hat_S = pyfbss.meica(X, ext_multi_ica=2)
        eval_meica = pyfbss_tb.bss_evaluation(S, hat_S, "sdr")
        eval_meica_results.append(eval_meica)

        uXs = pyfbss.meica_generate_uxs(X, ext_multi_ica=2)
        uW_prev = pyfbss.generate_initial_matrix_B(X)

        hat_S = run_meica_dist(X, uXs, uW_prev)
        eval_meica_dist = pyfbss_tb.bss_evaluation(S, hat_S, "sdr")
        eval_meica_dist_results.append(eval_meica_dist)

    eval_meica_avg = np.average(eval_meica_results)
    eval_meica_dist_avg = np.average(eval_meica_dist_results)
    print(
        f"Evalution result of centralized: {eval_meica_avg:.4f} dB, distributed: {eval_meica_dist_avg:.4f} dB"
    )


def evaluate_ux(duration, source_num):
    print(f"- Duration: {duration}, source_num: {source_num}")
    _, _, X = get_source_data(duration, source_num)
    x_size = len(X.tobytes())
    print(f"X size: {x_size} bytes")
    uXs = pyfbss.meica_generate_uxs(X, ext_multi_ica=2)
    accum_size = 0
    for u, uX in enumerate(uXs):
        ux_size = len(uX.tobytes())
        accum_size += ux_size
        print(f"uX_{u} size: {ux_size} bytes, accumulated size: {accum_size} bytes")
    bandwidth_tax = float(accum_size) / x_size
    print(f"Bandwidth tax: {bandwidth_tax}")


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="Distributed MEICA local tests.")
    parser.add_argument(
        "test",
        type=str,
        choices=[
            "chunk_num",
            "time_meica",
            "time_meica_dist",
            "evaluate_sdr",
            "evaluate_ux",
        ],
    )
    args = parser.parse_args()

    if args.test == "chunk_num":
        for duration in range(1, 6, 1):
            for source_num in range(2, 11, 1):
                get_chunk_num(duration, source_num)

    if args.test == "time_meica":
        time_meica(5, 10, number=10)

    if args.test == "time_meica_dist":
        for source_num in range(2, 11, 1):
            time_meica_dist(1, source_num)

    # Warning: Time consuming
    if args.test == "evaluate_sdr":
        evaluate_sdr(5, 3, number=1)

    if args.test == "evaluate_ux":
        for source_num in range(2, 11, 1):
            evaluate_ux(1, source_num)
