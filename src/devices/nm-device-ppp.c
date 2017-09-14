/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-device-ppp.h"

#include "nm-act-request.h"
#include "nm-device-factory.h"
#include "nm-device-private.h"
#include "nm-manager.h"
#include "nm-setting-pppoe.h"
#include "platform/nm-platform.h"
#include "ppp/nm-ppp-manager.h"
#include "ppp/nm-ppp-manager-call.h"
#include "ppp/nm-ppp-status.h"

#include "introspection/org.freedesktop.NetworkManager.Device.Ppp.h"

#include "nm-device-logging.h"
_LOG_DECLARE_SELF(NMDevicePpp);

/*****************************************************************************/

typedef struct _NMDevicePppPrivate {
	NMPPPManager *ppp_manager;
	NMIP4Config  *pending_ip4_config;
	char         *pending_ifname;
} NMDevicePppPrivate;

struct _NMDevicePpp {
	NMDevice parent;
	NMDevicePppPrivate _priv;
};

struct _NMDevicePppClass {
	NMDeviceClass parent;
};

G_DEFINE_TYPE (NMDevicePpp, nm_device_ppp, NM_TYPE_DEVICE)

#define NM_DEVICE_PPP_GET_PRIVATE(self) _NM_GET_PRIVATE (self, NMDevicePpp, NM_IS_DEVICE_PPP)

static gboolean
check_connection_compatible (NMDevice *device, NMConnection *connection)
{
	NMSettingPppoe *s_pppoe;

	if (!NM_DEVICE_CLASS (nm_device_ppp_parent_class)->check_connection_compatible (device, connection))
		return FALSE;

	if (!nm_streq0 (nm_connection_get_connection_type (connection),
	                NM_SETTING_PPPOE_SETTING_NAME))
		return FALSE;

	s_pppoe = nm_connection_get_setting_pppoe (connection);
	nm_assert (s_pppoe);

	return !!nm_setting_pppoe_get_parent (s_pppoe);
}

static NMDeviceCapabilities
get_generic_capabilities (NMDevice *device)
{
	return NM_DEVICE_CAP_IS_SOFTWARE;
}

static void
ppp_state_changed (NMPPPManager *ppp_manager, NMPPPStatus status, gpointer user_data)
{
	NMDevice *device = NM_DEVICE (user_data);

	switch (status) {
	case NM_PPP_STATUS_DISCONNECT:
		nm_device_state_changed (device, NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_PPP_DISCONNECT);
		break;
	case NM_PPP_STATUS_DEAD:
		nm_device_state_changed (device, NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_PPP_FAILED);
		break;
	case NM_PPP_STATUS_RUNNING:
		nm_device_activate_schedule_stage3_ip_config_start (device);
		break;
	default:
		break;
	}
}

static void
ppp_ip4_config (NMPPPManager *ppp_manager,
                const char *iface,
                NMIP4Config *config,
                gpointer user_data)
{
	NMDevice *device = NM_DEVICE (user_data);
	NMDevicePpp *self = NM_DEVICE_PPP (device);
	NMDevicePppPrivate *priv = NM_DEVICE_PPP_GET_PRIVATE (self);
	gboolean renamed;

	_LOGT (LOGD_DEVICE | LOGD_PPP, "received IPv4 config from pppd");

	if (nm_device_get_state (device) == NM_DEVICE_STATE_IP_CONFIG) {
		if (nm_device_activate_ip4_state_in_conf (device)) {
			if (!nm_device_take_over_link (device, iface, &renamed)) {
				nm_device_state_changed (device, NM_DEVICE_STATE_FAILED,
				                         NM_DEVICE_STATE_REASON_IP_CONFIG_UNAVAILABLE);
				return;
			}
			if (renamed)
				nm_manager_remove_device (nm_manager_get (), iface);

			nm_device_activate_schedule_ip4_config_result (device, config);
			return;
		}
	} else {
		if (priv->pending_ip4_config)
			g_object_unref (priv->pending_ip4_config);
		priv->pending_ip4_config = g_object_ref (config);
		g_free (priv->pending_ifname);
		priv->pending_ifname = g_strdup (iface);
	}
}

