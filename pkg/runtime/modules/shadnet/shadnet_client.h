// SPDX-License-Identifier: GPL-2.0-or-later

// shadNet client, following shadPS4 src/shadnet/client.{h,cpp}: one TCP session to a shadNet
// server for one console user.
//
// Packets carry a 15-byte little-endian header (type u8, command u16, total size u32, packet id
// u64) and a payload. The server opens with a ServerInfo packet (protocol version u32). The
// client then logs in (LoginRequest), and after the login reply asks for the WebAPI bearer
// token (GetToken), the server's features (GetServerFeatures) and reports its version
// (SetClientVersion); authentication completes with the features reply. Requests go out
// through a writer thread; a reader thread dispatches replies and notifications to the
// callbacks, on the reader thread.

#ifndef SHADNET_CLIENT_H
#define SHADNET_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "shadnet_pb.h"

#ifdef __cplusplus
extern "C" {
#endif

// Protocol constants (client.h).
#define SHADNET_HEADER_SIZE 15
#define SHADNET_CONNECT_TIMEOUT_MS 10000
#define SHADNET_CONNECT_MAX_ATTEMPTS 4
#define SHADNET_CONNECT_RETRY_BACKOFF_MS 1000
#define SHADNET_PROTOCOL_VERSION 1
#define SHADNET_MAX_PACKET_SIZE 0x800000
#define SHADNET_DEFAULT_PORT 31313

typedef enum ShadNetPacketType {
    SHADNET_PACKET_REQUEST = 0,
    SHADNET_PACKET_REPLY = 1,
    SHADNET_PACKET_NOTIFICATION = 2,
    SHADNET_PACKET_SERVER_INFO = 3,
} ShadNetPacketType;

// CommandType (client.h; must match the server's protocol.h).
typedef enum ShadNetCommand {
    SHADNET_CMD_LOGIN = 0,
    SHADNET_CMD_TERMINATE = 1,
    SHADNET_CMD_CREATE = 2,
    SHADNET_CMD_DELETE = 3,
    SHADNET_CMD_SEND_TOKEN = 4,
    SHADNET_CMD_SEND_RESET_TOKEN = 5,
    SHADNET_CMD_RESET_PASSWORD = 6,
    SHADNET_CMD_RESET_STATE = 7,
    SHADNET_CMD_ADD_FRIEND = 8,
    SHADNET_CMD_REMOVE_FRIEND = 9,
    SHADNET_CMD_ADD_BLOCK = 10,
    SHADNET_CMD_REMOVE_BLOCK = 11,
    SHADNET_CMD_GET_SERVER_FEATURES = 12,
    SHADNET_CMD_SET_CLIENT_VERSION = 13,
    SHADNET_CMD_GET_BOARD_INFOS = 30,
    SHADNET_CMD_RECORD_SCORE = 31,
    SHADNET_CMD_RECORD_SCORE_DATA = 32,
    SHADNET_CMD_GET_SCORE_DATA = 33,
    SHADNET_CMD_GET_SCORE_RANGE = 34,
    SHADNET_CMD_GET_SCORE_FRIENDS = 35,
    SHADNET_CMD_GET_SCORE_NPID = 36,
    SHADNET_CMD_GET_SCORE_ACCOUNT_ID = 37,
    SHADNET_CMD_GET_SCORE_GAME_DATA_BY_ACC_ID = 38,
    SHADNET_CMD_GET_TOKEN = 39,
    SHADNET_CMD_SET_APPEAR_OFFLINE = 40,
    SHADNET_CMD_LOOKUP_ONLINE_ID = 41,
    SHADNET_CMD_GET_AUTHORIZATION_CODE = 42,
    SHADNET_CMD_CONTEXT_START = 100,
    SHADNET_CMD_CREATE_ROOM = 101,
    SHADNET_CMD_JOIN_ROOM = 102,
    SHADNET_CMD_LEAVE_ROOM = 103,
    SHADNET_CMD_SEARCH_ROOM = 104,
    SHADNET_CMD_REQUEST_SIGNALING_INFOS = 105,
    SHADNET_CMD_CONTEXT_STOP = 106,
    SHADNET_CMD_SET_USER_INFO = 107,
    SHADNET_CMD_SET_ROOM_DATA_INTERNAL = 108,
    SHADNET_CMD_SET_ROOM_DATA_EXTERNAL = 109,
    SHADNET_CMD_KICKOUT_ROOM_MEMBER = 110,
    SHADNET_CMD_GET_WORLD_INFO_LIST = 111,
    SHADNET_CMD_GET_ROOM_DATA_EXTERNAL_LIST = 112,
    SHADNET_CMD_GET_USER_INFO_LIST = 113,
    SHADNET_CMD_GET_ROOM_MEMBER_DATA_EXTERNAL_LIST = 114,
    SHADNET_CMD_SEND_ROOM_MESSAGE = 115,
    SHADNET_CMD_SET_ROOM_MEMBER_DATA_INTERNAL = 116,
    SHADNET_CMD_TUS_SET_DATA = 201,
    SHADNET_CMD_TUS_GET_DATA = 202,
    SHADNET_CMD_TUS_SET_MULTI_SLOT_VARIABLE = 203,
    SHADNET_CMD_TUS_GET_MULTI_SLOT_VARIABLE = 204,
    SHADNET_CMD_TUS_ADD_AND_GET_VARIABLE = 205,
    SHADNET_CMD_TUS_GET_MULTI_SLOT_DATA_STATUS = 206,
    SHADNET_CMD_TUS_GET_MULTI_USER_DATA_STATUS = 207,
    SHADNET_CMD_TUS_GET_FRIENDS_DATA_STATUS = 208,
    SHADNET_CMD_TUS_DELETE_MULTI_SLOT_DATA = 209,
    SHADNET_CMD_TUS_GET_MULTI_USER_VARIABLE = 210,
    SHADNET_CMD_TUS_TRY_AND_SET_VARIABLE = 211,
    SHADNET_CMD_TUS_GET_FRIENDS_VARIABLE = 212,
    SHADNET_CMD_TUS_DELETE_MULTI_SLOT_VARIABLE = 213,
    SHADNET_CMD_TSS_GET_DATA = 214,
    SHADNET_CMD_UNLOCK_TROPHY = 301,
    SHADNET_CMD_SYNC_TROPHIES = 302,
} ShadNetCommand;

typedef enum ShadNetNotification {
    SHADNET_NOTIFY_FRIEND_QUERY = 5,
    SHADNET_NOTIFY_FRIEND_NEW = 6,
    SHADNET_NOTIFY_FRIEND_LOST = 7,
    SHADNET_NOTIFY_FRIEND_STATUS = 8,
    SHADNET_NOTIFY_ROOM_EVENT = 10,
    SHADNET_NOTIFY_ROOM_MESSAGE = 11,
    SHADNET_NOTIFY_WEBAPI_PUSH_EVENT = 17,
    SHADNET_NOTIFY_SERVICE_UNAVAILABLE = 18,
    SHADNET_NOTIFY_CLIENT_REJECTED = 19,
} ShadNetNotification;

// ErrorType: the byte every reply body starts with.
typedef enum ShadNetError {
    SHADNET_ERR_NO_ERROR = 0,
    SHADNET_ERR_MALFORMED = 1,
    SHADNET_ERR_INVALID = 2,
    SHADNET_ERR_INVALID_INPUT = 3,
    SHADNET_ERR_TOO_SOON = 4,
    SHADNET_ERR_LOGIN_ERROR = 5,
    SHADNET_ERR_LOGIN_ALREADY_LOGGED_IN = 6,
    SHADNET_ERR_LOGIN_INVALID_USERNAME = 7,
    SHADNET_ERR_LOGIN_INVALID_PASSWORD = 8,
    SHADNET_ERR_LOGIN_INVALID_TOKEN = 9,
    SHADNET_ERR_CREATION_ERROR = 10,
    SHADNET_ERR_CREATION_EXISTING_USERNAME = 11,
    SHADNET_ERR_CREATION_BANNED_EMAIL_PROVIDER = 12,
    SHADNET_ERR_CREATION_EXISTING_EMAIL = 13,
    SHADNET_ERR_ROOM_MISSING = 14,
    SHADNET_ERR_ROOM_ALREADY_JOINED = 15,
    SHADNET_ERR_ROOM_FULL = 16,
    SHADNET_ERR_ROOM_PASSWORD_MISMATCH = 17,
    SHADNET_ERR_ROOM_PASSWORD_MISSING = 18,
    SHADNET_ERR_ROOM_GROUP_NO_JOIN_LABEL = 19,
    SHADNET_ERR_ROOM_GROUP_FULL = 20,
    SHADNET_ERR_ROOM_GROUP_JOIN_LABEL_NOT_FOUND = 21,
    SHADNET_ERR_ROOM_GROUP_MAX_SLOT_MISMATCH = 22,
    SHADNET_ERR_UNAUTHORIZED = 23,
    SHADNET_ERR_DB_FAIL = 24,
    SHADNET_ERR_EMAIL_FAIL = 25,
    SHADNET_ERR_NOT_FOUND = 26,
    SHADNET_ERR_BLOCKED = 27,
    SHADNET_ERR_ALREADY_FRIEND = 28,
    SHADNET_ERR_SCORE_NOT_BEST = 29,
    SHADNET_ERR_SCORE_INVALID = 30,
    SHADNET_ERR_SCORE_HAS_DATA = 31,
    SHADNET_ERR_COND_FAIL = 32,
    SHADNET_ERR_UNSUPPORTED = 33,
    SHADNET_ERR_TOO_LARGE = 34,
} ShadNetError;

// ShadNetState.
typedef enum ShadNetState {
    SHADNET_STATE_OK = 0,
    SHADNET_STATE_FAILURE_INPUT,
    SHADNET_STATE_FAILURE_RESOLVE,
    SHADNET_STATE_FAILURE_CONNECT,
    SHADNET_STATE_FAILURE_SERVER_INFO,
    SHADNET_STATE_FAILURE_AUTH,
    SHADNET_STATE_FAILURE_ALREADY_IN,
    SHADNET_STATE_FAILURE_USERNAME,
    SHADNET_STATE_FAILURE_PASSWORD,
    SHADNET_STATE_FAILURE_TOKEN,
    SHADNET_STATE_FAILURE_PROTOCOL,
    SHADNET_STATE_FAILURE_OTHER,
} ShadNetState;

// NotifyServiceUnavailable / NotifyClientRejected constants.
#define SHADNET_SERVICE_MATCHING2 1
#define SHADNET_UNAVAILABLE_SERVER_FULL 1
#define SHADNET_UNAVAILABLE_NOT_ALLOWED 2
#define SHADNET_UNAVAILABLE_DISABLED 3
#define SHADNET_REJECTED_VERSION 1

// Notification 17: length-prefixed fields, not a protobuf message.
typedef struct ShadNetKeyValue {
    ShadPbBytes key;
    ShadPbBytes value;
} ShadNetKeyValue;

typedef struct ShadNetWebApiPushEvent {
    ShadPbBytes np_service_name; // may be empty (catch-all listeners match any)
    uint32_t np_service_label;
    ShadPbBytes data_type;
    ShadPbBytes data;
    ShadPbBytes from_npid;
    ShadPbBytes to_npid;
    const ShadNetKeyValue *extd_data;
    uint32_t extd_data_count;
    uint64_t from_account_id; // 0 = unknown / not sent
    uint64_t to_account_id;
} ShadNetWebApiPushEvent;

// Callbacks, called on the reader thread. Decoded messages are valid during the call only.
// Any callback may be NULL.
typedef struct ShadNetCallbacks {
    void *user;
    // error is the login reply's ErrorType; reply is NULL unless it is NO_ERROR and parsed.
    void (*login_result)(void *user, uint8_t error, const ShadPb_LoginReply *reply);
    void (*friend_query)(void *user, const ShadPb_NotifyFriendQuery *n);
    void (*friend_new)(void *user, const ShadPb_NotifyFriendNew *n);
    void (*friend_lost)(void *user, const ShadPb_NotifyFriendLost *n);
    void (*friend_status)(void *user, const ShadPb_NotifyFriendStatus *n);
    void (*room_event)(void *user, const ShadPb_NotifyRoomEvent *n);
    void (*room_message)(void *user, const ShadPb_NotifyRoomMessage *n);
    void (*webapi_push_event)(void *user, const ShadNetWebApiPushEvent *n);
    void (*service_unavailable)(void *user, const ShadPb_NotifyServiceUnavailable *n);
    void (*client_rejected)(void *user, const ShadPb_NotifyClientRejected *n);
    // The reply to any other request: its command, packet id, ErrorType byte and the body after
    // that byte (may be empty).
    void (*async_reply)(void *user, uint16_t cmd, uint64_t pkt_id, uint8_t error, const uint8_t *body,
                        size_t body_len);
} ShadNetCallbacks;

typedef struct ShadNetClient ShadNetClient;

// The login: credentials and the running title (LoginRequest title_id / title_name).
typedef struct ShadNetLogin {
    const char *host;
    uint16_t port;
    const char *npid;
    const char *password;
    const char *token;      // may be NULL or empty
    const char *title_id;   // param.sfo TITLE_ID, may be empty
    const char *title_name; // param.sfo TITLE, may be empty
    const char *client_version; // sent with SetClientVersion
} ShadNetLogin;

// shadPS4 ServerProbe (server_probe.cpp): one connection that only reads the ServerInfo
// handshake, so a dead or incompatible server is noticed before any login.
typedef enum ShadNetProbeResult {
    SHADNET_PROBE_OK,               // reachable, protocol version matches
    SHADNET_PROBE_UNREACHABLE,      // DNS failure, connect failure or timeout
    SHADNET_PROBE_VERSION_MISMATCH, // ServerInfo carries another protocol version
    SHADNET_PROBE_PROTOCOL_ERROR,   // no well-formed ServerInfo packet
} ShadNetProbeResult;
#define SHADNET_PROBE_TIMEOUT_MS 3000
ShadNetProbeResult shadnet_probe(const char *host, uint16_t port, uint32_t timeout_ms, uint32_t *server_version);

ShadNetClient *shadnet_client_create(const ShadNetCallbacks *callbacks);
// Stops the client and frees it.
void shadnet_client_destroy(ShadNetClient *c);

// Starts connecting (and logging in) on a thread of its own; the strings are copied.
void shadnet_client_start(ShadNetClient *c, const ShadNetLogin *login);
// Ends the session and joins the client's threads.
void shadnet_client_stop(ShadNetClient *c);

// Block until the connection is up or failed / the login completed or failed.
ShadNetState shadnet_client_wait_for_connection(ShadNetClient *c);
ShadNetState shadnet_client_wait_for_authenticated(ShadNetClient *c);

bool shadnet_client_is_connected(const ShadNetClient *c);
bool shadnet_client_is_authenticated(const ShadNetClient *c);
ShadNetState shadnet_client_state(const ShadNetClient *c);
uint64_t shadnet_client_user_id(const ShadNetClient *c);
const char *shadnet_client_npid(const ShadNetClient *c);
uint32_t shadnet_client_server_protocol_version(const ShadNetClient *c);
uint32_t shadnet_client_addr_local(const ShadNetClient *c);  // network byte order
uint32_t shadnet_client_addr_server(const ShadNetClient *c); // network byte order
bool shadnet_client_matching2_enabled(const ShadNetClient *c);
bool shadnet_client_trophies_enabled(const ShadNetClient *c);
// True on a client's reader thread: code there must not wait for a reply.
bool shadnet_client_on_reader_thread(void);
// Copies the avatar URL / bearer token into out (NUL-terminated, truncated to out_size);
// returns the full length.
size_t shadnet_client_avatar_url(ShadNetClient *c, char *out, size_t out_size);
size_t shadnet_client_bearer_token(ShadNetClient *c, char *out, size_t out_size);
uint32_t shadnet_client_num_friends(ShadNetClient *c);
// Copies friend index's NP id into out; false past the end.
bool shadnet_client_friend_npid(ShadNetClient *c, uint32_t index, char *out, size_t out_size);

// Queues a request; returns its packet id (0 when out of memory).
uint64_t shadnet_client_submit(ShadNetClient *c, uint16_t cmd, const uint8_t *payload, size_t len);
// Queues a request whose payload is msg's encoding with a u32 little-endian size prefix,
// preceded by prefix (prefix_len bytes, e.g. a communication id; may be 0).
uint64_t shadnet_client_submit_proto(ShadNetClient *c, uint16_t cmd, const void *prefix, size_t prefix_len,
                                     const ShadPbMessageDesc *desc, const void *msg);

uint64_t shadnet_client_add_friend(ShadNetClient *c, const char *npid);
uint64_t shadnet_client_remove_friend(ShadNetClient *c, const char *npid);
uint64_t shadnet_client_add_block(ShadNetClient *c, const char *npid);
uint64_t shadnet_client_remove_block(ShadNetClient *c, const char *npid);
// Records the Appear-Offline preference (sent with the login) and, once authenticated, sends
// it; returns the packet id, or 0 when it was only recorded.
uint64_t shadnet_client_set_appear_offline(ShadNetClient *c, bool enable);
uint64_t shadnet_client_lookup_online_id(ShadNetClient *c, const char *npid);
uint64_t shadnet_client_get_authorization_code(ShadNetClient *c, const char *client_id, const char *scope);
// com_id: the 12-character NP communication id.
uint64_t shadnet_client_unlock_trophy(ShadNetClient *c, const char *com_id, int32_t trophy_id, uint64_t timestamp);
uint64_t shadnet_client_sync_trophies(ShadNetClient *c, const char *com_id, const int32_t *ids,
                                      const uint64_t *timestamps, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
