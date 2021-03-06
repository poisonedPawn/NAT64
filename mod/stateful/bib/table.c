#include "nat64/mod/stateful/bib/table.h"
#include <net/ipv6.h>
#include "nat64/mod/common/rbtree.h"
#include "nat64/mod/stateful/bib/port_allocator.h"


void bibtable_init(struct bib_table *table)
{
	table->tree6 = RB_ROOT;
	table->tree4 = RB_ROOT;
	table->count = 0;
	spin_lock_init(&table->lock);
}

static void destroy_aux(struct rb_node *node)
{
	bibentry_kfree(rb_entry(node, struct bib_entry, tree6_hook));
}

void bibtable_destroy(struct bib_table *table)
{
	/*
	 * The values need to be released only in one of the trees
	 * because both trees point to the same values.
	 */
	rbtree_clear(&table->tree6, destroy_aux);
}

/**
 * Returns > 0 if bib->ipv6.l3 > addr.
 * Returns < 0 if bib->ipv6.l3 < addr.
 * Returns 0 if bib->ipv6.l3 == addr.
 */
static int compare_addr6(const struct bib_entry *bib,
		const struct in6_addr *addr)
{
	return ipv6_addr_cmp(&bib->ipv6.l3, addr);
}

/**
 * Returns > 0 if bib->ipv6 > addr.
 * Returns < 0 if bib->ipv6 < addr.
 * Returns 0 if bib->ipv6 == addr.
 */
static int compare_full6(const struct bib_entry *bib,
		const struct ipv6_transport_addr *addr)
{
	int gap;

	gap = compare_addr6(bib, &addr->l3);
	if (gap)
		return gap;

	gap = bib->ipv6.l4 - addr->l4;
	return gap;
}

/**
 * Returns > 0 if bib->ipv4.l3 > addr.
 * Returns < 0 if bib->ipv4.l3 < addr.
 * Returns zero if bib->ipv4.l3 == addr.
 */
static int compare_addr4(const struct bib_entry *bib,
		const struct in_addr *addr)
{
	return ipv4_addr_cmp(&bib->ipv4.l3, addr);
}

/**
 * Returns > 0 if bib->ipv4 > addr.
 * Returns < 0 if bib->ipv4 < addr.
 * Returns 0 if bib->ipv4 == addr.
 */
static int compare_full4(const struct bib_entry *bib,
		const struct ipv4_transport_addr *addr)
{
	int gap;

	gap = compare_addr4(bib, &addr->l3);
	if (gap)
		return gap;

	gap = bib->ipv4.l4 - addr->l4;
	return gap;
}

static struct bib_entry *find_by_addr6(const struct bib_table *table,
		const struct ipv6_transport_addr *addr)
{
	return rbtree_find(addr, &table->tree6, compare_full6, struct bib_entry,
			tree6_hook);
}

static struct bib_entry *find_by_addr4(const struct bib_table *table,
		const struct ipv4_transport_addr *addr)
{
	return rbtree_find(addr, &table->tree4, compare_full4, struct bib_entry,
			tree4_hook);
}

int bibtable_get6(struct bib_table *table,
		const struct ipv6_transport_addr *addr,
		struct bib_entry **result)
{
	spin_lock_bh(&table->lock);
	*result = find_by_addr6(table, addr);
	if (*result)
		bibentry_get(*result);
	spin_unlock_bh(&table->lock);

	return (*result) ? 0 : -ESRCH;
}

int bibtable_get4(struct bib_table *table,
		const struct ipv4_transport_addr *addr,
		struct bib_entry **result)
{
	spin_lock_bh(&table->lock);
	*result = find_by_addr4(table, addr);
	if (*result)
		bibentry_get(*result);
	spin_unlock_bh(&table->lock);

	return (*result) ? 0 : -ESRCH;
}

bool bibtable_contains4(struct bib_table *table,
		const struct ipv4_transport_addr *addr)
{
	bool result;

	spin_lock_bh(&table->lock);
	result = find_by_addr4(table, addr) ? true : false;
	spin_unlock_bh(&table->lock);

	return result;
}

static int add6(struct bib_table *table, struct bib_entry *bib)
{
	return rbtree_add(bib, &bib->ipv6, &table->tree6, compare_full6,
			struct bib_entry, tree6_hook);
}

static int add4(struct bib_table *table, struct bib_entry *bib)
{
	return rbtree_add(bib, &bib->ipv4, &table->tree4, compare_full4,
			struct bib_entry, tree4_hook);
}

int bibtable_add(struct bib_table *table, struct bib_entry *bib)
{
	int error;

	spin_lock_bh(&table->lock);

	error = add6(table, bib);
	if (error) {
		log_debug("IPv6 index failed.");
		goto fail;
	}

	error = add4(table, bib);
	if (error) {
		rb_erase(&bib->tree6_hook, &table->tree6);
		log_debug("IPv4 index failed.");
		goto fail;
	}

	table->count++;

	spin_unlock_bh(&table->lock);
	bibentry_log(bib, "Mapped");
	return 0;

fail:
	spin_unlock_bh(&table->lock);
	return error;
}

