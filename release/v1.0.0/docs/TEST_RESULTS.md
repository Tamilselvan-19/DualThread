# Test Results

**Date:** 2026-10-22  
**Build:** `Release | x64`  
**Environment:** Windows, Visual Studio 18.x (v145 toolset), MySQL 8.0, Memurai (Redis-compatible), Apache Kafka, local TCP/UDP loopback.

This document records what was actually exercised. It does not treat code inspection as a runtime test unless explicitly stated.

## 1. Build

Command:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "C:\assessment\Cpp-Chat-Messaging-System-Advanced-main\CppChatMessaging.sln" `
  /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
```

Result:

- `MSBUILD_EXIT=0` — **PASS**.
- All four projects (`ChatClient`, `ChatServer`, `LoadBalancer`, `DBWriter`) rebuilt successfully.
- Compiler/linker output contained warnings only; no build errors.
- Output binaries were present in `bin\Release`:
  - `ChatClient.exe`
  - `ChatServer.exe`
  - `DBWriter.exe`
  - `LoadBalancer.exe`
  - `hiredis.dll`, `rdkafka.dll`, `lz4.dll`

## 2. Service and dependency startup

| Check | Expected | Observed | Result |
|---|---|---|---|
| MySQL listening | 3306 | 3306 listening | PASS |
| Memurai/Redis listening | 6379 | 6379 listening | PASS |
| Kafka broker listening | 9092 | 9092 listening | PASS |
| ChatServer 1 | 6001 | 6001 listening | PASS |
| ChatServer 2 | 6002 | 6002 listening | PASS |
| LoadBalancer | 5000 | 5000 listening | PASS |
| DBWriter process | alive | running and consuming | PASS |
| Hidden services with closed stdin | stay alive | services remained alive and served clients | PASS |

The servers, DBWriter and load balancer were started as hidden background processes. Their stdin loops ignore EOF, which allows `-WindowStyle Hidden` startup without the process exiting immediately.

### 2.1 Helper script verification

| Test | Procedure | Observation | Result |
|---|---|---|---|
| Clean stop | `scripts\stop-all.ps1` while all services were running | LoadBalancer, both ChatServers and DBWriter stopped; ports 5000/6001/6002 free | PASS |
| Full cold start | Stopped Kafka with `stop-all.ps1 -StopKafka`, then ran `scripts\start-all.ps1` | Script started Kafka, waited for 9092, then started DBWriter, both servers and the LB; all ports listening; exit code 0 | PASS |
| Kafka already running | `scripts\start-all.ps1` with Kafka on 9092 | Script reported “Kafka is already listening” and did not start a duplicate broker | PASS |
| Kafka stop path | `scripts\stop-all.ps1 -StopKafka` | Broker at 9092 stopped; MySQL and Memurai intentionally left running | PASS |

