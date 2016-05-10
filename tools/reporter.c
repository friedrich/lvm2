/*
 * Copyright (C) 2001-2004 Sistina Software, Inc. All rights reserved.
 * Copyright (C) 2004-2009 Red Hat, Inc. All rights reserved.
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU Lesser General Public License v.2.1.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "tools.h"

#include "report.h"

struct report_args {
	int argc;
	char **argv;
	report_type_t report_type;
	int args_are_pvs;
	int aligned;
	int buffered;
	int headings;
	int field_prefixes;
	int quoted;
	int columns_as_rows;
	const char *keys;
	const char *options;
	const char *fields_to_compact;
	const char *separator;
	const char *selection;
};

static int _process_each_devtype(struct cmd_context *cmd, int argc,
				 struct processing_handle *handle)
{
	if (argc)
		log_warn("WARNING: devtypes currently ignores command line arguments.");

	if (!report_devtypes(handle->custom_handle))
		return_ECMD_FAILED;

	return ECMD_PROCESSED;
}

static int _vgs_single(struct cmd_context *cmd __attribute__((unused)),
		       const char *vg_name, struct volume_group *vg,
		       struct processing_handle *handle)
{
	struct selection_handle *sh = handle->selection_handle;

	if (!report_object(sh ? : handle->custom_handle, sh != NULL,
			   vg, NULL, NULL, NULL, NULL, NULL, NULL))
		return_ECMD_FAILED;

	check_current_backup(vg);

	return ECMD_PROCESSED;
}

static void _choose_lv_segment_for_status_report(const struct logical_volume *lv, const struct lv_segment **lv_seg)
{
	/*
	 * By default, take the first LV segment to report status for.
	 * If there's any other specific segment that needs to be
	 * reported instead for the LV, choose it here and assign it
	 * to lvdm->seg_status->seg. This is the segment whose
	 * status line will be used for report exactly.
	 */
	*lv_seg = first_seg(lv);
}

static int _do_info_and_status(struct cmd_context *cmd,
				const struct logical_volume *lv,
				const struct lv_segment *lv_seg,
				struct lv_with_info_and_seg_status *status,
				int do_info, int do_status)
{
	unsigned use_layer = lv_is_thin_pool(lv) ? 1 : 0;

	status->lv = lv;

	if (lv_is_historical(lv))
		return 1;

	if (do_status) {
		if (!(status->seg_status.mem = dm_pool_create("reporter_pool", 1024)))
			return_0;
		if (!lv_seg)
			_choose_lv_segment_for_status_report(lv, &lv_seg);
		if (do_info) {
			/* both info and status */
			status->info_ok = lv_info_with_seg_status(cmd, lv, lv_seg, use_layer, status, 1, 1);
			/* for inactive thin-pools reset lv info struct */
			if (use_layer && status->info_ok &&
			    !lv_info(cmd, lv, 0, NULL, 0, 0))
				memset(&status->info,  0, sizeof(status->info));
		} else
			/* status only */
			status->info_ok = lv_status(cmd, lv_seg, use_layer, &status->seg_status);
	} else if (do_info)
		/* info only */
		status->info_ok = lv_info(cmd, lv, use_layer, &status->info, 1, 1);

	return 1;
}

static int _do_lvs_with_info_and_status_single(struct cmd_context *cmd,
					       const struct logical_volume *lv,
					       int do_info, int do_status,
					       struct processing_handle *handle)
{
	struct selection_handle *sh = handle->selection_handle;
	struct lv_with_info_and_seg_status status = {
		.seg_status.type = SEG_STATUS_NONE
	};
	int r = ECMD_FAILED;

	if (!_do_info_and_status(cmd, lv, NULL, &status, do_info, do_status))
		goto_out;

	if (!report_object(sh ? : handle->custom_handle, sh != NULL,
			   lv->vg, lv, NULL, NULL, NULL, &status, NULL))
		goto out;

	r = ECMD_PROCESSED;
out:
	if (status.seg_status.mem)
		dm_pool_destroy(status.seg_status.mem);

	return r;
}

static int _lvs_single(struct cmd_context *cmd, struct logical_volume *lv,
		       struct processing_handle *handle)
{
	return _do_lvs_with_info_and_status_single(cmd, lv, 0, 0, handle);
}

static int _lvs_with_info_single(struct cmd_context *cmd, struct logical_volume *lv,
				 struct processing_handle *handle)
{
	return _do_lvs_with_info_and_status_single(cmd, lv, 1, 0, handle);
}

static int _lvs_with_status_single(struct cmd_context *cmd, struct logical_volume *lv,
				   struct processing_handle *handle)
{
	return _do_lvs_with_info_and_status_single(cmd, lv, 0, 1, handle);
}

