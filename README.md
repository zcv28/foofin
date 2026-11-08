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
[fooyin nightly release](https://github.com/fooyin/fooyin/releases/tag/nightly); the stable release
doesn't have the features it uses.

1. Download `foofin-<version>.fyplugin` from this repository's [Releases](../../releases). It works
   on Windows and Linux.
2. In fooyin, open **Settings → Plugins → Install**, pick the file, then restart fooyin.

The plugin must match your fooyin build. If you update fooyin and the plugin stops loading, open
this repository's [Actions](../../actions/workflows/build.yml) page, click **Run workflow** to
build against the current nightly, and take the `.fyplugin` from that run's artifacts.

## Usage

1. **Settings → Jellyfin**: enter your server address, username and password, then click **Sign In**.
2. Open **View → Jellyfin Browser**, or add the *Jellyfin Browser* widget (in *Internet*) to your
   layout with **Layout → Editing mode**.
3. Double-click a track to play it, or right-click for more options.

Jellyfin playlists appear as fooyin playlists named "Jellyfin: …". They update from the server
every 30 minutes, or when you click **Sync Now** in the settings. They're locked because changes
aren't sent back to Jellyfin. You can turn this off in the settings.

## Building

You need the fooyin SDK for your fooyin build: a nightly release asset, or build fooyin with
`-DINSTALL_HEADERS=ON`. You also need Qt 6 and a C++23 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<fooyin sdk>
cmake --build build --target package_plugin   # creates build/foofin-<os>-<arch>.fyplugin
```

Pushing a `v*` tag builds both platforms and publishes a release with the packages.

## Notes

- Your password isn't saved, only the login token from the server.
- Foofin can't run at the same time as other plugins that take over HTTP streaming, such as foonic.

## License

GPL-3.0-or-later
