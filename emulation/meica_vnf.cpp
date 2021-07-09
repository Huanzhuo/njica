/*
 * meica_vnf.cpp
 *
 * Copyright (C) 2020 Zuo Xiang <xianglinks@gmail.com>
 */

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_flow.h>
#include <rte_ip.h>
#include <rte_log.h>
#include <rte_memcpy.h>
#include <rte_udp.h>

#include <ffpp/config.h>
#include <ffpp/munf.h>
#include <ffpp/utils.h>

#include <pybind11/embed.h>
#include <pybind11/stl.h>
namespace py = pybind11;

#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <boost/asio/ip/host_name.hpp>

#include "meica_vnf_utils.hpp"

using namespace std;

namespace meica
{
/* MEICA VNF related constants */
static constexpr uint16_t BURST_SIZE = 128; // burst size for both RX and TX.
static constexpr uint16_t MAX_CHUNK_SIZE = 1400; // bytes

/* TODO:  <26-01-21, Zuo>: Remove this global variable. */
struct rte_mempool *fast_forward_pool = NULL;

/**
 * Working states of the MEICA VNF.
 * TODO: Rename them aligned to the names used in the paper.
 */
enum class VNF_STATE {
	RESET,
	FORWARD_X_CHUNKS,
	RECV_UW_CHUNKS,
	TRY_FORWARD_UW_CHUNKS,
	PROCESS_CHUNKS,
	SEND_UW_CHUNKS,
};

/**
 * Information struct of the MEICA VNF.
 */
struct vnf_info {
	VNF_STATE state;
	uint64_t message_count;
};

/* Global variables.*/
static volatile bool g_force_quit = false;
static bool g_verbose = false;

static void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		g_force_quit = true;
	}
}

/**
 * Main loop for store and forward mode.
 */
void run_store_forward_loop(const struct ffpp_munf_manager &manager)
{
	struct rte_mbuf *rx_buf[BURST_SIZE];
	struct rte_mbuf *tx_buf[BURST_SIZE];
	uint16_t r = 0;
	uint16_t t = 0;
	struct rte_mbuf *m;
	uint16_t nb_rx = 0;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;

	uint64_t fw_num = 0;
	cout << "[MEICA] Enter store and forward loop." << endl;
	while (!g_force_quit) {
		nb_rx = rte_eth_rx_burst(manager.rx_port_id, 0, rx_buf,
					 BURST_SIZE);
		if (nb_rx == 0) {
			rte_delay_us_sleep(1e3);
			continue;
		}
		t = 0;
		for (r = 0; r < nb_rx; ++r) {
			m = rx_buf[r];
			if (!is_valid_chunk(m)) {
				rte_pktmbuf_free(m);
				continue;
			}
			disable_udp_cksum(m);
			tx_buf[t] = rx_buf[r];
			++t;
		}

		fw_num +=
			rte_eth_tx_burst(manager.tx_port_id, 0, tx_buf, nb_rx);
		RTE_LOG(DEBUG, USER1, "[FWD] Totally forwarded %lu packets.\n",
			fw_num);
	}
}

/**
 * De-fragment all chunks of a message and return the message data.
 */
string defragment(const vector<struct rte_mbuf *> &chunk_buf,
		  const vector<struct service_header_cpu> &service_hdr_buf)
{
	service_header_cpu service_hdr;

	string msg_data;
	const uint8_t *payload = nullptr;
	assert(chunk_buf.size() == service_hdr_buf.size());
	auto iter_hdr = service_hdr_buf.cbegin();
	auto iter_chunk = chunk_buf.cbegin();

	for (iter_hdr, iter_chunk; iter_hdr < service_hdr_buf.cend();
	     ++iter_hdr, ++iter_chunk) {
		payload = rte_pktmbuf_mtod_offset((*iter_chunk), uint8_t *,
						  SERVICE_HEADER_OFFSET +
							  SERVICE_HEADER_LEN);
		msg_data.append((char *)payload,
				((*iter_hdr).chunk_len - SERVICE_HEADER_LEN));
	}

	return msg_data;
}

