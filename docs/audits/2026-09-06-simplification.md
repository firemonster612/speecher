# Shared streaming and simplification pass

The initial check found that the streaming lifecycle was still duplicated. Both OpenAI and Anthropic retained separate connection state, event framing, timers, cancellation, retries and completion. The earlier audit changes were already present as local edits and were preserved. This pass starts from that audited working tree, not from the original Git HEAD alone.

`StreamingRefinement` now owns those common responsibilities. Its callers start or cancel a refinement and receive text, completion or failure signals. Each provider supplies a request builder and decoders for its event and error formats. Authentication, model options, screenshot payloads, provider progress events and terminal failure interpretation stay with their provider.

The shared module preserves the original operation deadline across a standard-speed retry. It retries only before text has been emitted, remembers a rejected fast mode only after standard mode succeeds, and invalidates cancelled or replaced requests before queued completion. Providers no longer each maintain copies of that state.

This pass removes 270 production lines overall, counting the new shared module.

The repository-wide simplification sweep reused the preceding whole-app audit, inventoried source and tests, scanned repeated blocks and unused declarations, and traced the remaining candidates through their implementations and callers. The resulting changes are deliberately narrower than the inspection.

| Area | Additional simplification | Behavior retained |
| --- | --- | --- |
| Audio settings | Removed six uncalled per-field setters. | The active `setAudioCaptureSettings` write path, normalization and notifications. |
| Correction storage | Removed the private writer plus public forwarding wrapper; callers use `store` directly. | Stored JSON and correction learning/update behavior. |
| Snippet restoration | Replaced two always-identical cursors with one. | Placeholder restoration and malformed-placeholder rejection. |
| Ydotool output | Removed unreachable copy-selection code and its command builder. | Typing, clipboard paste, and target selection acquisition. |
| Ydotool setup | Removed unused `canEnable` and `stateId` methods. | Setup probing, status presentation and helper execution. |
| Authentication | Removed an unused private API-key forwarding method. | Actual credential discovery and refresh paths. |
| Claude voice | Removed the unused vocabulary argument from URL-query construction. | Vocabulary is still sent through the keyterms header. |
| Tests | Removed two assertions for the unreachable copy helper and narrowed Claude voice includes. | Tests for active typing, paste, credentials, and voice behavior. |

The existing native front ends, provider/platform adapters, settings schema, transcript pipeline, asynchronous startup preparation, clipboard ownership and updater modules still hide real behavior from callers. Removing them would spread that behavior across the codebase. No UI layout, platform selection, stored setting, release format, or dependency was changed in this pass. Small remaining duplication, such as native credential-watch setup, did not justify introducing another module on its own.

The final build passed with `cmake --build build -j 4`. The refiner suite passed 39 checks with one opt-in live test skipped, up from the 33 passing baseline checks. The added cases cover non-text progress, successful timeout fallback preserving fast-mode eligibility, and timeout during a nested event loop in a text listener. The complete reduced Linux suite passed all 21 CTest entries in 33.53 seconds. `git diff --check` passed.

Independent review found one regression in the first extraction: a timeout during a text listener could fail the request, then let the suspended parser announce completion. A regression test reproduced this for both providers. Moving generation invalidation into the common reply-detachment path made both cases pass without adding separate failure/completion flags. Successful completion captures the generation after detachment; later cancellation still invalidates it. Other cleanup changes had no substantive review findings. The second independent GPT and Fable reviews each reported zero Standards findings and zero Spec findings; the first-pass timeout finding is resolved.

The build uses `SPEECHER_WITH_KDE=OFF` because full KDE development dependencies are unavailable. No live vendor requests, microphone hardware checks, or native macOS/Windows execution ran in this pass. No UI rendering changed. The earlier audit's live AT-SPI and screenshot checks describe that earlier patch and were not repeated for this refactor.

Reproduction inputs, the exact pre-turn comparison, review reports and build/test logs are under `.scratch/simplify/`. Changes remain local and uncommitted.
