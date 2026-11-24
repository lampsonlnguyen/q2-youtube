# YouTube for Q2 Pod

A Q2 Pod card app for watching public YouTube videos on the Shanling Q2. Search
by title in the native Streaming menu. The desktop companion prepares a
small MP4 over Wi-Fi. Play the saved file in the Q2 Pod Videos app.

## Build

On Linux or WSL, install `make clang lld zip` and prepare the Q2 Pod
glibc 2.28 sysroot used to build the
[Videos card app](https://github.com/DiamondBond/q2-videos#building).
Run `make SYSROOT=/path/to/q2/sysroot`, or just `make` if your sysroot is
at `~/.cache/q2/sysroot`. The Makefile targets the Q2's MIPS little-endian
NaN2008 ABI and produces `dist/youtube.zip`. `make check` needs a host C
compiler and Node.js.

## Install and use

1. Install the [Videos card app](https://github.com/DiamondBond/q2-videos)
   through Q2 Pod's App Store. It requires Q2 Pod 1.9.0 or later.
2. Unzip `dist/youtube.zip` at the microSD root, creating the hidden
   `.youtube` directory.
3. On a computer on the same Wi-Fi, install Node.js 22+, FFmpeg and
   [yt-dlp](https://github.com/yt-dlp/yt-dlp#installation), including any
   JavaScript runtime needed by your yt-dlp installation. Keep both
   `yt-dlp` and `ffmpeg` on PATH, or set the `YT_DLP` and `FFMPEG`
   environment variables to their executable paths.
4. Set `Q2_YOUTUBE_HOST` to the computer's LAN IPv4 address and run
   `node companion.mjs` from this source folder. In PowerShell, for example,
   run `$env:Q2_YOUTUBE_HOST = '192.168.1.100'` first, using your actual address.
   Allow inbound TCP 8765 on the private network if your firewall prompts.
5. Save the printed URL as `.youtube/companion-url` on the card. Keep the
   companion running. Update the URL if you restart it since it generates a
   new token each time. The LAN connection uses plain HTTP.
6. On the Q2, open **Streaming → YouTube**, select **Search YouTube**, and
   type a video title or artist. Select a result to save it to the card.
7. Once the app says the video is ready, open **Library → Apps → Videos →
   YouTube** and select the file by ID.

When upgrading, rebuild and reinstall the card app and restart the matching
desktop companion. Copy its new URL to `.youtube/companion-url`. If search or
transfer fails, check the companion terminal for the yt-dlp or FFmpeg error.

This version saves one public, non-live video at a time, up to 20 minutes.
It uses the Q2's `wget` and the Videos app for local playback. Google login,
YouTube Music library browsing, and live streaming are outside this version.
The companion terminal prints details if a video can't be fetched.
