#include <linux/module.h>
#include <linux/printk.h>

#include "nat64/unit/unit_test.h"
#include "nat64/comm/str_utils.h"
#include "nat64/unit/skb_generator.h"
#include "nat64/unit/validator.h"
#include "translate_packet.c"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alberto Leiva Popper <aleiva@nic.mx>");
MODULE_DESCRIPTION("Translating the Packet module test.");


#define PAYLOAD_LEN 100
struct in6_addr dummies6[2];
struct in_addr dummies4[2];


static struct fragment *create_fragment_ipv4(int payload_len,
		int (*skb_create_fn)(struct ipv4_pair *, struct sk_buff **, u16), u16 df, u16 mf, u16 frag_off)
{
	struct fragment *frag;
	struct sk_buff *skb;
	struct ipv4_pair pair4;
	enum verdict result;

	// init the skb.
	pair4.remote.address = dummies4[0];
	pair4.remote.l4_id = 5644;
	pair4.local.address = dummies4[1];
	pair4.local.l4_id = 6721;
	if (skb_create_fn(&pair4, &skb, payload_len) != 0)
		return NULL;

	ip_hdr(skb)->frag_off = cpu_to_be16(df | mf | frag_off);

	// init the fragment.
	result = frag_create_ipv4(skb, &frag);
	if (!result) {
		log_warning("Could not allocate the fragment.");
		kfree_skb(skb);
		return NULL;
	}

	return frag;
}

static bool create_pkt_ipv4(struct packet *pkt, int payload_len,
		int (*skb_create_fn)(struct ipv4_pair *, struct sk_buff **, u16))
{
	struct fragment *frag = create_fragment_ipv4(payload_len, skb_create_fn, IP_DF, 0, 0);
	if (!frag)
		return false;

	INIT_LIST_HEAD(&pkt->fragments);
	list_add(&frag->next, pkt->fragments.prev);

	return true;
}

static struct fragment *create_fragment_ipv6(int payload_len,
		int (*skb_create_fn)(struct ipv6_pair *, struct sk_buff **, u16), u16 df, u16 mf, u16 frag_off)
{
	struct fragment *frag;
	struct sk_buff *skb;
	struct ipv6_pair pair6;
	struct ipv6hdr *hdr6;
	struct frag_hdr *frag_header;
	enum verdict result;

	// init the skb.
	pair6.remote.address = dummies6[0];
	pair6.remote.l4_id = 5644;
	pair6.local.address = dummies6[1];
	pair6.local.l4_id = 6721;
	if (skb_create_fn(&pair6, &skb, payload_len) != 0)
		return false;

	hdr6 = ipv6_hdr(skb);
	if (hdr6->nexthdr == NEXTHDR_FRAGMENT) {
		frag_header = (struct frag_hdr *) (hdr6 + 1);
		frag_header->frag_off = cpu_to_be16(frag_off << 3 | mf);
	}

	// init the fragment.
	result = frag_create_ipv6(skb, &frag);
	if (!result) {
		log_warning("Could not allocate the fragment.");
		kfree_skb(skb);
		return false;
	}

	return frag;
}

static bool create_pkt_ipv6(struct packet *pkt, int payload_len,
		int (*skb_create_fn)(struct ipv6_pair *, struct sk_buff **, u16))
{
	struct fragment *frag = create_fragment_ipv6(payload_len, skb_create_fn, IP_DF, 0, 0);
	if (!frag)
		return false;

	// init the packet.
	INIT_LIST_HEAD(&pkt->fragments);
	list_add(&frag->next, pkt->fragments.prev);

	return true;
}

static bool create_tuple_ipv6(struct tuple *tuple, u_int8_t l4proto)
{
	tuple->l3_proto = L3PROTO_IPV6;
	tuple->l4_proto = l4proto;
	tuple->src.addr.ipv6 = dummies6[0];
	tuple->src.l4_id = 1234;
	tuple->dst.addr.ipv6 = dummies6[1];
	tuple->dst.l4_id = 4321;

	return true;
}

static bool create_tuple_ipv4(struct tuple *tuple, u_int8_t l4proto)
{
	tuple->l3_proto = L3PROTO_IPV4;
	tuple->l4_proto = l4proto;
	tuple->src.addr.ipv4 = dummies4[0];
	tuple->src.l4_id = 1234;
	tuple->dst.addr.ipv4 = dummies4[1];
	tuple->dst.l4_id = 4321;

	return true;
}

