# Building BioSync for Windows (official, signed)

BioSync only counts as an **official** build when it is compiled with the shared **app secret**
baked in. That secret is what lets RiTEMS tell a genuine build apart from a modified/cracked one:
every attendance push is signed with it (HMAC-SHA256), and RiTEMS rejects any push that is not
correctly signed. A build compiled **without** the secret still runs, but RiTEMS answers its pushes
with `401 — Unsigned request rejected`, so its records stay stuck in "pending" forever.

> **The one rule:** never commit the secret and never hard-code it in a source file. It is only ever
> passed on the `cmake` command line, at build time, on your machine.

---

## 1. Where the secret comes from

The secret is a 64-character value stored in RiTEMS, in the gateway system config under the key
**`biosync-app-secret`**. Only a **vendor** user can see it (RiTEMS → *Settings → App Releases /
Vendor*). The exact same value is stored on the server, so client and server always agree.

- To read it from the server DB directly (ops only):
  `SELECT config_value FROM system_gateway_configs WHERE config_key='biosync-app-secret';`
- Treat it like a signing key: keep it in a password manager, not in the repo.

If you ever rotate it, you must (a) update the `biosync-app-secret` config in RiTEMS **and**
(b) rebuild + redistribute every client with the new value, or older clients stop being accepted.

---

## 2. Prerequisites (one-time)

| Tool | Version | Notes |
|------|---------|-------|
| Qt | **6.8.2** (MinGW 64-bit or MSVC 2022) | Install with the Qt Online Installer; tick *Qt 6.8.2 → MinGW 64-bit* (or MSVC). |
| CMake | 3.16+ | Bundled with Qt, or install separately. |
| Compiler | MinGW that ships with Qt, **or** Visual Studio 2022 | Match whatever Qt kit you installed. |
| Inno Setup | 6.x | For the installer (`installer/windows/BioSync.iss`). Download from jrsoftware.org. |

Add Qt's `bin` (e.g. `C:\Qt\6.8.2\mingw_64\bin`) and CMake to your `PATH`.

---

## 3. Build the signed executable

Open a terminal (the *Qt 6.8.2 (MinGW)* command prompt is easiest) in the repo root:

```bat
:: 1) Configure — THE SECRET GOES HERE, nowhere else
cmake -S . -B build -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBIOSYNC_APP_SECRET=PASTE_THE_64_CHAR_SECRET_HERE

:: 2) Compile
cmake --build build --config Release

:: 3) Bundle the Qt DLLs next to the .exe
windeployqt build\BioSync.exe
```

You now have `build\BioSync.exe` plus its Qt DLLs — a signed, official build.

> **MSVC instead of MinGW?** Same commands; just run them from the *x64 Native Tools Command Prompt
> for VS 2022* and use the MSVC Qt kit. Everything else is identical.

---

## 4. Make the installer

The Inno Setup script installs the app, registers the **headless boot task** (runs at startup as
SYSTEM, no login, no window) and the firewall rule. Point it at your build output and compile:

1. Open `installer\windows\BioSync.iss` in Inno Setup.
2. Set `#define MyBuildDir` to your `build` folder (the one holding `BioSync.exe` + DLLs).
3. Confirm `#define MyAppVersion` matches `project(BioSync VERSION x.y.z)` in `CMakeLists.txt`.
4. *Build → Compile*. Output: `BioSync_Setup_v<version>.exe`.

Because the installer keeps the same `AppId` and bumps `AppVersion`, installing it over an older
version is a true **in-place upgrade** — devices, config and pending records are kept, no fresh
install needed.

---

## 5. How the signing actually works (and why a crack can't connect)

- At build time, `-DBIOSYNC_APP_SECRET=...` bakes the secret into the binary as a compile
  definition (`CMakeLists.txt` → `target_compile_definitions`). It is **not** read from a file or
  the network at runtime, so it isn't sitting in a config anyone can copy.
- On every push, the app computes `HMAC-SHA256(secret, "<timestamp>.<body>")` and sends it in the
  `X-BioSync-Signature` header (with `X-BioSync-Timestamp`). See `src/service/ApiPusher.cpp`.
- RiTEMS recomputes the same HMAC with its stored copy of the secret and compares. It also rejects
  timestamps older than 5 minutes (replay protection). See the biosync push controller on the server.
- A **modified / cracked / repackaged** binary does not have the secret (you can't recompile it
  without the secret, and it isn't recoverable from a normal copy of the binary), so its pushes fail
  the signature check and RiTEMS refuses the data — even if the attacker also stole the API key.

Residual risk (be honest with yourself): a determined reverse-engineer with the official binary can
in principle extract the baked secret. That is the accepted limit of a symmetric scheme; rotate the
secret if you ever suspect it leaked.

---

## 6. How to verify a build is signed vs unsigned

Any of these:

1. **In the app** — open *About* (sidebar). It shows **Signed: Yes/No** and a short secret
   *fingerprint* (first bytes of a hash of the secret — safe to show, can't be reversed). An official
   build shows *Yes* and a fingerprint that matches other official builds.
2. **From the server's answer** — an unsigned build's pushes come back as
   `401 "Unsigned request rejected — please update BioSync to the latest signed version."` and its
   records stay pending. A signed build's records move to *sent*.
3. **On the binary** (advanced) — `strings BioSync.exe | findstr <known-fingerprint-bytes>`; only a
   build made with the real secret contains it.

---

## 7. If records are stuck in "pending" after an update

That means the running build is **unsigned** (built without the secret, or an old pre-signing 1.0.0).
No data is lost — pending records are queued and flush automatically once a correctly-signed build
runs. Fix it by installing an **official signed** build (from RiTEMS → downloads, or one you built
following §3–4 with the real secret).