void reset_bufs(vector<struct rte_mbuf *> &chunk_buf,
		vector<struct service_header_cpu> &service_hdr_buf)
{
	if (chunk_buf.size() != 0) {
		for (auto c : chunk_buf) {
			rte_pktmbuf_free(c);
		}
		chunk_buf.clear();
	}
	service_hdr_buf.clear();
}

bool inline check_service_hdr_buf(
	const vector<struct service_header_cpu> &service_hdr_buf)
{
	auto total_chunk_num = service_hdr_buf.back().total_chunk_num;
	if (service_hdr_buf.size() != total_chunk_num) {
		return false;
	}

	uint16_t expected_chunk_num = 0;
	for (auto hdr : service_hdr_buf) {
		// Packets are out-of-order or lost.
		if (hdr.chunk_num != expected_chunk_num) {
			return false;
		}
		expected_chunk_num += 1;
	}

	return true;
}

bool recv_send_chunks(const struct ffpp_munf_manager &manager,
		      vector<struct rte_mbuf *> &chunk_buf,
		      vector<struct service_header_cpu> &service_hdr_buf)
{
	struct rte_mbuf *m;
	struct rte_mbuf *m_copy;
	struct rte_mbuf *rx_buf[BURST_SIZE];
	struct service_header_cpu service_hdr;

	uint16_t r = 0;
	uint16_t nb_rx = 0;
	bool recv_timeout = false;

	// TODO: Add a timeout during receiving chunks for potential chunk
	// losses.
	// It is assumed that the uW always arrive AFTER X.
	while (!recv_timeout && !g_force_quit) {
		nb_rx = rte_eth_rx_burst(manager.rx_port_id, 0, rx_buf,
					 BURST_SIZE);

		if (nb_rx == 0) {
			rte_delay_us_sleep(1e3);
			continue;
		}
		for (r = 0; r < nb_rx; ++r) {
			m = rx_buf[r];
			if (!is_valid_chunk(m)) {
				rte_pktmbuf_free(m);
				continue;
			}
			service_hdr = unpack_service_header(m);
			// Fast forward all data messages
			if (service_hdr.msg_type == 0) {
				m_copy = deepcopy_chunk(fast_forward_pool, m);
				disable_udp_cksum(m_copy);
				rte_eth_tx_burst(manager.tx_port_id, 0, &m_copy,
						 1);
			}
			chunk_buf.push_back(m);
			service_hdr_buf.push_back(service_hdr);
		}
		if (service_hdr_buf.back().chunk_num ==
		    service_hdr_buf.back().total_chunk_num - 1) {
			break;
		}
	}
	return true;
}

template <typename T>
void reorder(std::vector<T> &vec, std::vector<size_t> new_order)
{
	assert(vec.size() == new_order.size());

	for (size_t vv = 0; vv < vec.size() - 1; ++vv) {
		if (new_order[vv] == vv) {
			continue;
		}
		size_t oo;
		for (oo = vv + 1; oo < new_order.size(); ++oo) {
			if (new_order[oo] == vv) {
				break;
			}
		}
		std::swap(vec[vv], vec[new_order[vv]]);
		std::swap(new_order[vv], new_order[oo]);
	}
}

/**
 * Recover lost or out-of-order chunks.
 */
void recover_chunks(vector<struct rte_mbuf *> &chunk_buf,
		    vector<struct service_header_cpu> &service_hdr_buf)
{
	auto total_chunk_num = service_hdr_buf.back().total_chunk_num;
	if (service_hdr_buf.size() != total_chunk_num) {
		rte_exit(EXIT_FAILURE,
			 "Fixing lost chunks is currently not implemented!\n");
	}

	// Sort out-of-order chunks.
	std::vector<size_t> indices(chunk_buf.size());
	std::iota(indices.begin(), indices.end(), 0);
	std::sort(indices.begin(), indices.end(), [&](int A, int B) -> bool {
		return service_hdr_buf[A].chunk_num <
		       service_hdr_buf[B].chunk_num;
	});
	// Sort chunk_buf and service_hdr_buf
	reorder(chunk_buf, indices);
	reorder(service_hdr_buf, indices);
}

