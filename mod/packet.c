#include "nat64/mod/packet.h"
#include "nat64/comm/constants.h"
#include "nat64/comm/types.h"
#include "nat64/comm/config_proto.h"
#include "nat64/mod/ipv6_hdr_iterator.h"
#include "nat64/mod/packet_db.h"

//#include <linux/list.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmpv6.h>
#include <linux/icmp.h>
#include <net/ip.h>
#include <net/ipv6.h>


#define MIN_IPV6_HDR_LEN sizeof(struct ipv6hdr)
#define MIN_IPV4_HDR_LEN sizeof(struct iphdr)
#define MIN_TCP_HDR_LEN sizeof(struct tcphdr)
#define MIN_UDP_HDR_LEN sizeof(struct udphdr)
#define MIN_ICMP6_HDR_LEN sizeof(struct icmp6hdr)
#define MIN_ICMP4_HDR_LEN sizeof(struct icmphdr)


static struct packet_config config;
static DEFINE_SPINLOCK(config_lock);


int pkt_init(void)
{
	config.fragment_timeout = FRAGMENT_MIN;
	return 0;
}

void pkt_destroy(void)
{
	/* No code. */
}

__u16 is_more_fragments_set_ipv6(struct frag_hdr *hdr)
{
	__u16 frag_off = be16_to_cpu(hdr->frag_off);
	return (frag_off & 0x1);
}

/**
 * Returns 1 if the More Fragments flag from the "header" header is set, 0 otherwise.
 */
__u16 is_more_fragments_set_ipv4(struct iphdr *hdr)
{
	__u16 frag_off = be16_to_cpu(hdr->frag_off);
	return (frag_off & IP_MF) >> 13;
}

__u16 get_fragment_offset_ipv6(struct frag_hdr *hdr)
{
	__u16 frag_off = be16_to_cpu(hdr->frag_off);
	return frag_off >> 3;
}

__u16 get_fragment_offset_ipv4(struct iphdr *hdr)
{
	__u16 frag_off = be16_to_cpu(hdr->frag_off);
	return frag_off & 0x1FFF;
}

__be16 build_ipv6_frag_off_field(__u16 fragment_offset, __u16 more_fragments)
{
	__u16 result = (fragment_offset << 3)
			| (more_fragments << 0);

	return cpu_to_be16(result);
}

/**
 * One-liner for creating the IPv4 header's Fragment Offset field.
 * TODO shouldn't those be booleans?
 */
__be16 build_ipv4_frag_off_field(__u16 dont_fragment, __u16 more_fragments, __u16 fragment_offset)
{
	__u16 result = (dont_fragment << 14)
			| (more_fragments << 13)
			| (fragment_offset << 0);

	return cpu_to_be16(result);
}


void frag_init(struct fragment *frag)
{
	memset(frag, 0, sizeof(*frag));
	INIT_LIST_HEAD(&frag->next);
}

struct ipv6hdr *frag_get_ipv6_hdr(struct fragment *frag)
{
	return frag->l3_hdr.ptr;
}

struct frag_hdr *frag_get_fragment_hdr(struct fragment *frag)
{
	return get_extension_header(frag_get_ipv6_hdr(frag), NEXTHDR_FRAGMENT);
}

struct iphdr *frag_get_ipv4_hdr(struct fragment *frag)
{
	return frag->l3_hdr.ptr;
}

struct tcphdr *frag_get_tcp_hdr(struct fragment *frag)
{
	return frag->l4_hdr.ptr;
}

struct udphdr *frag_get_udp_hdr(struct fragment *frag)
{
	return frag->l4_hdr.ptr;
}

struct icmp6hdr *frag_get_icmp6_hdr(struct fragment *frag)
{
	return frag->l4_hdr.ptr;
}

struct icmphdr *frag_get_icmp4_hdr(struct fragment *frag)
{
	return frag->l4_hdr.ptr;
}

unsigned char *frag_get_payload(struct fragment *frag)
{
	return frag->payload.ptr;
}

enum verdict frag_create_ipv6(struct sk_buff *skb, struct fragment **frag_out)
{
	struct fragment *frag;
	struct ipv6hdr *ipv6_header;
	struct frag_hdr *frag_header;
	struct hdr_iterator iterator;

	frag = kmalloc(sizeof(*frag), GFP_ATOMIC);
	if (!frag) {
		log_warning("Cannot allocate a fragment structure.");
		return VER_DROP;
	}

