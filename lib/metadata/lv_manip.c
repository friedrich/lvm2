/*
 * Copyright (C) 2001 Sistina Software
 *
 * This file is released under the LGPL.
 */

#include "metadata.h"
#include "pv_map.h"
#include "log.h"
#include "dbg_malloc.h"

#include <assert.h>

/*
 * These functions adjust the pe counts in pv's
 * after we've added or removed segments.
 */
static void _get_extents(struct stripe_segment *seg)
{
	int s, count;
	struct physical_volume *pv;

	for (s = 0; s < seg->stripes; s++) {
		pv = seg->area[s % seg->stripes].pv;
		count = seg->len / seg->stripes;
		pv->pe_allocated += count;
	}
}

static void _put_extents(struct stripe_segment *seg)
{
	int s, count;
	struct physical_volume *pv;

	for (s = 0; s < seg->stripes; s++) {
		pv = seg->area[s % seg->stripes].pv;
		count = seg->len / seg->stripes;

		assert(pv->pe_allocated >= count);
		pv->pe_allocated -= count;
	}
}

static struct stripe_segment *_alloc_segment(struct pool *mem, int stripes)
{
	struct stripe_segment *seg;
	uint32_t len = sizeof(*seg) + (stripes * sizeof(seg->area[0]));

	if (!(seg = pool_zalloc(mem, len))) {
		stack;
		return NULL;
	}

	return seg;
}

static int _alloc_stripe_area(struct logical_volume *lv, int stripes,
			      struct pv_area **areas, uint32_t *index)
{
	uint32_t count = lv->le_count - *index;
	uint32_t per_area = count / stripes;
	uint32_t smallest = areas[stripes - 1]->count;
	uint32_t s;
	struct stripe_segment *seg;

	if (smallest < per_area)
		per_area = smallest;

	if (!(seg = _alloc_segment(lv->vg->cmd->mem, stripes))) {
		log_err("Couldn't allocate new stripe segment.");
		return 0;
	}

	seg->lv = lv;
	seg->le = *index;
	seg->len = per_area * stripes;
	seg->stripes = stripes;

	for (s = 0; s < stripes; s++) {
		struct pv_area *pva = areas[s];
		seg->area[s].pv = pva->map->pv;
		seg->area[s].pe = pva->start;
		consume_pv_area(pva, per_area);
	}

	list_add(&lv->segments, &seg->list);
	*index += seg->len;
	return 1;
}

static int _comp_area(const void *l, const void *r)
{
	struct pv_area *lhs = *((struct pv_area **) l);
	struct pv_area *rhs = *((struct pv_area **) r);

	if (lhs->count < rhs->count)
		return -1;

	else if (lhs->count > rhs->count)
		return 1;

	return 0;
}

static int _alloc_striped(struct logical_volume *lv,
			  struct list *pvms, uint32_t allocated,
			  uint32_t stripes, uint32_t stripe_size)
{
	struct list *pvmh;
	struct pv_area **areas;
	int pv_count = 0, index;
	struct pv_map *pvm;
	size_t len;

	list_iterate (pvmh, pvms)
		pv_count++;

	/* allocate an array of pv_areas, one candidate per pv */
	len = sizeof(*areas) * pv_count;
	if (!(areas = dbg_malloc(sizeof(*areas) * pv_count))) {
		log_err("Couldn't allocate areas array.");
		return 0;
	}

	while (allocated != lv->le_count) {

		index = 0;
		list_iterate (pvmh, pvms) {
			pvm = list_item(pvmh, struct pv_map);

			if (list_empty(&pvm->areas))
				continue;

			areas[index++] = list_item(pvm->areas.n,
						   struct pv_area);
		}

		if (index < stripes)
			goto no_space;

		/* sort the areas so we allocate from the biggest */
		qsort(areas, index, sizeof(*areas), _comp_area);

		if (!_alloc_stripe_area(lv, stripes, areas, &allocated)) {
			stack;
			return 0;
		}
	}

	return 1;

 no_space:

	log_error("Insufficient free extents (suitable for striping) to "
		  "allocate logical volume %s: %u required",
			  lv->name, lv->le_count);
	return 0;
}


/*
 * The heart of the allocation code.  This
 * function takes a pv_area and allocates it to
 * the lv.  If the lv doesn't need the complete
 * area then the area is split, otherwise the area
 * is unlinked from the pv_map.
 */
