/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2015  Intel Mobile Communications GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */

#ifndef IEEE80211_I_H
#define IEEE80211_I_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/etherdevice.h>
#include <linux/leds.h>
#include <linux/idr.h>
#include <linux/rhashtable.h>
#include <linux/rbtree.h>
#include <kunit/visibility.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/fq.h>
#include "key.h"
#include "sta_info.h"
#include "debug.h"
#include "drop.h"

extern const struct cfg80211_ops mac80211_config_ops;

struct ieee80211_local;
struct ieee80211_mesh_fast_tx;

/* Maximum number of broadcast/multicast frames to buffer when some of the
 * associated stations are using power saving. */
#define AP_MAX_BC_BUFFER 128

/* Maximum number of frames buffered to all STAs, including multicast frames.
 * Note: increasing this limit increases the potential memory requirement. Each
 * frame can be up to about 2 kB long. */
#define TOTAL_MAX_TX_BUFFER 512

/* Required encryption head and tailroom */
#define IEEE80211_ENCRYPT_HEADROOM 8
#define IEEE80211_ENCRYPT_TAILROOM 18

/* power level hasn't been configured (or set to automatic) */
#define IEEE80211_UNSET_POWER_LEVEL	INT_MIN

/*
 * Some APs experience problems when working with U-APSD. Decreasing the
 * probability of that happening by using legacy mode for all ACs but VO isn't
 * enough.
 *
 * Cisco 4410N originally forced us to enable VO by default only because it
 * treated non-VO ACs as legacy.
 *
 * However some APs (notably Netgear R7000) silently reclassify packets to
 * different ACs. Since u-APSD ACs require trigger frames for frame retrieval
 * clients would never see some frames (e.g. ARP responses) or would fetch them
 * accidentally after a long time.
 *
 * It makes little sense to enable u-APSD queues by default because it needs
 * userspace applications to be aware of it to actually take advantage of the
 * possible additional powersavings. Implicitly depending on driver autotrigger
 * frame support doesn't make much sense.
 */
#define IEEE80211_DEFAULT_UAPSD_QUEUES 0

#define IEEE80211_DEFAULT_MAX_SP_LEN		\
	IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL

extern const u8 ieee80211_ac_to_qos_mask[IEEE80211_NUM_ACS];

#define IEEE80211_DEAUTH_FRAME_LEN	(24 /* hdr */ + 2 /* reason */)

#define IEEE80211_MAX_NAN_INSTANCE_ID 255

enum ieee80211_status_data {
	IEEE80211_STATUS_TYPE_MASK	= 0x00f,
	IEEE80211_STATUS_TYPE_INVALID	= 0,
	IEEE80211_STATUS_TYPE_SMPS	= 1,
	IEEE80211_STATUS_TYPE_NEG_TTLM	= 2,
	IEEE80211_STATUS_SUBDATA_MASK	= 0x1ff0,
};

static inline bool
ieee80211_sta_keep_active(struct sta_info *sta, u8 ac)
{
	/* Keep a station's queues on the active list for deficit accounting
	 * purposes if it was active or queued during the last 100ms.
	 */
	return time_before_eq(jiffies, sta->airtime[ac].last_active + HZ / 10);
}

struct ieee80211_bss {
	u32 device_ts_beacon, device_ts_presp;

	bool wmm_used;
	bool uapsd_supported;

#define IEEE80211_MAX_SUPP_RATES 32
	u8 supp_rates[IEEE80211_MAX_SUPP_RATES];
	size_t supp_rates_len;
	struct ieee80211_rate *beacon_rate;

	u32 vht_cap_info;

	/*
	 * During association, we save an ERP value from a probe response so
	 * that we can feed ERP info to the driver when handling the
	 * association completes. these fields probably won't be up-to-date
	 * otherwise, you probably don't want to use them.
	 */
	bool has_erp_value;
	u8 erp_value;

	/* Keep track of the corruption of the last beacon/probe response. */
	u8 corrupt_data;

	/* Keep track of what bits of information we have valid info for. */
	u8 valid_data;
};

/**
 * enum ieee80211_bss_corrupt_data_flags - BSS data corruption flags
 * @IEEE80211_BSS_CORRUPT_BEACON: last beacon frame received was corrupted
 * @IEEE80211_BSS_CORRUPT_PROBE_RESP: last probe response received was corrupted
 *
 * These are bss flags that are attached to a bss in the
 * @corrupt_data field of &struct ieee80211_bss.
 */
enum ieee80211_bss_corrupt_data_flags {
	IEEE80211_BSS_CORRUPT_BEACON		= BIT(0),
	IEEE80211_BSS_CORRUPT_PROBE_RESP	= BIT(1)
};

/**
 * enum ieee80211_bss_valid_data_flags - BSS valid data flags
 * @IEEE80211_BSS_VALID_WMM: WMM/UAPSD data was gathered from non-corrupt IE
 * @IEEE80211_BSS_VALID_RATES: Supported rates were gathered from non-corrupt IE
 * @IEEE80211_BSS_VALID_ERP: ERP flag was gathered from non-corrupt IE
 *
 * These are bss flags that are attached to a bss in the
 * @valid_data field of &struct ieee80211_bss.  They show which parts
 * of the data structure were received as a result of an un-corrupted
 * beacon/probe response.
 */
enum ieee80211_bss_valid_data_flags {
	IEEE80211_BSS_VALID_WMM			= BIT(1),
	IEEE80211_BSS_VALID_RATES		= BIT(2),
	IEEE80211_BSS_VALID_ERP			= BIT(3)
};

typedef unsigned __bitwise ieee80211_tx_result;
#define TX_CONTINUE	((__force ieee80211_tx_result) 0u)
#define TX_DROP		((__force ieee80211_tx_result) 1u)
#define TX_QUEUED	((__force ieee80211_tx_result) 2u)

#define IEEE80211_TX_UNICAST		BIT(1)
#define IEEE80211_TX_PS_BUFFERED	BIT(2)

struct ieee80211_tx_data {
	struct sk_buff *skb;
	struct sk_buff_head skbs;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_key *key;
	struct ieee80211_tx_rate rate;

	unsigned int flags;
};

/**
 * enum ieee80211_packet_rx_flags - packet RX flags
 * @IEEE80211_RX_AMSDU: a-MSDU packet
 * @IEEE80211_RX_MALFORMED_ACTION_FRM: action frame is malformed
 * @IEEE80211_RX_DEFERRED_RELEASE: frame was subjected to receive reordering
 *
 * These are per-frame flags that are attached to a frame in the
 * @rx_flags field of &struct ieee80211_rx_status.
 */
enum ieee80211_packet_rx_flags {
	IEEE80211_RX_AMSDU			= BIT(3),
	IEEE80211_RX_MALFORMED_ACTION_FRM	= BIT(4),
	IEEE80211_RX_DEFERRED_RELEASE		= BIT(5),
};

/**
 * enum ieee80211_rx_flags - RX data flags
 *
 * @IEEE80211_RX_BEACON_REPORTED: This frame was already reported
 *	to cfg80211_report_obss_beacon().
 *
 * These flags are used across handling multiple interfaces
 * for a single frame.
 */
enum ieee80211_rx_flags {
	IEEE80211_RX_BEACON_REPORTED	= BIT(0),
};

struct ieee80211_rx_data {
	struct list_head *list;
	struct sk_buff *skb;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_link_data *link;
	struct sta_info *sta;
	struct link_sta_info *link_sta;
	struct ieee80211_key *key;

	unsigned int flags;

	/*
	 * Index into sequence numbers array, 0..16
	 * since the last (16) is used for non-QoS,
	 * will be 16 on non-QoS frames.
	 */
	int seqno_idx;

	/*
	 * Index into the security IV/PN arrays, 0..16
	 * since the last (16) is used for CCMP-encrypted
	 * management frames, will be set to 16 on mgmt
	 * frames and 0 on non-QoS frames.
	 */
	int security_idx;

	int link_id;

	union {
		struct {
			u32 iv32;
			u16 iv16;
		} tkip;
		struct {
			u8 pn[IEEE80211_CCMP_PN_LEN];
		} ccm_gcm;
	};
};

struct ieee80211_csa_settings {
	const u16 *counter_offsets_beacon;
	const u16 *counter_offsets_presp;

	int n_counter_offsets_beacon;
	int n_counter_offsets_presp;

	u8 count;
};

struct ieee80211_color_change_settings {
	u16 counter_offset_beacon;
	u16 counter_offset_presp;
	u8 count;
};

struct beacon_data {
	u8 *head, *tail;
	int head_len, tail_len;
	struct ieee80211_meshconf_ie *meshconf;
	u16 cntdwn_counter_offsets[IEEE80211_MAX_CNTDWN_COUNTERS_NUM];
	u8 cntdwn_current_counter;
	struct cfg80211_mbssid_elems *mbssid_ies;
	struct cfg80211_rnr_elems *rnr_ies;
	struct rcu_head rcu_head;
};

struct probe_resp {
	struct rcu_head rcu_head;
	int len;
	u16 cntdwn_counter_offsets[IEEE80211_MAX_CNTDWN_COUNTERS_NUM];
	u8 data[];
};

struct fils_discovery_data {
	struct rcu_head rcu_head;
	int len;
	u8 data[];
};

struct unsol_bcast_probe_resp_data {
	struct rcu_head rcu_head;
	int len;
	u8 data[];
};

struct s1g_short_beacon_data {
	struct rcu_head rcu_head;
	u8 *short_head;
	u8 *short_tail;
	int short_head_len;
	int short_tail_len;
};

struct ps_data {
	/* yes, this looks ugly, but guarantees that we can later use
	 * bitmap_empty :)
	 * NB: don't touch this bitmap, use sta_info_{set,clear}_tim_bit */
	u8 tim[sizeof(unsigned long) * BITS_TO_LONGS(IEEE80211_MAX_AID + 1)]
			__aligned(__alignof__(unsigned long));
	struct sk_buff_head bc_buf;
	atomic_t num_sta_ps; /* number of stations in PS mode */
	int dtim_count;
	bool dtim_bc_mc;
	int sb_count; /* num short beacons til next long beacon */
};

struct ieee80211_if_ap {
	struct list_head vlans; /* write-protected with RTNL and local->mtx */

	struct ps_data ps;
	atomic_t num_mcast_sta; /* number of stations receiving multicast */

	bool multicast_to_unicast;
	bool active;
};

struct ieee80211_if_vlan {
	struct list_head list; /* write-protected with RTNL and local->mtx */

	/* used for all tx if the VLAN is configured to 4-addr mode */
	struct sta_info __rcu *sta;
	atomic_t num_mcast_sta; /* number of stations receiving multicast */
};

struct mesh_stats {
	__u32 fwded_mcast;		/* Mesh forwarded multicast frames */
	__u32 fwded_unicast;		/* Mesh forwarded unicast frames */
	__u32 fwded_frames;		/* Mesh total forwarded frames */
	__u32 dropped_frames_ttl;	/* Not transmitted since mesh_ttl == 0*/
	__u32 dropped_frames_no_route;	/* Not transmitted, no route found */
};

#define PREQ_Q_F_START		0x1
#define PREQ_Q_F_REFRESH	0x2
struct mesh_preq_queue {
	struct list_head list;
	u8 dst[ETH_ALEN];
	u8 flags;
};

struct ieee80211_roc_work {
	struct list_head list;

	struct ieee80211_sub_if_data *sdata;

	struct ieee80211_channel *chan;

	bool started, abort, hw_begun, notified;
	bool on_channel;

	unsigned long start_time;

	u32 duration, req_duration;
	struct sk_buff *frame;
	u64 cookie, mgmt_tx_cookie;
	enum ieee80211_roc_type type;
};

/* flags used in struct ieee80211_if_managed.flags */
enum ieee80211_sta_flags {
	IEEE80211_STA_CONNECTION_POLL	= BIT(1),
	IEEE80211_STA_CONTROL_PORT	= BIT(2),
	IEEE80211_STA_MFP_ENABLED	= BIT(6),
	IEEE80211_STA_UAPSD_ENABLED	= BIT(7),
	IEEE80211_STA_NULLFUNC_ACKED	= BIT(8),
	IEEE80211_STA_ENABLE_RRM	= BIT(15),
};

enum ieee80211_conn_mode {
	IEEE80211_CONN_MODE_S1G,
	IEEE80211_CONN_MODE_LEGACY,
	IEEE80211_CONN_MODE_HT,
	IEEE80211_CONN_MODE_VHT,
	IEEE80211_CONN_MODE_HE,
	IEEE80211_CONN_MODE_EHT,
};

#define IEEE80211_CONN_MODE_HIGHEST	IEEE80211_CONN_MODE_EHT

enum ieee80211_conn_bw_limit {
	IEEE80211_CONN_BW_LIMIT_20,
	IEEE80211_CONN_BW_LIMIT_40,
	IEEE80211_CONN_BW_LIMIT_80,
	IEEE80211_CONN_BW_LIMIT_160, /* also 80+80 */
	IEEE80211_CONN_BW_LIMIT_320,
};

struct ieee80211_conn_settings {
	enum ieee80211_conn_mode mode;
	enum ieee80211_conn_bw_limit bw_limit;
};

extern const struct ieee80211_conn_settings ieee80211_conn_settings_unlimited;

struct ieee80211_mgd_auth_data {
	struct cfg80211_bss *bss;
	unsigned long timeout;
	int tries;
	u16 algorithm, expected_transaction;

