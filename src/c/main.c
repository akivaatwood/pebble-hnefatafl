#include <pebble.h>

#define BOARD_SIZE 11
#define SQUARE_COUNT (BOARD_SIZE * BOARD_SIZE)
#define THRONE_INDEX 60
#define AI_DELAY_MS 450

typedef enum {
  PIECE_EMPTY = 0,
  PIECE_ATTACKER = 1,
  PIECE_DEFENDER = 2,
  PIECE_KING = 3
} Piece;

typedef enum {
  TURN_DEFENDERS = 0,
  TURN_ATTACKERS = 1
} Turn;

typedef enum {
  GAME_PLAYING = 0,
  GAME_DEFENDERS_WIN = 1,
  GAME_ATTACKERS_WIN = 2
} GameState;

typedef struct {
  uint8_t from;
  uint8_t to;
  int16_t score;
} AiMove;

static Window *s_window;
static Layer *s_board_layer;
static TextLayer *s_status_layer;
static TextLayer *s_help_layer;
static AppTimer *s_ai_timer;
static uint8_t s_board[SQUARE_COUNT];
static uint8_t s_choices[SQUARE_COUNT];
static int s_choice_count;
static int s_choice_index;
static int s_selected = -1;
static Turn s_turn;
static GameState s_game_state;
static uint32_t s_random_state;

static int board_index(int row, int col) {
  return (row * BOARD_SIZE) + col;
}

static int row_of(int index) {
  return index / BOARD_SIZE;
}

static int col_of(int index) {
  return index % BOARD_SIZE;
}

static bool is_corner(int index) {
  return index == 0 || index == BOARD_SIZE - 1 ||
         index == SQUARE_COUNT - BOARD_SIZE || index == SQUARE_COUNT - 1;
}

static bool is_restricted(int index) {
  return index == THRONE_INDEX || is_corner(index);
}

static bool is_hostile_empty_square(int index) {
  return index >= 0 && index < SQUARE_COUNT &&
         is_restricted(index) && s_board[index] == PIECE_EMPTY;
}

static bool same_side(Piece first, Piece second) {
  if (first == PIECE_ATTACKER || second == PIECE_ATTACKER) {
    return first == PIECE_ATTACKER && second == PIECE_ATTACKER;
  }
  return first != PIECE_EMPTY && second != PIECE_EMPTY;
}

static bool belongs_to_turn(Piece piece, Turn turn) {
  return turn == TURN_ATTACKERS ? piece == PIECE_ATTACKER :
         piece == PIECE_DEFENDER || piece == PIECE_KING;
}

static bool step_index(int index, int direction, int *result) {
  int row = row_of(index);
  int col = col_of(index);

  switch (direction) {
    case 0: row -= 1; break;
    case 1: col += 1; break;
    case 2: row += 1; break;
    default: col -= 1; break;
  }
  if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
    return false;
  }
  *result = board_index(row, col);
  return true;
}

static bool piece_has_move(int from) {
  Piece piece = (Piece)s_board[from];
  int direction;

  for (direction = 0; direction < 4; direction += 1) {
    int current = from;
    int next;

    while (step_index(current, direction, &next) && s_board[next] == PIECE_EMPTY) {
      if (piece == PIECE_KING || !is_restricted(next)) {
        return true;
      }
      current = next;
    }
  }
  return false;
}

static int collect_destinations(int from, uint8_t destinations[SQUARE_COUNT]) {
  Piece piece = (Piece)s_board[from];
  int count = 0;
  int direction;

  for (direction = 0; direction < 4; direction += 1) {
    int current = from;
    int next;

    while (step_index(current, direction, &next) && s_board[next] == PIECE_EMPTY) {
      if (piece == PIECE_KING || !is_restricted(next)) {
        destinations[count++] = (uint8_t)next;
      }
      current = next;
    }
  }
  return count;
}

