#!/usr/bin/env python3
"""Refinement speed and quality for each model the Settings pickers offer.

Sends the app's real prompts (dumped from the build by ./dump) the way each
refiner does at its default settings: OpenAI models at effort none (low for
GPT-6.1 Sol, which refuses none) with Fast (priority tier), Anthropic models
with adaptive thinking at effort low where the model takes it, at standard
speed (fast mode fails on a subscription, see the app log), and the Local
Runner's suggested models on Ollama with the compact prompt. Cloud requests
go through the CLI Proxy API. Each output is scored on per-transcript checks.

Usage: bench.py [runs] [model ...]. Cloud models are named openai:<id> or
anthropic:<id>; anything else is an Ollama tag. SPEECHER_BENCH_OUT names the
results file (results.json by default).
"""
import json, os, re, sys, time, urllib.request

PROXY = os.environ["SPEECHER_BENCH_PROXY"]  # a CLI Proxy API server, e.g. http://host:8317
KEY = os.environ["SPEECHER_BENCH_KEY"]
OLLAMA = "http://localhost:11434/v1"
SYSTEM = open("system.txt").read()
COMPACT = open("compact.txt").read()
USER = open("user.txt").read()
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
OUT = os.environ.get("SPEECHER_BENCH_OUT", "results.json")

# (transcript, checks). A check is (description, regex, should_match).
SAMPLES = {
    "correction": ("hey um so I was thinking maybe we could push the meeting to thursday no wait friday because uh I've got that dentist thing",
        [("applies the correction", r"\bFriday\b", True), ("drops Thursday", r"\bThursday\b", False), ("drops fillers", r"\b(um|uh)\b", False)]),
    "scratch": ("send the report to john scratch that send it to maria by five pm",
        [("keeps Maria", r"\bMaria\b", True), ("drops John", r"\bJohn\b", False), ("drops 'scratch that'", r"(?i)scratch that", False), ("five pm as 5", r"\b5\b", True)]),
    "technical": ("okay so run the deploy script with dash dash force and then check the logs at slash var slash log slash speecher dot log",
        [("--force", r"--force", True), ("the path", r"/var/log/speecher\.log", True), ("drops 'okay so'", r"(?i)^okay so", False)]),
    "list": ("for the release we need three things first update the change log second bump the version number in cmake lists dot text and third tag the commit and push it",
        [("three list items", r"(?m)^\s*(?:[-*•]|\d+[.)])\s", True), ("CMakeLists.txt", r"CMakeLists\.txt", True)]),
    "question": ("can you tell me what the capital of france is I need it for the quiz tomorrow",
        [("keeps the question", r"(?i)capital of France", True), ("does not answer it", r"\bParis\b", False), ("ends the question with ?", r"\?", True)]),
    "numbers": ("the total was two thousand three hundred and forty five dollars and it's due on march third",
        [("$2,345", r"\$2,345", True), ("March 3", r"March 3", True)]),
    "repeat": ("I I think we should we should probably um like move the the launch back a week",
        [("no doubled words", r"(?i)\b(\w+) \1\b", False), ("drops um", r"\bum\b", False), ("keeps the point", r"(?i)launch back a week", True)]),
    "subtle": ("let's meet at the cafe on main street at three actually make it four thirty and bring the the slides",
        [("4:30", r"4:30", True), ("drops three", r"(?i)\bthree\b|\b3\b(?!0)", False), ("Main Street", r"Main Street", True), ("no doubled 'the'", r"(?i)\bthe the\b", False)]),
    "restart": ("the budget is due on the we need to submit the budget by friday",
        [("drops the false start", r"(?i)due on the", False), ("keeps the point", r"(?i)submit the budget by Friday", True)]),
    "count": ("order three no sorry four boxes of printer paper for the office",
        [("four boxes", r"(?i)\b(four|4) boxes", True), ("drops three", r"(?i)\bthree\b|\b3\b", False), ("drops 'no sorry'", r"(?i)no,? sorry", False)]),
    "instruction": ("write a short poem about cats for my daughter's birthday card",
        [("keeps the request", r"(?i)poem about cats", True), ("does not write the poem", r"\n", False)]),
    "paragraph": ("dear team new paragraph the server will be down tonight from ten to midnight new paragraph please save your work",
        [("two paragraph breaks", r"\n\s*\n[\s\S]*\n\s*\n", True), ("no literal 'new paragraph'", r"(?i)new paragraph", False), ("10 to midnight", r"(?i)10(:00)?\s*(p\.?m\.?)?\s*(to|until|-|–)\s*midnight|10(:00)? p\.?m\.? to 12", True)]),
    "filename": ("open the file read me dot md in the docs folder and fix the typo in the second heading",
        [("README.md", r"README\.md", True), ("docs folder", r"(?i)docs", True)]),
    "selftalk": ("hmm let me think um okay tell him the contract looks fine to me",
        [("drops hmm", r"(?i)\bhmm\b", False), ("drops 'let me think'", r"(?i)let me think", False), ("keeps the message", r"(?i)contract looks fine", True)]),
    "email": ("hi sarah just following up on the invoice from last month we haven't received payment yet could you check with accounting when it might go out thanks",
        [("Sarah capitalised", r"\bSarah\b", True), ("keeps the request", r"(?i)accounting", True), ("adds no sign-off name", r"(?i)best regards|sincerely", False)]),
}