	unsigned long userspace_selectors[BITS_TO_LONGS(128)];

	u8 key[WLAN_KEY_LEN_WEP104];
	u8 key_len, key_idx;
	bool done, waiting;
	bool peer_confirmed;
	bool timeout_started;
	int link_id;

	u8 ap_addr[ETH_ALEN] __aligned(2);

	u16 sae_trans, sae_status;
	size_t data_len;
	u8 data[];
};

struct ieee80211_mgd_assoc_data {
	struct {
		struct cfg80211_bss *bss;

		u8 addr[ETH_ALEN] __aligned(2);

		u8 ap_ht_param;

		struct ieee80211_vht_cap ap_vht_cap;

		size_t elems_len;
		u8 *elems; /* pointing to inside ie[] below */

		struct ieee80211_conn_settings conn;

		u16 status;

		bool disabled;
	} link[IEEE80211_MLD_MAX_NUM_LINKS];

	u8 ap_addr[ETH_ALEN] __aligned(2);

	/* this is for a workaround, so we use it only for non-MLO */
	const u8 *supp_rates;
	u8 supp_rates_len;

	unsigned long timeout;
	int tries;

	u8 prev_ap_addr[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	bool wmm, uapsd;
	bool need_beacon;
	bool synced;
	bool timeout_started;
	bool comeback; /* whether the AP has requested association comeback */
	bool s1g;
	bool spp_amsdu;

	s8 assoc_link_id;

	__le16 ext_mld_capa_ops;

	u8 fils_nonces[2 * FILS_NONCE_LEN];
	u8 fils_kek[FILS_MAX_KEK_LEN];
	size_t fils_kek_len;

	size_t ie_len;
	u8 *ie_pos; /* used to fill ie[] with link[].elems */
	u8 ie[];
};

struct ieee80211_sta_tx_tspec {
	/* timestamp of the first packet in the time slice */
	unsigned long time_slice_start;

	u32 admitted_time; /* in usecs, unlike over the air */
	u8 tsid;
	s8 up; /* signed to be able to invalidate with -1 during teardown */

	/* consumed TX time in microseconds in the time slice */
	u32 consumed_tx_time;
	enum {
		TX_TSPEC_ACTION_NONE = 0,
		TX_TSPEC_ACTION_DOWNGRADE,
		TX_TSPEC_ACTION_STOP_DOWNGRADE,
	} action;
	bool downgraded;
};

/* Advertised TID-to-link mapping info */
struct ieee80211_adv_ttlm_info {
	/* time in TUs at which the new mapping is established, or 0 if there is
	 * no planned advertised TID-to-link mapping
	 */
	u16 switch_time;
	u32 duration; /* duration of the planned T2L map in TUs */
	u16 map; /* map of usable links for all TIDs */
	bool active; /* whether the advertised mapping is active or not */
};

DECLARE_EWMA(beacon_signal, 4, 4)

struct ieee80211_if_managed {
	struct timer_list timer;
	struct timer_list conn_mon_timer;
	struct timer_list bcn_mon_timer;
	struct wiphy_work monitor_work;
	struct wiphy_work beacon_connection_loss_work;
	struct wiphy_work csa_connection_drop_work;

	unsigned long beacon_timeout;
	unsigned long probe_timeout;
	int probe_send_count;
	bool nullfunc_failed;
	u8 connection_loss:1,
	   driver_disconnect:1,
	   reconnect:1,
	   associated:1;

	struct ieee80211_mgd_auth_data *auth_data;
	struct ieee80211_mgd_assoc_data *assoc_data;

	unsigned long userspace_selectors[BITS_TO_LONGS(128)];

	bool powersave; /* powersave requested for this iface */
	bool broken_ap; /* AP is broken -- turn off powersave */

	unsigned int flags;

	u16 mcast_seq_last;

	bool status_acked;
	bool status_received;
	__le16 status_fc;

	enum {
		IEEE80211_MFP_DISABLED,
		IEEE80211_MFP_OPTIONAL,
		IEEE80211_MFP_REQUIRED
	} mfp; /* management frame protection */

	/*
	 * Bitmask of enabled u-apsd queues,
	 * IEEE80211_WMM_IE_STA_QOSINFO_AC_BE & co. Needs a new association
	 * to take effect.
	 */
	unsigned int uapsd_queues;

	/*
	 * Maximum number of buffered frames AP can deliver during a
	 * service period, IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL or similar.
	 * Needs a new association to take effect.
	 */
	unsigned int uapsd_max_sp_len;

	u8 use_4addr;

	/*
	 * State variables for keeping track of RSSI of the AP currently
	 * connected to and informing driver when RSSI has gone
	 * below/above a certain threshold.
	 */
	int rssi_min_thold, rssi_max_thold;

	struct ieee80211_ht_cap ht_capa; /* configured ht-cap over-rides */
	struct ieee80211_ht_cap ht_capa_mask; /* Valid parts of ht_capa */
	struct ieee80211_vht_cap vht_capa; /* configured VHT overrides */
	struct ieee80211_vht_cap vht_capa_mask; /* Valid parts of vht_capa */
	struct ieee80211_s1g_cap s1g_capa; /* configured S1G overrides */
	struct ieee80211_s1g_cap s1g_capa_mask; /* valid s1g_capa bits */

	/* TDLS support */
	u8 tdls_peer[ETH_ALEN] __aligned(2);
	struct wiphy_delayed_work tdls_peer_del_work;
	struct sk_buff *orig_teardown_skb; /* The original teardown skb */
	struct sk_buff *teardown_skb; /* A copy to send through the AP */
	spinlock_t teardown_lock; /* To lock changing teardown_skb */
	bool tdls_wider_bw_prohibited;

	/* WMM-AC TSPEC support */
	struct ieee80211_sta_tx_tspec tx_tspec[IEEE80211_NUM_ACS];
	/* Use a separate work struct so that we can do something here
	 * while the sdata->work is flushing the queues, for example.
	 * otherwise, in scenarios where we hardly get any traffic out
	 * on the BE queue, but there's a lot of VO traffic, we might
	 * get stuck in a downgraded situation and flush takes forever.
	 */
	struct wiphy_delayed_work tx_tspec_wk;

	/* Information elements from the last transmitted (Re)Association
	 * Request frame.
	 */
	u8 *assoc_req_ies;
	size_t assoc_req_ies_len;

	struct wiphy_delayed_work ml_reconf_work;
	u16 removed_links;

	/* TID-to-link mapping support */
	struct wiphy_delayed_work ttlm_work;
	struct ieee80211_adv_ttlm_info ttlm_info;
	struct wiphy_work teardown_ttlm_work;

	/* dialog token enumerator for neg TTLM request */
	u8 dialog_token_alloc;
	struct wiphy_delayed_work neg_ttlm_timeout_work;

	/* Locally initiated multi-link reconfiguration */
	struct {
		struct ieee80211_mgd_assoc_data *add_links_data;
		struct wiphy_delayed_work wk;
		u16 removed_links;
		u16 added_links;
		u8 dialog_token;
	} reconf;

	/* Support for epcs */
	struct {
		bool enabled;
		u8 dialog_token;
	} epcs;
};

struct ieee80211_if_ibss {
	struct timer_list timer;
	struct wiphy_work csa_connection_drop_work;

	unsigned long last_scan_completed;

	u32 basic_rates;

	bool fixed_bssid;
	bool fixed_channel;
	bool privacy;

	bool control_port;
	bool userspace_handles_dfs;

	u8 bssid[ETH_ALEN] __aligned(2);
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len, ie_len;
	u8 *ie;
	struct cfg80211_chan_def chandef;

	unsigned long ibss_join_req;
	/* probe response/beacon for IBSS */
	struct beacon_data __rcu *presp;

	struct ieee80211_ht_cap ht_capa; /* configured ht-cap over-rides */
	struct ieee80211_ht_cap ht_capa_mask; /* Valid parts of ht_capa */

	spinlock_t incomplete_lock;
	struct list_head incomplete_stations;

	enum {
		IEEE80211_IBSS_MLME_SEARCH,
		IEEE80211_IBSS_MLME_JOINED,
	} state;
};

/**
 * struct ieee80211_if_ocb - OCB mode state
 *
 * @housekeeping_timer: timer for periodic invocation of a housekeeping task
 * @wrkq_flags: OCB deferred task action
 * @incomplete_lock: delayed STA insertion lock
 * @incomplete_stations: list of STAs waiting for delayed insertion
 * @joined: indication if the interface is connected to an OCB network
 */
struct ieee80211_if_ocb {
	struct timer_list housekeeping_timer;
	unsigned long wrkq_flags;

	spinlock_t incomplete_lock;
	struct list_head incomplete_stations;

	bool joined;
};

/**
 * struct ieee80211_mesh_sync_ops - Extensible synchronization framework interface
 *
 * these declarations define the interface, which enables
 * vendor-specific mesh synchronization
 *
 * @rx_bcn_presp: beacon/probe response was received
 * @adjust_tsf: TSF adjustment method
 */
struct ieee80211_mesh_sync_ops {
	void (*rx_bcn_presp)(struct ieee80211_sub_if_data *sdata, u16 stype,
			     struct ieee80211_mgmt *mgmt, unsigned int len,
			     const struct ieee80211_meshconf_ie *mesh_cfg,
			     struct ieee80211_rx_status *rx_status);

	/* should be called with beacon_data under RCU read lock */
	void (*adjust_tsf)(struct ieee80211_sub_if_data *sdata,
			   struct beacon_data *beacon);
	/* add other framework functions here */
};

struct mesh_csa_settings {
	struct rcu_head rcu_head;
	struct cfg80211_csa_settings settings;
};

/**
 * struct mesh_table - mesh hash table
 *
 * @known_gates: list of known mesh gates and their mpaths by the station. The
 * gate's mpath may or may not be resolved and active.
 * @gates_lock: protects updates to known_gates
 * @rhead: the rhashtable containing struct mesh_paths, keyed by dest addr
 * @walk_head: linked list containing all mesh_path objects
 * @walk_lock: lock protecting walk_head
 * @entries: number of entries in the table
 */
struct mesh_table {
	struct hlist_head known_gates;
	spinlock_t gates_lock;
	struct rhashtable rhead;
	struct hlist_head walk_head;
	spinlock_t walk_lock;
	atomic_t entries;		/* Up to MAX_MESH_NEIGHBOURS */
};

/**
 * struct mesh_tx_cache - mesh fast xmit header cache
 *
 * @rht: hash table containing struct ieee80211_mesh_fast_tx, using skb DA as key
 * @walk_head: linked list containing all ieee80211_mesh_fast_tx objects
 * @walk_lock: lock protecting walk_head and rht
 */
struct mesh_tx_cache {
	struct rhashtable rht;
	struct hlist_head walk_head;
	spinlock_t walk_lock;
};

struct ieee80211_if_mesh {
	struct timer_list housekeeping_timer;
	struct timer_list mesh_path_timer;
	struct timer_list mesh_path_root_timer;

	unsigned long wrkq_flags;
	unsigned long mbss_changed[64 / BITS_PER_LONG];

	bool userspace_handles_dfs;

	u8 mesh_id[IEEE80211_MAX_MESH_ID_LEN];
	size_t mesh_id_len;
	/* Active Path Selection Protocol Identifier */
	u8 mesh_pp_id;
	/* Active Path Selection Metric Identifier */
	u8 mesh_pm_id;
	/* Congestion Control Mode Identifier */
	u8 mesh_cc_id;
	/* Synchronization Protocol Identifier */
	u8 mesh_sp_id;
	/* Authentication Protocol Identifier */
	u8 mesh_auth_id;
	/* Local mesh Sequence Number */
	u32 sn;
	/* Last used PREQ ID */
	u32 preq_id;
	atomic_t mpaths;
	/* Timestamp of last SN update */
	unsigned long last_sn_update;
	/* Time when it's ok to send next PERR */
	unsigned long next_perr;
	/* Timestamp of last PREQ sent */
	unsigned long last_preq;
	struct mesh_rmc *rmc;
	spinlock_t mesh_preq_queue_lock;
	struct mesh_preq_queue preq_queue;
	int preq_queue_len;
	struct mesh_stats mshstats;
	struct mesh_config mshcfg;
	atomic_t estab_plinks;
	atomic_t mesh_seqnum;
	bool accepting_plinks;
	int num_gates;
	struct beacon_data __rcu *beacon;
	const u8 *ie;
	u8 ie_len;
	enum {
		IEEE80211_MESH_SEC_NONE = 0x0,
		IEEE80211_MESH_SEC_AUTHED = 0x1,
		IEEE80211_MESH_SEC_SECURED = 0x2,
	} security;
	bool user_mpm;
	/* Extensible Synchronization Framework */
	const struct ieee80211_mesh_sync_ops *sync_ops;
	s64 sync_offset_clockdrift_max;
	spinlock_t sync_offset_lock;
	/* mesh power save */
	enum nl80211_mesh_power_mode nonpeer_pm;
	int ps_peers_light_sleep;
	int ps_peers_deep_sleep;
	struct ps_data ps;
	/* Channel Switching Support */
	struct mesh_csa_settings __rcu *csa;
	enum {
		IEEE80211_MESH_CSA_ROLE_NONE,
		IEEE80211_MESH_CSA_ROLE_INIT,
		IEEE80211_MESH_CSA_ROLE_REPEATER,
	} csa_role;
	u8 chsw_ttl;
	u16 pre_value;