	// Layer 3
	ipv6_header = ipv6_hdr(skb);
	hdr_iterator_init(&iterator, ipv6_header);
	hdr_iterator_last(&iterator);

	frag->l3_hdr.proto = L3PROTO_IPV6;
	frag->l3_hdr.len = iterator.data - (void *) ipv6_header;
	frag->l3_hdr.ptr = ipv6_header;
	frag->l3_hdr.ptr_needs_kfree = false;

	// Layer 4
	frag_header = get_extension_header(ipv6_header, NEXTHDR_FRAGMENT);
	if (frag_header == NULL || be16_to_cpu(frag_header->frag_off) == 0) {
		switch (iterator.hdr_type) {
		case NEXTHDR_TCP:
			frag->l4_hdr.proto = L4PROTO_TCP;
			frag->l4_hdr.len = tcp_hdrlen(skb);
			break;

		case NEXTHDR_UDP:
			frag->l4_hdr.proto = L4PROTO_UDP;
			frag->l4_hdr.len = sizeof(struct udphdr);
			break;

		case NEXTHDR_ICMP:
			frag->l4_hdr.proto = L4PROTO_ICMP;
			frag->l4_hdr.len = sizeof(struct icmp6hdr);
			break;

		default:
			log_warning("Unsupported layer 4 protocol: %d", iterator.hdr_type);
			kfree(frag);
			return VER_DROP;
		}

	} else {
		frag->l4_hdr.proto = L4PROTO_NONE;
		frag->l4_hdr.len = 0;
	}

	frag->l4_hdr.ptr = iterator.data;
	frag->l4_hdr.ptr_needs_kfree = false;

	// Payload TODO
	frag->payload.len = skb->len - frag->l3_hdr.len - frag->l4_hdr.len;
	frag->payload.ptr = frag->l4_hdr.ptr + frag->l4_hdr.len;
	frag->payload.ptr_needs_kfree = false;

	// List
	INIT_LIST_HEAD(&frag->next);

	*frag_out = frag;
	return VER_CONTINUE;
}

enum verdict frag_create_ipv4(struct sk_buff *skb, struct fragment **frag_out)
{
	struct fragment *frag;
	struct iphdr *ipv4_header;
	u16 fragment_offset;

	frag = kmalloc(sizeof(*frag), GFP_ATOMIC);
	if (!frag) {
		log_warning("Cannot allocate a fragment structure.");
		return VER_DROP;
	}

	frag->skb = skb;

	// Layer 3
	ipv4_header = ip_hdr(skb);

	frag->l3_hdr.proto = L3PROTO_IPV4;
	frag->l3_hdr.len = ipv4_header->ihl << 2;
	frag->l3_hdr.ptr = ipv4_header;
	frag->l3_hdr.ptr_needs_kfree = false;

	// Layer 4, Payload
	fragment_offset = be16_to_cpu(ipv4_header->frag_off) & 0x1FFF;
	if (fragment_offset == 0) {
		switch (ipv4_header->protocol) {
		case IPPROTO_TCP:
			frag->l4_hdr.proto = L4PROTO_TCP;
			frag->l4_hdr.len = tcp_hdrlen(skb);
			break;

		case IPPROTO_UDP:
			frag->l4_hdr.proto = L4PROTO_UDP;
			frag->l4_hdr.len = sizeof(struct udphdr);
			break;

		case IPPROTO_ICMP:
			frag->l4_hdr.proto = L4PROTO_ICMP;
			frag->l4_hdr.len = sizeof(struct icmphdr);
			break;

		default:
			log_warning("Unsupported layer 4 protocol: %d", ipv4_header->protocol);
			kfree(frag);
			return VER_DROP;
		}
		frag->l4_hdr.ptr = frag->l3_hdr.ptr + frag->l3_hdr.len;
		frag->payload.ptr = frag->l4_hdr.ptr + frag->l4_hdr.len;

	} else {
		frag->l4_hdr.proto = L4PROTO_NONE;
		frag->l4_hdr.len = 0;
		frag->l4_hdr.ptr = NULL;
		frag->payload.ptr = frag->l3_hdr.ptr + frag->l3_hdr.len;
	}

	frag->l4_hdr.ptr_needs_kfree = false;
	frag->payload.len = skb->len - frag->l3_hdr.len - frag->l4_hdr.len;
	frag->payload.ptr_needs_kfree = false;

