/*
 * vISDN LAPD/q.921 protocol implementation
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LAPD_DATALINK_H
#define _LAPD_DATALINK_H

enum lapd_mdl_primitive_type
{
	LAPD_MDL_ASSIGN_REQUEST,
	LAPD_MDL_REMOVE_REQUEST,
	LAPD_MDL_ERROR_RESPONSE,
	LAPD_MDL_ERROR_INDICATION,
	LAPD_MDL_PERSISTENT_DEACTIVATION,
};

struct lapd_mdl_primitive
{
	enum lapd_mdl_primitive_type type;
	int param;
};

void lapd_mdl_primitive(
	struct lapd_sock *lapd_sock,
	enum lapd_mdl_primitive_type type,
	int param);

void lapd_datalink_state_init(struct lapd_sock *lapd_sock);

int lapd_dl_establish_request(struct lapd_sock *lapd_sock);
int lapd_dl_release_request(struct lapd_sock *lapd_sock);
void lapd_dl_data_request(struct lapd_sock *lapd_sock, struct sk_buff *skb);
void lapd_dl_unit_data_request(
		struct lapd_sock *lapd_sock,
		struct sk_buff *skb);

void lapd_persistent_deactivation(struct lapd_sock *lapd_sock);
void lapd_mdl_assign_request(struct lapd_sock *lapd_sock, int tei);
void lapd_mdl_remove_request(struct lapd_sock *lapd_sock);
void lapd_mdl_error_response(struct lapd_sock *lapd_sock);

int lapd_dlc_recv(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb);

#endif
