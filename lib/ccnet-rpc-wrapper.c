/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "include.h"

#include <ccnet.h>
#include <ccnet-object.h>
#include <rpcsyncwerk-client.h>
#include <ccnet/ccnetrpc-transport.h>


RpcsyncwerkClient *
ccnet_create_rpc_client (CcnetClient *cclient,
                         const char *peer_id,
                         const char *service_name)
{
    RpcsyncwerkClient *rpc_client;
    CcnetrpcTransportParam *priv;
    
    priv = g_new0(CcnetrpcTransportParam, 1);
    priv->session = cclient;
    priv->peer_id = g_strdup(peer_id);
    priv->service = g_strdup(service_name);

    rpc_client = rpcsyncwerk_client_new ();
    rpc_client->send = ccnetrpc_transport_send;
    rpc_client->arg = priv;

    return rpc_client;
}

RpcsyncwerkClient *
ccnet_create_pooled_rpc_client (struct CcnetClientPool *cpool,
                                const char *peer_id,
                                const char *service)
{
    RpcsyncwerkClient *rpc_client;
    CcnetrpcTransportParam *priv;
    
    priv = g_new0(CcnetrpcTransportParam, 1);
    priv->pool = cpool;
    priv->peer_id = g_strdup(peer_id);
    priv->service = g_strdup(service);

    rpc_client = rpcsyncwerk_client_new ();
    rpc_client->send = ccnetrpc_transport_send;
    rpc_client->arg = priv;

    return rpc_client;
}

RpcsyncwerkClient *
ccnet_create_async_rpc_client (CcnetClient *cclient, const char *peer_id,
                               const char *service_name)
{
    RpcsyncwerkClient *rpc_client;
    CcnetrpcAsyncTransportParam *async_priv;

    async_priv = g_new0 (CcnetrpcAsyncTransportParam, 1);
    async_priv->session = cclient;
    async_priv->peer_id = g_strdup(peer_id);
    async_priv->service = g_strdup (service_name);

    rpc_client = rpcsyncwerk_client_new ();

    rpc_client->async_send = ccnetrpc_async_transport_send;
    rpc_client->async_arg = async_priv;

    return rpc_client;
}

void
ccnet_rpc_client_free (RpcsyncwerkClient *client)
{
    CcnetrpcTransportParam *priv;

    if (!client) return;

    priv = client->arg;

    g_free (priv->peer_id);
    g_free (priv->service);
    g_free (priv);

    rpcsyncwerk_client_free (client);
}

void
ccnet_async_rpc_client_free (RpcsyncwerkClient *client)
{
    CcnetrpcAsyncTransportParam *priv = client->arg;

    g_free (priv->peer_id);
    g_free (priv->service);
    g_free (priv);

    rpcsyncwerk_client_free (client);
}

CcnetPeer *
ccnet_get_peer (RpcsyncwerkClient *client, const char *peer_id)
{
    if (!peer_id)
        return NULL;

    return (CcnetPeer *) rpcsyncwerk_client_call__object(
        client, "get_peer",CCNET_TYPE_PEER, NULL,
        1, "string", peer_id);
}

CcnetPeer *
ccnet_get_peer_by_idname (RpcsyncwerkClient *client, const char *idname)
{
    if (!idname)
        return NULL;
    return (CcnetPeer *) rpcsyncwerk_client_call__object(
        client, "get_peer_by_idname", CCNET_TYPE_PEER, NULL,
        1, "string", idname);
}

int
ccnet_get_peer_net_state (RpcsyncwerkClient *client, const char *peer_id)
{
    CcnetPeer *peer;
    int ret;
    peer = ccnet_get_peer (client, peer_id);
    if (!peer)
        return PEER_DOWN;
    ret = peer->net_state;
    g_object_unref (peer);
    return ret;
}

int
ccnet_get_peer_bind_status (RpcsyncwerkClient *client, const char *peer_id)
{
    CcnetPeer *peer;
    int ret;
    peer = ccnet_get_peer (client, peer_id);
    if (!peer)
        return BIND_UNKNOWN;
    ret = peer->bind_status;
    g_object_unref (peer);
    return ret;
}

CcnetPeer *
ccnet_get_default_relay (RpcsyncwerkClient *client)
{
    CcnetSessionBase *base = (CcnetSessionBase *)
        rpcsyncwerk_client_call__object(
            client, "get_session_info", CCNET_TYPE_SESSION_BASE, NULL, 0);

    if (!base)
        return NULL;

    CcnetPeer *relay = ccnet_get_peer (client, base->relay_id);
    g_object_unref (base);
    return relay;
}

GList *
ccnet_get_peers_by_role (RpcsyncwerkClient *client, const char *role)
{
    if (!role)
        return NULL;

    return rpcsyncwerk_client_call__objlist (
        client, "get_peers_by_role", CCNET_TYPE_PEER, NULL,
        1, "string", role);
}