	// List
	INIT_LIST_HEAD(&frag->next);

	*frag_out = frag;
	return VER_CONTINUE;
}

/**
 * Joins frag.l3_hdr, frag.l4_hdr and frag.payload into a single packet, placing the result in
 * frag.skb.
 *
 * Assumes that frag.skb is NULL (Hence, frag->*.ptr_belongs_to_skb are false).
 */
enum verdict frag_create_skb(struct fragment *frag)
{
	struct sk_buff *new_skb;
	__u16 head_room = 0, tail_room = 0;
	bool has_l4_hdr;

//	TODO
//	spin_lock_bh(&config_lock);
//	head_room = config.skb_head_room;
//	tail_room = config.skb_tail_room;
//	spin_unlock_bh(&config_lock);

	new_skb = alloc_skb(head_room /* user's reserved. */
			+ LL_MAX_HEADER /* kernel's reserved + layer 2. */
			+ frag->l3_hdr.len /* layer 3. */
			+ frag->l4_hdr.len /* layer 4. */
			+ frag->payload.len /* packet data. */
			+ tail_room, /* user's reserved+. */
			GFP_ATOMIC);
	if (!new_skb) {
		log_err(ERR_ALLOC_FAILED, "New packet allocation failed.");
		return VER_DROP;
	}
	frag->skb = new_skb;

	skb_reserve(new_skb, head_room + LL_MAX_HEADER);
	skb_put(new_skb, frag->l3_hdr.len + frag->l4_hdr.len + frag->payload.len);

	skb_reset_mac_header(new_skb);
	skb_reset_network_header(new_skb);
	skb_set_transport_header(new_skb, frag->l3_hdr.len);

//log_debug("payload[6] = %d", ((unsigned char *)frag->payload.ptr)[6]);
//log_debug("PAYLOAD LENGTH: %d", frag->payload.len);

	has_l4_hdr = (frag->l4_hdr.ptr != NULL);

	memcpy(skb_network_header(new_skb), frag->l3_hdr.ptr, frag->l3_hdr.len);
	if (has_l4_hdr) {
		memcpy(skb_transport_header(new_skb), frag->l4_hdr.ptr, frag->l4_hdr.len);
		memcpy(skb_transport_header(new_skb) + frag->l4_hdr.len, frag->payload.ptr, frag->payload.len);
	} else {
		memcpy(skb_transport_header(new_skb), frag->payload.ptr, frag->payload.len);
	}

	if (frag->l3_hdr.ptr_needs_kfree)
		kfree(frag->l3_hdr.ptr);
	if (frag->l4_hdr.ptr_needs_kfree)
		kfree(frag->l4_hdr.ptr);
	if (frag->payload.ptr_needs_kfree)
		kfree(frag->payload.ptr);

//log_debug("bools: %d %d %d", frag->l3_hdr.ptr_needs_kfree, frag->l4_hdr.ptr_needs_kfree, frag->payload.ptr_needs_kfree);

	frag->l3_hdr.ptr = skb_network_header(new_skb);
	if (has_l4_hdr) {
		frag->l4_hdr.ptr = skb_transport_header(new_skb);
		frag->payload.ptr = skb_transport_header(new_skb) + frag->l4_hdr.len;
	} else {
		frag->l4_hdr.ptr = NULL;
		frag->payload.ptr = skb_transport_header(new_skb);
	}

	frag->l3_hdr.ptr_needs_kfree = false;
	frag->l4_hdr.ptr_needs_kfree = false;
	frag->payload.ptr_needs_kfree = false;

	switch (frag->l3_hdr.proto) {
	case L3PROTO_IPV4:
		new_skb->protocol = htons(ETH_P_IP);
		break;
	case L3PROTO_IPV6:
		new_skb->protocol = htons(ETH_P_IPV6);
		break;
	default:
		log_err(ERR_L3PROTO, "Invalid protocol type: %u", frag->l3_hdr.proto);
		return VER_DROP;
	}

	return VER_CONTINUE;
}

void frag_kfree(struct fragment *frag)
{
	if (frag->skb)
		kfree_skb(frag->skb);
	if (frag->l3_hdr.ptr_needs_kfree)
		kfree(frag->l3_hdr.ptr);
	if (frag->l4_hdr.ptr_needs_kfree)
		kfree(frag->l4_hdr.ptr);
	if (frag->payload.ptr_needs_kfree)
		kfree(frag->payload.ptr);

	list_del(&frag->next);

	kfree(frag);
}

