/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
//#include <rte_udp.h>
#include <rte_ip.h>

#include <rte_common.h>
#include <rte_tcp.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// #define PKT_TX_IPV4          (1ULL << 55)
// #define PKT_TX_IP_CKSUM      (1ULL << 54)

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

/* === 实验参数（可调） === */
#define MSS_BYTES       1400                 /* 与 server 保持一致，用于 seq 对齐 */
#define MAX_WINDOW_LEN  16                   /* 发送侧窗口（段数） */

uint32_t NUM_PING = 100;

static struct rte_ether_addr dst_mac = {{0x14,0x58,0xD0,0x58,0x4F,0xC3}};  /* 按你环境改 */
/* Define the mempool globally */
struct rte_mempool *mbuf_pool = NULL;
static struct rte_ether_addr my_eth;
static size_t message_size = 1000;
static uint32_t seconds = 1;

size_t window_len = 10;

int flow_size = 10000;
int packet_len = 1392;
int flow_num = 1;


static uint64_t raw_time(void) {
    struct timespec tstart={0,0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    uint64_t t = (uint64_t)(tstart.tv_sec*1.0e9 + tstart.tv_nsec);
    return t;

}

static uint64_t time_now(uint64_t offset) {
    return raw_time() - offset;
}

uint32_t
checksum(unsigned char *buf, uint32_t nbytes, uint32_t sum)
{
	unsigned int	 i;

	/* Checksum all the pairs of bytes first. */
	for (i = 0; i < (nbytes & ~1U); i += 2) {
		sum += (uint16_t)ntohs(*((uint16_t *)(buf + i)));
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}

	if (i < nbytes) {
		sum += buf[i] << 8;
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}

	return sum;
}

uint32_t
wrapsum(uint32_t sum)
{
	sum = ~sum & 0xFFFF;
	return htons(sum);
}

static inline uint16_t wrapsum16(uint32_t sum){
    sum = ~sum & 0xFFFF;
    return htons((uint16_t)sum);
}

/* ====== 方案B：TCP负载中的 mini header ====== */
#pragma pack(push,1)
typedef struct {
    uint32_t seg_idx_be;  /* 段号（网络序） */
    uint16_t seg_len_be;  /* 本段真实数据长度（网络序，不含本小头） */
    uint16_t reserved;
} mini_hdr_t;
#pragma pack(pop)


/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */

/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0)
	{
		printf("Error during getting device (port %u) info: %s\n",
			   port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++)
	{
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
										rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++)
	{
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
										rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	retval = rte_eth_macaddr_get(port, &my_eth);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
		   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
		   port, RTE_ETHER_ADDR_BYTES(&my_eth));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}