	/* offset from skb->data while building IE */
	int meshconf_offset;

	struct mesh_table mesh_paths;
	struct mesh_table mpp_paths; /* Store paths for MPP&MAP */
	int mesh_paths_generation;
	int mpp_paths_generation;
	struct mesh_tx_cache tx_cache;
};

#ifdef CONFIG_MAC80211_MESH
#define IEEE80211_IFSTA_MESH_CTR_INC(msh, name)	\
	do { (msh)->mshstats.name++; } while (0)
#else
#define IEEE80211_IFSTA_MESH_CTR_INC(msh, name) \
	do { } while (0)
#endif

/**
 * enum ieee80211_sub_if_data_flags - virtual interface flags
 *
 * @IEEE80211_SDATA_ALLMULTI: interface wants all multicast packets
 * @IEEE80211_SDATA_DONT_BRIDGE_PACKETS: bridge packets between
 *	associated stations and deliver multicast frames both
 *	back to wireless media and to the local net stack.
 * @IEEE80211_SDATA_DISCONNECT_RESUME: Disconnect after resume.
 * @IEEE80211_SDATA_IN_DRIVER: indicates interface was added to driver
 * @IEEE80211_SDATA_DISCONNECT_HW_RESTART: Disconnect after hardware restart
 *  recovery
 */
enum ieee80211_sub_if_data_flags {
	IEEE80211_SDATA_ALLMULTI		= BIT(0),
	IEEE80211_SDATA_DONT_BRIDGE_PACKETS	= BIT(3),
	IEEE80211_SDATA_DISCONNECT_RESUME	= BIT(4),
	IEEE80211_SDATA_IN_DRIVER		= BIT(5),
	IEEE80211_SDATA_DISCONNECT_HW_RESTART	= BIT(6),
};

/**
 * enum ieee80211_sdata_state_bits - virtual interface state bits
 * @SDATA_STATE_RUNNING: virtual interface is up & running; this
 *	mirrors netif_running() but is separate for interface type
 *	change handling while the interface is up
 * @SDATA_STATE_OFFCHANNEL: This interface is currently in offchannel
 *	mode, so queues are stopped
 * @SDATA_STATE_OFFCHANNEL_BEACON_STOPPED: Beaconing was stopped due
 *	to offchannel, reset when offchannel returns
 */
enum ieee80211_sdata_state_bits {
	SDATA_STATE_RUNNING,
	SDATA_STATE_OFFCHANNEL,
	SDATA_STATE_OFFCHANNEL_BEACON_STOPPED,
};

/**
 * enum ieee80211_chanctx_mode - channel context configuration mode
 *
 * @IEEE80211_CHANCTX_SHARED: channel context may be used by
 *	multiple interfaces
 * @IEEE80211_CHANCTX_EXCLUSIVE: channel context can be used
 *	only by a single interface. This can be used for example for
 *	non-fixed channel IBSS.
 */
enum ieee80211_chanctx_mode {
	IEEE80211_CHANCTX_SHARED,
	IEEE80211_CHANCTX_EXCLUSIVE
};

/**
 * enum ieee80211_chanctx_replace_state - channel context replacement state
 *
 * This is used for channel context in-place reservations that require channel
 * context switch/swap.
 *
 * @IEEE80211_CHANCTX_REPLACE_NONE: no replacement is taking place
 * @IEEE80211_CHANCTX_WILL_BE_REPLACED: this channel context will be replaced
 *	by a (not yet registered) channel context pointed by %replace_ctx.
 * @IEEE80211_CHANCTX_REPLACES_OTHER: this (not yet registered) channel context
 *	replaces an existing channel context pointed to by %replace_ctx.
 */
enum ieee80211_chanctx_replace_state {
	IEEE80211_CHANCTX_REPLACE_NONE,
	IEEE80211_CHANCTX_WILL_BE_REPLACED,
	IEEE80211_CHANCTX_REPLACES_OTHER,
};

struct ieee80211_chanctx {
	struct list_head list;
	struct rcu_head rcu_head;

	struct list_head assigned_links;
	struct list_head reserved_links;

	enum ieee80211_chanctx_replace_state replace_state;
	struct ieee80211_chanctx *replace_ctx;

	enum ieee80211_chanctx_mode mode;
	bool driver_present;

	/* temporary data for search algorithm etc. */
	struct ieee80211_chan_req req;

	bool radar_detected;

	/* MUST be last - ends in a flexible-array member. */
	struct ieee80211_chanctx_conf conf;
};

struct mac80211_qos_map {
	struct cfg80211_qos_map qos_map;
	struct rcu_head rcu_head;
};

enum txq_info_flags {
	IEEE80211_TXQ_STOP,
	IEEE80211_TXQ_AMPDU,
	IEEE80211_TXQ_NO_AMSDU,
	IEEE80211_TXQ_DIRTY,
};

/**
 * struct txq_info - per tid queue
 *
 * @tin: contains packets split into multiple flows
 * @def_cvars: codel vars for the @tin's default_flow
 * @cstats: code statistics for this queue
 * @frags: used to keep fragments created after dequeue
 * @schedule_order: used with ieee80211_local->active_txqs
 * @schedule_round: counter to prevent infinite loops on TXQ scheduling
 * @flags: TXQ flags from &enum txq_info_flags
 * @txq: the driver visible part
 */
struct txq_info {
	struct fq_tin tin;
	struct codel_vars def_cvars;
	struct codel_stats cstats;

	u16 schedule_round;
	struct list_head schedule_order;

	struct sk_buff_head frags;

	unsigned long flags;

	/* keep last! */
	struct ieee80211_txq txq;
};

struct ieee80211_if_mntr {
	u32 flags;
	u8 mu_follow_addr[ETH_ALEN] __aligned(2);

	struct list_head list;
};

/**
 * struct ieee80211_if_nan - NAN state
 *
 * @conf: current NAN configuration
 * @func_lock: lock for @func_inst_ids
 * @function_inst_ids: a bitmap of available instance_id's
 */
struct ieee80211_if_nan {
	struct cfg80211_nan_conf conf;

	/* protects function_inst_ids */
	spinlock_t func_lock;
	struct idr function_inst_ids;
};

struct ieee80211_link_data_managed {
	u8 bssid[ETH_ALEN] __aligned(2);

	u8 dtim_period;
	enum ieee80211_smps_mode req_smps, /* requested smps mode */
				 driver_smps_mode; /* smps mode request */

	struct ieee80211_conn_settings conn;

	s16 p2p_noa_index;

	bool tdls_chan_switch_prohibited;

	bool have_beacon;
	bool tracking_signal_avg;
	bool disable_wmm_tracking;
	bool operating_11g_mode;

	struct {
		struct wiphy_delayed_work switch_work;
		struct cfg80211_chan_def ap_chandef;
		struct ieee80211_parsed_tpe tpe;
		unsigned long time;
		bool waiting_bcn;
		bool ignored_same_chan;
		bool blocked_tx;
	} csa;

	struct wiphy_work request_smps_work;
	/* used to reconfigure hardware SM PS */
	struct wiphy_work recalc_smps;

	bool beacon_crc_valid;
	u32 beacon_crc;
	struct ewma_beacon_signal ave_beacon_signal;
	int last_ave_beacon_signal;

	/*
	 * Number of Beacon frames used in ave_beacon_signal. This can be used
	 * to avoid generating less reliable cqm events that would be based
	 * only on couple of received frames.
	 */
	unsigned int count_beacon_signal;

	/* Number of times beacon loss was invoked. */
	unsigned int beacon_loss_count;

	/*
	 * Last Beacon frame signal strength average (ave_beacon_signal / 16)
	 * that triggered a cqm event. 0 indicates that no event has been
	 * generated for the current association.
	 */
	int last_cqm_event_signal;

	int wmm_last_param_set;
	int mu_edca_last_param_set;
};

struct ieee80211_link_data_ap {
	struct beacon_data __rcu *beacon;
	struct probe_resp __rcu *probe_resp;
	struct fils_discovery_data __rcu *fils_discovery;
	struct unsol_bcast_probe_resp_data __rcu *unsol_bcast_probe_resp;
	struct s1g_short_beacon_data __rcu *s1g_short_beacon;

	/* to be used after channel switch. */
	struct cfg80211_beacon_data *next_beacon;
};

struct ieee80211_link_data {
	struct ieee80211_sub_if_data *sdata;
	unsigned int link_id;

	struct list_head assigned_chanctx_list; /* protected by wiphy mutex */
	struct list_head reserved_chanctx_list; /* protected by wiphy mutex */

	/* multicast keys only */
	struct ieee80211_key __rcu *gtk[NUM_DEFAULT_KEYS +
					NUM_DEFAULT_MGMT_KEYS +
					NUM_DEFAULT_BEACON_KEYS];
	struct ieee80211_key __rcu *default_multicast_key;
	struct ieee80211_key __rcu *default_mgmt_key;
	struct ieee80211_key __rcu *default_beacon_key;


	bool operating_11g_mode;

	struct {
		struct wiphy_work finalize_work;
		struct ieee80211_chan_req chanreq;
	} csa;

	struct wiphy_work color_change_finalize_work;
	struct wiphy_delayed_work color_collision_detect_work;
	u64 color_bitmap;

	/* context reservation -- protected with wiphy mutex */
	struct ieee80211_chanctx *reserved_chanctx;
	struct ieee80211_chan_req reserved;
	bool reserved_radar_required;
	bool reserved_ready;

	u8 needed_rx_chains;
	enum ieee80211_smps_mode smps_mode;

	int user_power_level; /* in dBm */
	int ap_power_level; /* in dBm */

	bool radar_required;
	struct wiphy_delayed_work dfs_cac_timer_work;

	union {
		struct ieee80211_link_data_managed mgd;
		struct ieee80211_link_data_ap ap;
	} u;

	struct ieee80211_tx_queue_params tx_conf[IEEE80211_NUM_ACS];

	struct ieee80211_bss_conf *conf;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *debugfs_dir;
#endif
};

struct ieee80211_sub_if_data {
	struct list_head list;

	struct wireless_dev wdev;

	/* keys */
	struct list_head key_list;

	/* count for keys needing tailroom space allocation */
	int crypto_tx_tailroom_needed_cnt;
	int crypto_tx_tailroom_pending_dec;
	struct wiphy_delayed_work dec_tailroom_needed_wk;

	struct net_device *dev;
	struct ieee80211_local *local;

	unsigned int flags;

	unsigned long state;

	char name[IFNAMSIZ];

	struct ieee80211_fragment_cache frags;

	/* TID bitmap for NoAck policy */
	u16 noack_map;

	/* bit field of ACM bits (BIT(802.1D tag)) */
	u8 wmm_acm;

	struct ieee80211_key __rcu *keys[NUM_DEFAULT_KEYS];
	struct ieee80211_key __rcu *default_unicast_key;

	u16 sequence_number;
	u16 mld_mcast_seq;
	__be16 control_port_protocol;
	bool control_port_no_encrypt;
	bool control_port_no_preauth;
	bool control_port_over_nl80211;

	atomic_t num_tx_queued;
	struct mac80211_qos_map __rcu *qos_map;

	struct wiphy_work work;
	struct sk_buff_head skb_queue;
	struct sk_buff_head status_queue;

	/*
	 * AP this belongs to: self in AP mode and
	 * corresponding AP in VLAN mode, NULL for
	 * all others (might be needed later in IBSS)
	 */
	struct ieee80211_if_ap *bss;

	/* bitmap of allowed (non-MCS) rate indexes for rate control */
	u32 rc_rateidx_mask[NUM_NL80211_BANDS];

	bool rc_has_mcs_mask[NUM_NL80211_BANDS];
	u8  rc_rateidx_mcs_mask[NUM_NL80211_BANDS][IEEE80211_HT_MCS_MASK_LEN];

	bool rc_has_vht_mcs_mask[NUM_NL80211_BANDS];
	u16 rc_rateidx_vht_mcs_mask[NUM_NL80211_BANDS][NL80211_VHT_NSS_MAX];

	/* Beacon frame (non-MCS) rate (as a bitmap) */
	u32 beacon_rateidx_mask[NUM_NL80211_BANDS];
	bool beacon_rate_set;

	union {
		struct ieee80211_if_ap ap;
		struct ieee80211_if_vlan vlan;
		struct ieee80211_if_managed mgd;
		struct ieee80211_if_ibss ibss;
		struct ieee80211_if_mesh mesh;
		struct ieee80211_if_ocb ocb;
		struct ieee80211_if_mntr mntr;
		struct ieee80211_if_nan nan;
	} u;

	struct ieee80211_link_data deflink;
	struct ieee80211_link_data __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];

	/* for ieee80211_set_active_links_async() */
	struct wiphy_work activate_links_work;
	u16 desired_active_links;

	u16 restart_active_links;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct {
		struct dentry *subdir_stations;
		struct dentry *default_unicast_key;
		struct dentry *default_multicast_key;
		struct dentry *default_mgmt_key;
		struct dentry *default_beacon_key;
	} debugfs;
#endif

	/* must be last, dynamically sized area in this! */
	struct ieee80211_vif vif;
};

static inline
struct ieee80211_sub_if_data *vif_to_sdata(struct ieee80211_vif *p)
{
	return container_of(p, struct ieee80211_sub_if_data, vif);
}

