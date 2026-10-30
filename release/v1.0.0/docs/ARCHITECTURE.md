# Architecture

## 1. Purpose

DualThread is a Windows C++ chat system that demonstrates a layered, multi-process messaging architecture:

- an MFC desktop client with a custom-drawn WhatsApp/Teams-style interface,
- a TCP load balancer in front of two independent chat servers,
- Redis for authentication caching, presence and cross-server Pub/Sub,
- Kafka as the durable message pipeline,
- a dedicated DBWriter process that persists messages to MySQL through native ODBC,
- UDP heartbeats for liveness and round-trip-time feedback.

The design deliberately keeps the network, cache, broker, database and UI responsibilities in separate processes. A failure in one part produces visible, recoverable behaviour rather than a silent stall.

## 2. Process topology

```text
                       +-------------------------+
                       |       ChatClient        |
                       |  MFC UI + ChatSession   |
                       +-----+-------------+-----+
                             |             |
                    TCP 5000 |             | UDP 5001
                             v             v
                       +-------------------------+
                       |      LoadBalancer       |
                       | round robin + health    |
                       +-----+-------------+-----+
                             |             |
                TCP 6001     |             |     TCP 6002
                UDP 7001     v             v     UDP 7002
                       +-----------+   +-----------+
                       | ChatServer|   | ChatServer|
                       |   #1      |   |   #2      |
                       +-----+-----+   +-----+-----+
                             |               |
              Redis Pub/Sub  |               |  Kafka producer
          chat.broadcast     |               |  chat-messages
          chat.presence      |               |
                             +-------+-------+
                                     |
                                     v
                              +-------------+
                              |   DBWriter  |
                              | Kafka -> SQL|
                              +------+------+
                                     |
                                     v
                              +-------------+
                              |    MySQL    |
                              | ChatDatabase|
                              +-------------+
```

### Ports

| Component | TCP | UDP | Notes |
|---|---:|---:|---|
| MySQL | 3306 | - | `ChatDatabase` |
| Redis / Memurai | 6379 | - | Cache, presence, Pub/Sub |
| Kafka | 9092 | - | Topic `chat-messages` |
| LoadBalancer | 5000 | 5001 | Client entry point |
| ChatServer 1 | 6001 | 7001 | Backend 1 |
| ChatServer 2 | 6002 | 7002 | Backend 2 |

### Startup order

1. MySQL and Redis/Memurai.
2. Kafka broker.
3. `DBWriter`.
4. ChatServer 1 and ChatServer 2.
5. LoadBalancer.
6. One or more `ChatClient` instances.

The servers and DBWriter tolerate a closed stdin so they can be launched as hidden background processes. A `q` line on an interactive stdin still performs a graceful shutdown.

## 3. Components

### 3.1 ChatClient

Responsibilities:

- Login dialog with asynchronous authentication (`LoginDlg.cpp`).
- TCP session, reconnect loop and UDP heartbeat (`ChatSession.cpp`).
- Conversation list, message bubbles, composer, group dialog and custom GDI+ drawing (`MainDlg.cpp`, `UiControls.cpp`, `GroupDlg.cpp`).
- All protocol/UI state is owned by the UI thread. The networking threads post results with `PostMessage` and heap-owned payloads; they never call MFC methods directly.

Important UI behaviour:

- The window is created at 1200 x 780 outer size (approximately 1200 x 750 client area), and the layout responds to `WM_SIZE`.
- Minimum tracking size is 980 x 640.
- Enter sends; Shift+Enter is handled by the composer subclass and inserts a newline.
- Unread counts are maintained per conversation and cleared when the conversation is selected.
- The composer text is preserved while the connection is lost; the send button is disabled.
- On reconnection the client clears its loaded-history cache and requests groups, presence and the selected history again.

### 3.2 LoadBalancer

- Listens for TCP connections on 5000 and UDP datagrams on 5001.
- Selects a healthy backend for each new TCP connection in round-robin order.
- Performs TCP health checks against `6001`/`6002` at a configurable interval and timeout.
- Proxies bytes in both directions and closes the upstream session when either side closes.
- Forwards UDP heartbeat traffic to the selected/available backend UDP ports.
- Logs routing decisions and closed sessions to `logs/loadbalancer.log`.

