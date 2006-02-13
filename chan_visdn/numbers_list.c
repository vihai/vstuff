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

#include <stdlib.h>

#include <asterisk/pbx.h>

#include <list.h>

#include "util.h"
#include "numbers_list.h"

void visdn_numbers_list_flush(struct list_head *list)
{
	struct visdn_number *num, *t;
	list_for_each_entry_safe(num, t, list, node) {
		list_del(&num->node);
		free(num);
	}
}

void visdn_numbers_list_copy(
	struct list_head *dst,
	const struct list_head *src)
{
	visdn_numbers_list_flush(dst);

	struct visdn_number *num;
	list_for_each_entry(num, src, node) {

		struct visdn_number *num2;

		num2 = malloc(sizeof(*num2));
		strcpy(num2->number, num->number);
		list_add_tail(&num2->node, dst);
	}
}

int visdn_numbers_list_match(
	struct list_head *list,
	const char *number)
{
	struct visdn_number *num;
	list_for_each_entry(num, list, node) {
		if (ast_extension_match(num->number, number))
			return TRUE;
	}

	return FALSE;
}

void visdn_numbers_list_from_string(
	struct list_head *list,
	const char *value)
{
	char *str = strdup(value);
	char *strpos = str;
	char *tok;

	struct visdn_number *num, *t;
	list_for_each_entry_safe(num, t, list, node) {
		list_del(&num->node);
		free(num);
	}

	while ((tok = strsep(&strpos, ","))) {
		while(*tok == ' ' || *tok == '\t')
			tok++;

		while(*(tok + strlen(tok) - 1) == ' ' ||
			*(tok + strlen(tok) - 1) == '\t')
			*(tok + strlen(tok) - 1) = '\0';

		struct visdn_number *num;
		num = malloc(sizeof(*num));
		memset(num, 0, sizeof(num));

		strncpy(num->number, tok, sizeof(num->number));

		list_add_tail(&num->node, list);
	}

	free(str);
}

