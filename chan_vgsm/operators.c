/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "../config.h"

#include <asterisk/config.h>

#include "util.h"
#include "chan_vgsm.h"
#include "operators.h"

struct vgsm_operator_info *vgsm_operators_search(__u16 mcc, __u16 mnc)
{
	struct vgsm_operator_info *op_info;

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry(op_info, &vgsm.op_list, node) {
		if (op_info->mcc == mcc &&
		    op_info->mnc == mnc) {

			ast_mutex_unlock(&vgsm.lock);

			return op_info;
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

struct vgsm_operator_country *vgsm_operators_country_search(__u16 mcc)
{
	struct vgsm_operator_country *op_country;

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry(op_country, &vgsm.op_countries_list, node) {
		if (op_country->mcc == mcc) {
			ast_mutex_unlock(&vgsm.lock);

			return op_country;
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

static void vgsm_operators_countries_init(void)
{
	struct ast_config *cfg;

	cfg = ast_config_load(VGSM_OP_COUNTRY_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s: %s\n",
			VGSM_OP_COUNTRY_CONFIG_FILE,
			strerror(errno));

		return;
	}

	struct vgsm_operator_country *op_country, *t;
	list_for_each_entry_safe(op_country, t, &vgsm.op_countries_list, node) {

		if (op_country->name)
			free(op_country->name);

		list_del(&op_country->node);
		free(op_country);
	}

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		struct vgsm_operator_info *op_country;
		op_country = malloc(sizeof(*op_country));
		memset(op_country, 0, sizeof(*op_country));

		op_country->mcc = atoi(cat);

		struct ast_variable *var;
		var = ast_variable_browse(cfg, (char *)cat);
		while (var) {
			if (!strcasecmp(var->name, "name"))
				op_country->name = strdup(var->value);
			else {
				ast_log(LOG_WARNING,
					"Unknown parameter '%s' in %s\n",
					var->name,
					VGSM_OP_CONFIG_FILE);
			}
			
			var = var->next;
		}

		list_add_tail(&op_country->node, &vgsm.op_countries_list);
	}

	ast_config_destroy(cfg);
}

static void vgsm_operators_info_init(void)
{
	struct ast_config *cfg;

	cfg = ast_config_load(VGSM_OP_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s: %s\n",
			VGSM_OP_CONFIG_FILE,
			strerror(errno));

		return;
	}

	struct vgsm_operator_info *op_info, *t;

	list_for_each_entry_safe(op_info, t, &vgsm.op_list, node) {

		if (op_info->name)
			free(op_info->name);

		if (op_info->name_short)
			free(op_info->name_short);

		if (op_info->date)
			free(op_info->date);

		if (op_info->bands)
			free(op_info->bands);

		list_del(&op_info->node);
		free(op_info);
	}

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		struct vgsm_operator_info *op_info;
		op_info = malloc(sizeof(*op_info));
		memset(op_info, 0, sizeof(*op_info));

		if (sscanf(cat, "%03hu%hu", &op_info->mcc, &op_info->mnc) < 2) {

			op_info->mcc = -1;
			op_info->mnc = -1;

			ast_log(LOG_WARNING,
				"Cannot parse operator ID '%s'\n",
				cat);
		}

		op_info->country = vgsm_operators_country_search(op_info->mcc);

		struct ast_variable *var;
		var = ast_variable_browse(cfg, (char *)cat);
		while (var) {
			if (!strcasecmp(var->name, "name"))
				op_info->name = strdup(var->value);
			else if (!strcasecmp(var->name, "name_short"))
				op_info->name_short = strdup(var->value);
			else if (!strcasecmp(var->name, "date"))
				op_info->date = strdup(var->value);
			else if (!strcasecmp(var->name, "bands"))
				op_info->bands = strdup(var->value);
			else {
				ast_log(LOG_WARNING,
					"Unknown parameter '%s' in %s\n",
					var->name,
					VGSM_OP_CONFIG_FILE);
			}
			
			var = var->next;
		}

		list_add_tail(&op_info->node, &vgsm.op_list);
	}

	ast_config_destroy(cfg);
}

void vgsm_operators_init(void)
{
	ast_mutex_lock(&vgsm.lock);
	vgsm_operators_countries_init();
	vgsm_operators_info_init();
	ast_mutex_unlock(&vgsm.lock);
}