static NMActStageReturn
act_stage2_config (NMDevice *device, NMDeviceStateReason *out_failure_reason)
{
	NMDevicePpp *self = NM_DEVICE_PPP (device);
	NMDevicePppPrivate *priv = NM_DEVICE_PPP_GET_PRIVATE (self);
	NMSettingPppoe *s_pppoe;
	NMActRequest *req;
	GError *err = NULL;

	req = nm_device_get_act_request (NM_DEVICE (self));
	g_return_val_if_fail (req, NM_ACT_STAGE_RETURN_FAILURE);

	s_pppoe = (NMSettingPppoe *) nm_device_get_applied_setting ((NMDevice *) self, NM_TYPE_SETTING_PPPOE);
	g_return_val_if_fail (s_pppoe, NM_ACT_STAGE_RETURN_FAILURE);

	g_clear_object (&priv->pending_ip4_config);
	nm_clear_g_free (&priv->pending_ifname);

	priv->ppp_manager = nm_ppp_manager_create (nm_setting_pppoe_get_parent (s_pppoe), &err);

	if (   !priv->ppp_manager
	    || !nm_ppp_manager_start (priv->ppp_manager, req,
	                              nm_setting_pppoe_get_username (s_pppoe),
	                              30, 0, &err)) {
		_LOGW (LOGD_DEVICE | LOGD_PPP, "PPPoE failed to start: %s", err->message);
		g_error_free (err);

		g_clear_object (&priv->ppp_manager);

		NM_SET_OUT (out_failure_reason, NM_DEVICE_STATE_REASON_PPP_START_FAILED);
		return NM_ACT_STAGE_RETURN_FAILURE;
	}

	g_signal_connect (priv->ppp_manager, NM_PPP_MANAGER_SIGNAL_STATE_CHANGED,
	                  G_CALLBACK (ppp_state_changed),
	                  self);
	g_signal_connect (priv->ppp_manager, NM_PPP_MANAGER_SIGNAL_IP4_CONFIG,
	                  G_CALLBACK (ppp_ip4_config),
	                  self);

	return NM_ACT_STAGE_RETURN_POSTPONE;
}

static NMActStageReturn
act_stage3_ip4_config_start (NMDevice *device,
                             NMIP4Config **out_config,
                             NMDeviceStateReason *out_failure_reason)
{
	NMDevicePpp *self = NM_DEVICE_PPP (device);
	NMDevicePppPrivate *priv = NM_DEVICE_PPP_GET_PRIVATE (self);
	gboolean renamed;

	if (priv->pending_ip4_config) {
		if  (!nm_device_take_over_link (device, priv->pending_ifname, &renamed))
			return NM_ACT_STAGE_RETURN_FAILURE;
		if (renamed)
			nm_manager_remove_device (nm_manager_get (), priv->pending_ifname);
		if (out_config)
			*out_config = g_steal_pointer (&priv->pending_ip4_config);
		else
			g_clear_object (&priv->pending_ip4_config);
		return NM_ACT_STAGE_RETURN_SUCCESS;
	}

	/* Wait IPCP termination */
	return NM_ACT_STAGE_RETURN_POSTPONE;
}

static gboolean
create_and_realize (NMDevice *device,
                    NMConnection *connection,
                    NMDevice *parent,
                    const NMPlatformLink **out_plink,
                    GError **error)
{
	int parent_ifindex;

	if (!parent) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_MISSING_DEPENDENCIES,
		             "PPP devices can not be created without a parent interface");
		return FALSE;
	}

	parent_ifindex = nm_device_get_ifindex (parent);
	g_warn_if_fail (parent_ifindex > 0);

	nm_device_parent_set_ifindex (device, parent_ifindex);

	/* The interface is created later */

	return TRUE;
}

