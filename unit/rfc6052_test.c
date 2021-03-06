#include <linux/module.h>
#include <linux/printk.h>
#include <linux/inet.h>

#include "nat64/unit/unit_test.h"
#include "nat64/common/types.h"
#include "nat64/common/str_utils.h"
#include "rfc6052.c"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ramiro Nava");
MODULE_DESCRIPTION("RFC 6052 module test.");

static bool test(const char *prefix6_str, const unsigned int prefix6_len,
		const char *addr4_str, const char *addr6_str)
{
	struct in6_addr addr6;
	struct ipv6_prefix prefix;
	struct in_addr addr4;
	bool success = true;

	if (str_to_addr6(prefix6_str, &prefix.address))
		return false;
	prefix.len = prefix6_len;

	/* 6 to 4 */
	if (str_to_addr6(addr6_str, &addr6))
		return false;
	memset(&addr4, 0, sizeof(addr4));

	success &= ASSERT_INT(0, addr_6to4(&addr6, &prefix, &addr4),
			"result code of %pI6c - %pI6c/%u = %s",
			&addr6, &prefix.address, prefix.len, addr4_str);
	success &= ASSERT_ADDR4(addr4_str, &addr4, "6to4 address result");

	/* 4 to 6 */
	if (str_to_addr4(addr4_str, &addr4))
		return false;
	memset(&addr6, 0, sizeof(addr6));

	success &= ASSERT_INT(0, addr_4to6(&addr4, &prefix, &addr6),
			"result code of %pI4c + %pI6c/%u = %s",
			&addr4, &prefix.address, prefix.len, addr6_str);
	success &= ASSERT_ADDR6(addr6_str, &addr6, "4to6 address result");

	return success;
}

/**
 * Taken from https://tools.ietf.org/html/rfc6052#section-2.4.
 */
static bool test_rfc6052_table(void)
{
	bool success = true;

	success &= test("2001:db8::", 32, "192.0.2.33",
			"2001:db8:c000:221::");
	success &= test("2001:db8:100::", 40, "192.0.2.33",
			"2001:db8:1c0:2:21::");
	success &= test("2001:db8:122::", 48, "192.0.2.33",
			"2001:db8:122:c000:2:2100::");
	success &= test("2001:db8:122:300::", 56, "192.0.2.33",
			"2001:db8:122:3c0:0:221::");
	success &= test("2001:db8:122:344::", 64, "192.0.2.33",
			"2001:db8:122:344:c0:2:2100::");
	success &= test("2001:db8:122:344::", 96, "192.0.2.33",
			"2001:db8:122:344::192.0.2.33");

	return success;
}

int init_module(void)
{
	START_TESTS("rfc6052.c");

	CALL_TEST(test_rfc6052_table(), "Translation tests");

	END_TESTS;
}

void cleanup_module(void)
{
	/* No code. */
}
