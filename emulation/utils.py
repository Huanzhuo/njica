#! /usr/bin/env python3
# -*- coding: utf-8 -*-
# vim:fenc=utf-8

import functools
import time


def timer(func):
    @functools.wraps(func)
    def wrapper_timer(*args, **kwargs):
        tic = time.perf_counter()
        value = func(*args, **kwargs)
        toc = time.perf_counter()
        elapsed_time = toc - tic
        print(f"Elapsed time of func: {func.__name__}: {elapsed_time:0.4f} seconds")
        return value

    return wrapper_timer
