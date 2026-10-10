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

Each output is scored on per-transcript checks. One check was dropped when
rating ("10 to midnight" in the `paragraph` sample): the prompt does not ask
for digits there, and "ten to midnight" is fine.

`results.json` holds the 2026-10-05 run, whose Local Runner figures the
ratings still use. `results-2026-10-10.json` holds the run behind the cloud
ratings: every model the Settings Model rows list except the GPT-5.4 models,
which only an OpenAI API key reaches. It came from

```sh
SPEECHER_BENCH_OUT=results-2026-10-10.json python3 bench.py 3 \
  openai:gpt-6-luna openai:gpt-6.1-sol openai:gpt-6-astra openai:gpt-5.6-luna \
  openai:gpt-5.6-terra openai:gpt-5.5 anthropic:claude-opus-5-5 \
  anthropic:claude-opus-5 anthropic:claude-sonnet-5-5 anthropic:claude-haiku-5-5 \
  anthropic:claude-haiku-4-5-20251001
```

run as one process per provider, with GPT-5.5 in a third once the others had
finished. Haiku 4.5 ran under its dated ID, since the
proxy does not route the undated one.
