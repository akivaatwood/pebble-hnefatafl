#include <pebble.h>

#define MAX_BOARD_SIZE 11
#define MAX_SQUARE_COUNT (MAX_BOARD_SIZE * MAX_BOARD_SIZE)
#define DEFAULT_BOARD_SIZE 11
#define PERSIST_KEY_BOARD_SIZE 1
#define AI_DELAY_MS 450

extern uint32_t MESSAGE_KEY_BOARD_SIZE;

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
static uint8_t s_board[MAX_SQUARE_COUNT];
static uint8_t s_choices[MAX_SQUARE_COUNT];
static const uint8_t s_board_size_options[] = {7, 9, 11};
static uint8_t s_board_size = DEFAULT_BOARD_SIZE;
static int s_board_size_choice_index = 2;
static bool s_choosing_board_size;
static int s_choice_count;
static int s_choice_index;
static int s_selected = -1;
static Turn s_turn;
static GameState s_game_state;
static uint32_t s_random_state;

static bool valid_board_size(int size) {
  return size == 7 || size == 9 || size == 11;
}

static int board_size_option_index(int size) {
  int index;
  for (index = 0; index < (int)ARRAY_LENGTH(s_board_size_options); index += 1) {
    if (s_board_size_options[index] == size) {
      return index;
    }
  }
  return 2;
}

static int square_count(void) {
  return s_board_size * s_board_size;
}

static int board_index(int row, int col) {
  return (row * s_board_size) + col;
}

static int row_of(int index) {
  return index / s_board_size;
}

static int col_of(int index) {
  return index % s_board_size;
}

static int throne_index(void) {
  return board_index(s_board_size / 2, s_board_size / 2);
}

static bool is_corner(int index) {
  int count = square_count();
  return index == 0 || index == s_board_size - 1 ||
         index == count - s_board_size || index == count - 1;
}

static bool is_restricted(int index) {
  return index == throne_index() || is_corner(index);
}