#define sdata_dereference(p, sdata) \
	wiphy_dereference(sdata->local->hw.wiphy, p)

#define for_each_sdata_link(_local, _link)				\
	/* outer loop just to define the variables ... */		\
	for (struct ieee80211_sub_if_data *___sdata = NULL;		\
	     !___sdata;							\
	     ___sdata = (void *)~0 /* always stop */)			\
	list_for_each_entry(___sdata, &(_local)->interfaces, list)	\
	if (ieee80211_sdata_running(___sdata))				\
	for (int ___link_id = 0;					\
	     ___link_id < ARRAY_SIZE(___sdata->link);			\
	     ___link_id++)						\
	if ((_link = wiphy_dereference((_local)->hw.wiphy,		\
				       ___sdata->link[___link_id])))

/*
 * for_each_sdata_link_rcu() must be used under RCU read lock.
 */
#define for_each_sdata_link_rcu(_local, _link)						\
	/* outer loop just to define the variables ... */				\
	for (struct ieee80211_sub_if_data *___sdata = NULL;				\
	     !___sdata;									\
	     ___sdata = (void *)~0 /* always stop */)					\
	list_for_each_entry_rcu(___sdata, &(_local)->interfaces, list)			\
	if (ieee80211_sdata_running(___sdata))						\
	for (int ___link_id = 0;							\
	     ___link_id < ARRAY_SIZE((___sdata)->link);					\
	     ___link_id++)								\
	if ((_link = rcu_dereference((___sdata)->link[___link_id])))

#define for_each_link_data(sdata, __link)					\
	/* outer loop just to define the variable ... */			\
	for (struct ieee80211_sub_if_data *__sdata = (sdata); __sdata;		\
		__sdata = NULL /* always stop */)				\
	for (int __link_id = 0;							\
	     __link_id < ARRAY_SIZE((__sdata)->link); __link_id++)		\
		if ((!(__sdata)->vif.valid_links ||				\
		     (__sdata)->vif.valid_links & BIT(__link_id)) &&		\
		    ((__link) = sdata_dereference((__sdata)->link[__link_id],	\
						  (__sdata))))

/*
 * for_each_link_data_rcu should be used under RCU read lock.
 */
#define for_each_link_data_rcu(sdata, __link)					\
	/* outer loop just to define the variable ... */			\
	for (struct ieee80211_sub_if_data *__sdata = (sdata); __sdata;		\
		__sdata = NULL /* always stop */)				\
	for (int __link_id = 0;							\
	     __link_id < ARRAY_SIZE((__sdata)->link); __link_id++)		\
		if ((!(__sdata)->vif.valid_links ||				\
		     (__sdata)->vif.valid_links & BIT(__link_id)) &&		\
		    ((__link) = rcu_dereference((__sdata)->link[__link_id])))	\

static inline int
ieee80211_get_mbssid_beacon_len(struct cfg80211_mbssid_elems *elems,
				struct cfg80211_rnr_elems *rnr_elems,
				u8 i)
{
	int len = 0;

	if (!elems || !elems->cnt || i > elems->cnt)
		return 0;

	if (i < elems->cnt) {
		len = elems->elem[i].len;
		if (rnr_elems) {
			len += rnr_elems->elem[i].len;
			for (i = elems->cnt; i < rnr_elems->cnt; i++)
				len += rnr_elems->elem[i].len;
		}
		return len;
	}

	/* i == elems->cnt, calculate total length of all MBSSID elements */
	for (i = 0; i < elems->cnt; i++)
		len += elems->elem[i].len;

	if (rnr_elems) {
		for (i = 0; i < rnr_elems->cnt; i++)
			len += rnr_elems->elem[i].len;
	}

	return len;
}

enum {
	IEEE80211_RX_MSG	= 1,
	IEEE80211_TX_STATUS_MSG	= 2,
};

enum queue_stop_reason {
	IEEE80211_QUEUE_STOP_REASON_DRIVER,
	IEEE80211_QUEUE_STOP_REASON_PS,
	IEEE80211_QUEUE_STOP_REASON_CSA,
	IEEE80211_QUEUE_STOP_REASON_AGGREGATION,
	IEEE80211_QUEUE_STOP_REASON_SUSPEND,
	IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
	IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL,
	IEEE80211_QUEUE_STOP_REASON_FLUSH,
	IEEE80211_QUEUE_STOP_REASON_TDLS_TEARDOWN,
	IEEE80211_QUEUE_STOP_REASON_RESERVE_TID,
	IEEE80211_QUEUE_STOP_REASON_IFTYPE_CHANGE,

	IEEE80211_QUEUE_STOP_REASONS,
};

#ifdef CONFIG_MAC80211_LEDS
struct tpt_led_trigger {
	char name[32];
	const struct ieee80211_tpt_blink *blink_table;
	unsigned int blink_table_len;
	struct timer_list timer;
	struct ieee80211_local *local;
	unsigned long prev_traffic;
	unsigned long tx_bytes, rx_bytes;
	unsigned int active, want;
	bool running;
};
#endif

/**
 * enum mac80211_scan_flags - currently active scan mode
 *
 * @SCAN_SW_SCANNING: We're currently in the process of scanning but may as
 *	well be on the operating channel
 * @SCAN_HW_SCANNING: The hardware is scanning for us, we have no way to
 *	determine if we are on the operating channel or not
 * @SCAN_ONCHANNEL_SCANNING:  Do a software scan on only the current operating
 *	channel. This should not interrupt normal traffic.
 * @SCAN_COMPLETED: Set for our scan work function when the driver reported
 *	that the scan completed.
 * @SCAN_ABORTED: Set for our scan work function when the driver reported
 *	a scan complete for an aborted scan.
 * @SCAN_HW_CANCELLED: Set for our scan work function when the scan is being
 *	cancelled.
 * @SCAN_BEACON_WAIT: Set whenever we're passive scanning because of radar/no-IR
 *	and could send a probe request after receiving a beacon.
 * @SCAN_BEACON_DONE: Beacon received, we can now send a probe request
 */
enum mac80211_scan_flags {
	SCAN_SW_SCANNING,
	SCAN_HW_SCANNING,
	SCAN_ONCHANNEL_SCANNING,
	SCAN_COMPLETED,
	SCAN_ABORTED,
	SCAN_HW_CANCELLED,
	SCAN_BEACON_WAIT,
	SCAN_BEACON_DONE,
};

/**
 * enum mac80211_scan_state - scan state machine states
 *
 * @SCAN_DECISION: Main entry point to the scan state machine, this state
 *	determines if we should keep on scanning or switch back to the
 *	operating channel
 * @SCAN_SET_CHANNEL: Set the next channel to be scanned
 * @SCAN_SEND_PROBE: Send probe requests and wait for probe responses
 * @SCAN_SUSPEND: Suspend the scan and go back to operating channel to
 *	send out data
 * @SCAN_RESUME: Resume the scan and scan the next channel
 * @SCAN_ABORT: Abort the scan and go back to operating channel
 */
enum mac80211_scan_state {
	SCAN_DECISION,
	SCAN_SET_CHANNEL,
	SCAN_SEND_PROBE,
	SCAN_SUSPEND,
	SCAN_RESUME,
	SCAN_ABORT,
};

DECLARE_STATIC_KEY_FALSE(aql_disable);

struct ieee80211_local {
	/* embed the driver visible part.
	 * don't cast (use the static inlines below), but we keep
	 * it first anyway so they become a no-op */
	struct ieee80211_hw hw;

	struct fq fq;
	struct codel_vars *cvars;
	struct codel_params cparams;

	/* protects active_txqs and txqi->schedule_order */
	spinlock_t active_txq_lock[IEEE80211_NUM_ACS];
	struct list_head active_txqs[IEEE80211_NUM_ACS];
	u16 schedule_round[IEEE80211_NUM_ACS];

	/* serializes ieee80211_handle_wake_tx_queue */
	spinlock_t handle_wake_tx_queue_lock;

	u16 airtime_flags;
	u32 aql_txq_limit_low[IEEE80211_NUM_ACS];
	u32 aql_txq_limit_high[IEEE80211_NUM_ACS];
	u32 aql_threshold;
	atomic_t aql_total_pending_airtime;
	atomic_t aql_ac_pending_airtime[IEEE80211_NUM_ACS];

	const struct ieee80211_ops *ops;

	/*
	 * private workqueue to mac80211. mac80211 makes this accessible
	 * via ieee80211_queue_work()
	 */
	struct workqueue_struct *workqueue;

	unsigned long queue_stop_reasons[IEEE80211_MAX_QUEUES];
	int q_stop_reasons[IEEE80211_MAX_QUEUES][IEEE80211_QUEUE_STOP_REASONS];
	/* also used to protect ampdu_ac_queue and amdpu_ac_stop_refcnt */
	spinlock_t queue_stop_reason_lock;

	int open_count;
	int monitors, virt_monitors, tx_mntrs;
	/* number of interfaces with corresponding FIF_ flags */
	int fif_fcsfail, fif_plcpfail, fif_control, fif_other_bss, fif_pspoll,
	    fif_probe_req;
	bool probe_req_reg;
	bool rx_mcast_action_reg;
	unsigned int filter_flags; /* FIF_* */

	struct cfg80211_chan_def dflt_chandef;
	bool emulate_chanctx;

	/* protects the aggregated multicast list and filter calls */
	spinlock_t filter_lock;

	/* used for uploading changed mc list */
	struct wiphy_work reconfig_filter;

	/* aggregated multicast list */
	struct netdev_hw_addr_list mc_list;

	bool tim_in_locked_section; /* see ieee80211_beacon_get() */

	/*
	 * suspended is true if we finished all the suspend _and_ we have
	 * not yet come up from resume. This is to be used by mac80211
	 * to ensure driver sanity during suspend and mac80211's own
	 * sanity. It can eventually be used for WoW as well.
	 */
	bool suspended;

	/* suspending is true during the whole suspend process */
	bool suspending;

	/*
	 * Resuming is true while suspended, but when we're reprogramming the
	 * hardware -- at that time it's allowed to use ieee80211_queue_work()
	 * again even though some other parts of the stack are still suspended
	 * and we still drop received frames to avoid waking the stack.
	 */
	bool resuming;

	/*
	 * quiescing is true during the suspend process _only_ to
	 * ease timer cancelling etc.
	 */
	bool quiescing;

	/* device is started */
	bool started;

	/* device is during a HW reconfig */
	bool in_reconfig;

	/* reconfiguration failed ... suppress some warnings etc. */
	bool reconfig_failure;

	/* wowlan is enabled -- don't reconfig on resume */
	bool wowlan;

	struct wiphy_work radar_detected_work;

	/* number of RX chains the hardware has */
	u8 rx_chains;

	/* bitmap of which sbands were copied */
	u8 sband_allocated;

	int tx_headroom; /* required headroom for hardware/radiotap */

	/* Tasklet and skb queue to process calls from IRQ mode. All frames
	 * added to skb_queue will be processed, but frames in
	 * skb_queue_unreliable may be dropped if the total length of these
	 * queues increases over the limit. */
#define IEEE80211_IRQSAFE_QUEUE_LIMIT 128
	struct tasklet_struct tasklet;
	struct sk_buff_head skb_queue;
	struct sk_buff_head skb_queue_unreliable;

	spinlock_t rx_path_lock;

	/* Station data */
	/*
	 * The list, hash table and counter are protected
	 * by the wiphy mutex, reads are done with RCU.
	 */
	spinlock_t tim_lock;
	unsigned long num_sta;
	struct list_head sta_list;
	struct rhltable sta_hash;
	struct rhltable link_sta_hash;
	struct timer_list sta_cleanup;
	int sta_generation;

	struct sk_buff_head pending[IEEE80211_MAX_QUEUES];
	struct tasklet_struct tx_pending_tasklet;
	struct tasklet_struct wake_txqs_tasklet;

	atomic_t agg_queue_stop[IEEE80211_MAX_QUEUES];

	/* number of interfaces with allmulti RX */
	atomic_t iff_allmultis;

	struct rate_control_ref *rate_ctrl;

	struct arc4_ctx wep_tx_ctx;
	struct arc4_ctx wep_rx_ctx;
	u32 wep_iv;

	/* see iface.c */
	struct list_head interfaces;
	struct list_head mon_list; /* only that are IFF_UP */
	struct mutex iflist_mtx;

	/* Scanning and BSS list */
	unsigned long scanning;
	struct cfg80211_ssid scan_ssid;
	struct cfg80211_scan_request *int_scan_req;
	struct cfg80211_scan_request __rcu *scan_req;
	struct ieee80211_scan_request *hw_scan_req;
	struct cfg80211_chan_def scan_chandef;
	enum nl80211_band hw_scan_band;
	int scan_channel_idx;
	int scan_ies_len;
	int hw_scan_ies_bufsize;
	struct cfg80211_scan_info scan_info;

	struct wiphy_work sched_scan_stopped_work;
	struct ieee80211_sub_if_data __rcu *sched_scan_sdata;
	struct cfg80211_sched_scan_request __rcu *sched_scan_req;
	u8 scan_addr[ETH_ALEN];

	unsigned long leave_oper_channel_time;
	enum mac80211_scan_state next_scan_state;
	struct wiphy_delayed_work scan_work;
	struct ieee80211_sub_if_data __rcu *scan_sdata;

	/* Temporary remain-on-channel for off-channel operations */
	struct ieee80211_channel *tmp_channel;

