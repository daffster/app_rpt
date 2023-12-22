
#include "asterisk.h"

#include <dahdi/user.h>

#include "asterisk/channel.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */

#include "app_rpt.h"

#include "rpt_bridging.h"
#include "rpt_call.h"

static const char *rpt_chan_type_str(enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "rxchan";
	case RPT_TXCHAN:
		return "txchan";
	case RPT_PCHAN:
		return "pchan";
	case RPT_DAHDITXCHAN:
		return "dahditxchan";
	case RPT_MONCHAN:
		return "monchan";
	case RPT_PARROTCHAN:
		return "parrotchan";
	case RPT_TELECHAN:
		return "telechan";
	case RPT_BTELECHAN:
		return "btelechan";
	case RPT_VOXCHAN:
		return "voxchan";
	case RPT_TXPCHAN:
		return "txpchan";
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_name(struct rpt *myrpt, enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return myrpt->rxchanname;
	case RPT_TXCHAN:
		return myrpt->txchanname;
	case RPT_PCHAN:
	case RPT_DAHDITXCHAN:
	case RPT_MONCHAN:
	case RPT_PARROTCHAN:
	case RPT_TELECHAN:
	case RPT_BTELECHAN:
	case RPT_VOXCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

static struct ast_channel **rpt_chan_channel(struct rpt *myrpt, enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return &myrpt->rxchannel;
	case RPT_TXCHAN:
		return &myrpt->txchannel;
	case RPT_PCHAN:
		return &myrpt->pchannel;
	case RPT_DAHDITXCHAN:
		return &myrpt->dahditxchannel;
	case RPT_MONCHAN:
		return &myrpt->monchannel;
	case RPT_PARROTCHAN:
		return &myrpt->parrotchannel;
	case RPT_TELECHAN:
		return &myrpt->telechannel;
	case RPT_BTELECHAN:
		return &myrpt->btelechannel;
	case RPT_VOXCHAN:
		return &myrpt->voxchannel;
	case RPT_TXPCHAN:
		return &myrpt->txpchannel;
	}
	ast_assert(0);
	return NULL;
}

#define RPT_DIAL_TIME 999

void rpt_hangup(struct rpt *myrpt, enum rpt_chan_type chantype)
{
	struct ast_channel **chanptr = rpt_chan_channel(myrpt, chantype);

	if (!*chanptr) {
		ast_log(LOG_WARNING, "No %s channel to hang up\n", rpt_chan_type_str(chantype));
		return;
	}

	/* If RXCHAN == TXCHAN, and we hang up one, also NULL out the other one */

	switch (chantype) {
	case RPT_RXCHAN:
		if (myrpt->txchannel && myrpt->txchannel == *chanptr) {
			ast_debug(2, "Also resetting txchannel\n");
			myrpt->txchannel = NULL;
		}
		break;
	case RPT_TXCHAN:
		if (myrpt->rxchannel && myrpt->rxchannel == *chanptr) {
			ast_debug(2, "Also resetting rxchannel\n");
			myrpt->rxchannel = NULL;
		}
		break;
	default:
		break;
	}

	ast_debug(2, "Hanging up channel %s\n", ast_channel_name(*chanptr));
	ast_hangup(*chanptr);
	*chanptr = NULL;
}

static const char *rpt_chan_app(enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "(Repeater Rx)";
	case RPT_TXCHAN:
		return "(Repeater Tx)";
	case RPT_PCHAN:
	case RPT_DAHDITXCHAN:
	case RPT_MONCHAN:
	case RPT_PARROTCHAN:
	case RPT_TELECHAN:
	case RPT_BTELECHAN:
	case RPT_VOXCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_app_data(enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "Rx";
	case RPT_TXCHAN:
		return "Tx";
	case RPT_PCHAN:
	case RPT_DAHDITXCHAN:
	case RPT_MONCHAN:
	case RPT_PARROTCHAN:
	case RPT_TELECHAN:
	case RPT_BTELECHAN:
	case RPT_VOXCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

int rpt_request(struct rpt *myrpt, struct ast_format_cap *cap, enum rpt_chan_type chantype)
{
	char chanstr[256];
	const char *channame;
	struct ast_channel *chan, **chanptr;
	char *tech, *device;

	channame = rpt_chan_name(myrpt, chantype);

	if (ast_strlen_zero(channame)) {
		ast_log(LOG_WARNING, "No %s specified\n", rpt_chan_type_str(chantype));
		return -1;
	}

	ast_copy_string(chanstr, channame, sizeof(chanstr));

	device = chanstr;
	tech = strsep(&device, "/");

	if (ast_strlen_zero(device)) {
		ast_log(LOG_ERROR, "%s device format must be tech/device\n", rpt_chan_type_str(chantype));
		return -1;
	}

	chan = ast_request(tech, cap, NULL, NULL, device, NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request %s/%s\n", tech, device);
		return -1;
	}

	if (ast_channel_state(chan) == AST_STATE_BUSY) {
		ast_log(LOG_ERROR, "Requested channel %s is busy?\n", ast_channel_name(chan));
		ast_hangup(chan);
		return -1;
	}

	rpt_make_call(chan, device, RPT_DIAL_TIME, tech, rpt_chan_app(chantype), rpt_chan_app_data(chantype), myrpt->name);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_log(LOG_ERROR, "Requested channel %s not up?\n", ast_channel_name(chan));
		ast_hangup(chan);
		return -1;
	}

	chanptr = rpt_chan_channel(myrpt, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_RXCHAN:
		myrpt->dahdirxchannel = !strcasecmp(tech, "DAHDI") ? chan : NULL;
		break;
	case RPT_TXCHAN:
		myrpt->dahditxchannel = !strcasecmp(tech, "DAHDI") && strcasecmp(device, "pseudo") ? chan : NULL;
		break;
	default:
		break;
	}

	return 0;
}

int rpt_request_pseudo(struct rpt *myrpt, struct ast_format_cap *cap, enum rpt_chan_type chantype)
{
	struct ast_channel *chan, **chanptr;

	chan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request pseudo channel\n");
		return -1;
	}

	ast_debug(1, "Requested channel %s\n", ast_channel_name(chan));

	/* A subset of what rpt_make_call does... */
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	rpt_disable_cdr(chan);
	ast_answer(chan);

	chanptr = rpt_chan_channel(myrpt, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_PCHAN:
		if (!myrpt->dahdirxchannel) {
			myrpt->dahdirxchannel = chan;
		}
		break;
	default:
		break;
	}

	return 0;
}

// todo: remove FAILED_TO_OBTAIN_PSEUDO_CHANNEL
