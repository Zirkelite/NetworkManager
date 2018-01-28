/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright 2018 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-dbus-object.h"

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE (NMDBusObject,
	PROP_PATH,
);

typedef struct _NMDBusObjectPrivate {
	char *path;
} NMDBusObjectPrivate;

G_DEFINE_ABSTRACT_TYPE (NMDBusObject, nm_dbus_object, G_TYPE_DBUS_OBJECT_SKELETON);

#define NM_DBUS_OBJECT_GET_PRIVATE(self) _NM_GET_PRIVATE_PTR (self, NMDBusObject, NM_IS_DBUS_OBJECT)

/*****************************************************************************/

#define _NMLOG_DOMAIN      LOGD_CORE
#define _NMLOG(level, ...) __NMLOG_DEFAULT_WITH_ADDR (level, _NMLOG_DOMAIN, "dbus-object", __VA_ARGS__)

#define _NMLOG2_DOMAIN      LOGD_DBUS_PROPS
#define _NMLOG2(level, ...) __NMLOG_DEFAULT_WITH_ADDR (level, _NMLOG2_DOMAIN, "properties-changed", __VA_ARGS__)

/*****************************************************************************/

static char *
_create_export_path (NMDBusObjectClass *klass)
{
	const char *class_export_path, *p;
	static GHashTable *prefix_counters;
	guint64 *counter;

	class_export_path = klass->export_path;

	nm_assert (class_export_path);

	p = strchr (class_export_path, '%');
	if (p) {
		if (G_UNLIKELY (!prefix_counters))
			prefix_counters = g_hash_table_new (nm_str_hash, g_str_equal);

		nm_assert (p[1] == 'l');
		nm_assert (p[2] == 'l');
		nm_assert (p[3] == 'u');
		nm_assert (p[4] == '\0');

		counter = g_hash_table_lookup (prefix_counters, class_export_path);
		if (!counter) {
			counter = g_slice_new0 (guint64);
			g_hash_table_insert (prefix_counters, (char *) class_export_path, counter);
		}

		NM_PRAGMA_WARNING_DISABLE("-Wformat-nonliteral")
		return g_strdup_printf (class_export_path, (unsigned long long) (++(*counter)));
		NM_PRAGMA_WARNING_REENABLE
	}

	return g_strdup (class_export_path);
}

/**
 * nm_dbus_object_get_path:
 * @self: an #NMDBusObject
 *
 * Gets @self's D-Bus path.
 *
 * Returns: @self's D-Bus path, or %NULL if @self is not exported.
 */
const char *
nm_dbus_object_get_path (NMDBusObject *self)
{
	g_return_val_if_fail (NM_IS_DBUS_OBJECT (self), NULL);

	return NM_DBUS_OBJECT_GET_PRIVATE (self)->path;
}

/**
 * nm_dbus_object_is_exported:
 * @self: an #NMDBusObject
 *
 * Checks if @self is exported
 *
 * Returns: %TRUE if @self is exported
 */
gboolean
nm_dbus_object_is_exported (NMDBusObject *self)
{
	g_return_val_if_fail (NM_IS_DBUS_OBJECT (self), FALSE);

	return NM_DBUS_OBJECT_GET_PRIVATE (self)->path != NULL;
}

/**
 * nm_dbus_object_export:
 * @self: an #NMDBusObject
 *
 * Exports @self on all active and future D-Bus connections.
 *
 * The path to export @self on is taken from its #NMObjectClass's %export_path
 * member. If the %export_path contains "%u", then it will be replaced with a
 * monotonically increasing integer ID (with each distinct %export_path having
 * its own counter). Otherwise, %export_path will be used literally (implying
 * that @self must be a singleton).
 *
 * Returns: the path @self was exported under
 */
const char *
nm_dbus_object_export (NMDBusObject *self)
{
	NMDBusObjectPrivate *priv;

	g_return_val_if_fail (NM_IS_DBUS_OBJECT (self), NULL);
	priv = NM_DBUS_OBJECT_GET_PRIVATE (self);

	g_return_val_if_fail (!priv->path, priv->path);

	priv->path = _create_export_path (NM_DBUS_OBJECT_GET_CLASS (self));

	_LOGT ("export: \"%s\"", priv->path);

	_notify (self, PROP_PATH);
	return priv->path;
}

/**
 * nm_dbus_object_unexport:
 * @self: an #NMDBusObject
 *
 * Unexports @self on all active D-Bus connections (and prevents it from being
 * auto-exported on future connections).
 */
void
nm_dbus_object_unexport (NMDBusObject *self)
{
	NMDBusObjectPrivate *priv;

	g_return_if_fail (NM_IS_DBUS_OBJECT (self));
	priv = NM_DBUS_OBJECT_GET_PRIVATE (self);

	g_return_if_fail (priv->path);

	_LOGT ("unexport: \"%s\"", priv->path);

	g_clear_pointer (&priv->path, g_free);

	_notify (self, PROP_PATH);
}

/*****************************************************************************/

void
_nm_dbus_object_clear_and_unexport (NMDBusObject **location)
{
	NMDBusObject *self;
	NMDBusObjectPrivate *priv;

	if (!location || !*location)
		return;

	self = *location;
	*location = NULL;

	g_return_if_fail (NM_IS_DBUS_OBJECT (self));

	priv = NM_DBUS_OBJECT_GET_PRIVATE (self);

	if (priv->path)
		nm_dbus_object_unexport (self);

	g_object_unref (self);
}

/*****************************************************************************/

/*****************************************************************************/

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDBusObject *self = NM_DBUS_OBJECT (object);
	NMDBusObjectPrivate *priv = NM_DBUS_OBJECT_GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_PATH:
		g_value_set_string (value, priv->path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_dbus_object_init (NMDBusObject *self)
{
	NMDBusObjectPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NM_TYPE_DBUS_OBJECT, NMDBusObjectPrivate);
	self->_priv = priv;
}

static void
constructed (GObject *object)
{
	NMDBusObjectClass *klass;

	G_OBJECT_CLASS (nm_dbus_object_parent_class)->constructed (object);

	klass = NM_DBUS_OBJECT_GET_CLASS (object);

	if (klass->export_on_construction)
		nm_dbus_object_export ((NMDBusObject *) object);
}

static void
dispose (GObject *object)
{
	NMDBusObject *self = NM_DBUS_OBJECT (object);
	NMDBusObjectPrivate *priv = NM_DBUS_OBJECT_GET_PRIVATE (self);

	if (priv->path)
		nm_dbus_object_unexport (self);

	G_OBJECT_CLASS (nm_dbus_object_parent_class)->dispose (object);
}

static void
nm_dbus_object_class_init (NMDBusObjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMDBusObjectPrivate));

	object_class->constructed = constructed;
	object_class->dispose = dispose;
	object_class->get_property = get_property;

	obj_properties[PROP_PATH] =
	    g_param_spec_string (NM_DBUS_OBJECT_PATH, "", "",
	                         NULL,
	                         G_PARAM_READABLE |
	                         G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, _PROPERTY_ENUMS_LAST, obj_properties);
}
