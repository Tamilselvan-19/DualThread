# Requirements Traceability

Status meanings:

- **PASS** — exercised at runtime in the delivered build.
- **IMPLEMENTED** — present and code-inspected, but not separately exercised in the final verification run.
- **PARTIAL** — implemented; the automated check was inconclusive, so manual confirmation is still recommended.
- **N/A** — not part of the current UI.

| # | Requirement | Implementation | Verification | Status |
|---|---|---|---|---|
| 1 | Native Windows C++ desktop chat application | `ChatClient/` (MFC, Unicode, v145 toolset) | Full `Release|x64` rebuild; client login and main window screenshots | PASS |
| 2 | Keep existing TCP/UDP/Kafka/Redis/MySQL/load-balancer architecture | Two servers, LB, Redis Pub/Sub/cache, Kafka topic `chat-messages`, MySQL persistence | Cross-server personal and group messages; logs show both server IDs; Kafka events stored by DBWriter | PASS |
| 3 | Native C++ ODBC access, no JDBC/Java DB access | `ChatServer/MySQLDatabase.cpp`, `DBWriter/DatabaseWriter.cpp` use `odbc32.lib` | ODBC connection, auth, history and inserts observed in running services | PASS |
| 4 | Preserve protocol/topic/key/table names where possible | Existing auth/chat frames, `chat-messages`, `chat:*`, `tbl_UserDetails`, `tbl_ChatDetails` retained | Source inspection and running logs | PASS |
| 5 | Document schema changes | `database/schema.sql`, `database/migration_v2_groups.sql`, README “Data model changes” | SQL files and docs present | PASS |
| 6 | UI approximately 1200 x 750 and resizable | `MainDlg` default size and `OnSize`/`OnGetMinMaxInfo` (minimum 980 x 640) | Default window measured at 1200 x 780 outer; resize logic code-inspected | IMPLEMENTED |
| 7 | “DualThread” branding | Login and main window titles/logo/brand text and footer | Login and main screenshots | PASS |
| 8 | Personal conversation list and chat | `MainDlg`, `CConversationList`, `CMessageView` | user1 ↔ user2 messages across two servers; screenshots | PASS |
| 9 | Group conversations | `GroupDlg`, `group_create`/`group_join`, `tbl_Groups`, `tbl_GroupMembers` | “UI Group 0825” created, cross-server group message received; “Join Test 0831” joined | PASS |
| 10 | Group create flow | `CGroupDlg` create mode, server `group_create` | UI creation logged, DB rows and UI header verified | PASS |
| 11 | Group join flow | `CGroupDlg` join mode, server `group_join` | user1 joined `Join Test 0831`; DB membership rows for user1 and user2; UI header “2 members” and “Joined group.” toast | PASS |
| 12 | Online presence dots | Redis `chat:online:*`, `presence_list`, `presence` frames, `m_onlineUsers` | Green dot for online user; presence recovered after reconnect | PASS |
| 13 | Unread counts/badges | Per-conversation `unread`, incremented for non-selected incoming messages | Badge “1” on user3-selected list while user2 sent; cleared on selecting user2 | PASS |
| 14 | Logout | `logout` frame, `ChatSession::Stop`, return to login loop | user2 signed out to “DualThread - Sign in”; server logged disconnect; re-login succeeded on server2 | PASS |
| 15 | Message history | `history` frame, `MySQLDatabase::GetHistory`, last 100 rows | Personal/group history loaded after selection and reconnect; DB rows verified | PASS |
| 16 | Aligned message bubbles and timestamps | `CMessageView`, `ChatMessageItem` with `mine`, sender, time | Personal/group screenshots show right/left bubbles with timestamps | PASS |
| 17 | Multiline composer | `CComposerEdit` with `ES_MULTILINE`, `ES_WANTRETURN` | Composer accepts text; Enter path verified | PASS |
| 18 | Enter sends | `CComposerEdit::OnKeyDown` posts send request for `VK_RETURN` without Shift | `WM_KEYDOWN VK_RETURN` sent “Enter send test 0835”; server log and user2 receipt | PASS |
| 19 | Shift+Enter inserts a newline | `CComposerEdit::OnKeyDown` calls the edit default when Shift is held | Code inspection confirmed; a synthetic Shift+Enter attempt did not conclusively insert a newline because of focus/input-injection limits | PARTIAL |
| 20 | Clear failure/reconnect feedback | Reconnect status pill/profile, disabled send, error toast, preserved composer | Stopped LB: “Reconnecting...”, disabled send, red toast, text retained; restarted LB: Connected and “Reconnected. Messages are flowing again.” | PASS |
| 21 | Automatic reconnection | `ChatSession::NetworkLoop` backoff 1–10s, re-auth, refresh groups/presence/history | Message “Reconnect test 0830” sent after LB restart and received by user2; SQL row 117 | PASS |
| 22 | Worker threads never touch MFC controls directly | Network/heartbeat threads post `WM_APP_*` messages with heap payloads | Code inspection of `ChatSession` and `MainDlg::OnSessionFrame`/`OnSessionStatus` | IMPLEMENTED |
| 23 | Safe socket/thread shutdown | `Stop` sets flag, shutdown socket, joins threads; server client threads closed/joined | Logout/re-login and hidden service shutdown/reuse during testing | PASS |
| 24 | MySQL persistence through DBWriter | Kafka consumer → `DatabaseWriter::InsertMessage` | Rows 113–118 for UI personal/group/reconnect/Enter messages; table total 67 rows at capture | PASS |
| 25 | Cross-server routing for personal and group chat | Redis `chat.broadcast`; `OnCrossServerMessage` dispatches `chat`, `group_chat`, `typing`, `group_sync`, `presence` | user1 on server1 and user2 on server2 exchanged personal and group messages; logs show both server IDs | PASS |
| 26 | Group display name not replaced by raw ID | `MainDlg::EnsureConversation` guard; `HandleHistoryItem` passes known name | Header/list showed “UI Group 0825” instead of the group ID | PASS |
| 27 | Complete build/startup instructions | `README.md`, `scripts/start-all.ps1`, `scripts/stop-all.ps1` | `stop-all.ps1` freed 5000/6001/6002; a cold start with Kafka stopped brought up Kafka, DBWriter, both servers and the LB; `start-all.ps1` also skipped an already-running Kafka | PASS |
| 28 | Documentation and screenshots | `docs/ARCHITECTURE.md`, this file, `docs/TEST_RESULTS.md`, `docs/screenshots/` | Files present and linked from README | PASS |
| 29 | Delivery ZIP | Staging/ZIP created from the source tree with configs, schema, docs, screenshots and `bin/Release` executables/DLLs | Final archive inspected; path reported to the user | PASS |

