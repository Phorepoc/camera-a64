/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_fwnode.h -- the static device tree behind the shim's fwnode/OF API: a board describes its nodes with these
 * structs (glue/pinephone_dt.c, transcribed from linux/arch/arm64/boot/dts/allwinner/), shim/rt/phe_fwnode.c walks them.
 * Properties are u32 cells, strings, or references (a node plus argument cells, like <&pio 4 13 flags>). */
#ifndef PHE_FWNODE_H
#define PHE_FWNODE_H
#include "phe_linux.h"

enum phe_prop_kind { PHE_PROP_FLAG, PHE_PROP_U32, PHE_PROP_U64, PHE_PROP_STR, PHE_PROP_REF };

struct phe_node;
struct phe_prop {
	const char *name;
	enum phe_prop_kind kind;
	unsigned int n;			/* cells (U32/U64/REF args, all entries), strings (STR) */
	unsigned int cells;		/* PHE_PROP_REF: argument cells per entry (n / cells entries) */
	const u32 *u32s;
	const u64 *u64s;
	const char * const *strs;
	struct phe_node *ref;		/* PHE_PROP_REF: the target node (all entries point at the same provider) */
};

struct phe_node {
	struct device_node np;		/* np.fwnode is the handle the drivers see */
	const char *name;		/* "endpoint@0", "port", "rear-camera@4c" */
	struct phe_node *parent;
	struct phe_node *const *children;	/* NULL-terminated */
	const struct phe_prop *props;	/* terminated by name == NULL */
};

#define PHE_NODE_OF(f) container_of(to_of_node(f), struct phe_node, np)
const struct phe_prop *phe_node_prop(const struct phe_node *n, const char *name);
void phe_fwnode_init(struct phe_node *root);	/* links parents and np.fwnode for the whole tree */
#endif