static int collect_movable_pieces(Turn turn, uint8_t pieces[SQUARE_COUNT]) {
  int count = 0;
  int index;

  for (index = 0; index < SQUARE_COUNT; index += 1) {
    if (belongs_to_turn((Piece)s_board[index], turn) && piece_has_move(index)) {
      pieces[count++] = (uint8_t)index;
    }
  }
  return count;
}

static bool side_supports_capture(Piece mover, int index) {
  if (index < 0 || index >= SQUARE_COUNT) {
    return false;
  }
  return same_side(mover, (Piece)s_board[index]) || is_hostile_empty_square(index);
}

static bool king_is_captured(int king_index) {
  int adjacent[4];
  int hostile_count = 0;
  int direction;

  for (direction = 0; direction < 4; direction += 1) {
    if (step_index(king_index, direction, &adjacent[direction])) {
      if (s_board[adjacent[direction]] == PIECE_ATTACKER ||
          is_hostile_empty_square(adjacent[direction])) {
        hostile_count += 1;
      }
    } else {
      adjacent[direction] = -1;
    }
  }

  if (king_index == THRONE_INDEX) {
    return hostile_count == 4;
  }
  if ((adjacent[0] == THRONE_INDEX || adjacent[1] == THRONE_INDEX ||
       adjacent[2] == THRONE_INDEX || adjacent[3] == THRONE_INDEX)) {
    return hostile_count == 4;
  }
  return ((adjacent[0] >= 0 && adjacent[2] >= 0 &&
           s_board[adjacent[0]] == PIECE_ATTACKER &&
           s_board[adjacent[2]] == PIECE_ATTACKER) ||
          (adjacent[1] >= 0 && adjacent[3] >= 0 &&
           s_board[adjacent[1]] == PIECE_ATTACKER &&
           s_board[adjacent[3]] == PIECE_ATTACKER));
}

static int resolve_captures(int moved_to) {
  Piece mover = (Piece)s_board[moved_to];
  int captured = 0;
  int direction;

  for (direction = 0; direction < 4; direction += 1) {
    int victim;
    int support;
    Piece victim_piece;

    if (!step_index(moved_to, direction, &victim)) {
      continue;
    }
    victim_piece = (Piece)s_board[victim];
    if (victim_piece == PIECE_EMPTY || same_side(mover, victim_piece)) {
      continue;
    }
    if (victim_piece == PIECE_KING) {
      if (mover == PIECE_ATTACKER && king_is_captured(victim)) {
        s_board[victim] = PIECE_EMPTY;
        captured += 8;
      }
      continue;
    }
    if (step_index(victim, direction, &support) &&
        side_supports_capture(mover, support)) {
      s_board[victim] = PIECE_EMPTY;
      captured += 1;
    }
  }
  return captured;
}

static void update_status(void) {
  const char *status;
  const char *help;

  if (s_game_state == GAME_DEFENDERS_WIN) {
    status = "KING ESCAPES";
    help = "Hold Select: new game";
  } else if (s_game_state == GAME_ATTACKERS_WIN) {
    status = "KING CAPTURED";
    help = "Hold Select: new game";
  } else if (s_turn == TURN_ATTACKERS) {
    status = "ATTACKERS THINKING";
    help = "You defend the king";
  } else if (s_selected >= 0) {
    status = "CHOOSE DESTINATION";
    help = "Up/Down target  Select";
  } else {
    status = "DEFEND THE KING";
    help = "Up/Down cycle  Select";
  }
  text_layer_set_text(s_status_layer, status);
  text_layer_set_text(s_help_layer, help);
}

static void refresh_choices(void) {
  if (s_game_state != GAME_PLAYING) {
    s_choice_count = 0;
  } else if (s_selected >= 0) {
    s_choice_count = collect_destinations(s_selected, s_choices);
  } else {
    s_choice_count = collect_movable_pieces(s_turn, s_choices);
  }
  s_choice_index = 0;
  update_status();
  if (s_board_layer) {
    layer_mark_dirty(s_board_layer);
  }
}