## Screenshot evidence

| Screenshot | Shows |
|---|---|
| `docs/screenshots/login.png` | DualThread login with server/port/user fields |
| `docs/screenshots/personal-chat.png` | Personal conversation, bubbles, timestamps, online header |
| `docs/screenshots/group-chat.png` | Group conversation, group name, member count, history |
| `docs/screenshots/typing-indicator.png` | “user2 is typing...” in header and message area |
| `docs/screenshots/unread-badge.png` | Unread “1” badge on a non-selected conversation |
| `docs/screenshots/reconnect-feedback.png` | Reconnecting pill/profile, disabled send, retained composer |
| `docs/screenshots/reconnected.png` | Connected state and “Reconnected. Messages are flowing again.” toast |
| `docs/screenshots/group-joined.png` | Joined group, header/member count and “Joined group.” toast |

## SQL evidence

| Message | Type | Target | Row |
|---|---|---|---:|
| `UI personal test 0821` | personal | user1 | 113–114 |
| `Unread test 0822` | personal | user1 | 115 |
| `UI group message 0826` | group | `grp-3faff28acd9e2c-3367` | 116 |
| `Reconnect test 0830` | personal | user2 | 117 |
| `Enter send test 0835` | personal | user2 | 118 |

All rows were present in `ChatDatabase.tbl_ChatDetails` with the expected `ConversationType` and `TargetId`. The table total at the end of the run was 67 rows.

## Not exercised

The following were not separately automated and are not claimed as runtime-verified in this delivery:

- Shift+Enter newline (implemented; automated injection inconclusive — see item 19).
- Simultaneous keyboard-driven resize/DPI scenarios beyond the default and layout code inspection.
- Group leave from the client UI (server support exists; no dedicated leave button was exercised).
- Kafka/MySQL/Redis outage combinations beyond the reconnect and persistence paths listed above.
- High-volume or long-duration load testing.
- Automated unit/integration test suites (the project has no test runner; verification used the live services and UI).
