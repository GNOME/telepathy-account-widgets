/*
 * empathy-connection-aggregator.c - Source for EmpathyConnectionAggregator
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <config.h>

#include "empathy-connection-aggregator.h"

#include <telepathy-glib/telepathy-glib.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"
#include "empathy-utils.h"


#include "extensions/extensions.h"

G_DEFINE_TYPE (EmpathyConnectionAggregator, empathy_connection_aggregator, 
    G_TYPE_OBJECT);

struct _EmpathyConnectionAggregatorPriv {
  TpAccountManager *mgr;

  /* List of owned TpConnection */
  GList *conns;
};

static void
empathy_connection_aggregator_dispose (GObject *object)
{
  EmpathyConnectionAggregator *self = (EmpathyConnectionAggregator *) object;

  g_clear_object (&self->priv->mgr);

  g_list_free_full (self->priv->conns, g_object_unref);
  self->priv->conns = NULL;

  G_OBJECT_CLASS (empathy_connection_aggregator_parent_class)->dispose (object);
}

static void
empathy_connection_aggregator_class_init (
    EmpathyConnectionAggregatorClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->dispose = empathy_connection_aggregator_dispose;

  g_type_class_add_private (klass, sizeof (EmpathyConnectionAggregatorPriv));
}

static void
conn_invalidated_cb (TpConnection *conn,
    guint domain,
    gint code,
    gchar *message,
    EmpathyConnectionAggregator *self)
{
  self->priv->conns = g_list_remove (self->priv->conns, conn);

  g_object_unref (conn);
}

static void
check_connection (EmpathyConnectionAggregator *self,
    TpConnection *conn)
{
  if (g_list_find (self->priv->conns, conn) != NULL)
    return;

  self->priv->conns = g_list_prepend (self->priv->conns,
      g_object_ref (conn));

  tp_g_signal_connect_object (conn, "invalidated",
      G_CALLBACK (conn_invalidated_cb), self, 0);
}

static void
check_account (EmpathyConnectionAggregator *self,
    TpAccount *account)
{
  TpConnection *conn;

  conn = tp_account_get_connection (account);
  if (conn != NULL)
    check_connection (self, conn);
}

static void
account_conn_changed_cb (TpAccount *account,
    GParamSpec *spec,
    EmpathyConnectionAggregator *self)
{
  check_account (self, account);
}

static void
add_account (EmpathyConnectionAggregator *self,
    TpAccount *account)
{
  check_account (self, account);

  tp_g_signal_connect_object (account, "notify::connection",
      G_CALLBACK (account_conn_changed_cb), self, 0);
}

static void
account_validity_changed_cb (TpAccountManager *manager,
    TpAccount *account,
    gboolean valid,
    EmpathyConnectionAggregator *self)
{
  if (valid)
    add_account (self, account);
}

static void
am_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyConnectionAggregator *self = EMPATHY_CONNECTION_AGGREGATOR (user_data);
  GError *error = NULL;
  GList *accounts, *l;

  if (!tp_proxy_prepare_finish (source, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      goto out;
    }

  accounts = tp_account_manager_get_valid_accounts (self->priv->mgr);
  for (l = accounts; l != NULL; l = g_list_next (l))
    {
      TpAccount *account = l->data;

      add_account (self, account);
    }

  tp_g_signal_connect_object (self->priv->mgr, "account-validity-changed",
      G_CALLBACK (account_validity_changed_cb), self, 0);

  g_list_free (accounts);

out:
  g_object_unref (self);
}

static void
empathy_connection_aggregator_init (EmpathyConnectionAggregator *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_CONNECTION_AGGREGATOR, EmpathyConnectionAggregatorPriv);

  self->priv->mgr = tp_account_manager_dup ();

  tp_proxy_prepare_async (self->priv->mgr, NULL, am_prepare_cb,
      g_object_ref (self));
}

EmpathyConnectionAggregator *
empathy_connection_aggregator_dup_singleton (void)
{
  static EmpathyConnectionAggregator *aggregator = NULL;

  if (G_LIKELY (aggregator != NULL))
      return g_object_ref (aggregator);

  aggregator = g_object_new (EMPATHY_TYPE_CONNECTION_AGGREGATOR, NULL);

  g_object_add_weak_pointer (G_OBJECT (aggregator), (gpointer *) &aggregator);
  return aggregator;
}