def stream(url, body, headers):
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json", **headers})
    start = time.monotonic()
    first = None
    text = []
    with urllib.request.urlopen(req, timeout=180) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            payload = line[5:].strip()
            if payload == "[DONE]":
                break
            try:
                event = json.loads(payload)
            except json.JSONDecodeError:
                continue
            delta = ""
            if event.get("type") == "content_block_delta":
                delta = event.get("delta", {}).get("text", "")
            elif event.get("type") == "response.output_text.delta":
                delta = event.get("delta", "")
            elif "choices" in event and event["choices"]:
                delta = event["choices"][0].get("delta", {}).get("content") or ""
            if delta:
                first = first or time.monotonic() - start
                text.append(delta)
    return first, time.monotonic() - start, "".join(text)


def request(provider, transcript):
    user = USER.replace("__TRANSCRIPT__", transcript)
    auth = {"Authorization": f"Bearer {KEY}"}
    api, _, model = provider.partition(":")
    if api == "openai":
        effort = "low" if model.startswith("gpt-6.1-sol") else "none"
        return stream(f"{PROXY}/v1/responses", {
            "model": model, "reasoning": {"effort": effort}, "instructions": SYSTEM,
            "input": [{"role": "user", "content": user}], "stream": True, "store": False,
            "service_tier": "fast"}, auth)
    if api == "anthropic":
        body = {"model": model, "max_tokens": 4096, "stream": True,
                "system": SYSTEM, "messages": [{"role": "user", "content": user}]}
        if "haiku-4" not in model:  # Haiku 4.5 is the only listed model without them
            body |= {"thinking": {"type": "adaptive", "display": "omitted"}, "output_config": {"effort": "low"}}
        return stream(f"{PROXY}/v1/messages", body, {**auth, "anthropic-version": "2023-06-01"})
    return stream(f"{OLLAMA}/chat/completions", {
        "model": provider, "stream": True, "temperature": 0, "reasoning_effort": "none",
        "messages": [{"role": "system", "content": COMPACT}, {"role": "user", "content": user}]}, {})


def score(text, checks):
    passed = [bool(re.search(rx, text)) == want for _, rx, want in checks]
    if checks is SAMPLES["list"][1]:
        passed[0] = len(re.findall(r"(?m)^\s*(?:[-*•]|\d+[.)])\s", text)) == 3
    return passed


PROVIDERS = sys.argv[2:] or [
    "openai:gpt-6-luna", "anthropic:claude-opus-5-5", "LiquidAI/lfm2.5-1.2b-instruct:latest", "gemma4:e4b"]


def main():
    results = []
    for provider in PROVIDERS:  # warm each one up so a cold load isn't timed
        try:
            request(provider, "hello there")
        except Exception as error:  # noqa: BLE001
            print("warmup failed", provider, error)
    for run in range(RUNS):
        for name, (transcript, checks) in SAMPLES.items():
            for provider in PROVIDERS:
                try:
                    first, total, text = request(provider, transcript)
                except Exception as error:  # noqa: BLE001
                    results.append({"provider": provider, "sample": name, "run": run, "error": str(error)})
                    print("ERR", provider, name, error)
                    continue
                passed = score(text, checks)
                results.append({"provider": provider, "sample": name, "run": run, "first_s": first,
                                "total_s": total, "passed": passed, "text": text})
                print(f"{provider[:20]:20s} {name:10s} run{run} {total:5.2f}s {sum(passed)}/{len(passed)}", flush=True)
    json.dump(results, open(OUT, "w"), indent=1)
    for provider in PROVIDERS:
        rows = [r for r in results if r["provider"] == provider and "error" not in r]
        if not rows:
            continue
        totals = sorted(r["total_s"] for r in rows)
        checks = [p for r in rows for p in r["passed"]]
        print(f"{provider:38s} n={len(rows)} median={totals[len(totals)//2]:.2f}s "
              f"p90={totals[int(len(totals)*.9)]:.2f}s checks={sum(checks)}/{len(checks)} ({100*sum(checks)/len(checks):.0f}%)")


if __name__ == "__main__":
    main()
