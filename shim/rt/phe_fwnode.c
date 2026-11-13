/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_fwnode.c -- the fwnode / OF property and graph API over the static tree of phe_fwnode.h, with the semantics the
 * vendored drivers and v4l2-fwnode.c expect: properties by name, child iteration, "port"/"endpoint" graph walking
 * with "reg" ids and "remote-endpoint" references, and reference arguments (gpios, clocks, supplies). */
#include "../include/phe_fwnode.h"

static struct phe_node *N(const struct fwnode_handle *f) { return f ? PHE_NODE_OF(f) : NULL; }
static struct fwnode_handle *F(struct phe_node *n) { return n ? &n->np.fwnode : NULL; }

static void link(struct phe_node *n, struct phe_node *parent)
{
	struct phe_node *const *c;
	n->parent = parent;
	n->np.name = n->name;
	n->np.full_name = n->name;
	n->np.fwnode.node = (const struct phe_fwnode *)n;
	for (c = n->children; c && *c; ++c)
		link(*c, n);
}

void phe_fwnode_init(struct phe_node *root) { link(root, NULL); }

const struct phe_prop *phe_node_prop(const struct phe_node *n, const char *name)
{
	const struct phe_prop *p;
	for (p = n ? n->props : NULL; p && p->name; ++p)
		if (!shim_strcmp(p->name, name))
			return p;
	return NULL;
}

struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *f) { return f; }
void fwnode_handle_put(struct fwnode_handle *f) { (void)f; }
bool fwnode_device_is_available(const struct fwnode_handle *f)
{
	const struct phe_prop *p = phe_node_prop(N(f), "status");
	return !p || !shim_strcmp(p->strs[0], "okay");
}
struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *f) { return F(N(f) ? N(f)->parent : NULL); }

struct fwnode_handle *fwnode_get_next_child_node(const struct fwnode_handle *f, struct fwnode_handle *child)
{
	struct phe_node *n = N(f), *const *c;
	if (!n || !n->children)
		return NULL;
	c = n->children;
	if (child) {
		while (*c && *c != N(child))
			++c;
		if (*c)
			++c;
	}
	return F(*c);
}

static bool name_is(const char *full, const char *base)
{
	size_t l = shim_strlen(base);
	return !shim_strncmp(full, base, l) && (full[l] == 0 || full[l] == '@');
}

struct fwnode_handle *fwnode_get_named_child_node(const struct fwnode_handle *f, const char *name)
{
	struct fwnode_handle *c;
	fwnode_for_each_child_node(f, c)
		if (name_is(N(c)->name, name))
			return c;
	return NULL;
}

const char *fwnode_get_name(const struct fwnode_handle *f) { return N(f) ? N(f)->name : NULL; }

bool fwnode_property_present(const struct fwnode_handle *f, const char *name) { return phe_node_prop(N(f), name) != NULL; }

int fwnode_property_read_u32_array(const struct fwnode_handle *f, const char *name, u32 *val, size_t nval)
{
	const struct phe_prop *p = phe_node_prop(N(f), name);
	size_t i;
	if (!p)
		return -EINVAL;
	if (p->kind != PHE_PROP_U32)
		return -EPROTO;
	if (!val)
		return (int)p->n;
	if (nval > p->n)
		return -EOVERFLOW;
	for (i = 0; i < nval; ++i)
		val[i] = p->u32s[i];
	return 0;
}

int fwnode_property_read_u64_array(const struct fwnode_handle *f, const char *name, u64 *val, size_t nval)
{
	const struct phe_prop *p = phe_node_prop(N(f), name);
	size_t i;
	if (!p)
		return -EINVAL;
	if (p->kind != PHE_PROP_U64)
		return -EPROTO;
	if (!val)
		return (int)p->n;
	if (nval > p->n)
		return -EOVERFLOW;
	for (i = 0; i < nval; ++i)
		val[i] = p->u64s[i];
	return 0;
}

int fwnode_property_read_string(const struct fwnode_handle *f, const char *name, const char **val)
{
	const struct phe_prop *p = phe_node_prop(N(f), name);
	if (!p)
		return -EINVAL;
	if (p->kind != PHE_PROP_STR || !p->n)
		return -EPROTO;
	*val = p->strs[0];
	return 0;
}

int fwnode_property_get_reference_args(const struct fwnode_handle *f, const char *prop, const char *nargs_prop,
	unsigned int nargs, unsigned int index, struct fwnode_reference_args *args)
{
	const struct phe_prop *p = phe_node_prop(N(f), prop);
	unsigned int i, cells, entries;
	(void)nargs_prop; (void)nargs;
	if (!p || p->kind != PHE_PROP_REF)
		return -ENOENT;
	cells = p->cells;
	entries = cells ? p->n / cells : 1;
	if (index >= entries)
		return -ENOENT;
	if (args) {
		args->fwnode = F(p->ref);
		args->nargs = cells;
		for (i = 0; i < cells && i < NR_FWNODE_REFERENCE_ARGS; ++i)
			args->args[i] = p->u32s[index * cells + i];
	}
	return 0;
}

