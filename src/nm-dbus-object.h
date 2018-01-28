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

#ifndef __NM_DBUS_OBJECT_H__
#define __NM_DBUS_OBJECT_H__

/*****************************************************************************/

#define NM_TYPE_DBUS_OBJECT            (nm_dbus_object_get_type ())
#define NM_DBUS_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DBUS_OBJECT, NMDBusObject))
#define NM_DBUS_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NM_TYPE_DBUS_OBJECT, NMDBusObjectClass))
#define NM_IS_DBUS_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DBUS_OBJECT))
#define NM_IS_DBUS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  NM_TYPE_DBUS_OBJECT))
#define NM_DBUS_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  NM_TYPE_DBUS_OBJECT, NMDBusObjectClass))

#define NM_DBUS_OBJECT_PATH "path"

struct _NMDBusObjectPrivate;

struct _NMDBusObject {
	GObject parent;
	struct _NMDBusObjectPrivate *_priv;
};

typedef struct {
	GObjectClass parent;

	const char *export_path;

	GDBusInterfaceInfo **interface_infos;

	bool export_on_construction;
} NMDBusObjectClass;

GType nm_dbus_object_get_type (void);

const char *nm_dbus_object_export      (NMDBusObject *self);
const char *nm_dbus_object_get_path    (NMDBusObject *self);
gboolean    nm_dbus_object_is_exported (NMDBusObject *self);
void        nm_dbus_object_unexport    (NMDBusObject *self);

void        _nm_dbus_object_clear_and_unexport (NMDBusObject **location);
#define nm_dbus_object_clear_and_unexport(location) _nm_dbus_object_clear_and_unexport ((NMDBusObject **) (location))

void _nm_dbus_object_register_object (NMDBusObject *self,
                                      GDBusConnection dbus_connection);

void _nm_dbus_object_unregister_object (NMDBusObject *self,
                                        GDBusConnection dbus_connection);

#endif /* __NM_DBUS_OBJECT_H__ */
