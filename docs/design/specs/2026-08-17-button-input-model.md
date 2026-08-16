# Button input model

Status: proposal. Nothing here is implemented.

## What the board actually gives us

Three buttons are visible on the case, and only two of them are ours.

| Button | Wiring | Available to firmware |
| --- | --- | --- |
| BOOT | GPIO0, active low | yes |
| KEY | GPIO18, active low | yes |
| PWR | power path, not in the documented pin map | **no** |

PWR is a hardware power control: a long press cuts power, a single click powers
the board on. Waveshare documents it that way and lists no GPIO for it, so
firmware cannot read it, cannot override it, and must not plan features around
it. It is worth stating plainly because "the middle button does nothing" reads
like an omission on our side, and it is not one.

The only trace PWR leaves for us is indirect: the boot after a PWR power-cycle
reports `ESP_RST_POWERON`, which `app_main` already logs.

## What the two real buttons currently do

`ButtonFilter` emits four events from two pins:

| Gesture | Event | Meaning |
| --- | --- | --- |
| KEY short | `Previous` | previous page |
| KEY long (2 s) | `EnterSetup` | open/close Wi-Fi setup |
| BOOT short | `Next` | next page |
| BOOT long (2 s) | `OpenMenu` | open the settings menu |

Taking BOOT long presses is safe despite GPIO0 being the download strap: the
strap is sampled at reset, not while running.

## The actual problem

The four events are **global**. Every gesture means the same thing everywhere,
so every new capability has to invent a new gesture, and the gesture vocabulary
on two buttons runs out immediately.

Except it is not really global, because two places already broke the rule:

- The OTA confirmation prompt reads BOOT as *accept* and KEY as *cancel*.
- The settings menu needs *move* and *activate*, which do not map onto
  *previous page* and *next page* at all.

So context-sensitivity already exists, twice, ad hoc, with no shared mechanism
and nothing telling the user which meaning is currently in force. That is the
thing to fix — not the shortage of buttons.

## Proposal

**1. Route gestures through the active screen, not globally.**

Keep `ButtonFilter` exactly as it is: it is a debounce filter and it is correct.
Above it, add a small routing layer where whatever is currently on screen
declares what each gesture does. A gesture the current screen does not claim
falls through to the global default (page navigation), so nothing that works
today stops working.

This turns four gestures into four gestures *per context*, which is enough for
the settings menu, the OTA prompt, a future alarm, and anything AirPlay wants,
without inventing a single new hold duration.

**2. Show the user what the buttons do.**

The strongest argument for context-sensitive buttons is also the strongest
argument against them: the same press does different things at different times.
On a device with no labels, that is only acceptable if the screen says so.

There is room for a two-item hint — one per button — at the bottom of a page.
It costs a line of layout and removes the guesswork that currently makes the OTA
prompt's BOOT/KEY meanings undiscoverable unless you read the prompt text.

**3. Do not add double-press.**

It is the obvious way to find more gestures and it should be resisted. A
double-press forces every single press to wait for the double-press window
before it can act, which makes the whole UI feel slow to serve one rare
shortcut. Context routing gets the same capability without taxing every press.

## Open questions

- Where the hint line lives: a page footer, or the tray. The tray is already
  carrying date, network, battery and now a dynamic indicator slot, so the
  footer is more likely, but the page-dot row is already down there.
- Whether `EnterSetup` should stay a global gesture. It is a recovery action —
  the way back when the network is wrong — so it arguably should be reachable
  from everywhere regardless of context, unlike everything else here.
