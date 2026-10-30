# DualThread Automation Agents

This project ships **three OpenCode agents** and a supporting, dependency-light
test harness. The agents are real, runnable workflows — not a plan. They operate
on the same repository and share a single evidence format: the SAT JSON result
and the Excel acceptance workbook.

| Agent | File | Responsibility |
|---|---|---|
| **SAT Tester** | `.opencode/agents/sat-tester.md` | Inspect every project file, build, unit-test, run the full protocol/integration suite, and produce the Excel report. |
| **Release Engineer** | `.opencode/agents/release-engineer.md` | Gate on SAT, rebuild `Release|x64`, stage the EXEs/DLLs, hash and ZIP them, and optionally publish. |
| **Auto-Healer** | `.opencode/agents/auto-healer.md` | Read failing SAT checks, diagnose the source-level cause, edit the C++/config/SQL, rebuild the EXE and rerun the tests. |

The **pipeline** (`tools/sat/pipeline.py`) is an orchestrator script, not a
fourth agent. It simply calls the three workflows in order.

## Data flow

```text
                 +-----------------------+
                 |   sat-tester agent    |
                 | inventory / build /   |
                 | unit / integration    |
                 +-----------+-----------+
                             |
              reports/sat-results-latest.json
              reports/SAT_Report_latest.xlsx
                             |
          +------------------+------------------+
          |                                     |
          v                                     v
 +----------------------+            +----------------------+
 |  auto-healer agent   |            | release-engineer     |
 | source fix -> rebuild|            | gate -> stage -> zip |
 | -> rerun SAT         |            | -> optional publish  |
 +----------+-----------+            +----------+-----------+
            |                                   |
            +----------> SAT gate PASS <---------+
```

The auto-healer never patches `.exe` bytes. "Changing the EXE" always means:
**edit source → rebuild → verify the new binary hash and test result.**

## Quick start

From the project root:

```powershell
# Full acceptance run (build + unit + integration + Excel)
py -3 -m tools.sat.sat_runner --full

# Same thing through the convenience wrapper
powershell -ExecutionPolicy Bypass -File .\run-sat.ps1 -Mode full
```

The report is written to:

- `reports/sat-results-<timestamp>.json` — machine-readable evidence.
- `reports/sat-results-latest.json` — latest evidence for the other agents.
- `reports/SAT_Report_<timestamp>.xlsx` — formatted acceptance workbook.
- `reports/SAT_Report_latest.xlsx` — latest workbook.

Exit code `0` means the gate is `PASS` (zero `FAIL` checks). Warnings and skips
are still reported.

### Modes

| Command | What runs |
|---|---|
| `--mode full` (or `--full`) | Environment, inventory, structure, static, build, unit, integration, acceptance, Excel |
| `--mode quick` | Environment, inventory, structure, static, acceptance, Excel (no services) |
| `--mode files` | Environment and per-file inventory only |
| `--mode build` | Environment, inventory, static, build, unit, acceptance, Excel |
| `--skip-build` | Test the existing `bin/Release` artifacts |
| `--skip-integration` | Do not start or exercise the service stack |
| `--skip-ui` | Do not launch `ChatClient.exe` (headless/CI) |

If `py` is unavailable, call the interpreter directly:

```powershell
& "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe" -m tools.sat.sat_runner --full
```

## What the SAT agent actually tests

### Every tracked file

The inventory phase reads, hashes (SHA-256), classifies, and validates every
project file: C++ sources and headers, project/solution files, INI/JSON configs,
SQL, PowerShell, Markdown, docs images, and built PE files. Generated directories
(`reports/`, `release/`, `logs/`, `.git/`, `.vs/`, intermediate `Debug/` and
`Release/`) are excluded.

Per-file checks include:

- readability and non-zero size for source/config/docs;
- UTF-8 validity, BOM, merge-conflict markers;
- JSON and INI syntax;
- PE machine type for `.exe`/`.dll`, image/PDF signatures;
- source-file coverage by a build target or a local include.

### Structure and static analysis