static int current_choice(void) {
  return s_choice_count > 0 ? s_choices[s_choice_index] : -1;
}

static int find_king(void) {
  int index;
  for (index = 0; index < SQUARE_COUNT; index += 1) {
    if (s_board[index] == PIECE_KING) {
      return index;
    }
  }
  return -1;
}

static void finish_turn(void);

static void execute_move(int from, int to) {
  Piece piece = (Piece)s_board[from];

  s_board[to] = piece;
  s_board[from] = PIECE_EMPTY;
  resolve_captures(to);

  if (piece == PIECE_KING && is_corner(to)) {
    s_game_state = GAME_DEFENDERS_WIN;
  } else if (find_king() < 0) {
    s_game_state = GAME_ATTACKERS_WIN;
  }
  finish_turn();
}

static int attacker_move_score(int from, int to) {
  uint8_t board_copy[SQUARE_COUNT];
  int king = find_king();
  int before_distance = king >= 0 ?
    abs(row_of(from) - row_of(king)) + abs(col_of(from) - col_of(king)) : 0;
  int after_distance = king >= 0 ?
    abs(row_of(to) - row_of(king)) + abs(col_of(to) - col_of(king)) : 0;
  int score;
  int captures;

  memcpy(board_copy, s_board, sizeof(s_board));
  s_board[to] = s_board[from];
  s_board[from] = PIECE_EMPTY;
  captures = resolve_captures(to);
  score = (captures * 120) + ((before_distance - after_distance) * 4);
  if (find_king() < 0) {
    score += 30000;
  }
  if (king >= 0 && (row_of(to) == row_of(king) || col_of(to) == col_of(king))) {
    score += 8;
  }
  if (is_corner(to)) {
    score += 25;
  }
  memcpy(s_board, board_copy, sizeof(s_board));
  return score;
}

static uint32_t next_random(void) {
  s_random_state = (s_random_state * 1664525UL) + 1013904223UL;
  return s_random_state;
}

static bool choose_ai_move(AiMove *best_move) {
  uint8_t pieces[SQUARE_COUNT];
  uint8_t destinations[SQUARE_COUNT];
  int piece_count = collect_movable_pieces(TURN_ATTACKERS, pieces);
  int best_score = -32768;
  int tie_count = 0;
  int piece_index;

  for (piece_index = 0; piece_index < piece_count; piece_index += 1) {
    int from = pieces[piece_index];
    int destination_count = collect_destinations(from, destinations);
    int destination_index;

    for (destination_index = 0; destination_index < destination_count;
         destination_index += 1) {
      int to = destinations[destination_index];
      int score = attacker_move_score(from, to);

      if (score > best_score) {
        best_score = score;
        best_move->from = (uint8_t)from;
        best_move->to = (uint8_t)to;
        best_move->score = (int16_t)score;
        tie_count = 1;
      } else if (score == best_score) {
        tie_count += 1;
        if ((next_random() % (uint32_t)tie_count) == 0) {
          best_move->from = (uint8_t)from;
          best_move->to = (uint8_t)to;
          best_move->score = (int16_t)score;
        }
      }
    }
  }
  return piece_count > 0;
}

static void ai_timer_callback(void *context) {
  AiMove move;
  (void)context;
  s_ai_timer = NULL;

  if (s_game_state != GAME_PLAYING || s_turn != TURN_ATTACKERS) {
    return;
  }
  if (choose_ai_move(&move)) {
    execute_move(move.from, move.to);
  } else {
    s_game_state = GAME_DEFENDERS_WIN;
    update_status();
    layer_mark_dirty(s_board_layer);
  }
}

static void finish_turn(void) {
  s_selected = -1;
  if (s_game_state != GAME_PLAYING) {
    refresh_choices();
    return;
  }

  s_turn = s_turn == TURN_DEFENDERS ? TURN_ATTACKERS : TURN_DEFENDERS;
  if (collect_movable_pieces(s_turn, s_choices) == 0) {
    s_game_state = s_turn == TURN_DEFENDERS ?
      GAME_ATTACKERS_WIN : GAME_DEFENDERS_WIN;
    refresh_choices();
    return;
  }
  refresh_choices();
  if (s_turn == TURN_ATTACKERS) {
    s_ai_timer = app_timer_register(AI_DELAY_MS, ai_timer_callback, NULL);
  }
}