static int _lvs_with_info_and_status_single(struct cmd_context *cmd, struct logical_volume *lv,
					    struct processing_handle *handle)
{
	return _do_lvs_with_info_and_status_single(cmd, lv, 1, 1, handle);
}

static int _do_segs_with_info_and_status_single(struct cmd_context *cmd,
						const struct lv_segment *seg,
						int do_info, int do_status,
						struct processing_handle *handle)
{
	struct selection_handle *sh = handle->selection_handle;
	struct lv_with_info_and_seg_status status = {
		.seg_status.type = SEG_STATUS_NONE
	};
	int r = ECMD_FAILED;

	if (!_do_info_and_status(cmd, seg->lv, seg, &status, do_info, do_status))
		goto_out;

	if (!report_object(sh ? : handle->custom_handle, sh != NULL,
			   seg->lv->vg, seg->lv, NULL, seg, NULL, &status, NULL))
	goto_out;

	r = ECMD_PROCESSED;
out:
	if (status.seg_status.mem)
		dm_pool_destroy(status.seg_status.mem);

	return r;
}

static int _segs_single(struct cmd_context *cmd, struct lv_segment *seg,
			struct processing_handle *handle)
{
	return _do_segs_with_info_and_status_single(cmd, seg, 0, 0, handle);
}

static int _segs_with_info_single(struct cmd_context *cmd, struct lv_segment *seg,
				  struct processing_handle *handle)
{
	return _do_segs_with_info_and_status_single(cmd, seg, 1, 0, handle);
}

static int _segs_with_status_single(struct cmd_context *cmd, struct lv_segment *seg,
				    struct processing_handle *handle)
{
	return _do_segs_with_info_and_status_single(cmd, seg, 0, 1, handle);
}

static int _segs_with_info_and_status_single(struct cmd_context *cmd, struct lv_segment *seg,
					     struct processing_handle *handle)
{
	return _do_segs_with_info_and_status_single(cmd, seg, 1, 1, handle);
}

static int _lvsegs_single(struct cmd_context *cmd, struct logical_volume *lv,
			  struct processing_handle *handle)
{
	if (!arg_count(cmd, all_ARG) && !lv_is_visible(lv))
		return ECMD_PROCESSED;

	return process_each_segment_in_lv(cmd, lv, handle, _segs_single);
}

static int _lvsegs_with_info_single(struct cmd_context *cmd, struct logical_volume *lv,
				    struct processing_handle *handle)
{
	if (!arg_count(cmd, all_ARG) && !lv_is_visible(lv))
		return ECMD_PROCESSED;

	return process_each_segment_in_lv(cmd, lv, handle, _segs_with_info_single);
}

static int _lvsegs_with_status_single(struct cmd_context *cmd, struct logical_volume *lv,
				      struct processing_handle *handle)
{
	if (!arg_count(cmd, all_ARG) && !lv_is_visible(lv))
		return ECMD_PROCESSED;

	return process_each_segment_in_lv(cmd, lv, handle, _segs_with_status_single);
}

static int _lvsegs_with_info_and_status_single(struct cmd_context *cmd, struct logical_volume *lv,
					       struct processing_handle *handle)
{
	if (!arg_count(cmd, all_ARG) && !lv_is_visible(lv))
		return ECMD_PROCESSED;

	return process_each_segment_in_lv(cmd, lv, handle, _segs_with_info_and_status_single);
}

static int _do_pvsegs_sub_single(struct cmd_context *cmd,
				 struct volume_group *vg,
				 struct pv_segment *pvseg,
				 int do_info,
				 int do_status,
				 struct processing_handle *handle)
{
	struct selection_handle *sh = handle->selection_handle;
	int ret = ECMD_PROCESSED;
	struct lv_segment *seg = pvseg->lvseg;

	struct segment_type _freeseg_type = {
		.name = "free",
		.flags = SEG_VIRTUAL | SEG_CANNOT_BE_ZEROED,
	};

	struct volume_group _free_vg = {
		.cmd = cmd,
		.name = "",
		.pvs = DM_LIST_HEAD_INIT(_free_vg.pvs),
		.lvs = DM_LIST_HEAD_INIT(_free_vg.lvs),
		.historical_lvs = DM_LIST_HEAD_INIT(_free_vg.historical_lvs),
		.tags = DM_LIST_HEAD_INIT(_free_vg.tags),
	};

	struct logical_volume _free_logical_volume = {
		.vg = vg ?: &_free_vg,
		.name = "",
		.status = VISIBLE_LV,
		.major = -1,
		.minor = -1,
		.tags = DM_LIST_HEAD_INIT(_free_logical_volume.tags),
		.segments = DM_LIST_HEAD_INIT(_free_logical_volume.segments),
		.segs_using_this_lv = DM_LIST_HEAD_INIT(_free_logical_volume.segs_using_this_lv),
		.indirect_glvs = DM_LIST_HEAD_INIT(_free_logical_volume.indirect_glvs),
		.snapshot_segs = DM_LIST_HEAD_INIT(_free_logical_volume.snapshot_segs),
	};