static char *nexthdr_to_string(u8 nexthdr)
{
	switch (nexthdr) {
	case NEXTHDR_TCP:
		return "TCP";
	case NEXTHDR_UDP:
		return "UDP";
	case NEXTHDR_ICMP:
		return "ICMP";
	case NEXTHDR_FRAGMENT:
		return "Fragment";
	}

	return "Don't know";
}

static char *protocol_to_string(u8 protocol)
{
	switch (protocol) {
	case IPPROTO_TCP:
		return "TCP";
	case IPPROTO_UDP:
		return "UDP";
	case IPPROTO_ICMP:
		return "ICMP";
	}

	return "Don't know";
}

void frag_print(struct fragment *frag)
{
	struct ipv6hdr *hdr6;
	struct frag_hdr *frag_header;
	struct iphdr *hdr4;
	struct tcphdr *tcp_header;
	struct udphdr *udp_header;
	struct in_addr addr4;
	u16 frag_off;

	if (!frag) {
		log_info("(null)");
		return;
	}

	log_info("Layer 3 - proto:%s length:%u kfree:%d", l3proto_to_string(frag->l3_hdr.proto),
			frag->l3_hdr.len, frag->l3_hdr.ptr_needs_kfree);
	switch (frag->l3_hdr.proto) {
	case L3PROTO_IPV6:
		hdr6 = frag_get_ipv6_hdr(frag);
		log_info("		version: %u", hdr6->version);
		log_info("		traffic class: %u", (hdr6->priority << 4) | (hdr6->flow_lbl[0] >> 4));
		log_info("		flow label: %u", ((hdr6->flow_lbl[0] & 0xf) << 16) | (hdr6->flow_lbl[1] << 8) | hdr6->flow_lbl[0]);
		log_info("		payload length: %u", be16_to_cpu(hdr6->payload_len));
		log_info("		next header: %s", nexthdr_to_string(hdr6->nexthdr));
		log_info("		hop limit: %u", hdr6->hop_limit);
		log_info("		source address: %pI6c", &hdr6->saddr);
		log_info("		destination address: %pI6c", &hdr6->daddr);

		if (hdr6->nexthdr == NEXTHDR_FRAGMENT) {
			frag_header = (struct frag_hdr *) (hdr6 + 1);
			frag_off = be16_to_cpu(frag_header->frag_off);
			log_info("Fragment header:");
			log_info("		next header: %s", nexthdr_to_string(frag_header->nexthdr));
			log_info("		reserved: %u", frag_header->reserved);
			log_info("		fragment offset: %u", frag_off >> 3);
			log_info("		more fragments: %u", frag_off & 0x1);
			log_info("		identification: %u", be32_to_cpu(frag_header->identification));
		}
		break;

	case L3PROTO_IPV4:
		hdr4 = frag_get_ipv4_hdr(frag);
		frag_off = be16_to_cpu(hdr4->frag_off);
		log_info("		version: %u", hdr4->version);
		log_info("		header length: %u", hdr4->ihl);
		log_info("		type of service: %u", hdr4->tos);
		log_info("		total length: %u", be16_to_cpu(hdr4->tot_len));
		log_info("		identification: %u", be16_to_cpu(hdr4->id));
		log_info("		more fragments: %u", (frag_off & IP_MF) >> 13);
		log_info("		don't fragment: %u", (frag_off & IP_DF) >> 14);
		log_info("		fragment offset: %u", frag_off & 0x1fff);
		log_info("		time to live: %u", hdr4->ttl);
		log_info("		protocol: %s", protocol_to_string(hdr4->protocol));
		log_info("		checksum: %u", hdr4->check);
		addr4.s_addr = hdr4->saddr;
		log_info("		source address: %pI4", &addr4);
		addr4.s_addr = hdr4->daddr;
		log_info("		destination address: %pI4", &addr4);
		break;
	}

	log_info("Layer 4 - proto:%s length:%u kfree:%d", l4proto_to_string(frag->l4_hdr.proto),
			frag->l4_hdr.len, frag->l4_hdr.ptr_needs_kfree);
	switch (frag->l4_hdr.proto) {
	case L4PROTO_TCP:
		tcp_header = frag_get_tcp_hdr(frag);
		log_info("		source port: %u", be16_to_cpu(tcp_header->source));
		log_info("		destination port: %u", be16_to_cpu(tcp_header->dest));
		log_info("		seq: %u", be32_to_cpu(tcp_header->seq));
		log_info("		ack_seq: %u", be32_to_cpu(tcp_header->ack_seq));
		log_info("		doff:%u res1:%u cwr:%u ece:%u urg:%u", tcp_header->doff, tcp_header->res1,
				tcp_header->cwr, tcp_header->ece, tcp_header->urg);
		log_info("		ack:%u psh:%u rst:%u syn:%u fin:%u", tcp_header->ack, tcp_header->psh,
				tcp_header->rst, tcp_header->syn, tcp_header->fin);
		log_info("		window: %u", be16_to_cpu(tcp_header->window));
		log_info("		check: %u", tcp_header->check);
		log_info("		urg_ptr: %u", be16_to_cpu(tcp_header->urg_ptr));
		break;

	case L4PROTO_UDP:
		udp_header = frag_get_udp_hdr(frag);
		log_info("		source port: %u", be16_to_cpu(udp_header->source));
		log_info("		destination port: %u", be16_to_cpu(udp_header->dest));
		log_info("		length: %u", be16_to_cpu(udp_header->len));
		log_info("		checksum: %u", udp_header->check);
		break;

	case L4PROTO_ICMP:
		/* too lazy */
	case L4PROTO_NONE:
		break;
	}

	log_info("Payload - length:%u kfree:%d", frag->payload.len, frag->payload.ptr_needs_kfree);
}

