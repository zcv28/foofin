# Foofin

A [fooyin](https://github.com/fooyin/fooyin) plugin for playing music from a [Jellyfin](https://jellyfin.org) server.

- Browse artists, albums, genres, playlists, favourites and recently played or added music, with
  cover art and search.
- Play anything, add it to a playlist or queue it, or drag it into a playlist.
- Gapless playback, seeking and the waveform seekbar work just like with local files.
- Your Jellyfin playlists show up as (read-only) fooyin playlists.
- Optional transcoding to MP3, Opus or AAC.
- Plays are reported back to Jellyfin, so play counts stay in sync.

## Install

Foofin needs a **nightly build of fooyin**, from the
[fooyin nightly release](https://github.com/fooyin/fooyin/releases/tag/nightly). See
[Why a fooyin nightly?](#why-a-fooyin-nightly) below.

1. Download `foofin-<version>.fyplugin` from this repository's [Releases](../../releases). It works
   on Windows and Linux.
2. In fooyin, open **Settings → Plugins → Install**, pick the file, then restart fooyin.

Each release says which fooyin build it was made for. If a later fooyin nightly refuses to load
the plugin, a new Foofin release is needed.

## Usage

1. **Settings → Jellyfin**: enter your server address, username and password, then click **Sign In**.
2. Open **View → Jellyfin Browser**, or add the *Jellyfin Browser* widget (in *Internet*) to your
   layout with **Layout → Editing mode**.
3. Double-click a track to play it, or right-click for more options.

Jellyfin playlists appear as fooyin playlists named "Jellyfin: …". They update from the server
every 30 minutes, or when you click **Sync Now** in the settings. They're locked because changes
aren't sent back to Jellyfin. You can turn this off in the settings.

## Why a fooyin nightly?

The current stable release, fooyin 0.14.1 (October 2026), won't do, for three reasons:

- **Seeking needs a newer fooyin.** Foofin streams files over HTTP with fooyin's own network
  stream. HTTP range requests, which let that stream jump to any position, were added to fooyin on
  2 November 2026, after 0.14.1. Without them a Jellyfin track can only be read from start to
  finish. You couldn't seek, and some files (for example M4A files whose index sits at the end)
  wouldn't play at all.
- **fooyin has no stable plugin ABI yet.** A plugin is compiled machine code that calls straight
  into fooyin's libraries, so it has to be built against the same headers as the fooyin it runs
  in. Several classes Foofin uses (remote streams, audio inputs, tracks) have changed since 0.14.1,
  so a build made for the nightly can't safely run in 0.14.1. For the same reason a Foofin build is
  tied to the fooyin build it was made for.
- **Only nightlies come with an SDK.** To compile a plugin you need fooyin's headers and libraries
  for your platform. fooyin started publishing these SDK packages after 0.14.1, so they exist only
  for nightly builds. That's what lets Foofin's CI build Windows and Linux packages without
  compiling fooyin itself.

Once a stable fooyin release ships with these changes and an SDK, Foofin can be built for it
instead.

## Building

You need the fooyin SDK for your fooyin build: a nightly release asset, or build fooyin with
`-DINSTALL_HEADERS=ON`. You also need Qt 6 and a C++23 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<fooyin sdk>
cmake --build build --target package_plugin   # creates build/foofin-<os>-<arch>.fyplugin
```

Every push is built and tested on Windows and Linux. Pushing a `v*` tag also builds the packages
and publishes a release.

## Notes

- Your password isn't saved, only the login token from the server.
- Foofin can't run at the same time as other plugins that take over HTTP streaming, such as foonic.

## License

GPL-3.0-or-later