	struct lv_segment _free_lv_segment = {
		.lv = &_free_logical_volume,
		.segtype = &_freeseg_type,
		.len = pvseg->len,
		.tags = DM_LIST_HEAD_INIT(_free_lv_segment.tags),
		.origin_list = DM_LIST_HEAD_INIT(_free_lv_segment.origin_list),
	};

	struct lv_with_info_and_seg_status status = {
		.seg_status.type = SEG_STATUS_NONE,
		.lv = &_free_logical_volume
	};

	if (seg && !_do_info_and_status(cmd, seg->lv, seg, &status, do_info, do_status))
		goto_out;

	if (!report_object(sh ? : handle->custom_handle, sh != NULL,
			   vg, seg ? seg->lv : &_free_logical_volume,
			   pvseg->pv, seg ? : &_free_lv_segment, pvseg,
			   &status, pv_label(pvseg->pv))) {
		ret = ECMD_FAILED;
		goto_out;
	}

 out:
	if (status.seg_status.mem)
		dm_pool_destroy(status.seg_status.mem);

	return ret;
}

static int _pvsegs_sub_single(struct cmd_context *cmd,
			      struct volume_group *vg,
			      struct pv_segment *pvseg,
			      struct processing_handle *handle)
{
	return _do_pvsegs_sub_single(cmd, vg, pvseg, 0, 0, handle);
}

static int _pvsegs_with_lv_info_sub_single(struct cmd_context *cmd,
					   struct volume_group *vg,
					   struct pv_segment *pvseg,
					   struct processing_handle *handle)
{
	return _do_pvsegs_sub_single(cmd, vg, pvseg, 1, 0, handle);
}

static int _pvsegs_with_lv_status_sub_single(struct cmd_context *cmd,
					     struct volume_group *vg,
					     struct pv_segment *pvseg,
					     struct processing_handle *handle)
{
	return _do_pvsegs_sub_single(cmd, vg, pvseg, 0, 1, handle);
}

static int _pvsegs_with_lv_info_and_status_sub_single(struct cmd_context *cmd,
						      struct volume_group *vg,
						      struct pv_segment *pvseg,
						      struct processing_handle *handle)
{
	return _do_pvsegs_sub_single(cmd, vg, pvseg, 1, 1, handle);
}

static int _pvsegs_single(struct cmd_context *cmd,
			  struct volume_group *vg,
			  struct physical_volume *pv,
			  struct processing_handle *handle)
{
	return process_each_segment_in_pv(cmd, vg, pv, handle, _pvsegs_sub_single);
}

static int _pvsegs_with_lv_info_single(struct cmd_context *cmd,
				       struct volume_group *vg,
				       struct physical_volume *pv,
				       struct processing_handle *handle)
{
	return process_each_segment_in_pv(cmd, vg, pv, handle, _pvsegs_with_lv_info_sub_single);
}

static int _pvsegs_with_lv_status_single(struct cmd_context *cmd,
					 struct volume_group *vg,
					 struct physical_volume *pv,
					 struct processing_handle *handle)
{
	return process_each_segment_in_pv(cmd, vg, pv, handle, _pvsegs_with_lv_status_sub_single);
}

static int _pvsegs_with_lv_info_and_status_single(struct cmd_context *cmd,
						  struct volume_group *vg,
						  struct physical_volume *pv,
						  struct processing_handle *handle)
{
	return process_each_segment_in_pv(cmd, vg, pv, handle, _pvsegs_with_lv_info_and_status_sub_single);
}

static int _pvs_single(struct cmd_context *cmd, struct volume_group *vg,
		       struct physical_volume *pv,
		       struct processing_handle *handle)
{
	struct selection_handle *sh = handle->selection_handle;

	if (!report_object(sh ? : handle->custom_handle, sh != NULL,
			   vg, NULL, pv, NULL, NULL, NULL, NULL))
		return_ECMD_FAILED;

	return ECMD_PROCESSED;
}

static int _label_single(struct cmd_context *cmd, struct label *label,
		         struct processing_handle *handle)
{
	struct selection_handle *sh = handle->selection_handle;

	if (!report_object(sh ? : handle->custom_handle, sh != NULL,
			   NULL, NULL, NULL, NULL, NULL, NULL, label))
		return_ECMD_FAILED;

	return ECMD_PROCESSED;
}

static int _pvs_in_vg(struct cmd_context *cmd, const char *vg_name,
		      struct volume_group *vg,
		      struct processing_handle *handle)
{
	return process_each_pv_in_vg(cmd, vg, handle, &_pvs_single);
}

