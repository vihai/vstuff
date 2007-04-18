/*
 * Userland Kstreamer interface
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBKSTREAMER_PD_TOKENIZER_H
#define _LIBKSTREAMER_PD_TOKENIZER_H

#include "util.h"

#include <list.h>

#define KS_AUTOROUTE_TOKEN "_autoroute_"

struct ks_pd_token
{
	struct list_head node;

	int id;
	char *text;
};

struct ks_pd_parameter
{
	struct list_head node;

	struct ks_pd_token *name;
	struct ks_pd_token *value;
};

struct ks_pd_parameters
{
	struct list_head list;
};

struct ks_pd_channel
{
	struct list_head node;

	struct ks_pd_token *name;
	struct ks_pd_parameters *parameters;
};

struct ks_pd_channels
{
	struct list_head list;
};

struct ks_pd_pipeline
{
	struct ks_pd_token *ep1;
	struct ks_pd_token *ep2;
	struct ks_pd_channels *channels;
};

struct ks_pd
{
	struct ks_pd_pipeline *pipeline;

	KSBOOL failed;
	const char *failure_reason;
};

struct ks_pd *ks_pd_parse(const char *str);

struct ks_conn;
void ks_pd_dump(struct ks_pd *pd, struct ks_conn *conn, int level);

void ks_pd_token_destroy(struct ks_pd_token *token);

#endif