- Required files, solution projects, project source references, `Release|x64`.
- INI required keys and cross-file consistency (ports, Kafka topic, brokers,
  database, distinct server IDs).
- SQL tables/columns and `MessageId` uniqueness.
- Markdown links and images, README operational coverage.
- PowerShell parser validation.
- vcpkg manifest and `.gitignore` hygiene.
- C++ delimiter balance, local include resolution, unsafe C string APIs, TODO
  markers, header guards.
- MFC threading invariant: worker threads must use `PostMessage`, not touch
  controls directly.
- Protocol constants: 16 MiB frame cap and 8000-character message limit.
- Python tooling compilation and a literal-credential scan.

### Build and unit tests

- `MSBuild CppChatMessaging.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64`.
- Parsed compiler diagnostics, warning threshold, first-party warnings.
- PE x64 validation and SHA-256 for all four EXEs and runtime DLLs.
- Stale-binary detection.
- Compiles `tools/sat/cpp_tests/test_common.cpp` against the real `Common`
  sources and runs JSON, SHA-256, INI, thread-safe queue, timestamp and
  concurrency assertions.

### End-to-end integration

The suite starts the stack through `scripts/start-all.ps1` when needed and then
drives the real wire protocol through the load balancer:

- authentication for two users, including rejection of a wrong password;
- Redis `PING`, auth cache and online presence;
- presence list and typing indicator;
- personal message across two servers (Redis Pub/Sub routing);
- group creation, membership notification, group message and group history;
- UDP heartbeat through the load balancer (`hb_ack`);
- empty, over-8000-character, unknown-recipient and injection-shaped input;
- 40-message latency/throughput sample with p95 threshold;
- Kafka topic `chat-messages` and DBWriter → MySQL persistence;
- logout, reconnect/re-authentication;
- optional `ChatClient.exe` window smoke test;
- runtime log scan for fatal/error lines.

If Kafka is unavailable, the runner retries the application stack without Kafka
and marks persistence checks as failed/skipped with an explicit reason.

### Acceptance traceability

`acceptance.py` maps each documented requirement (personal chat, groups,
presence, typing, unread badges, history, reconnect, heartbeat, Redis, Kafka,
MySQL, load balancing, MFC UI, validation, release build) to:

- source evidence (specific files/patterns), and
- runtime evidence (passing SAT test IDs).

The Excel **Traceability** sheet is the resulting matrix.

## Excel workbook

`reports/SAT_Report_latest.xlsx` contains:

1. **Dashboard** — gate, pass rate, counts, phase table, top defects.
2. **Test Cases** — every check with status, evidence, file:line, owner, tags.
3. **Defects** — actionable failure/warning queue for the auto-healer.
4. **File Inventory** — every inspected file with hash and per-file status.
5. **Build Log** — parsed MSVC diagnostics plus raw build tail.
6. **Environment** — host, toolchain, ports, git revision, integration run ID.
7. **Traceability** — requirement → code → runtime evidence.
8. **Release Manifest** — EXE/DLL hashes and PE architecture.
9. **Heal Log** — auto-healer actions (empty until the healer runs).
10. **Run History** — recent SAT runs in this workspace.

## Release agent

The release agent is blocked unless the latest SAT gate is `PASS`.

```powershell
# Safe default: rebuild + stage locally + ZIP + hashes
py -3 -m tools.sat.release_push --version v1.0.0

# Copy to a local folder or UNC share
py -3 -m tools.sat.release_push --version v1.0.0 --destination "\\fileserver\drop\DualThread"

# Explicit opt-in: commit/tag/push the package to the git remote
py -3 -m tools.sat.release_push --version v1.0.0 --git-push
```

Output:

- `release/<version>/bin/Release/` — four EXEs and runtime DLLs.
- `release/<version>/config|database|scripts|docs/` — deployment assets.
- `release/<version>/manifest.json` — source revision, SAT summary, hashes.
- `release/<version>/SHA256SUMS.txt`.
- `release/<version>/SAT_Report.xlsx` and `SAT_Report.json`.
- `release/DualThread-<version>-win64.zip`.
- `reports/release-manifest-latest.json`.