static void place_piece(int row, int col, Piece piece) {
  s_board[board_index(row, col)] = (uint8_t)piece;
}

static void reset_game(void) {
  static const uint8_t attackers[][2] = {
    {0,3},{0,4},{0,5},{0,6},{0,7},{1,5},
    {3,0},{3,10},{4,0},{4,10},{5,0},{5,1},{5,9},{5,10},
    {6,0},{6,10},{7,0},{7,10},{9,5},
    {10,3},{10,4},{10,5},{10,6},{10,7}
  };
  static const uint8_t defenders[][2] = {
    {3,5},{4,4},{4,5},{4,6},{5,3},{5,4},
    {5,6},{5,7},{6,4},{6,5},{6,6},{7,5}
  };
  unsigned int index;

  if (s_ai_timer) {
    app_timer_cancel(s_ai_timer);
    s_ai_timer = NULL;
  }
  memset(s_board, PIECE_EMPTY, sizeof(s_board));
  for (index = 0; index < sizeof(attackers) / sizeof(attackers[0]); index += 1) {
    place_piece(attackers[index][0], attackers[index][1], PIECE_ATTACKER);
  }
  for (index = 0; index < sizeof(defenders) / sizeof(defenders[0]); index += 1) {
    place_piece(defenders[index][0], defenders[index][1], PIECE_DEFENDER);
  }
  place_piece(5, 5, PIECE_KING);
  s_turn = TURN_DEFENDERS;
  s_game_state = GAME_PLAYING;
  s_selected = -1;
  refresh_choices();
}

static GColor board_color(void) {
#ifdef PBL_COLOR
  return GColorFromRGB(255, 170, 85);
#else
  return GColorWhite;
#endif
}

static GColor special_color(void) {
#ifdef PBL_COLOR
  return GColorRed;
#else
  return GColorLightGray;
#endif
}

static GColor cursor_color(void) {
#ifdef PBL_COLOR
  return GColorYellow;
#else
  return GColorBlack;
#endif
}

static GColor selected_color(void) {
#ifdef PBL_COLOR
  return GColorRed;
#else
  return GColorBlack;
#endif
}

static void draw_piece(GContext *ctx, GRect rect, Piece piece) {
  GPoint center = grect_center_point(&rect);
  int radius = rect.size.w / 2 - 2;

  if (radius < 2) {
    radius = 2;
  }
  graphics_context_set_fill_color(ctx,
    piece == PIECE_ATTACKER ? GColorBlack : GColorWhite);
  graphics_context_set_stroke_color(ctx,
    piece == PIECE_ATTACKER ? GColorWhite : GColorBlack);
  graphics_fill_circle(ctx, center, radius);
  graphics_draw_circle(ctx, center, radius);

  if (piece == PIECE_KING) {
    int arm = radius > 3 ? radius - 2 : 2;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx,
      GPoint(center.x - arm, center.y), GPoint(center.x + arm, center.y));
    graphics_draw_line(ctx,
      GPoint(center.x, center.y - arm), GPoint(center.x, center.y + arm));
  }
}

static void board_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int cell = bounds.size.w / BOARD_SIZE;
  int board_pixels;
  int origin_x;
  int origin_y;
  int cursor = current_choice();
  int available_width = bounds.size.w;
  int available_height = bounds.size.h;
  int row;
  int col;

#ifdef PBL_ROUND
  available_width = (available_width * 72) / 100;
  available_height = (available_height * 86) / 100;