/**
 * Update IP and UDP total length fields with the given chunk payload length.
 */
void update_l3_l4_header(struct rte_mbuf *m, uint32_t payload_len)
{
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
					   sizeof(struct rte_ether_hdr));
	udp_hdr = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr +
					 sizeof(struct rte_ipv4_hdr));

	uint16_t udp_dgram_len =
		payload_len + SERVICE_HEADER_LEN + sizeof(struct rte_udp_hdr);
	uint16_t ip_total_length = udp_dgram_len + sizeof(struct rte_ipv4_hdr);
	udp_hdr->dgram_len = rte_cpu_to_be_16(udp_dgram_len);
	ipv4_hdr->total_length = rte_cpu_to_be_16(ip_total_length);
}

struct rte_mbuf *create_uW_chunk(struct rte_mempool *pool,
				 const struct rte_mbuf *m_data_full,
				 const struct service_header_cpu &hdr,
				 const string &payload)
{
	struct rte_mbuf *m_result;
	assert(m_data_full->data_len == 1458);
	assert(m_data_full->pkt_len == 1458);
	// Copy everything from the m_data_full and add new payload.
	m_result = deepcopy_chunk(fast_forward_pool, m_data_full);
	rte_pktmbuf_trim(m_result, MAX_CHUNK_SIZE);
	assert(m_result->data_len == 58);
	assert(m_result->pkt_len == 58);
	// Pack the new header
	pack_service_header(m_result, hdr);
	// Add payload
	rte_pktmbuf_append(m_result, payload.size());

	uint8_t *payload_offset = rte_pktmbuf_mtod_offset(
		m_result, uint8_t *,
		SERVICE_HEADER_OFFSET + SERVICE_HEADER_LEN);
	rte_memcpy(payload_offset, payload.c_str(), payload.size());

	if (unlikely(m_result->data_len != MAX_CHUNK_SIZE)) {
		update_l3_l4_header(m_result, payload.size());
	}

	return m_result;
}

void update_uW_chunk_buf(const struct ffpp_munf_manager &manager,
			 vector<struct rte_mbuf *> &uW_chunk_buf,
			 const struct rte_mbuf *m_data_full,
			 const struct service_header_cpu hdr_template,
			 bool has_final_result, uint16_t new_iter_num,
			 string new_uW_bytes)

{
	struct service_header_cpu new_hdr = hdr_template;
	new_hdr.msg_type = 1;
	new_hdr.msg_flags = 0;
	if (has_final_result == true) {
		RTE_LOG(DEBUG, USER1,
			"Final result is ready! Set message flags to 1\n");
		new_hdr.msg_flags = 1;
	}
	new_hdr.iter_num = new_iter_num;
	new_hdr.data_chunk_num = 0;

	new_hdr.total_chunk_num =
		std::ceil(double(new_uW_bytes.size()) / MAX_CHUNK_SIZE);

	// Replace previous uW with the new_uW_bytes
	for (auto m : uW_chunk_buf) {
		rte_pktmbuf_free(m);
	}
	uW_chunk_buf.clear();

	string payload;
	for (auto i = 0; i < new_uW_bytes.size(); i += MAX_CHUNK_SIZE) {
		payload = new_uW_bytes.substr(i, MAX_CHUNK_SIZE);
		new_hdr.chunk_len = payload.size() + SERVICE_HEADER_LEN;
		new_hdr.chunk_num = i;
		uW_chunk_buf.push_back(create_uW_chunk(
			manager.pool, m_data_full, new_hdr, payload));
	}
}

