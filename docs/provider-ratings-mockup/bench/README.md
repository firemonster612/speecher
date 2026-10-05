# Refinement benchmark

How the refinement Quality and Speed ratings were measured (2026-10-05).

1. Build Speecher, then compile `dump.cpp` against the build to write the app's
   real prompts (`system.txt`, `compact.txt`, `user.txt`):

   ```sh
   g++ -std=c++20 -fPIC dump.cpp -I../../../src -I../../../build \
     $(pkg-config --cflags Qt6Core) ../../../build/libspeecher_providers.a \
     ../../../build/libspeecher_domain.a $(pkg-config --libs Qt6Core) -o dump && ./dump
   ```

2. Run `SPEECHER_BENCH_PROXY=http://host:8317 SPEECHER_BENCH_KEY=... python3 bench.py 3`.
   Cloud requests go through a CLI Proxy API server; the Local Runner models
   through Ollama on `localhost:11434`.

Each output is scored on per-transcript checks. `results.json` holds the
2026-10-05 run. One check was dropped when rating ("10 to midnight" in the
`paragraph` sample): the prompt does not ask for digits there, and "ten to
midnight" is fine.
