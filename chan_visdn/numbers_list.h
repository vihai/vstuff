/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _NUMBERS_LIST_H
#define _NUMBERS_LIST_H

struct visdn_number
{
	struct list_head node;
	char number[32];
};

void visdn_numbers_list_flush(struct list_head *list);
void visdn_numbers_list_copy(
	struct list_head *dst,
	const struct list_head *src);
int visdn_numbers_list_match(
	struct list_head *list,
	const char *number);

void visdn_numbers_list_from_string(
	struct list_head *list,
	const char *value);

#endif
