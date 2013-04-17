/*
 * Copyright (C) 2010 Red Hat, Inc. All rights reserved.
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU Lesser General Public License v.2.1.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _LVM_PROPERTIES_H
#define _LVM_PROPERTIES_H

#include "libdevmapper.h"
#include "lvm-types.h"
#include "metadata.h"
#include "report.h"

struct lvm_property_type {
	unsigned type;
	const char *id;
	unsigned is_settable:1;
	unsigned is_string:1;
	unsigned is_integer:1;
	union {
		const char *string;
		uint64_t integer;
	} value;
	int (*get) (const void *obj, struct lvm_property_type *prop);
	int (*set) (void *obj, struct lvm_property_type *prop);
};

int lvseg_get_property(const struct lv_segment *lvseg,
		       struct lvm_property_type *prop);
int lv_get_property(const struct logical_volume *lv,
		    struct lvm_property_type *prop);
int vg_get_property(const struct volume_group *vg,
		    struct lvm_property_type *prop);
int pvseg_get_property(const struct pv_segment *pvseg,
		       struct lvm_property_type *prop);
int pv_get_property(const struct physical_volume *pv,
		    struct lvm_property_type *prop);
int lv_set_property(struct logical_volume *lv,
		    struct lvm_property_type *prop);
int vg_set_property(struct volume_group *vg,
		    struct lvm_property_type *prop);
int pv_set_property(struct physical_volume *pv,
		    struct lvm_property_type *prop);

int lv_create_param_get_property(const struct lvcreate_params *lvcp,
		struct lvm_property_type *prop);

int lv_create_param_set_property(struct lvcreate_params *lvcp,
		    struct lvm_property_type *prop);

#endif
