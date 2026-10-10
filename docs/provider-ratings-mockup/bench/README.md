# Refinement benchmark

How the refinement Quality and Speed ratings were measured.

1. Build Speecher, then compile `dump.cpp` against the build to write the app's
   real prompts (`system.txt`, `compact.txt`, `user.txt`):

   ```sh
   g++ -std=c++20 -fPIC dump.cpp -I../../../src -I../../../build \
     $(pkg-config --cflags Qt6Core) ../../../build/libspeecher_providers.a \
     ../../../build/libspeecher_domain.a $(pkg-config --libs Qt6Core) -o dump && ./dump
   ```

2. Run `SPEECHER_BENCH_PROXY=http://host:8317 SPEECHER_BENCH_KEY=... python3 bench.py 3`.
   Cloud requests go through a CLI Proxy API server; the Local Runner models
   through Ollama on `localhost:11434`. Name models after the run count to
   bench others, such as `anthropic:claude-haiku-5-5` or `openai:gpt-6.1-sol`.

Each output is scored on per-transcript checks. `results.json` holds the
2026-10-05 run, whose Local Runner figures the ratings still use.
`results-2026-10-10.json` holds the run behind the cloud ratings: every model
the Settings Model rows list. Haiku 4.5 ran as `claude-haiku-4-5-20251001`,
since the proxy does not route the undated ID. One check was dropped when rating ("10 to midnight" in the
`paragraph` sample): the prompt does not ask for digits there, and "ten to
midnight" is fine.