static int _pvsegs_in_vg(struct cmd_context *cmd, const char *vg_name,
			 struct volume_group *vg,
			 struct processing_handle *handle)
{
	return process_each_pv_in_vg(cmd, vg, handle, &_pvsegs_single);
}

static int _get_final_report_type(int args_are_pvs,
				  report_type_t report_type,
				  int *lv_info_needed,
				  int *lv_segment_status_needed,
				  report_type_t *final_report_type)
{
	/* Do we need to acquire LV device info in addition? */
	*lv_info_needed = (report_type & (LVSINFO | LVSINFOSTATUS)) ? 1 : 0;

	/* Do we need to acquire LV device status in addition? */
	*lv_segment_status_needed = (report_type & (LVSSTATUS | LVSINFOSTATUS)) ? 1 : 0;

	/* Ensure options selected are compatible */
	if (report_type & SEGS)
		report_type |= LVS;
	if (report_type & PVSEGS)
		report_type |= PVS;
	if ((report_type & (LVS | LVSINFO | LVSSTATUS | LVSINFOSTATUS)) &&
	    (report_type & (PVS | LABEL)) && !args_are_pvs) {
		log_error("Can't report LV and PV fields at the same time");
		return 0;
	}

	/* Change report type if fields specified makes this necessary */
	if ((report_type & PVSEGS) ||
	    ((report_type & (PVS | LABEL)) && (report_type & (LVS | LVSINFO | LVSSTATUS | LVSINFOSTATUS))))
		report_type = PVSEGS;
	else if ((report_type & PVS) ||
		 ((report_type & LABEL) && (report_type & VGS)))
		report_type = PVS;
	else if (report_type & SEGS)
		report_type = SEGS;
	else if (report_type & (LVS | LVSINFO | LVSSTATUS | LVSINFOSTATUS))
		report_type = LVS;

	*final_report_type = report_type;
	return 1;
}

static int _report_all_in_vg(struct cmd_context *cmd, struct processing_handle *handle,
			     struct volume_group *vg, report_type_t type,
			     int do_lv_info, int do_lv_seg_status)
{
	int r = 0;

	switch (type) {
		case VGS:
			r = _vgs_single(cmd, vg->name, vg, handle);
			break;
		case LVS:
			r = process_each_lv_in_vg(cmd, vg, NULL, NULL, 0, handle,
						  do_lv_info && !do_lv_seg_status ? &_lvs_with_info_single :
						  !do_lv_info && do_lv_seg_status ? &_lvs_with_status_single :
						  do_lv_info && do_lv_seg_status ? &_lvs_with_info_and_status_single :
										   &_lvs_single);
			break;
		case SEGS:
			r = process_each_lv_in_vg(cmd, vg, NULL, NULL, 0, handle,
						  do_lv_info && !do_lv_seg_status ? &_lvsegs_with_info_single :
						  !do_lv_info && do_lv_seg_status ? &_lvsegs_with_status_single :
						  do_lv_info && do_lv_seg_status ? &_lvsegs_with_info_and_status_single :
										   &_lvsegs_single);
			break;
		case PVS:
			r = process_each_pv_in_vg(cmd, vg, handle, &_pvs_single);
			break;
		case PVSEGS:
			r = process_each_pv_in_vg(cmd, vg, handle,
						  do_lv_info && !do_lv_seg_status ? &_pvsegs_with_lv_info_single :
						  !do_lv_info && do_lv_seg_status ? &_pvsegs_with_lv_status_single :
						  do_lv_info && do_lv_seg_status ? &_pvsegs_with_lv_info_and_status_single :
										   &_pvsegs_single);
			break;
		default:
			log_error(INTERNAL_ERROR "_report_all_in_vg: incorrect report type");
			break;
	}

	return r;
}

static int _report_all_in_lv(struct cmd_context *cmd, struct processing_handle *handle,
			     struct logical_volume *lv, report_type_t type,
			     int do_lv_info, int do_lv_seg_status)
{
	int r = 0;

	switch (type) {
		case LVS:
			r = _do_lvs_with_info_and_status_single(cmd, lv, do_lv_info, do_lv_seg_status, handle);
			break;
		case SEGS:
			r = process_each_segment_in_lv(cmd, lv, handle,
						       do_lv_info && !do_lv_seg_status ? &_segs_with_info_single :
							       !do_lv_info && do_lv_seg_status ? &_segs_with_status_single :
							       do_lv_info && do_lv_seg_status ? &_segs_with_info_and_status_single :
												&_segs_single);
			break;
		default:
			log_error(INTERNAL_ERROR "_report_all_in_lv: incorrect report type");
			break;
	}

	return r;
}