If the load balancer stops, clients lose their TCP connection and enter their reconnect loop. When it returns, new sessions are routed to a healthy backend; users may end up on a different server than before, which exercises the Redis cross-server paths.

### 3.3 ChatServer

Each server is an independent process with the same code and a different INI file.

- `TcpServer` owns the accept loop, a thread per authenticated client, the local `userId -> ClientInfo` map and the group routing cache.
- `RedisCache` validates users, caches SHA-256 hashes, tracks online users with TTL and subscribes to `chat.broadcast` and `chat.presence`.
- `MySQLDatabase` uses ODBC directly (`SQLDriverConnect`, prepared statements and `SQLBindParameter`) for users, groups and history.
- `KafkaProducer` queues accepted messages and publishes them asynchronously to `chat-messages`.
- `UdpHeartbeat` listens for client heartbeats, answers with `hb_ack`, updates presence TTL and tracks liveness.
- `ServerConfig` loads the INI file, including legacy key aliases.

### 3.4 DBWriter

- A librdkafka consumer in consumer group `dbwriter-group`.
- Consumes `chat-messages` with `auto.offset.reset=earliest` and `enable.auto.commit=false`.
- Parses each event and requires `messageId`, `userId`, `timestamp` and `message`.
- Inserts `ConversationType` and `TargetId` when the extended schema is present; otherwise it falls back to the legacy insert shape.
- Commits the Kafka offset only after the SQL insert succeeds.
- Reconnects to MySQL and retries the insert without committing the offset when the database is unavailable.

### 3.5 MySQL

`tbl_ChatDetails.MessageId` is unique. That makes redelivery safe at the database boundary and gives the pipeline an at-least-once character.

## 4. Main flows

### 4.1 Login

```text
Client              LoadBalancer              ChatServer                 Redis/MySQL
  |                       |                        |                         |
  | TCP connect :5000     |                        |                         |
  |---------------------->| route to backend       |                         |
  |                       |----------------------->| accept + read auth      |
  | auth {userId,password}|                        |------------------------>| GET chat:user:<id>
  |                       |                        |<------------------------| cache hit / miss
  |                       |                        | (miss: SELECT user)     |
  |                       |                        | SHA-256 compare         |
  |<----------------------|------------------------| auth_ok {serverId}      |
  |                       |                        | user_list, presence_list|
  |                       |                        | group_list              |
```

- If the cached hash is present, Redis is the fast path.
- If Redis misses, the server reads `tbl_UserDetails` and caches the hash for 300 seconds.
- If MySQL is unavailable but a cached hash exists, the cached credential can still be validated.
- The server registers the user in its local map, marks `chat:online:<userId>` in Redis, broadcasts presence locally and publishes presence on `chat.presence`.
- A second login for the same user replaces the previous client and closes the older socket with `Signed in from another location`.

### 4.2 Personal message

```text
Client A -> LB -> Server 1
  chat {to: "user2", message, clientMsgId}
Server 1:
  validate recipient
  create MessageId
  send to local user2 if connected
  publish on Redis chat.broadcast
  enqueue Kafka event {conversationType: personal, targetId: user2}
  send ack {clientMsgId, messageId}
Server 2:
  receives Redis event; if user2 is local, sends the same chat frame
DBWriter:
  consumes Kafka event and inserts a personal row
```

Client A echoes its own message immediately. The `ack` frame marks the pending message as sent; an error frame marks it as failed and restores the composer text.

### 4.3 Group message

```text
Client -> Server:
  group_chat {groupId, message, clientMsgId}
Server:
  verify column/group membership
  create MessageId
  broadcast to local members of that group
  publish on chat.broadcast with targetId = groupId
  enqueue Kafka event {conversationType: group, targetId: groupId, targetName}
Other server:
  sees Redis group_chat event and broadcasts to its local members of the group
DBWriter:
  inserts a group row
```