unsigned int pkt_get_fragment_timeout(void)
{
	unsigned int result;

	spin_lock_bh(&config_lock);
	result = config.fragment_timeout;
	spin_unlock_bh(&config_lock);

	return result;
}

struct packet *pkt_create_ipv6(struct fragment *frag)
{
	struct packet *pkt;
	struct ipv6hdr *hdr6 = frag_get_ipv6_hdr(frag);
	struct frag_hdr *hdr_frag = frag_get_fragment_hdr(frag);

	pkt = kmalloc(sizeof(*pkt), GFP_ATOMIC);
	if (!pkt) {
		log_err(ERR_ALLOC_FAILED, "Could not allocate a packet.");
		return NULL;
	}

	INIT_LIST_HEAD(&pkt->fragments);
	pkt->total_bytes = 0;
	pkt->current_bytes = 0;
	pkt->fragment_id = be32_to_cpu(hdr_frag->identification);
	pkt->dying_time = 0;
	pkt->proto = frag->l3_hdr.proto;
	pkt->addr.ipv6.src = hdr6->saddr;
	pkt->addr.ipv6.dst = hdr6->daddr;
	INIT_LIST_HEAD(&pkt->pkt_list_node);

	pkt_add_frag_ipv6(pkt, frag);

	return pkt;
}

struct packet *pkt_create_ipv4(struct fragment *frag)
{
	struct packet *pkt;
	struct iphdr *hdr4 = frag_get_ipv4_hdr(frag);

	pkt = kmalloc(sizeof(*pkt), GFP_ATOMIC);
	if (!pkt) {
		log_err(ERR_ALLOC_FAILED, "Could not allocate a packet.");
		return NULL;
	}

	INIT_LIST_HEAD(&pkt->fragments);
	pkt->total_bytes = 0;
	pkt->current_bytes = 0;
	pkt->fragment_id = be16_to_cpu(hdr4->id);
	pkt->dying_time = 0;
	pkt->proto = frag->l3_hdr.proto;
	pkt->addr.ipv4.src.s_addr = hdr4->saddr;
	pkt->addr.ipv4.dst.s_addr = hdr4->daddr;
	INIT_LIST_HEAD(&pkt->pkt_list_node);

	pkt_add_frag_ipv4(pkt, frag);

	return pkt;
}

void pkt_add_frag_ipv6(struct packet *pkt, struct fragment *frag)
{
	struct frag_hdr *hdr_frag = frag_get_fragment_hdr(frag);

	list_add(&frag->next, pkt->fragments.prev);

	if (!is_more_fragments_set_ipv6(hdr_frag))
		pkt->total_bytes = get_fragment_offset_ipv6(hdr_frag) + frag->l4_hdr.len + frag->payload.len;
	pkt->current_bytes += frag->l4_hdr.len + frag->payload.len;
	pkt->dying_time = jiffies_to_msecs(jiffies) + pkt_get_fragment_timeout();
}