static int _report_all_in_pv(struct cmd_context *cmd, struct processing_handle *handle,
			     struct physical_volume *pv, report_type_t type,
			     int do_lv_info, int do_lv_seg_status)
{
	int r = 0;

	switch (type) {
		case PVS:
			r = _pvs_single(cmd, pv->vg, pv, handle);
			break;
		case PVSEGS:
			r = process_each_segment_in_pv(cmd, pv->vg, pv, handle,
						       do_lv_info && !do_lv_seg_status ? &_pvsegs_with_lv_info_sub_single :
						       !do_lv_info && do_lv_seg_status ? &_pvsegs_with_lv_status_sub_single :
						       do_lv_info && do_lv_seg_status ? &_pvsegs_with_lv_info_and_status_sub_single :
											&_pvsegs_sub_single);
			break;
		default:
			log_error(INTERNAL_ERROR "_report_all_in_pv: incorrect report type");
			break;
	}

	return r;
}

int report_for_selection(struct cmd_context *cmd,
			 struct processing_handle *parent_handle,
			 struct physical_volume *pv,
			 struct volume_group *vg,
			 struct logical_volume *lv)
{
	struct selection_handle *sh = parent_handle->selection_handle;
	int args_are_pvs = sh->orig_report_type == PVS;
	int do_lv_info, do_lv_seg_status;
	struct processing_handle *handle;
	int r = 0;

	if (!_get_final_report_type(args_are_pvs,
				    sh->orig_report_type | sh->report_type,
				    &do_lv_info,
				    &do_lv_seg_status,
				    &sh->report_type))
		return_0;

	if (!(handle = init_processing_handle(cmd)))
		return_0;

	/*
	 * We're already reporting for select so override
	 * internal_report_for_select to 0 as we can call
	 * process_each_* functions again and we could
	 * end up in an infinite loop if we didn't stop
	 * internal reporting for select right here.
	 *
	 * So the overall call trace from top to bottom looks like this:
	 *
	 * process_each_* (top-level one, using processing_handle with internal reporting enabled and selection_handle) ->
	 *   select_match_*(processing_handle with selection_handle) ->
	 *     report for selection ->
	 *     	 (creating new processing_handle here with internal reporting disabled!!!)
	 *       reporting_fn OR process_each_* (using *new* processing_handle with original selection_handle) 
	 *
	 * The selection_handle is still reused so we can track
	 * whether any of the items the top-level one is composed
	 * of are still selected or not unerneath. Do not destroy
	 * this selection handle - it needs to be passed to upper
	 * layers to check the overall selection status.
	 */
	handle->internal_report_for_select = 0;
	handle->selection_handle = sh;

	/*
	 * Remember:
	 *   sh->orig_report_type is the original report type requested (what are we selecting? PV/VG/LV?)
	 *   sh->report_type is the report type actually used (it counts with all types of fields used in selection criteria)
	 */
	switch (sh->orig_report_type) {
		case LVS:
			r = _report_all_in_lv(cmd, handle, lv, sh->report_type, do_lv_info, do_lv_seg_status);
			break;
		case VGS:
			r = _report_all_in_vg(cmd, handle, vg, sh->report_type, do_lv_info, do_lv_seg_status);
			break;
		case PVS:
			r = _report_all_in_pv(cmd, handle, pv, sh->report_type, do_lv_info, do_lv_seg_status);
			break;
		default:
			log_error(INTERNAL_ERROR "report_for_selection: incorrect report type");
			break;
	}

	/*
	 * Keep the selection handle provided from the caller -
	 * do not destroy it - the caller will still use it to
	 * pass the result through it to layers above.
	 */
	handle->selection_handle = NULL;
	destroy_processing_handle(cmd, handle);
	return r;
}

static void _check_pv_list(struct cmd_context *cmd, struct report_args *args)
{
	unsigned i;
	int rescan_done = 0;

	if (!args->argv)
		return;

	args->args_are_pvs = (args->report_type == PVS ||
			     args->report_type == LABEL ||
			     args->report_type == PVSEGS) ? 1 : 0;

	if (args->args_are_pvs && args->argc) {
		for (i = 0; i < args->argc; i++) {
			if (!rescan_done && !dev_cache_get(args->argv[i], cmd->full_filter)) {
				cmd->filter->wipe(cmd->filter);
				/* FIXME scan only one device */
				lvmcache_label_scan(cmd);
				rescan_done = 1;
			}
			if (*args->argv[i] == '@') {
				/*
				 * Tags are metadata related, not label
				 * related, change report type accordingly!
				 */
				if (args->report_type == LABEL)
					args->report_type = PVS;
				/*
				 * If we changed the report_type and we did rescan,
				 * no need to iterate over dev list further - nothing
				 * else would change.
				 */
				if (rescan_done)
					break;
			}
		}
	}
}

