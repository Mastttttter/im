# IM

IM is a Qt Quick desktop instant messaging client built on [Tox](https://tox.chat/). It supports local encrypted profiles, friend and group chat, file transfer, audio/video calls, and an OpenAI-compatible AI assistant.

The UI is primarily Chinese and the application window title is `IM`.

## Features

- Local login/register flow with encrypted per-account storage.
- Tox identity creation, restoration, and persistence.
- Friend requests, friend deletion, and friend remark editing.
- One-to-one text chat.
- Group/conference chat.
- File sending and receiving.
- Audio and video calls through ToxAV and Qt Multimedia.
- AI assistant backed by a configurable OpenAI-compatible chat completions endpoint.
- Light/dark Material-themed Qt Quick UI.
- Notice/log center for application events.

## Tech stack

- C and C++26
- CMake 4.3.3+
- Qt 6.10+ (`Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `QuickDialogs2`, `Multimedia`)
- libcurl
- SQLCipher through SQLiteCpp
- Vendored `c-toxcore`
- Vendored `SQLiteCpp`

The project builds `toxcore_shared` with ToxAV enabled. Third-party tests, Tox bootstrap daemons, SQLiteCpp tests, and SQLiteCpp examples are disabled by the root CMake configuration.

## Prerequisites

Install these before configuring the project:

- CMake 4.3.3 or newer
- Ninja
- A C++26-capable compiler
- Qt 6.10 or newer with the modules listed above
- libcurl development files
- SQLCipher development files
- libsodium
- opus
- libvpx
- pthreads/Threads support

On Linux, the exact package names depend on the distribution. CMake must be able to discover Qt 6, CURL, SQLCipher, libsodium, opus, and libvpx.

## Build

From the repository root:

```sh
cmake -S . -B out/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug
```

The executable is written to:

```text
out/Debug/bin/im
```

## Run

```sh
./out/Debug/bin/im
```

If you want `call_config.ini` or `bootstrap_nodes.json` to be used, keep them in either the process current working directory or beside the executable, as described below.

## First login

The login screen accepts either an existing local account or a new account.

Account rules:

- Account name: 5-10 characters.
- Account name characters: English letters, digits, and underscore only.
- Password: English letters and digits only, at least 8 characters.

Important: the app does not store the password. The password is used to open the encrypted local database. If the password is forgotten, that profile's local data may be unrecoverable.

## Local data and encryption

The app stores profile data under Qt's `QStandardPaths::AppDataLocation` for organization `im` and application `im`.

Profile database paths:

```text
default account: <AppDataLocation>/app.db
named account:   <AppDataLocation>/profiles/<account>/app.db
```

Stored data includes:

- Tox savedata
- contacts and remarks
- friend messages
- conference/group metadata
- conference messages
- AI assistant history
- UI/theme preferences
- AI settings

Each profile also uses lock/shared-memory guards so the same account cannot be opened in another process at the same time.

## AI assistant

The AI assistant uses a libcurl client that sends OpenAI-compatible chat completion requests to:

```text
<base_url>/chat/completions
```

Default settings:

```text
base URL:    https://api.tokenpony.cn/v1
provider:    tokenpony
model:       qwen3-8b
temperature: 0.0
max tokens:  4096
```

Configure the endpoint, model, provider, API key, temperature, and token limits in the application UI. The API key is saved in the encrypted local profile database, not read from an environment variable.

For OpenAI-compatible providers, set the base URL without the trailing `/chat/completions`; the app appends that path automatically.

## Tox bootstrap nodes

The app can load optional Tox bootstrap nodes from `bootstrap_nodes.json`.

Search locations:

1. Application executable directory.
2. Current working directory.

The JSON may be either an array or an object with `nodes` or `bootstrap_nodes`.

Example:

```json
{
  "nodes": [
    {
      "address": "tox.abilinski.com",
      "port": 33445,
      "public_key": "PUBLIC_KEY_HEX"
    }
  ]
}
```

Accepted address fields include `address`, `host`, `ipv4`, or `ip`. Accepted public-key fields include `public_key`, `publicKey`, `publicKeyHex`, or `key`.

If no valid file is found, the app uses embedded public bootstrap nodes.

## Audio/video call configuration

Audio/video call settings can be configured with `call_config.ini`.

Search locations:

1. Current working directory.
2. Application executable directory.

The repository includes a default file:

```ini
[audio]
bitrate_kbps=48
sample_rate=48000
channels=1
frame_duration_ms=20

[video]
bitrate_kbps=5000
send_fps=15
target_width=0
target_height=0
keep_aspect=1
```

Restart the app after editing this file. Values are range-checked by the application.

## Repository layout

```text
.
├── CMakeLists.txt                 # Root CMake build definition
├── main.cpp                       # Qt app entry point and QML context setup
├── call_config.ini                # Optional audio/video call settings
├── include/ai/                    # AI client public header
├── resources/                     # Qt resource file and SVG assets
├── src/ai/                        # libcurl OpenAI-compatible AI client
├── src/app/                       # App controller, models, profile/storage services
├── src/audio/                     # Qt Multimedia audio manager
├── src/core/                      # Tox wrapper, app logging, call config
├── src/persistence/               # SQLCipher/SQLiteCpp storage layer
├── src/qml/                       # Qt Quick UI, dialogs, components, theme
├── src/video/                     # Qt camera/video manager
└── third_party/                   # Vendored c-toxcore and SQLiteCpp
```

Generated build directories such as `build/`, `out/`, and `cmake-build-*` are ignored by Git.

## Development notes

- `CMAKE_RUNTIME_OUTPUT_DIRECTORY` is set to `<build-dir>/bin`.
- The QML module URI is `im`, version `1.0`.
- The app uses Qt Quick Controls Material style.
- Tox savedata is restored from the encrypted database on login, or created on first use.
- The app sets the Tox self name to the account name.
- `call_config.ini` changes require an app restart.
- There are currently no project-level automated tests configured.

## Smoke test

A basic manual smoke test is:

```sh
cmake -S . -B out/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/Debug
./out/Debug/bin/im
```

Then verify that the login screen opens, a new account can be created, and the main chat UI appears after login.

## Troubleshooting

### CMake cannot find Qt

Make sure Qt 6.10+ is installed and discoverable by CMake. You may need to set `CMAKE_PREFIX_PATH` to the Qt installation prefix.

### CMake cannot find SQLCipher

Install SQLCipher development files and make sure CMake can find the library. The app enables `SQLITE_HAS_CODEC` and links SQLCipher when available.

### Login fails with an encrypted database error

The most likely causes are a wrong password, an incompatible database file, or a profile already locked by another running process.

### AI requests fail

Check that the base URL, model name, provider, and API key are set correctly in the UI. The base URL should be the API root, not the full `/chat/completions` endpoint.