static bool validate_pkt_ipv6_udp(struct packet *pkt, struct tuple *tuple, int payload_len)
{
	struct fragment *frag;

	// Validate the fragment
	if (!validate_fragment_count(pkt, 1))
		return false;

	frag = container_of(pkt->fragments.next, struct fragment, next);

	if (!validate_frag_ipv6(frag, sizeof(struct ipv6hdr)))
		return false;
	if (!validate_frag_udp(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	// Validate the skb
	if (!validate_ipv6_hdr(frag_get_ipv6_hdr(frag), frag->l4_hdr.len + frag->payload.len,
			NEXTHDR_UDP, tuple))
		return false;
	if (!validate_udp_hdr(frag_get_udp_hdr(frag), payload_len, tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}

static bool validate_pkt_ipv6_tcp(struct packet *pkt, struct tuple *tuple, int payload_len)
{
	struct fragment *frag;

	// Validate the fragment
	if (!validate_fragment_count(pkt, 1))
		return false;

	frag = container_of(pkt->fragments.next, struct fragment, next);

	if (!validate_frag_ipv6(frag, sizeof(struct ipv6hdr)))
		return false;
	if (!validate_frag_tcp(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	// Validate the skb
	if (!validate_ipv6_hdr(frag_get_ipv6_hdr(frag), frag->l4_hdr.len + frag->payload.len,
			NEXTHDR_TCP, tuple))
		return false;
	if (!validate_tcp_hdr(frag_get_tcp_hdr(frag), sizeof(struct tcphdr), tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}

static bool validate_pkt_ipv6_icmp(struct packet *pkt, struct tuple *tuple, int payload_len)
{
	struct fragment *frag;

	// Validate the fragment
	if (!validate_fragment_count(pkt, 1))
		return false;

	frag = container_of(pkt->fragments.next, struct fragment, next);

	if (!validate_frag_ipv6(frag, sizeof(struct ipv6hdr)))
		return false;
	if (!validate_frag_icmp6(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	// Validate the skb
	if (!validate_ipv6_hdr(frag_get_ipv6_hdr(frag), frag->l4_hdr.len + frag->payload.len,
			NEXTHDR_ICMP, tuple))
		return false;
	if (!validate_icmp6_hdr(frag_get_icmp6_hdr(frag), 5644, tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}

static bool validate_pkt_ipv4_udp(struct packet *pkt, struct tuple *tuple, int payload_len)
{
	struct fragment *frag;

	// Validate the fragment
	if (!validate_fragment_count(pkt, 1))
		return false;

	frag = container_of(pkt->fragments.next, struct fragment, next);

	if (!validate_frag_ipv4(frag))
		return false;
	if (!validate_frag_udp(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	// Validate the skb
	if (!validate_ipv4_hdr(frag_get_ipv4_hdr(frag),
			sizeof(struct iphdr) + sizeof(struct udphdr) + payload_len, IPPROTO_UDP, tuple))
		return false;
	if (!validate_udp_hdr(frag_get_udp_hdr(frag), payload_len, tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}

static bool validate_pkt_ipv4_tcp(struct packet *pkt, struct tuple *tuple, int payload_len)
{
	struct fragment *frag;

	// Validate the fragment
	if (!validate_fragment_count(pkt, 1))
		return false;

	frag = container_of(pkt->fragments.next, struct fragment, next);

	if (!validate_frag_ipv4(frag))
		return false;
	if (!validate_frag_tcp(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	// Validate the skb
	if (!validate_ipv4_hdr(frag_get_ipv4_hdr(frag),
			sizeof(struct iphdr) + sizeof(struct tcphdr) + payload_len, IPPROTO_TCP, tuple))
		return false;
	if (!validate_tcp_hdr(frag_get_tcp_hdr(frag), sizeof(struct tcphdr), tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}

static bool validate_pkt_ipv4_icmp(struct packet *pkt, struct tuple *tuple, int payload_len)
{
	struct fragment *frag;

	// Validate the fragment
	if (!validate_fragment_count(pkt, 1))
		return false;

	frag = container_of(pkt->fragments.next, struct fragment, next);

	if (!validate_frag_ipv4(frag))
		return false;
	if (!validate_frag_icmp4(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	// Validate the skb
	if (!validate_ipv4_hdr(frag_get_ipv4_hdr(frag),
			sizeof(struct iphdr) + sizeof(struct icmphdr) + payload_len, IPPROTO_ICMP, tuple))
		return false;
	if (!validate_icmp4_hdr(frag_get_icmp4_hdr(frag), 5644, tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}

static bool validate_pkt_multiple_4to6(struct packet *pkt, struct tuple *tuple)
{
	struct fragment *frag;
	int payload_len;
	u16 offset;

	if (!validate_fragment_count(pkt, 3))
		return false;

	log_debug("Validating the first fragment...");
	frag = container_of(pkt->fragments.next, struct fragment, next);
	offset = 0;
	payload_len = 1280 - sizeof(struct ipv6hdr) - sizeof(struct frag_hdr) - sizeof(struct udphdr);

	if (!validate_frag_ipv6(frag, sizeof(struct ipv6hdr) + sizeof(struct frag_hdr)))
		return false;
	if (!validate_frag_udp(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	log_debug("Validating the first skb...");
	if (!validate_ipv6_hdr(frag_get_ipv6_hdr(frag), 1280 - sizeof(struct ipv6hdr), NEXTHDR_FRAGMENT, tuple))
		return false;
	if (!validate_frag_hdr(frag_get_fragment_hdr(frag), offset, IP6_MF))
		return false;
	if (!validate_udp_hdr(frag_get_udp_hdr(frag), 2000, tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, offset))
		return false;

	log_debug("Validating the second fragment...");
	frag = container_of(frag->next.next, struct fragment, next);
	offset = 1232;
	payload_len = 776;

	if (!validate_frag_ipv6(frag, sizeof(struct ipv6hdr) + sizeof(struct frag_hdr)))
		return false;
	if (!validate_frag_empty_l4(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	log_debug("Validating the second skb...");
	if (!validate_ipv6_hdr(frag_get_ipv6_hdr(frag), sizeof(struct frag_hdr) + payload_len, NEXTHDR_FRAGMENT, tuple))
		return false;
	if (!validate_frag_hdr(frag_get_fragment_hdr(frag), offset, IP6_MF))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, offset - sizeof(struct udphdr)))
		return false;

	log_debug("Validating the third fragment...");
	frag = container_of(frag->next.next, struct fragment, next);
	offset = 2008;
	payload_len = 100;

	if (!validate_frag_ipv6(frag, sizeof(struct ipv6hdr) + sizeof(struct frag_hdr)))
		return false;
	if (!validate_frag_empty_l4(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	log_debug("Validating the third skb...");
	if (!validate_ipv6_hdr(frag_get_ipv6_hdr(frag), sizeof(struct frag_hdr) + payload_len, NEXTHDR_FRAGMENT, tuple))
		return false;
	if (!validate_frag_hdr(frag_get_fragment_hdr(frag), offset, 0))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}



static bool validate_pkt_multiple_6to4(struct packet *pkt, struct tuple *tuple)
{
	struct fragment *frag;
	int payload_len;
	u16 offset;

	if (!validate_fragment_count(pkt, 2))
		return false;

	log_debug("Validating the first fragment...");
	frag = container_of(pkt->fragments.next, struct fragment, next);
	offset = 0;
	payload_len = 2000;

	if (!validate_frag_ipv4(frag))
		return false;
	if (!validate_frag_tcp(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	log_debug("Validating the first skb...");
	if (!validate_ipv4_hdr(frag_get_ipv4_hdr(frag), sizeof(struct iphdr) + sizeof(struct tcphdr) + payload_len, IPPROTO_TCP, tuple))
		return false;
	if (!validate_tcp_hdr(frag_get_tcp_hdr(frag), sizeof(struct tcphdr), tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, offset))
		return false;

	log_debug("Validating the second fragment...");
	frag = container_of(frag->next.next, struct fragment, next);
	offset = sizeof(struct tcphdr) + 2000;
	payload_len = 100;

	if (!validate_frag_ipv4(frag))
		return false;
	if (!validate_frag_empty_l4(frag))
		return false;
	if (!validate_frag_payload(frag, payload_len))
		return false;

	log_debug("Validating the second skb...");
	if (!validate_ipv4_hdr(frag_get_ipv4_hdr(frag), sizeof(struct iphdr) + payload_len, IPPROTO_TCP, tuple))
		return false;
	if (!validate_payload(frag_get_payload(frag), payload_len, 0))
		return false;

	return true;
}



bool test_simple_4to6_udp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv6(&tuple, L4PROTO_UDP))
		return false;
	if (!create_pkt_ipv4(&pkt_in, PAYLOAD_LEN, create_skb_ipv4_udp))
		return false;

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_ipv6_udp(&pkt_out, &tuple, PAYLOAD_LEN))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_simple_4to6_tcp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv6(&tuple, L4PROTO_TCP))
		return false;
	if (!create_pkt_ipv4(&pkt_in, PAYLOAD_LEN, create_skb_ipv4_tcp))
		return false;

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_ipv6_tcp(&pkt_out, &tuple, PAYLOAD_LEN))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_simple_4to6_icmp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv6(&tuple, L4PROTO_ICMP))
		return false;
	if (!create_pkt_ipv4(&pkt_in, PAYLOAD_LEN, create_skb_ipv4_icmp))
		return false;

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_ipv6_icmp(&pkt_out, &tuple, PAYLOAD_LEN))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_simple_6to4_udp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv4(&tuple, L4PROTO_UDP))
		return false;
	if (!create_pkt_ipv6(&pkt_in, PAYLOAD_LEN, create_skb_ipv6_udp))
		return false;

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_ipv4_udp(&pkt_out, &tuple, PAYLOAD_LEN))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_simple_6to4_tcp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv4(&tuple, L4PROTO_TCP))
		return false;
	if (!create_pkt_ipv6(&pkt_in, PAYLOAD_LEN, create_skb_ipv6_tcp))
		return false;

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_ipv4_tcp(&pkt_out, &tuple, PAYLOAD_LEN))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_simple_6to4_icmp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv4(&tuple, L4PROTO_ICMP))
		return false;
	if (!create_pkt_ipv6(&pkt_in, PAYLOAD_LEN, create_skb_ipv6_icmp))
		return false;

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_ipv4_icmp(&pkt_out, &tuple, PAYLOAD_LEN))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_multiple_4to6_udp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;
	struct fragment *frag;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv6(&tuple, L4PROTO_UDP))
		return false;



	INIT_LIST_HEAD(&pkt_in.fragments);

	frag = create_fragment_ipv4(2000, create_skb_ipv4_udp, 0, IP_MF, 0);
	if (!frag)
		goto fail;
	list_add(&frag->next, pkt_in.fragments.prev);

	frag = create_fragment_ipv4(100, create_skb_ipv4_empty, 0, 0, 2008);
	if (!frag)
		goto fail;
	list_add(&frag->next, pkt_in.fragments.prev);

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_multiple_4to6(&pkt_out, &tuple))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

bool test_multiple_6to4_tcp(void)
{
	struct packet pkt_in, pkt_out;
	struct tuple tuple;
	enum verdict result;
	struct fragment *frag;

	// Init
	INIT_LIST_HEAD(&pkt_out.fragments);

	if (!create_tuple_ipv4(&tuple, L4PROTO_TCP))
		return false;



	INIT_LIST_HEAD(&pkt_in.fragments);

	frag = create_fragment_ipv6(2000, create_skb_ipv6_tcp, 0, IP_MF, 0);
	if (!frag)
		goto fail;
	list_add(&frag->next, pkt_in.fragments.prev);

	frag = create_fragment_ipv6(100, create_skb_ipv6_empty, 0, 0, 2008);
	if (!frag)
		goto fail;
	list_add(&frag->next, pkt_in.fragments.prev);

	// Call the function
	result = translating_the_packet(&tuple, &pkt_in, &pkt_out);
	if (result != VER_CONTINUE)
		goto fail;

	// Validate
	if (!validate_pkt_multiple_6to4(&pkt_out, &tuple))
		goto fail;

	// Yaaaaaaaaay
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return true;

fail:
	pkt_kfree(&pkt_in);
	pkt_kfree(&pkt_out);
	return false;
}

int init_module(void)
{
	START_TESTS("Translating the Packet (IPv4 to IPv6)");

	if (str_to_addr6("1::1", &dummies6[0]) != 0)
		return -EINVAL;
	if (str_to_addr6("2::2", &dummies6[1]) != 0)
		return -EINVAL;
	if (str_to_addr4("1.1.1.1", &dummies4[0]) != 0)
		return -EINVAL;
	if (str_to_addr4("2.2.2.2", &dummies4[1]) != 0)
		return -EINVAL;

	translate_packet_init();

	CALL_TEST(test_simple_4to6_udp(), "Simple 4->6 UDP");
	CALL_TEST(test_simple_4to6_tcp(), "Simple 4->6 TCP");
	CALL_TEST(test_simple_4to6_icmp(), "Simple 4->6 ICMP");
	CALL_TEST(test_simple_6to4_udp(), "Simple 6->4 UDP");
	CALL_TEST(test_simple_6to4_tcp(), "Simple 6->4 TCP");
	CALL_TEST(test_simple_6to4_icmp(), "Simple 6->4 ICMP");
	CALL_TEST(test_multiple_4to6_udp(), "Multiple 4->6 UDP");
	CALL_TEST(test_multiple_6to4_tcp(), "Multiple 6->4 TCP");

	translate_packet_destroy();

	END_TESTS;
}

void cleanup_module(void)
{
	/* No code. */
}