	/* channel contexts */
	struct list_head chanctx_list;

#ifdef CONFIG_MAC80211_LEDS
	struct led_trigger tx_led, rx_led, assoc_led, radio_led;
	struct led_trigger tpt_led;
	atomic_t tx_led_active, rx_led_active, assoc_led_active;
	atomic_t radio_led_active, tpt_led_active;
	struct tpt_led_trigger *tpt_led_trigger;
#endif

#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
	/* SNMP counters */
	/* dot11CountersTable */
	u32 dot11TransmittedFragmentCount;
	u32 dot11MulticastTransmittedFrameCount;
	u32 dot11FailedCount;
	u32 dot11RetryCount;
	u32 dot11MultipleRetryCount;
	u32 dot11FrameDuplicateCount;
	u32 dot11ReceivedFragmentCount;
	u32 dot11MulticastReceivedFrameCount;
	u32 dot11TransmittedFrameCount;

	/* TX/RX handler statistics */
	unsigned int tx_handlers_drop;
	unsigned int tx_handlers_queued;
	unsigned int tx_handlers_drop_wep;
	unsigned int tx_handlers_drop_not_assoc;
	unsigned int tx_handlers_drop_unauth_port;
	unsigned int rx_handlers_drop;
	unsigned int rx_handlers_queued;
	unsigned int rx_handlers_drop_nullfunc;
	unsigned int rx_handlers_drop_defrag;
	unsigned int tx_expand_skb_head;
	unsigned int tx_expand_skb_head_cloned;
	unsigned int rx_expand_skb_head_defrag;
	unsigned int rx_handlers_fragments;
	unsigned int tx_status_drop;
#define I802_DEBUG_INC(c) (c)++
#else /* CONFIG_MAC80211_DEBUG_COUNTERS */
#define I802_DEBUG_INC(c) do { } while (0)
#endif /* CONFIG_MAC80211_DEBUG_COUNTERS */


	int total_ps_buffered; /* total number of all buffered unicast and
				* multicast packets for power saving stations
				*/

	bool pspolling;
	/*
	 * PS can only be enabled when we have exactly one managed
	 * interface (and monitors) in PS, this then points there.
	 */
	struct ieee80211_sub_if_data *ps_sdata;
	struct wiphy_work dynamic_ps_enable_work;
	struct wiphy_work dynamic_ps_disable_work;
	struct timer_list dynamic_ps_timer;
	struct notifier_block ifa_notifier;
	struct notifier_block ifa6_notifier;

	/*
	 * The dynamic ps timeout configured from user space via WEXT -
	 * this will override whatever chosen by mac80211 internally.
	 */
	int dynamic_ps_forced_timeout;

	int user_power_level; /* in dBm, for all interfaces */

	struct work_struct restart_work;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct local_debugfsdentries {
		struct dentry *rcdir;
		struct dentry *keys;
	} debugfs;
	bool force_tx_status;
#endif

	/*
	 * Remain-on-channel support
	 */
	struct wiphy_delayed_work roc_work;
	struct list_head roc_list;
	struct wiphy_work hw_roc_start, hw_roc_done;
	unsigned long hw_roc_start_time;
	u64 roc_cookie_counter;

	struct idr ack_status_frames;
	spinlock_t ack_status_lock;

	struct ieee80211_sub_if_data __rcu *p2p_sdata;

	/* virtual monitor interface */
	struct ieee80211_sub_if_data __rcu *monitor_sdata;
	struct ieee80211_chan_req monitor_chanreq;

	/* extended capabilities provided by mac80211 */
	u8 ext_capa[8];

	bool wbrf_supported;
};

static inline struct ieee80211_sub_if_data *
IEEE80211_DEV_TO_SUB_IF(const struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline struct ieee80211_sub_if_data *
IEEE80211_WDEV_TO_SUB_IF(struct wireless_dev *wdev)
{
	return container_of(wdev, struct ieee80211_sub_if_data, wdev);
}

static inline struct ieee80211_supported_band *
ieee80211_get_sband(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum nl80211_band band;

	WARN_ON(ieee80211_vif_is_mld(&sdata->vif));

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.bss_conf.chanctx_conf);

	if (!chanctx_conf) {
		rcu_read_unlock();
		return NULL;
	}

	band = chanctx_conf->def.chan->band;
	rcu_read_unlock();

	return local->hw.wiphy->bands[band];
}

static inline struct ieee80211_supported_band *
ieee80211_get_link_sband(struct ieee80211_link_data *link)
{
	struct ieee80211_local *local = link->sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum nl80211_band band;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(link->conf->chanctx_conf);
	if (!chanctx_conf) {
		rcu_read_unlock();
		return NULL;
	}

	band = chanctx_conf->def.chan->band;
	rcu_read_unlock();

	return local->hw.wiphy->bands[band];
}

/* this struct holds the value parsing from channel switch IE  */
struct ieee80211_csa_ie {
	struct ieee80211_chan_req chanreq;
	u8 mode;
	u8 count;
	u8 ttl;
	u16 pre_value;
	u16 reason_code;
	u32 max_switch_time;
};

enum ieee80211_elems_parse_error {
	IEEE80211_PARSE_ERR_INVALID_END		= BIT(0),
	IEEE80211_PARSE_ERR_DUP_ELEM		= BIT(1),
	IEEE80211_PARSE_ERR_BAD_ELEM_SIZE	= BIT(2),
	IEEE80211_PARSE_ERR_UNEXPECTED_ELEM	= BIT(3),
	IEEE80211_PARSE_ERR_DUP_NEST_ML_BASIC	= BIT(4),
};

/* Parsed Information Elements */
struct ieee802_11_elems {
	const u8 *ie_start;
	size_t total_len;
	u32 crc;

	/* pointers to IEs */
	const struct ieee80211_tdls_lnkie *lnk_id;
	const struct ieee80211_ch_switch_timing *ch_sw_timing;
	const u8 *ext_capab;
	const u8 *ssid;
	const u8 *supp_rates;
	const u8 *ds_params;
	const struct ieee80211_tim_ie *tim;
	const u8 *rsn;
	const u8 *rsnx;
	const u8 *erp_info;
	const u8 *ext_supp_rates;
	const u8 *wmm_info;
	const u8 *wmm_param;
	const struct ieee80211_ht_cap *ht_cap_elem;
	const struct ieee80211_ht_operation *ht_operation;
	const struct ieee80211_vht_cap *vht_cap_elem;
	const struct ieee80211_vht_operation *vht_operation;
	const struct ieee80211_meshconf_ie *mesh_config;
	const u8 *he_cap;
	const struct ieee80211_he_operation *he_operation;
	const struct ieee80211_he_spr *he_spr;
	const struct ieee80211_mu_edca_param_set *mu_edca_param_set;
	const struct ieee80211_he_6ghz_capa *he_6ghz_capa;
	const u8 *uora_element;
	const u8 *mesh_id;
	const u8 *peering;
	const __le16 *awake_window;
	const u8 *preq;
	const u8 *prep;
	const u8 *perr;
	const struct ieee80211_rann_ie *rann;
	const struct ieee80211_channel_sw_ie *ch_switch_ie;
	const struct ieee80211_ext_chansw_ie *ext_chansw_ie;
	const struct ieee80211_wide_bw_chansw_ie *wide_bw_chansw_ie;
	const u8 *max_channel_switch_time;
	const u8 *country_elem;
	const u8 *pwr_constr_elem;
	const u8 *cisco_dtpc_elem;
	const struct ieee80211_timeout_interval_ie *timeout_int;
	const u8 *opmode_notif;
	const struct ieee80211_sec_chan_offs_ie *sec_chan_offs;
	struct ieee80211_mesh_chansw_params_ie *mesh_chansw_params_ie;
	const struct ieee80211_bss_max_idle_period_ie *max_idle_period_ie;
	const struct ieee80211_multiple_bssid_configuration *mbssid_config_ie;
	const struct ieee80211_bssid_index *bssid_index;
	u8 max_bssid_indicator;
	u8 dtim_count;
	u8 dtim_period;
	const struct ieee80211_addba_ext_ie *addba_ext_ie;
	const struct ieee80211_s1g_cap *s1g_capab;
	const struct ieee80211_s1g_oper_ie *s1g_oper;
	const struct ieee80211_s1g_bcn_compat_ie *s1g_bcn_compat;
	const struct ieee80211_aid_response_ie *aid_resp;
	const struct ieee80211_eht_cap_elem *eht_cap;
	const struct ieee80211_eht_operation *eht_operation;
	const struct ieee80211_multi_link_elem *ml_basic;
	const struct ieee80211_multi_link_elem *ml_reconf;
	const struct ieee80211_multi_link_elem *ml_epcs;
	const struct ieee80211_bandwidth_indication *bandwidth_indication;
	const struct ieee80211_ttlm_elem *ttlm[IEEE80211_TTLM_MAX_CNT];

	/* not the order in the psd values is per element, not per chandef */
	struct ieee80211_parsed_tpe tpe;
	struct ieee80211_parsed_tpe csa_tpe;

	/* length of them, respectively */
	u8 ext_capab_len;
	u8 ssid_len;
	u8 supp_rates_len;
	u8 tim_len;
	u8 rsn_len;
	u8 rsnx_len;
	u8 ext_supp_rates_len;
	u8 wmm_info_len;
	u8 wmm_param_len;
	u8 he_cap_len;
	u8 mesh_id_len;
	u8 peering_len;
	u8 preq_len;
	u8 prep_len;
	u8 perr_len;
	u8 country_elem_len;
	u8 bssid_index_len;
	u8 eht_cap_len;

	/* mult-link element can be de-fragmented and thus u8 is not sufficient */
	size_t ml_basic_len;
	size_t ml_reconf_len;
	size_t ml_epcs_len;

	u8 ttlm_num;

	/*
	 * store the per station profile pointer and length in case that the
	 * parsing also handled Multi-Link element parsing for a specific link
	 * ID.
	 */
	struct ieee80211_mle_per_sta_profile *prof;
	size_t sta_prof_len;

	/* whether/which parse error occurred while retrieving these elements */
	u8 parse_error;
};

static inline struct ieee80211_local *hw_to_local(
	struct ieee80211_hw *hw)
{
	return container_of(hw, struct ieee80211_local, hw);
}

static inline struct txq_info *to_txq_info(struct ieee80211_txq *txq)
{
	return container_of(txq, struct txq_info, txq);
}

static inline bool txq_has_queue(struct ieee80211_txq *txq)
{
	struct txq_info *txqi = to_txq_info(txq);

	return !(skb_queue_empty(&txqi->frags) && !txqi->tin.backlog_packets);
}

static inline bool
ieee80211_have_rx_timestamp(struct ieee80211_rx_status *status)
{
	return status->flag & RX_FLAG_MACTIME;
}

void ieee80211_vif_inc_num_mcast(struct ieee80211_sub_if_data *sdata);
void ieee80211_vif_dec_num_mcast(struct ieee80211_sub_if_data *sdata);

void ieee80211_vif_block_queues_csa(struct ieee80211_sub_if_data *sdata);
void ieee80211_vif_unblock_queues_csa(struct ieee80211_sub_if_data *sdata);

/* This function returns the number of multicast stations connected to this
 * interface. It returns -1 if that number is not tracked, that is for netdevs
 * not in AP or AP_VLAN mode or when using 4addr.
 */
static inline int
ieee80211_vif_get_num_mcast_if(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.type == NL80211_IFTYPE_AP)
		return atomic_read(&sdata->u.ap.num_mcast_sta);
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN && !sdata->u.vlan.sta)
		return atomic_read(&sdata->u.vlan.num_mcast_sta);
	return -1;
}

u64 ieee80211_calculate_rx_timestamp(struct ieee80211_local *local,
				     struct ieee80211_rx_status *status,
				     unsigned int mpdu_len,
				     unsigned int mpdu_offset);
int ieee80211_hw_config(struct ieee80211_local *local, int radio_idx,
			u32 changed);
int ieee80211_hw_conf_chan(struct ieee80211_local *local);
void ieee80211_hw_conf_init(struct ieee80211_local *local);
void ieee80211_tx_set_protected(struct ieee80211_tx_data *tx);
void ieee80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u64 changed);
void ieee80211_vif_cfg_change_notify(struct ieee80211_sub_if_data *sdata,
				     u64 changed);
void ieee80211_link_info_change_notify(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_link_data *link,
				       u64 changed);
void ieee80211_configure_filter(struct ieee80211_local *local);
u64 ieee80211_reset_erp_info(struct ieee80211_sub_if_data *sdata);

void ieee80211_handle_queued_frames(struct ieee80211_local *local);

u64 ieee80211_mgmt_tx_cookie(struct ieee80211_local *local);
int ieee80211_attach_ack_skb(struct ieee80211_local *local, struct sk_buff *skb,
			     u64 *cookie, gfp_t gfp);

void ieee80211_check_fast_rx(struct sta_info *sta);
void __ieee80211_check_fast_rx_iface(struct ieee80211_sub_if_data *sdata);
void ieee80211_check_fast_rx_iface(struct ieee80211_sub_if_data *sdata);
void ieee80211_clear_fast_rx(struct sta_info *sta);

bool ieee80211_is_our_addr(struct ieee80211_sub_if_data *sdata,
			   const u8 *addr, int *out_link_id);

/* STA code */
void ieee80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata);
int ieee80211_mgd_auth(struct ieee80211_sub_if_data *sdata,
		       struct cfg80211_auth_request *req);
