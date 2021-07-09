#! /usr/bin/env python3
# -*- coding: utf-8 -*-
# vim:fenc=utf-8

"""
About: Core functions of a MEICA end host.

Currently, source data (X matrix) is fragmented into UDP datagrams with a
preamble (or header) for distributed MEICA.
The choice of UDP instead of TCP is due to following reasons:

- The TCP is designed based on end-to-end principle. For example, the sender
  would stop sending new segments and re-transmit the already sent segments when
  it does not receive the ACKs from the server. In our COIN scenario, the
  network nodes need to buffer source data for computing. If the source data can
  not be packed into the TCP segments of a window size. The network can not get
  the data to compute.

- Flow granularity: TCP provides a steam of bytes. Data is dynamically
  distributed across different segments (controlled by the TCP stack). The data
  needed for COIN computing can be split up and difficult to reassemble.

Therefore, UDP is used just for fast **PROTOTYPING**.

Datagram-based transport protocols like SCTP could be a reasonable candidate for
advanced transport features including re-transmission and congestion control.

These sources are just used for **PoC**, the long term roadmap is to implement the ideas into SCTP protocol.
"""

import logging
import math
import pickle
import struct
import sys
import typing

from dataclasses import dataclass

import utils

import numpy as np

sys.path.insert(0, "../")

from pyfastbss_testbed import pyfbss_tb

# This is used as the default payload size for each chunk.
MEICA_IP_TOTAL_LEN: typing.Final[int] = 1400  # bytes

# The number of chunks has a big impact on the queuing and transmission latency
# for COIN applications. So the maximal chunk number should be limited. For
# larger data, multiple messages should be used.
MAX_CHUNK_NUM: typing.Final[int] = 4096

LEVELS = {
    "debug": logging.DEBUG,
    "info": logging.INFO,
}


def get_logger(level):
    log_level = LEVELS.get(level, None)
    if not log_level:
        raise RuntimeError("Unknown logging level!")
    logger = logging.getLogger("meica_host")
    handler = logging.StreamHandler()
    formatter = logging.Formatter("%(asctime)s %(levelname)-6s %(message)s")
    handler.setFormatter(formatter)
    logger.addHandler(handler)
    logger.setLevel(log_level)
    return logger


"""
This customized header (in the UDP payload) is used since I have not found a
de-facto standard way for COIN applications.
Discussion of issues of transport layer protocol can be found [here](https://datatracker.ietf.org/rg/coinrg/about/).
This is ONLY a draft version for a single stream, sufficiency and consistency are not considered yet.

Service Header ONLY for the **DATAPLANE or data messages**. The control messages are NOT yet implemented.
Service Header Fields:

0               1               2               3
0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
+---------------+---------------+-------------+---------------+
| Message Type  | Message Flags | Total Message Number (D)    |
+---------------+---------------+-------------+---------------+
| Message Number                | Total Chunk Number          |
+---------------+---------------+-------------+---------------+
| Chunk Number                  | Chunk Length                |
+---------------+---------------+-------------+---------------+

! Above fields could be merged into header(s) of message-oriented transport
protocol such as [SCTP](https://en.wikipedia.org/wiki/Stream_Control_Transmission_Protocol).
+-------------------------------------------------------------+
! Following fields are related to the idea of distributed MEICA.

+---------------+---------------+-------------+---------------+
| Data Chunk Number (D)         | Iteration Number            |
+---------------+---------------+-------------+---------------+

# TODO: Replace (D)EPRECIATED fields with something useful.

- Message Type:
    - 0: Unprocessed raw data (e.g. mixture matrix X).
    - 1: Intermediate result of the iteration (e.g. uW).

    TBD:
    - 2: Preprocessed data (e.g. uX generated from X).


- Message Flags: Reserved. The description depends on the message type.

    - Message type 0: Unused.

    - Message type 1:
        - 0: The iteration was not completed.
        - 1: The iteration is completed.

- Total Message Number (DEPRECIATED): Total number of messages to send.
- Message Number: Sequence number of current message.

- Total Chunk Number: Total number of chunks in the current message.
- Chunk Number: Sequence number of the current chunk.

- Chunk Length: A 16-bit unsigned value specifying the total length of the chunk
  in bytes (excludes any padding), that includes all fields of service header.

- Data Chunk Number (DEPRECIATED): Number of data chunks. Now it equals total
  chunk num.

- Iteration number: Current iteration number of the processing function (The 'mu' in MEICA code).

Several fields are currently missing for addressing, security or advanced
transport layer features including congestion control, retransmission.
They will be explored in the future work.
"""


