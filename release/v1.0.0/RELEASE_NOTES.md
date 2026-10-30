# DualThread v1.0.0

Built: 2026-10-30T12:58:29+05:30
SAT gate: PASS
Git: main@135c642

## Executables

| File | Bytes | SHA-256 |
|---|---:|---|
| `ChatClient.exe` | 715776 | `8ada2030fc71efee...` |
| `ChatServer.exe` | 672256 | `7f5cfe03cd683031...` |
| `LoadBalancer.exe` | 483840 | `e2db7cd6ae572a11...` |
| `DBWriter.exe` | 464896 | `7d42f08391bae886...` |
| `hiredis.dll` | 120832 | `2026682b3031cc63...` |
| `rdkafka.dll` | 2398208 | `6ba3e8ca45144994...` |
| `lz4.dll` | 130048 | `d029d2e9cb7ca843...` |

## Run

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start-all.ps1
Start-Process .\bin\Release\ChatClient.exe -WorkingDirectory (Get-Location)
```

The complete acceptance workbook is included as `SAT_Report.xlsx`.