void process_chunks(const struct ffpp_munf_manager &manager,
		    const struct rte_mbuf *m_data_full,
		    struct service_header_cpu hdr_template,
		    const string &X_bytes,
		    vector<struct rte_mbuf *> &uW_chunk_buf,
		    vector<struct service_header_cpu> &uW_service_hdr_buf,
		    const uint32_t max_rounds)
{
	auto meica_vnf_module = py::module::import("meica_vnf");
	auto run_meica_dist_func = meica_vnf_module.attr("run_meica_dist");
	bool has_final_result = false;
	string uW_bytes = "";
	uint16_t iter_num = 0;

	if (uW_chunk_buf.size() != 0) {
		uW_bytes = defragment(uW_chunk_buf, uW_service_hdr_buf);
		assert(uW_bytes.size() != 0);
		iter_num = uW_service_hdr_buf.back().iter_num;
	} else {
		iter_num = 0;
	}

	// Call the run_meica_dist function defined in ./meica_vnf.py
	string bytes_out = run_meica_dist_func(static_cast<py::bytes>(X_bytes),
					       static_cast<py::bytes>(uW_bytes),
					       iter_num, max_rounds)
				   .cast<string>();
	if (uint8_t(bytes_out.at(0)) == 1) {
		has_final_result = true;
	}
	uint8_t new_iter_num = uint8_t(bytes_out.at(1));
	string new_uW_bytes = bytes_out.substr(2);

	// The m_data_full is a ugly workaround for poor default packet
	// generation support from DPDK. Should be replaced with a better
	// mechanism.
	update_uW_chunk_buf(manager, uW_chunk_buf, m_data_full, hdr_template,
			    has_final_result, new_iter_num, new_uW_bytes);
	return;
}

/**
 * The operation performed before sending all chunks.
 *
 * - Handle checksums.
 * - Could be used to add recoded chunks with RLNC.
 */
void pre_send_chunks(vector<struct rte_mbuf *> &chunk_buf)
{
	for (auto c : chunk_buf) {
		recalc_ipv4_udp_cksum(c);
	}
}

void send_chunks(const struct ffpp_munf_manager &manager,
		 vector<struct rte_mbuf *> &chunk_buf)
{
	pre_send_chunks(chunk_buf);
	uint64_t tx_num = 0;
	// TODO (Zuo): Optimize to burst TX.
	for (auto c : chunk_buf) {
		tx_num += rte_eth_tx_burst(manager.tx_port_id, 0, &c, 1);
	}
	RTE_LOG(DEBUG, USER1, "[MEICA] Send %lu chunks.\n", chunk_buf.size());
}

/**
 * Main loop for compute and forward mode.
 */
