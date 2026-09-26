# Transcribe files captures

Recorded on a KDE Plasma 6 (Wayland) virtual machine running Debian 13, with a
build of this branch installed and its desktop entry registered. Transcription
used the live ChatGPT Codex service and refinement used OpenAI, so the text is
real output, not a stub. The three input files are synthesized speech.

- `linux-e2e.mp4`: the whole flow. Three audio files are selected in Dolphin
  and opened with "Open with Speecher". The compact Transcribe window opens
  with the files listed, the batch runs, and the `-transcribed.txt` files
  appear beside the audio.
- `linux-compact-window.png`: the Configure step, as the window opens.
- `linux-processing.png`: the Transcribe step, with the second of three files
  at 60% while its audio is still being sent.
- `linux-results.png`: the Export step, with all three transcripts saved.