The group membership cache is loaded from MySQL at login and refreshed on `group_create`, `group_join`, `group_leave` and Redis `group_sync` events.

### 4.4 History

- The client sends `history` with either `with=<userId>` or `groupId=<id>` and `limit` (default 100).
- Group history requires membership; personal history is restricted to rows where the user is the sender or target.
- The SQL query selects the newest rows with `LIMIT`, then the server reverses them into chronological order and sends `history_item` frames followed by `history_end`; the UI appends them.
- The UI caches loaded histories per conversation and refreshes after reconnection.

### 4.5 Presence and typing

- Presence is the union of Redis `chat:online:*` keys and the server's local client map.
- Online/offline changes are broadcast locally and published on `chat.presence`; the other server forwards them to its local clients.
- `typing` frames are transient. They are not persisted and not queued through Kafka. The client sends a typing event at most every two seconds and expires the remote indicator after four seconds; it sends `isTyping=false` after three seconds of inactivity or after sending.
- Presence state can be briefly stale immediately after a reconnect. The client requests `presence_list` on `SESSION_CONNECTED`, so the list converges without a restart.

### 4.6 Reconnection

`ChatSession::NetworkLoop` runs on a worker thread:

```text
no socket -> ConnectInternal
  success -> SESSION_CONNECTED, reset backoff
  failure -> SESSION_DISCONNECTED ("<error> Reconnecting...")
             sleep 1, 2, 4, 8, then 10 seconds
readable socket -> RecvFrame -> post frame to UI

socket error/EOF -> DropSocket
                 -> post "Connection lost. Reconnecting..."
```

The UI receives status messages on `WM_APP_SESSION_STATUS`:

- `SESSION_CONNECTED`: green “Connected | N ms”, clears error state, refreshes groups/presence/history.
- `SESSION_RECONNECTING` / `SESSION_DISCONNECTED`: red “Reconnecting...” pill, profile text, disabled send button, transient error message.
- `SESSION_AUTH_FAILED`: fatal authentication failure, no reconnect loop.

A rebuilt socket uses the same credentials and sends a fresh `auth` frame. A message that was not accepted before the drop remains in the composer and can be retried.

## 5. Threading and UI safety

| Thread | Owns | Publishes to UI via |
|---|---|---|
| MFC UI thread | controls, conversation model, selection, composer | direct calls only on UI thread |
| TCP network thread | socket, framing, reconnect | `WM_APP_SESSION_FRAME`, `WM_APP_SESSION_STATUS` |
| UDP heartbeat thread | heartbeat socket, RTT | `WM_APP_SESSION_HEARTBEAT` |
| Server accept thread | listening socket, client threads | n/a |
| Server client thread | one authenticated connection | socket sends |
| Redis subscriber thread | Pub/Sub context and reconnect | `OnCrossServerMessage` |
| Kafka producer thread | librdkafka producer | n/a |
| Kafka consumer thread | consumer and offset commit | n/a |
| UDP monitor thread | heartbeat tracking and timeouts | n/a |

Each posted message allocates its payload; the UI handler takes ownership and deletes it. `ChatSession::Stop` sets the running flag, shuts down the socket to unblock `recv`/`select`, joins both threads and then closes the socket. Server client threads set `alive=false`, call `shutdown` and are joined by `TcpServer::Stop`.

## 6. Storage model

### Redis keys and channels

| Key/channel | Value | Lifetime |
|---|---|---|
| `chat:user:<userId>` | `{"hash":"<sha256>","ip":"..."}` | 300s TTL, refreshed on login |
| `chat:online:<userId>` | server ID | 15s TTL, refreshed by heartbeat |
| `chat.broadcast` | JSON message/group/typing/group-sync events | transient Pub/Sub |
| `chat.presence` | JSON online/offline events | transient Pub/Sub |

### Kafka

| Setting | Value |
|---|---|
| Topic | `chat-messages` |
| Consumer group | `dbwriter-group` |
| Producer | one per chat server |
| Consumer offset | committed manually after a successful insert |
| Invalid event | logged and committed (skipped) |

