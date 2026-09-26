# Speecher Home with insights: web mockup

A replacement for the Dictation page. The page becomes Home: a compact
dictation card on top, usage insights below. It is the design reference for the
Qt, WinUI and SwiftUI ports.

Open `index.html` directly or serve the folder. There is no build step. The bar
at the top belongs to the mockup, not the design. It switches between:

| Param | Values | What it shows |
|---|---|---|
| `state` | `active` *(default)*, `new`, `lapsed` | A year of use; the first day; a streak that ended |
| `theme` | `dark` *(default)*, `light` | Breeze Dark / Breeze Light palette |
| `accent` | `blue` *(default)*, `violet`, `orange` | The Highlight role |
| `width` | `wide` *(default)*, `narrow` | 1040 px or 720 px window |
| `page` | `home` *(default)*, `general` | Home, or the General settings page with the Insights group |
| `insights` | `on` *(default)*, `off` | The Insights setting |
| `full` | present or absent | Drop the window height so a screenshot shows the whole page |

All numbers come from one generated session list, so they agree with each
other. Finishing a dictation in the mockup adds a session and the page updates.

## Sections

1. **Dictation card.** Status, the shortcut hint, Start/Stop, and the last
   transcript clamped to two lines with its word count, app and time, plus a
   copy button. It replaces the large transcript box and the four summary
   cards, which duplicated the sidebar.
2. **Stat tiles** for the chosen period (7 days, 30 days, this year, all time):
   words dictated compared with a famous book ("About half of *Hamlet*"),
   streak with this week's dots, dictations per active day, and hours of audio
   transcribed. The 7 and 30 day periods show the change against the period
   before.
3. **Activity.** A year heatmap in GitHub's layout. It can show dictations, words
   or minutes of audio. The four colour levels are quartiles of your own
   active days. A narrow window shows fewer weeks; cells never go below 10 px.
4. **When you talk.** Dictations by hour, with a label ("Morning talker") and
   your busiest weekday.
5. **Pace.** Speaking pace in words per minute and time saved against typing at
   40 wpm.
6. **Where your words go.** Share of words per Target app, tagged with its
   Writing Profile. The top five are shown and the rest fold into "other apps".
7. **Cleanup.** Filler words removed by refinement and Learned Corrections.
8. **Records.** Next word milestone, longest streak, longest dictation,
   busiest and wordiest day, first dictation.
9. **Privacy note** under the last card: insights stay on this computer and
   are never sent to the cloud, with a link to the setting.

## Book comparison

The words tile picks the book and plain fraction closest to your total: a
quarter, a third, half, three-quarters, as long as, twice, three or four times.
Half, as long as and twice win ties because they read easiest. Hovering shows
the book's word count.

| Book | Words | Source |
|---|---|---|
| *Macbeth* | 17,121 | Open Source Shakespeare |
| *Romeo and Juliet* | 24,545 | Open Source Shakespeare |
| *Hamlet* | 30,557 | Open Source Shakespeare |
| *The Great Gatsby* | 47,094 | Nathan Bransford's novel word counts |
| *Harry Potter and the Sorcerer's Stone* | 76,944 | Nathan Bransford |
| *The Hobbit* | 95,356 | Nathan Bransford |
| *To Kill a Mockingbird* | 100,388 | Nathan Bransford |
| *The Fellowship of the Ring* | 187,790 | Nathan Bransford |
| *Moby-Dick* | 209,117 | Nathan Bransford |
| *War and Peace* | 561,304 | Nathan Bransford |

Below about 4,000 words it counts Gettysburg Addresses (272 words) instead.
Published counts differ by edition by a few percent, which doesn't matter at
"about half".

## Insights setting

General gets an **Insights** group:

- **Keep insights about your dictation** (checkbox, on by default). The
  description says what is recorded (word counts, times, app names, never text
  or audio) and that it stays on this computer.
- While it's off, nothing new is recorded and a note says existing history
  stays until cleared.
- **Clear insights history…** is a button row. Clicking it swaps the row for an
  inline confirmation (Cancel / Delete History), not a dialog.

With insights off, Home shows the dictation card and one "Insights are off"
card that links to the setting. With insights on and an empty log (new install
or just cleared), it shows "No insights yet".

## Colours

Only palette roles: Window, Base, Button, Text, PlaceholderText (muted text),
Highlight. Every chart colour is Base mixed toward Highlight (30, 52, 76,
100 %), so it follows the user's accent and scheme on every platform. The
record dot on Start Dictation keeps the current page's red.

## Data the native app needs

None of this is stored today. While the Insights setting is on, each finished
Dictation Session would append one local record:

- end time (local date and hour)
- audio duration
- word count of the delivered text
- Target app name and Writing Profile
- filler words removed by refinement (count per word)

Streaks, records and every chart are derived from that log. Learned Correction
counts already exist.
