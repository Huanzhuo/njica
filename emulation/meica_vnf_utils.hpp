/*
 * meica_vnf_utils.hpp
 */

#pragma once

#include <stdint.h>

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_udp.h>

#include <vector>

namespace meica
{
/**
 * MEICA service header (little-endian): Check the header definition in ./meica_host.py.
 */
struct service_header_cpu {
	uint8_t msg_type;
	uint8_t msg_flags;
	rte_le16_t total_msg_num;
	rte_le16_t msg_num;
	rte_le16_t total_chunk_num;
	rte_le16_t chunk_num;
	rte_le16_t chunk_len;
	rte_le16_t data_chunk_num;
	rte_le16_t iter_num;
};

constexpr uint32_t SERVICE_HEADER_OFFSET = sizeof(struct rte_ether_hdr) +
					   sizeof(struct rte_ipv4_hdr) +
					   sizeof(struct rte_udp_hdr);

constexpr uint32_t SERVICE_HEADER_LEN = sizeof(struct service_header_cpu);

constexpr uint32_t ALL_HEADERS_LEN = SERVICE_HEADER_OFFSET + SERVICE_HEADER_LEN;

void print_service_header(const struct service_header_cpu &hdr);

// Pack and unpack the MEICA service header from DPDK's mbuf.
struct service_header_cpu unpack_service_header(struct rte_mbuf *m);

void pack_service_header(struct rte_mbuf *m,
			 const struct service_header_cpu &hdr);

// Functions for rte_mbuf processing.

struct rte_mbuf *deepcopy_chunk(struct rte_mempool *pool,
				const struct rte_mbuf *m);

void disable_udp_cksum(struct rte_mbuf *m);
void recalc_ipv4_udp_cksum(struct rte_mbuf *m);

/**
 * Check if a mbuf is a valid chunk.
 */
bool inline is_valid_chunk(struct rte_mbuf *m)
{
	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;

	eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);

	if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
		return false;
	}
	ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
					   sizeof(struct rte_ether_hdr));
	if (ipv4_hdr->next_proto_id != IPPROTO_UDP) {
		return false;
	}

	// TODO (Zuo): Verify authentication information.

	return true;
}

} // namespace meica