static void
deactivate (NMDevice *device)
{
	NMDevicePpp *self = NM_DEVICE_PPP (device);
	NMDevicePppPrivate *priv = NM_DEVICE_PPP_GET_PRIVATE (self);

	if (priv->ppp_manager) {
		nm_ppp_manager_stop_sync (priv->ppp_manager);
		g_clear_object (&priv->ppp_manager);
	}
}

static void
nm_device_ppp_init (NMDevicePpp *self)
{
}

static void
dispose (GObject *object)
{
	NMDevicePpp *self = NM_DEVICE_PPP (object);
	NMDevicePppPrivate *priv = NM_DEVICE_PPP_GET_PRIVATE (self);

	g_clear_object (&priv->pending_ip4_config);
	nm_clear_g_free (&priv->pending_ifname);

	G_OBJECT_CLASS (nm_device_ppp_parent_class)->dispose (object);
}

static void
nm_device_ppp_class_init (NMDevicePppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *parent_class = NM_DEVICE_CLASS (klass);

	NM_DEVICE_CLASS_DECLARE_TYPES (klass, NM_SETTING_PPPOE_SETTING_NAME, NM_LINK_TYPE_PPP)

	object_class->dispose = dispose;

	parent_class->act_stage2_config = act_stage2_config;
	parent_class->act_stage3_ip4_config_start = act_stage3_ip4_config_start;
	parent_class->check_connection_compatible = check_connection_compatible;
	parent_class->create_and_realize = create_and_realize;
	parent_class->deactivate = deactivate;
	parent_class->get_generic_capabilities = get_generic_capabilities;

	nm_exported_object_class_add_interface (NM_EXPORTED_OBJECT_CLASS (klass),
	                                        NMDBUS_TYPE_DEVICE_PPP_SKELETON,
	                                        NULL);
}

/*****************************************************************************/

#define NM_TYPE_PPP_DEVICE_FACTORY (nm_ppp_device_factory_get_type ())
#define NM_PPP_DEVICE_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_PPP_DEVICE_FACTORY, NMPppDeviceFactory))

static NMDevice *
create_device (NMDeviceFactory *factory,
               const char *iface,
               const NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	return (NMDevice *) g_object_new (NM_TYPE_DEVICE_PPP,
	                                  NM_DEVICE_IFACE, iface,
	                                  NM_DEVICE_TYPE_DESC, "Ppp",
	                                  NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_PPP,
	                                  NM_DEVICE_LINK_TYPE, NM_LINK_TYPE_PPP,
	                                  NULL);
}

static gboolean
match_connection (NMDeviceFactory *factory, NMConnection *connection)
{
	NMSettingPppoe *s_pppoe;

	s_pppoe = nm_connection_get_setting_pppoe (connection);
	nm_assert (s_pppoe);

	return !!nm_setting_pppoe_get_parent (s_pppoe);
}

static const char *
get_connection_parent (NMDeviceFactory *factory, NMConnection *connection)
{
	NMSettingPppoe *s_pppoe;

	nm_assert (nm_connection_is_type (connection, NM_SETTING_PPPOE_SETTING_NAME));

	s_pppoe = nm_connection_get_setting_pppoe (connection);
	nm_assert (s_pppoe);

	return nm_setting_pppoe_get_parent (s_pppoe);
}

static char *
get_connection_iface (NMDeviceFactory *factory,
                      NMConnection *connection,
                      const char *parent_iface)
{
	nm_assert (nm_connection_is_type (connection, NM_SETTING_PPPOE_SETTING_NAME));

	if (!parent_iface)
		return NULL;

	return g_strdup (nm_connection_get_interface_name (connection));
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL (PPP, Ppp, ppp,
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES (NM_LINK_TYPE_PPP)
	NM_DEVICE_FACTORY_DECLARE_SETTING_TYPES (NM_SETTING_PPPOE_SETTING_NAME),
	factory_class->get_connection_parent = get_connection_parent;
	factory_class->get_connection_iface = get_connection_iface;
	factory_class->create_device = create_device;
	factory_class->match_connection = match_connection;
);