static int _alloc_linear_area(struct logical_volume *lv, uint32_t *index,
			      struct pv_map *map, struct pv_area *pva)
{
	uint32_t count, remaining;
	struct stripe_segment *seg;

	count = pva->count;
	remaining = lv->le_count - *index;
	if (count < remaining)
		count = remaining;

	if (!(seg = _alloc_segment(lv->vg->cmd->mem, 1))) {
		log_err("Couldn't allocate new stripe segment.");
		return 0;
	}

	seg->lv = lv;
	seg->le = *index;
	seg->len = count;
	seg->stripe_size = 0;
	seg->stripes = 1;
	seg->area[0].pv = map->pv;
	seg->area[0].pe = pva->start;

	list_add(&lv->segments, &seg->list);

	consume_pv_area(pva, count);
	*index += count;
	return 1;
}

/*
 * Only one area per pv is allowed, so we search
 * for the biggest area, or the first area that
 * can complete the allocation.
 */

/*
 * FIXME: subsequent lvextends may not be contiguous.
 */
static int _alloc_contiguous(struct logical_volume *lv,
			     struct list *pvms, uint32_t allocated)
{
	struct list *tmp1;
	struct pv_map *pvm;
	struct pv_area *pva;

	list_iterate(tmp1, pvms) {
		pvm = list_item(tmp1, struct pv_map);

		if (list_empty(&pvm->areas))
			continue;

		/* first item in the list is the biggest */
		pva = list_item(pvm->areas.n, struct pv_area);

		if (!_alloc_linear_area(lv, &allocated, pvm, pva)) {
			stack;
			return 0;
		}

		if (allocated == lv->le_count)
			break;
	}

	if (allocated != lv->le_count) {
		log_error("Insufficient free extents to "
			  "allocate logical volume %s: %u required",
			  lv->name, lv->le_count);
		return 0;
	}

	return 1;
}

/*
 * Areas just get allocated in order until the lv
 * is full.
 */
static int _alloc_simple(struct logical_volume *lv,
			 struct list *pvms, uint32_t allocated)
{
	struct list *tmp1, *tmp2;
	struct pv_map *pvm;
	struct pv_area *pva;

	list_iterate(tmp1, pvms) {
		pvm = list_item(tmp1, struct pv_map);

		list_iterate(tmp2, &pvm->areas) {
			pva = list_item(tmp2, struct pv_area);
			if (!_alloc_linear_area(lv, &allocated, pvm, pva) ||
			    (allocated == lv->le_count))
				goto done;
		}
	}

 done:
	if (allocated != lv->le_count) {
		log_error("Insufficient free logical extents to "
			  "allocate logical volume %s: %u required",
			  lv->name, lv->le_count);
		return 0;
	}

	return 0;
}

/*
 * Chooses a correct allocation policy.
 */
static int _allocate(struct volume_group *vg, struct logical_volume *lv,
		     struct list *acceptable_pvs, uint32_t allocated,
		     uint32_t stripes, uint32_t stripe_size)
{
	int r = 0;
	struct pool *scratch;
	struct list *pvms, *old_tail = lv->segments.p, *segh;

	if (!(scratch = pool_create(1024))) {
		stack;
		return 0;
	}

	/*
	 * Build the sets of available areas on
	 * the pv's.
	 */
	if (!(pvms = create_pv_maps(scratch, vg, acceptable_pvs))) {
		goto out;
	}

	if (stripes > 1)
		r = _alloc_striped(lv, pvms, allocated, stripes, stripe_size);

	else if (lv->status & ALLOC_CONTIGUOUS)
		r = _alloc_contiguous(lv, pvms, allocated);

	else if (lv->status & ALLOC_SIMPLE)
		r = _alloc_simple(lv, pvms, allocated);

	else {
		log_error("Unknown allocation policy: "
			  "unable to setup logical volume.");
		goto out;
	}

	if (r) {
		vg->free_count -= lv->le_count - allocated;

		/* Iterate through the new segments,
		 * updating pe counts in pv's. */
		for (segh = lv->segments.p; segh != old_tail; segh = segh->p)
			_get_extents(list_item(segh, struct stripe_segment));
	} else {
		/* Put the segment list back how
		 * we found it. */
		old_tail->n = &lv->segments;
		lv->segments.p = old_tail;
	}

      out:
	pool_destroy(scratch);
	return r;
}