void pkt_add_frag_ipv4(struct packet *pkt, struct fragment *frag)
{
	struct iphdr *hdr4 = frag_get_ipv4_hdr(frag);

	list_add(&frag->next, pkt->fragments.prev);

	if (!is_more_fragments_set_ipv4(hdr4))
		pkt->total_bytes = get_fragment_offset_ipv4(hdr4) + frag->l4_hdr.len + frag->payload.len;
	pkt->current_bytes += frag->l4_hdr.len + frag->payload.len;
	pkt->dying_time = jiffies_to_msecs(jiffies) + pkt_get_fragment_timeout();
}

/* TODO si current_bytes > total_bytes, hay que MATAR A pkt INMEDIATAMENTE!!! */
bool pkt_is_complete(struct packet *pkt)
{
	return (pkt->total_bytes != 0) && (pkt->total_bytes == pkt->current_bytes);
}

void pkt_kfree(struct packet *pkt)
{
	if (!pkt)
		return;

	while (!list_empty(&pkt->fragments)) {
		/* pkt->fragment.next is the first element of the list. */
		struct fragment *frag = list_entry(pkt->fragments.next, struct fragment, next);
		frag_kfree(frag);
	}

	kfree(pkt);
}





/*
 * -- Old --
 */






static enum verdict validate_lengths_tcp(struct sk_buff *skb, u16 l3_hdr_len)
{
	if (skb->len < l3_hdr_len + MIN_TCP_HDR_LEN) {
		log_debug("Packet is too small to contain a basic TCP header.");
		return VER_DROP;
	}

	if (skb->len < l3_hdr_len + tcp_hdrlen(skb)) {
		log_debug("Packet is too small to contain a TCP header.");
		return VER_DROP;
	}

	return VER_CONTINUE;
}

static enum verdict validate_lengths_udp(struct sk_buff *skb, u16 l3_hdr_len)
{
	u16 datagram_len;

	if (skb->len < l3_hdr_len + MIN_UDP_HDR_LEN) {
		log_debug("Packet is too small to contain a UDP header.");
		return VER_DROP;
	}

	datagram_len = be16_to_cpu(udp_hdr(skb)->len);
	if (skb->len != l3_hdr_len + datagram_len) {
		log_debug("The network header's length is not consistent with the UDP header's length.");
		return VER_DROP;
	}

	return VER_CONTINUE;
}

static enum verdict validate_lengths_icmp6(struct sk_buff *skb, u16 l3_hdr_len)
{
	if (skb->len < l3_hdr_len + MIN_ICMP6_HDR_LEN) {
		log_debug("Packet is too small to contain a ICMPv6 header.");
		return VER_DROP;
	}

	return VER_CONTINUE;
}

static enum verdict validate_lengths_icmp4(struct sk_buff *skb, u16 l3_hdr_len)
{
	if (skb->len < l3_hdr_len + MIN_ICMP4_HDR_LEN) {
		log_debug("Packet is too small to contain a ICMP header.");
		return VER_DROP;
	}

	return VER_CONTINUE;
}

static enum verdict validate_csum_ipv6(__sum16 *pkt_csum, struct sk_buff *skb,
		unsigned int datagram_len, int l4_proto)
{
	struct ipv6hdr *ip6_hdr = ipv6_hdr(skb);
	__sum16 tmp;
	__sum16 computed_csum;

	tmp = *pkt_csum;
	*pkt_csum = 0;
	computed_csum = csum_ipv6_magic(&ip6_hdr->saddr, &ip6_hdr->daddr, datagram_len, l4_proto,
			csum_partial(skb_transport_header(skb), datagram_len, 0));
	*pkt_csum = tmp;

	if (tmp != computed_csum) {
		log_warning("Checksum doesn't match (protocol: %d). Expected: %x, actual: %x.", l4_proto,
				computed_csum, tmp);
		return VER_DROP;
	}

	return VER_CONTINUE;
}

static enum verdict validate_csum_tcp6(struct sk_buff *skb, int datagram_len)
{
	struct tcphdr *hdr = tcp_hdr(skb);
	return validate_csum_ipv6(&hdr->check, skb, datagram_len, IPPROTO_TCP);
}

