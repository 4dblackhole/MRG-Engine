# PaddedTone.mp3

Synthetic test audio generated for this repository: 44100 Hz stereo, one second,
250 ms silence + 500 ms 1 kHz sine at half scale with opposite channel phase +
250 ms silence. Encoded with FFmpeg libmp3lame at 128 kbit/s. No third-party
recording or music is used. The waveform and fixture are dedicated to the public
domain under CC0-1.0. The file's LAME tag intentionally retains encoder delay and
padding, so the public PCM reader and FMOD playback duration must both expose
44100 frames and retain the intentional leading silence.
