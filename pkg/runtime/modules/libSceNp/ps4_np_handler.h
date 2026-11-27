// SPDX-License-Identifier: GPL-2.0-or-later

// NP handler, following shadPS4 src/core/libraries/np/np_handler (np_handler.cpp,
// np_handler_friends.cpp): the shadNet session of each logged-in user, which the NP libraries
// ask whether a user is signed in, for the user's NP id, account id and bearer token, and
// through which they send server requests.
//
// Configuration (shadPS4's shadNet settings): the JSON object in <data>/shadnet.json, or in
// the file PS4_SHADNET_CONFIG names, where <data> is PS4RECOMP_DATA_DIR or
// ~/Library/Application Support/PS4Recomp:
//
//   {
//     "enabled": true,                 // shadPS4 shadnet_enabled (default false: offline)
//     "server": "host:port",           // shadnet_server; port 31313 when omitted
//     "npid": "MyOnlineId",            // the account (shadPS4 per-user shadnet_npid)
//     "password": "...",               // shadnet_password
//     "token": "...",                  // shadnet_token, optional
//     "appear_offline": false,         // shadPS4's Appear-Offline preference
//     "np_country": "us",              // the profile NpManager reports (shadPS4 user
//     "np_language": "en",             // settings np_country, np_language, np_age and
//     "np_age": 13,                    // np_date_of_birth "YYYY-MM-DD"); these defaults
//     "np_date_of_birth": "2000-01-01" // apply when a value is missing or invalid
//   }
//
// The runtime logs in one user (libSceUserService), so the account is that user's. Without an
// enabled configuration with an account, every user stays signed out, as in shadPS4.

#ifndef PS4_NP_HANDLER_H
#define PS4_NP_HANDLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ps4_np.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_NP_COMMUNITY_ERROR_BAD_RESPONSE ((int32_t)0x80550708)
#define ORBIS_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_USER_NPID ((int32_t)0x8055081B)

typedef struct Ps4ShadNetConfig {
    bool enabled;
    char server[256];
    char npid[64];
    char password[256];
    char token[256];
    bool appear_offline;
    char np_country[8];
    char np_language[8];
    int32_t np_age;
    char np_date_of_birth[16];
} Ps4ShadNetConfig;

// Reads the configuration file (see above); a missing file leaves shadNet disabled.
void ps4_np_handler_load_config(Ps4ShadNetConfig *out);

// shadPS4 NpHandler::Initialize: reads the configuration (or takes config, for tests), probes
// the server and connects every logged-in user that has an account. Runs once; later calls
// return at once. Blocks until the connections are established or have failed.
void ps4_np_handler_initialize(const Ps4ShadNetConfig *config);
// shadPS4 NpHandler::Shutdown: signs every user out and stops the worker.
void ps4_np_handler_shutdown(void);
// libSceUserService login / logout of a user.
void ps4_np_handler_on_user_logged_in(int32_t user_id);
void ps4_np_handler_on_user_logged_out(int32_t user_id);

// shadPS4 EmulatorSettings.IsShadNetEnabled: configured on and not disabled for this session
// (an unreachable or incompatible server, or a server that rejected this client).
bool ps4_np_handler_enabled(void);
// The configuration Initialize read (all zero before it ran).
const Ps4ShadNetConfig *ps4_np_handler_config(void);
void ps4_np_handler_set_appear_offline(bool enable);
bool ps4_np_handler_appear_offline(void);

bool ps4_np_handler_is_signed_in(int32_t user_id);
bool ps4_np_handler_any_signed_in(void);
// The user's NP id (all zero when signed out).
OrbisNpId ps4_np_handler_np_id(int32_t user_id);
OrbisNpAccountId ps4_np_handler_account_id(int32_t user_id);
uint32_t ps4_np_handler_local_ip(int32_t user_id); // network byte order, 0 when signed out
// Copies into out (NUL-terminated); returns the full length (0 when signed out).
size_t ps4_np_handler_avatar_url(int32_t user_id, char *out, size_t out_size);
size_t ps4_np_handler_bearer_token(int32_t user_id, char *out, size_t out_size);
// The signed-in user with this account id / online id, or -1.
int32_t ps4_np_handler_user_by_account_id(uint64_t account_id);
int32_t ps4_np_handler_user_by_online_id(const OrbisNpOnlineId *online_id);
uint32_t ps4_np_handler_num_friends(int32_t user_id);
bool ps4_np_handler_friend_npid(int32_t user_id, uint32_t index, char *out, size_t out_size);

// The user's friend list as the server last reported it (login reply and friend
// notifications). Each name has at most PS4_NP_HANDLER_NPID_CAP - 1 characters.
#define PS4_NP_HANDLER_NPID_CAP 64
typedef struct Ps4NpFriend {
    char npid[PS4_NP_HANDLER_NPID_CAP];
    bool online;
} Ps4NpFriend;
// Copies up to cap friends into out; returns the number of friends. received / sent /
// blocked: the pending requests and blocked users, counted the same way (may be NULL).
uint32_t ps4_np_handler_friend_list(int32_t user_id, Ps4NpFriend *out, uint32_t cap, uint32_t *received,
                                    uint32_t *sent, uint32_t *blocked);

// NP state callbacks (shadPS4 RegisterStateCallback): called with ORBIS_NP_STATE_SIGNED_IN /
// SIGNED_OUT whenever a user's session starts or ends, on the thread that changed it.
typedef void (*Ps4NpStateCallback)(void *user, int32_t user_id, int32_t state);
int32_t ps4_np_handler_register_state_callback(Ps4NpStateCallback cb, void *user);
void ps4_np_handler_unregister_state_callback(int32_t handle);

// shadPS4 NpHandler::ResolveOnlineId: asks the server for an online id's account id and
// canonical spelling. on_result runs once, with ORBIS_OK or an NP error, on the client's
// reader thread or (for an immediate failure) on the caller's.
typedef void (*Ps4NpLookupResult)(void *user, int32_t result, uint64_t account_id, const char *canonical_online_id);
void ps4_np_handler_resolve_online_id(int32_t user_id, const char *online_id, Ps4NpLookupResult on_result,
                                      void *user);

#ifdef __cplusplus
}
#endif

#endif