/* >8 End of main functional part of port initialization. */
/* ====== 发送一个 TCP 段（带 mini_hdr） ====== */
static int send_segment(uint16_t port, uint16_t queue,
                        uint16_t dport,        /* 5001+fid-1 */
                        uint32_t client_isn,   /* 此流 ISN */
                        uint32_t seg_idx,      /* 段号 */
                        const uint8_t *data,   /* 指向数据（可以是NULL，表示发空数据） */
                        uint16_t seg_len)      /* 数据长度（不含mini_hdr） */
{
    /* 计算总payload：mini_hdr + seg_len */
    const uint16_t payload_len = (uint16_t)(sizeof(mini_hdr_t) + seg_len);

    struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
    if (!m) return -1;

    uint8_t *p = rte_pktmbuf_mtod(m, uint8_t *);
    size_t hdr = 0;

    /* L2 */
    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)p;
    rte_ether_addr_copy(&my_eth, &eth->src_addr);
    rte_ether_addr_copy(&dst_mac, &eth->dst_addr);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    p += sizeof(*eth); hdr += sizeof(*eth);

    /* L3 */
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)p;
    ip->version_ihl     = 0x45;
    ip->type_of_service = 0;
    ip->total_length    = rte_cpu_to_be_16(sizeof(*ip) + sizeof(struct rte_tcp_hdr) + payload_len);
    ip->packet_id       = rte_cpu_to_be_16(1);
    ip->fragment_offset = 0;
    ip->time_to_live    = 64;
    ip->next_proto_id   = IPPROTO_TCP;
    /* 这里的 IP 地址并不影响 server（它只看 L2+TCP）——可按需设置 */
    ip->src_addr        = rte_cpu_to_be_32(0x0A000001); /* 10.0.0.1 */
    ip->dst_addr        = rte_cpu_to_be_32(0x0A000002); /* 10.0.0.2 */
    ip->hdr_checksum    = 0;
    ip->hdr_checksum    = wrapsum16(checksum((unsigned char*)ip, sizeof(*ip), 0));
    p += sizeof(*ip); hdr += sizeof(*ip);

    /* L4 TCP */
    struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)p;
    tcp->src_port = rte_cpu_to_be_16(dport);     /* 源端口可等于目的端口（实验内网） */
    tcp->dst_port = rte_cpu_to_be_16(dport);     /* 5001 + (fid-1) */
    tcp->sent_seq = rte_cpu_to_be_32(client_isn + seg_idx * MSS_BYTES);
    tcp->recv_ack = rte_cpu_to_be_32(0);         /* 我们不接收来自 server 的数据 */
    tcp->data_off = (5 << 4);                    /* 20字节，无TCP选项 */
    tcp->tcp_flags= (RTE_TCP_ACK_FLAG | RTE_TCP_PSH_FLAG); /* 数据段通常带 ACK */
    tcp->rx_win   = rte_cpu_to_be_16(65535);
    tcp->tcp_urp  = 0;
    tcp->cksum    = 0;
    p += sizeof(*tcp); hdr += sizeof(*tcp);

    /* Payload: mini_hdr + data */
    mini_hdr_t mh;
    mh.seg_idx_be = htonl(seg_idx);
    mh.seg_len_be = htons(seg_len);
    mh.reserved   = 0;
    memcpy(p, &mh, sizeof(mh)); p += sizeof(mh);
    if (seg_len && data) memcpy(p, data, seg_len);

    /* 长度与校验 */
    m->l2_len  = sizeof(struct rte_ether_hdr);
    m->l3_len  = sizeof(struct rte_ipv4_hdr);
    m->l4_len  = sizeof(struct rte_tcp_hdr);
    m->data_len= hdr + payload_len;
    m->pkt_len = hdr + payload_len;
    m->nb_segs = 1;

    tcp->cksum = rte_ipv4_udptcp_cksum(ip, (void*)tcp);

    uint16_t sent = rte_eth_tx_burst(port, queue, &m, 1);
    if (sent < 1) { rte_pktmbuf_free(m); return -1; }
    return 0;
}

/* ====== 解析 server 返回的纯 TCP ACK，返回 flow_id（1..N；否则0）并给出 ack_num ====== */
static int parse_ack(struct rte_mbuf *pkt, uint32_t *ack_num_out){
    uint8_t *p = rte_pktmbuf_mtod(pkt, uint8_t *);
    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)p;
    if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) return 0;
    p += sizeof(*eth);

    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)p;
    if (ip->next_proto_id != IPPROTO_TCP) return 0;
    p += sizeof(*ip);

    struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)p;
    /* server 回 ACK：src_port=我们之前的 dst_port(5001+fid-1) */
    uint16_t sport = rte_be_to_cpu_16(tcp->src_port);

    if (ack_num_out) *ack_num_out = rte_be_to_cpu_32(tcp->recv_ack);

    if (sport >= 5001 && sport < 5001 + 64) {
        return (int)(sport - 5001 + 1);
    }
    return 0;
}

/* >8 End Basic forwarding application lcore. */