### MySQL tables

`tbl_UserDetails`

| Column | Type | Notes |
|---|---|---|
| `UserId` | `VARCHAR(100)` | primary key |
| `PasswordHash` | `VARCHAR(255)` | SHA-256 hex in this demo |
| `IPAddress` | `VARCHAR(100)` | last known address |
| `Timestamp` | `DATETIME` | default current time |

`tbl_ChatDetails`

| Column | Type | Notes |
|---|---|---|
| `UniqueId` | `INT AUTO_INCREMENT` | primary key |
| `UserId` | `VARCHAR(100)` | sender |
| `MessageId` | `VARCHAR(255)` | unique |
| `Timestamp` | `DATETIME` | UTC message time |
| `ChatMessage` | `TEXT` | message body |
| `ConversationType` | `VARCHAR(16)` | `personal` or `group` |
| `TargetId` | `VARCHAR(100)` | recipient or group ID |

`tbl_Groups`

| Column | Type | Notes |
|---|---|---|
| `GroupId` | `VARCHAR(64)` | primary key, generated by the server |
| `GroupName` | `VARCHAR(128)` | display name |
| `OwnerId` | `VARCHAR(100)` | creator |
| `CreatedAt` | `DATETIME` | default current time |

`tbl_GroupMembers`

| Column | Type | Notes |
|---|---|---|
| `GroupId` | `VARCHAR(64)` | part of composite primary key |
| `UserId` | `VARCHAR(100)` | part of composite primary key |
| `JoinedAt` | `DATETIME` | default current time |

## 7. Protocol

Every TCP frame is:

```text
+----------------------+----------------------------+
| 4-byte unsigned int  | UTF-8 JSON object          |
| big-endian length N  | N bytes                     |
+----------------------+----------------------------+
```

- `MAX_FRAME_SIZE` = 16 MiB.
- Chat text is limited to 8000 characters by the server.
- Message/ack correlation uses `clientMsgId`; durable correlation uses `messageId`.
- TCP sockets use a 5-second send timeout. Authentication has a 10-second receive timeout.
- After authentication the receive timeout is cleared and liveness is maintained by the client heartbeat, logout and connection close.

Important frame types are listed in [README.md](../README.md#protocol-summary).

## 8. Failure behaviour

| Failure | Behaviour |
|---|---|
| LoadBalancer down | Client socket closes; UI shows Reconnecting, send disabled, auto-retry 1–10s |
| ChatServer down | LoadBalancer health checks remove it; existing clients reconnect and may be routed to the other server |
| Redis down | Servers log a warning and fall back to MySQL for auth; presence may be stale until Redis returns |
| MySQL down | Auth cache may still allow cached users; history/group operations return an error; DBWriter retries writes |
| Kafka down | Chat servers log producer unavailability and drop events after retries; live delivery still works |
| DBWriter down | Kafka retains events; restarting DBWriter with the same group continues from the committed offset |
| Duplicate login | Previous session receives an error and is closed; the new session remains active |
| Invalid credentials | `auth` failure with a friendly message; client stops reconnecting and returns to the form |

## 9. Limits and deliberate simplifications

- History is limited to the most recent 100 messages per conversation.
- Presence uses TTL keys and a local map; there is no durable presence history.
- Typing state is best-effort and intentionally not persisted.
- Group membership changes are synchronised through Redis Pub/Sub; a server that was offline catches up from MySQL on the next `group_list`.
- The demo has no message editing, deletion, attachments, read receipts beyond unread counts, or end-to-end encryption.
- Password hashing is unsalted SHA-256 for the assessment; see the security notes in the README.
- The load balancer is a simple round-robin TCP proxy, not a general-purpose reverse proxy.

## 10. Verification references

The exact build command, observed test results, SQL evidence and screenshot list are in:

- [README.md](../README.md)
- [TRACEABILITY.md](TRACEABILITY.md)
- [TEST_RESULTS.md](TEST_RESULTS.md)
