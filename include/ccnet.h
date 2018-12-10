/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef _CCNET_H
#define _CCNET_H

#include <ccnet/option.h>

#include <glib.h>

#include <ccnet/valid-check.h>
#include <ccnet/peer.h>
#include <ccnet/message.h>
#include <ccnet/status-code.h>
#include <ccnet/processor.h>

#include <ccnet/ccnet-session-base.h>
#include <ccnet/ccnet-client.h>

#include <ccnet/proc-factory.h>
#include <ccnet/sendcmd-proc.h>
#include <ccnet/mqclient-proc.h>
#include <ccnet/invoke-service-proc.h>

#include <ccnet/timer.h>

#include <rpcsyncwerk-client.h>

/* mainloop */

void ccnet_main (CcnetClient *client);

typedef void (*RegisterServiceCB) (gboolean success);
void ccnet_register_service (CcnetClient *client,
                             const char *service, const char *group,
                             GType proc_type, RegisterServiceCB cb);
gboolean ccnet_register_service_sync (CcnetClient *client,
                                      const char *service,
                                      const char *group);
CcnetClient *ccnet_init (const char *central_config_dir, const char *confdir);

void ccnet_send_command (CcnetClient *client, const char *command,
                         SendcmdProcRcvrspCallback cmd_cb, void *cbdata);

void ccnet_add_peer (CcnetClient *client, const char *id, const char *addr);

void ccnet_connect_peer (CcnetClient *client, const char *id);
void ccnet_disconnect_peer (CcnetClient *client, const char *id);

/* client pool */

struct CcnetClientPool;
typedef struct CcnetClientPool CcnetClientPool;

struct CcnetClientPool *
ccnet_client_pool_new (const char *central_config_dir, const char *conf_dir);

CcnetClient *
ccnet_client_pool_get_client (struct CcnetClientPool *cpool);

void
ccnet_client_pool_return_client (struct CcnetClientPool *cpool,
                                 CcnetClient *client);

/* rpc wrapper */

/* Create rpc client using a single client for transport. */
RpcsyncwerkClient *
ccnet_create_rpc_client (CcnetClient *cclient, const char *peer_id,
                         const char *service_name);

/* Create rpc client using client pool for transport. */
RpcsyncwerkClient *
ccnet_create_pooled_rpc_client (struct CcnetClientPool *cpool,
                                const char *peer_id,
                                const char *service);

RpcsyncwerkClient *
ccnet_create_async_rpc_client (CcnetClient *cclient, const char *peer_id,
                               const char *service_name);

void ccnet_rpc_client_free (RpcsyncwerkClient *client);
void ccnet_async_rpc_client_free (RpcsyncwerkClient *client);

CcnetPeer *ccnet_get_peer (RpcsyncwerkClient *client, const char *peer_id);
CcnetPeer *ccnet_get_peer_by_idname (RpcsyncwerkClient *client, const char *idname);
int ccnet_get_peer_net_state (RpcsyncwerkClient *client, const char *peer_id);
int ccnet_get_peer_bind_status (RpcsyncwerkClient *client, const char *peer_id);
int ccnet_peer_is_ready (RpcsyncwerkClient *client, const char *peer_id);
CcnetPeer *ccnet_get_default_relay (RpcsyncwerkClient *client);
GList *ccnet_get_peers_by_role (RpcsyncwerkClient *client, const char *role);

char *ccnet_get_binding_email (RpcsyncwerkClient *client, const char *peer_id);
GList *ccnet_get_groups_by_user (RpcsyncwerkClient *client, const char *user, int return_ancestors);
GList *ccnet_get_org_groups_by_user (RpcsyncwerkClient *client, const char *user, int org_id);
GList *
ccnet_get_group_members (RpcsyncwerkClient *client, int group_id);
int
ccnet_org_user_exists (RpcsyncwerkClient *client, int org_id, const char *user);

int
ccnet_get_binding_email_async (RpcsyncwerkClient *client, const char *peer_id,
                               AsyncCallback callback, void *user_data);

char *ccnet_sign_message (RpcsyncwerkClient *client, const char *message);
int ccnet_verify_message (RpcsyncwerkClient *client,
                          const char *message,
                          const char *sig_base64,
                          const char *peer_id);

char *
ccnet_pubkey_encrypt (RpcsyncwerkClient *client,
                      const char *msg_base64,
                      const char *peer_id);

char *
ccnet_privkey_decrypt (RpcsyncwerkClient *client, const char *msg_base64);

char *ccnet_get_config (RpcsyncwerkClient *client, const char *key);
int ccnet_set_config (RpcsyncwerkClient *client, const char *key, const char *value);

void
ccnet_login_to_relay (RpcsyncwerkClient *client, const char *relay_id,
                      const char *username, const char *passwd);

int
ccnet_update_peer_address (RpcsyncwerkClient *client, const char *peer_id,
                           const char *addr, int port);

#endif
