# devil_trigger_v3 — OPAL negotiation & trigger

This repository contains `devil_trigger_v3.cpp`, a small program that preloads an OPAL-RT project, sends negotiation packets (preset 6-float `msg2` and text `msg1`) to a negotiation endpoint, listens on UDP port 5008 for a `READY_` message from a specific IP, and calls `OpalExecute` when `READY_` is received.

Files added
- `devil_trigger_v3.cpp` — negotiation sender, listener, OPAL preload/execute.

Defaults
- Negotiation target: `192.168.10.123:5008`
- Listen (bind): `0.0.0.0:5008`
- Accept `READY_` from: `192.168.10.56`
- Default project path: `C:\Archivos_INI_OPAL\ergs_test5\models\CommAsync\CommAsync.llp`

Usage

Build (MSVC)

```cmd
cl devil_trigger_v3.cpp ws2_32.lib OpalApi.lib /I"C:\Path\To\OPAL\include" /link /LIBPATH:"C:\Path\To\OPAL\lib"
```

Build (MinGW / g++)

```bash
g++ -o devil_trigger_v3.exe devil_trigger_v3.cpp -lws2_32 -L"C:/Path/To/OPAL/lib" -lOpalApi -I"C:/Path/To/OPAL/include"
```

Run

```cmd
devil_trigger_v3.exe [project_path] [neg_ip] [neg_port] [expected_ready_ip] [bind_port]

Example (use defaults):
devil_trigger_v3.exe

Example (explicit):
devil_trigger_v3.exe "C:\Archivos_INI_OPAL\ergs_test5\models\CommAsync\CommAsync.llp" 192.168.10.123 5008 192.168.10.56 5008
```

Testing
- Start the `Server_simulator` program (it can reply with `READY_`).
- Run `devil_trigger_v3.exe`.
- Observe console logs: sent negotiation packets and then `READY_` decode and `OpalExecute` call.

Quick build & test script

Use the included PowerShell helper to build and optionally run a quick test that sends `READY_` to the local process:

```powershell
.\build_and_test.ps1 -OpalInclude "C:\Path\To\OPAL\include" -OpalLib "C:\Path\To\OPAL\lib" -RunTest
```

Notes
- The `-RunTest` flag starts `devil_trigger_v3.exe` and then runs `send_ready.exe` to send a `READY_` packet to `127.0.0.1:5008`.
- Ensure you run PowerShell with appropriate privileges if firewall prompts appear.

Troubleshooting
- Ensure OPAL SDK (headers + `OpalApi.lib`) is installed and accessible to the compiler/linker.
- If using MinGW, link `-lws2_32` and point to OPAL `.a/.lib` as appropriate.
- Windows firewall may block UDP; allow the executable or test on the same host.
- Bind may fail if the chosen IP/port is already in use.

Next steps
- Add a small config file parser or CLI flags, retry/resend logic, and unit tests. These are planned in the tracked TODO list.

Cleanup & error handling
- `devil_trigger_v3.cpp` includes basic retry/resend logic with CLI parameters to control retries and intervals.
- Review and adjust `MAX_RETRIES` and retry timings in the source to fit your network environment.