The final cold start command was:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\stop-all.ps1 -StopKafka
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start-all.ps1
```

## 3. Functional tests

### 3.1 Login and session

| Test | Procedure | Observation | Result |
|---|---|---|---|
| user1 login | user1 / pass123 via 127.0.0.1:5000 | Main window “DualThread - user1”, Connected | PASS |
| user2 login | user2 / pass123 via 127.0.0.1:5000 | Main window “DualThread - user2”, Connected | PASS |
| Different backends | Inspect server logs | user1 on server1, user2 on server2 during cross-server tests | PASS |
| Logout | user2 selected Sign out | Returned to “DualThread - Sign in”; server logged `User disconnected: user2` | PASS |
| Re-login | user2 signed in again | Main window returned; routed to server2; conversations and presence loaded | PASS |

### 3.2 Personal messaging

| Test | Procedure | Observation | Result |
|---|---|---|---|
| Cross-server send | user1 selected user2, sent `UI personal test 0821` | user2 received message; aligned bubbles and timestamps visible | PASS |
| Second send | user1 sent the same text again | user2 received the second copy; two DB rows | PASS |
| Enter key send | Set composer to `Enter send test 0835`, send `WM_KEYDOWN VK_RETURN` | Message sent, composer cleared, user2 received it | PASS |
| History | Select user2 after messages | Prior personal history displayed chronologically | PASS |
| Unread badge | user1 selected user3; user2 sent `Unread test 0822` | Badge “1” appeared on user2; preview updated; selecting user2 cleared badge | PASS |

### 3.3 Group messaging

| Test | Procedure | Observation | Result |
|---|---|---|---|
| Create group | user2 opened + New group, create mode, name `UI Group 0825`, member user1 | Group created; user1 and user2 saw the group; header showed the friendly name | PASS |
| Cross-server group message | user2 sent `UI group message 0826` to the group | user1 received it on server1; group history displayed it | PASS |
| Group name regression | Open group history and switch conversations | Header/list continued to show `UI Group 0825`; the raw group ID did not replace the name | PASS |
| Join group | user2 created `Join Test 0831`; user1 opened + New group, join mode, entered the group ID | UI showed `Join Test 0831`, “2 members”, and “Joined group.”; `tbl_GroupMembers` contained user1 and user2 | PASS |

### 3.4 Presence and typing

| Test | Procedure | Observation | Result |
|---|---|---|---|
| Presence dot | user1 main window while user2 online | Green dot and “Online” in the personal header | PASS |
| Typing indicator | user2 typed into the composer without sending | user1 header showed “user2 is typing...”; typing bubble appeared at the bottom of the thread | PASS |
| Presence after reconnect | Reconnect user1, wait for presence refresh | user2 returned to Online without restarting the client | PASS |

### 3.5 Reconnection and failure feedback

| Test | Procedure | Observation | Result |
|---|---|---|---|
| Connection loss | Stopped `LoadBalancer.exe` while user1 was connected | Header pill “Reconnecting...”, profile “Reconnecting...”, red transient message, send disabled | PASS |
| Composer retention | Attempted to send `Reconnect test 0830` while disconnected | Message was not sent; text remained in the composer | PASS |
| Recovery | Restarted the load balancer | Header returned to “Connected”, RTT shown, send enabled | PASS |
| Recovery toast | Wait for reconnect | “Reconnected. Messages are flowing again.” displayed | PASS |
| Message after recovery | Sent the retained text | user2 received it; server logged the personal message; SQL row 117 | PASS |

Presence was briefly stale immediately after the restart (user2 still appeared offline for a moment), then converged to Online after the refresh. This is expected eventual consistency, not a stuck state.

### 3.6 SQL persistence

Query used:

```sql
SELECT UniqueId, MessageId, UserId, Timestamp, ChatMessage, ConversationType, TargetId
FROM tbl_ChatDetails
WHERE ChatMessage IN (
  'UI personal test 0821',
  'Unread test 0822',
  'UI group message 0826',
  'Reconnect test 0830',
  'Enter send test 0835'
)
ORDER BY UniqueId;
```

Observed rows:

| UniqueId | Message | User | ConversationType | TargetId |
|---:|---|---|---|---|
| 113 | `UI personal test 0821` | user2 | personal | user1 |
| 114 | `UI personal test 0821` | user2 | personal | user1 |
| 115 | `Unread test 0822` | user2 | personal | user1 |
| 116 | `UI group message 0826` | user2 | group | `grp-3faff28acd9e2c-3367` |
| 117 | `Reconnect test 0830` | user1 | personal | user2 |
| 118 | `Enter send test 0835` | user1 | personal | user2 |

- `SELECT COUNT(*) FROM tbl_ChatDetails` returned **67** at the end of the run.
- Group rows: `UI Group 0825` (`grp-3faff28acd9e2c-3367`, owner user2) and `Join Test 0831` (`grp-3faff5e8d7fe86-c862`, owner user2), plus QA groups created during earlier checks.
- DBWriter remained alive and its log showed the corresponding stored messages.

Result: **PASS**.

## 4. Screenshot evidence

| File | Evidence |
|---|---|
| `docs/screenshots/login.png` | Clean login screen with DualThread branding |
| `docs/screenshots/personal-chat.png` | Personal conversation and message bubbles |
| `docs/screenshots/group-chat.png` | Group conversation with friendly group name and history |
| `docs/screenshots/typing-indicator.png` | “user2 is typing...” in header and typing bubble |
| `docs/screenshots/unread-badge.png` | Unread badge “1” on user2 while user3 is selected |
| `docs/screenshots/reconnect-feedback.png` | Reconnecting state, disabled send, preserved composer |
| `docs/screenshots/reconnected.png` | Connected state and reconnected toast |
| `docs/screenshots/group-joined.png` | Joined group with member count and toast |

The PNG files are full-window captures. The documentation images were cropped by 20 px on the left and 22 px at the bottom to remove the transparent window-frame/desktop strip and taskbar captured by `CopyFromScreen`.

## 5. Implemented but not conclusively automated

### Shift+Enter newline

`CComposerEdit::OnKeyDown` checks `GetKeyState(VK_SHIFT)` and only sends when Enter is pressed without Shift; otherwise it calls the base edit handler, which inserts a newline. The source path is correct, but the synthetic UI attempt did not prove the newline:

- A real mouse click focused the composer.
- `keybd_event` Shift+Enter plus `WM_CHAR` input resulted in `line two` and `line one` on the same line, with the caret in an unexpected position.
- This is consistent with synthetic keyboard focus/message-ordering limitations, not necessarily an application defect.

Status: **IMPLEMENTED / PARTIAL**. A manual keyboard test (hold Shift and press Enter) is recommended before treating this item as fully runtime-verified.

### Resize and DPI

The window uses `OnSize` and `OnGetMinMaxInfo`; the default outer size was measured at 1200 x 780, and the layout code has a minimum of 980 x 640. A separate resize/DPI runtime test was not performed after the final build.

Status: **IMPLEMENTED**, not separately runtime-tested.

### Group leave

The server supports `group_leave`, but the final client UI does not expose a dedicated Leave group action. The create/join flows required by this delivery were exercised.

## 6. Not run

- Automated unit, integration or UI test suites. The project has no test runner.
- Sustained high-message-volume or multi-hour soak testing.
- Kafka broker outage, Redis outage, and MySQL outage combinations beyond the reconnect/persistence paths above.
- Cross-machine networking (all verification used loopback).
- TLS, authentication hardening, or production deployment testing.
- Testing on a clean machine without Visual Studio/VC++ runtime/vcpkg installed.

## 7. Overall result

The delivered build meets the functional requirements exercised above: personal and group chat, cross-server routing, presence, typing, unread badges, history, group create/join, logout, reconnect feedback/recovery, and MySQL persistence through Kafka and native ODBC.

The only functional item with a non-conclusive runtime check is **Shift+Enter newline**; it is implemented and code-inspected, but not claimed as a passed automated test.