int ieee80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req);
int ieee80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req);
int ieee80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req);
void ieee80211_send_pspoll(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata);
void ieee80211_recalc_ps(struct ieee80211_local *local);
void ieee80211_recalc_ps_vif(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb);
void ieee80211_sta_rx_queued_ext(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb);
void ieee80211_sta_reset_beacon_monitor(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_reset_conn_monitor(struct ieee80211_sub_if_data *sdata);
void ieee80211_mgd_stop(struct ieee80211_sub_if_data *sdata);
void ieee80211_mgd_conn_tx_status(struct ieee80211_sub_if_data *sdata,
				  __le16 fc, bool acked);
void ieee80211_mgd_quiesce(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_restart(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_handle_tspec_ac_params(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_connection_lost(struct ieee80211_sub_if_data *sdata,
				   u8 reason, bool tx);
void ieee80211_mgd_setup_link(struct ieee80211_link_data *link);
void ieee80211_mgd_stop_link(struct ieee80211_link_data *link);
void ieee80211_mgd_set_link_qos_params(struct ieee80211_link_data *link);

/* IBSS code */
void ieee80211_ibss_notify_scan_completed(struct ieee80211_local *local);
void ieee80211_ibss_setup_sdata(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_rx_no_sta(struct ieee80211_sub_if_data *sdata,
			      const u8 *bssid, const u8 *addr, u32 supp_rates);
int ieee80211_ibss_join(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_ibss_params *params);
int ieee80211_ibss_leave(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb);
int ieee80211_ibss_csa_beacon(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings,
			      u64 *changed);
int ieee80211_ibss_finish_csa(struct ieee80211_sub_if_data *sdata,
			      u64 *changed);
void ieee80211_ibss_stop(struct ieee80211_sub_if_data *sdata);

/* OCB code */
void ieee80211_ocb_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_ocb_rx_no_sta(struct ieee80211_sub_if_data *sdata,
			     const u8 *bssid, const u8 *addr, u32 supp_rates);
void ieee80211_ocb_setup_sdata(struct ieee80211_sub_if_data *sdata);
int ieee80211_ocb_join(struct ieee80211_sub_if_data *sdata,
		       struct ocb_setup *setup);
int ieee80211_ocb_leave(struct ieee80211_sub_if_data *sdata);

/* mesh code */
void ieee80211_mesh_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_mesh_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb);
int ieee80211_mesh_csa_beacon(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings,
			      u64 *changed);
int ieee80211_mesh_finish_csa(struct ieee80211_sub_if_data *sdata,
			      u64 *changed);

/* scan/BSS handling */
void ieee80211_scan_work(struct wiphy *wiphy, struct wiphy_work *work);
int ieee80211_request_ibss_scan(struct ieee80211_sub_if_data *sdata,
				const u8 *ssid, u8 ssid_len,
				struct ieee80211_channel **channels,
				unsigned int n_channels);
int ieee80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req);
void ieee80211_scan_cancel(struct ieee80211_local *local);
void ieee80211_run_deferred_scan(struct ieee80211_local *local);
void ieee80211_scan_rx(struct ieee80211_local *local, struct sk_buff *skb);

void ieee80211_inform_bss(struct wiphy *wiphy, struct cfg80211_bss *bss,
			  const struct cfg80211_bss_ies *ies, void *data);

void ieee80211_mlme_notify_scan_completed(struct ieee80211_local *local);
struct ieee80211_bss *
ieee80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt,
			  size_t len,
			  struct ieee80211_channel *channel);
void ieee80211_rx_bss_put(struct ieee80211_local *local,
			  struct ieee80211_bss *bss);

/* scheduled scan handling */
int
__ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_sched_scan_request *req);
int ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				       struct cfg80211_sched_scan_request *req);
int ieee80211_request_sched_scan_stop(struct ieee80211_local *local);
void ieee80211_sched_scan_end(struct ieee80211_local *local);
void ieee80211_sched_scan_stopped_work(struct wiphy *wiphy,
				       struct wiphy_work *work);

/* off-channel/mgmt-tx */
void ieee80211_offchannel_stop_vifs(struct ieee80211_local *local);
void ieee80211_offchannel_return(struct ieee80211_local *local);
void ieee80211_roc_setup(struct ieee80211_local *local);
void ieee80211_start_next_roc(struct ieee80211_local *local);
void ieee80211_reconfig_roc(struct ieee80211_local *local);
void ieee80211_roc_purge(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata);
int ieee80211_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct ieee80211_channel *chan,
				unsigned int duration, u64 *cookie);
int ieee80211_cancel_remain_on_channel(struct wiphy *wiphy,
				       struct wireless_dev *wdev, u64 cookie);
int ieee80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct cfg80211_mgmt_tx_params *params, u64 *cookie);
int ieee80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
				  struct wireless_dev *wdev, u64 cookie);

/* channel switch handling */
void ieee80211_csa_finalize_work(struct wiphy *wiphy, struct wiphy_work *work);
int ieee80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_csa_settings *params);

/* color change handling */
void ieee80211_color_change_finalize_work(struct wiphy *wiphy,
					  struct wiphy_work *work);
void ieee80211_color_collision_detection_work(struct wiphy *wiphy,
					      struct wiphy_work *work);

/* interface handling */
#define MAC80211_SUPPORTED_FEATURES_TX	(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | \
					 NETIF_F_HW_CSUM | NETIF_F_SG | \
					 NETIF_F_HIGHDMA | NETIF_F_GSO_SOFTWARE | \
					 NETIF_F_HW_TC)
#define MAC80211_SUPPORTED_FEATURES_RX	(NETIF_F_RXCSUM)
#define MAC80211_SUPPORTED_FEATURES	(MAC80211_SUPPORTED_FEATURES_TX | \
					 MAC80211_SUPPORTED_FEATURES_RX)

int ieee80211_iface_init(void);
void ieee80211_iface_exit(void);
int ieee80211_if_add(struct ieee80211_local *local, const char *name,
		     unsigned char name_assign_type,
		     struct wireless_dev **new_wdev, enum nl80211_iftype type,
		     struct vif_params *params);
int ieee80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum nl80211_iftype type);
void ieee80211_if_remove(struct ieee80211_sub_if_data *sdata);
void ieee80211_remove_interfaces(struct ieee80211_local *local);
u32 ieee80211_idle_off(struct ieee80211_local *local);
void ieee80211_recalc_idle(struct ieee80211_local *local);
void ieee80211_adjust_monitor_flags(struct ieee80211_sub_if_data *sdata,
				    const int offset);
int ieee80211_do_open(struct wireless_dev *wdev, bool coming_up);
void ieee80211_sdata_stop(struct ieee80211_sub_if_data *sdata);
int ieee80211_add_virtual_monitor(struct ieee80211_local *local);
void ieee80211_del_virtual_monitor(struct ieee80211_local *local);

bool __ieee80211_recalc_txpower(struct ieee80211_link_data *link);
void ieee80211_recalc_txpower(struct ieee80211_link_data *link,
			      bool update_bss);
void ieee80211_recalc_offload(struct ieee80211_local *local);

static inline bool ieee80211_sdata_running(struct ieee80211_sub_if_data *sdata)
{
	return test_bit(SDATA_STATE_RUNNING, &sdata->state);
}

/* link handling */
void ieee80211_link_setup(struct ieee80211_link_data *link);
void ieee80211_link_init(struct ieee80211_sub_if_data *sdata,
			 int link_id,
			 struct ieee80211_link_data *link,
			 struct ieee80211_bss_conf *link_conf);
void ieee80211_link_stop(struct ieee80211_link_data *link);
int ieee80211_vif_set_links(struct ieee80211_sub_if_data *sdata,
			    u16 new_links, u16 dormant_links);
static inline void ieee80211_vif_clear_links(struct ieee80211_sub_if_data *sdata)
{
	ieee80211_vif_set_links(sdata, 0, 0);
}

void ieee80211_apvlan_link_setup(struct ieee80211_sub_if_data *sdata);
void ieee80211_apvlan_link_clear(struct ieee80211_sub_if_data *sdata);

/* tx handling */
void ieee80211_clear_tx_pending(struct ieee80211_local *local);
void ieee80211_tx_pending(struct tasklet_struct *t);
netdev_tx_t ieee80211_monitor_start_xmit(struct sk_buff *skb,
					 struct net_device *dev);
netdev_tx_t ieee80211_subif_start_xmit(struct sk_buff *skb,
				       struct net_device *dev);
netdev_tx_t ieee80211_subif_start_xmit_8023(struct sk_buff *skb,
					    struct net_device *dev);
void __ieee80211_subif_start_xmit(struct sk_buff *skb,
				  struct net_device *dev,
				  u32 info_flags,
				  u32 ctrl_flags,
				  u64 *cookie);
struct sk_buff *
ieee80211_build_data_template(struct ieee80211_sub_if_data *sdata,
			      struct sk_buff *skb, u32 info_flags);
void ieee80211_tx_monitor(struct ieee80211_local *local, struct sk_buff *skb,
			  int retry_count, struct ieee80211_tx_status *status);

void ieee80211_check_fast_xmit(struct sta_info *sta);
void ieee80211_check_fast_xmit_all(struct ieee80211_local *local);
void ieee80211_check_fast_xmit_iface(struct ieee80211_sub_if_data *sdata);
void ieee80211_clear_fast_xmit(struct sta_info *sta);
int ieee80211_tx_control_port(struct wiphy *wiphy, struct net_device *dev,
			      const u8 *buf, size_t len,
			      const u8 *dest, __be16 proto, bool unencrypted,
			      int link_id, u64 *cookie);
int ieee80211_probe_mesh_link(struct wiphy *wiphy, struct net_device *dev,
			      const u8 *buf, size_t len);
void __ieee80211_xmit_fast(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta,
			   struct ieee80211_fast_tx *fast_tx,
			   struct sk_buff *skb, bool ampdu,
			   const u8 *da, const u8 *sa);
void ieee80211_aggr_check(struct ieee80211_sub_if_data *sdata,
			  struct sta_info *sta, struct sk_buff *skb);

/* HT */
void ieee80211_apply_htcap_overrides(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_sta_ht_cap *ht_cap);
bool ieee80211_ht_cap_ie_to_sta_ht_cap(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_supported_band *sband,
				       const struct ieee80211_ht_cap *ht_cap_ie,
				       struct link_sta_info *link_sta);
void ieee80211_send_delba(struct ieee80211_sub_if_data *sdata,
			  const u8 *da, u16 tid,
			  u16 initiator, u16 reason_code);
int ieee80211_send_smps_action(struct ieee80211_sub_if_data *sdata,
			       enum ieee80211_smps_mode smps, const u8 *da,
			       const u8 *bssid, int link_id);
void ieee80211_add_addbaext(struct sk_buff *skb,
			    const u8 req_addba_ext_data,
			    u16 buf_size);
u8 ieee80211_retrieve_addba_ext_data(struct sta_info *sta,
				     const void *elem_data, ssize_t elem_len,
				     u16 *buf_size);
void __ieee80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				    u16 initiator, u16 reason, bool stop);
void __ieee80211_start_rx_ba_session(struct sta_info *sta,
				     u8 dialog_token, u16 timeout,
				     u16 start_seq_num, u16 ba_policy, u16 tid,
				     u16 buf_size, bool tx, bool auto_seq,
				     const u8 addba_ext_data);
void ieee80211_sta_tear_down_BA_sessions(struct sta_info *sta,
					 enum ieee80211_agg_stop_reason reason);
void ieee80211_process_delba(struct ieee80211_sub_if_data *sdata,
			     struct sta_info *sta,
			     struct ieee80211_mgmt *mgmt, size_t len);
void ieee80211_process_addba_resp(struct ieee80211_local *local,
				  struct sta_info *sta,
				  struct ieee80211_mgmt *mgmt,
				  size_t len);
void ieee80211_process_addba_request(struct ieee80211_local *local,
				     struct sta_info *sta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len);

static inline struct ieee80211_mgmt *
ieee80211_mgmt_ba(struct sk_buff *skb, const u8 *da,
		  struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_mgmt *mgmt = skb_put_zero(skb, 24);

	ether_addr_copy(mgmt->da, da);
	ether_addr_copy(mgmt->sa, sdata->vif.addr);

	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
	    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
		ether_addr_copy(mgmt->bssid, sdata->vif.addr);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		ether_addr_copy(mgmt->bssid, sdata->vif.cfg.ap_addr);
	else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		ether_addr_copy(mgmt->bssid, sdata->u.ibss.bssid);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	return mgmt;
}

int __ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				   enum ieee80211_agg_stop_reason reason);
void ieee80211_start_tx_ba_cb(struct sta_info *sta, int tid,
			      struct tid_ampdu_tx *tid_tx);
void ieee80211_stop_tx_ba_cb(struct sta_info *sta, int tid,
			     struct tid_ampdu_tx *tid_tx);
void ieee80211_ba_session_work(struct wiphy *wiphy, struct wiphy_work *work);
void ieee80211_tx_ba_session_handle_start(struct sta_info *sta, int tid);
void ieee80211_release_reorder_timeout(struct sta_info *sta, int tid);

u8 ieee80211_mcs_to_chains(const struct ieee80211_mcs_info *mcs);
enum nl80211_smps_mode
ieee80211_smps_mode_to_smps_mode(enum ieee80211_smps_mode smps);