void run_compute_forward_loop(const struct ffpp_munf_manager &manager,
			      bool is_leader, uint32_t max_rounds)
{
	struct rte_mbuf *m;
	struct rte_mbuf *rx_buf[BURST_SIZE];
	struct rte_mbuf *tx_buf[BURST_SIZE];
	uint16_t r = 0; // number of received chunks in one burst fetch.
	uint16_t t = 0; // number of transmitted chunks in one burst fetch.

	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	uint16_t nb_rx = 0;

	cout << "[MEICA] Enter compute and forward loop." << endl;
	cout << "\t- Maximal allowed processing rounds: " << max_rounds << endl;

	vector<struct rte_mbuf *> X_chunk_buf;
	vector<struct rte_mbuf *> uW_chunk_buf;
	string X_bytes = "";
	vector<struct service_header_cpu> X_service_hdr_buf;
	vector<struct service_header_cpu> uW_service_hdr_buf;

	struct vnf_info info = {
		.state = VNF_STATE::FORWARD_X_CHUNKS,
		.message_count = 0,
	};

	py::scoped_interpreter guard{};
	while (!g_force_quit) {
		switch (info.state) {
		case VNF_STATE::RESET:
			RTE_LOG(DEBUG, USER1, "State: Reset VNF!\n");
			reset_bufs(X_chunk_buf, X_service_hdr_buf);
			reset_bufs(uW_chunk_buf, uW_service_hdr_buf);
			info.state = VNF_STATE::FORWARD_X_CHUNKS;
			break;

		case VNF_STATE::FORWARD_X_CHUNKS:
			assert(X_chunk_buf.size() == 0 &&
			       X_service_hdr_buf.size() == 0);
			RTE_LOG(DEBUG, USER1,
				"State: Receive and send X chunks.\n");
			if (recv_send_chunks(manager, X_chunk_buf,
					     X_service_hdr_buf) == true) {
				if (is_leader == true) {
					info.state = VNF_STATE::PROCESS_CHUNKS;
				} else {
					info.state = VNF_STATE::RECV_UW_CHUNKS;
				}
			} else {
				info.state = VNF_STATE::RESET;
			}
			break;

		case VNF_STATE::RECV_UW_CHUNKS:
			assert(is_leader == false);
			RTE_LOG(DEBUG, USER1, "State: Receive uW chunks.\n");
			assert(uW_chunk_buf.size() == 0 &&
			       uW_service_hdr_buf.size() == 0);
			recv_send_chunks(manager, uW_chunk_buf,
					 uW_service_hdr_buf);
			info.state = VNF_STATE::TRY_FORWARD_UW_CHUNKS;
			break;

		case VNF_STATE::TRY_FORWARD_UW_CHUNKS:
			RTE_LOG(DEBUG, USER1,
				"State: Try to fast forward uW chunks with final result.\n");
			if (uW_service_hdr_buf.front().msg_flags == 1) {
				RTE_LOG(DEBUG, USER1,
					"Current uW message is fast forwarded!\n");
				info.state = VNF_STATE::SEND_UW_CHUNKS;
			} else {
				info.state = VNF_STATE::PROCESS_CHUNKS;
			}
			break;

		case VNF_STATE::PROCESS_CHUNKS:
			RTE_LOG(DEBUG, USER1,
				"State: Process chunks. Data chunk buffer size: %lu, result chunk buffer size: %lu.\n",
				X_chunk_buf.size(), uW_chunk_buf.size());
			if (!check_service_hdr_buf(X_service_hdr_buf)) {
				RTE_LOG(DEBUG, USER1,
					"ISSUE: Need chunk recovery!\n");
				recover_chunks(X_chunk_buf, X_service_hdr_buf);
			}
			if (!check_service_hdr_buf(X_service_hdr_buf)) {
				rte_exit(EXIT_FAILURE,
					 "Failed to recover data chunks!");
			}
			// MARK: ASSUME result chunks are always in order.
			X_bytes = defragment(X_chunk_buf, X_service_hdr_buf);

			process_chunks(manager, X_chunk_buf.front(),
				       X_service_hdr_buf.front(), X_bytes,
				       uW_chunk_buf, uW_service_hdr_buf,
				       max_rounds);

			// Original X chunks are useless now, cleanup them.
			// ONLY the uW_chunk_buf needs to be sent.
			for (auto m : X_chunk_buf) {
				rte_pktmbuf_free(m);
			}

			info.state = VNF_STATE::SEND_UW_CHUNKS;
			break;

		case VNF_STATE::SEND_UW_CHUNKS:
			RTE_LOG(DEBUG, USER1, "State: Send uW chunks.\n");
			send_chunks(manager, uW_chunk_buf);

			X_chunk_buf.clear();
			X_service_hdr_buf.clear();
			uW_chunk_buf.clear();
			uW_service_hdr_buf.clear();

			info.state = VNF_STATE::FORWARD_X_CHUNKS;
			break;

		default:
			cerr << "Unknown state!" << endl;
			g_force_quit = true;
		}
	}
} // Python interpretor stops here (RAII).
} // namespace meica