static __rte_noreturn void
lcore_main()
{
    const uint16_t PORT = 1, QUEUE = 0;

    /* 每段数据长度（不含 mini_hdr），受 MSS 限制 */
    const uint16_t DATA_PER_SEG = (uint16_t)RTE_MIN(packet_len, MSS_BYTES - (int)sizeof(mini_hdr_t));

    /* 每条流的状态 */
    uint32_t total_bytes[64]={0}, total_segs[64]={0};
    uint32_t next_seg[64]={0}, base_seg[64]={0}, in_flight[64]={0}, client_isn[64]={0};

    for (int fid=1; fid<=flow_num; ++fid){
        total_bytes[fid] = (uint32_t)flow_size;
        total_segs[fid]  = (total_bytes[fid] + DATA_PER_SEG - 1) / DATA_PER_SEG;
        next_seg[fid]    = 0;
        base_seg[fid]    = 0;
        in_flight[fid]   = 0;
        client_isn[fid]  = (uint32_t)rand();  /* 与 server 的反推公式匹配 */
    }

    int active_flows = flow_num;

    while (active_flows > 0) {
        /* 逐流尽量填满窗口 */
        for (int fid=1; fid<=flow_num; ++fid){
            if (base_seg[fid] >= total_segs[fid]) continue;
            while (in_flight[fid] < MAX_WINDOW_LEN && next_seg[fid] < total_segs[fid]) {
                uint32_t seg_idx = next_seg[fid];
                uint16_t seg_len = DATA_PER_SEG;
                if (seg_idx == total_segs[fid]-1) {
                    uint32_t sent_bytes = seg_idx * (uint32_t)DATA_PER_SEG;
                    uint32_t remain = total_bytes[fid] - sent_bytes;
                    if (remain < seg_len) seg_len = (uint16_t)remain;
                }
                if (send_segment(PORT, QUEUE, (uint16_t)(5000 + fid),
                                 client_isn[fid], seg_idx,
                                 NULL /*无实际数据*/, seg_len) == 0) {
                    next_seg[fid]++; in_flight[fid]++;
                } else break;
            }
        }

        /* 收 ACK，推进窗口 */
        struct rte_mbuf *rx[BURST_SIZE];
        uint16_t nb_rx = rte_eth_rx_burst(PORT, QUEUE, rx, BURST_SIZE);
        if (nb_rx == 0) continue;

        for (uint16_t i=0;i<nb_rx;i++){
            uint32_t ack_num = 0;
            int fid = parse_ack(rx[i], &ack_num);
            if (fid>0 && fid<=flow_num) {
                uint32_t ack_bytes = (uint32_t)(ack_num - client_isn[fid]);
                uint32_t ack_segs  = ack_bytes / MSS_BYTES;   /* 向下取整到段边界 */
                if (ack_segs > base_seg[fid]) {
                    uint32_t newly = ack_segs - base_seg[fid];
                    base_seg[fid] = ack_segs;
                    in_flight[fid] = (newly > in_flight[fid]) ? 0 : (in_flight[fid] - newly);

                    if (base_seg[fid] >= total_segs[fid]) {
                        active_flows--;
                        printf("[flow %d] done: bytes=%u segs=%u\n",
                               fid, total_bytes[fid], total_segs[fid]);
                    }
                }
            }
            rte_pktmbuf_free(rx[i]);
        }
    }

    printf("All flows done.\n");
}
/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */

int main(int argc, char *argv[])
{

	unsigned nb_ports;
	uint16_t portid;

    if (argc == 3) {
        flow_num = (int) atoi(argv[1]);
        flow_size =  (int) atoi(argv[2]);
    } else {
        printf( "usage: ./lab1-client <flow_num> <flow_size>\n");
        return 1;
    }

    NUM_PING = flow_size / packet_len;

	/* Initializion the Environment Abstraction Layer (EAL). 8< */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initialization the Environment Abstraction Layer (EAL). */

	argc -= ret;
	argv += ret;

    nb_ports = rte_eth_dev_count_avail();
	/* Allocates mempool to hold the mbufs. 8< */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
										MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing all ports. 8< */
	RTE_ETH_FOREACH_DEV(portid)
	if (portid == 1 && port_init(portid, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
				 portid);
	/* >8 End of initializing all ports. */

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

	/* Call lcore_main on the main core only. Called on single lcore. 8< */
	lcore_main();
	/* >8 End of called on single lcore. */
    printf("Done!\n");
	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
