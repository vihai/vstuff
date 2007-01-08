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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/netlink.h>

#include "util.h"
#include "logging.h"

#include "pd_grammar.h"
#include "pd_parser.h"

static inline KSBOOL isidentstart(char c)
{
	return isalpha(c) || c == '_';
}

static inline KSBOOL isident(char c)
{
	return isalnum(c) || c == '_';
}

static struct ks_pd_token *get_token(
	const char *buf,
	int *nreadp)
{
	int c;

	int nread = 1;
	int size = 1;
	const char *start = buf;

	struct ks_pd_token *token = malloc(sizeof(*token));
	if (!token) {
		*nreadp = 0;
		return NULL;
	}

	switch (*buf) {
	case ' ':
	case '\t':
	case '\n':
	case '\f':
	case '\r': {
		int i;
		for(i=1; isspace(buf[i]); i++) {}

		token->id = TK_SPACE;

		nread = i;
		size = i;
	}
	break;

	case '(':
		token->id = TK_LPAREN;
	break;

	case ')':
		token->id = TK_RPAREN;
	break;

	case '=':
		token->id = TK_EQ;
		nread = 1 + (buf[1] == '=');
		size = nread;
	break;

	case '>':
		token->id = TK_GT;
	break;

	case ',':
		token->id = TK_COMMA;
	break;

	case '\'':
	case '"': {
		int delim = buf[0];
		int i;

		for(i=1; (c = buf[i]); i++) {

			if (c == delim) {
				if (buf[i+1] == delim)
					i++;
				else
					break;
			}
		}

		if(!c) {
			token->id = TK_ILLEGAL;
			break;
		}

		token->id = TK_STRING;
		start++;

		nread = i + 1;
		size = i - 1;
	}
	break;

	case '0': {
		int i = 0;

		if (buf[i + 1] == 'x' || buf[i + 1] == 'X') {

			token->id = TK_HEXINT;

			for (i=2; isxdigit(buf[i]); i++);

			if(isalpha(buf[i]) || buf[i] == '_')
				token->id = TK_ILLEGAL;

		} else if (isdigit(buf[i + 1])) {
			// OCTAL
			token->id = TK_ILLEGAL;
		} else {
			token->id = TK_ILLEGAL;
		}

		nread = i;
		size = nread;
	}
	break;

	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9': {
		int i;

		token->id = TK_INTEGER;

		for (i=0; isdigit(buf[i]); i++);

		if(isalpha(buf[i]) || buf[i] == '_')
			token->id = TK_ILLEGAL;

		size = nread;
		nread = i;
	}
	break;

	default: {
		if(!isidentstart(*buf)) {
			token->id = TK_ILLEGAL;
			break;
		}

		int i;
		for(i=1; isident(buf[i]); i++);

		token->id = TK_IDENTIFIER;
		nread = i;
		size = nread;
	}
	break;
	}

	token->text = strndup(start, size);
	if (!token->text) {
		*nreadp = 0;
		free(token);

		return NULL;
	}

	*nreadp = nread;

	return token;
}

extern void *ks_pd_parser_Alloc(void *(*malloc)(size_t));
extern void *ks_pd_parser_Free(void *parser, void (*free)(void *));
extern void *ks_pd_parser_(
	void *parser,
	int token_id,
	struct ks_pd_token *token,
	struct ks_pd *pd);

void ks_pd_token_destroy(struct ks_pd_token *token)
{
	if (token->text)
		free(token->text);

	free(token);
}

void ks_pd_dump(struct ks_pd *pd)
{
	if (pd->failed) {
		printf("FAILED RESULT!\n");
		return;
	}

	struct ks_pd_pipeline *pipeline = pd->pipeline;

	printf("EP1 = %s\n", pipeline->ep1->text);

	struct ks_pd_channel *chan;
	list_for_each_entry(chan, &pipeline->channels->list, node) {

		printf("CHAN = %s\n", chan->name->text);

		if (!chan->parameters)
			continue;

		struct ks_pd_parameter *par;
		list_for_each_entry(par, &chan->parameters->list, node) {
			printf("  PAR = %s=%s\n",
				par->name->text,
				par->value->text);
		}
	}
	printf("EP2 = %s\n", pipeline->ep2->text);
}

struct ks_pd *ks_pd_parse(const char *str)
{
	struct ks_pd *pd;

	pd = malloc(sizeof(*pd));
	if (!pd)
		return NULL;

	memset(pd, 0, sizeof(*pd));

	void *parser = ks_pd_parser_Alloc(malloc);
	if (!parser)
		return NULL;

//	ks_pd_parser_Trace(stdout, "ksps: ");

	const char *pos = str;
	while(*pos) {
		struct ks_pd_token *token;
		int nread;

		token = get_token(pos, &nread);
		if (!token)
			return NULL;

		pos += nread;

		if (token->id == TK_SPACE)
			continue;
		else if (token->id == TK_ILLEGAL)
			break;

		ks_pd_parser_(parser, token->id, token, pd);

		if (pd->failed)
			break;
	}

	ks_pd_parser_(parser, 0, NULL, pd);

	ks_pd_parser_Free(parser, free);

	return pd;
}