static enum verdict validate_csum_udp6(struct sk_buff *skb, int datagram_len)
{
	struct udphdr *hdr = udp_hdr(skb);
	return validate_csum_ipv6(&hdr->check, skb, datagram_len, IPPROTO_UDP);
}

static enum verdict validate_csum_icmp6(struct sk_buff *skb, int datagram_len)
{
	struct icmp6hdr *hdr = icmp6_hdr(skb);
	return validate_csum_ipv6(&hdr->icmp6_cksum, skb, datagram_len, IPPROTO_ICMPV6);
}

static enum verdict validate_csum_tcp4(struct sk_buff *skb, int datagram_len)
{
	struct tcphdr *hdr = tcp_hdr(skb);
	__sum16 tmp;
	__sum16 computed_csum;

	tmp = hdr->check;
	hdr->check = 0;
	computed_csum = csum_tcpudp_magic(ip_hdr(skb)->saddr, ip_hdr(skb)->daddr, datagram_len,
			IPPROTO_TCP, csum_partial(skb_transport_header(skb), datagram_len, 0));
	hdr->check = tmp;

	if (tmp != computed_csum) {
		log_warning("Checksum doesn't match (TCP). Expected: %x, actual: %x.", computed_csum, tmp);
		return VER_DROP;
	}

	return VER_CONTINUE;

}

static enum verdict validate_csum_udp4(struct sk_buff *skb, int datagram_len)
{
	struct udphdr *hdr = udp_hdr(skb);
	__sum16 tmp;
	__sum16 computed_csum;

	if (hdr->check == 0)
		return VER_CONTINUE;

	tmp = hdr->check;
	hdr->check = 0;
	computed_csum = csum_tcpudp_magic(ip_hdr(skb)->saddr, ip_hdr(skb)->daddr, datagram_len,
			IPPROTO_UDP, csum_partial(skb_transport_header(skb), datagram_len, 0));
	hdr->check = tmp;

	if (computed_csum == 0)
		computed_csum = 0xFFFF;

	if (tmp != computed_csum) {
		log_warning("Checksum doesn't match (UDP). Expected: %x, actual: %x.", computed_csum, tmp);
		return VER_DROP;
	}

	return VER_CONTINUE;
}

static enum verdict validate_csum_icmp4(struct sk_buff *skb, int datagram_len)
{
	struct icmphdr *hdr = icmp_hdr(skb);
	__sum16 tmp;
	__sum16 computed_csum;

	tmp = hdr->checksum;
	hdr->checksum = 0;
	computed_csum = ip_compute_csum(hdr, datagram_len);
	hdr->checksum = tmp;

	if (tmp != computed_csum) {
		log_warning("Checksum doesn't match (ICMPv4). Expected: %x, actual: %x.",
				computed_csum, tmp);
		return VER_DROP;
	}

	return VER_CONTINUE;
}

enum verdict validate_skb_ipv6(struct sk_buff *skb)
{
	struct ipv6hdr *ip6_hdr = ipv6_hdr(skb);
	u16 ip6_hdr_len; /* Includes extension headers. */
	u16 datagram_len;
	enum verdict result;

	struct hdr_iterator iterator = HDR_ITERATOR_INIT(ip6_hdr);
	enum hdr_iterator_result iterator_result;

	/*
	if (skb->len < MIN_IPV6_HDR_LEN) {
		log_debug("Packet is too small to contain a basic IPv6 header.");
		return VER_DROP;
	}
	*/
	if (skb->len != MIN_IPV6_HDR_LEN + be16_to_cpu(ip6_hdr->payload_len)) {
		log_debug("The socket buffer's length does not match the IPv6 header's payload lengh field.");
		return VER_DROP;
	}

	iterator_result = hdr_iterator_last(&iterator);
	switch (iterator_result) {
	case HDR_ITERATOR_SUCCESS:
		log_crit(ERR_INVALID_ITERATOR, "Iterator reports there are headers beyond the payload.");
		return VER_DROP;
	case HDR_ITERATOR_END:
		/* Success. */
		break;
	case HDR_ITERATOR_UNSUPPORTED:
		/* RFC 6146 section 5.1. */
		log_info("Packet contains an Authentication or ESP header, which I do not support.");
		return VER_DROP;
	case HDR_ITERATOR_OVERFLOW:
		log_warning("IPv6 extension header analysis ran past the end of the packet. "
				"Packet seems corrupted; ignoring.");
		return VER_DROP;
	default:
		log_crit(ERR_INVALID_ITERATOR, "Unknown header iterator result code: %d.", iterator_result);
		return VER_DROP;
	}

