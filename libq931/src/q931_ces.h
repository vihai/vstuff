#ifndef _Q931_CES_H
#define _Q931_CES_H


// Connection endpoint suffix
struct q931_ces
{
	struct list_head node;

	struct q931_call *call;
	struct q931_dlc *dlc;
	enum q931_network_state state;
};

void q931_ces_dispatch_message(
	struct q931_call *call,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt);

struct q931_ces *q931_ces_alloc(struct q931_call *call);
void q931_ces_free(struct q931_ces *ces);

void q931_ces_alerting_request(struct q931_ces *ces);
void q931_ces_connect_request(struct q931_ces *ces);
void q931_ces_call_proceeding_reques(struct q931_ces *ces);
void q931_ces_setup_ack_request(struct q931_ces *ces);
void q931_ces_release_request(struct q931_ces *ces);
void q931_ces_info_request(struct q931_ces *ces);

#endif
