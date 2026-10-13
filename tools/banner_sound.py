"""Make the sound the HOME Menu plays when the game's icon is selected.

    python tools/banner_sound.py [IN.wav [OUT.wav]]

By default it reads one of the game's own zombie moans, as
tools/prepare_data.py converts it from the APK
(out/ZombieOutbreakSim/audio/sound/zombie_moan_02.wav, the loudest and
fullest of the ten). It writes 3DS Files/meta/banner_sound.wav, which the
Makefile's cia rule hands to bannertool's makebanner, beside the icon and
banner that tools/make_meta.py makes from the APK.

The HOME Menu plays a banner sound only if it has two channels and lasts at
most 3 seconds (3dbrew, CBMD): a mono one stays silent. bannertool does not
convert the sound, it wraps the WAV's PCM as it is, so the WAV must be right:
- stereo (a mono input goes to both channels);
- 16-bit PCM at 32 kHz. ffmpeg resamples it. 32 kHz keeps the banner near
  the size of other homebrew banners whose sound is known to play: a
  2.6 s moan is 330 KB, against 460 KB at 44.1 kHz;
- cut to 3 s;
- brought to a peak of -1 dBFS (the moan peaks at -6), the same gain on
  both channels;
- faded out over its last 50 ms, so a cut never clicks.
"""

import array
import os
import shutil
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IN = os.path.join(ROOT, "out", "ZombieOutbreakSim", "audio", "sound", "zombie_moan_02.wav")
DEFAULT_OUT = os.path.join(ROOT, "3DS Files", "meta", "banner_sound.wav")
FFMPEG = shutil.which("ffmpeg") or "ffmpeg"

CHANNELS = 2
RATE = 32000
MAX_SECONDS = 3.0
PEAK = 0.891  # -1 dBFS
FADE_SECONDS = 0.05
# Trailing frames under this (-60 dBFS) on both channels are dropped before
# the fade.
QUIET = 33


def resample(src):
    """src as 16-bit stereo PCM at RATE, as an array of interleaved samples."""
    fd, tmp = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        subprocess.run([FFMPEG, "-v", "error", "-y", "-i", src, "-map_metadata", "-1", "-fflags", "+bitexact",
                        "-ac", str(CHANNELS), "-ar", str(RATE), "-c:a", "pcm_s16le", tmp], check=True)
        with wave.open(tmp, "rb") as w:
            assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (CHANNELS, 2, RATE)
            pcm = array.array("h", w.readframes(w.getnframes()))
    finally:
        os.unlink(tmp)
    if sys.byteorder == "big":
        pcm.byteswap()
    return pcm


def main(src, dst):
    pcm = resample(src)
    frames = [pcm[i:i + CHANNELS] for i in range(0, len(pcm), CHANNELS)]
    frames = frames[:int(MAX_SECONDS * RATE)]
    while frames and max(abs(s) for s in frames[-1]) < QUIET:
        frames.pop()
    if not frames:
        sys.exit("banner_sound: %s is silent" % src)

    gain = PEAK * 32767 / max(abs(s) for f in frames for s in f)
    fade = min(len(frames), int(FADE_SECONDS * RATE))
    out = array.array("h")
    for i, f in enumerate(frames):
        left = len(frames) - i
        g = gain * (left / fade if left < fade else 1.0)
        out.extend(max(-32768, min(32767, int(round(s * g)))) for s in f)
    if sys.byteorder == "big":
        out.byteswap()

    with wave.open(dst, "wb") as w:
        w.setnchannels(CHANNELS)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(out.tobytes())
    print("wrote %s: %.2f s, %d channels at %d Hz, gain %.2f" % (dst, len(frames) / RATE, CHANNELS, RATE, gain))


if __name__ == "__main__":
    if len(sys.argv) > 3:
        sys.exit(__doc__)
    main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IN,
         sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT)