@dataclass
class ServiceHeader(object):

    _PACK_STR = "!BBHHHHHHH"

    msg_type: int = 0
    msg_flags: int = 0
    total_msg_num: int = 0
    msg_num: int = 0
    total_chunk_num: int = 0
    chunk_num: int = 0
    chunk_len: int = 0
    data_chunk_num: int = 0
    iter_num: int = 0
    length: int = struct.calcsize(_PACK_STR)

    def serialize(self) -> bytes:
        data = struct.pack(
            self._PACK_STR,
            self.msg_type,
            self.msg_flags,
            self.total_msg_num,
            self.msg_num,
            self.total_chunk_num,
            self.chunk_num,
            self.chunk_len,
            self.data_chunk_num,
            self.iter_num,
        )
        return data

    @classmethod
    def parse(cls, data: bytes):
        ret = struct.unpack(cls._PACK_STR, data)
        return cls(*ret)


class MEICAHost(object):

    """Base class for a MEICA-enabled end host."""

    @staticmethod
    def serialize(x_array):
        return pickle.dumps(x_array)

    def fragment(
        self, x_array: np.ndarray, msg_type: int, total_msg_num: int, msg_num: int
    ) -> tuple:
        """Fragment X matrix into chunks.

        :param x_array: X matrix in np.ndarray format.
        :param msg_type: Type of the message.
        :param total_msg_num: Total message number.
        :param msg_num: Current message number.

        :return: A tuple of all chunks (header+payload) and the length of the serialized X matrix in bytes.
        """
        x_bytes = self.serialize(x_array)
        full_chunks_num = math.floor(len(x_bytes) / MEICA_IP_TOTAL_LEN)
        total_chunk_num = full_chunks_num + 1
        chunks = list()

        for c in range(full_chunks_num):
            hdr = ServiceHeader(
                msg_type=msg_type,
                msg_flags=0,
                total_msg_num=total_msg_num,
                msg_num=msg_num,
                total_chunk_num=total_chunk_num,
                chunk_num=c,
                chunk_len=MEICA_IP_TOTAL_LEN + ServiceHeader.length,
                data_chunk_num=total_chunk_num,
                iter_num=0,
            )
            chunks.append(
                (
                    hdr.serialize(),
                    x_bytes[c * MEICA_IP_TOTAL_LEN : (c + 1) * MEICA_IP_TOTAL_LEN],
                )
            )

        # The last chunk.
        hdr = ServiceHeader(
            msg_type=msg_type,
            msg_flags=0,
            total_msg_num=total_msg_num,
            msg_num=msg_num,
            total_chunk_num=total_chunk_num,
            chunk_num=full_chunks_num,
            chunk_len=len(x_bytes)
            - (MEICA_IP_TOTAL_LEN * full_chunks_num)
            + ServiceHeader.length,
            data_chunk_num=total_chunk_num,
            iter_num=0,
        )
        chunks.append(
            (hdr.serialize(), x_bytes[full_chunks_num * MEICA_IP_TOTAL_LEN :])
        )

        return (chunks, len(x_bytes))

    def check_chunks(self, chunks: list) -> bool:
        """Run sanity checks on chunks.

        :param chunks: A list of all chunks of a single message.
        """
        prev_chunk_num = -1
        for hdr_bytes, _ in chunks:
            hdr = ServiceHeader.parse(hdr_bytes)
            chunk_num = hdr.chunk_num
            if chunk_num != prev_chunk_num + 1:
                return False
            prev_chunk_num = chunk_num

        return True

    def defragment(self, chunks: list) -> np.ndarray:
        """Defragment chunks into array in np.ndarray format

        :param chunks: A list of chunks.

        :return: A tuple of data array and result array.
        """
        array_bytes = b"".join(c[1] for c in chunks)
        array = pickle.loads(array_bytes)
        return array


@utils.timer
def run_tests():
    print("* Run basic tests...")

    header = ServiceHeader(
        msg_type=0,
        msg_flags=0,
        total_msg_num=1,
        msg_num=0,
        total_chunk_num=100,
        chunk_num=0,
        chunk_len=1416,
        data_chunk_num=90,
        iter_num=0,
    )
    assert header.length == 16
    data = header.serialize()

    new_header = ServiceHeader.parse(data)
    new_data = new_header.serialize()
    assert data == new_data
    assert new_header.msg_type == 0
    assert new_header.msg_flags == 0
    assert new_header.total_msg_num == 1
    assert new_header.msg_num == 0
    assert new_header.total_chunk_num == 100
    assert new_header.chunk_num == 0
    assert new_header.chunk_len == 1416
    assert new_header.data_chunk_num == 90
    assert new_header.iter_num == 0

    folder_address = "/in-network_bss/google_dataset/32000_wav_factory"
    _, _, X = pyfbss_tb.generate_matrix_S_A_X(
        folder_address,
        3,
        2,
        mixing_type="normal",
        max_min=(1, 0.01),
        mu_sigma=(0, 1),
    )
    data_size = len(X.tobytes())
    print(f"- Data size: {data_size}")
    host = MEICAHost()
    chunks, msg_len = host.fragment(X, msg_type=0, total_msg_num=1, msg_num=0)
    print(f"- Message size: {msg_len}")
    new_X = host.defragment(chunks)
    assert (X == new_X).all()

    print("* All tests passed!")


if __name__ == "__main__":
    run_tests()