/* ---- graph: node -> "port"/"ports" -> "endpoint" ---- */
static u32 reg_of(struct phe_node *n)
{
	u32 r = 0;
	fwnode_property_read_u32_array(F(n), "reg", &r, 1);
	return r;
}

struct fwnode_handle *fwnode_graph_get_next_endpoint(const struct fwnode_handle *f, struct fwnode_handle *prev)
{
	struct fwnode_handle *port, *ep, *ports = fwnode_get_named_child_node(f, "ports");
	const struct fwnode_handle *holder = ports ? ports : f;
	bool seen = prev == NULL;
	fwnode_for_each_child_node(holder, port) {
		if (!name_is(N(port)->name, "port"))
			continue;
		fwnode_for_each_child_node(port, ep) {
			if (!name_is(N(ep)->name, "endpoint"))
				continue;
			if (seen)
				return ep;
			if (ep == prev)
				seen = true;
		}
	}
	return NULL;
}

bool fwnode_graph_is_endpoint(const struct fwnode_handle *f) { return f && name_is(N(f)->name, "endpoint"); }

struct fwnode_handle *fwnode_graph_get_port_parent(const struct fwnode_handle *f)
{
	struct phe_node *n = N(f);
	if (!n || !n->parent)
		return NULL;
	n = n->parent;				/* the port */
	if (n->parent && name_is(n->parent->name, "ports"))
		n = n->parent;
	return F(n->parent);
}

struct fwnode_handle *fwnode_graph_get_remote_endpoint(const struct fwnode_handle *f)
{
	const struct phe_prop *p = phe_node_prop(N(f), "remote-endpoint");
	return p && p->kind == PHE_PROP_REF ? F(p->ref) : NULL;
}

struct fwnode_handle *fwnode_graph_get_remote_port_parent(const struct fwnode_handle *f)
{
	return fwnode_graph_get_port_parent(fwnode_graph_get_remote_endpoint(f));
}

int fwnode_graph_parse_endpoint(const struct fwnode_handle *f, struct fwnode_endpoint *endpoint)
{
	struct phe_node *ep = N(f);
	if (!ep || !ep->parent)
		return -EINVAL;
	endpoint->port = reg_of(ep->parent);
	endpoint->id = reg_of(ep);
	endpoint->local_fwnode = f;
	return 0;
}

struct fwnode_handle *fwnode_graph_get_endpoint_by_id(const struct fwnode_handle *f, u32 port, u32 endpoint,
	unsigned long flags)
{
	struct fwnode_handle *ep, *best = NULL;
	struct fwnode_endpoint e;
	fwnode_graph_for_each_endpoint(f, ep) {
		if (fwnode_graph_parse_endpoint(ep, &e) || e.port != port)
			continue;
		if (!(flags & FWNODE_GRAPH_DEVICE_DISABLED) &&
		    !fwnode_device_is_available(fwnode_graph_get_remote_port_parent(ep)))
			continue;
		if (e.id == endpoint)
			return ep;
		if ((flags & FWNODE_GRAPH_ENDPOINT_NEXT) && e.id > endpoint && (!best || e.id < reg_of(N(best))))
			best = ep;
	}
	return best;
}

/* ---- OF matching ---- */
static const struct of_device_id *match(const struct of_device_id *t, const struct phe_node *n)
{
	const struct phe_prop *p = phe_node_prop(n, "compatible");
	unsigned int i;
	for (; t && t->compatible[0]; ++t)
		for (i = 0; p && i < p->n; ++i)
			if (!shim_strcmp(t->compatible, p->strs[i]))
				return t;
	return NULL;
}

const struct of_device_id *phe_of_match(const struct of_device_id *table, const struct fwnode_handle *f)
{
	return match(table, N(f));
}

const void *of_device_get_match_data(const struct device *dev)
{
	const struct of_device_id *m;
	if (!dev->driver || !dev->fwnode)
		return NULL;
	m = match(dev->driver->of_match_table, N(dev->fwnode));
	return m ? m->data : NULL;
}

int device_match_fwnode(struct device *dev, const void *fwnode) { return dev->fwnode == fwnode; }
void device_set_node(struct device *dev, struct fwnode_handle *fwnode)
{
	dev->fwnode = fwnode;
	dev->of_node = fwnode ? to_of_node(fwnode) : NULL;
}
