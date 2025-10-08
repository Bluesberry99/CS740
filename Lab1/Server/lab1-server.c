/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <inttypes.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>   // 新增：TCP 头定义


#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define PORT_NUM 4

#define SERVER_PORT_BASE 5001   // 5001..映射为 flow_id 1..N
#define MAX_FLOWS        8
#define MAX_WINDOW_LEN   32     // 用 32-bit 位图
#define MSS_BYTES        1400   // 实验给 payload 长度的话，设成一致

static size_t window_len = 16;  // 窗口里最多 16 段

// === TCP 负载里的迷你头（统一用网络字节序传输）===
#pragma pack(push, 1)
typedef struct {
    uint32_t seg_idx_be;  // 段号（网络序）
    uint16_t seg_len_be;  // 本段真实负载长度（网络序，不含 mini_hdr）
    uint16_t reserved;    // 预留
} mini_hdr_t;
#pragma pack(pop)

struct rte_mempool *mbuf_pool = NULL;
static struct rte_ether_addr my_eth;
//size_t window_len = 10;

int flow_size = 10000;
int packet_len = 1000;
int ack_len = 10;
int flow_num = 1;

// === 窗口内每个槽位：是否已收 + 实际字节数 ===
typedef struct { bool have; uint16_t len; } seg_slot_t;

// === 每条 TCP 流的接收状态（按目的端口映射 flow_id）===
typedef struct {
    bool     inited;
    uint32_t client_isn;      // 首次计算得到：seq - seg_idx*MSS
    uint32_t base_seg;        // 窗口左边界的段号（最小未确认段）
    uint64_t bytes_contig;    // 从 ISN 起连续确认的“字节数”（累计 ACK 指向位置）
    seg_slot_t win[MAX_WINDOW_LEN];  // 窗口槽位
} tcp_flow_state_t;

static tcp_flow_state_t flows[MAX_FLOWS + 1];   // flow_id: 1..MAX_FLOWS

static inline int tcp_port_to_flow_id(uint16_t be_dport) {
    uint16_t port = rte_be_to_cpu_16(be_dport);
    if (port < SERVER_PORT_BASE) return 0;
    int id = (int)(port - SERVER_PORT_BASE + 1);
    if (id < 1 || id > MAX_FLOWS) return 0;
    return id;
}

// seq -> 段号（无符号减法自然回绕）
static inline uint32_t seq_to_seg_idx(uint32_t seq, uint32_t client_isn) {
    uint32_t off = (uint32_t)(seq - client_isn);
    return off / MSS_BYTES;
}