static char *_generate_lv_name(struct volume_group *vg,
			       char *buffer, size_t len)
{
	struct list *lvh;
	struct logical_volume *lv;
	int high = -1, i, s;

	list_iterate(lvh, &vg->lvs) {
		lv = &(list_item(lvh, struct lv_list)->lv);

		if (sscanf(lv->name, "lvol%d", &i) != 1)
			continue;

		if (i > high)
			high = i;
	}

	if ((s = snprintf(buffer, len, "lvol%d", high + 1)) < 0 || s >= len)
		return NULL;

	return buffer;
}

struct logical_volume *lv_create(const char *name,
				 uint32_t status,
				 uint32_t stripes,
				 uint32_t stripe_size,
				 uint32_t extents,
				 struct volume_group *vg,
				 struct list *acceptable_pvs)
{
	struct cmd_context *cmd = vg->cmd;
	struct lv_list *ll = NULL;
	struct logical_volume *lv;
	char dname[32];

	if (!extents) {
		log_error("Unable to create logical volume %s with no extents",
			  name);
		return NULL;
	}

	if (vg->free_count < extents) {
		log_error("Insufficient free extents (%u) in volume group %s: "
			  "%u required", vg->free_count, vg->name, extents);
		return NULL;
	}

	if (vg->max_lv == vg->lv_count) {
		log_error("Maximum number of logical volumes (%u) reached "
			  "in volume group %s", vg->max_lv, vg->name);
		return NULL;
	}

	if (!name && !(name = _generate_lv_name(vg, dname, sizeof(dname)))) {
		log_error("Failed to generate unique name for the new "
			  "logical volume");
		return NULL;
	}

	log_verbose("Creating logical volume %s", name);

	if (!(ll = pool_zalloc(cmd->mem, sizeof(*ll)))) {
		stack;
		return NULL;
	}

	list_init(&ll->list);

	lv = &ll->lv;

	strcpy(lv->id.uuid, "");

	if (!(lv->name = pool_strdup(cmd->mem, name))) {
		stack;
		goto bad;
	}

	lv->status = status;
	lv->read_ahead = 0;
	lv->size = extents * vg->extent_size;
	lv->le_count = extents;
	lv->vg = vg;
	list_init(&lv->segments);

	if (!_allocate(vg, lv, acceptable_pvs, 0u, stripes, stripe_size)) {
		stack;
		goto bad;
	}

	vg->lv_count++;
	list_add(&vg->lvs, &ll->list);

	return lv;

 bad:
	if (ll)
		pool_free(cmd->mem, ll);

	return NULL;
}

int lv_reduce(struct logical_volume *lv, uint32_t extents)
{
	struct list *segh;
	struct stripe_segment *seg;
	uint32_t count = extents;

	for (segh = lv->segments.p;
	     (segh != &lv->segments) && count;
	     segh = segh->p) {
		seg = list_item(segh, struct stripe_segment);

		if (seg->len <= count) {
			/* remove this segment completely */
			count -= seg->len;
			_put_extents(seg);
			list_del(segh);
		} else {
			/* reduce this segment */
			_put_extents(seg);
			seg->len -= count;
			_get_extents(seg);
			count = 0;
		}
	}

	lv->le_count -= extents;
	lv->size = lv->le_count * lv->vg->extent_size;

	return 1;
}

int lv_extend(struct logical_volume *lv,
	      uint32_t stripes, uint32_t stripe_size,
	      uint32_t extents,
	      struct list *acceptable_pvs)
{
	uint32_t old_le_count = lv->le_count;
	uint64_t old_size = lv->size;

	lv->le_count += extents;
	lv->size += extents * lv->vg->extent_size;

	if (!_allocate(lv->vg, lv, acceptable_pvs, old_le_count,
		       stripes, stripe_size)) {
		lv->le_count = old_le_count;
		lv->size = old_size;
		return 0;
	}

	return 1;
}

int lv_remove(struct volume_group *vg, struct logical_volume *lv)
{
	struct list *segh;

	/* iterate through the lv's segments freeing off the pe's */
	list_iterate (segh, &lv->segments)
		_put_extents(list_item(segh, struct stripe_segment));

	vg->lv_count--;
	vg->free_count += lv->le_count;

	list_del(&list_head(lv, struct lv_list, lv));

	return 1;
}