#endif
  cell = available_width / BOARD_SIZE;
  if (cell > available_height / BOARD_SIZE) {
    cell = available_height / BOARD_SIZE;
  }
  board_pixels = cell * BOARD_SIZE;
  origin_x = (bounds.size.w - board_pixels) / 2;
  origin_y = (bounds.size.h - board_pixels) / 2;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  for (row = 0; row < BOARD_SIZE; row += 1) {
    for (col = 0; col < BOARD_SIZE; col += 1) {
      int index = board_index(row, col);
      GRect square = GRect(origin_x + col * cell,
                           origin_y + row * cell,
                           cell,
                           cell);
      GColor fill = is_restricted(index) ? special_color() : board_color();

      graphics_context_set_fill_color(ctx, fill);
      graphics_fill_rect(ctx, square, 0, GCornerNone);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, square);

      if (index == cursor) {
        graphics_context_set_stroke_color(ctx, cursor_color());
        graphics_draw_rect(ctx, grect_inset(square, GEdgeInsets(1)));
        graphics_draw_rect(ctx, grect_inset(square, GEdgeInsets(2)));
      }
      if (index == s_selected) {
        graphics_context_set_stroke_color(ctx, selected_color());
        graphics_draw_rect(ctx, grect_inset(square, GEdgeInsets(1)));
        graphics_draw_rect(ctx, grect_inset(square, GEdgeInsets(2)));
      }
      if (s_board[index] != PIECE_EMPTY) {
        draw_piece(ctx, square, (Piece)s_board[index]);
      }
    }
  }
}

static void cycle_choice(int delta) {
  if (s_game_state != GAME_PLAYING || s_turn != TURN_DEFENDERS ||
      s_choice_count <= 0) {
    return;
  }
  s_choice_index = (s_choice_index + delta + s_choice_count) % s_choice_count;
  layer_mark_dirty(s_board_layer);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  cycle_choice(-1);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  cycle_choice(1);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  int choice;
  (void)recognizer;
  (void)context;

  if (s_game_state != GAME_PLAYING || s_turn != TURN_DEFENDERS) {
    return;
  }
  choice = current_choice();
  if (choice < 0) {
    return;
  }
  if (s_selected < 0) {
    s_selected = choice;
    refresh_choices();
  } else {
    execute_move(s_selected, choice);
  }
}

static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  reset_game();
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  if (s_selected >= 0 && s_turn == TURN_DEFENDERS) {
    s_selected = -1;
    refresh_choices();
  } else {
    window_stack_pop(true);
  }
}

static void click_config_provider(void *context) {
  (void)context;
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 180, up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 180, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700,
                              select_long_click_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int status_height = 22;
  int help_height = 18;

  s_status_layer = text_layer_create(GRect(0, 0, bounds.size.w, status_height));
  text_layer_set_background_color(s_status_layer, GColorBlack);
  text_layer_set_text_color(s_status_layer, GColorWhite);
  text_layer_set_font(s_status_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  s_help_layer = text_layer_create(GRect(0,
                                         bounds.size.h - help_height,
                                         bounds.size.w,
                                         help_height));
  text_layer_set_background_color(s_help_layer, GColorBlack);
  text_layer_set_text_color(s_help_layer, GColorWhite);
  text_layer_set_font(s_help_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_help_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_help_layer));

  s_board_layer = layer_create(GRect(0,
                                     status_height,
                                     bounds.size.w,
                                     bounds.size.h - status_height - help_height));
  layer_set_update_proc(s_board_layer, board_update_proc);
  layer_add_child(root, s_board_layer);
  reset_game();
}

static void window_unload(Window *window) {
  (void)window;
  if (s_ai_timer) {
    app_timer_cancel(s_ai_timer);
    s_ai_timer = NULL;
  }
  layer_destroy(s_board_layer);
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_help_layer);
}

static void init(void) {
  s_random_state = (uint32_t)time(NULL) ^ 0x5441464CUL;
  app_message_open(APP_MESSAGE_INBOX_SIZE_MINIMUM,
                   APP_MESSAGE_OUTBOX_SIZE_MINIMUM);
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