int main(int argc, char *argv[])
{
	bool is_leader = false;
	string mode = "store_forward";
	uint32_t max_rounds = 4;
	string core = "1";
	uint32_t mem = 512;
	string host_name = boost::asio::ip::host_name();
	string iface = host_name + "-s" + host_name.back();
	meica::g_force_quit = false;

	try {
		po::options_description desc(
			"VNF for distributed MEICA, usage:");
		// clang-format off
		desc.add_options()
                        ("help,h", "Produce help message")
                        ("verbose,v", "Enable verbose mode.")
                        ("leader,l", "Run as the leader node.")
                        ("iface,i", po::value<string>(), "The name of the IO interface.")
                        ("mode,m", po::value<string>(), "Set VNF mode. The default is store_forward.")
                        ("max_rounds", po::value<uint32_t>(), "Set the maximal allowed computing iterations.")
                        ("core,c", po::value<string>(), "The CPU cores (split by comma) to use. For example, 0,1 will use first two CPU cores.")
                        ("mem", po::value<uint32_t>(), "Set the amount of memory to preallocate at startup.");
		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help")) {
			cout << desc << "\n";
			return 1;
		}
		if (vm.count("verbose")) {
			cout << "[MEICA] Verbose mode is enabled." << endl;
                        meica::g_verbose = true;
		}
                if (vm.count("leader")) {
                        is_leader = true;
                }
                if (vm.count("iface")) {
                        iface = vm["iface"].as<string>();
                }
                if (vm.count("mode")) {
                        mode = vm["mode"].as<string>();
                }
                if (vm.count("max_rounds")) {
                        max_rounds = vm["max_rounds"].as<uint32_t>();
                }
                if (vm.count("core")) {
                        core = vm["core"].as<string>();
                }
                if (vm.count("mem")) {
                        mem = vm["mem"].as<uint32_t>();
                }
	} catch (exception &e) {
		cerr << "Error:" << e.what() << endl;
		return 1;
	}

	if (mode == "store_forward" || mode == "compute_forward") {
		cout << "[MEICA] Current working mode: " << mode << endl;
	} else {
		cerr << "Error: Unknown mode: " << mode << endl;
		return 0;
	}
                cout << "- Iterface name: " << iface << endl;
        cout << "- Core list: " << core << "; Preallocated memory: " << mem <<endl;
        cout << "- Host name: " << host_name << endl;
	if (is_leader == true) {
		cout << "- Role: Leader node." << endl;
	}

        // Init DPDK EAL.
        string file_prefix_conf = "--file-prefix=" + host_name;
        string vdev_conf = "net_af_packet0,iface=" + iface;
        const char *rte_argv[] = {
                "-l", core.c_str(),
                "-m", to_string(mem).c_str(), "--no-huge", "--no-pci", file_prefix_conf.c_str(),
                "--vdev", vdev_conf.c_str(),
                nullptr};
        int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
	int ret;
        ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments.\n");
	}

	signal(SIGINT, meica::signal_handler);
	signal(SIGTERM, meica::signal_handler);

        if (meica::g_verbose== true) {
                rte_log_set_level(RTE_LOGTYPE_USER1, RTE_LOG_DEBUG);
        }


	struct ffpp_munf_manager munf_manager;
	struct rte_mempool *pool = NULL;


        // WARN: Temporary work around... The number of X chunks to buffer is
        // currently too large for typical DPDK applications...
        meica::fast_forward_pool = rte_pktmbuf_pool_create("fast_forward_pool", 4096,
                        256, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
                rte_socket_id());
        if (meica::fast_forward_pool== NULL)
                rte_exit(EXIT_FAILURE, "Cannot init the fast forward pool!\n");

	ffpp_munf_init_manager(&munf_manager, "test_manager", pool);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Cannot get the MAC address.\n");
	}

	if (mode == "store_forward") {
		meica::run_store_forward_loop(munf_manager);
	} else if (mode == "compute_forward") {
		meica::run_compute_forward_loop(munf_manager, is_leader, max_rounds);
	}

	cout << "Main loop ends, run cleanups..." << endl;
	ffpp_munf_cleanup_manager(&munf_manager);
	rte_eal_cleanup();

	return 0;
}