static void _del_option_from_list(struct dm_list *sll, const char *prefix,
				  size_t prefix_len, const char *str)
{
	struct dm_list *slh;
	struct dm_str_list *sl;
	const char *a = str, *b;

	prefix_len--;
	dm_list_uniterate(slh, sll, sll) {
		sl = dm_list_item(slh, struct dm_str_list);

		/* exact match */
		if (!strcmp(str, sl->str)) {
			dm_list_del(slh);
			return;
		}

		/* also try to match with known prefix */
		b = sl->str;
		if (!strncmp(prefix, a, prefix_len)) {
			a += prefix_len;
			if (*a == '_')
				a++;
		}
		if (!strncmp(prefix, b, prefix_len)) {
			b += prefix_len;
			if (*b == '_')
				b++;
		}
		if (!strcmp(a, b)) {
			dm_list_del(slh);
			return;
		}
	}
}

static int _get_report_options(struct cmd_context *cmd,
			       struct report_args *args)
{
	const char *prefix = report_get_field_prefix(args->report_type);
	size_t prefix_len = strlen(prefix);
	struct arg_value_group_list *current_group;
	struct dm_list *final_opts_list;
	struct dm_list *final_compact_list = NULL;
	struct dm_list *opts_list = NULL;
	struct dm_str_list *sl;
	const char *opts;
	struct dm_pool *mem;
	int r = ECMD_PROCESSED;

	if (!(mem = dm_pool_create("report_options", 128))) {
		log_error("Failed to create temporary mempool to process report options.");
		return ECMD_FAILED;
	}

	if (!(final_opts_list = str_to_str_list(mem, args->options, ",", 1))) {
		r = ECMD_FAILED;
		goto_out;
	}

	dm_list_iterate_items(current_group, &cmd->arg_value_groups) {
		if (!grouped_arg_is_set(current_group->arg_values, options_ARG))
			continue;

		opts = grouped_arg_str_value(current_group->arg_values, options_ARG, NULL);
		if (!opts || !*opts) {
			log_error("Invalid options string: %s", opts);
			r = EINVALID_CMD_LINE;
			goto out;
		}

		switch (*opts) {
			case '+':
				/* fall through */
			case '-':
				/* fall through */
			case '#':
				if (!(opts_list = str_to_str_list(mem, opts + 1, ",", 1))) {
					r = ECMD_FAILED;
					goto_out;
				}
				if (*opts == '+') {
					dm_list_splice(final_opts_list, opts_list);
				} else if (*opts == '-') {
					dm_list_iterate_items(sl, opts_list)
						_del_option_from_list(final_opts_list, prefix,
								      prefix_len, sl->str);
				} else if (*opts == '#') {
					if (!final_compact_list)
						final_compact_list = opts_list;
					else
						dm_list_splice(final_compact_list, opts_list);
				}
				break;
			default:
				if (!(final_opts_list = str_to_str_list(mem, opts, ",", 1))) {
					r = ECMD_FAILED;
					goto_out;
				}
		}
	}

	if (!(args->options = str_list_to_str(cmd->mem, final_opts_list, ","))) {
		r = ECMD_FAILED;
		goto_out;
	}
	if (final_compact_list &&
	    !(args->fields_to_compact = str_list_to_str(cmd->mem, final_compact_list, ","))) {
		dm_pool_free(cmd->mem, (char *) args->options);
		r = ECMD_FAILED;
		goto_out;
	}
out:
	dm_pool_destroy(mem);

	return r;
}