char *
ccnet_get_binding_email (RpcsyncwerkClient *client, const char *peer_id)
{
    if (!peer_id)
        return NULL;

    return rpcsyncwerk_client_call__string (
        client, "get_binding_email", NULL,
        1, "string", peer_id);
}

GList *
ccnet_get_groups_by_user (RpcsyncwerkClient *client, const char *user, int return_ancestors)
{
    if (!user)
        return NULL;

    return rpcsyncwerk_client_call__objlist (
        client, "get_groups", CCNET_TYPE_GROUP, NULL,
        2, "string", user, "int", return_ancestors);
}

GList *
ccnet_get_org_groups_by_user (RpcsyncwerkClient *client, const char *user, int org_id)
{
    if (!user)
        return NULL;

    return rpcsyncwerk_client_call__objlist (
        client, "get_org_groups_by_user", CCNET_TYPE_GROUP, NULL,
        2, "string", user, "int", org_id);
}

GList *
ccnet_get_group_members (RpcsyncwerkClient *client, int group_id)
{
    return rpcsyncwerk_client_call__objlist (
        client, "get_group_members", CCNET_TYPE_GROUP_USER, NULL,
        1, "int", group_id);
}

int
ccnet_org_user_exists (RpcsyncwerkClient *client, int org_id, const char *user)
{
    return rpcsyncwerk_client_call__int (client, "org_user_exists", NULL,
                                    2, "int", org_id, "string", user);
}

#if 0
int
ccnet_get_peer_async (RpcsyncwerkClient *client, const char *peer_id,
                      AsyncCallback callback, void *user_data)
{
    return get_peer_async (client, peer_id, callback, user_data);
}
#endif

int
ccnet_get_binding_email_async (RpcsyncwerkClient *client, const char *peer_id,
                               AsyncCallback callback, void *user_data)
{
    return rpcsyncwerk_client_async_call__string (
        client, "get_binding_email", callback, user_data,
        1, "string", peer_id);
}

char *
ccnet_sign_message (RpcsyncwerkClient *client, const char *message)
{
    if (!message)
        return NULL;

    return rpcsyncwerk_client_call__string (
        client, "sign_message", NULL,
        1, "string", message);
}

int
ccnet_verify_message (RpcsyncwerkClient *client,
                      const char *message,
                      const char *sig_base64,
                      const char *peer_id)
{
    if (!message || !sig_base64 || !peer_id)
        return -1;

    return rpcsyncwerk_client_call__int (
        client, "verify_message", NULL,
        3, "string", message, "string", sig_base64, "string", peer_id);
}

char *
ccnet_pubkey_encrypt (RpcsyncwerkClient *client,
                      const char *msg_base64,
                      const char *peer_id)
{
    if (!msg_base64 || !peer_id)
        return NULL;

    return rpcsyncwerk_client_call__string (client, "pubkey_encrypt", NULL, 2,
                                       "string", msg_base64, "string", peer_id);
}

char *
ccnet_privkey_decrypt (RpcsyncwerkClient *client, const char *msg_base64)
{
    if (!msg_base64)
        return NULL;

    return rpcsyncwerk_client_call__string (client, "privkey_decrypt", NULL, 1,
                                       "string", msg_base64);
}

char *
ccnet_get_config (RpcsyncwerkClient *client, const char *key)
{
    if (!key)
        return NULL;

    return rpcsyncwerk_client_call__string (
        client, "get_config", NULL, 
        1, "string", key);
}

int
ccnet_set_config (RpcsyncwerkClient *client, const char *key, const char *value)
{
    if (!key || !value)
        return -1;

    return rpcsyncwerk_client_call__int (
        client, "set_config", NULL,
        2, "string", key, "string", value);
}

void
ccnet_login_to_relay (RpcsyncwerkClient *client, const char *relay_id,
                      const char *username, const char *passwd)
{
    rpcsyncwerk_client_call__int (client, "login_relay", NULL,
                             3, "string", relay_id,
                             "string", username, "string", passwd);
}

gboolean
ccnet_peer_is_ready (RpcsyncwerkClient *client, const char *peer_id)
{
    CcnetPeer *peer;
    gboolean ret;
    peer = ccnet_get_peer (client, peer_id);
    if (!peer)
        return FALSE;
    ret = peer->is_ready;
    g_object_unref (peer);
    return ret;
}

int
ccnet_update_peer_address (RpcsyncwerkClient *client, const char *peer_id,
                           const char *addr, int port)
{
    return rpcsyncwerk_client_call__int (
        client, "update_peer_address", NULL,
        3, "string", peer_id, "string", addr, "int", port);
}