/**
 * Spinlock must be held.
 */
static void rm(struct bib_table *table, struct bib_entry *bib)
{
	if (!WARN(RB_EMPTY_NODE(&bib->tree6_hook), "Faulty IPv6 index"))
		rb_erase(&bib->tree6_hook, &table->tree6);
	if (!WARN(RB_EMPTY_NODE(&bib->tree4_hook), "Faulty IPv4 index"))
		rb_erase(&bib->tree4_hook, &table->tree4);
	table->count--;

	bibentry_log(bib, "Forgot");
}

void bibtable_rm(struct bib_table *table, struct bib_entry *bib)
{
	spin_lock_bh(&table->lock);
	rm(table, bib);
	spin_unlock_bh(&table->lock);
}

static struct rb_node *find_starting_point(struct bib_table *table,
		const struct ipv4_transport_addr *offset, bool include_offset)
{
	struct bib_entry *bib;
	struct rb_node **node;
	struct rb_node *parent;

	/* If there's no offset, start from the beginning. */
	if (!offset)
		return rb_first(&table->tree4);

	/* If offset is found, start from offset or offset's next. */
	rbtree_find_node(offset, &table->tree4, compare_full4, struct bib_entry,
			tree4_hook, parent, node);
	if (*node)
		return include_offset ? (*node) : rb_next(*node);

	if (!parent)
		return NULL;

	/*
	 * If offset is not found, start from offset's next anyway.
	 * (If offset was meant to exist, it probably timed out and died while
	 * the caller wasn't holding the spinlock; it's nothing to worry about.)
	 */
	bib = rb_entry(parent, struct bib_entry, tree4_hook);
	return (compare_full4(bib, offset) < 0) ? rb_next(parent) : parent;
}

/**
 * The iteration is "safe"; it doesn't die if func() removes and/or deletes the
 * entry.
 */
static int __foreach(struct bib_table *table,
		int (*func)(struct bib_entry *, void *), void *arg,
		const struct ipv4_transport_addr *offset, bool include_offset)
{
	struct rb_node *node, *next;
	int error = 0;
	spin_lock_bh(&table->lock);

	node = find_starting_point(table, offset, include_offset);
	for (; node && !error; node = next) {
		next = rb_next(node);
		error = func(rb_entry(node, struct bib_entry, tree4_hook), arg);
	}

	spin_unlock_bh(&table->lock);
	return error;
}

int bibtable_foreach(struct bib_table *table,
		int (*func)(struct bib_entry *, void *), void *arg,
		const struct ipv4_transport_addr *offset)
{
	return __foreach(table, func, arg, offset, false);
}

int bibtable_count(struct bib_table *table, __u64 *result)
{
	spin_lock_bh(&table->lock);
	*result = table->count;
	spin_unlock_bh(&table->lock);
	return 0;
}

struct iteration_args {
	struct bib_table *table;
	const struct ipv4_prefix *prefix;
	const struct port_range *ports;
	unsigned int deleted_count;
};

static int __flush(struct bib_entry *bib, void *void_args)
{
	struct iteration_args *args = void_args;

	/*
	 * All we need to do is remove the fake user.
	 * Otherwise we might free entries being actively pointed by sessions.
	 */
	if (bib->is_static && bibentry_return(bib)) {
		rm(args->table, bib);
		bibentry_kfree(bib);
		args->deleted_count++;
	}

	return 0;
}

void bibtable_flush(struct bib_table *table)
{
	struct iteration_args args = {
			.table = table,
			.deleted_count = 0,
	};

	__foreach(table, __flush, &args, NULL, 0);
	log_debug("Deleted %u BIB entries.", args.deleted_count);
}

static int __delete_taddr4s(struct bib_entry *bib, void *void_args)
{
	struct iteration_args *args = void_args;

	if (!prefix4_contains(args->prefix, &bib->ipv4.l3))
		return 1; /* positive = break iteration early, not an error. */
	if (!port_range_contains(args->ports, bib->ipv4.l4))
		return 0;

	return __flush(bib, void_args);
}

void bibtable_delete_taddr4s(struct bib_table *table,
		const struct ipv4_prefix *prefix, struct port_range *ports)
{
	struct iteration_args args = {
			.table = table,
			.prefix = prefix,
			.ports = ports,
			.deleted_count = 0,
	};
	struct ipv4_transport_addr offset = {
			.l3 = prefix->address,
			.l4 = ports->min,
	};

	__foreach(table, __delete_taddr4s, &args, &offset, true);
	log_debug("Deleted %u BIB entries.", args.deleted_count);
}

