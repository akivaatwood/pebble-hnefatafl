# Hnefatafl for Pebble

A complete 11 x 11 Hnefatafl game for Pebble watches. You command the king and
defenders; the watch commands the attackers.

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

The board starts from the common 11 x 11 Hnefatafl arrangement with 24
attackers, 12 defenders, and the king.

## Build

```sh
pebble build
```
