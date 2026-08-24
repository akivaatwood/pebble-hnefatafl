# Hnefatafl for Pebble

A complete Hnefatafl game for Pebble watches with 7 x 7, 9 x 9, and 11 x 11
boards. You command the king and defenders; the watch commands the attackers.

Open the app's settings in the Pebble phone app to choose a board size and view
a compact controls and rules reference. Changing the size starts a new game;
the choice is saved on the phone and watch.

The watch also asks for a board size whenever the app starts or a new game is
requested. Use Up and Down to highlight 7 x 7, 9 x 9, or 11 x 11, then press
Select to begin.

## Controls

- **Up / Down:** Cycle through movable pieces or legal destinations.
- **Select:** Choose a piece or confirm its move.
- **Back:** Cancel the selected piece; otherwise exit normally.
- **Hold Select:** Start a new game.

## Rules

- All pieces move any unobstructed distance orthogonally, like a chess rook.
- Only the king may occupy the central throne or a corner refuge.
- The defenders win when the king reaches any corner.
- Ordinary pieces are captured by sandwiching them between two enemies.
- Empty corners and the empty throne act as hostile capturing squares.
- The king is captured by four attackers on the throne, three attackers while
  adjacent to the throne, or an opposing pair elsewhere.
- The attackers win if the king is captured or the defenders have no legal
  moves.

The 7 x 7 game uses a Brandubh-style layout, the 9 x 9 game uses a
Tablut-style layout, and the 11 x 11 game keeps the original Hnefatafl layout.

## Build

```sh
pebble build
```