static int _do_report(struct cmd_context *cmd, struct report_args *args)
{
	struct processing_handle *handle = NULL;
	report_type_t report_type = args->report_type;
	void *report_handle = NULL;
	int lock_global = 0;
	int lv_info_needed;
	int lv_segment_status_needed;
	int r = ECMD_FAILED;

	if (!(handle = init_processing_handle(cmd)))
		goto_out;

	if (!(report_handle = report_init(cmd, args->options, args->keys, &report_type,
					  args->separator, args->aligned, args->buffered,
					  args->headings, args->field_prefixes, args->quoted,
					  args->columns_as_rows, args->selection)))
		goto_out;

	handle->internal_report_for_select = 0;
	handle->include_historical_lvs = cmd->include_historical_lvs;
	handle->custom_handle = report_handle;

	if (!_get_final_report_type(args->args_are_pvs,
				    report_type, &lv_info_needed,
				    &lv_segment_status_needed,
				    &report_type))
		goto_out;

	/*
	 * We lock VG_GLOBAL to enable use of metadata cache.
	 * This can pause alongide pvscan or vgscan process for a while.
	 */
	if (args->args_are_pvs && (report_type == PVS || report_type == PVSEGS) &&
	    !lvmetad_used()) {
		lock_global = 1;
		if (!lock_vol(cmd, VG_GLOBAL, LCK_VG_READ, NULL)) {
			log_error("Unable to obtain global lock.");
			goto out;
		}
	}

	switch (report_type) {
		case DEVTYPES:
			r = _process_each_devtype(cmd, args->argc, handle);
			break;
		case LVSINFO:
			/* fall through */
		case LVSSTATUS:
			/* fall through */
		case LVSINFOSTATUS:
			/* fall through */
		case LVS:
			r = process_each_lv(cmd, args->argc, args->argv, NULL, NULL, 0, handle,
					    lv_info_needed && !lv_segment_status_needed ? &_lvs_with_info_single :
					    !lv_info_needed && lv_segment_status_needed ? &_lvs_with_status_single :
					    lv_info_needed && lv_segment_status_needed ? &_lvs_with_info_and_status_single :
											 &_lvs_single);
			break;
		case VGS:
			r = process_each_vg(cmd, args->argc, args->argv, NULL, NULL, 0,
					    handle, &_vgs_single);
			break;
		case LABEL:
			r = process_each_label(cmd, args->argc, args->argv,
					       handle, &_label_single);
			break;
		case PVS:
			if (args->args_are_pvs)
				r = process_each_pv(cmd, args->argc, args->argv, NULL,
						    arg_is_set(cmd, all_ARG), 0,
						    handle, &_pvs_single);
			else
				r = process_each_vg(cmd, args->argc, args->argv, NULL, NULL,
						    0, handle, &_pvs_in_vg);
			break;
		case SEGS:
			r = process_each_lv(cmd, args->argc, args->argv, NULL, NULL, 0, handle,
					    lv_info_needed && !lv_segment_status_needed ? &_lvsegs_with_info_single :
					    !lv_info_needed && lv_segment_status_needed ? &_lvsegs_with_status_single :
					    lv_info_needed && lv_segment_status_needed ? &_lvsegs_with_info_and_status_single :
											 &_lvsegs_single);
			break;
		case PVSEGS:
			if (args->args_are_pvs)
				r = process_each_pv(cmd, args->argc, args->argv, NULL,
						    arg_is_set(cmd, all_ARG), 0,
						    handle,
						    lv_info_needed && !lv_segment_status_needed ? &_pvsegs_with_lv_info_single :
						    !lv_info_needed && lv_segment_status_needed ? &_pvsegs_with_lv_status_single :
						    lv_info_needed && lv_segment_status_needed ? &_pvsegs_with_lv_info_and_status_single :
												 &_pvsegs_single);
			else
				r = process_each_vg(cmd, args->argc, args->argv, NULL, NULL,
						    0, handle, &_pvsegs_in_vg);
			break;
		case CMDSTATUS:
			/* Status is reported throughout the code via report_cmdstatus calls. */
			break;
		default:
			log_error(INTERNAL_ERROR "_do_report: unknown report type.");
			return 0;
	}

	if (find_config_tree_bool(cmd, report_compact_output_CFG, NULL)) {
		if (!dm_report_compact_fields(report_handle))
			log_error("Failed to compact report output.");
	} else if (args->fields_to_compact) {
		if (!dm_report_compact_given_fields(report_handle, args->fields_to_compact))
			log_error("Failed to compact given columns in report output.");
	}

	dm_report_output(report_handle);

	if (lock_global)
		unlock_vg(cmd, VG_GLOBAL);
out:
	if (handle)
		destroy_processing_handle(cmd, handle);
	if (report_handle)
		dm_report_free(report_handle);
	return r;
}