static bool is_hostile_empty_square(int index) {
  return index >= 0 && index < square_count() &&
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
  if (row < 0 || row >= s_board_size || col < 0 || col >= s_board_size) {
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

static int collect_destinations(int from, uint8_t destinations[MAX_SQUARE_COUNT]) {
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

static int collect_movable_pieces(Turn turn, uint8_t pieces[MAX_SQUARE_COUNT]) {
  int count = 0;
  int index;

  for (index = 0; index < square_count(); index += 1) {
    if (belongs_to_turn((Piece)s_board[index], turn) && piece_has_move(index)) {
      pieces[count++] = (uint8_t)index;
    }
  }
  return count;
}

static bool side_supports_capture(Piece mover, int index) {
  if (index < 0 || index >= square_count()) {
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

  if (king_index == throne_index()) {
    return hostile_count == 4;
  }
  if ((adjacent[0] == throne_index() || adjacent[1] == throne_index() ||
       adjacent[2] == throne_index() || adjacent[3] == throne_index())) {
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

  if (s_choosing_board_size) {
    status = "BOARD SIZE";
    help = "Up/Down  Select";
  } else if (s_game_state == GAME_DEFENDERS_WIN) {
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
  for (index = 0; index < square_count(); index += 1) {
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
  uint8_t board_copy[MAX_SQUARE_COUNT];
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
  uint8_t pieces[MAX_SQUARE_COUNT];
  uint8_t destinations[MAX_SQUARE_COUNT];
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

static void place_pieces(const uint8_t positions[][2], size_t count,
                         Piece piece) {
  size_t index;
  for (index = 0; index < count; index += 1) {
    place_piece(positions[index][0], positions[index][1], piece);
  }
}

static void reset_game(void) {
  static const uint8_t attackers_7[][2] = {
    {0,3},{1,3},{3,0},{3,1},{3,5},{3,6},{5,3},{6,3}
  };
  static const uint8_t defenders_7[][2] = {
    {2,3},{3,2},{3,4},{4,3}
  };
  static const uint8_t attackers_9[][2] = {
    {0,3},{0,4},{0,5},{1,4},
    {3,0},{3,8},{4,0},{4,1},{4,7},{4,8},{5,0},{5,8},
    {7,4},{8,3},{8,4},{8,5}
  };
  static const uint8_t defenders_9[][2] = {
    {2,4},{3,4},{4,2},{4,3},{4,5},{4,6},{5,4},{6,4}
  };
  static const uint8_t attackers_11[][2] = {
    {0,3},{0,4},{0,5},{0,6},{0,7},{1,5},
    {3,0},{3,10},{4,0},{4,10},{5,0},{5,1},{5,9},{5,10},
    {6,0},{6,10},{7,0},{7,10},{9,5},
    {10,3},{10,4},{10,5},{10,6},{10,7}
  };
  static const uint8_t defenders_11[][2] = {
    {3,5},{4,4},{4,5},{4,6},{5,3},{5,4},
    {5,6},{5,7},{6,4},{6,5},{6,6},{7,5}
  };

  if (s_ai_timer) {
    app_timer_cancel(s_ai_timer);
    s_ai_timer = NULL;
  }
  memset(s_board, PIECE_EMPTY, sizeof(s_board));
  if (s_board_size == 7) {
    place_pieces(attackers_7, ARRAY_LENGTH(attackers_7), PIECE_ATTACKER);
    place_pieces(defenders_7, ARRAY_LENGTH(defenders_7), PIECE_DEFENDER);
  } else if (s_board_size == 9) {
    place_pieces(attackers_9, ARRAY_LENGTH(attackers_9), PIECE_ATTACKER);
    place_pieces(defenders_9, ARRAY_LENGTH(defenders_9), PIECE_DEFENDER);
  } else {
    place_pieces(attackers_11, ARRAY_LENGTH(attackers_11), PIECE_ATTACKER);
    place_pieces(defenders_11, ARRAY_LENGTH(defenders_11), PIECE_DEFENDER);
  }
  place_piece(s_board_size / 2, s_board_size / 2, PIECE_KING);
  s_turn = TURN_DEFENDERS;
  s_game_state = GAME_PLAYING;
  s_selected = -1;
  refresh_choices();
}

static void begin_new_game(void) {
  if (s_ai_timer) {
    app_timer_cancel(s_ai_timer);
    s_ai_timer = NULL;
  }
  s_board_size_choice_index = board_size_option_index(s_board_size);
  s_choosing_board_size = true;
  s_selected = -1;
  s_choice_count = 0;
  update_status();
  if (s_board_layer) {
    layer_mark_dirty(s_board_layer);
  }
}

static void start_selected_game(void) {
  s_board_size = s_board_size_options[s_board_size_choice_index];
  persist_write_int(PERSIST_KEY_BOARD_SIZE, s_board_size);
  s_choosing_board_size = false;
  reset_game();
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

static void draw_board_size_menu(GContext *ctx, GRect bounds) {
  static const char *labels[] = {"7 x 7", "9 x 9", "11 x 11"};
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  int item_height = 30;
  int menu_height = item_height * (int)ARRAY_LENGTH(labels);
  int origin_y = (bounds.size.h - menu_height) / 2;
  int index;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  for (index = 0; index < (int)ARRAY_LENGTH(labels); index += 1) {
    GRect item = GRect(12, origin_y + index * item_height,
                       bounds.size.w - 24, item_height);
    if (index == s_board_size_choice_index) {
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, item, 4, GCornersAll);
      graphics_context_set_text_color(ctx, GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, GColorBlack);
    }
    graphics_draw_text(ctx, labels[index], font, item,
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }
}

static void board_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int cell = bounds.size.w / s_board_size;
  int board_pixels;
  int origin_x;
  int origin_y;
  int cursor = current_choice();
  int available_width = bounds.size.w;
  int available_height = bounds.size.h;
  int row;
  int col;

  if (s_choosing_board_size) {
    draw_board_size_menu(ctx, bounds);
    return;
  }

#ifdef PBL_ROUND
  available_width = (available_width * 72) / 100;
  available_height = (available_height * 86) / 100;
#endif
  cell = available_width / s_board_size;
  if (cell > available_height / s_board_size) {
    cell = available_height / s_board_size;
  }
  board_pixels = cell * s_board_size;
  origin_x = (bounds.size.w - board_pixels) / 2;
  origin_y = (bounds.size.h - board_pixels) / 2;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  for (row = 0; row < s_board_size; row += 1) {
    for (col = 0; col < s_board_size; col += 1) {
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
  if (s_choosing_board_size) {
    int option_count = (int)ARRAY_LENGTH(s_board_size_options);
    s_board_size_choice_index =
      (s_board_size_choice_index + delta + option_count) % option_count;
    layer_mark_dirty(s_board_layer);
    return;
  }
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

  if (s_choosing_board_size) {
    start_selected_game();
    return;
  }
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
  begin_new_game();
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  if (s_choosing_board_size) {
    window_stack_pop(true);
  } else if (s_selected >= 0 && s_turn == TURN_DEFENDERS) {
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
  begin_new_game();
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

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
  Tuple *board_size_tuple = dict_find(iterator, MESSAGE_KEY_BOARD_SIZE);
  (void)context;

  if (!board_size_tuple) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Settings message missing BOARD_SIZE");
    return;
  }

  int board_size = board_size_tuple->value->int32;
  APP_LOG(APP_LOG_LEVEL_INFO, "Received board size: %d", board_size);
  if (!valid_board_size(board_size)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring invalid board size: %d", board_size);
    return;
  }

  persist_write_int(PERSIST_KEY_BOARD_SIZE, board_size);
  if (s_board_size != board_size) {
    s_board_size = (uint8_t)board_size;
    begin_new_game();
  } else if (s_choosing_board_size) {
    s_board_size_choice_index = board_size_option_index(board_size);
    layer_mark_dirty(s_board_layer);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_ERROR, "Settings message dropped: %d", reason);
}

static void init(void) {
  int saved_board_size = persist_read_int(PERSIST_KEY_BOARD_SIZE);
  s_random_state = (uint32_t)time(NULL) ^ 0x5441464CUL;
  if (valid_board_size(saved_board_size)) {
    s_board_size = (uint8_t)saved_board_size;
  }
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  AppMessageResult message_result = app_message_open(
    APP_MESSAGE_INBOX_SIZE_MINIMUM, APP_MESSAGE_OUTBOX_SIZE_MINIMUM);
  APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage opened: %d", message_result);
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