Publishing to a share or git remote is an external side effect. The agent asks
for explicit confirmation before `--destination` or `--git-push`.

## Auto-healer agent

Analysis is safe by default; edits require `--apply` or an agent session.

```powershell
# Build a repair plan and write per-defect request files
py -3 -m tools.sat.auto_heal

# Apply only deterministic, safe hygiene fixes (encoding, BOM, .gitignore)
py -3 -m tools.sat.auto_heal --apply

# Hand code defects to the OpenCode auto-healer, rebuild, rerun SAT
py -3 -m tools.sat.auto_heal --invoke-agent --rebuild --rerun
```

Artifacts:

- `reports/heal-plan-<timestamp>.json`.
- `reports/heal-requests/<timestamp>-NN-<test>.md` — evidence, candidate files,
  source excerpt, constraints and verification commands.
- `reports/heal-backups/<timestamp>/` — backups of files changed by safe fixes.
- `reports/heal-log.jsonl` — append-only audit log, surfaced in Excel.

Guardrails enforced in the agent definition and prompt:

- `tools/sat/**` is read-only; tests cannot be deleted, skipped or weakened.
- No binary patching; rebuild from source.
- No authentication bypass, disabled validation, SQL string concatenation or
  hard-coded test data.
- Keep protocol, ports, topic and channel names stable.
- Maximum 3 attempts per defect; stop and write a handoff report if no progress.
- No commit/tag/push unless the user explicitly asks.

## One-command pipeline

```powershell
# SAT only
py -3 -m tools.sat.pipeline

# SAT -> heal -> SAT -> release (heal invokes the agent)
py -3 -m tools.sat.pipeline --heal --invoke-agent --release --version v1.0.0
```

The pipeline is deliberately thin: it calls `sat_runner`, `auto_heal` and
`release_push` as subprocesses so each stage keeps its own exit code, report and
audit trail.

## Using the OpenCode agents

Open this folder in OpenCode and select an agent from the agent picker, or ask
the primary agent to use one:

- `sat-tester` — run and interpret the full acceptance suite.
- `release-engineer` — stage/publish a verified build.
- `auto-healer` — diagnose and fix failing checks.

The definitions live in `.opencode/agents/` and use OpenCode V2 Markdown
frontmatter. The auto-healer's edit permissions are limited to product source,
config, SQL, scripts and docs; test tooling and `.git` are denied.

## Configuration

`tools/sat/config.json` controls:

- project paths, excluded directories/globs, required files and artifacts;
- MSBuild, vcvars, vcpkg, Kafka and MySQL locations;
- ports and demo credentials used by integration tests;
- latency/persistence thresholds and UI smoke-test behavior;
- release staging contents and git remote/branch.

Runtime secrets are read from `config/*.ini` and masked in reports. The checked-in
demo credentials are flagged as a warning; replace them before production.

## Troubleshooting

| Symptom | Action |
|---|---|
| `openpyxl` missing | `py -3 -m pip install openpyxl` |
| MSBuild not found | Install VS C++ workload or add the path to `msbuild_candidates` |
| `vcpkg` libs missing | `vcpkg install hiredis:x64-windows librdkafka:x64-windows` |
| Kafka not listening | `scripts\start-all.ps1` starts it; or run with `--skip-integration` |
| MySQL/Redis down | Start `MySQL80` and `Memurai`; rerun the full suite |
| UI test skipped | No interactive desktop; use `--skip-ui` or run on a desktop session |
| Excel not generated | Check the `RPT-001` failure in the JSON and the Python traceback |
| Release blocked | SAT gate is not `PASS`; run the SAT/auto-healer agents first |
| OpenCode CLI not found | Set `OPENCODE_BIN` or pass `--agent-command`; request files are still written |

## Evidence policy

- A check is `PASS` only when it actually ran and produced evidence.
- A missing dependency is `SKIP` with a reason, never `PASS`.
- The gate is `FAIL` if any check is `FAIL`; warnings do not block, but they are
  listed in the Defects sheet and must be triaged.
- Every release is tied to a SAT report, a git revision and artifact hashes.
