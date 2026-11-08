# Foofin

A [fooyin](https://github.com/fooyin/fooyin) plugin for playing music from a [Jellyfin](https://jellyfin.org) server.

- Browse artists, albums, genres, playlists, favourites and recently played or added music, with
  cover art and search.
- Play anything, add it to a playlist or queue it, or drag it into a playlist.
- Seek within tracks, just like local files.
- Optional transcoding to MP3, Opus or AAC.
- Plays are reported back to Jellyfin, so play counts stay in sync.

## Install (Windows)

Foofin needs a **nightly build of fooyin**; the stable release doesn't have the features it uses.

1. Install the latest fooyin nightly from the
   [fooyin nightly release](https://github.com/fooyin/fooyin/releases/tag/nightly).
2. Download `fyplugin_foofin.dll` from this repository's
   [Actions](../../actions/workflows/build.yml) page. Open the latest successful run, and its
   artifact name shows the fooyin version it was built for.
3. In fooyin, open **Settings → Plugins → Install**, pick the DLL, then restart fooyin.

The plugin must match your fooyin version. After updating fooyin, get a fresh DLL: re-run the
workflow (**Run workflow** on the Actions page) to build against the current nightly.

## Install (Linux)

Build and install fooyin from source with `-DINSTALL_HEADERS=ON`, then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp build/fyplugin_foofin.so ~/.local/lib/fooyin/plugins/
```

## Usage

1. **Settings → Jellyfin**: enter your server address, username and password, then click **Sign In**.
2. Open **View → Jellyfin Browser**, or add the *Jellyfin Browser* widget to your layout.
3. Double-click a track to play it, or right-click for more options.

## Notes

- Your password isn't saved, only the login token from the server.
- Playlists keep working if you sign in again, but not if the server address changes.
- Foofin can't run at the same time as other plugins that take over HTTP streaming, such as foonic.

## License

GPL-3.0-or-later
