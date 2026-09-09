# [WIP] WildCardBoy

Handheld Game Console with Replaceable MCU

## Concept

WildCardBoy is a handheld game console whose "brain" (MCU) can be swapped out.
It consists of two units:

- **Host shell**: The main unit with an LCD, keypad, TF card slot, and power supply.
  It has no game-execution capability of its own; instead, it has a slot for a logic card on its back.
- **Logic card**: A daughterboard that plugs into the slot. It carries the MCU that runs the game,
  along with the CPU peripheral circuitry of the target game console (e.g. TinyJoypad, Arduboy).

The differences between game consoles are confined to the logic card,
so supporting a new game console only requires building a new logic card.

The host shell acts as an RP2350B-based host controller with the following roles:

- Detecting the logic card and identifying its type via the on-card EEPROM (card profile)
- Reading keypad input and sending it to the logic card
- Driving the LCD based on drawing commands received from the logic card
- Flashing game programs stored on the TF card to the MCU on the logic card

For detailed specifications, see [spec/](spec/).

For demos and the latest development status, see the [#WildCardBoy tag on X](https://x.com/hashtag/WildCardBoy?f=live).