// 标记窗口并尽量“吃掉”左侧连续段，按字节推进累计 ACK
static inline void sw_mark_and_advance(tcp_flow_state_t *st,
                                       uint32_t seg_idx, uint16_t seg_len)
{
    if (seg_idx < st->base_seg) return;                       // 老段，已确认过
    uint32_t rel = seg_idx - st->base_seg;
    if (rel >= MAX_WINDOW_LEN) return;                        // 超出窗口右界，等发送端滑动

    st->win[rel].have = true;
    st->win[rel].len  = seg_len;

    // 从窗口左端开始，连续“有”的就吃掉
    while (st->win[0].have) {
        st->bytes_contig += (uint16_t)(st->win[0].len + sizeof(mini_hdr_t));                  // 累计“字节”
        // 窗口整体左移一格（直白写法，易懂；可换成环形队列优化拷贝）
        for (uint32_t i = 0; i + 1 < MAX_WINDOW_LEN; ++i)
            st->win[i] = st->win[i + 1];
        st->win[MAX_WINDOW_LEN - 1].have = false;
        st->win[MAX_WINDOW_LEN - 1].len  = 0;
        st->base_seg++;
    }
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

uint16_t
wrapsum(uint32_t sum)
{
	sum = ~sum & 0xFFFF;
	return htons(sum);
}

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

static int get_port(struct sockaddr_in *src,
                        struct sockaddr_in *dst,
                        void **payload,
                        size_t *payload_len,
                        struct rte_mbuf *pkt)
{
    // packet layout order is (from outside -> in):
    // ether_hdr
    // ipv4_hdr
    // udp_hdr
    // client timestamp
    uint8_t *p = rte_pktmbuf_mtod(pkt, uint8_t *);
    size_t header = 0;

    // check the ethernet header
    struct rte_ether_hdr * const eth_hdr = (struct rte_ether_hdr *)(p);
    p += sizeof(*eth_hdr);
    header += sizeof(*eth_hdr);
    uint16_t eth_type = ntohs(eth_hdr->ether_type);
    //struct rte_ether_addr mac_addr = {};
	//rte_eth_macaddr_get(1, &mac_addr);
    if (!rte_is_same_ether_addr(&my_eth, &eth_hdr->dst_addr) ) {
        /*printf("Bad MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
            eth_hdr->dst_addr.addr_bytes[0], eth_hdr->dst_addr.addr_bytes[1],
			eth_hdr->dst_addr.addr_bytes[2], eth_hdr->dst_addr.addr_bytes[3],
			eth_hdr->dst_addr.addr_bytes[4], eth_hdr->dst_addr.addr_bytes[5]);
		*/
        return 0;
    }
    if (RTE_ETHER_TYPE_IPV4 != eth_type) {
        printf("Bad ether type\n");
        return 0;
    }

    // check the IP header
    struct rte_ipv4_hdr *const ip_hdr = (struct rte_ipv4_hdr *)(p);
    p += sizeof(*ip_hdr);
    header += sizeof(*ip_hdr);

    // In network byte order.
    in_addr_t ipv4_src_addr = ip_hdr->src_addr;
    in_addr_t ipv4_dst_addr = ip_hdr->dst_addr;

    if (IPPROTO_TCP != ip_hdr->next_proto_id) {
        //printf("Bad next proto_id\n");
        return 0;
    }
    
    src->sin_addr.s_addr = ipv4_src_addr;
    dst->sin_addr.s_addr = ipv4_dst_addr;

    struct rte_tcp_hdr * const tcp_hdr = (struct rte_tcp_hdr *)(p);
    uint16_t tcp_hlen_bytes = (tcp_hdr->data_off >> 4) * 4;
    p += tcp_hlen_bytes;
    header += tcp_hlen_bytes;

    in_port_t tcp_src_port = tcp_hdr->src_port;   // network byte order
    in_port_t tcp_dst_port = tcp_hdr->dst_port;   // network byte order
    int ret = 0;

	

	uint16_t p1 = rte_cpu_to_be_16(5001);
	uint16_t p2 = rte_cpu_to_be_16(5002);
	uint16_t p3 = rte_cpu_to_be_16(5003);
	uint16_t p4 = rte_cpu_to_be_16(5004);
	// printf("dst port %d, %d\n", tcp_hdr->dst_port, p2);
	
	if (tcp_hdr->dst_port ==  p1)
	{
		ret = 1;
	}
	if (tcp_hdr->dst_port ==  p2)
	{
		ret = 2;
	}
	if (tcp_hdr->dst_port ==  p3)
	{
		ret = 3;
	}
	if (tcp_hdr->dst_port ==  p4)
	{
		ret = 4;
	}

    src->sin_port = tcp_src_port;
    dst->sin_port = tcp_dst_port;
    
    src->sin_family = AF_INET;
    dst->sin_family = AF_INET;
    
    *payload_len = pkt->pkt_len - header;
    *payload = (void *)p;
    
	return ret;

}

/* Basic forwarding application lcore. 8< */
static __rte_noreturn void
lcore_main(void)
{
	uint16_t port;
	uint32_t rec = 0;
	//uint16_t nb_rx;

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	RTE_ETH_FOREACH_DEV(port)
	if (rte_eth_dev_socket_id(port) >= 0 &&
		rte_eth_dev_socket_id(port) !=
			(int)rte_socket_id())
		printf("WARNING, port %u is on remote NUMA node to "
			   "polling thread.\n\tPerformance will "
			   "not be optimal.\n",
			   port);

	printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n",
		   rte_lcore_id());

	/* Main work of application loop. 8< */
	for (;;)
	{
		RTE_ETH_FOREACH_DEV(port)
		{
			/* Get burst of RX packets, from port1 */
			if (port != 1)
				continue;

			struct rte_mbuf *bufs[BURST_SIZE];
			struct rte_mbuf *pkt;
			struct rte_ether_hdr *eth_h;
			struct rte_ipv4_hdr *ip_h;
			struct rte_tcp_hdr *tcp_h;      //change to tcp header
			struct rte_ether_addr eth_addr;
			uint32_t ip_addr;
			uint8_t i;
			uint8_t nb_replies = 0;

			struct rte_mbuf *acks[BURST_SIZE];
			struct rte_mbuf *ack;
			// char *buf_ptr;
			struct rte_ether_hdr *eth_h_ack;
			struct rte_ipv4_hdr *ip_h_ack;
			struct rte_tcp_hdr *tcp_h_ack;

			const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);


			if (unlikely(nb_rx == 0))
				continue;

			for (i = 0; i < nb_rx; i++)
			{
				pkt = bufs[i];
				struct sockaddr_in src, dst;
                void *payload = NULL;
                size_t payload_length = 0;
                int udp_port_id = get_port(&src, &dst, &payload, &payload_length, pkt);
				if(udp_port_id != 0){
					//wyj
					// printf("received: %d\n", rec);
				}

				eth_h = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
				if (eth_h->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
				{
					rte_pktmbuf_free(pkt);
					continue;
				}

				ip_h = rte_pktmbuf_mtod_offset(pkt, struct rte_ipv4_hdr *,
											   sizeof(struct rte_ether_hdr));
                if (ip_h->next_proto_id != IPPROTO_TCP) {
                    rte_pktmbuf_free(pkt);
                    continue;
                }

				tcp_h = rte_pktmbuf_mtod_offset(pkt, struct rte_tcp_hdr *,
											   sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

                // 统计行可留可删
                // rte_pktmbuf_dump(stdout, pkt, pkt->pkt_len);
                rec++;
                uint16_t tcp_hlen_bytes   = (tcp_h->data_off >> 4) * 4;
                uint16_t total_len        = rte_be_to_cpu_16(ip_h->total_length);
                uint16_t tcp_payload_len  = total_len - sizeof(*ip_h) - tcp_hlen_bytes;
                uint8_t *pl               = (uint8_t*)tcp_h + tcp_hlen_bytes;  // TCP payload 起点
                

                // 不是数据段（纯ACK/控制）也可以回一个累计ACK，这里不强制
                uint32_t ack_num = rte_be_to_cpu_32(tcp_h->sent_seq); // 兜底

                int fid = tcp_port_to_flow_id(tcp_h->dst_port);
                if (fid > 0) {
                    tcp_flow_state_t *st = &flows[fid];

                    if (tcp_payload_len >= sizeof(mini_hdr_t)) {
                        mini_hdr_t *mh = (mini_hdr_t *)pl;
                        uint32_t seg_idx = ntohl(mh->seg_idx_be);
                        uint16_t seg_len = ntohs(mh->seg_len_be);

                        if (tcp_payload_len >= sizeof(mini_hdr_t) + seg_len) {
                            uint32_t seq = rte_be_to_cpu_32(tcp_h->sent_seq);
                            uint32_t inferred_isn = seq - seg_idx * MSS_BYTES;
							if (!st->inited || st->client_isn != inferred_isn) {
								st->inited       = true;
								st->client_isn   = inferred_isn;
								st->base_seg     = 0;
								st->bytes_contig = 0;
								for (int k=0; k<MAX_WINDOW_LEN; ++k) { st->win[k].have = false; st->win[k].len = 0; }
							}
							sw_mark_and_advance(st, seg_idx, seg_len);
                        }
                    }

                    if (st->inited) {
                        ack_num = st->client_isn + st->bytes_contig; // 字节级 ACK
                    }
                }

                // ========= 构造并发送 纯TCP ACK =========
                ack = rte_pktmbuf_alloc(mbuf_pool);
                if (ack == NULL) { rte_pktmbuf_free(pkt); continue; }

                uint8_t *ptr = rte_pktmbuf_mtod(ack, uint8_t *);
                size_t header_size = 0;

                // L2
                eth_h_ack = (struct rte_ether_hdr *)ptr;
                rte_ether_addr_copy(&my_eth, &eth_h_ack->src_addr);
                rte_ether_addr_copy(&eth_h->src_addr, &eth_h_ack->dst_addr);
                eth_h_ack->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
                ptr += sizeof(*eth_h_ack);
                header_size += sizeof(*eth_h_ack);

                // L3
                ip_h_ack = (struct rte_ipv4_hdr *)ptr;
                ip_h_ack->version_ihl     = 0x45;
                ip_h_ack->type_of_service = 0;
                ip_h_ack->total_length    = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr));
                ip_h_ack->packet_id       = rte_cpu_to_be_16(1);
                ip_h_ack->fragment_offset = 0;
                ip_h_ack->time_to_live    = 64;
                ip_h_ack->next_proto_id   = IPPROTO_TCP;
                ip_h_ack->src_addr        = ip_h->dst_addr;
                ip_h_ack->dst_addr        = ip_h->src_addr;
                ip_h_ack->hdr_checksum    = 0;
                ip_h_ack->hdr_checksum    = wrapsum(checksum((unsigned char *)ip_h_ack, sizeof(struct rte_ipv4_hdr), 0));
                ptr += sizeof(*ip_h_ack);
                header_size += sizeof(*ip_h_ack);

                // L4 TCP (纯ACK)
                tcp_h_ack = (struct rte_tcp_hdr *)ptr;
                ptr += sizeof(*tcp_h_ack);
                header_size += sizeof(*tcp_h_ack);

                tcp_h_ack->src_port = tcp_h->dst_port;
                tcp_h_ack->dst_port = tcp_h->src_port;
                tcp_h_ack->sent_seq = rte_cpu_to_be_32(0);                 // 我方 seq（不发数据）
                tcp_h_ack->recv_ack = rte_cpu_to_be_32(ack_num);           // 字节级累计 ACK
                tcp_h_ack->data_off = (5 << 4);                            // 20字节头
                tcp_h_ack->tcp_flags= RTE_TCP_ACK_FLAG;
                tcp_h_ack->rx_win   = rte_cpu_to_be_16((uint16_t)RTE_MIN(window_len * MSS_BYTES, 65535));
                tcp_h_ack->tcp_urp  = 0;
                tcp_h_ack->cksum    = 0;

                // TCP 校验和（依赖 IP 伪首部）
                tcp_h_ack->cksum = rte_ipv4_udptcp_cksum(ip_h_ack, (void *)tcp_h_ack);

                // mbuf 长度
                ack->l2_len  = RTE_ETHER_HDR_LEN;
                ack->l3_len  = sizeof(struct rte_ipv4_hdr);
                ack->l4_len  = sizeof(struct rte_tcp_hdr);
                ack->data_len= header_size;
                ack->pkt_len = header_size;
                ack->nb_segs = 1;

                acks[nb_replies++] = ack;

                // 释放收到的包
                rte_pktmbuf_free(pkt);

			}

			/* Send back echo replies. */
			uint16_t nb_tx = 0;
			if (nb_replies > 0)
			{
				nb_tx = rte_eth_tx_burst(port, 0, acks, nb_replies);
			}

			/* Free any unsent packets. */
			if (unlikely(nb_tx < nb_replies))
			{
				uint16_t buf;
				for (buf = nb_tx; buf < nb_replies; buf++)
					rte_pktmbuf_free(acks[buf]);
			}
		}
	}
	/* >8 End of loop. */
}
/* >8 End Basic forwarding application lcore. */

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int main(int argc, char *argv[])
{
	// struct rte_mempool *mbuf_pool;
	unsigned nb_ports = 1;
	uint16_t portid;
	
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

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