void ieee80211_ht_handle_chanwidth_notif(struct ieee80211_local *local,
					 struct ieee80211_sub_if_data *sdata,
					 struct sta_info *sta,
					 struct link_sta_info *link_sta,
					 u8 chanwidth, enum nl80211_band band);

/* VHT */
void
ieee80211_vht_cap_ie_to_sta_vht_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const struct ieee80211_vht_cap *vht_cap_ie,
				    const struct ieee80211_vht_cap *vht_cap_ie2,
				    struct link_sta_info *link_sta);
enum ieee80211_sta_rx_bandwidth
_ieee80211_sta_cap_rx_bw(struct link_sta_info *link_sta,
			 struct cfg80211_chan_def *chandef);
static inline enum ieee80211_sta_rx_bandwidth
ieee80211_sta_cap_rx_bw(struct link_sta_info *link_sta)
{
	return _ieee80211_sta_cap_rx_bw(link_sta, NULL);
}
enum ieee80211_sta_rx_bandwidth
_ieee80211_sta_cur_vht_bw(struct link_sta_info *link_sta,
			  struct cfg80211_chan_def *chandef);
static inline enum ieee80211_sta_rx_bandwidth
ieee80211_sta_cur_vht_bw(struct link_sta_info *link_sta)
{
	return _ieee80211_sta_cur_vht_bw(link_sta, NULL);
}
void ieee80211_sta_init_nss(struct link_sta_info *link_sta);
enum nl80211_chan_width
ieee80211_sta_cap_chan_bw(struct link_sta_info *link_sta);
void ieee80211_process_mu_groups(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_link_data *link,
				 struct ieee80211_mgmt *mgmt);
u32 __ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
				  struct link_sta_info *sta,
				  u8 opmode, enum nl80211_band band);
void ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
				 struct link_sta_info *sta,
				 u8 opmode, enum nl80211_band band);
void ieee80211_apply_vhtcap_overrides(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_sta_vht_cap *vht_cap);
void ieee80211_get_vht_mask_from_cap(__le16 vht_cap,
				     u16 vht_mask[NL80211_VHT_NSS_MAX]);
enum nl80211_chan_width
ieee80211_sta_rx_bw_to_chan_width(struct link_sta_info *sta);

/* HE */
void
ieee80211_he_cap_ie_to_sta_he_cap(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_supported_band *sband,
				  const u8 *he_cap_ie, u8 he_cap_len,
				  const struct ieee80211_he_6ghz_capa *he_6ghz_capa,
				  struct link_sta_info *link_sta);
void
ieee80211_he_spr_ie_to_bss_conf(struct ieee80211_vif *vif,
				const struct ieee80211_he_spr *he_spr_ie_elem);

void
ieee80211_he_op_ie_to_bss_conf(struct ieee80211_vif *vif,
			const struct ieee80211_he_operation *he_op_ie_elem);

/* S1G */
void ieee80211_s1g_sta_rate_init(struct sta_info *sta);
bool ieee80211_s1g_is_twt_setup(struct sk_buff *skb);
void ieee80211_s1g_rx_twt_action(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb);
void ieee80211_s1g_status_twt_action(struct ieee80211_sub_if_data *sdata,
				     struct sk_buff *skb);
void ieee80211_s1g_cap_to_sta_s1g_cap(struct ieee80211_sub_if_data *sdata,
				      const struct ieee80211_s1g_cap *s1g_cap_ie,
				      struct link_sta_info *link_sta);

/* Spectrum management */
void ieee80211_process_measurement_req(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt,
				       size_t len);
/**
 * ieee80211_parse_ch_switch_ie - parses channel switch IEs
 * @sdata: the sdata of the interface which has received the frame
 * @elems: parsed 802.11 elements received with the frame
 * @current_band: indicates the current band
 * @vht_cap_info: VHT capabilities of the transmitter
 * @conn: contains information about own capabilities and restrictions
 *	to decide which channel switch announcements can be accepted
 * @bssid: the currently connected bssid (for reporting)
 * @unprot_action: whether the frame was an unprotected frame or not,
 *	used for reporting
 * @csa_ie: parsed 802.11 csa elements on count, mode, chandef and mesh ttl.
 *	All of them will be filled with if success only.
 * Return: 0 on success, <0 on error and >0 if there is nothing to parse.
 */
int ieee80211_parse_ch_switch_ie(struct ieee80211_sub_if_data *sdata,
				 struct ieee802_11_elems *elems,
				 enum nl80211_band current_band,
				 u32 vht_cap_info,
				 struct ieee80211_conn_settings *conn,
				 u8 *bssid, bool unprot_action,
				 struct ieee80211_csa_ie *csa_ie);

/* Suspend/resume and hw reconfiguration */
int ieee80211_reconfig(struct ieee80211_local *local);
void ieee80211_stop_device(struct ieee80211_local *local, bool suspend);

int __ieee80211_suspend(struct ieee80211_hw *hw,
			struct cfg80211_wowlan *wowlan);

static inline int __ieee80211_resume(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	WARN(test_bit(SCAN_HW_SCANNING, &local->scanning) &&
	     !test_bit(SCAN_COMPLETED, &local->scanning),
		"%s: resume with hardware scan still in progress\n",
		wiphy_name(hw->wiphy));

	return ieee80211_reconfig(hw_to_local(hw));
}

/* utility functions/constants */
extern const void *const mac80211_wiphy_privid; /* for wiphy privid */
const char *ieee80211_conn_mode_str(enum ieee80211_conn_mode mode);
enum ieee80211_conn_bw_limit
ieee80211_min_bw_limit_from_chandef(struct cfg80211_chan_def *chandef);
int ieee80211_frame_duration(enum nl80211_band band, size_t len,
			     int rate, int erp, int short_preamble);
void ieee80211_regulatory_limit_wmm_params(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_tx_queue_params *qparam,
					   int ac);
void ieee80211_clear_tpe(struct ieee80211_parsed_tpe *tpe);
void ieee80211_set_wmm_default(struct ieee80211_link_data *link,
			       bool bss_notify, bool enable_qos);
void ieee80211_xmit(struct ieee80211_sub_if_data *sdata,
		    struct sta_info *sta, struct sk_buff *skb);

void __ieee80211_tx_skb_tid_band(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb, int tid, int link_id,
				 enum nl80211_band band);

/* sta_out needs to be checked for ERR_PTR() before using */
int ieee80211_lookup_ra_sta(struct ieee80211_sub_if_data *sdata,
			    struct sk_buff *skb,
			    struct sta_info **sta_out);

static inline void
ieee80211_tx_skb_tid_band(struct ieee80211_sub_if_data *sdata,
			  struct sk_buff *skb, int tid,
			  enum nl80211_band band)
{
	rcu_read_lock();
	__ieee80211_tx_skb_tid_band(sdata, skb, tid, -1, band);
	rcu_read_unlock();
}

void ieee80211_tx_skb_tid(struct ieee80211_sub_if_data *sdata,
			  struct sk_buff *skb, int tid, int link_id);

static inline void ieee80211_tx_skb(struct ieee80211_sub_if_data *sdata,
				    struct sk_buff *skb)
{
	/* Send all internal mgmt frames on VO. Accordingly set TID to 7. */
	ieee80211_tx_skb_tid(sdata, skb, 7, -1);
}

/**
 * struct ieee80211_elems_parse_params - element parsing parameters
 * @mode: connection mode for parsing
 * @start: pointer to the elements
 * @len: length of the elements
 * @action: %true if the elements came from an action frame
 * @filter: bitmap of element IDs to filter out while calculating
 *	the element CRC
 * @crc: CRC starting value
 * @bss: the BSS to parse this as, for multi-BSSID cases this can
 *	represent a non-transmitting BSS in which case the data
 *	for that non-transmitting BSS is returned
 * @link_id: the link ID to parse elements for, if a STA profile
 *	is present in the multi-link element, or -1 to ignore;
 *	note that the code currently assumes parsing an association
 *	(or re-association) response frame if this is given
 * @from_ap: frame is received from an AP (currently used only
 *	for EHT capabilities parsing)
 */
struct ieee80211_elems_parse_params {
	enum ieee80211_conn_mode mode;
	const u8 *start;
	size_t len;
	bool action;
	u64 filter;
	u32 crc;
	struct cfg80211_bss *bss;
	int link_id;
	bool from_ap;
};

struct ieee802_11_elems *
ieee802_11_parse_elems_full(struct ieee80211_elems_parse_params *params);

static inline struct ieee802_11_elems *
ieee802_11_parse_elems_crc(const u8 *start, size_t len, bool action,
			   u64 filter, u32 crc,
			   struct cfg80211_bss *bss)
{
	struct ieee80211_elems_parse_params params = {
		.mode = IEEE80211_CONN_MODE_HIGHEST,
		.start = start,
		.len = len,
		.action = action,
		.filter = filter,
		.crc = crc,
		.bss = bss,
		.link_id = -1,
	};

	return ieee802_11_parse_elems_full(&params);
}

static inline struct ieee802_11_elems *
ieee802_11_parse_elems(const u8 *start, size_t len, bool action,
		       struct cfg80211_bss *bss)
{
	return ieee802_11_parse_elems_crc(start, len, action, 0, 0, bss);
}

extern const int ieee802_1d_to_ac[8];

static inline int ieee80211_ac_from_tid(int tid)
{
	return ieee802_1d_to_ac[tid & 7];
}

void ieee80211_dynamic_ps_enable_work(struct wiphy *wiphy,
				      struct wiphy_work *work);
void ieee80211_dynamic_ps_disable_work(struct wiphy *wiphy,
				       struct wiphy_work *work);
void ieee80211_dynamic_ps_timer(struct timer_list *t);
void ieee80211_send_nullfunc(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     bool powersave);
void ieee80211_send_4addr_nullfunc(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_tx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr, bool ack, u16 tx_time);
unsigned int
ieee80211_get_vif_queues(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata);
void ieee80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     unsigned long queues,
				     enum queue_stop_reason reason,
				     bool refcounted);
void ieee80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				     unsigned long queues,
				     enum queue_stop_reason reason,
				     bool refcounted);
void ieee80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason,
				    bool refcounted);
void ieee80211_stop_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason,
				    bool refcounted);
static inline void
ieee80211_stop_vif_queues(struct ieee80211_local *local,
			  struct ieee80211_sub_if_data *sdata,
			  enum queue_stop_reason reason)
{
	ieee80211_stop_queues_by_reason(&local->hw,
					ieee80211_get_vif_queues(local, sdata),
					reason, true);
}

static inline void
ieee80211_wake_vif_queues(struct ieee80211_local *local,
			  struct ieee80211_sub_if_data *sdata,
			  enum queue_stop_reason reason)
{
	ieee80211_wake_queues_by_reason(&local->hw,
					ieee80211_get_vif_queues(local, sdata),
					reason, true);
}
static inline void
ieee80211_stop_vif_queues_norefcount(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     enum queue_stop_reason reason)
{
	ieee80211_stop_queues_by_reason(&local->hw,
					ieee80211_get_vif_queues(local, sdata),
					reason, false);
}
static inline void
ieee80211_wake_vif_queues_norefcount(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     enum queue_stop_reason reason)
{
	ieee80211_wake_queues_by_reason(&local->hw,
					ieee80211_get_vif_queues(local, sdata),
					reason, false);
}
void ieee80211_add_pending_skb(struct ieee80211_local *local,
			       struct sk_buff *skb);
void ieee80211_add_pending_skbs(struct ieee80211_local *local,
				struct sk_buff_head *skbs);
void ieee80211_flush_queues(struct ieee80211_local *local,
			    struct ieee80211_sub_if_data *sdata, bool drop);
void __ieee80211_flush_queues(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      unsigned int queues, bool drop);

static inline bool ieee80211_can_run_worker(struct ieee80211_local *local)
{
	/*
	 * It's unsafe to try to do any work during reconfigure flow.
	 * When the flow ends the work will be requeued.
	 */
	if (local->in_reconfig)
		return false;

	/*
	 * If quiescing is set, we are racing with __ieee80211_suspend.
	 * __ieee80211_suspend flushes the workers after setting quiescing,
	 * and we check quiescing / suspended before enqueuing new workers.
	 * We should abort the worker to avoid the races below.
	 */
	if (local->quiescing)
		return false;

	/*
	 * We might already be suspended if the following scenario occurs:
	 * __ieee80211_suspend		Control path
	 *
	 *				if (local->quiescing)
	 *					return;
	 * local->quiescing = true;
	 * flush_workqueue();
	 *				queue_work(...);
	 * local->suspended = true;
	 * local->quiescing = false;
	 *				worker starts running...
	 */
	if (local->suspended)
		return false;

	return true;
}

int ieee80211_txq_setup_flows(struct ieee80211_local *local);
void ieee80211_txq_set_params(struct ieee80211_local *local, int radio_idx);
void ieee80211_txq_teardown_flows(struct ieee80211_local *local);
void ieee80211_txq_init(struct ieee80211_sub_if_data *sdata,
			struct sta_info *sta,
			struct txq_info *txq, int tid);
void ieee80211_txq_purge(struct ieee80211_local *local,
			 struct txq_info *txqi);
void ieee80211_purge_sta_txqs(struct sta_info *sta);
void ieee80211_txq_remove_vlan(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata);
void ieee80211_fill_txq_stats(struct cfg80211_txq_stats *txqstats,
			      struct txq_info *txqi);