	/* IPv6 header length = transport header offset - IPv6 header offset. */
	ip6_hdr_len = iterator.data - (void *) ip6_hdr;
	datagram_len = skb->len - ip6_hdr_len;

	/*
	 * Set the skb's transport header pointer.
	 * It's yet to be set because the packet hasn't reached the kernel's transport layer.
	 * And despite that, its availability through the rest of the module will be appreciated.
	 */
	skb_set_transport_header(skb, ip6_hdr_len);

	switch (iterator.hdr_type) {
	case NEXTHDR_TCP:
		result = validate_lengths_tcp(skb, ip6_hdr_len);
		if (result != VER_CONTINUE)
			return result;
		result = validate_csum_tcp6(skb, datagram_len);
		if (result != VER_CONTINUE)
			return result;
		break;

	case NEXTHDR_UDP:
		result = validate_lengths_udp(skb, ip6_hdr_len);
		if (result != VER_CONTINUE)
			return result;
		result = validate_csum_udp6(skb, datagram_len);
		if (result != VER_CONTINUE)
			return result;
		break;

	case NEXTHDR_ICMP:
		result = validate_lengths_icmp6(skb, ip6_hdr_len);
		if (result != VER_CONTINUE)
			return result;
		result = validate_csum_icmp6(skb, datagram_len);
		if (result != VER_CONTINUE)
			return result;
		break;

	default:
		log_debug("Packet does not use TCP, UDP or ICMPv6.");
		return VER_DROP;
	}

	return result;
}

enum verdict validate_skb_ipv4(struct sk_buff *skb)
{
	struct iphdr *ip4_hdr = ip_hdr(skb);
	u16 ip4_hdr_len;
	u16 datagram_len;
	enum verdict result;

	/*
	if (skb->len < MIN_IPV4_HDR_LEN) {
		log_debug("Packet is too small to contain a basic IP header.");
		return VER_DROP;
	}
	*/
	if (ip4_hdr->ihl < 5) {
		log_debug("Packet's IHL field is too small.");
		return VER_DROP;
	}
	if (ip_fast_csum((u8 *) ip4_hdr, ip4_hdr->ihl)) {
		log_debug("Packet's IPv4 checksum is incorrect.");
		return VER_DROP;
	}

	ip4_hdr_len = 4 * ip4_hdr->ihl;

	if (skb->len < ip4_hdr_len) {
		log_debug("Packet is too small to contain the IP header + options.");
		return VER_DROP;
	}
	if (skb->len != be16_to_cpu(ip4_hdr->tot_len)) {
		log_debug("The socket buffer's length does not equal the IPv4 header's lengh field.");
		return VER_DROP;
	}

	datagram_len = skb->len - ip4_hdr_len;

	/*
	 * Set the skb's transport header pointer.
	 * It's yet to be set because the packet hasn't reached the kernel's transport layer.
	 * And despite that, its availability through the rest of the module will be appreciated.
	 */
	skb_set_transport_header(skb, ip4_hdr_len);

	switch (ip4_hdr->protocol) {
	case IPPROTO_TCP:
		result = validate_lengths_tcp(skb, ip4_hdr_len);
		if (result != VER_CONTINUE)
			return result;
		result = validate_csum_tcp4(skb, datagram_len);
		if (result != VER_CONTINUE)
			return result;
		break;

	case IPPROTO_UDP:
		result = validate_lengths_udp(skb, ip4_hdr_len);
		if (result != VER_CONTINUE)
			return result;
		result = validate_csum_udp4(skb, datagram_len);
		if (result != VER_CONTINUE)
			return result;
		break;

	case IPPROTO_ICMP:
		result = validate_lengths_icmp4(skb, ip4_hdr_len);
		if (result != VER_CONTINUE)
			return result;
		result = validate_csum_icmp4(skb, datagram_len);
		if (result != VER_CONTINUE)
			return result;
		break;

	default:
		log_debug("Packet does not use TCP, UDP or ICMP.");
		return VER_DROP;
	}

	return result;
}
