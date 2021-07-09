/*
 * meica_vnf_utils.cpp
 */

#include <cassert>
#include <iostream>

#include "meica_vnf_utils.hpp"

using namespace std;

namespace meica
{
struct _service_header {
	uint8_t msg_type;
	uint8_t msg_flags;
	rte_be16_t total_msg_num;
	rte_be16_t msg_num;
	rte_be16_t total_chunk_num;
	rte_be16_t chunk_num;
	rte_be16_t chunk_len;
	rte_be16_t data_chunk_num;
	rte_be16_t iter_num;
};

void print_service_header(const struct service_header_cpu &hdr)
{
	cout << "--- MEICA Service header:" << endl;
	cout << "- Message type: " << unsigned(hdr.msg_type) << endl;
	cout << "- Message flags: " << unsigned(hdr.msg_flags) << endl;
	cout << "- Total message number:" << hdr.total_msg_num << endl;
	cout << "- Message number:" << hdr.msg_num << endl;
	cout << "- Total chunk number: " << hdr.total_chunk_num << endl;
	cout << "- Chunk number: " << hdr.chunk_num << endl;
	cout << "- Chunk length: " << hdr.chunk_len << endl;
	cout << "- Data chunk length:" << hdr.data_chunk_num << endl;
	cout << "- Iteration number: " << hdr.iter_num << endl;
}

struct service_header_cpu unpack_service_header(struct rte_mbuf *m)
{
	struct service_header_cpu hdr_cpu;
	hdr_cpu.msg_flags = 0;

	struct _service_header *hdr_ptr;

	hdr_ptr = rte_pktmbuf_mtod_offset(m, struct _service_header *,
					  SERVICE_HEADER_OFFSET);

	hdr_cpu.msg_type = hdr_ptr->msg_type;
	hdr_cpu.msg_flags = hdr_ptr->msg_flags;
	hdr_cpu.total_msg_num = rte_be_to_cpu_16(hdr_ptr->total_msg_num);
	hdr_cpu.msg_num = rte_be_to_cpu_16(hdr_ptr->msg_num);
	hdr_cpu.total_chunk_num = rte_be_to_cpu_16(hdr_ptr->total_chunk_num);
	hdr_cpu.chunk_num = rte_be_to_cpu_16(hdr_ptr->chunk_num);
	hdr_cpu.chunk_len = rte_be_to_cpu_16(hdr_ptr->chunk_len);
	hdr_cpu.data_chunk_num = rte_be_to_cpu_16(hdr_ptr->data_chunk_num);
	hdr_cpu.iter_num = rte_be_to_cpu_16(hdr_ptr->iter_num);

	return hdr_cpu;
}

void pack_service_header(struct rte_mbuf *m,
			 const struct service_header_cpu &hdr)
{
	struct _service_header *hdr_ptr;
	hdr_ptr = rte_pktmbuf_mtod_offset(m, struct _service_header *,
					  SERVICE_HEADER_OFFSET);
	hdr_ptr->msg_type = hdr.msg_type;
	hdr_ptr->msg_flags = hdr.msg_flags;
	hdr_ptr->total_msg_num = rte_cpu_to_be_16(hdr.total_msg_num);
	hdr_ptr->msg_num = rte_cpu_to_be_16(hdr.msg_num);
	hdr_ptr->total_chunk_num = rte_cpu_to_be_16(hdr.total_chunk_num);
	hdr_ptr->chunk_num = rte_cpu_to_be_16(hdr.chunk_num);
	hdr_ptr->chunk_len = rte_cpu_to_be_16(hdr.chunk_len);
	hdr_ptr->data_chunk_num = rte_cpu_to_be_16(hdr.data_chunk_num);
	hdr_ptr->iter_num = rte_cpu_to_be_16(hdr.iter_num);
}

struct rte_mbuf *deepcopy_chunk(struct rte_mempool *pool,
				const struct rte_mbuf *m)
{
	assert(m != nullptr && pool != nullptr);
	if (m->nb_segs > 1) {
		rte_exit(EXIT_FAILURE,
			 "Deep copy doest not support scattered segments.\n");
	}
	struct rte_mbuf *m_copy;
	m_copy = rte_pktmbuf_alloc(pool);
	if (m_copy == nullptr) {
		rte_exit(EXIT_FAILURE, "Failed to allocate the m_copy!\n");
	}
	m_copy->data_len = m->data_len;
	m_copy->pkt_len = m->pkt_len;
	if (rte_pktmbuf_headroom(m) != RTE_PKTMBUF_HEADROOM) {
		rte_exit(EXIT_FAILURE, "mbuf's header room is not default.\n");
	}
	rte_memcpy(rte_pktmbuf_mtod(m_copy, uint8_t *),
		   rte_pktmbuf_mtod(m, uint8_t *), m->data_len);
	return m_copy;
}

void disable_udp_cksum(struct rte_mbuf *m)
{
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
					   sizeof(struct rte_ether_hdr));
	udp_hdr = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr +
					 sizeof(struct rte_ipv4_hdr));
	udp_hdr->dgram_cksum = 0;
}

void recalc_ipv4_udp_cksum(struct rte_mbuf *m)
{
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
					   sizeof(struct rte_ether_hdr));
	udp_hdr = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr +
					 sizeof(struct rte_ipv4_hdr));
	udp_hdr->dgram_cksum = 0;
	ipv4_hdr->hdr_checksum = 0;
	ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr);
}

} // namespace meica