void ieee80211_wake_txqs(struct tasklet_struct *t);
void ieee80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg, u16 status,
			 const u8 *extra, size_t extra_len, const u8 *bssid,
			 const u8 *da, const u8 *key, u8 key_len, u8 key_idx,
			 u32 tx_flags);
void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
				    const u8 *da, const u8 *bssid,
				    u16 stype, u16 reason,
				    bool send_frame, u8 *frame_buf);

enum {
	IEEE80211_PROBE_FLAG_DIRECTED		= BIT(0),
	IEEE80211_PROBE_FLAG_MIN_CONTENT	= BIT(1),
	IEEE80211_PROBE_FLAG_RANDOM_SN		= BIT(2),
};

int ieee80211_build_preq_ies(struct ieee80211_sub_if_data *sdata, u8 *buffer,
			     size_t buffer_len,
			     struct ieee80211_scan_ies *ie_desc,
			     const u8 *ie, size_t ie_len,
			     u8 bands_used, u32 *rate_masks,
			     struct cfg80211_chan_def *chandef,
			     u32 flags);
struct sk_buff *ieee80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  const u8 *src, const u8 *dst,
					  u32 ratemask,
					  struct ieee80211_channel *chan,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  u32 flags);
u32 ieee80211_sta_get_rates(struct ieee80211_sub_if_data *sdata,
			    struct ieee802_11_elems *elems,
			    enum nl80211_band band, u32 *basic_rates);
int __ieee80211_request_smps_mgd(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_link_data *link,
				 enum ieee80211_smps_mode smps_mode);
void ieee80211_recalc_smps(struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_link_data *link);
void ieee80211_recalc_min_chandef(struct ieee80211_sub_if_data *sdata,
				  int link_id);

size_t ieee80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset);
u8 *ieee80211_ie_build_ht_cap(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			      u16 cap);
u8 *ieee80211_ie_build_ht_oper(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			       const struct cfg80211_chan_def *chandef,
			       u16 prot_mode, bool rifs_mode);
void ieee80211_ie_build_wide_bw_cs(u8 *pos,
				   const struct cfg80211_chan_def *chandef);
u8 *ieee80211_ie_build_vht_cap(u8 *pos, struct ieee80211_sta_vht_cap *vht_cap,
			       u32 cap);
u8 *ieee80211_ie_build_vht_oper(u8 *pos, struct ieee80211_sta_vht_cap *vht_cap,
				const struct cfg80211_chan_def *chandef);
u8 ieee80211_ie_len_he_cap(struct ieee80211_sub_if_data *sdata);
u8 *ieee80211_ie_build_he_oper(u8 *pos, const struct cfg80211_chan_def *chandef);
u8 *ieee80211_ie_build_eht_oper(u8 *pos, const struct cfg80211_chan_def *chandef,
				const struct ieee80211_sta_eht_cap *eht_cap);
int ieee80211_parse_bitrates(enum nl80211_chan_width width,
			     const struct ieee80211_supported_band *sband,
			     const u8 *srates, int srates_len, u32 *rates);
u8 *ieee80211_add_wmm_info_ie(u8 *buf, u8 qosinfo);
void ieee80211_add_s1g_capab_ie(struct ieee80211_sub_if_data *sdata,
				struct ieee80211_sta_s1g_cap *caps,
				struct sk_buff *skb);
void ieee80211_add_aid_request_ie(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb);

/* element building in SKBs */
int ieee80211_put_srates_elem(struct sk_buff *skb,
			      const struct ieee80211_supported_band *sband,
			      u32 basic_rates, u32 masked_rates,
			      u8 element_id);
int ieee80211_put_he_cap(struct sk_buff *skb,
			 struct ieee80211_sub_if_data *sdata,
			 const struct ieee80211_supported_band *sband,
			 const struct ieee80211_conn_settings *conn);
int ieee80211_put_he_6ghz_cap(struct sk_buff *skb,
			      struct ieee80211_sub_if_data *sdata,
			      enum ieee80211_smps_mode smps_mode);
int ieee80211_put_eht_cap(struct sk_buff *skb,
			  struct ieee80211_sub_if_data *sdata,
			  const struct ieee80211_supported_band *sband,
			  const struct ieee80211_conn_settings *conn);
int ieee80211_put_reg_conn(struct sk_buff *skb,
			   enum ieee80211_channel_flags flags);

/* channel management */
bool ieee80211_chandef_ht_oper(const struct ieee80211_ht_operation *ht_oper,
			       struct cfg80211_chan_def *chandef);
bool ieee80211_chandef_vht_oper(struct ieee80211_hw *hw, u32 vht_cap_info,
				const struct ieee80211_vht_operation *oper,
				const struct ieee80211_ht_operation *htop,
				struct cfg80211_chan_def *chandef);
void ieee80211_chandef_eht_oper(const struct ieee80211_eht_operation_info *info,
				struct cfg80211_chan_def *chandef);
bool ieee80211_chandef_he_6ghz_oper(struct ieee80211_local *local,
				    const struct ieee80211_he_operation *he_oper,
				    const struct ieee80211_eht_operation *eht_oper,
				    struct cfg80211_chan_def *chandef);
bool ieee80211_chandef_s1g_oper(const struct ieee80211_s1g_oper_ie *oper,
				struct cfg80211_chan_def *chandef);
void ieee80211_chandef_downgrade(struct cfg80211_chan_def *chandef,
				 struct ieee80211_conn_settings *conn);
static inline void
ieee80211_chanreq_downgrade(struct ieee80211_chan_req *chanreq,
			    struct ieee80211_conn_settings *conn)
{
	ieee80211_chandef_downgrade(&chanreq->oper, conn);
	if (WARN_ON(!conn))
		return;
	if (conn->mode < IEEE80211_CONN_MODE_EHT)
		chanreq->ap.chan = NULL;
}

bool ieee80211_chanreq_identical(const struct ieee80211_chan_req *a,
				 const struct ieee80211_chan_req *b);

int __must_check
_ieee80211_link_use_channel(struct ieee80211_link_data *link,
			    const struct ieee80211_chan_req *req,
			    enum ieee80211_chanctx_mode mode,
			    bool assign_on_failure);

static inline int __must_check
ieee80211_link_use_channel(struct ieee80211_link_data *link,
			   const struct ieee80211_chan_req *req,
			   enum ieee80211_chanctx_mode mode)
{
	return _ieee80211_link_use_channel(link, req, mode, false);
}

int __must_check
ieee80211_link_reserve_chanctx(struct ieee80211_link_data *link,
			       const struct ieee80211_chan_req *req,
			       enum ieee80211_chanctx_mode mode,
			       bool radar_required);
int __must_check
ieee80211_link_use_reserved_context(struct ieee80211_link_data *link);
void ieee80211_link_unreserve_chanctx(struct ieee80211_link_data *link);

int __must_check
ieee80211_link_change_chanreq(struct ieee80211_link_data *link,
			      const struct ieee80211_chan_req *req,
			      u64 *changed);
void __ieee80211_link_release_channel(struct ieee80211_link_data *link,
				      bool skip_idle_recalc);
void ieee80211_link_release_channel(struct ieee80211_link_data *link);
void ieee80211_link_vlan_copy_chanctx(struct ieee80211_link_data *link);
void ieee80211_link_copy_chanctx_to_vlans(struct ieee80211_link_data *link,
					  bool clear);
int ieee80211_chanctx_refcount(struct ieee80211_local *local,
			       struct ieee80211_chanctx *ctx);

void ieee80211_recalc_smps_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *chanctx);
void ieee80211_recalc_chanctx_min_def(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      struct ieee80211_link_data *rsvd_for,
				      bool check_reserved);
bool ieee80211_is_radar_required(struct ieee80211_local *local,
				 struct cfg80211_scan_request *req);
bool ieee80211_is_radio_idx_in_scan_req(struct wiphy *wiphy,
					struct cfg80211_scan_request *scan_req,
					int radio_idx);

void ieee80211_dfs_cac_timer_work(struct wiphy *wiphy, struct wiphy_work *work);
void ieee80211_dfs_cac_cancel(struct ieee80211_local *local,
			      struct ieee80211_chanctx *chanctx);
void ieee80211_dfs_radar_detected_work(struct wiphy *wiphy,
				       struct wiphy_work *work);
int ieee80211_send_action_csa(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings);
void ieee80211_recalc_sb_count(struct ieee80211_sub_if_data *sdata, u64 tsf);
void ieee80211_recalc_dtim(struct ieee80211_sub_if_data *sdata, u64 tsf);
int ieee80211_check_combinations(struct ieee80211_sub_if_data *sdata,
				 const struct cfg80211_chan_def *chandef,
				 enum ieee80211_chanctx_mode chanmode,
				 u8 radar_detect, int radio_idx);
int ieee80211_max_num_channels(struct ieee80211_local *local, int radio_idx);
u32 ieee80211_get_radio_mask(struct wiphy *wiphy, struct net_device *dev);
void ieee80211_recalc_chanctx_chantype(struct ieee80211_local *local,
				       struct ieee80211_chanctx *ctx);

/* TDLS */
int ieee80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, int link_id,
			u8 action_code, u8 dialog_token, u16 status_code,
			u32 peer_capability, bool initiator,
			const u8 *extra_ies, size_t extra_ies_len);
int ieee80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, enum nl80211_tdls_operation oper);
void ieee80211_tdls_peer_del_work(struct wiphy *wiphy, struct wiphy_work *wk);
int ieee80211_tdls_channel_switch(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *addr, u8 oper_class,
				  struct cfg80211_chan_def *chandef);
void ieee80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
					  struct net_device *dev,
					  const u8 *addr);
void ieee80211_teardown_tdls_peers(struct ieee80211_link_data *link);
void ieee80211_tdls_handle_disconnect(struct ieee80211_sub_if_data *sdata,
				      const u8 *peer, u16 reason);
void
ieee80211_process_tdls_channel_switch(struct ieee80211_sub_if_data *sdata,
				      struct sk_buff *skb);


const char *ieee80211_get_reason_code_string(u16 reason_code);
u16 ieee80211_encode_usf(int val);
u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum nl80211_iftype type);

extern const struct ethtool_ops ieee80211_ethtool_ops;

u32 ieee80211_calc_expected_tx_airtime(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *pubsta,
				       int len, bool ampdu);
#ifdef CONFIG_MAC80211_NOINLINE
#define debug_noinline noinline
#else
#define debug_noinline
#endif

void ieee80211_init_frag_cache(struct ieee80211_fragment_cache *cache);
void ieee80211_destroy_frag_cache(struct ieee80211_fragment_cache *cache);

u8 ieee80211_ie_len_eht_cap(struct ieee80211_sub_if_data *sdata);

void
ieee80211_eht_cap_ie_to_sta_eht_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const u8 *he_cap_ie, u8 he_cap_len,
				    const struct ieee80211_eht_cap_elem *eht_cap_ie_elem,
				    u8 eht_cap_len,
				    struct link_sta_info *link_sta);
void ieee80211_process_neg_ttlm_req(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_mgmt *mgmt, size_t len);
void ieee80211_process_neg_ttlm_res(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_mgmt *mgmt, size_t len);
int ieee80211_req_neg_ttlm(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_ttlm_params *params);
void ieee80211_process_ttlm_teardown(struct ieee80211_sub_if_data *sdata);

void ieee80211_check_wbrf_support(struct ieee80211_local *local);
void ieee80211_add_wbrf(struct ieee80211_local *local, struct cfg80211_chan_def *chandef);
void ieee80211_remove_wbrf(struct ieee80211_local *local, struct cfg80211_chan_def *chandef);
int ieee80211_mgd_set_epcs(struct ieee80211_sub_if_data *sdata, bool enable);
void ieee80211_process_epcs_ena_resp(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len);
void ieee80211_process_epcs_teardown(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len);

int ieee80211_mgd_assoc_ml_reconf(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_ml_reconf_req *req);

void ieee80211_process_ml_reconf_resp(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_mgmt *mgmt, size_t len);
void ieee80211_stop_mbssid(struct ieee80211_sub_if_data *sdata);

#if IS_ENABLED(CONFIG_MAC80211_KUNIT_TEST)
#define EXPORT_SYMBOL_IF_MAC80211_KUNIT(sym) EXPORT_SYMBOL_IF_KUNIT(sym)
#define VISIBLE_IF_MAC80211_KUNIT
ieee80211_rx_result
ieee80211_drop_unencrypted_mgmt(struct ieee80211_rx_data *rx);
int ieee80211_calc_chandef_subchan_offset(const struct cfg80211_chan_def *ap,
					  u8 n_partial_subchans);
void ieee80211_rearrange_tpe_psd(struct ieee80211_parsed_tpe_psd *psd,
				 const struct cfg80211_chan_def *ap,
				 const struct cfg80211_chan_def *used);
struct ieee802_11_elems *
ieee80211_determine_chan_mode(struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_conn_settings *conn,
			      struct cfg80211_bss *cbss, int link_id,
			      struct ieee80211_chan_req *chanreq,
			      struct cfg80211_chan_def *ap_chandef,
			      unsigned long *userspace_selectors);
#else
#define EXPORT_SYMBOL_IF_MAC80211_KUNIT(sym)
#define VISIBLE_IF_MAC80211_KUNIT static
#endif

#endif /* IEEE80211_I_H */