static int _config_report(struct cmd_context *cmd, struct report_args *args)
{
	args->aligned = find_config_tree_bool(cmd, report_aligned_CFG, NULL);
	args->buffered = find_config_tree_bool(cmd, report_buffered_CFG, NULL);
	args->headings = find_config_tree_bool(cmd, report_headings_CFG, NULL);
	args->separator = find_config_tree_str(cmd, report_separator_CFG, NULL);
	args->field_prefixes = find_config_tree_bool(cmd, report_prefixes_CFG, NULL);
	args->quoted = find_config_tree_bool(cmd, report_quoted_CFG, NULL);
	args->columns_as_rows = find_config_tree_bool(cmd, report_colums_as_rows_CFG, NULL);

	/* Check PV specifics and do extra changes/actions if needed. */
	_check_pv_list(cmd, args);

	switch (args->report_type) {
		case DEVTYPES:
			args->keys = find_config_tree_str(cmd, report_devtypes_sort_CFG, NULL);
			if (!arg_count(cmd, verbose_ARG))
				args->options = find_config_tree_str(cmd, report_devtypes_cols_CFG, NULL);
			else
				args->options = find_config_tree_str(cmd, report_devtypes_cols_verbose_CFG, NULL);
			break;
		case LVS:
			args->keys = find_config_tree_str(cmd, report_lvs_sort_CFG, NULL);
			if (!arg_count(cmd, verbose_ARG))
				args->options = find_config_tree_str(cmd, report_lvs_cols_CFG, NULL);
			else
				args->options = find_config_tree_str(cmd, report_lvs_cols_verbose_CFG, NULL);
			break;
		case VGS:
			args->keys = find_config_tree_str(cmd, report_vgs_sort_CFG, NULL);
			if (!arg_count(cmd, verbose_ARG))
				args->options = find_config_tree_str(cmd, report_vgs_cols_CFG, NULL);
			else
				args->options = find_config_tree_str(cmd, report_vgs_cols_verbose_CFG, NULL);
			break;
		case LABEL:
		case PVS:
			args->keys = find_config_tree_str(cmd, report_pvs_sort_CFG, NULL);
			if (!arg_count(cmd, verbose_ARG))
				args->options = find_config_tree_str(cmd, report_pvs_cols_CFG, NULL);
			else
				args->options = find_config_tree_str(cmd, report_pvs_cols_verbose_CFG, NULL);
			break;
		case SEGS:
			args->keys = find_config_tree_str(cmd, report_segs_sort_CFG, NULL);
			if (!arg_count(cmd, verbose_ARG))
				args->options = find_config_tree_str(cmd, report_segs_cols_CFG, NULL);
			else
				args->options = find_config_tree_str(cmd, report_segs_cols_verbose_CFG, NULL);
			break;
		case PVSEGS:
			args->keys = find_config_tree_str(cmd, report_pvsegs_sort_CFG, NULL);
			if (!arg_count(cmd, verbose_ARG))
				args->options = find_config_tree_str(cmd, report_pvsegs_cols_CFG, NULL);
			else
				args->options = find_config_tree_str(cmd, report_pvsegs_cols_verbose_CFG, NULL);
			break;
		case CMDSTATUS:
			args->keys = find_config_tree_str(cmd, report_status_sort_CFG, NULL);
			args->options = find_config_tree_str(cmd, report_status_cols_CFG, NULL);
			break;
		default:
			log_error(INTERNAL_ERROR "_report: unknown report type.");
			return 0;
	}

	/* If -o supplied use it, else use default for report_type */
	if (arg_count(cmd, options_ARG) &&
	    (_get_report_options(cmd, args) != ECMD_PROCESSED))
		return_0;

	if (!args->fields_to_compact)
		args->fields_to_compact = find_config_tree_str_allow_empty(cmd, report_compact_output_cols_CFG, NULL);

	/* -O overrides default sort settings */
	args->keys = arg_str_value(cmd, sort_ARG, args->keys);

	args->separator = arg_str_value(cmd, separator_ARG, args->separator);
	if (arg_count(cmd, separator_ARG))
		args->aligned = 0;
	if (arg_count(cmd, aligned_ARG))
		args->aligned = 1;
	if (arg_count(cmd, unbuffered_ARG) && !arg_count(cmd, sort_ARG))
		args->buffered = 0;
	if (arg_count(cmd, noheadings_ARG))
		args->headings = 0;
	if (arg_count(cmd, nameprefixes_ARG)) {
		args->aligned = 0;
		args->field_prefixes = 1;
	}
	if (arg_count(cmd, unquoted_ARG))
		args->quoted = 0;
	if (arg_count(cmd, rows_ARG))
		args->columns_as_rows = 1;

	if (arg_count(cmd, select_ARG))
		args->selection = arg_str_value(cmd, select_ARG, NULL);

	return 1;
}

static int _report(struct cmd_context *cmd, int argc, char **argv, report_type_t report_type)
{
	struct report_args args = {0};

	/*
	 * Include foreign VGs that contain active LVs.
	 * That shouldn't happen in general, but if it does by some
	 * mistake, then we want to display those VGs and allow the
	 * LVs to be deactivated.
	 */
	cmd->include_active_foreign_vgs = 1;

	args.argc = argc;
	args.argv = argv;
	args.report_type = report_type;

	if (!_config_report(cmd, &args))
		return_ECMD_FAILED;

	return _do_report(cmd, &args);
}

int lvs(struct cmd_context *cmd, int argc, char **argv)
{
	report_type_t type;

	if (arg_count(cmd, segments_ARG))
		type = SEGS;
	else
		type = LVS;

	return _report(cmd, argc, argv, type);
}

int vgs(struct cmd_context *cmd, int argc, char **argv)
{
	return _report(cmd, argc, argv, VGS);
}

int pvs(struct cmd_context *cmd, int argc, char **argv)
{
	report_type_t type;

	if (arg_count(cmd, segments_ARG))
		type = PVSEGS;
	else
		type = LABEL;

	return _report(cmd, argc, argv, type);
}

int devtypes(struct cmd_context *cmd, int argc, char **argv)
{
	return _report(cmd, argc, argv, DEVTYPES);
}
