/*=============================================================================
 * Kaissa Chess Engine - WinBoard Protocol Version
 * Based on the original Turbo-C Kaissa Chess Engine (1992)
 *=============================================================================*/
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Non-blocking stdin for permanent brain */
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif
#ifdef __ANDROID__
#include <arm_neon.h>
#endif
/*=============================================================================
 * Language and Type Definitions
 *=============================================================================*/
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWRD;
#ifndef _WIN32
typedef int BOOLEAN;
#endif
typedef unsigned long long U64;
#define TRUE  1
#define FALSE 0
#define OK    0
#ifndef _WIN32
#define ERROR -1
#endif
/*=============================================================================
 * Chess Constants
 *=============================================================================*/
#define TOTAL_NUM_PIECE  32
#define NUM_PIECE        16
#define NSQUARE          64
#define DUMMY           (-1)
#define NDIR             8
#define NLINE            8
#define NFILE            8
#define NPIECE           6
#define LINELEN          8
#define FIRSTLINE        0
#define LASTLINE         7
/* Move flags */
#define M_EN_PASSANT     0x01
#define M_CASTLE_LEFT    0x02
#define M_CASTLE_RIGHT   0x04
#define M_NULLMOVE       0x08
#define M_PROMOTION      0x10
#define M_CAPTURE        0x20
#define M_ISCHECK        0x40
#define M_KILLER         0x80
/* Position flags */
#define P_CHECK          0x10
#define P_CAPTURE        0x20
/* Piece colors */
#define NOCOLOR          -1
#define WHITE_PIECE      0
#define BLACK_PIECE      1
/* Piece names */
#define NOPIECE          0
#define KING             1
#define QUEEN            2
#define ROOK             3
#define BISHOP           4
#define KNIGHT           5
#define PAWN             6
/* Piece costs — Kaissa original ratios scaled to centipawn base (P=100).
 * Kaissa internal: K=255, Q=19, R=10, B=7, N=7, P=2  →  ×50 per unit.
 * King stays as a large sentinel value for SEE / mate detection. */
#define KING_COST        20000
#define QUEEN_COST       950   /* Kaissa 19  × 50  */
#define ROOK_COST        500   /* Kaissa 10  × 50  */
#define BISHOP_COST      350   /* Kaissa  7  × 50  */
#define KNIGHT_COST      350   /* Kaissa  7  × 50  */
#define PAWN_COST        100   /* Kaissa  2  × 50  */
/* Castle flags */
#define CASTLELEFT       8
#define CASTLERIGHT      4
#define CASTLELEFTDONE   2
#define CASTLERIGHTDONE  1
/* Search constants */
#define MAXDEPTH         18
#define MOVE_STACK_SIZE  500
#define POS_STACK_SIZE   MAXDEPTH
/* Evaluation constants */
#define MAT_INFINITY     25000
#define POS_INFINITY     0x7FFF
/* Direction constants */
typedef unsigned char DIR;
#define NODIR 0x00
#define _LEFT_UP   0x01
#define _UP        0x02
#define _RIGHT_UP  0x04
#define _RIGHT     0x08
#define _RIGHT_DOWN 0x10
#define _DOWN      0x20
#define _LEFT_DOWN 0x40
#define _LEFT      0x80
/* Square numbers */
typedef signed char SQUARE_NUM;
/* Square constants */
#define SQ(a,b) ((b)*8+(a))
/*=============================================================================
 * Piece Masks
 *=============================================================================*/
#define KING_MASK        0x0001
#define QUEEN_MASK       0x0002
#define LEFT_ROOK_MASK   0x0004
#define RIGHT_ROOK_MASK  0x0008
#define LEFT_BISHOP_MASK 0x0010
#define RIGHT_BISHOP_MASK 0x0020
#define LEFT_KNIGHT_MASK 0x0040
#define RIGHT_KNIGHT_MASK 0x0080
#define PAWN_MASK        0xFF00
#define ROOK_MASK    (LEFT_ROOK_MASK | RIGHT_ROOK_MASK)
#define BISHOP_MASK  (LEFT_BISHOP_MASK | RIGHT_BISHOP_MASK)
#define KNIGHT_MASK  (LEFT_KNIGHT_MASK | RIGHT_KNIGHT_MASK)

#define INPUT_BUFFER_SIZE 4096

typedef enum{
  MODE_CONSOLE, MODE_XBOARD, MODE_UCI
} INPUT_MODE;

static int xboard_mode = 0;
static INPUT_MODE input_mode = MODE_CONSOLE;
/*=============================================================================
 * Data Structures
 *=============================================================================*/
typedef signed char PIECE_COLOR;
typedef signed char PIECE_NAME;
typedef signed int MAT_EVAL;
typedef signed int POS_EVAL;

typedef struct{
  MAT_EVAL material;
  POS_EVAL position;
} EVAL;

typedef struct{
  PIECE_NAME name;
  PIECE_COLOR color;
  int cost;
} WOOD_PIECE;

typedef struct{
  SQUARE_NUM from;
  SQUARE_NUM to;
  unsigned int flags;
  MAT_EVAL profit;
} MOVE;

typedef union{
  DWRD dwrd[2];
  WORD word[4];
  BYTE byte[8];
} PIECE_MASK;

typedef struct{
  SQUARE_NUM edge[NDIR];
  PIECE_MASK attackedby;
  WORD moves;
  WOOD_PIECE wood;
  PIECE_MASK who_mask;
  DIR bound;
  SQUARE_NUM mynum;
} SQUARE_INFO;

typedef struct{
  SQUARE_NUM where;
} PIECE_IN_LIST;

typedef struct{
  SQUARE_INFO square[NSQUARE];
  PIECE_IN_LIST piece_list[TOTAL_NUM_PIECE];
  PIECE_MASK piece_mask[NPIECE + 1];
  PIECE_MASK pos_mask;
  unsigned char castle[2];
  SQUARE_NUM king_sq[2];
  unsigned long hash;
  SQUARE_NUM passage[NLINE + 1];
} NEAR_POSITION;

typedef struct{
  MOVE *first, *last;
  MOVE* current;
  MOVE* previous;
  signed char curr_n;
  MOVE best;
  signed char best_n;
  unsigned int g_status;
  MAT_EVAL material_eval;
  MAT_EVAL mat_deficit;
  PIECE_COLOR move_color;
  SQUARE_NUM en_pass;
  BYTE nslow;
  BYTE ncheck;
  BYTE neval;
  BYTE nfvcheck;
  unsigned int posflags;
} ADD_POSITION;

typedef struct{
  NEAR_POSITION n;
  ADD_POSITION a;
} POSITION;

/*=============================================================================
 * Global Variables
 *=============================================================================*/
/* Position and search */
static POSITION pos_stack[POS_STACK_SIZE];
static POSITION* pos_stk;
static POSITION* pos_sp;
static int level;
static MOVE move_stk[MOVE_STACK_SIZE];
static EVAL eval_stk[MAXDEPTH + 2];
static EVAL beta_stk[MAXDEPTH + 2];
/* Piece lists */
static PIECE_IN_LIST piece_list[TOTAL_NUM_PIECE];
static PIECE_MASK piece_masks[NPIECE + 1];
static PIECE_MASK pos_mask;
static unsigned char castle[2];
static SQUARE_NUM king_sq[2];
/* Current game state */
static PIECE_COLOR move_color;
static PIECE_COLOR our_color, enemy_color;
/* engine_color: the colour the engine is actually playing (set by white/black/go).
 * Unlike move_color, this never flips mid-search and is safe to read at any time. */
static PIECE_COLOR engine_color = WHITE_PIECE;
static int material_eval;
static int nslow, ncheck, nfvcheck, neval;
static int check_flag;
static int en_pass_square;
static unsigned long position_hash;
/* Move generation */
static MOVE* movep;
static MOVE *first_move, *last_move;
static int g_status;
static int mat_deficit;
/* Direction tables */
static int increment[NDIR] = {7,8,9,1,-7,-8,-9,-1};
static int knincrement[NDIR] = {6,15,17,10,-6,-15,-17,-10};
static DIR direction[NSQUARE][NSQUARE];
static unsigned char square_dirmask[NSQUARE];
static unsigned char square_kndirmask[NSQUARE];
static unsigned char bit_quantity[512];
static int byteshift[NDIR];
/* Evaluation tables */
static int piece_cost[NPIECE + 1] = {
  0, KING_COST, QUEEN_COST, ROOK_COST,
  BISHOP_COST, KNIGHT_COST, PAWN_COST - 40};
static WORD trans_mask[NPIECE + 1];
/*=============================================================================
 * EVALUATION CONSTANTS - CORRECTED SCALING
 *=============================================================================*/
/* One "Kaissa unit" expressed in centipawns.
 * Original Kaissa: P=2 internal scale. We use P=100 (centipawns).
 * The conversion factor is 50 (since 100/2 = 50).
 * However, the original Kaissa raw values (like 20 for rook on 7th) 
 * were already in centipawns on their internal scale.
 * To convert to our scale: raw_value * 50 / 2 = raw_value * 25.
 * But to keep values reasonable, we use a smaller multiplier. */
#define KU 1   /* Changed from 2 to 1 - no extra scaling */
/* ---- Kaissa positional weights (centipawns) ---- */
#define K_ISOLATED_PAWN        (-10 * KU)   /* per pawn                          */
#define K_ISOLATED_SEMIOPEN    (-10 * KU)   /* extra if on semi-open file        */
#define K_DOUBLED_PAWN         (-5  * KU)   /* per extra pawn on same file       */
#define K_PHALANGA             (8   * KU)   /* two pawns, same rank, adj files */
#define K_PAWN_CENTER          (15  * KU)   /* pawn on d4/e4/d5/e5 - restored to original */
#define K_PAWN_ATTACK          (1   * KU)   /* per square attacked by pawn       */
#define K_PAWN_ATTACK_CENTER   (4   * KU)   /* pawn attacking a center square */
#define K_STRONG_SQUARE        (8   * KU)   /* sq attacked by own pawn, never enemy pawn */
#define K_BACKWARD_PAWN        (-5  * KU)   /* pawn behind a strong square       */
#define K_PAWN_ATK_KING        (8   * KU)   /* pawn attacking sq adj to enemy king */
#define K_PAWN_BLOCK_FC        (-20 * KU)   /* f2/c2 or f7/c7 blocked by enemy */
#define K_PAWN_BLOCK_DE        (-20 * KU)   /* d2/e2 or d7/e7 blocked by enemy */
#define K_PASSED_PAWN_BLOCK    (-10 * KU)   /* piece blocking a passed pawn      */
#define K_PASS_TRAJ_ATK        (3   * KU)   /* attacking squares on passer's path */
#define K_PASS_BLOCK_PROFIT    (5   * KU)   /* profitable atk on passed pawn blocker */
#define K_BN_ATK_RQ            (5   * KU)   /* B/N attacks enemy R/Q             */
#define K_ROOK_7TH             (20  * KU)   /* rook on 7th (middlegame)          */
#define K_ROOK_7TH_EG          (12  * KU)   /* rook on 7th (endgame)             */
#define K_PIECE_INIT_SQ        (-8  * KU)   /* B/N still on original square      */
#define K_KNIGHT_STRONG        (10  * KU)   /* knight on a strong square */
#define K_BISHOP_STRONG        (10  * KU)   /* bishop on a strong square */
#define K_TWO_BISHOPS          30   /* bishop pair bonus — stronger in open/EG */
#define K_KNIGHT_QUEEN         (5   * KU)   /* knight and queen both on board    */
#define K_KNIGHT_CENTER        (12  * KU)   /* knight on central 4 squares - restored to original */
#define K_PIECE_ATK_BY_BISHOP  (8   * KU)   /* piece attacked by a bishop        */
#define K_BOUND_PIECE          (10  * KU)   /* enemy pinned piece                */
#define K_ROOK_BEHIND_PASSER   (15  * KU)   /* own rook behind own passed pawn   */
#define K_ROOK_BEHIND_ENEMY_P  (10  * KU)   /* own rook behind enemy passed pawn */
/* CASTLING - Moderate increases */
#define K_CASTLE_LOST          (-30 * KU)   /* castling rights permanently lost (was -50) */
#define K_CASTLE_DONE          (25  * KU)   /* actually castled (flat) (was 30) */
#define K_CASTLE_EARLY         (20  * KU)   /* extra bonus: castled in middlegame (was 35) */
#define K_CASTLE_DELAY         (-10 * KU)   /* per developed piece while uncastled (was -30) */
#define K_UNCASTLED_PENALTY    (10  * KU)   /* penalty per developed piece without castling - NEW */
#define K_KNIGHT_ON_RIM        (-20 * KU)   /* knight on edge file or rank       */
#define K_KNIGHT_NEAR_RIM      (-6  * KU)   /* knight on 2nd edge file or rank   */
#define K_KNIGHT_OUTPOST       (15  * KU)   /* knight on 5th/6th rank supported by pawn - reduced */
#define K_BISHOP_ATK_PIECE     0
#define K_QUEEN_MOB_KING       (-2  * KU)   /* per sq of virtual queen from king (danger) */
#define K_BN_ATK_NEAR_KING     (-5  * KU)   /* B/N attack near own king          */
#define K_BN_ATK_STRONG_SQ     (5   * KU)   /* B/N attack on a strong square */
#define K_MOBILITY             (1   * KU)   /* per pseudo-legal attack/move - restored to original */
/* Per-piece mobility bonuses (cp per reachable square, beyond the flat K_MOBILITY).
 * Knights plateau quickly; rooks/queens benefit from many open squares. */
#define K_MOB_KNIGHT  2   /* cp per knight attack square (8 max)  */
#define K_MOB_BISHOP  3   /* cp per bishop attack square          */
#define K_MOB_ROOK    2   /* cp per rook attack square            */
#define K_MOB_QUEEN   1   /* cp per queen attack square (many)    */
/* Late-Move Pruning move counts at depth 1/2/3 (quiet moves to search before
 * pruning the rest).  These are indices [depth 1..3]. */
static const int LMP_COUNT[4] = {0,5,11,20};
#define K_ROOK_CONNECTED   12   /* cp: two own rooks see each other on rank/file */
#define K_BISHOP_OUTPOST   12   /* cp: bishop on strong square in enemy territory */
#define K_PIECE_ATK_CENTER     (3   * KU)   /* piece attack on center square */
#define K_ROOK_OPEN            (20  * KU)   /* rook on open file                 */
#define K_ROOK_SEMIOPEN        (12  * KU)   /* rook on semi-open file            */
#define K_ROOK_ATK_OPEN        (4   * KU)   /* rook attacks along open file      */
#define K_ROOK_ATK_SEMIOPEN    (2   * KU)   /* rook attacks along semi-open file */
#define K_KING_OPP_EG          (8   * KU)   /* king opposition in endgame        */
#define K_KING_DIST_EG         (-2  * KU)   /* king dist from passed pawn (EG)   */
#define K_KING_DIST            (-50 * KU)   /* king distance from enemy king (EG)*/
#define K_KING_DIST_CENTER     (-100* KU)   /* king distance from center (EG)    */
/* DEVELOPMENT - Moderately increased, not excessive */
#define K_DEVELOPMENT_BONUS        (10 * KU)   /* per developed minor piece (was 25) */
#define K_DEVELOPMENT_ADVANTAGE    (15 * KU)   /* extra for being ahead in development - reduced */
#define K_CENTER_CONTROL           (3  * KU)   /* per attacker on center squares - reduced */
#define K_QUEEN_RETREAT_PENALTY    (-15 * KU)  /* penalty for retreating queen to back rank */
#define K_TEMPO_BONUS              (5  * KU)   /* side to move advantage - reduced */
/* "Profitable attack" signals.
 *
 * The Kaissa PC manual lists these as 300 and 10,000 in Kaissa's internal
 * scale where P=2.  Applied directly as centipawn bonuses (P=100) that would
 * be 15,000 cp and 500,000 cp — effectively infinite — which completely
 * overrides material evaluation and causes the engine to make panicky,
 * irrational moves chasing or fleeing phantom tactical patterns.
 *
 * In the original Kaissa these large values were used as near-infinite
 * forcing signals (aspiration bounds / search constraints), not as smooth
 * additive static-eval terms.  In our alpha-beta framework, where the search
 * itself resolves tactics, the eval only needs a moderate nudge:
 *
 *   K_PROFITABLE_ATK   ≈ 1 pawn  — one undefended piece can be captured
 *   K_DOUBLE_PROFIT_ATK ≈ 3 pawns — fork; typically wins a piece after SEE
 *
 * These values preserve the ordering intent (double >> single >> 0) while
 * keeping them well below the range where they distort material decisions. */
/* "Profitable attack" and fork signals */
#define K_PROFITABLE_ATK       (30)    /* gentle nudge — search finds tactics  */
#define K_DOUBLE_PROFIT_ATK    (60)    /* fork nudge   — search/SEE handles it */
/* King danger score weights (cp per attacker in the king zone).
 * Non-linear: the danger table maps total weighted attackers → penalty.
 * Weights: Q=5 R=3 B=2 N=2 (piece attack into 3×3 king zone). */
static const int KING_DANGER_WEIGHT[7] = {0,0,5,3,2,2,0};
/* Penalty table indexed by clamped weighted-attacker sum (0..19 → 20 entries) */
static const int KING_DANGER_TABLE[20] = {
  0,4,10,18,28,40,55,72,90,110,
  132,155,180,205,230,255,278,300,320,340
};
/* Space bonus: cp per safe square controlled in the central 4 ranks of enemy half */
#define K_SPACE  2
/* Contempt: engine avoids draws unless position is genuinely <= -20 cp */
#define CONTEMPT_CP  20
/* Hanging piece: penalty when a less-valuable enemy attacker targets our piece */
#define K_HANGING_PIECE  15
/* Candidate passer: pawn one advance away from becoming a passer */
#define K_CANDIDATE_PASSER  8
#define K_PAWN_SHIELD          25    /* cp per pawn shielding the castled king */
#define K_OPEN_FILE_NEAR_KING  (-35) /* cp: own-pawnless file next to king */
#define K_OPEN_FILE_ENEMY_PAWN (-25) /* cp: extra if enemy pawn on that file */
#define K_KING_EXPOSURE        (-8)  /* cp per attacker adjacent to king */
#define K_QUEEN_NEAR_KING      (-15) /* cp penalty for enemy queen near king */
#define K_ROOK_ON_7TH_ATTACK   (30)  /* cp bonus for rook on 7th attacking king */
/* Endgame detection threshold */
#define ENDGAME_MATERIAL  2600  /* ≈ rook + minor per side */
/* -----------------------------------------------------------------------
 * Tapered evaluation: blends middlegame (gp=256) and endgame (gp=0) terms.
 * TAPER(mg, eg, gp) linearly interpolates between mg and eg.
 * Safe for negative values because division is used (not right-shift).
 * ----------------------------------------------------------------------- */
#define TAPER(mg, eg, gp) (((mg) * (gp) + (eg) * (256 - (gp))) / 256)
/* Futility pruning margins (depth 0 unused, 1 = one-ply, 2 = two-ply, 3 = three-ply) */
static const int FUTILITY_MARGIN[4] = {0,120,280,500};
/* Delta pruning */
#define DELTA_MARGIN 300
/* Aspiration windows — 50 cp starting window reduces expensive re-searches */
#define ASP_WINDOW_INIT   50
#define ASP_WINDOW_WIDE  200
/*=============================================================================
 * Bitboard Representation
 *=============================================================================*/
/* Board occupancy bitboards (maintained alongside mailbox) */
static U64 bb_piece[NPIECE + 1]; /* [piece_name] : all squares of that piece type */
static U64 bb_side[2]; /* [color]      : all squares occupied by color   */
/* Pre-computed attack tables */
static U64 bb_atk_knight[64];
static U64 bb_atk_king[64];
static U64 bb_atk_pawn[2][64]; /* [attacking_color][sq]: squares attacked by pawn */
static U64 bb_ray[64][8]; /* [sq][dir]: full ray (all squares in direction)   */
/* Transposition Table */
#define TT_SIZE   (1 << 20)   /* 1048576 entries (~24 MB) — 8× larger for better hit rate */
#define TT_MASK   (TT_SIZE - 1)
#define TT_NONE   0
#define TT_EXACT  1
#define TT_ALPHA  2  /* upper bound */
#define TT_BETA   3  /* lower bound */

typedef struct{
  U64 hash;
  short score;
  short depth;
  BYTE flag;
  MOVE best_move;
} TT_ENTRY;

static TT_ENTRY tt[TT_SIZE];
/* -----------------------------------------------------------------------
 * Pawn Hash Table — caches the expensive pawn-structure evaluation so
 * that positions that differ only in piece placement but share the same
 * pawn skeleton reuse the result.  A modest 16 K entry table is enough;
 * pawn structures repeat far more often than full positions.
 * ----------------------------------------------------------------------- */
#define PHT_SIZE  (1 << 14)   /* 16384 entries */
#define PHT_MASK  (PHT_SIZE - 1)

typedef struct{
  U64 pawn_hash; /* Zobrist hash of pawn skeleton only */
  int score; /* pawn structure score (White's perspective) */
  U64 w_passed; /* bitboard of White passed pawns */
  U64 b_passed; /* bitboard of Black passed pawns */
  U64 w_strong; /* strong squares for White */
  U64 b_strong; /* strong squares for Black */
} PHT_ENTRY;

static PHT_ENTRY pht[PHT_SIZE];

static void pht_clear(void){
  memset(pht,0,sizeof(pht));
}

/* Killer Moves & History Heuristic */
#define MAX_KILLERS 2
static MOVE killers[MAXDEPTH + 4][MAX_KILLERS];
static int history[64][64]; /* [from][to] — bonus for quiet beta-cutoff moves  */
static int history_malus[64][64]; /* negative scores for quiet moves that failed */
/* Countermove heuristic: for a given (from,to) last move, store the quiet move
 * that most recently caused a beta-cutoff in response. */
static MOVE countermove[64][64]; /* [last_from][last_to] → best response */
/* Principal Variation table */
#define MAX_PV_LEN   64 // (MAXDEPTH + 2)
static MOVE pv_table[MAX_PV_LEN][MAX_PV_LEN];
static int pv_length[MAX_PV_LEN];
/* Position stack for takeback.
 * Must hold the full game history PLUS the deepest search path.
 * Worst case: ~300 game half-moves + MAXDEPTH(18) + check extensions(~10)
 * + null-move saves(~6) + SEE was fixed (no longer adds to stack).
 * 512 gives ample headroom; the old value of 128 overflowed after ~105 moves. */
#define MAX_STACK 512

typedef struct{
  SQUARE_INFO squares[NSQUARE];
  PIECE_IN_LIST piece_list[TOTAL_NUM_PIECE];
  PIECE_MASK piece_mask[NPIECE + 1];
  PIECE_MASK pos_mask;
  unsigned char castle[2];
  SQUARE_NUM king_sq[2];
  PIECE_COLOR move_color;
  int material_eval;
  SQUARE_NUM en_pass_square;
  unsigned long position_hash;
  U64 zobrist_hash;
  U64 sv_bb_piece[NPIECE + 1];
  U64 sv_bb_side[2];
  int halfmove_clock; /* 50-move rule counter (resets on pawn move or capture) */
} SAVED_POSITION;

static SAVED_POSITION pos_stack_simple[MAX_STACK];
static int stack_ptr = 0;
/* WinBoard communication */
static int post_mode = 1;
static int time_control = 0;
static int wtime = 0, btime = 0;
static int winc = 0, binc = 0;
static int movestogo = 0;
static int search_depth = MAXDEPTH;
static int thinking = 0;
static int analyze_mode = 0;
static int force_mode = 0;
/* Fifty-move rule counter (half-moves since last pawn move or capture) */
static int halfmove_clock = 0;
/* Search control */
static int stop_search = 0;
static clock_t search_start_time = 0;
static int search_nodes = 0;
static int search_time_limit_ms = 5000;
/* Permanent brain (pondering) state */
static int ponder_mode = 0;
static int is_pondering = 0;
static int ponder_hit = 0;
static MOVE ponder_move;
static char ponder_buf[512];
static int ponder_buf_ready = 0;
/* Zobrist Hashing */
static U64 zob_piece[64][8][2];
static U64 zob_side;
static U64 zob_ep[8];
static U64 zob_castle[16];
static int zob_initialized = 0;
/* Pawn-only Zobrist keys (subset of zob_piece, just for PAWN entries).
 * Computed from the same seed so they're consistent. */
#define ZOB_PAWN(sq, color) zob_piece[(sq)][PAWN][(color)]
/* SEE values */
static int see_val[7] = {
  0, KING_COST, QUEEN_COST, ROOK_COST,
  BISHOP_COST, KNIGHT_COST, PAWN_COST};
/*=============================================================================
 * Function Declarations
 *=============================================================================*/
static void init_engine(void);
static void init_position(void);
static void set_start_position(void);
static int make_move(MOVE* move);
static void take_back_move(void);
static int is_legal_move(MOVE* move);
static void generate_moves(MOVE** first, MOVE** last);
static void generate_moves_for_piece(SQUARE_NUM from, MOVE** mp);
static int search_best_move(MOVE* best_move);
static int static_evaluate(void); /* Kaissa "static evaluation function" */
unsigned long calculate_hash(void);
static void save_position(void);
static void restore_position(void);
static int fv_search(int alpha, int beta);
static int alphabeta(
  int depth, int alpha, int beta, int* best_move_index,
  MOVE* moves, int num_moves);
/* FV0: Kaissa's mandatory first-pass — order root moves by their FV (quiescence) score */
static void fv0_sort(MOVE* moves, int num_moves);
/* FV1: Kaissa's second pass — after FV0, sort opponent replies to the best root move */
static void fv1_sort(MOVE* best_root_move, MOVE* opp_moves, int* num_opp_moves);
static int in_check(void);
static int see(SQUARE_NUM from_sq, SQUARE_NUM to_sq);
static int piece_attacks_sq(
  SQUARE_NUM from, SQUARE_NUM to,
  PIECE_NAME name, PIECE_COLOR color);
static void play_move(void);
static void start_pondering(void);
static int pick_ponder_move(MOVE* out);
static void send_move_to_gui(MOVE* move);
static int is_endgame(void);
static int king_distance(SQUARE_NUM a, SQUARE_NUM b);
static int evaluate_king_safety(PIECE_COLOR color);
static void compute_strong_squares(PIECE_COLOR color, U64* out);
static int bb_attacks_for_piece(SQUARE_NUM sq);
static int is_piece_pinned(SQUARE_NUM sq);
static int centre_manhattan(SQUARE_NUM sq);
static int kp_vs_k_bonus(void);
static int is_center_square(SQUARE_NUM sq);
static WOOD_PIECE* who_on_square(SQUARE_NUM sq);
/* Portable timer functions */
static void timer_init_portable(void);
static void timer_start_portable(void);
static int timer_elapsed_ms_portable(void);
/* Console-mode interface */
static void print_board(void);
static void print_console_help(void);
static void process_console_command(char* cmd);
static int set_from_fen(const char* fen);
/* UCI interface */
static void process_uci_command(char* cmd);
static void parse_uci_position(char* cmd);
static void uci_new_game(void);
static void parse_uci_go(char* cmd);
static void parse_uci_setoption(char* cmd);
static void print_uci_score(int score, int side_to_move);
// Perft
static unsigned long long perft(int depth);
static void perft_root(int depth);
/*=============================================================================
 * Bit Utilities
 *=============================================================================*/
static int bb_lsb(U64 b){
  #if defined(__GNUC__)
  return __builtin_ctzll(b);
  #elif defined(_MSC_VER)
  unsigned long idx;
  _BitScanForward64(&idx,b);
  return (int)idx;
  #else
  int n = 0;
  if (! (b & 0xFFFFFFFFULL)){
    n += 32;
    b >>= 32;
  }
  if (! (b & 0xFFFFULL)){
    n += 16;
    b >>= 16;
  }
  if (! (b & 0xFFULL)){
    n += 8;
    b >>= 8;
  }
  if (! (b & 0xFULL)){
    n += 4;
    b >>= 4;
  }
  if (! (b & 0x3ULL)){
    n += 2;
    b >>= 2;
  }
  if (! (b & 0x1ULL)){
    n += 1;
  }
  return n;
  #endif
}

static inline int bb_msb(U64 b){
  #if defined(__GNUC__)
  return 63 - __builtin_clzll(b);
  #elif defined(_MSC_VER)
  unsigned long idx;
  _BitScanReverse64(&idx,b);
  return (int)idx;
  #else
  int n = 63;
  if (! (b & 0xFFFFFFFF00000000ULL)){
    n -= 32;
    b <<= 32;
  }
  if (! (b & 0xFFFF000000000000ULL)){
    n -= 16;
    b <<= 16;
  }
  if (! (b & 0xFF00000000000000ULL)){
    n -= 8;
    b <<= 8;
  }
  if (! (b & 0xF000000000000000ULL)){
    n -= 4;
    b <<= 4;
  }
  if (! (b & 0xC000000000000000ULL)){
    n -= 2;
    b <<= 2;
  }
  if (! (b & 0x8000000000000000ULL)){
    n -= 1;
  }
  return n;
  #endif
}

static inline int bb_popcount(U64 b){
  #if defined(__GNUC__)
  return __builtin_popcountll(b);
  #else
  b -= (b >> 1) & 0x5555555555555555ULL;
  b = (b & 0x3333333333333333ULL) + ((b >> 2) & 0x3333333333333333ULL);
  b = (b + (b >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return (int)((b * 0x0101010101010101ULL) >> 56);
  #endif
}

/*=============================================================================
 * Sliding Piece Attacks
 *=============================================================================*/
static U64 bb_sliding_attacks(int sq, int dir, U64 occ){
  U64 r = bb_ray[sq][dir];
  U64 b = r & occ;
  if (! b) return r;
  int blocker = (dir == 0 || dir == 1 || dir == 2 || dir == 7)
    ?bb_lsb(b):bb_msb(b);
  return r ^ bb_ray[blocker][dir];
}

static U64 bb_rook_attacks(int sq, U64 occ){
  return bb_sliding_attacks(sq,0,occ) | bb_sliding_attacks(sq,2,occ)
    | bb_sliding_attacks(sq,4,occ) | bb_sliding_attacks(sq,6,occ);
}

static U64 bb_bishop_attacks(int sq, U64 occ){
  return bb_sliding_attacks(sq,1,occ) | bb_sliding_attacks(sq,3,occ)
    | bb_sliding_attacks(sq,5,occ) | bb_sliding_attacks(sq,7,occ);
}

/*=============================================================================
 * Attack Table Initialisation
 *=============================================================================*/
static void init_bb_attack_tables(void){
  static const int ray_delta[8] = {8,9,1,-7,-8,-9,-1,7};
  int sq, dir, s;

  for (sq = 0; sq < 64; sq++){
    for (dir = 0; dir < 8; dir++){
      bb_ray[sq][dir] = 0ULL;
      s = sq;
      while (1){
        int f0 = s % 8;
        int s2 = s + ray_delta[dir];
        if (s2 < 0 || s2 >= 64) break;
        int f2 = s2 % 8;
        if (abs(f2 - f0) > 1) break;
        bb_ray[sq][dir] |= (1ULL << s2);
        s = s2;
      }
    }
  }

  static const int kn_delta[8] = {15,17,10,-6,-15,-17,-10,6};
  for (sq = 0; sq < 64; sq++){
    bb_atk_knight[sq] = 0ULL;
    int f = sq % 8, r = sq / 8;
    for (dir = 0; dir < 8; dir++){
      int to = sq + kn_delta[dir];
      if (to < 0 || to >= 64) continue;
      int tf = to % 8, tr = to / 8;
      if (abs(tf - f) > 2 || abs(tr - r) > 2) continue;
      if (abs(tf - f) + abs(tr - r) != 3) continue;
      bb_atk_knight[sq] |= (1ULL << to);
    }
  }

  for (sq = 0; sq < 64; sq++){
    bb_atk_king[sq] = 0ULL;
    int f = sq % 8;
    for (dir = 0; dir < 8; dir++){
      int to = sq + ray_delta[dir];
      if (to < 0 || to >= 64) continue;
      if (abs((to % 8) - f) > 1) continue;
      bb_atk_king[sq] |= (1ULL << to);
    }
  }

  for (sq = 0; sq < 64; sq++){
    int f = sq % 8;
    bb_atk_pawn[WHITE_PIECE][sq] = 0ULL;
    bb_atk_pawn[BLACK_PIECE][sq] = 0ULL;
    if (f > 0 && sq + 7 < 64) bb_atk_pawn[WHITE_PIECE][sq] |= (1ULL << (sq + 7));
    if (f < 7 && sq + 9 < 64) bb_atk_pawn[WHITE_PIECE][sq] |= (1ULL << (sq + 9));
    if (f > 0 && sq - 9 >= 0) bb_atk_pawn[BLACK_PIECE][sq] |= (1ULL << (sq - 9));
    if (f < 7 && sq - 7 >= 0) bb_atk_pawn[BLACK_PIECE][sq] |= (1ULL << (sq - 7));
  }
}

/*=============================================================================
 * Non-blocking stdin input check
 *=============================================================================*/
static int input_available(void){
  #ifdef _WIN32
  /* _kbhit() only detects keyboard input — it returns 0 for piped stdin,
   * which is exactly how WinBoard/Arena communicates with the engine.
   * Use PeekNamedPipe for pipes and fall back to _kbhit for a real console. */
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD type = GetFileType(h);
  if (type == FILE_TYPE_PIPE){
    DWORD avail = 0;
    if (PeekNamedPipe(h, NULL,0, NULL,&avail, NULL))
      return avail > 0;
    return 0; /* pipe error — treat as no input */
  }
  /* Console (interactive use) */
  return _kbhit();
  #else
  struct timeval tv = {0,0};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO,&fds);
  return select(STDIN_FILENO + 1,&fds, NULL, NULL,&tv) > 0;
  #endif
}

static int read_line_nb(char* buf, int maxlen){
  if (fgets(buf,maxlen, stdin) == NULL) return 0;
  buf[strcspn(buf,"\r\n")] = '\0';
  return 1;
}

/*=============================================================================
 * Zobrist Hashing
 *=============================================================================*/
static U64 zob_rand(U64* state){
  *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
  return *state ^ (*state >> 33);
}

static void init_zobrist(void){
  U64 state = 0xDEADBEEFCAFEBABEULL;
  int sq, p, c, i;
  for (sq = 0; sq < 64; sq++)
    for (p = 0; p < 8; p++)
      for (c = 0; c < 2; c++)
        zob_piece[sq][p][c] = zob_rand(&state);
  zob_side = zob_rand(&state);
  for (i = 0; i < 8; i++) zob_ep[i] = zob_rand(&state);
  for (i = 0; i < 16; i++) zob_castle[i] = zob_rand(&state);
  zob_initialized = 1;
}

static U64 compute_zobrist_hash(void){
  U64 h = 0;
  int sq;
  for (sq = 0; sq < 64; sq++){
    WOOD_PIECE* w = who_on_square(sq);
    if (w->name != NOPIECE)
      h ^= zob_piece[sq][w->name][w->color];
  }
  if (move_color == BLACK_PIECE) h ^= zob_side;
  if (en_pass_square != DUMMY) h ^= zob_ep[en_pass_square % 8];
  h ^= zob_castle[(castle[0] & 0xF) | ((castle[1] & 0xF) << 4)];
  return h;
}

/* Pawn-skeleton hash: only pawns contribute.  Used to probe the pawn hash table. */
static U64 compute_pawn_hash(void){
  U64 h = 0;
  U64 pawns = bb_piece[PAWN];
  while (pawns){
    int sq = bb_lsb(pawns);
    pawns &= pawns - 1;
    PIECE_COLOR c = (bb_side[WHITE_PIECE] >> sq) & 1?WHITE_PIECE:BLACK_PIECE;
    h ^= ZOB_PAWN(sq,c);
  }
  return h;
}

/*=============================================================================
 * Transposition Table
 *=============================================================================*/
static void tt_clear(void){
  memset(tt,0,sizeof(tt));
  pht_clear();
}

static void tt_store(U64 hash, int depth, int score, int flag, MOVE* best){
  TT_ENTRY* e = &tt[hash & TT_MASK];
  e->hash = hash;
  e->score = (short)score;
  e->depth = (short)depth;
  e->flag = (BYTE)flag;
  if (best) e->best_move = *best;
  else e->best_move.from = DUMMY;
}

static int tt_probe(
  U64 hash, int depth, int alpha, int beta,
  int* score_out, MOVE* best_out){
  TT_ENTRY* e = &tt[hash & TT_MASK];
  if (e->hash != hash) return 0;
  if (best_out && e->best_move.from != DUMMY)
    *best_out = e->best_move;
  if (e->depth >= depth){
    int s = e->score;
    if (e->flag == TT_EXACT){
      *score_out = s;
      return 1;
    }
    if (e->flag == TT_ALPHA && s <= alpha){
      *score_out = alpha;
      return 1;
    }
    if (e->flag == TT_BETA && s >= beta){
      *score_out = beta;
      return 1;
    }
  }
  return 0;
}

/*=============================================================================
 * Killer Moves & History Heuristic
 *=============================================================================*/
static void clear_killers_history(void){
  memset(killers,0,sizeof(killers));
  memset(history,0,sizeof(history));
  memset(history_malus,0,sizeof(history_malus));
  memset(countermove,0,sizeof(countermove));
}

static void store_killer(int ply, MOVE* m){
  if (m->flags & M_CAPTURE) return;
  if (killers[ply][0].from == m->from && killers[ply][0].to == m->to) return;
  killers[ply][1] = killers[ply][0];
  killers[ply][0] = *m;
}

static int is_killer(int ply, MOVE* m){
  int k;
  for (k = 0; k < MAX_KILLERS; k++)
    if (killers[ply][k].from == m->from && killers[ply][k].to == m->to)
      return 1;
  return 0;
}

/*=============================================================================
 * Utility Functions
 *=============================================================================*/
static int my_tolower(int c){
  return (c >= 'A' && c <= 'Z')?c + ('a' - 'A'):c;
}

static int char_to_piece(char c){
  switch (my_tolower(c)){
  case 'k': return KING;
  case 'q': return QUEEN;
  case 'r': return ROOK;
  case 'b': return BISHOP;
  case 'n': return KNIGHT;
  case 'p': return PAWN;
  default: return NOPIECE;
  }
}

static int square_from_coords(int file, int rank){
  if (file >= 0 && file < 8 && rank >= 0 && rank < 8)
    return rank * 8 + file;
  return DUMMY;
}

static void parse_square(const char* str, SQUARE_NUM* sq){
  if (str[0] >= 'a' && str[0] <= 'h' && str[1] >= '1' && str[1] <= '8')
    *sq = (str[1] - '1') * 8 + (str[0] - 'a');
  else
    *sq = DUMMY;
}

static void square_to_string(SQUARE_NUM sq, char* str){
  if (sq >= 0 && sq < 64){
    str[0] = 'a' + (sq % 8);
    str[1] = '1' + (sq / 8);
    str[2] = '\0';
  } else{
    str[0] = '\0';
  }
}

static void move_to_string(MOVE* move, char* str){
  char from[3], to[3];
  square_to_string(move->from,from);
  square_to_string(move->to,to);
  sprintf(str,"%s%s",from,to);
  if (move->flags & M_PROMOTION)
    strcat(str,"q");
}

static void move_to_string_display(MOVE* move, char* str){
  if (move->flags & M_CASTLE_RIGHT){
    strcpy(str,"O-O");
  } else if (move->flags & M_CASTLE_LEFT){
    strcpy(str,"O-O-O");
  } else{
    move_to_string(move,str);
  }
}

static int parse_move(const char* str, MOVE* move){
  SQUARE_NUM from, to;
  int len = (int)strlen(str);

  if (len >= 4){
    parse_square(str,&from);
    parse_square(str + 2,&to);

    if (from != DUMMY && to != DUMMY){
      move->from = from;
      move->to = to;
      move->flags = 0;
      move->profit = 0;

      if (len >= 5 && my_tolower(str[4]) == 'q')
        move->flags |= M_PROMOTION;

      /* Detect en passant: pawn moves diagonally to the ep square.
       * make_move() also auto-detects this, but setting the flag here
       * means any code that inspects move->flags before make_move()
       * (e.g. printing, filtering) sees the correct type. */
      if (en_pass_square != DUMMY &&
        to == en_pass_square &&
        abs((to % 8) - (from % 8)) == 1 &&
        who_on_square(from)->name == PAWN &&
        who_on_square(from)->color == move_color &&
        who_on_square(to)->name == NOPIECE){
        move->flags |= M_EN_PASSANT | M_CAPTURE;
      }

      return 1;
    }
  }
  return 0;
}

/*=============================================================================
 * Console Mode — Board Display and Command Processing
 *
 * These functions implement the Kaissa 1970s-style teletype interface:
 *   - Long algebraic notation  (e2e4  or  e2-e4)
 *   - ASCII board printed after every half-move
 *   - NEW, GO, SWITCH, UNDO, TIME, DEPTH, SHOW, POST/NOPOST, QUIT
 *=============================================================================*/
/* Pretty-print the current board position to stdout. */
static void print_board(void){
  /* Upper-case = White, lower-case = Black  (classic teletype convention) */
  static const char piece_char[2][7] = {
    {'.','K','Q','R','B','N','P'}, /* WHITE */
    {'.','k','q','r','b','n','p'} /* BLACK */
  };
  int rank, file, sq;

  printf("\n");
  printf("     a   b   c   d   e   f   g   h\n");
  printf("   +---+---+---+---+---+---+---+---+\n");
  for (rank = 7; rank >= 0; rank--){
    printf(" %d |",rank + 1);
    for (file = 0; file < 8; file++){
      sq = rank * 8 + file;
      WOOD_PIECE* w = who_on_square(sq);
      if (w->name == NOPIECE)
        printf(" . |");
      else
        printf(" %c |",piece_char[w->color][w->name]);
    }
    printf(" %d\n",rank + 1);
    printf("   +---+---+---+---+---+---+---+---+\n");
  }
  printf("     a   b   c   d   e   f   g   h\n\n");
  printf("  %s to move",move_color == WHITE_PIECE?"White":"Black");
  if (in_check())
    printf("  *** CHECK ***");
  printf("\n\n");
  fflush(stdout);
}

/* One-line move history entry stored for display (12 bytes covers any move string) */
#define CONSOLE_HIST_MAX 512
static char console_history[CONSOLE_HIST_MAX][12];
static int console_hist_ptr = 0;
/* Strip hyphens so "e2-e4" parses identically to "e2e4". */
static int parse_move_console(const char* str, MOVE* move){
  char clean[8];
  int i = 0;
  const char* p = str;
  while (*p && i < 7){
    if (*p != '-') clean[i++] = *p;
    p++;
  }
  clean[i] = '\0';
  return parse_move(clean,move);
}

static void print_console_help(void){
  printf("\n");
  printf("  +-------------------------------------------------+\n");
  printf("  |          KAISSA  --  CONSOLE COMMANDS           |\n");
  printf("  +-------------------------------------------------+\n");
  printf("  |  <move>          Enter your move                |\n");
  printf("  |                  Format: e2e4  or  e2-e4        |\n");
  printf("  |                  Promotion: e7e8q               |\n");
  printf("  |  new             Start a new game               |\n");
  printf("  |  go              Force Kaissa to move now       |\n");
  printf("  |  switch  (sw)    Switch sides with Kaissa       |\n");
  printf("  |  undo    (u)     Take back the last move        |\n");
  printf("  |  show    (d)     Redisplay the board            |\n");
  printf("  |  time  <n>       Set think time (seconds/move)  |\n");
  printf("  |  depth <n>       Set search depth (plies)       |\n");
  printf("  |  post            Show Kaissa's thinking         |\n");
  printf("  |  nopost          Hide Kaissa's thinking         |\n");
  printf("  |  fen             Display current FEN string     |\n");
  printf("  |  setboard <fen>  Load position from FEN         |\n");
  printf("  |  xboard          Enter WinBoard protocol mode   |\n");
  printf("  |  quit            Exit Kaissa                    |\n");
  printf("  +-------------------------------------------------+\n");
  printf("\n");
  fflush(stdout);
}

/*
 * process_console_command — Kaissa 1970s teletype command dispatcher.
 *
 * Called from main() whenever xboard_mode == 0.  Accepts moves in long
 * algebraic notation (with or without hyphen) plus the management commands
 * listed in print_console_help().
 */
static void process_console_command(char* cmd){
  MOVE move;

  /* Trim leading whitespace */
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  /* Trim trailing whitespace */
  int len = (int)strlen(cmd);
  while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t' ||
    cmd[len - 1] == '\r' || cmd[len - 1] == '\n'))
    cmd[--len] = '\0';

  if (len == 0) return;

  /* ------------------------------------------------------------------ */
  /* NEW  — reset to starting position                                    */
  /* ------------------------------------------------------------------ */
  if (strcmp(cmd,"new") == 0){
    init_position();
    set_start_position();
    force_mode = 0;
    thinking = 0;
    stop_search = 1;
    move_color = WHITE_PIECE;
    engine_color = BLACK_PIECE; /* human plays White by default */
    analyze_mode = 0;
    stack_ptr = 0;
    console_hist_ptr = 0;
    ponder_move.from = DUMMY;
    ponder_buf_ready = 0;
    halfmove_clock = 0;
    tt_clear();
    clear_killers_history();
    printf("\n  New game.  You play White, Kaissa plays Black.\n");
    print_board();
  }

  /* ------------------------------------------------------------------ */
  /* SWITCH / SW  — swap which side the human and engine play            */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"switch") == 0 || strcmp(cmd,"sw") == 0){
    engine_color = 1 - engine_color;
    printf("  Sides switched. Kaissa now plays %s.\n",
      engine_color == WHITE_PIECE?"White":"Black");
    /* If it is now the engine's turn, let it move immediately */
    if (move_color == engine_color && ! force_mode){
      printf("  Kaissa is thinking...\n");
      fflush(stdout);
      play_move();
      print_board();
    }
  }

  /* ------------------------------------------------------------------ */
  /* GO  — engine plays whichever side is to move right now              */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"go") == 0){
    engine_color = move_color;
    force_mode = 0;
    analyze_mode = 0;
    printf("  Kaissa is thinking...\n");
    fflush(stdout);
    play_move();
    print_board();
  }

  /* ------------------------------------------------------------------ */
  /* UNDO / U / TAKEBACK  — retract last half-move pair                  */
  /*                                                                      */
  /* If it is currently the human's turn (engine just replied), we take  */
  /* back two half-moves so the human returns to the position before     */
  /* their last move.  If it is the engine's turn (human just moved but  */
  /* engine has not responded yet), we take back one half-move.          */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"undo") == 0 ||
    strcmp(cmd,"u") == 0 ||
    strcmp(cmd,"takeback") == 0){
    int to_undo = 0;
    /* It is the human's turn — engine already replied: undo both */
    if (move_color != engine_color && stack_ptr >= 2)
      to_undo = 2;
    else if (stack_ptr >= 1)
      to_undo = 1;

    if (to_undo == 0){
      printf("  No moves to take back.\n");
    } else{
      int i;
      for (i = 0; i < to_undo; i++){
        take_back_move();
        if (console_hist_ptr > 0) console_hist_ptr--;
      }
      printf("  %s move(s) taken back.\n",to_undo == 2?"Two":"One");
      print_board();
    }
  }

  /* ------------------------------------------------------------------ */
  /* TIME <n>  — set per-move time limit in whole seconds                */
  /* ------------------------------------------------------------------ */
  else if (strncmp(cmd,"time",4) == 0 &&
    (cmd[4] == ' ' || cmd[4] == '\0')){
    int secs = 5;
    sscanf(cmd + 4,"%d",&secs);
    if (secs < 1) secs = 1;
    search_time_limit_ms = secs * 1000;
    time_control = 1;
    movestogo = 0;
    wtime = 0;
    btime = 0;
    printf("  Time limit: %d second(s) per move.\n",secs);
  }

  /* ------------------------------------------------------------------ */
  /* DEPTH <n>  — set search depth in plies                              */
  /* ------------------------------------------------------------------ */
  else if (strncmp(cmd,"depth",5) == 0 &&
    (cmd[5] == ' ' || cmd[5] == '\0')){
    int d = 8;
    sscanf(cmd + 5,"%d",&d);
    if (d < 1) d = 1;
    if (d > MAXDEPTH) d = MAXDEPTH;
    search_depth = d;
    time_control = 0; /* depth mode overrides time mode */
    printf("  Search depth: %d ply.\n",d);
  }

  /* ------------------------------------------------------------------ */
  /* SHOW / DISPLAY / D  — redraw the board                              */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"show") == 0 ||
    strcmp(cmd,"display") == 0 ||
    strcmp(cmd,"d") == 0){
    print_board();
  }

  /* ------------------------------------------------------------------ */
  /* POST / NOPOST  — toggle search output                               */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"post") == 0){
    post_mode = 1;
    printf("  Thinking output ON.\n");
  } else if (strcmp(cmd,"nopost") == 0){
    post_mode = 0;
    printf("  Thinking output OFF.\n");
  }

  /* ------------------------------------------------------------------ */
  /* HELP / ?                                                             */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"help") == 0 || strcmp(cmd,"h") == 0){
    print_console_help();
  }

  /* ------------------------------------------------------------------ */
  /* FEN  — display the current position as a FEN string                  */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"fen") == 0){
    /* Build piece-placement field */
    char fen_buf[128];
    int fi = 0;
    int r, f;
    for (r = 7; r >= 0; r--){
      int empty = 0;
      for (f = 0; f < 8; f++){
        int sq = r * 8 + f;
        WOOD_PIECE* w = who_on_square(sq);
        if (w->name == NOPIECE){
          empty++;
        } else{
          if (empty){
            fen_buf[fi++] = '0' + empty;
            empty = 0;
          }
          static const char pnames[] = ".kqrbnp";
          char c = pnames[(int)w->name];
          if (w->color == WHITE_PIECE) c = (char)toupper((unsigned char)c);
          fen_buf[fi++] = c;
        }
      }
      if (empty){
        fen_buf[fi++] = '0' + empty;
      }
      if (r > 0) fen_buf[fi++] = '/';
    }
    fen_buf[fi] = '\0';

    /* Castling */
    char cast_buf[8];
    int ci = 0;
    if (castle[WHITE_PIECE] & CASTLERIGHT) cast_buf[ci++] = 'K';
    if (castle[WHITE_PIECE] & CASTLELEFT) cast_buf[ci++] = 'Q';
    if (castle[BLACK_PIECE] & CASTLERIGHT) cast_buf[ci++] = 'k';
    if (castle[BLACK_PIECE] & CASTLELEFT) cast_buf[ci++] = 'q';
    if (! ci) cast_buf[ci++] = '-';
    cast_buf[ci] = '\0';

    /* En passant */
    char ep_buf[4] = "-";
    if (en_pass_square != DUMMY){
      ep_buf[0] = 'a' + (en_pass_square % 8);
      ep_buf[1] = '1' + (en_pass_square / 8);
      ep_buf[2] = '\0';
    }

    printf("  FEN: %s %c %s %s %d 1\n",
      fen_buf,
      move_color == WHITE_PIECE?'w':'b',
      cast_buf,ep_buf,halfmove_clock);
    fflush(stdout);
  }

  /* ------------------------------------------------------------------ */
  /* SETBOARD  — load a position from FEN string                          */
  /* ------------------------------------------------------------------ */
  else if (strncmp(cmd,"setboard",8) == 0 || strncmp(cmd,"fen ",4) == 0){
    const char* fen = cmd + (strncmp(cmd,"setboard",8) == 0?8:4);
    while (*fen == ' ') fen++;
    if (! set_from_fen(fen)){
      printf("  Invalid FEN — board unchanged.\n");
    } else{
      tt_clear();
      clear_killers_history();
      stack_ptr = 0;
      printf("  Position set.\n");
      print_board();
    }
    fflush(stdout);
  }
  else if (strncmp(cmd, "perft", 5) == 0) {

    int depth=1;

    sscanf(cmd + 5, "%d", &depth);

    if (depth < 1)
      depth=1;

    if (depth > 10)
      depth=10;

    perft_root(depth);
    }

  /* ------------------------------------------------------------------ */
  /* QUIT / EXIT / BYE                                                   */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"quit") == 0 ||
    strcmp(cmd,"exit") == 0 ||
    strcmp(cmd,"bye") == 0){
    printf("  Goodbye.\n");
    exit(0);
  }

  /* ------------------------------------------------------------------ */
  /* XBOARD  — hand off to the WinBoard/XBoard protocol                  */
  /* ------------------------------------------------------------------ */
  else if (strcmp(cmd,"xboard") == 0){
    xboard_mode = 1;
    printf("\n");
    fflush(stdout);
  }

  /* ------------------------------------------------------------------ */
  /* MOVE  — long algebraic notation, with or without hyphen             */
  /* ------------------------------------------------------------------ */
  else{
    if (! parse_move_console(cmd,&move)){
      printf("  Unknown command: '%s'  (type 'help' for commands)\n",cmd);
      return;
    }

    if (! is_legal_move(&move)){
      printf("  Illegal move: %s\n",cmd);
      return;
    }

    if (make_move(&move) != 0){
      printf("  Illegal move: %s\n",cmd);
      return;
    }

    /* Record in history and echo */
    char mv_display[10];
    move_to_string_display(&move,mv_display);
    if (console_hist_ptr < CONSOLE_HIST_MAX)
      strncpy(console_history[console_hist_ptr++],mv_display,sizeof(console_history[0]) - 1);

    int full_move = (stack_ptr + 1) / 2;
    if (move_color == WHITE_PIECE) /* move_color has already flipped */
      printf("  %d. ... %s\n",full_move,mv_display);
    else
      printf("  %d. %s\n",full_move,mv_display);

    print_board();

    /* Let the engine reply if it is now its turn */
    if (! force_mode && ! analyze_mode && move_color == engine_color){
      printf("  Kaissa is thinking...\n");
      fflush(stdout);
      play_move();
      print_board();
    }
  }
}

/*=============================================================================
 * Board Initialization
 *=============================================================================*/
static void init_trans_mask(void){
  trans_mask[0] = 0;
  trans_mask[KING] = KING_MASK;
  trans_mask[QUEEN] = QUEEN_MASK;
  trans_mask[ROOK] = ROOK_MASK;
  trans_mask[BISHOP] = BISHOP_MASK;
  trans_mask[KNIGHT] = KNIGHT_MASK;
  trans_mask[PAWN] = PAWN_MASK;
}

static void init_bit_quantity(void){
  int i, j;
  for (i = 0; i < 512; i++){
    bit_quantity[i] = 0;
    for (j = 0; j < 8; j++)
      if (i & (1 << j))
        bit_quantity[i]++;
  }
}

static void init_direction_masks(void){
  int sq, file, line, x, y, dir;

  for (sq = 0; sq < NSQUARE; sq++){
    file = sq % 8;
    line = sq / 8;

    square_dirmask[sq] = 0;
    square_kndirmask[sq] = 0;

    for (dir = 0; dir < NDIR; dir++){
      x = file + ((dir == 0 || dir == 6)?-1:(dir == 2 || dir == 4)?1:0);
      y = line + ((dir == 0 || dir == 1 || dir == 2)?1:(dir == 4 || dir == 5 || dir == 6)?-1:0);
      if (x >= 0 && x < 8 && y >= 0 && y < 8)
        square_dirmask[sq] |= (1 << dir);

      x = file + ((dir == 0)?-2:(dir == 1)?-1:(dir == 2)?1:(dir == 3)?2:
        (dir == 4)?2:(dir == 5)?1:(dir == 6)?-1:-2);
      y = line + ((dir == 0 || dir == 1)?2:(dir == 2 || dir == 3)?1:
        (dir == 4 || dir == 5)?-1:(dir == 6 || dir == 7)?-2:0);
      if (x >= 0 && x < 8 && y >= 0 && y < 8)
        square_kndirmask[sq] |= (1 << dir);
    }
  }
}

static void init_directions(void){
  int src, dest, sfile, sline, dfile, dline, dx, dy;

  for (src = 0; src < NSQUARE; src++){
    sfile = src % 8;
    sline = src / 8;
    for (dest = 0; dest < NSQUARE; dest++){
      if (src == dest){
        direction[src][dest] = NODIR;
        continue;
      }
      dfile = dest % 8;
      dline = dest / 8;
      dx = dfile - sfile;
      dy = dline - sline;

      if (dx == 0){
        direction[src][dest] = (dy > 0)?_UP:_DOWN;
      } else if (dy == 0){
        direction[src][dest] = (dx > 0)?_RIGHT:_LEFT;
      } else if (dx == dy){
        direction[src][dest] = (dx > 0)?_RIGHT_UP:_LEFT_DOWN;
      } else if (dx == -dy){
        direction[src][dest] = (dx > 0)?_RIGHT_DOWN:_LEFT_UP;
      } else{
        direction[src][dest] = NODIR;
      }
    }
  }
}

static void init_byteshift(void){
  int i;
  for (i = 0; i < NDIR; i++)
    byteshift[i] = increment[i] * (int)sizeof(SQUARE_INFO);
}

/*=============================================================================
 * Square Access
 *=============================================================================*/
static SQUARE_INFO* square_ptr(SQUARE_NUM sq){
  return &pos_sp->n.square[sq];
}

static WOOD_PIECE* who_on_square(SQUARE_NUM sq){
  return &pos_sp->n.square[sq].wood;
}

static void clear_square(SQUARE_NUM sq){
  SQUARE_INFO* sqp = square_ptr(sq);
  if (sqp->wood.name != NOPIECE){
    U64 clr = ~(1ULL << (unsigned)sq);
    bb_piece[sqp->wood.name] &= clr;
    bb_side[sqp->wood.color] &= clr;
  }
  sqp->wood.name = NOPIECE;
  sqp->wood.color = NOCOLOR;
  sqp->wood.cost = 0;
  sqp->who_mask.dwrd[0] = 0;
  sqp->who_mask.dwrd[1] = 0;
}

static void set_square(SQUARE_NUM sq, PIECE_NAME name, PIECE_COLOR color){
  SQUARE_INFO* sqp = square_ptr(sq);
  if (sqp->wood.name != NOPIECE){
    U64 clr = ~(1ULL << (unsigned)sq);
    bb_piece[sqp->wood.name] &= clr;
    bb_side[sqp->wood.color] &= clr;
  }
  sqp->wood.name = name;
  sqp->wood.color = color;
  sqp->wood.cost = piece_cost[name];
  sqp->who_mask.dwrd[0] = 0;
  sqp->who_mask.dwrd[1] = 0;
  {
    U64 bit = 1ULL << (unsigned)sq;
    bb_piece[name] |= bit;
    bb_side[color] |= bit;
  }
  if (name == KING)
    king_sq[color] = sq;
}

/*=============================================================================
 * Piece List Management
 *=============================================================================*/
static int color_to_king_num(PIECE_COLOR color){
  return color * NUM_PIECE;
}

static void set_piece_in_list(int index, SQUARE_NUM sq){
  if (index >= 0 && index < TOTAL_NUM_PIECE)
    pos_sp->n.piece_list[index].where = sq;
}

static void update_piece_mask(PIECE_NAME name, PIECE_COLOR color, DWRD mask, int add){
  if (add){
    pos_sp->n.piece_mask[name].dwrd[0] |= mask;
    pos_sp->n.pos_mask.dwrd[0] |= mask;
  } else{
    pos_sp->n.piece_mask[name].dwrd[0] &= ~mask;
    pos_sp->n.pos_mask.dwrd[0] &= ~mask;
  }
}

/*=============================================================================
 * Attack Detection (bitboard-based)
 *=============================================================================*/
static int is_square_attacked(SQUARE_NUM sq, PIECE_COLOR by_color){
  U64 occ = bb_side[0] | bb_side[1];
  U64 by = bb_side[by_color];

  if (bb_atk_knight[sq] & bb_piece[KNIGHT] & by) return 1;
  if (bb_atk_king[sq] & bb_piece[KING] & by) return 1;
  if (bb_atk_pawn[1 - by_color][sq] & bb_piece[PAWN] & by) return 1;
  if (bb_bishop_attacks(sq,occ) & (bb_piece[BISHOP] | bb_piece[QUEEN]) & by) return 1;
  if (bb_rook_attacks(sq,occ) & (bb_piece[ROOK] | bb_piece[QUEEN]) & by) return 1;
  return 0;
}

static int in_check(void){
  return is_square_attacked(king_sq[move_color],1 - move_color);
}

/*=============================================================================
 * Move Making
 *=============================================================================*/
static void update_hash_for_piece(SQUARE_NUM sq, PIECE_NAME name, PIECE_COLOR color){
  position_hash ^= (sq + 1) * (name * 100 + color * 50 + 1);
}

int make_move(MOVE* move){
  SQUARE_INFO *fromp, *top;
  PIECE_NAME name;
  PIECE_COLOR color;
  int captured = 0;
  int i;

  save_position();

  fromp = square_ptr(move->from);
  top = square_ptr(move->to);
  name = fromp->wood.name;
  color = fromp->wood.color;

  if (name == NOPIECE || color != move_color){
    restore_position();
    return -1;
  }

  if (top->wood.name != NOPIECE){
    captured = top->wood.cost;
    material_eval -= captured;

    for (i = 0; i < TOTAL_NUM_PIECE; i++){
      if (pos_sp->n.piece_list[i].where == move->to){
        pos_sp->n.piece_list[i].where = DUMMY;
        break;
      }
    }

    update_piece_mask(top->wood.name,top->wood.color,(1 << move->to),0);
    move->flags |= M_CAPTURE;

    if (top->wood.name == ROOK){
      int rk_file = move->to % 8;
      PIECE_COLOR rook_color = top->wood.color;
      if (rk_file == 0) castle[rook_color] &= ~CASTLELEFT;
      if (rk_file == 7) castle[rook_color] &= ~CASTLERIGHT;
    }
  }

  /* En passant detection — auto-set M_EN_PASSANT when the board state
   * unambiguously identifies this as an ep capture.  This handles moves
   * arriving from the GUI via parse_move() (flags=0) as well as internally
   * generated moves that already carry the flag.
   *
   * Conditions (all must hold):
   *   1. Moving piece is a pawn.
   *   2. The move is diagonal (file changes by exactly 1).
   *   3. The destination square is empty — not a regular capture.
   *   4. The destination equals en_pass_square.
   *
   * Without this, a GUI-supplied en passant move silently skips the
   * captured-pawn removal, leaving an extra enemy pawn on the board and
   * diverging the engine's position from the game for every subsequent move. */
  if (name == PAWN &&
    abs((move->to % 8) - (move->from % 8)) == 1 &&
    top->wood.name == NOPIECE &&
    move->to == en_pass_square){
    move->flags |= M_EN_PASSANT | M_CAPTURE;
  }

  if (move->flags & M_EN_PASSANT){
    int ep_dir = (color == WHITE_PIECE)?-8:8;
    SQUARE_NUM ep_cap = move->to + ep_dir;
    if (ep_cap >= 0 && ep_cap < 64 && who_on_square(ep_cap)->name == PAWN &&
      who_on_square(ep_cap)->color != color){
      material_eval -= PAWN_COST;
      update_piece_mask(PAWN,1 - color,(1 << ep_cap),0);
      for (i = 0; i < TOTAL_NUM_PIECE; i++){
        if (pos_sp->n.piece_list[i].where == ep_cap){
          pos_sp->n.piece_list[i].where = DUMMY;
          break;
        }
      }
      clear_square(ep_cap);
    }
  }

  update_piece_mask(name,color,(1 << move->from),0);

  for (i = 0; i < TOTAL_NUM_PIECE; i++){
    if (pos_sp->n.piece_list[i].where == move->from){
      pos_sp->n.piece_list[i].where = DUMMY;
      break;
    }
  }

  clear_square(move->from);

  set_square(move->to,name,color);

  for (i = 0; i < TOTAL_NUM_PIECE; i++){
    if (pos_sp->n.piece_list[i].where == DUMMY &&
      i >= color_to_king_num(color) &&
      i < color_to_king_num(color) + NUM_PIECE){
      pos_sp->n.piece_list[i].where = move->to;
      break;
    }
  }

  update_piece_mask(name,color,(1 << move->to),1);

  if ((move->flags & M_PROMOTION) && name == PAWN){
    int new_rank = move->to / 8;
    if (new_rank == 0 || new_rank == 7){
      update_piece_mask(PAWN,color,(1 << move->to),0);
      update_piece_mask(QUEEN,color,(1 << move->to),1);
      who_on_square(move->to)->name = QUEEN;
      who_on_square(move->to)->cost = QUEEN_COST;
      material_eval += QUEEN_COST - PAWN_COST;
    }
  } else if (name == PAWN && (move->to / 8 == 0 || move->to / 8 == 7)){
    /* Pawn reached back rank without M_PROMOTION flag (e.g. from GUI).
     * Always promote to queen — same as the engine always does. */
    update_piece_mask(PAWN,color,(1 << move->to),0);
    update_piece_mask(QUEEN,color,(1 << move->to),1);
    who_on_square(move->to)->name = QUEEN;
    who_on_square(move->to)->cost = QUEEN_COST;
    material_eval += QUEEN_COST - PAWN_COST;
    move->flags |= M_PROMOTION;
  }

  if (name == KING){
    if (abs(move->to - move->from) == 2){
      castle[color] = (move->to > move->from)?CASTLERIGHTDONE
        :CASTLELEFTDONE;
    } else{
      castle[color] = 0;
    }

    if (abs(move->to - move->from) == 2){
      SQUARE_NUM rook_from, rook_to;

      if (move->to > move->from){
        rook_from = move->to + 1;
        rook_to = move->to - 1;
      } else{
        rook_from = move->to - 2;
        rook_to = move->to + 1;
      }

      if (rook_from >= 0 && rook_from < 64 && rook_to >= 0 && rook_to < 64){
        update_piece_mask(ROOK,color,(1 << rook_from),0);
        clear_square(rook_from);

        for (i = 0; i < TOTAL_NUM_PIECE; i++){
          if (pos_sp->n.piece_list[i].where == rook_from){
            pos_sp->n.piece_list[i].where = DUMMY;
            break;
          }
        }

        set_square(rook_to, ROOK,color);
        update_piece_mask(ROOK,color,(1 << rook_to),1);

        for (i = 0; i < TOTAL_NUM_PIECE; i++){
          if (pos_sp->n.piece_list[i].where == DUMMY &&
            i >= color_to_king_num(color) &&
            i < color_to_king_num(color) + NUM_PIECE){
            pos_sp->n.piece_list[i].where = rook_to;
            break;
          }
        }
      }
    }
  }

  if (name == ROOK){
    if (move->from % 8 == 0){
      castle[color] &= ~CASTLELEFT;
    } else if (move->from % 8 == 7){
      castle[color] &= ~CASTLERIGHT;
    }
  }

  update_hash_for_piece(move->from,name,color);
  update_hash_for_piece(move->to,name,color);
  if (captured){
    update_hash_for_piece(move->to,top->wood.name,top->wood.color);
  }

  if (name == PAWN && abs(move->to - move->from) == 16){
    en_pass_square = move->from + (move->to - move->from) / 2;
  } else{
    en_pass_square = DUMMY;
  }

  /* 50-move rule: reset on pawn move or capture, otherwise increment */
  if (name == PAWN || (move->flags & M_CAPTURE))
    halfmove_clock = 0;
  else
    halfmove_clock++;

  move_color = 1 - move_color;

  return 0;
}

void take_back_move(void){
  restore_position();
}

/*=============================================================================
 * Move Generation
 *=============================================================================*/
static void add_move(MOVE** mp, SQUARE_NUM from, SQUARE_NUM to, unsigned int flags, int profit){
  (*mp)->from = from;
  (*mp)->to = to;
  (*mp)->flags = flags;
  (*mp)->profit = profit;
  (*mp)++;
}

static void generate_pawn_moves(SQUARE_NUM from, MOVE** mp){
  SQUARE_INFO* fromp = square_ptr(from);
  PIECE_COLOR color = fromp->wood.color;
  int dir = (color == WHITE_PIECE)?8:-8;
  int promo_rank = (color == WHITE_PIECE)?7:0;
  SQUARE_NUM to = from + dir;

  if (to >= 0 && to < 64 && who_on_square(to)->name == NOPIECE){
    if (to / 8 == promo_rank)
      add_move(mp,from,to, M_PROMOTION, QUEEN_COST - PAWN_COST);
    else{
      add_move(mp,from,to,0,0);

      if ((color == WHITE_PIECE && from / 8 == 1) ||
        (color == BLACK_PIECE && from / 8 == 6)){
        to = from + dir * 2;
        if (to >= 0 && to < 64 && who_on_square(to)->name == NOPIECE)
          add_move(mp,from,to,0,0);
      }
    }
  }

  if (from % 8 > 0){
    to = from + dir - 1;
    if (to >= 0 && to < 64 && who_on_square(to)->name != NOPIECE &&
      who_on_square(to)->color != color){
      if (to / 8 == promo_rank)
        add_move(mp,from,to, M_PROMOTION | M_CAPTURE,who_on_square(to)->cost + QUEEN_COST - PAWN_COST);
      else
        add_move(mp,from,to, M_CAPTURE,who_on_square(to)->cost);
    }
  }
  if (from % 8 < 7){
    to = from + dir + 1;
    if (to >= 0 && to < 64 && who_on_square(to)->name != NOPIECE &&
      who_on_square(to)->color != color){
      if (to / 8 == promo_rank)
        add_move(mp,from,to, M_PROMOTION | M_CAPTURE,who_on_square(to)->cost + QUEEN_COST - PAWN_COST);
      else
        add_move(mp,from,to, M_CAPTURE,who_on_square(to)->cost);
    }
  }

  if (en_pass_square != DUMMY){
    SQUARE_NUM captured_sq = en_pass_square - dir;
    if (captured_sq >= 0 && captured_sq < 64 &&
      from / 8 == captured_sq / 8 &&
      abs((from % 8) - (en_pass_square % 8)) == 1){
      add_move(mp,from,en_pass_square, M_EN_PASSANT | M_CAPTURE, PAWN_COST);
    }
  }
}

static void generate_knight_moves(SQUARE_NUM from, MOVE** mp){
  SQUARE_INFO* fromp = square_ptr(from);
  PIECE_COLOR color = fromp->wood.color;
  int i, to;

  for (i = 0; i < NDIR; i++){
    to = from + knincrement[i];
    if (to >= 0 && to < 64 && abs((to % 8) - (from % 8)) <= 2){
      if (who_on_square(to)->color != color){
        add_move(mp,from,to,
          (who_on_square(to)->name != NOPIECE)?M_CAPTURE:0,
          who_on_square(to)->cost);
      }
    }
  }
}

static void generate_sliding_moves(SQUARE_NUM from, MOVE** mp, const int* directions, int num_dirs){
  SQUARE_INFO* fromp = square_ptr(from);
  PIECE_COLOR color = fromp->wood.color;
  int i, step, to, prev;

  for (i = 0; i < num_dirs; i++){
    step = directions[i];
    for (prev = from, to = from + step;
         to >= 0 && to < 64 && abs((to % 8) - (prev % 8)) <= 1;
         prev = to, to += step){
      if (who_on_square(to)->color == color)
        break;
      add_move(mp,from,to,
        (who_on_square(to)->name != NOPIECE)?M_CAPTURE:0,
        who_on_square(to)->cost);
      if (who_on_square(to)->name != NOPIECE)
        break;
    }
  }
}

static void generate_king_moves(SQUARE_NUM from, MOVE** mp){
  SQUARE_INFO* fromp = square_ptr(from);
  PIECE_COLOR color = fromp->wood.color;
  int i, to;

  for (i = 0; i < NDIR; i++){
    to = from + increment[i];
    if (to >= 0 && to < 64 && abs((to % 8) - (from % 8)) <= 1){
      if (who_on_square(to)->color != color &&
        ! is_square_attacked(to,1 - color)){
        add_move(mp,from,to,
          (who_on_square(to)->name != NOPIECE)?M_CAPTURE:0,
          who_on_square(to)->cost);
      }
    }
  }

  if (! is_square_attacked(from,1 - color)){
    if (castle[color] & CASTLERIGHT){
      SQUARE_NUM rook_sq = (from / 8) * 8 + 7;
      if (who_on_square(rook_sq)->name == ROOK &&
        who_on_square(rook_sq)->color == color &&
        who_on_square(from + 1)->name == NOPIECE &&
        who_on_square(from + 2)->name == NOPIECE &&
        ! is_square_attacked(from + 1,1 - color) &&
        ! is_square_attacked(from + 2,1 - color)){
        add_move(mp,from,from + 2, M_CASTLE_RIGHT,0);
      }
    }
    if (castle[color] & CASTLELEFT){
      SQUARE_NUM rook_sq = (from / 8) * 8;
      if (from - 1 >= 0 && from - 2 >= 0 && from - 3 >= 0 &&
        who_on_square(rook_sq)->name == ROOK &&
        who_on_square(rook_sq)->color == color &&
        who_on_square(from - 1)->name == NOPIECE &&
        who_on_square(from - 2)->name == NOPIECE &&
        who_on_square(from - 3)->name == NOPIECE &&
        ! is_square_attacked(from - 1,1 - color) &&
        ! is_square_attacked(from - 2,1 - color)){
        add_move(mp,from,from - 2, M_CASTLE_LEFT,0);
      }
    }
  }
}

static void generate_moves_for_piece(SQUARE_NUM from, MOVE** mp){
  PIECE_NAME name = who_on_square(from)->name;

  if (name == NOPIECE || who_on_square(from)->color != move_color)
    return;

  switch (name){
  case PAWN: generate_pawn_moves(from,mp);
    break;
  case KNIGHT: generate_knight_moves(from,mp);
    break;
  case BISHOP:
    {
      int dirs[] = {7,9,-7,-9};
      generate_sliding_moves(from,mp,dirs,4);
      break;
    }
  case ROOK:
    {
      int dirs[] = {1,8,-1,-8};
      generate_sliding_moves(from,mp,dirs,4);
      break;
    }
  case QUEEN:
    {
      int dirs[] = {1,8,-1,-8,7,9,-7,-9};
      generate_sliding_moves(from,mp,dirs,8);
      break;
    }
  case KING: generate_king_moves(from,mp);
    break;
  }
}

void generate_moves(MOVE** first, MOVE** last){
  SQUARE_NUM sq;
  MOVE* mp = *first;

  for (sq = 0; sq < NSQUARE; sq++)
    generate_moves_for_piece(sq,&mp);

  *last = mp;
}

int is_legal_move(MOVE* move){
  PIECE_COLOR moving_color = move_color;

  if (make_move(move) != 0)
    return 0;

  /* Legality test: is our king attacked after the move?
   *
   * We deliberately use a MAILBOX-based scan (piece_attacks_sq) rather
   * than is_square_attacked (bitboard-based).  make_move() always keeps
   * the mailbox correct; the bitboards can temporarily lag when see() or
   * quiescence manipulates pos_sp->n.square[] directly without going
   * through set_square/clear_square.  A mailbox scan is immune to that
   * class of desync and is the safer source of truth here.
   */
  SQUARE_NUM ksq = king_sq[moving_color];
  PIECE_COLOR enemy = 1 - moving_color;
  int attacked = 0;
  int s;
  for (s = 0; s < NSQUARE && ! attacked; s++){
    WOOD_PIECE* w = who_on_square(s);
    if (w->name != NOPIECE && w->color == enemy)
      attacked = piece_attacks_sq(s,ksq,w->name,w->color);
  }

  take_back_move();
  return ! attacked;
}

static void generate_legal_moves(MOVE** first, MOVE** last){
  MOVE* mp = *first;
  MOVE* legal = *first;

  generate_moves(first,last);

  for (mp = *first; mp < *last; mp++){
    /* Skip self-moves */
    if (mp->from == mp->to){
      continue;
    }

    if (is_legal_move(mp)){
      if (legal != mp)
        *legal = *mp;
      legal++;
    }
  }
  *last = legal;
}

/*=============================================================================
 * Position Stack Management
 *=============================================================================*/
static void save_position(void){
  if (stack_ptr >= MAX_STACK - 1){
    /* This should never happen with MAX_STACK=512, but guard against it
     * explicitly.  Silently skipping the save (old behaviour) would corrupt
     * the board on the paired restore_position() call. */
    fprintf(stderr,"FATAL: position stack overflow at depth %d\n",stack_ptr);
    return;
  }
  SAVED_POSITION* saved = &pos_stack_simple[stack_ptr];
  NEAR_POSITION* n = &pos_sp->n;

  memcpy(saved->squares,n->square,sizeof(n->square));
  memcpy(saved->piece_list,n->piece_list,sizeof(n->piece_list));
  memcpy(saved->piece_mask,n->piece_mask,sizeof(n->piece_mask));
  saved->pos_mask = n->pos_mask;
  saved->castle[0] = castle[0];
  saved->castle[1] = castle[1];
  saved->king_sq[0] = king_sq[0];
  saved->king_sq[1] = king_sq[1];
  saved->move_color = move_color;
  saved->material_eval = material_eval;
  saved->en_pass_square = en_pass_square;
  saved->position_hash = position_hash;
  saved->zobrist_hash = compute_zobrist_hash();
  memcpy(saved->sv_bb_piece,bb_piece,sizeof(bb_piece));
  memcpy(saved->sv_bb_side,bb_side,sizeof(bb_side));
  saved->halfmove_clock = halfmove_clock;

  stack_ptr++;
}

static void restore_position(void){
  if (stack_ptr > 0){
    stack_ptr--;
    SAVED_POSITION* saved = &pos_stack_simple[stack_ptr];
    NEAR_POSITION* n = &pos_sp->n;

    memcpy(n->square,saved->squares,sizeof(n->square));
    memcpy(n->piece_list,saved->piece_list,sizeof(n->piece_list));
    memcpy(n->piece_mask,saved->piece_mask,sizeof(n->piece_mask));
    n->pos_mask = saved->pos_mask;
    castle[0] = saved->castle[0];
    castle[1] = saved->castle[1];
    king_sq[0] = saved->king_sq[0];
    king_sq[1] = saved->king_sq[1];
    move_color = saved->move_color;
    material_eval = saved->material_eval;
    en_pass_square = saved->en_pass_square;
    position_hash = saved->position_hash;
    memcpy(bb_piece,saved->sv_bb_piece,sizeof(bb_piece));
    memcpy(bb_side,saved->sv_bb_side,sizeof(bb_side));
    halfmove_clock = saved->halfmove_clock;

    for (int i = 0; i < NSQUARE; i++){
      n->square[i].mynum = i;
    }
  }
}

static int repetition_count(void){
  U64 cur = compute_zobrist_hash();
  int cnt = 0, k;
  for (k = 0; k < stack_ptr; k++)
    if (pos_stack_simple[k].zobrist_hash == cur)
      cnt++;
  return cnt;
}

/*=============================================================================
 * King Safety Evaluation
 *=============================================================================*/
static int evaluate_king_safety(PIECE_COLOR color){
  int score = 0;
  SQUARE_NUM king_pos = king_sq[color];
  int king_file = king_pos % 8;
  int king_rank = king_pos / 8;
  PIECE_COLOR enemy = 1 - color;
  int endgame = is_endgame();

  if (endgame) return 0;

  int pawn_dir = (color == WHITE_PIECE)?8:-8;
  int shield = 0;
  int f;
  for (f = -1; f <= 1; f++){
    int file = king_file + f;
    if (file < 0 || file > 7) continue;

    int sq1 = king_pos + pawn_dir + f;
    int sq2 = king_pos + pawn_dir * 2 + f;

    if (sq1 >= 0 && sq1 < 64){
      if (who_on_square(sq1)->name == PAWN &&
        who_on_square(sq1)->color == color){
        shield += 2;
        continue;
      }
    }
    if (sq2 >= 0 && sq2 < 64){
      if (who_on_square(sq2)->name == PAWN &&
        who_on_square(sq2)->color == color){
        shield += 1;
      }
    }
  }
  score += shield * K_PAWN_SHIELD;

  int own_pawn_on_king_file = 0;
  int r;
  for (r = 0; r < 8; r++){
    SQUARE_NUM s = r * 8 + king_file;
    if (who_on_square(s)->name == PAWN && who_on_square(s)->color == color){
      own_pawn_on_king_file = 1;
      break;
    }
  }
  if (! own_pawn_on_king_file && king_rank > 1 && king_rank < 6){
    score -= 40;
  }

  for (f = king_file - 1; f <= king_file + 1; f++){
    if (f < 0 || f > 7) continue;
    int own_pawns = 0, enemy_pawns = 0;
    for (r = 0; r < 8; r++){
      SQUARE_NUM s = r * 8 + f;
      if (who_on_square(s)->name == PAWN){
        if (who_on_square(s)->color == color) own_pawns++;
        else enemy_pawns++;
      }
    }
    if (own_pawns == 0){
      score += K_OPEN_FILE_NEAR_KING;
      if (enemy_pawns > 0)
        score += K_OPEN_FILE_ENEMY_PAWN;
    }
  }

  /* -----------------------------------------------------------------------
   * Weighted king danger score.
   * Build a 3×3 king zone plus the two squares immediately in front.
   * For each enemy piece that attacks any zone square, add its weight to a
   * running total.  Map the total through KING_DANGER_TABLE for a non-linear
   * penalty (danger grows faster as more pieces join the attack).
   * ----------------------------------------------------------------------- */
  {
    U64 king_zone = bb_atk_king[king_pos] | (1ULL << king_pos);
    /* extend zone one rank toward the enemy (the "sensitive" squares) */
    if (color == WHITE_PIECE)
      king_zone |= (king_zone << 8) & 0xFFFFFFFFFFFFFFFFULL;
    else
      king_zone |= (king_zone >> 8) & 0xFFFFFFFFFFFFFFFFULL;

    int danger = 0;
    U64 occ_kd = bb_side[0] | bb_side[1];

    /* Knights */
    U64 en_knights = bb_piece[KNIGHT] & bb_side[enemy];
    U64 tmp_kd = en_knights;
    while (tmp_kd){
      int esq = bb_lsb(tmp_kd);
      tmp_kd &= tmp_kd - 1;
      if (bb_atk_knight[esq] & king_zone)
        danger += KING_DANGER_WEIGHT[KNIGHT];
    }
    /* Bishops */
    U64 en_bishops = bb_piece[BISHOP] & bb_side[enemy];
    tmp_kd = en_bishops;
    while (tmp_kd){
      int esq = bb_lsb(tmp_kd);
      tmp_kd &= tmp_kd - 1;
      if (bb_bishop_attacks(esq,occ_kd) & king_zone)
        danger += KING_DANGER_WEIGHT[BISHOP];
    }
    /* Rooks */
    U64 en_rooks = bb_piece[ROOK] & bb_side[enemy];
    tmp_kd = en_rooks;
    while (tmp_kd){
      int esq = bb_lsb(tmp_kd);
      tmp_kd &= tmp_kd - 1;
      if (bb_rook_attacks(esq,occ_kd) & king_zone)
        danger += KING_DANGER_WEIGHT[ROOK];
    }
    /* Queens */
    U64 en_queens = bb_piece[QUEEN] & bb_side[enemy];
    tmp_kd = en_queens;
    while (tmp_kd){
      int esq = bb_lsb(tmp_kd);
      tmp_kd &= tmp_kd - 1;
      if ((bb_rook_attacks(esq,occ_kd) | bb_bishop_attacks(esq,occ_kd)) & king_zone)
        danger += KING_DANGER_WEIGHT[QUEEN];
    }

    if (danger > 19) danger = 19;
    score -= KING_DANGER_TABLE[danger];

    /* Retain simple adjacent-square attacker penalty as an extra bonus
     * for positions where the king is directly surrounded */
  }

  int attackers = 0;
  int dir;
  for (dir = 0; dir < NDIR; dir++){
    SQUARE_NUM to = king_pos + increment[dir];
    if (to >= 0 && to < 64 && abs((to % 8) - king_file) <= 1){
      if (is_square_attacked(to,enemy))
        attackers++;
    }
  }
  /* Halve the old flat penalty — the danger table now carries most of the weight */
  score -= attackers * (K_KING_EXPOSURE / 2);

  U64 queen_mask = bb_piece[QUEEN] & bb_side[enemy];
  if (queen_mask){
    int queen_sq = bb_lsb(queen_mask);
    int queen_dist = king_distance(king_pos,queen_sq);
    if (queen_dist <= 2)
      score -= K_QUEEN_NEAR_KING * (3 - queen_dist);
  }

  if (color == WHITE_PIECE){
    U64 rook_7th = bb_piece[ROOK] & bb_side[enemy] & 0x00FF000000000000ULL;
    if (rook_7th && is_square_attacked(king_pos,enemy))
      score -= K_ROOK_ON_7TH_ATTACK;
  } else{
    U64 rook_7th = bb_piece[ROOK] & bb_side[enemy] & 0x000000000000FF00ULL;
    if (rook_7th && is_square_attacked(king_pos,enemy))
      score -= K_ROOK_ON_7TH_ATTACK;
  }

  int safe_escapes = 0;
  for (dir = 0; dir < NDIR; dir++){
    SQUARE_NUM to = king_pos + increment[dir];
    if (to >= 0 && to < 64 && abs((to % 8) - king_file) <= 1){
      if (who_on_square(to)->color != color &&
        ! is_square_attacked(to,enemy))
        safe_escapes++;
    }
  }
  if (safe_escapes == 0 && is_square_attacked(king_pos,enemy))
    score -= 200;
  else if (safe_escapes <= 1)
    score -= 30;
  else if (safe_escapes >= 3)
    score += 15;

  return score;
}

/*=============================================================================
 * Endgame Evaluation Helpers
 *=============================================================================*/
static int king_distance(SQUARE_NUM a, SQUARE_NUM b){
  int df = abs((a % 8) - (b % 8));
  int dr = abs((a / 8) - (b / 8));
  return (df > dr)?df:dr;
}

static int centre_manhattan(SQUARE_NUM sq){
  int f = sq % 8, r = sq / 8;
  int fd = (f < 4)?(3 - f):(f - 4);
  int rd = (r < 4)?(3 - r):(r - 4);
  return fd + rd;
}

static int is_endgame(void){
  int total = 0, sq2;
  for (sq2 = 0; sq2 < NSQUARE; sq2++){
    PIECE_NAME n = who_on_square(sq2)->name;
    if (n != NOPIECE && n != KING && n != PAWN)
      total += piece_cost[n];
  }
  return total < ENDGAME_MATERIAL;
}

/* compute_game_phase — returns 256 in the opening/early middlegame, 0 in a
 * bare endgame.  Piece phase weights: Q=4, R=2, B=1, N=1 (max total = 24
 * across both sides), mapped linearly to the 0-256 range.
 * Pawns and kings do not contribute so the function correctly identifies
 * material-poor endings regardless of remaining pawn count. */
static int compute_game_phase(void){
  /* indexed by PIECE_NAME: NOPIECE=0,KING=1,QUEEN=2,ROOK=3,BISHOP=4,KNIGHT=5,PAWN=6 */
  static const int pw[7] = {0,0,4,2,1,1,0};
  int total = 0, sq;
  for (sq = 0; sq < NSQUARE; sq++){
    PIECE_NAME n = who_on_square(sq)->name;
    if (n >= 0 && n <= 6) total += pw[(int)n];
  }
  if (total > 24) total = 24;
  return total * 256 / 24;
}

static int kp_vs_k_bonus(void){
  int score = 0, sq2;
  for (sq2 = 0; sq2 < NSQUARE; sq2++){
    if (who_on_square(sq2)->name != PAWN) continue;
    PIECE_COLOR pc = who_on_square(sq2)->color;
    PIECE_COLOR opp = 1 - pc;
    int file = sq2 % 8, rank = sq2 / 8;
    int promo_rank = (pc == WHITE_PIECE)?7:0;
    int steps_to_promo = (pc == WHITE_PIECE)?(7 - rank):rank;
    SQUARE_NUM promo_sq = file + promo_rank * 8;
    int def_dist = king_distance(king_sq[opp],promo_sq);
    if (def_dist > steps_to_promo + 1){
      int bonus = 150 + steps_to_promo * (-20);
      score += (pc == WHITE_PIECE)?bonus:-bonus;
    }
  }
  return score;
}

static int is_center_square(SQUARE_NUM sq){
  return (sq == 27 || sq == 28 || sq == 35 || sq == 36);
}

static void compute_strong_squares(PIECE_COLOR color, U64* out){
  U64 own_pawns = bb_piece[PAWN] & bb_side[color];
  U64 attacked_by_own = 0ULL;
  U64 tmp = own_pawns;
  while (tmp){
    int sq = bb_lsb(tmp);
    tmp &= tmp - 1;
    attacked_by_own |= bb_atk_pawn[color][sq];
  }
  U64 strong = 0ULL;
  U64 cand = attacked_by_own;
  while (cand){
    int sq = bb_lsb(cand);
    cand &= cand - 1;
    int file = sq % 8;
    int rank = sq / 8;
    int can_be_attacked = 0;
    int f2;
    for (f2 = file - 1; f2 <= file + 1; f2 += 2){
      if (f2 < 0 || f2 > 7) continue;
      int r2;
      for (r2 = 0; r2 < 8; r2++){
        SQUARE_NUM psq = r2 * 8 + f2;
        if (who_on_square(psq)->name == PAWN &&
          who_on_square(psq)->color == (1 - color)){
          if (color == WHITE_PIECE && r2 >= rank + 1){
            can_be_attacked = 1;
          } else if (color == BLACK_PIECE && r2 <= rank - 1){
            can_be_attacked = 1;
          }
        }
      }
    }
    if (! can_be_attacked) strong |= (1ULL << sq);
  }
  *out = strong;
}

static int bb_attacks_for_piece(SQUARE_NUM sq){
  PIECE_NAME pn = who_on_square(sq)->name;
  U64 occ = bb_side[0] | bb_side[1];
  switch (pn){
  case KNIGHT: return bb_popcount(bb_atk_knight[sq]);
  case BISHOP: return bb_popcount(bb_bishop_attacks(sq,occ));
  case ROOK: return bb_popcount(bb_rook_attacks(sq,occ));
  case QUEEN: return bb_popcount(bb_rook_attacks(sq,occ) | bb_bishop_attacks(sq,occ));
  case KING: return bb_popcount(bb_atk_king[sq]);
  default: return 0;
  }
}

static int is_piece_pinned(SQUARE_NUM sq){
  PIECE_COLOR pc = who_on_square(sq)->color;
  SQUARE_NUM ksq = king_sq[pc];
  WOOD_PIECE saved = pos_sp->n.square[sq].wood;
  pos_sp->n.square[sq].wood.name = NOPIECE;
  pos_sp->n.square[sq].wood.color = NOCOLOR;
  pos_sp->n.square[sq].wood.cost = 0;
  U64 occ = (bb_side[0] | bb_side[1]) & ~(1ULL << sq);
  PIECE_COLOR enemy = 1 - pc;
  U64 enemy_sliders = (bb_piece[ROOK] | bb_piece[QUEEN]) & bb_side[enemy];
  U64 enemy_diag = (bb_piece[BISHOP] | bb_piece[QUEEN]) & bb_side[enemy];
  int pinned = (bb_rook_attacks(ksq,occ) & enemy_sliders) ||
    (bb_bishop_attacks(ksq,occ) & enemy_diag);
  pos_sp->n.square[sq].wood = saved;
  return pinned;
}

/*=============================================================================
 * Piece-Square Tables
 * Index: sq = rank*8+file, rank 0 = White's 1st rank (a1=0, h8=63).
 * For Black, mirror vertically: pst_sq = (7 - sq/8)*8 + sq%8.
 * Values in centipawns, intentionally modest to complement existing terms.
 *=============================================================================*/
/* Pawn: encourage central advance, penalise static back-rank pawns */
static const int pst_pawn[64] = {
  0,0,0,0,0,0,0,0, /* rank 1 */
  2,4,4,-8,-8,4,4,2, /* rank 2 */
  2,-2,-4,0,0,-4,-2,2, /* rank 3 */
  0,0,0,10,10,0,0,0, /* rank 4 */
  2,2,5,12,12,5,2,2, /* rank 5 */
  5,5,10,15,15,10,5,5, /* rank 6 */
  25,25,25,25,25,25,25,25, /* rank 7 */
  0,0,0,0,0,0,0,0 /* rank 8 */
};
/* Knight: strongly penalise rim, reward centre approach */
static const int pst_knight[64] = {
  -20,-16,-12,-12,-12,-12,-16,-20, /* rank 1 */
  -16,-8,0,0,0,0,-8,-16, /* rank 2 */
  -12,0,4,6,6,4,0,-12, /* rank 3 */
  -12,2,6,8,8,6,2,-12, /* rank 4 */
  -12,0,6,8,8,6,0,-12, /* rank 5 */
  -12,2,4,6,6,4,2,-12, /* rank 6 */
  -16,-8,0,2,2,0,-8,-16, /* rank 7 */
  -20,-16,-12,-12,-12,-12,-16,-20 /* rank 8 */
};
/* Bishop: reward diagonals, discourage corners */
static const int pst_bishop[64] = {
  -8,-4,-4,-4,-4,-4,-4,-8, /* rank 1 */
  -4,0,0,0,0,0,0,-4, /* rank 2 */
  -4,0,2,4,4,2,0,-4, /* rank 3 */
  -4,2,2,4,4,2,2,-4, /* rank 4 */
  -4,0,4,4,4,4,0,-4, /* rank 5 */
  -4,4,4,4,4,4,4,-4, /* rank 6 */
  -4,2,0,0,0,0,2,-4, /* rank 7 */
  -8,-4,-4,-4,-4,-4,-4,-8 /* rank 8 */
};
/* Rook: reward 7th rank, centralise files, a1/h1 are fine (castled) */
static const int pst_rook[64] = {
  0,0,0,2,2,0,0,0, /* rank 1 */
  -2,0,0,0,0,0,0,-2, /* rank 2 */
  -2,0,0,0,0,0,0,-2, /* rank 3 */
  -2,0,0,0,0,0,0,-2, /* rank 4 */
  -2,0,0,0,0,0,0,-2, /* rank 5 */
  -2,0,0,0,0,0,0,-2, /* rank 6 */
  2,5,5,5,5,5,5,2, /* rank 7 */
  0,0,0,0,0,0,0,0 /* rank 8 */
};
/* Queen MG: slight central preference; no early development */
static const int pst_queen_mg[64] = {
  -8,-4,-4,-2,-2,-4,-4,-8, /* rank 1 */
  -4,0,0,0,0,0,0,-4, /* rank 2 */
  -4,0,2,2,2,2,0,-4, /* rank 3 */
  -2,0,2,2,2,2,0,-2, /* rank 4 */
  0,0,2,2,2,2,0,-2, /* rank 5 */
  -4,2,2,2,2,2,0,-4, /* rank 6 */
  -4,0,2,0,0,0,0,-4, /* rank 7 */
  -8,-4,-4,-2,-2,-4,-4,-8 /* rank 8 */
};
/* King MG: stay on back rank, prefer corners after castling */
static const int pst_king_mg[64] = {
  10,16,8,0,0,8,16,10, /* rank 1 */
  10,10,0,0,0,0,10,10, /* rank 2 */
  -10,-20,-20,-20,-20,-20,-20,-10, /* rank 3 */
  -20,-30,-30,-40,-40,-30,-30,-20, /* rank 4 */
  -20,-30,-30,-40,-40,-30,-30,-20, /* rank 5 */
  -10,-20,-20,-20,-20,-20,-20,-10, /* rank 6 */
  10,10,0,0,0,0,10,10, /* rank 7 */
  10,16,8,0,0,8,16,10 /* rank 8 */
};
/* King EG: centralise */
static const int pst_king_eg[64] = {
  -25,-16,-12,-8,-8,-12,-16,-25, /* rank 1 */
  -16,-8,0,4,4,0,-8,-16, /* rank 2 */
  -12,0,8,12,12,8,0,-12, /* rank 3 */
  -8,4,12,16,16,12,4,-8, /* rank 4 */
  -8,4,12,16,16,12,4,-8, /* rank 5 */
  -12,0,8,12,12,8,0,-12, /* rank 6 */
  -16,-8,0,4,4,0,-8,-16, /* rank 7 */
  -25,-16,-12,-8,-8,-12,-16,-25 /* rank 8 */
};
/* Helper: vertical-mirror a square (White PST index -> Black lookup) */
static inline int pst_mirror(int sq){
  return (7 - sq / 8) * 8 + sq % 8;
}

/*=============================================================================
 * Insufficient Material Draw Detection
 * Returns 1 if neither side can deliver checkmate by any legal sequence.
 *=============================================================================*/
static int is_insufficient_material(void){
  /* Count pieces for each side (excluding kings) */
  int w_queens = bb_popcount(bb_piece[QUEEN] & bb_side[WHITE_PIECE]);
  int b_queens = bb_popcount(bb_piece[QUEEN] & bb_side[BLACK_PIECE]);
  int w_rooks = bb_popcount(bb_piece[ROOK] & bb_side[WHITE_PIECE]);
  int b_rooks = bb_popcount(bb_piece[ROOK] & bb_side[BLACK_PIECE]);
  int w_bishops = bb_popcount(bb_piece[BISHOP] & bb_side[WHITE_PIECE]);
  int b_bishops = bb_popcount(bb_piece[BISHOP] & bb_side[BLACK_PIECE]);
  int w_knights = bb_popcount(bb_piece[KNIGHT] & bb_side[WHITE_PIECE]);
  int b_knights = bb_popcount(bb_piece[KNIGHT] & bb_side[BLACK_PIECE]);
  int w_pawns = bb_popcount(bb_piece[PAWN] & bb_side[WHITE_PIECE]);
  int b_pawns = bb_popcount(bb_piece[PAWN] & bb_side[BLACK_PIECE]);

  /* Any pawn, rook, or queen means mate is possible */
  if (w_pawns || b_pawns || w_rooks || b_rooks || w_queens || b_queens)
    return 0;

  /* K vs K */
  if (! w_bishops && ! w_knights && ! b_bishops && ! b_knights) return 1;

  /* K+B vs K  or  K+N vs K */
  if (! b_bishops && ! b_knights && (w_bishops + w_knights) == 1) return 1;
  if (! w_bishops && ! w_knights && (b_bishops + b_knights) == 1) return 1;

  /* K+B vs K+B — same colour bishops */
  if (w_bishops == 1 && b_bishops == 1 && ! w_knights && ! b_knights){
    int wsq = bb_lsb(bb_piece[BISHOP] & bb_side[WHITE_PIECE]);
    int bsq = bb_lsb(bb_piece[BISHOP] & bb_side[BLACK_PIECE]);
    if (((wsq / 8 + wsq % 8) & 1) == ((bsq / 8 + bsq % 8) & 1))
      return 1; /* same colour complex */
  }

  return 0;
}

static int static_evaluate(void){
  int score = 0;
  int sq;
  int endgame = is_endgame();
  /* Game phase for tapered evaluation (256 = full middlegame, 0 = pure endgame) */
  int gp = compute_game_phase();

  /* ------------------------------------------------------------------ */
  /* 1. MATERIAL + PIECE-SQUARE TABLES                                   */
  /* ------------------------------------------------------------------ */
  for (sq = 0; sq < NSQUARE; sq++){
    PIECE_NAME name = who_on_square(sq)->name;
    PIECE_COLOR color = who_on_square(sq)->color;
    if (name == NOPIECE) continue;
    int sign = (color == WHITE_PIECE)?1:-1;
    score += sign * piece_cost[name];

    /* PST bonus — index mirrors for Black */
    int psq = (color == WHITE_PIECE)?sq:pst_mirror(sq);
    switch (name){
    case PAWN:
      score += sign * pst_pawn[psq];
      break;
    case KNIGHT:
      score += sign * pst_knight[psq];
      break;
    case BISHOP:
      score += sign * pst_bishop[psq];
      break;
    case ROOK:
      score += sign * pst_rook[psq];
      break;
    case QUEEN:
      score += sign * TAPER(pst_queen_mg[psq],0,gp);
      break;
    case KING:
      score += sign * TAPER(pst_king_mg[psq],pst_king_eg[psq],gp);
      break;
    default:
      break;
    }
  }

  /* ------------------------------------------------------------------ */
  /* 2. PAWN STRUCTURE  (with pawn hash table cache)                     */
  /* ------------------------------------------------------------------ */
  U64 w_strong = 0ULL, b_strong = 0ULL;
  U64 w_passed_bb = 0ULL, b_passed_bb = 0ULL; /* for king-proximity below */

  /* Pawn file counts — needed by rook evaluation in section 3.
   * Cheap to compute from bitboards; don't bother caching. */
  int wpf[8] = {0}, bpf[8] = {0};
  {
    U64 tmp = bb_piece[PAWN] & bb_side[WHITE_PIECE];
    while (tmp){
      int s = bb_lsb(tmp);
      tmp &= tmp - 1;
      wpf[s % 8]++;
    }
    tmp = bb_piece[PAWN] & bb_side[BLACK_PIECE];
    while (tmp){
      int s = bb_lsb(tmp);
      tmp &= tmp - 1;
      bpf[s % 8]++;
    }
  }
  {
    /* Probe pawn hash */
    U64 ph = compute_pawn_hash();
    PHT_ENTRY* pe = &pht[ph & PHT_MASK];
    int pawn_score_cached = 0;

    if (pe->pawn_hash == ph && ph != 0){
      /* Cache hit: use stored values */
      score += pe->score;
      w_strong = pe->w_strong;
      b_strong = pe->b_strong;
      w_passed_bb = pe->w_passed;
      b_passed_bb = pe->b_passed;
      pawn_score_cached = 1;
    }

    if (! pawn_score_cached){
      int pawn_score = 0;
      int wprank[8][8] = {{0}};
      int bprank[8][8] = {{0}};

      for (sq = 0; sq < NSQUARE; sq++){
        if (who_on_square(sq)->name != PAWN) continue;
        int file = sq % 8, rank = sq / 8;
        if (who_on_square(sq)->color == WHITE_PIECE){
          wprank[file][rank]++;
        } else{
          bprank[file][rank]++;
        }
      }

      compute_strong_squares(WHITE_PIECE,&w_strong);
      compute_strong_squares(BLACK_PIECE,&b_strong);

      int file2;
      for (file2 = 0; file2 < 8; file2++){
        int wl = (file2 > 0)?wpf[file2 - 1]:0;
        int wr = (file2 < 7)?wpf[file2 + 1]:0;
        int bl = (file2 > 0)?bpf[file2 - 1]:0;
        int br = (file2 < 7)?bpf[file2 + 1]:0;

        if (wpf[file2] > 1)
          pawn_score += K_DOUBLED_PAWN * (wpf[file2] - 1);
        if (bpf[file2] > 1)
          pawn_score -= K_DOUBLED_PAWN * (bpf[file2] - 1);

        if (wpf[file2] > 0 && wl == 0 && wr == 0){
          pawn_score += K_ISOLATED_PAWN * wpf[file2];
          if (bpf[file2] == 0)
            pawn_score += K_ISOLATED_SEMIOPEN * wpf[file2];
        }
        if (bpf[file2] > 0 && bl == 0 && br == 0){
          pawn_score -= K_ISOLATED_PAWN * bpf[file2];
          if (wpf[file2] == 0)
            pawn_score -= K_ISOLATED_SEMIOPEN * bpf[file2];
        }

        if (file2 < 7){
          int rank2;
          for (rank2 = 0; rank2 < 8; rank2++){
            if (wprank[file2][rank2] > 0 && wprank[file2 + 1][rank2] > 0)
              pawn_score += K_PHALANGA;
            if (bprank[file2][rank2] > 0 && bprank[file2 + 1][rank2] > 0)
              pawn_score -= K_PHALANGA;
          }
        }
      }

      for (sq = 0; sq < NSQUARE; sq++){
        if (who_on_square(sq)->name != PAWN) continue;
        PIECE_COLOR pc = who_on_square(sq)->color;
        int sign = (pc == WHITE_PIECE)?1:-1;
        int file2 = sq % 8, rank2 = sq / 8;

        if (is_center_square(sq))
          pawn_score += sign * K_PAWN_CENTER;

        U64 atk = bb_atk_pawn[pc][sq];
        int natk = bb_popcount(atk);
        pawn_score += sign * K_PAWN_ATTACK * natk;

        int center_atk = bb_popcount(atk & (1ULL << 27 | 1ULL << 28 | 1ULL << 35 | 1ULL << 36));
        pawn_score += sign * K_PAWN_ATTACK_CENTER * center_atk;

        SQUARE_NUM eking = king_sq[1 - pc];
        U64 king_zone_p = bb_atk_king[eking] | (1ULL << eking);
        int king_atk = bb_popcount(atk & king_zone_p);
        pawn_score += sign * K_PAWN_ATK_KING * king_atk;

        if ((file2 == 5 || file2 == 2)){
          int block_rank = (pc == WHITE_PIECE)?1:6;
          if (rank2 == block_rank){
            int fwd = sq + (pc == WHITE_PIECE?8:-8);
            if (fwd >= 0 && fwd < 64 &&
              who_on_square(fwd)->name != NOPIECE &&
              who_on_square(fwd)->color != pc)
              pawn_score += sign * K_PAWN_BLOCK_FC;
          }
        }

        if ((file2 == 3 || file2 == 4)){
          int block_rank = (pc == WHITE_PIECE)?1:6;
          if (rank2 == block_rank){
            int fwd = sq + (pc == WHITE_PIECE?8:-8);
            if (fwd >= 0 && fwd < 64 &&
              who_on_square(fwd)->name != NOPIECE &&
              who_on_square(fwd)->color != pc)
              pawn_score += sign * K_PAWN_BLOCK_DE;
          }
        }

        {
          U64 own_strong = (pc == WHITE_PIECE)?w_strong:b_strong;
          int fwd = sq + (pc == WHITE_PIECE?8:-8);
          if (fwd >= 0 && fwd < 64 && (own_strong & (1ULL << fwd)))
            pawn_score += sign * K_BACKWARD_PAWN;
        }

        /* Candidate passer: if the pawn advances one square, would it be passed?
         * Only count if the square in front is empty and no enemy pawn directly
         * opposes on the same file ahead. */
        {
          int fwd2 = sq + (pc == WHITE_PIECE?8:-8);
          if (fwd2 >= 0 && fwd2 < 64 &&
            who_on_square(fwd2)->name == NOPIECE){
            int cand_rank = fwd2 / 8;
            int cand_file = fwd2 % 8;
            int cfl = (cand_file > 0)?cand_file - 1:cand_file;
            int cfr = (cand_file < 7)?cand_file + 1:cand_file;
            int is_candidate = 1;
            int cr;
            if (pc == WHITE_PIECE){
              for (cr = cand_rank; cr < 8 && is_candidate; cr++){
                int cf;
                for (cf = cfl; cf <= cfr; cf++)
                  if (who_on_square(cr * 8 + cf)->name == PAWN &&
                    who_on_square(cr * 8 + cf)->color == BLACK_PIECE){
                    is_candidate = 0;
                    break;
                  }
              }
            } else{
              for (cr = cand_rank; cr >= 0 && is_candidate; cr--){
                int cf;
                for (cf = cfl; cf <= cfr; cf++)
                  if (who_on_square(cr * 8 + cf)->name == PAWN &&
                    who_on_square(cr * 8 + cf)->color == WHITE_PIECE){
                    is_candidate = 0;
                    break;
                  }
              }
            }
            if (is_candidate)
              pawn_score += sign * K_CANDIDATE_PASSER;
          }
        }

        /* Passed pawn detection — cache the rank bonus only */
        {
          int passed = 1;
          int fl = (file2 > 0)?file2 - 1:file2;
          int fr = (file2 < 7)?file2 + 1:file2;
          int r2;
          if (pc == WHITE_PIECE){
            for (r2 = rank2 + 1; r2 < 8 && passed; r2++){
              int f3;
              for (f3 = fl; f3 <= fr; f3++)
                if (who_on_square(r2 * 8 + f3)->name == PAWN &&
                  who_on_square(r2 * 8 + f3)->color == BLACK_PIECE){
                  passed = 0;
                  break;
                }
            }
            if (passed){
              w_passed_bb |= (1ULL << sq);
              static const int pp_rank_bonus[8] = {0,5,15,30,55,90,140,0};
              pawn_score += pp_rank_bonus[rank2] * (128 + (256 - gp)) / 256;
            }
          } else{
            for (r2 = rank2 - 1; r2 >= 0 && passed; r2--){
              int f3;
              for (f3 = fl; f3 <= fr; f3++)
                if (who_on_square(r2 * 8 + f3)->name == PAWN &&
                  who_on_square(r2 * 8 + f3)->color == WHITE_PIECE){
                  passed = 0;
                  break;
                }
            }
            if (passed){
              b_passed_bb |= (1ULL << sq);
              static const int pp_rank_bonus_b[8] = {0,140,90,55,30,15,5,0};
              pawn_score -= pp_rank_bonus_b[rank2] * (128 + (256 - gp)) / 256;
            }
          }
        }
      }

      pawn_score += K_STRONG_SQUARE * bb_popcount(w_strong);
      pawn_score -= K_STRONG_SQUARE * bb_popcount(b_strong);

      /* Store in pawn hash table */
      pe->pawn_hash = ph;
      pe->score = pawn_score;
      pe->w_passed = w_passed_bb;
      pe->b_passed = b_passed_bb;
      pe->w_strong = w_strong;
      pe->b_strong = b_strong;

      score += pawn_score;
    }

    /* ------------------------------------------------------------------
     * Passed pawn piece-dependent bonuses (NOT cached — use piece/king
     * positions that change each call).
     * ------------------------------------------------------------------ */
    U64 tmp_pp;

    /* White passed pawns */
    tmp_pp = w_passed_bb;
    while (tmp_pp){
      int sq2 = bb_lsb(tmp_pp);
      tmp_pp &= tmp_pp - 1;
      int file2 = sq2 % 8, rank2 = sq2 / 8;

      /* Own king supports the passer */
      if (king_distance(king_sq[WHITE_PIECE],sq2) <= 2) score += 15;

      int blocker = sq2 + 8;
      if (blocker < 64 && who_on_square(blocker)->name != NOPIECE){
        score += K_PASSED_PAWN_BLOCK;
        if (is_square_attacked(blocker, WHITE_PIECE))
          score += K_PASS_BLOCK_PROFIT;
      }
      int r2;
      for (r2 = rank2 + 1; r2 < 8; r2++){
        if (is_square_attacked(r2 * 8 + file2, WHITE_PIECE))
          score += K_PASS_TRAJ_ATK;
      }
      for (r2 = rank2 - 1; r2 >= 0; r2--){
        SQUARE_NUM rsq = r2 * 8 + file2;
        if (who_on_square(rsq)->name == ROOK){
          score += (who_on_square(rsq)->color == WHITE_PIECE)
            ?K_ROOK_BEHIND_PASSER:K_ROOK_BEHIND_ENEMY_P;
          break;
        }
      }
      score += K_KING_DIST_EG * king_distance(king_sq[BLACK_PIECE],file2 + 7 * 8)
        * (256 - gp) / 256;
    }

    /* Black passed pawns */
    tmp_pp = b_passed_bb;
    while (tmp_pp){
      int sq2 = bb_lsb(tmp_pp);
      tmp_pp &= tmp_pp - 1;
      int file2 = sq2 % 8, rank2 = sq2 / 8;

      if (king_distance(king_sq[BLACK_PIECE],sq2) <= 2) score -= 15;

      int blocker = sq2 - 8;
      if (blocker >= 0 && who_on_square(blocker)->name != NOPIECE){
        score -= K_PASSED_PAWN_BLOCK;
        if (is_square_attacked(blocker, BLACK_PIECE))
          score -= K_PASS_BLOCK_PROFIT;
      }
      int r2;
      for (r2 = rank2 - 1; r2 >= 0; r2--){
        if (is_square_attacked(r2 * 8 + file2, BLACK_PIECE))
          score -= K_PASS_TRAJ_ATK;
      }
      for (r2 = rank2 + 1; r2 < 8; r2++){
        SQUARE_NUM rsq = r2 * 8 + file2;
        if (who_on_square(rsq)->name == ROOK){
          score -= (who_on_square(rsq)->color == BLACK_PIECE)
            ?K_ROOK_BEHIND_PASSER:K_ROOK_BEHIND_ENEMY_P;
          break;
        }
      }
      score -= K_KING_DIST_EG * king_distance(king_sq[WHITE_PIECE],file2 + 0 * 8)
        * (256 - gp) / 256;
    }

    /* ---------------------------------------------------------------- */
    /* 3. PIECE EVALUATION                                               */
    /* ---------------------------------------------------------------- */
    int w_bishops = 0, b_bishops = 0, w_knights = 0, b_knights = 0;
    int w_queens = 0, b_queens = 0;

    static const struct{
      int sq;
      PIECE_NAME name;
      PIECE_COLOR color;
    } init_sq[] = {
      {1, KNIGHT, WHITE_PIECE},{6, KNIGHT, WHITE_PIECE},
      {2, BISHOP, WHITE_PIECE},{5, BISHOP, WHITE_PIECE},
      {57, KNIGHT, BLACK_PIECE},{62, KNIGHT, BLACK_PIECE},
      {58, BISHOP, BLACK_PIECE},{61, BISHOP, BLACK_PIECE},
    };
    int ns = sizeof(init_sq) / sizeof(init_sq[0]);
    int ii;
    for (ii = 0; ii < ns; ii++){
      if (who_on_square(init_sq[ii].sq)->name == init_sq[ii].name &&
        who_on_square(init_sq[ii].sq)->color == init_sq[ii].color){
        int sign2 = (init_sq[ii].color == WHITE_PIECE)?1:-1;
        score += sign2 * K_PIECE_INIT_SQ;
      }
    }

    for (sq = 0; sq < NSQUARE; sq++){
      PIECE_NAME pn = who_on_square(sq)->name;
      PIECE_COLOR pc = who_on_square(sq)->color;
      if (pn == NOPIECE) continue;
      int sign2 = (pc == WHITE_PIECE)?1:-1;
      int file2b = sq % 8, rank2b = sq / 8;
      PIECE_COLOR enemy = 1 - pc;
      U64 own_strong2 = (pc == WHITE_PIECE)?w_strong:b_strong;
      U64 enemy_strong = (pc == WHITE_PIECE)?b_strong:w_strong;

      if (pn == BISHOP){
        if (pc == WHITE_PIECE) w_bishops++;
        else b_bishops++;

        if (own_strong2 & (1ULL << sq))
          score += sign2 * K_BISHOP_STRONG;

        if (! endgame){
          SQUARE_NUM ok = king_sq[pc];
          U64 king_z = bb_atk_king[ok] | (1ULL << ok);
          U64 batk = bb_bishop_attacks(sq,bb_side[0] | bb_side[1]);
          if (batk & king_z)
            score += sign2 * TAPER(K_BN_ATK_NEAR_KING,0,gp);
          score += sign2 * TAPER(K_BN_ATK_STRONG_SQ,0,gp) *
            bb_popcount(batk & enemy_strong);
        }

        {
          U64 batk = bb_bishop_attacks(sq,bb_side[0] | bb_side[1]);
          U64 enemy_rq = (bb_piece[ROOK] | bb_piece[QUEEN]) & bb_side[enemy];
          if (batk & enemy_rq) score += sign2 * K_BN_ATK_RQ;
          if (batk & bb_side[enemy]){
            score += sign2 * K_BISHOP_ATK_PIECE *
              bb_popcount(batk & bb_side[enemy]);
          }
          score += sign2 * K_PIECE_ATK_BY_BISHOP *
            bb_popcount(batk & bb_side[enemy] &
              ~bb_piece[PAWN] & ~bb_piece[KING]);
          score += sign2 * K_PIECE_ATK_CENTER *
            bb_popcount(batk & (1ULL << 27 | 1ULL << 28 | 1ULL << 35 | 1ULL << 36));

          /* Safe mobility: squares not attacked by enemy pawns */
          U64 ep = bb_atk_pawn[enemy][0]; /* dummy init */
          { /* build enemy pawn attacks */
            ep = 0ULL;
            U64 tmp2 = bb_piece[PAWN] & bb_side[enemy];
            while (tmp2){
              int s2 = bb_lsb(tmp2);
              tmp2 &= tmp2 - 1;
              ep |= bb_atk_pawn[enemy][s2];
            }
          }
          int safe_mob = bb_popcount(batk & ~ep & ~bb_side[pc]);
          score += sign2 * K_MOB_BISHOP * safe_mob;
        }

        /* Bishop outpost: bishop on enemy-half strong square */
        if (own_strong2 & (1ULL << sq)){
          if ((pc == WHITE_PIECE && sq / 8 >= 4) ||
            (pc == BLACK_PIECE && sq / 8 <= 3))
            score += sign2 * K_BISHOP_OUTPOST;
        }
      } else if (pn == KNIGHT){
        if (pc == WHITE_PIECE) w_knights++;
        else b_knights++;

        if (own_strong2 & (1ULL << sq))
          score += sign2 * K_KNIGHT_STRONG;
        if (is_center_square(sq))
          score += sign2 * K_KNIGHT_CENTER;

        {
          int kf = sq % 8, kr = sq / 8;
          if (kf == 0 || kf == 7 || kr == 0 || kr == 7)
            score += sign2 * K_KNIGHT_ON_RIM;
          else if (kf == 1 || kf == 6 || kr == 1 || kr == 6)
            score += sign2 * K_KNIGHT_NEAR_RIM;
        }

        /* Knight outpost bonus */
        if ((pc == WHITE_PIECE && rank2b >= 4) ||
          (pc == BLACK_PIECE && rank2b <= 3)){
          U64 support = bb_atk_pawn[pc][sq];
          if (support & bb_piece[PAWN] & bb_side[pc]){
            score += sign2 * K_KNIGHT_OUTPOST;
          }
        }

        U64 natk = bb_atk_knight[sq];
        if (! endgame){
          SQUARE_NUM ok = king_sq[pc];
          U64 king_z = bb_atk_king[ok] | (1ULL << ok);
          if (natk & king_z) score += sign2 * TAPER(K_BN_ATK_NEAR_KING,0,gp);
          score += sign2 * TAPER(K_BN_ATK_STRONG_SQ,0,gp) *
            bb_popcount(natk & enemy_strong);
        }
        U64 enemy_rq = (bb_piece[ROOK] | bb_piece[QUEEN]) & bb_side[enemy];
        if (natk & enemy_rq) score += sign2 * K_BN_ATK_RQ;
        score += sign2 * K_PIECE_ATK_CENTER *
          bb_popcount(natk & (1ULL << 27 | 1ULL << 28 | 1ULL << 35 | 1ULL << 36));

        /* Safe mobility */
        {
          U64 ep2 = 0ULL;
          U64 tmp3 = bb_piece[PAWN] & bb_side[enemy];
          while (tmp3){
            int s3 = bb_lsb(tmp3);
            tmp3 &= tmp3 - 1;
            ep2 |= bb_atk_pawn[enemy][s3];
          }
          int safe_mob2 = bb_popcount(natk & ~ep2 & ~bb_side[pc]);
          score += sign2 * K_MOB_KNIGHT * safe_mob2;
        }
      } else if (pn == ROOK){
        int seventh = (pc == WHITE_PIECE)?6:1;
        if (rank2b == seventh){
          score += sign2 * TAPER(K_ROOK_7TH,K_ROOK_7TH_EG,gp);
        }
        int own_pawns_here = (pc == WHITE_PIECE)?wpf[file2b]:bpf[file2b];
        int enemy_pawns_here = (pc == WHITE_PIECE)?bpf[file2b]:wpf[file2b];
        if (own_pawns_here == 0 && enemy_pawns_here == 0){
          score += sign2 * K_ROOK_OPEN;
        } else if (own_pawns_here == 0){
          score += sign2 * K_ROOK_SEMIOPEN;
        }
        U64 occ = bb_side[0] | bb_side[1];
        U64 ratk = bb_rook_attacks(sq,occ);
        U64 file_mask = 0ULL;
        {
          int r3;
          for (r3 = 0; r3 < 8; r3++) file_mask |= (1ULL << (r3 * 8 + file2b));
        }
        U64 ratk_file = ratk & file_mask;
        if (own_pawns_here == 0 && enemy_pawns_here == 0)
          score += sign2 * K_ROOK_ATK_OPEN * bb_popcount(ratk_file);
        else if (own_pawns_here == 0)
          score += sign2 * K_ROOK_ATK_SEMIOPEN * bb_popcount(ratk_file);
        score += sign2 * K_PIECE_ATK_CENTER *
          bb_popcount(ratk & (1ULL << 27 | 1ULL << 28 | 1ULL << 35 | 1ULL << 36));

        /* Safe mobility */
        {
          U64 ep3 = 0ULL;
          U64 tmp4 = bb_piece[PAWN] & bb_side[enemy];
          while (tmp4){
            int s4 = bb_lsb(tmp4);
            tmp4 &= tmp4 - 1;
            ep3 |= bb_atk_pawn[enemy][s4];
          }
          int safe_mob3 = bb_popcount(ratk & ~ep3 & ~bb_side[pc]);
          score += sign2 * K_MOB_ROOK * safe_mob3;
        }

        /* Rook connectivity: bonus if another own rook is visible on same rank/file */
        {
          U64 own_rooks = bb_piece[ROOK] & bb_side[pc] & ~(1ULL << sq);
          if (own_rooks & ratk)
            score += sign2 * K_ROOK_CONNECTED;
        }
      } else if (pn == QUEEN){
        if (pc == WHITE_PIECE) w_queens++;
        else b_queens++;
        U64 occ = bb_side[0] | bb_side[1];
        U64 qatk = bb_rook_attacks(sq,occ) | bb_bishop_attacks(sq,occ);
        score += sign2 * K_PIECE_ATK_CENTER *
          bb_popcount(qatk & (1ULL << 27 | 1ULL << 28 | 1ULL << 35 | 1ULL << 36));
        /* Safe queen mobility */
        {
          U64 ep4 = 0ULL;
          U64 tmp5 = bb_piece[PAWN] & bb_side[enemy];
          while (tmp5){
            int s5 = bb_lsb(tmp5);
            tmp5 &= tmp5 - 1;
            ep4 |= bb_atk_pawn[enemy][s5];
          }
          int safe_mob4 = bb_popcount(qatk & ~ep4 & ~bb_side[pc]);
          score += sign2 * K_MOB_QUEEN * safe_mob4;
        }
      }

      if (pn != KING && pn != PAWN){
        if (is_piece_pinned(sq))
          score -= sign2 * K_BOUND_PIECE;
      }

      if (pn != KING && pn != PAWN){
        U64 occ2 = bb_side[0] | bb_side[1];
        U64 piece_atk = 0ULL;
        switch (pn){
        case KNIGHT: piece_atk = bb_atk_knight[sq];
          break;
        case BISHOP: piece_atk = bb_bishop_attacks(sq,occ2);
          break;
        case ROOK: piece_atk = bb_rook_attacks(sq,occ2);
          break;
        case QUEEN: piece_atk = bb_rook_attacks(sq,occ2) | bb_bishop_attacks(sq,occ2);
          break;
        default: break;
        }
        U64 targets = piece_atk & bb_side[enemy] & ~bb_piece[PAWN] & ~bb_piece[KING];
        int profitable = 0;
        U64 t = targets;
        while (t){
          int tsq = bb_lsb(t);
          t &= t - 1;
          if (! is_square_attacked(tsq,enemy)) profitable++;
        }
        if (profitable >= 2) score += sign2 * K_DOUBLE_PROFIT_ATK;
        else if (profitable == 1) score += sign2 * K_PROFITABLE_ATK;
      }

      /* Hanging piece: lightweight check using is_square_attacked only.
       * SEE is too expensive to call from static_evaluate (it does full
       * save/restore_position per call — O(n²) SEE calls per eval node
       * made the engine hang for minutes in complex endgames).
       * Instead: if this piece is attacked by any enemy piece AND not
       * defended by any own piece, apply a flat penalty. */
      if (pn != KING && pn != PAWN){
        if (is_square_attacked(sq,enemy) &&
          ! is_square_attacked(sq,pc)){
          /* Completely undefended and attacked — flat penalty scaled
           * by piece value so losing a queen matters more than a pawn */
          score -= sign2 * (piece_cost[pn] / 10);
        }
      }
    }

    /* Bishop pair: most valuable in open/endgame positions (taper MG→EG) */
    if (w_bishops >= 2) score += TAPER(20,40,gp);
    if (b_bishops >= 2) score -= TAPER(20,40,gp);
    if (w_knights >= 1 && w_queens >= 1) score += K_KNIGHT_QUEEN;
    if (b_knights >= 1 && b_queens >= 1) score -= K_KNIGHT_QUEEN;

    /* Bishop colour weakness: penalise when own pawns clog own bishop's diagonals.
     * Count own pawns on same colour complex as each bishop.
     * Light squares (sq%2==0 for sq where (file+rank)%2==0), dark = 1. */
    if (! endgame){
      int sq2;
      for (sq2 = 0; sq2 < NSQUARE; sq2++){
        if (who_on_square(sq2)->name != BISHOP) continue;
        PIECE_COLOR bc = who_on_square(sq2)->color;
        int sign3 = (bc == WHITE_PIECE)?1:-1;
        int bcolor = (sq2 % 2); /* 0=light 1=dark square */
        int own_p_on_color = 0;
        int sq3;
        for (sq3 = 0; sq3 < NSQUARE; sq3++){
          if (who_on_square(sq3)->name == PAWN &&
            who_on_square(sq3)->color == bc &&
            (sq3 % 2) == bcolor)
            own_p_on_color++;
        }
        /* Each pawn on the bishop's colour reduces its scope slightly */
        score -= sign3 * own_p_on_color * 3;
      }
    }

    /* Space advantage: count safe squares in enemy territory (ranks 5-7 for
     * White, ranks 0-2 for Black) that are attacked but not occupied by our own
     * pieces and not controlled by enemy pawns.  Matters mostly in MG. */
    if (! endgame){
      U64 wp_atk = 0ULL, bp_atk = 0ULL;
      {
        U64 wpawns = bb_piece[PAWN] & bb_side[WHITE_PIECE];
        U64 bpawns = bb_piece[PAWN] & bb_side[BLACK_PIECE];
        U64 t;
        t = wpawns;
        while (t){
          int s = bb_lsb(t);
          t &= t - 1;
          wp_atk |= bb_atk_pawn[WHITE_PIECE][s];
        }
        t = bpawns;
        while (t){
          int s = bb_lsb(t);
          t &= t - 1;
          bp_atk |= bb_atk_pawn[BLACK_PIECE][s];
        }
      }
      /* White space: ranks 4-6 (bit masks for ranks 5-7 in 0-indexed) */
      U64 w_space_mask = 0x00FFFFFF00000000ULL; /* ranks 4-6 (bits 32-55) */
      U64 b_space_mask = 0x0000000000FFFFFFULL; /* ranks 0-2 (bits 0-23)  */
      int w_space = bb_popcount(w_space_mask & ~bb_side[WHITE_PIECE] & ~bp_atk);
      int b_space = bb_popcount(b_space_mask & ~bb_side[BLACK_PIECE] & ~wp_atk);
      score += (w_space - b_space) * TAPER(K_SPACE,0,gp);
    }
  }

  /* ------------------------------------------------------------------ */
  /* 4. KING SAFETY / CASTLING - ENHANCED                                */
  /* ------------------------------------------------------------------ */
  {
    static const struct{
      int sq;
      PIECE_NAME name;
      PIECE_COLOR color;
    }
    home_sq[] = {
      {3, QUEEN, WHITE_PIECE},{59, QUEEN, BLACK_PIECE},
      {2, BISHOP, WHITE_PIECE},{5, BISHOP, WHITE_PIECE},
      {58, BISHOP, BLACK_PIECE},{61, BISHOP, BLACK_PIECE},
      {1, KNIGHT, WHITE_PIECE},{6, KNIGHT, WHITE_PIECE},
      {57, KNIGHT, BLACK_PIECE},{62, KNIGHT, BLACK_PIECE},
    };
    int w_developed = 0, b_developed = 0;
    {
      int hh;
      for (hh = 0; hh < (int)(sizeof(home_sq) / sizeof(home_sq[0])); hh++){
        WOOD_PIECE* w = who_on_square(home_sq[hh].sq);
        if (w->name != home_sq[hh].name || w->color != home_sq[hh].color){
          if (home_sq[hh].color == WHITE_PIECE) w_developed++;
          else b_developed++;
        }
      }
    }

    int c;
    for (c = 0; c < 2; c++){
      int sign = (c == WHITE_PIECE)?1:-1;
      int developed = (c == WHITE_PIECE)?w_developed:b_developed;

      if (castle[c] & (CASTLELEFTDONE | CASTLERIGHTDONE)){
        score += sign * K_CASTLE_DONE;
        /* Castling early matters most in the opening; taper to zero in EG */
        score += sign * TAPER(K_CASTLE_EARLY,0,gp);
      } else if (castle[c] & (CASTLELEFT | CASTLERIGHT)){
        int capped = (developed > 5)?5:developed;
        /* Delay penalty matters only when there is a king to attack */
        score += sign * TAPER(K_CASTLE_DELAY,0,gp) * capped;
        if (developed >= 2){
          score += sign * TAPER(K_UNCASTLED_PENALTY,0,gp) * (developed - 1);
        }
      } else{
        score += sign * K_CASTLE_LOST;
      }

      {
        /* King-safety: full weight in opening/middlegame, fades to zero in EG */
        SQUARE_NUM ksq = king_sq[c];
        U64 occ = bb_side[0] | bb_side[1];
        U64 qmob = bb_rook_attacks(ksq,occ) | bb_bishop_attacks(ksq,occ);
        score += sign * TAPER(K_QUEEN_MOB_KING,0,gp) * bb_popcount(qmob);
        score += sign * (evaluate_king_safety(c) * gp / 256);
      }
    }
  }

  /* ------------------------------------------------------------------ */
  /* 5. ENDGAME SPECIFICS                                                 */
  /* ------------------------------------------------------------------ */
  if (endgame){
    int w_mat = 0, b_mat = 0;
    for (sq = 0; sq < NSQUARE; sq++){
      PIECE_NAME pn = who_on_square(sq)->name;
      PIECE_COLOR pc = who_on_square(sq)->color;
      if (pn == NOPIECE || pn == KING) continue;
      if (pc == WHITE_PIECE) w_mat += piece_cost[pn];
      else b_mat += piece_cost[pn];
    }
    int mat_diff = w_mat - b_mat;
    if (mat_diff != 0){
      PIECE_COLOR win_side = (mat_diff > 0)?WHITE_PIECE:BLACK_PIECE;
      PIECE_COLOR lose_side = 1 - win_side;
      int sign = (win_side == WHITE_PIECE)?1:-1;
      /* Endgame king-manoeuvring: full weight when gp=0, zero when gp=256 */
      int eg_weight = 256 - gp;

      score += sign * (-K_KING_DIST_CENTER) *
        centre_manhattan(king_sq[lose_side]) * eg_weight / (6 * 256);

      int kd = king_distance(king_sq[win_side],king_sq[lose_side]);
      score += sign * (-K_KING_DIST) * (14 - kd) * eg_weight / (14 * 256);

      if (kd == 2)
        score += sign * K_KING_OPP_EG * eg_weight / 256;
    }

    score += kp_vs_k_bonus() * (256 - gp) / 256;

    /* Corner drive: when winning, reward pushing the losing king to a corner.
     * centre_manhattan gives 0 at centre → 6 at corner; negate so corner = big. */
    if (mat_diff != 0){
      PIECE_COLOR win_side2 = (mat_diff > 0)?WHITE_PIECE:BLACK_PIECE;
      PIECE_COLOR lose_side2 = 1 - win_side2;
      int sign2 = (win_side2 == WHITE_PIECE)?1:-1;
      int corner_bonus = centre_manhattan(king_sq[lose_side2]) * 4;
      score += sign2 * corner_bonus * (256 - gp) / 256;
    }
  }

  /* ------------------------------------------------------------------ */
  /* 6. DEVELOPMENT & ACTIVITY BONUS - SIGNIFICANTLY ENHANCED            */
  /* ------------------------------------------------------------------ */
  {
    int w_developed_minor = 0, b_developed_minor = 0;
    int w_center_attacks = 0, b_center_attacks = 0;
    U64 center_squares = 1ULL << 27 | 1ULL << 28 | 1ULL << 35 | 1ULL << 36;

    for (sq = 0; sq < 64; sq++){
      PIECE_NAME pn = who_on_square(sq)->name;
      PIECE_COLOR pc = who_on_square(sq)->color;
      int rank = sq / 8;

      if (pn == KNIGHT || pn == BISHOP){
        if (pc == WHITE_PIECE && rank > 0){
          w_developed_minor++;
        } else if (pc == BLACK_PIECE && rank < 7){
          b_developed_minor++;
        }
      }

      /* Center attacks for all pieces except king */
      if (pn != NOPIECE && pn != KING){
        U64 attacks = 0;
        U64 occ = bb_side[0] | bb_side[1];
        switch (pn){
        case KNIGHT: attacks = bb_atk_knight[sq];
          break;
        case BISHOP: attacks = bb_bishop_attacks(sq,occ);
          break;
        case ROOK: attacks = bb_rook_attacks(sq,occ);
          break;
        case QUEEN: attacks = bb_rook_attacks(sq,occ) | bb_bishop_attacks(sq,occ);
          break;
        default: break;
        }
        int center_atk = bb_popcount(attacks & center_squares);
        if (pc == WHITE_PIECE){
          w_center_attacks += center_atk;
        } else{
          b_center_attacks += center_atk;
        }
      }
    }

    /* Development bonuses: opening/middlegame only — taper to zero in EG */
    score += (w_developed_minor - b_developed_minor) * (K_DEVELOPMENT_BONUS * gp / 256);

    /* Extra for development advantage */
    int dev_adv = w_developed_minor - b_developed_minor;
    if (dev_adv > 0){
      score += (K_DEVELOPMENT_ADVANTAGE * gp / 256) * dev_adv;
    } else if (dev_adv < 0){
      score -= (K_DEVELOPMENT_ADVANTAGE * gp / 256) * (-dev_adv);
    }

    /* Center control: also opening/middlegame weighted */
    score += (w_center_attacks - b_center_attacks) * (K_CENTER_CONTROL * gp / 256);

    /* Penalize queen retreats to back rank */
    int w_queen_sq = -1, b_queen_sq = -1;
    for (sq = 0; sq < 64; sq++){
      if (who_on_square(sq)->name == QUEEN){
        if (who_on_square(sq)->color == WHITE_PIECE) w_queen_sq = sq;
        else b_queen_sq = sq;
      }
    }
    if (w_queen_sq >= 0 && (w_queen_sq / 8) == 0 && w_queen_sq != 3){
      score += K_QUEEN_RETREAT_PENALTY;
    }
    if (b_queen_sq >= 0 && (b_queen_sq / 8) == 7 && b_queen_sq != 59){
      score -= K_QUEEN_RETREAT_PENALTY;
    }
  }

  /* Tempo bonus - side to move advantage */
  score += (move_color == WHITE_PIECE)?K_TEMPO_BONUS:-K_TEMPO_BONUS;

  /* Score is from White's perspective; negate for Black to move */
  if (move_color != WHITE_PIECE)
    score = -score;

  return score;
}

/* Add this function to help order moves when winning */
static int is_stalemating_move(MOVE* move){
  /* Make the move temporarily and check if opponent would be stalemated */
  if (make_move(move) != 0)
    return 0;

  /* After make_move(), move_color is the opponent.  Generate their legal
   * moves and check whether their king is in check. */
  MOVE test_moves[256];
  MOVE *first = test_moves, *last = test_moves;
  generate_legal_moves(&first,&last);

  /* Stalemate: opponent has no legal moves AND their king is not in check */
  int is_stalemate = (last == first &&
    ! is_square_attacked(king_sq[move_color],1 - move_color));

  take_back_move();
  return is_stalemate;
}

/*=============================================================================
 * Root-Only Stalemate Avoidance - CORRECTED
 *=============================================================================*/
/* Simple winning position check - no cache: called once per root, fast enough */
static int is_winning_position_simple(void){
  int total_white = 0, total_black = 0;
  int sq;

  for (sq = 0; sq < NSQUARE; sq++){
    PIECE_NAME name = who_on_square(sq)->name;
    if (name == NOPIECE || name == KING) continue;

    if (who_on_square(sq)->color == WHITE_PIECE)
      total_white += piece_cost[name];
    else
      total_black += piece_cost[name];

    if (move_color == WHITE_PIECE){
      if (total_white - total_black >= ROOK_COST){
        return 1;
      }
    } else{
      if (total_black - total_white >= ROOK_COST){
        return 1;
      }
    }
  }

  int material_advantage = (move_color == WHITE_PIECE)?
    (total_white - total_black):
    (total_black - total_white);

  if (is_endgame() && material_advantage >= PAWN_COST * 2){
    return 1;
  }

  return (material_advantage >= ROOK_COST);
}

/* Check if a specific move would cause stalemate - with validation */
static int move_causes_stalemate(MOVE* move){
  /* Validate move is not a self-move */
  if (move->from == move->to){
    return 1; /* Treat as invalid/stalemating */
  }

  /* Make the move */
  if (make_move(move) != 0){
    return 1; /* Invalid move - treat as stalemating */
  }

  /* After make_move(), move_color has already been flipped to the opponent
   * (the side that now needs to reply).  Generate THEIR legal moves to see
   * whether they are stalemated.  The old code did an extra flip here which
   * caused generate_legal_moves() to run for the ORIGINAL mover instead. */
  MOVE test_moves[256];
  MOVE *first = test_moves, *last = test_moves;
  generate_legal_moves(&first,&last);
  int opponent_has_moves = (last > first);
  /* Check: opponent's king in check? (check ≠ stalemate) */
  int king_attacked = is_square_attacked(king_sq[move_color],1 - move_color);

  take_back_move();

  /* Stalemate if opponent has no moves AND king not in check */
  return (! opponent_has_moves && ! king_attacked);
}

/* Filter stalemating moves from the move list at root only */
static int filter_stalemating_moves(MOVE* moves, int num_moves, MOVE* filtered_moves, MOVE* best_alternative){
  if (! is_winning_position_simple()){
    /* Not winning - keep all moves */
    memcpy(filtered_moves,moves,num_moves * sizeof(MOVE));
    return num_moves;
  }

  int filtered_count = 0;

  for (int i = 0; i < num_moves; i++){
    /* Skip invalid moves */
    if (moves[i].from == moves[i].to){
      continue;
    }

    if (! move_causes_stalemate(&moves[i])){
      filtered_moves[filtered_count++] = moves[i];
    }
  }

  /* If all moves would stalemate, we have a problem.
   * In this case, return the original move list but mark the best alternative */
  if (filtered_count == 0){
    /* Find the best move from original list (avoid a1a1 type moves) */
    MOVE best_move;
    best_move.from = DUMMY;

    for (int i = 0; i < num_moves; i++){
      if (moves[i].from != moves[i].to){
        if (best_move.from == DUMMY){
          best_move = moves[i];
        }
        /* Prefer captures or promotions */
        if (moves[i].flags & (M_CAPTURE | M_PROMOTION)){
          best_move = moves[i];
          break;
        }
      }
    }

    if (best_move.from != DUMMY){
      *best_alternative = best_move;
      filtered_moves[0] = best_move;
      return 1;
    }

    /* Last resort - return original but filtered of self-moves */
    filtered_count = 0;
    for (int i = 0; i < num_moves; i++){
      if (moves[i].from != moves[i].to){
        filtered_moves[filtered_count++] = moves[i];
      }
    }
  }

  return filtered_count;
}

/*=============================================================================
 * Evaluate position with stalemate awareness
 * Returns evaluation with penalty for positions that might lead to stalemate
 * when winning
 *=============================================================================*/
static int evaluate_with_stalemate_awareness(void){
  int base_eval = static_evaluate();

  /* If we're not winning, no need for stalemate avoidance */
  if (! is_winning_position_simple())
    return base_eval;

  /* Check if we're about to stalemate the opponent */
  int our_color = move_color;
  int enemy_color = 1 - our_color;

  /* Temporarily switch to opponent's perspective to check for stalemate */
  PIECE_COLOR saved_color = move_color;
  move_color = enemy_color;

  MOVE test_moves[256];
  MOVE *first = test_moves, *last = test_moves;
  generate_legal_moves(&first,&last);
  int enemy_has_moves = (last > first);

  move_color = saved_color;

  /* If enemy has no legal moves and their king is not in check -> stalemate */
  if (! enemy_has_moves && ! is_square_attacked(king_sq[enemy_color],our_color)){
    /* Stalemate! Apply a penalty based on how winning we are */
    int material_adv = abs(base_eval) / 100; /* Rough material advantage */
    int penalty = 200 + material_adv * 50; /* Larger penalty = avoid stalemate */
    return base_eval - penalty;
  }

  return base_eval;
}

/* Modify alpha_beta_simple to use stalemate-aware evaluation */
/* Find this section in alpha_beta_simple (around line 1380-1400) and replace: */
/* The original code at the beginning of alpha_beta_simple has:
 *   int check = in_check();
 *   ...
 *   if (depth - ext <= 0 || stop_search) {
 *       return quiescence_simple(alpha, beta);
 *   }
 */
/* Replace the quiescence call with this version that checks for stalemate first: */
/*=============================================================================
 * Static Exchange Evaluation (SEE)
 *=============================================================================*/
static int piece_attacks_sq(
  SQUARE_NUM from, SQUARE_NUM to,
  PIECE_NAME name, PIECE_COLOR color){
  int df = (to % 8) - (from % 8);
  int dr = (to / 8) - (from / 8);
  int adf = abs(df), adr = abs(dr);
  int step, sq2;

  if (from == to) return 0;

  switch (name){
  case PAWN:
    return (adf == 1) &&
      (color == WHITE_PIECE?dr == 1:dr == -1);
  case KNIGHT:
    return (adf == 1 && adr == 2) || (adf == 2 && adr == 1);
  case KING:
    return adf <= 1 && adr <= 1;
  case BISHOP:
    if (adf != adr || adf == 0) return 0;
    step = (df > 0?1:-1) + (dr > 0?8:-8);
    for (sq2 = from + step; sq2 != to; sq2 += step)
      if (who_on_square(sq2)->name != NOPIECE) return 0;
    return 1;
  case ROOK:
    if (df != 0 && dr != 0) return 0;
    step = (df != 0)?(df > 0?1:-1):(dr > 0?8:-8);
    for (sq2 = from + step; sq2 != to; sq2 += step)
      if (who_on_square(sq2)->name != NOPIECE) return 0;
    return 1;
  case QUEEN:
    {
      int sf, sr;
      if (df != 0 && dr != 0 && adf != adr) return 0;
      sf = (df == 0)?0:(df > 0?1:-1);
      sr = (dr == 0)?0:(dr > 0?8:-8);
      step = sf + sr;
      if (step == 0) return 0;
      for (sq2 = from + step; sq2 != to; sq2 += step)
        if (who_on_square(sq2)->name != NOPIECE) return 0;
      return 1;
    }
  default: return 0;
  }
}

static int see_rec(SQUARE_NUM to_sq, PIECE_COLOR side){
  int lva_val = POS_INFINITY;
  SQUARE_NUM lva_sq = DUMMY;
  PIECE_NAME lva_name = NOPIECE;
  SQUARE_NUM sq2;
  WOOD_PIECE saved_to, saved_from;
  int gain;

  for (sq2 = 0; sq2 < NSQUARE; sq2++){
    WOOD_PIECE* w = who_on_square(sq2);
    if (w->name == NOPIECE || w->color != side) continue;
    if (piece_attacks_sq(sq2,to_sq,w->name,w->color)){
      if (see_val[w->name] < lva_val){
        lva_val = see_val[w->name];
        lva_sq = sq2;
        lva_name = w->name;
      }
    }
  }

  if (lva_sq == DUMMY) return 0;

  int target_val = see_val[who_on_square(to_sq)->name];

  saved_to = pos_sp->n.square[to_sq].wood;
  saved_from = pos_sp->n.square[lva_sq].wood;

  pos_sp->n.square[to_sq].wood.name = lva_name;
  pos_sp->n.square[to_sq].wood.color = side;
  pos_sp->n.square[to_sq].wood.cost = lva_val;
  pos_sp->n.square[lva_sq].wood.name = NOPIECE;
  pos_sp->n.square[lva_sq].wood.color = NOCOLOR;
  pos_sp->n.square[lva_sq].wood.cost = 0;

  gain = target_val - see_rec(to_sq,1 - side);

  pos_sp->n.square[to_sq].wood = saved_to;
  pos_sp->n.square[lva_sq].wood = saved_from;

  return (gain > 0)?gain:0;
}

static int see(SQUARE_NUM from_sq, SQUARE_NUM to_sq){
  /* Save the full position (including bitboards) before temporarily
   * rewriting mailbox squares for the exchange sequence.  see_rec()
   * mutates pos_sp->n.square[].wood directly without going through
   * set_square/clear_square, so bb_piece/bb_side fall out of sync
   * while the recursion is live.  Wrapping with save/restore keeps
   * every other consumer (is_square_attacked, in_check, etc.) safe
   * even if they are called from a context that overlaps with SEE.
   * The cost is two extra memcpy calls; correctness outweighs that. */
  save_position();

  PIECE_COLOR side = who_on_square(from_sq)->color;
  PIECE_NAME attacker = who_on_square(from_sq)->name;
  WOOD_PIECE saved_to, saved_from;
  int gain;

  int target_val = see_val[who_on_square(to_sq)->name];

  saved_to = pos_sp->n.square[to_sq].wood;
  saved_from = pos_sp->n.square[from_sq].wood;

  pos_sp->n.square[to_sq].wood.name = attacker;
  pos_sp->n.square[to_sq].wood.color = side;
  pos_sp->n.square[to_sq].wood.cost = see_val[attacker];
  pos_sp->n.square[from_sq].wood.name = NOPIECE;
  pos_sp->n.square[from_sq].wood.color = NOCOLOR;
  pos_sp->n.square[from_sq].wood.cost = 0;

  gain = target_val - see_rec(to_sq,1 - side);

  pos_sp->n.square[to_sq].wood = saved_to;
  pos_sp->n.square[from_sq].wood = saved_from;

  restore_position();

  return gain;
}

/*=============================================================================
 * FV0 Sort — Kaissa's Mandatory First-Pass Root Move Ordering
 *
 * Original Kaissa: "The first and only obligatory iteration for all Kaissa
 * levels is the preliminary FV0 search.  It is just FV after each possible
 * move in the position on the board.  All moves are ordered according to the
 * results of this search."
 *
 * Runs fv_search (Forcing Variation / quiescence) after every root move and
 * sets move->profit to that score, then sorts the list descending so the best
 * FV0 move comes first — exactly as the original Kaissa PC algorithm did.
 *=============================================================================*/
static void fv0_sort(MOVE* moves, int num_moves){
  int i;
  for (i = 0; i < num_moves; i++){
    if (make_move(&moves[i]) == 0){
      /* Evaluate from the opponent's perspective then negate */
      int s = -fv_search(-POS_INFINITY, POS_INFINITY);
      take_back_move();
      moves[i].profit = s;
    } else{
      moves[i].profit = -(POS_INFINITY);
    }
    /* Respect the global stop flag so pondering/time-out is still honoured */
    if (stop_search) return;
  }
  /* Insertion sort — list is usually short at root, so O(n²) is fine */
  for (i = 1; i < num_moves; i++){
    MOVE key = moves[i];
    int j = i - 1;
    while (j >= 0 && moves[j].profit < key.profit){
      moves[j + 1] = moves[j];
      j--;
    }
    moves[j + 1] = key;
  }
}

/*=============================================================================
 * FV1 — Kaissa's Second Ordering Pass (Opponent Replies to Best FV0 Move)
 *
 * Original Kaissa: "For all other levels it repeats the procedure for the
 * opponent's moves after the best FV0 move.  This search is called FV1."
 *
 * Makes the best FV0 root move, generates the opponent's legal replies,
 * scores each with fv_search, sorts them descending, then restores the
 * position.  The sorted list is returned in opp_moves[] so that the main
 * iterative-deepening loop can feed it to alphabeta at depth − 1 instead of
 * letting alphabeta re-generate and re-order those moves from scratch.
 *
 * This is applied only when depth ≥ 2 (FV0-only level uses fv0_sort alone).
 *=============================================================================*/
static void fv1_sort(MOVE* best_root_move, MOVE* opp_moves, int* num_opp_moves){
  *num_opp_moves = 0;

  /* Step 1: make the best FV0 move */
  if (make_move(best_root_move) != 0)
    return;

  /* Step 2: generate opponent's legal replies */
  MOVE* first = opp_moves;
  MOVE* last = opp_moves;
  generate_legal_moves(&first,&last);
  int n = (int)(last - first);

  /* Step 3: score each reply with fv_search */
  int i;
  for (i = 0; i < n && ! stop_search; i++){
    if (make_move(&opp_moves[i]) == 0){
      opp_moves[i].profit = -fv_search(-POS_INFINITY, POS_INFINITY);
      take_back_move();
    } else{
      opp_moves[i].profit = -(POS_INFINITY);
    }
  }

  /* Step 4: restore position (undo best_root_move) */
  take_back_move();

  if (stop_search) return;

  /* Step 5: insertion sort descending (opponent wants highest from their side,
   * which is lowest from our perspective — but we store opponent's score
   * as seen from the opponent, so highest = best for opponent = worst for us;
   * the caller passes this list to alphabeta which will negate as usual) */
  for (i = 1; i < n; i++){
    MOVE key = opp_moves[i];
    int j = i - 1;
    while (j >= 0 && opp_moves[j].profit < key.profit){
      opp_moves[j + 1] = opp_moves[j];
      j--;
    }
    opp_moves[j + 1] = key;
  }

  *num_opp_moves = n;
}

/*=============================================================================
 * FV Search (Forcing Variation) — Kaissa's Quiescence Search
 *
 * qs_depth tracks recursive quiescence depth.  Checking moves are only
 * generated at qs_depth == 0 (the immediate horizon node); all recursive
 * calls increment qs_depth so the check loop is skipped, preventing the
 * unbounded check-escape-check chain that caused the original hang.
 *=============================================================================*/
static int qs_depth_counter = 0; /* tracks how deep we are in fv_search */
static int fv_search(int alpha, int beta){
  int stand_pat, score;
  MOVE moves[128];
  MOVE *first = moves, *last = moves;
  MOVE* mp;
  SQUARE_NUM sq2;
  int i;
  int in_check_local = in_check();

  qs_depth_counter++;
  int my_qs_depth = qs_depth_counter; /* snapshot: 1 = horizon, 2+ = recursive */

  #define FV_RETURN(v) do { qs_depth_counter = my_qs_depth - 1; return (v); } while(0)

  search_nodes++;
  if ((search_nodes & 0xFFF) == 0){
    if (timer_elapsed_ms_portable() >= search_time_limit_ms)
      stop_search = 1;
    if (is_pondering && ! stop_search && input_available()){
      if (read_line_nb(ponder_buf,sizeof(ponder_buf)))
        ponder_buf_ready = 1;
      stop_search = 1;
    }
    if (stop_search)
      FV_RETURN(alpha);
  }

  /* TT probe in quiescence */
  U64 qs_hash = compute_zobrist_hash();
  {
    int qs_tt_score = 0;
    MOVE qs_tt_best;
    qs_tt_best.from = DUMMY;
    if (tt_probe(qs_hash,0,alpha,beta,&qs_tt_score,&qs_tt_best))
      FV_RETURN(qs_tt_score);
  }

  /* Hard cap on quiescence recursion depth.
   * The in-check evasion branch recurses without limit; consecutive checks
   * (rook gives check, king evades, rook checks again) can blow out to 20+
   * levels.  Cap at 12 and just return the stand_pat if exceeded. */
  #define QS_MAX_DEPTH 12

  if (in_check_local){
    /* If we're too deep, don't recurse further — return a pessimistic score */
    if (my_qs_depth > QS_MAX_DEPTH){
      FV_RETURN(-(MAT_INFINITY - qs_depth_counter));
    }
    stand_pat = static_evaluate();
    MOVE all_moves[512];
    MOVE *all_first = all_moves, *all_last = all_moves;
    generate_legal_moves(&all_first,&all_last);
    /* Checkmate in qsearch */
    if (all_last == all_first)
      FV_RETURN(-(MAT_INFINITY - qs_depth_counter));

    for (mp = all_first; mp < all_last && ! stop_search; mp++){
      if (make_move(mp) != 0) continue;
      score = -fv_search(-beta,-alpha);
      take_back_move();
      if (score >= beta)
        FV_RETURN(beta);
      if (score > alpha) alpha = score;
    }
    FV_RETURN(alpha);
  }

  stand_pat = static_evaluate();
  if (stand_pat >= beta)
    FV_RETURN(beta);
  if (stand_pat > alpha) alpha = stand_pat;

  if (! is_endgame()){
    int best_capture = 0, sq3;
    for (sq3 = 0; sq3 < NSQUARE; sq3++){
      WOOD_PIECE* w = who_on_square(sq3);
      if (w->name != NOPIECE && w->color != move_color &&
        w->cost > best_capture)
        best_capture = w->cost;
    }
    if (stand_pat + best_capture + DELTA_MARGIN <= alpha)
      FV_RETURN(alpha);
  }

  mp = first;
  for (sq2 = 0; sq2 < NSQUARE; sq2++){
    WOOD_PIECE* w = who_on_square(sq2);
    if (w->name == NOPIECE || w->color != move_color) continue;

    MOVE piece_moves[64];
    MOVE *pm = piece_moves, *pm_last = piece_moves;
    generate_moves_for_piece(sq2,&pm_last);

    for (pm = piece_moves; pm < pm_last; pm++){
      if (pm->flags & M_CAPTURE){
        int see_score = see(pm->from,pm->to);
        if (see_score >= 0){
          int cap_val = piece_cost[who_on_square(pm->to)->name];
          if (stand_pat + cap_val + DELTA_MARGIN <= alpha)
            continue;
          *mp = *pm;
          mp->profit = see_score;
          mp++;
          if (mp >= first + 128) goto done;
        }
      } else if (pm->flags & M_PROMOTION){
        /* Non-capturing queen promotions are almost always winning —
         * include them even though they aren't captures. */
        PIECE_NAME promo = (pm->flags >> 8) & 0xF;
        if (promo == QUEEN || promo == 0 /* default = queen */){
          *mp = *pm;
          mp->profit = QUEEN_COST - PAWN_COST; /* large bonus */
          mp++;
          if (mp >= first + 128) goto done;
        }
      }
    }
  }
done:
  last = mp;

  for (i = 1; i < (int)(last - first); i++){
    MOVE key = first[i];
    int j = i - 1;
    while (j >= 0 && first[j].profit < key.profit){
      first[j + 1] = first[j];
      j--;
    }
    first[j + 1] = key;
  }

  for (mp = first; mp < last && ! stop_search; mp++){
    if (! is_legal_move(mp)) continue;
    if (make_move(mp) != 0) continue;
    score = -fv_search(-beta,-alpha);
    take_back_move();
    if (score >= beta)
      FV_RETURN(beta);
    if (score > alpha) alpha = score;
  }

  /* Checking moves: only at qs depth 1 (the immediate horizon), never in
   * recursive calls.  Without this guard the search recurses unboundedly:
   *   horizon → check → escape → check → escape → ...
   * Depth guard: my_qs_depth == 1 means this is the first fv_search call
   * from alphabeta (depth 0 of qsearch).  Recursive calls have my_qs_depth >= 2
   * and skip this block entirely.
   *
   * Additional guard: alpha > stand_pat - 50 means we haven't found a big
   * improvement from captures, so checking moves can meaningfully contribute.
   * When alpha is at -POS_INFINITY (full-window fv0_sort call) we skip too. */
  if (my_qs_depth == 1 &&
    ! in_check_local &&
    alpha > -(MAT_INFINITY / 2) &&
    (beta - alpha) > 1 &&
    alpha >= stand_pat - 50){
    for (sq2 = 0; sq2 < NSQUARE; sq2++){
      WOOD_PIECE* w2 = who_on_square(sq2);
      if (w2->name == NOPIECE || w2->color != move_color) continue;
      MOVE pmoves[64];
      MOVE *pm2 = pmoves, *pm2_last = pmoves;
      generate_moves_for_piece(sq2,&pm2_last);
      for (pm2 = pmoves; pm2 < pm2_last && ! stop_search; pm2++){
        if (pm2->flags & M_CAPTURE) continue; /* already searched */
        if (pm2->flags & M_PROMOTION) continue;
        if (! is_legal_move(pm2)) continue;
        if (make_move(pm2) != 0) continue;
        int gives_check = in_check();
        if (! gives_check){
          take_back_move();
          continue;
        }
        score = -fv_search(-beta,-alpha);
        take_back_move();
        if (score >= beta)
          FV_RETURN(beta);
        if (score > alpha) alpha = score;
      }
    }
  }

  FV_RETURN(alpha);
}
#undef FV_RETURN
/*=============================================================================
 * Alpha-Beta Search — Kaissa's Main Tree Search
 *
 * Called iteratively from search_best_move (the Move Selection Algorithm).
 * Uses alpha-beta with aspiration windows ("Rounds" in Kaissa terminology),
 * transposition table, null-move pruning, killer moves, LMR, and futility
 * pruning.  The quiescence call-out is fv_search() (Forcing Variation).
 *=============================================================================*/
static int alphabeta(
  int depth, int alpha, int beta, int* best_move_index,
  MOVE* moves, int num_moves){
  int i, score;
  MOVE move;
  int has_moves = 0;
  int ply = level;
  int check = in_check();
  int orig_alpha = alpha;
  MOVE tt_best;
  tt_best.from = DUMMY;

  /* Cap PV array index to prevent buffer overflow */
  int pv_idx = ply;
  if (pv_idx >= MAX_PV_LEN - 1){
    pv_idx = MAX_PV_LEN - 2;
  }
  int next_idx = pv_idx + 1;
  if (next_idx >= MAX_PV_LEN){
    next_idx = MAX_PV_LEN - 1;
  }

  search_nodes++;

  /* Check time more frequently in deeper searches */
  if ((search_nodes & 0xFF) == 0){ /* Every ~256 nodes instead of 4096 */
    if (timer_elapsed_ms_portable() >= search_time_limit_ms){
      stop_search = 1;
      return 0;
    }
    if (is_pondering && ! stop_search && input_available()){
      if (read_line_nb(ponder_buf,sizeof(ponder_buf)))
        ponder_buf_ready = 1;
      stop_search = 1;
      return 0;
    }
  }

  int ext = check?1:0;

  if (depth - ext <= 0 || stop_search){
    return fv_search(alpha,beta);
  }

  if (repetition_count() >= 1){
    /* Return a small contempt penalty instead of hard 0 so the engine
     * avoids draws when ahead and accepts them only when behind. */
    int contempt = (move_color == engine_color)?-CONTEMPT_CP:CONTEMPT_CP;
    return contempt;
  }

  /* 50-move rule: 100 half-moves without pawn move or capture = draw */
  if (halfmove_clock >= 100)
    return 0;

  /* Insufficient material — neither side can mate */
  if (is_insufficient_material())
    return 0;

  U64 cur_hash = compute_zobrist_hash();
  int is_pv_node = (beta > alpha + 1);
  int tt_score_probe = 0;
  if (tt_probe(cur_hash,depth,alpha,beta,&tt_score_probe,&tt_best)){
    if (! is_pv_node)
      return tt_score_probe;
  }

  /* Compute static eval once here; reused by razoring, futility, and SEE pruning.
   * Only computed when the conditions that need it are plausible (non-PV, not in check,
   * low depth) to avoid calling it at every node. */
  int do_futility = (! check && depth <= 3 && ! is_pv_node &&
    alpha > -(MAT_INFINITY - 100) &&
    alpha < (MAT_INFINITY - 100));
  int need_static_eval = (do_futility ||
    (! is_pv_node && ! check && depth <= 2));
  int static_eval_cached = need_static_eval?static_evaluate():0;

  /* Razoring: at very low depth, if static eval + margin can't reach alpha,
   * just return the qsearch score rather than doing a full search. */
  if (! is_pv_node && ! check && depth <= 2 &&
    alpha > -(MAT_INFINITY - 100) && alpha < (MAT_INFINITY - 100)){
    int razor_margin = 150 + 150 * depth; /* 300 @ depth 2, 150 @ depth 1 */
    if (static_eval_cached + razor_margin < alpha){
      int rscore = fv_search(alpha - 1,alpha);
      if (rscore < alpha)
        return rscore;
    }
  }

  /* Null move pruning.
   * Skip in the endgame when the side to move has only king + pawns —
   * zugzwang positions are common there and null move would give wrong scores. */
  if (! check && depth >= 3 && best_move_index == NULL &&
    beta < MAT_INFINITY - 100 && alpha > -(MAT_INFINITY - 100)){
    int own_material = 0;
    int sq2;
    for (sq2 = 0; sq2 < NSQUARE; sq2++){
      WOOD_PIECE* w = who_on_square(sq2);
      if (w->name != NOPIECE && w->name != KING && w->name != PAWN &&
        w->color == move_color)
        own_material += w->cost;
    }
    /* Require at least a rook's worth of non-pawn material to avoid
     * zugzwang errors; also skip in pure K+P endings entirely. */
    if (own_material >= ROOK_COST){
      save_position();

      PIECE_COLOR saved_color = move_color;
      SQUARE_NUM saved_ep = en_pass_square;
      move_color = 1 - move_color;
      en_pass_square = DUMMY;
      level++;

      /* Adaptive R: deeper positions can afford a larger skip */
      int R = depth / 3 + 1;
      if (R < 2) R = 2;
      if (R > 4) R = 4;
      int null_score = -alphabeta(depth - R - 1,-beta,-beta + 1,
        NULL, NULL,0);
      level--;

      restore_position();

      move_color = saved_color;
      en_pass_square = saved_ep;

      if (! stop_search && null_score >= beta)
        return beta;
    }
  }

  /* Clear PV for this node */
  pv_length[pv_idx] = 0;
  level++;

  MOVE local_moves[512];
  MOVE *first = local_moves, *last = local_moves;
  if (moves == NULL){
    generate_legal_moves(&first,&last);
    num_moves = (int)(last - first);
    moves = first;
  }

  /* Internal Iterative Deepening: if no TT move at a PV node, do a reduced
   * search first to populate the TT so we get a good move to search first. */
  if (is_pv_node && depth >= 5 && tt_best.from == DUMMY && ! stop_search){
    alphabeta(depth - 3,alpha,beta, NULL, NULL,0);
    tt_probe(cur_hash,0,alpha,beta,&tt_score_probe,&tt_best);
  }

  if (num_moves == 0){
    level--;
    if (check)
      return -(MAT_INFINITY - ply);
    return 0;
  }

  MOVE ordered_moves[512];
  memcpy(ordered_moves,moves,num_moves * sizeof(MOVE));

  /* Score moves for ordering.
   * Priority: TT best (1 000 000) > winning captures (600 000+) >
   *           killers (400 000) > countermove (380 000) >
   *           history (0+) > losing captures (-100 000+). */

  /* Last move made before entering this node (for countermove lookup) */
  MOVE last_move_for_cm;
  last_move_for_cm.from = DUMMY;
  last_move_for_cm.to = DUMMY;
  if (ply > 0 && stack_ptr > 0){
    /* The most recent saved position holds the move that led here */
    int prev = stack_ptr - 1;
    if (prev >= 0 && prev < MAX_STACK){
      /* We don't store the move directly; use TT best from one ply up
       * as a proxy, or simply track via a separate stack. For now we
       * approximate using the prev stack's piece that changed.
       * Simpler: pass last move via a thread-local. Use pv_table ply-1. */
      if (ply >= 1 && pv_length[pv_idx > 0?pv_idx - 1:0] > 0)
        last_move_for_cm = pv_table[pv_idx > 0?pv_idx - 1:0][0];
    }
  }

  for (i = 0; i < num_moves; i++){
    int move_score = 0;
    MOVE* m = &ordered_moves[i];

    if (tt_best.from != DUMMY && is_legal_move(&tt_best) &&
      m->from == tt_best.from && m->to == tt_best.to)
      move_score = 1000000;
    else if (m->flags & M_PROMOTION){
      /* Queen promotion — always high priority */
      move_score = 800000;
    } else if (m->flags & M_CAPTURE){
      /* Use SEE to distinguish winning from losing captures */
      int see_score = see(m->from,m->to);
      if (see_score >= 0){
        /* Winning/equal capture: MVV-LVA tie-break */
        int victim = piece_cost[who_on_square(m->to)->name];
        int attacker = piece_cost[who_on_square(m->from)->name];
        move_score = 600000 + victim * 100 - attacker;
      } else{
        /* Losing capture: go after quiet moves */
        move_score = -100000 + see_score;
      }
    } else{
      if (is_killer(ply,m)){
        move_score = 400000;
      } else if (last_move_for_cm.from != DUMMY &&
        last_move_for_cm.from >= 0 && last_move_for_cm.from < 64 &&
        last_move_for_cm.to >= 0 && last_move_for_cm.to < 64){
        MOVE* cm = &countermove[last_move_for_cm.from][last_move_for_cm.to];
        if (cm->from == m->from && cm->to == m->to)
          move_score = 380000;
        else
          move_score = history[m->from][m->to]
            + history_malus[m->from][m->to];
      } else{
        move_score = history[m->from][m->to]
          + history_malus[m->from][m->to];
      }
    }
    m->profit = move_score;
  }

  /* Sort moves by score (descending) */
  for (i = 1; i < num_moves; i++){
    MOVE key = ordered_moves[i];
    int j = i - 1;
    while (j >= 0 && ordered_moves[j].profit < key.profit){
      ordered_moves[j + 1] = ordered_moves[j];
      j--;
    }
    ordered_moves[j + 1] = key;
  }

  MOVE best_move_local;
  best_move_local.from = DUMMY;
  int best_found = 0;

  int futility_base = 0;
  if (do_futility)
    futility_base = static_eval_cached + FUTILITY_MARGIN[depth];

  /* Singular extensions: if the TT entry says one move is much better than all
   * others, extend its search by one ply.  Conditions: TT move available,
   * depth >= 7, TT entry is a lower bound with sufficient depth, and a
   * reduced search (excl. the TT move) fails below the TT score minus margin.
   * We use a single-response extension — the extension is applied only once,
   * inside the loop for move i==0 (the TT move at the front of the list). */
  int singular_ext_move_from = DUMMY;
  int singular_ext_move_to = DUMMY;
  if (! stop_search && ! check && depth >= 7 &&
    tt_best.from != DUMMY &&
    tt_score_probe > -(MAT_INFINITY - 200) &&
    tt_score_probe < (MAT_INFINITY - 200)){
    /* Reduced search with TT move excluded: temporarily mark it */
    int se_margin = depth * 12;
    int se_beta = tt_score_probe - se_margin;
    /* Only try if TT score meaningfully exceeds our current alpha */
    if (se_beta > alpha){
      /* One-pass reduced alphabeta treating TT move as illegal:
       * We approximate by running fv_search (qsearch) here which is
       * cheap.  A full reduced-depth search would be stronger but
       * risks search-tree blow-up.  This lightweight version still
       * catches the common case where the TT move is clearly singular. */
      int se_score = fv_search(se_beta - 1,se_beta);
      if (! stop_search && se_score < se_beta){
        singular_ext_move_from = tt_best.from;
        singular_ext_move_to = tt_best.to;
      }
    }
  }

  for (i = 0; i < num_moves && ! stop_search; i++){
    move = ordered_moves[i];
    has_moves = 1;

    if (do_futility && i > 0 &&
      ! (move.flags & M_CAPTURE) &&
      ! (move.flags & M_PROMOTION) &&
      ! is_killer(ply,&move) &&
      futility_base <= alpha)
      continue;

    /* Late Move Pruning: at low depth, stop searching quiet moves beyond
     * the threshold.  Only applies outside PV nodes and when not in check. */
    if (! is_pv_node && ! check && depth <= 3 && depth >= 1 &&
      i >= LMP_COUNT[depth] &&
      ! (move.flags & M_CAPTURE) &&
      ! (move.flags & M_PROMOTION) &&
      ! is_killer(ply,&move))
      break;

    /* SEE-based capture pruning: skip clearly losing captures at low depth
     * (e.g. QxP where P is defended — wastes time on hopeless lines). */
    if (! is_pv_node && ! check && depth <= 4 && i > 0 &&
      (move.flags & M_CAPTURE) && ! (move.flags & M_PROMOTION)){
      int cap_see = see(move.from,move.to);
      if (cap_see < -PAWN_COST)
        continue;
    }

    if (make_move(&move) == 0){
      int new_depth = depth - 1;

      /* Singular extension: this move is the sole good move at this node */
      if (move.from == singular_ext_move_from &&
        move.to == singular_ext_move_to)
        new_depth += 1;

      int reduce = 0;
      if (! is_pv_node && ! check && i >= 3 && depth >= 3 &&
        ! (move.flags & M_CAPTURE) &&
        ! (move.flags & M_PROMOTION) &&
        ! is_killer(ply,&move)){
        /* LMR: reduce more for later moves and deeper searches */
        reduce = 1;
        if (depth >= 5 && i >= 6) reduce = 2;
        if (depth >= 8 && i >= 12) reduce = 3;
      }

      if (reduce){
        score = -alphabeta(new_depth - reduce,-alpha - 1,-alpha,
          NULL, NULL,0);
        if (! stop_search && score > alpha)
          score = -alphabeta(new_depth,-beta,-alpha,
            NULL, NULL,0);
      } else if (i > 0 && is_pv_node){
        score = -alphabeta(new_depth,-alpha - 1,-alpha,
          NULL, NULL,0);
        if (! stop_search && score > alpha && score < beta)
          score = -alphabeta(new_depth,-beta,-alpha,
            NULL, NULL,0);
      } else{
        score = -alphabeta(new_depth,-beta,-alpha, NULL, NULL,0);
      }

      take_back_move();

      if (score > alpha){
        alpha = score;
        best_move_local = move;
        best_found = 1;

        /* Store PV with bounds checking */
        pv_table[pv_idx][0] = move;

        if (pv_length[next_idx] > 0){
          int copy_len = pv_length[next_idx];
          if (copy_len > MAX_PV_LEN - 1 - pv_idx)
            copy_len = MAX_PV_LEN - 1 - pv_idx;
          memcpy(&pv_table[pv_idx][1],pv_table[next_idx],
            copy_len * sizeof(MOVE));
        }
        pv_length[pv_idx] = 1 + pv_length[next_idx];
        if (pv_length[pv_idx] > MAX_PV_LEN - pv_idx)
          pv_length[pv_idx] = MAX_PV_LEN - pv_idx;

        if (best_move_index){
          for (int j = 0; j < num_moves; j++){
            if (moves[j].from == move.from && moves[j].to == move.to){
              *best_move_index = j;
              break;
            }
          }
        }

        if (alpha >= beta){
          if (! (move.flags & M_CAPTURE) && ! (move.flags & M_PROMOTION)){
            store_killer(ply,&move);
            /* History bonus: reward the move that caused the cut */
            history[move.from][move.to] += depth * depth;
            if (history[move.from][move.to] > 50000)
              history[move.from][move.to] = 25000;
            /* Countermove: record this as the refutation of the last move */
            if (last_move_for_cm.from >= 0 && last_move_for_cm.from < 64 &&
              last_move_for_cm.to >= 0 && last_move_for_cm.to < 64){
              countermove[last_move_for_cm.from][last_move_for_cm.to] = move;
            }
            /* History malus: penalise all quiet moves searched before this cut */
            for (int mi = 0; mi < i; mi++){
              MOVE* m2 = &ordered_moves[mi];
              if (! (m2->flags & M_CAPTURE) && ! (m2->flags & M_PROMOTION)){
                history_malus[m2->from][m2->to] -= depth * depth;
                if (history_malus[m2->from][m2->to] < -50000)
                  history_malus[m2->from][m2->to] = -25000;
              }
            }
          }
          break;
        }
      }
    }
  }

  if (! has_moves){
    level--;
    if (check)
      return -(MAT_INFINITY - ply);
    return 0;
  }

  level--;

  if (! stop_search){
    int flag;
    if (best_found && alpha >= beta) flag = TT_BETA;
    else if (alpha > orig_alpha) flag = TT_EXACT;
    else flag = TT_ALPHA;
    tt_store(cur_hash,depth,alpha,
      flag,best_found?&best_move_local:NULL);
  }

  return alpha;
}

/*=============================================================================
 * Portable Timer Functions
 *=============================================================================*/
static void timer_init_portable(void){}

static void timer_start_portable(void){
  search_start_time = clock();
}

static int timer_elapsed_ms_portable(void){
  clock_t now = clock();
  return (int)((now - search_start_time) * 1000 / CLOCKS_PER_SEC);
}
static unsigned int timer_ms_now(void) {
  return (unsigned int)
    (clock() * 1000 / CLOCKS_PER_SEC);
}

static void print_time_info(void){
  if (! xboard_mode) return;

  int my_time_cs = (move_color == WHITE_PIECE)?wtime:btime;
  int opp_time_cs = (move_color == WHITE_PIECE)?btime:wtime;

  fprintf(stderr,"TIME: my=%d.%02d, opp=%d.%02d, move=%s\n",
    my_time_cs / 100,my_time_cs % 100,
    opp_time_cs / 100,opp_time_cs % 100,
    move_color == WHITE_PIECE?"WHITE":"BLACK");
}

/* =============================================================================
 * Time management for both sides - ensure minimum search depth
 * ============================================================================= */
/* The time variables are set by the "time" and "otim" commands:
   - "time N" sets wtime if it's White's turn, or btime if it's Black's turn
   - "otim N" sets the opponent's time
   
   The engine should ONLY use its own time (move_color determines which one)
*/
static int get_search_time_ms(void){
  /* Get OUR time based on who is to move */
  int my_time_cs;
  int my_inc_cs;

  if (move_color == WHITE_PIECE){
    my_time_cs = wtime;
    my_inc_cs = winc;
  } else{
    my_time_cs = btime;
    my_inc_cs = binc;
  }

  /* Convert to milliseconds */
  int my_time_ms = my_time_cs * 10;
  int my_inc_ms = my_inc_cs * 10;

  /* Emergency: very low time (less than 2 seconds) */
  if (my_time_ms < 2000){
    return my_time_ms / 2;
  }

  /* -----------------------------------------------------------------------
   * Game-phase detection: count non-king pieces on the board.
   * Opening  : > 20 pieces  (both sides mostly developed)
   * Middlegame: 12–20 pieces
   * Endgame  : 6–12 pieces
   * Late EG  : < 6 pieces
   * ----------------------------------------------------------------------- */
  int pieces_on_board = 0;
  for (int i = 0; i < NSQUARE; i++){
    PIECE_NAME n = who_on_square(i)->name;
    if (n != NOPIECE && n != KING)
      pieces_on_board++;
  }

  /* Phase label (0=late EG, 1=EG, 2=middlegame, 3=opening) */
  int phase;
  if (pieces_on_board > 20) phase = 3; /* Opening      */
  else if (pieces_on_board > 12) phase = 2; /* Middlegame   */
  else if (pieces_on_board > 6) phase = 1; /* Endgame      */
  else phase = 0; /* Late endgame */

  /* Estimated moves remaining — used to spread time evenly.
   * Opening/middlegame use a smaller estimate so each move gets a
   * larger slice of the clock.  Endgame uses a larger estimate
   * (more precise manoeuvring needed but less branching). */
  int moves_est;
  switch (phase){
  case 3: moves_est = 20;
    break; /* Opening:     ~20 moves left  */
  case 2: moves_est = 20;
    break; /* Middlegame:  ~20 moves left  */
  case 1: moves_est = 20;
    break; /* Endgame:     ~20 moves left  */
  default: moves_est = 15;
    break; /* Late EG:     ~15 moves left  */
  }
  if (movestogo > 0 && movestogo < moves_est)
    moves_est = movestogo;

  /* -----------------------------------------------------------------------
   * Time divisor: controls what fraction of remaining time we spend per
   * move.  Opening/middlegame use a smaller divisor (= more time per move).
   * Endgame uses a larger one to preserve the clock for precision play.
   *   phase 3 (opening)    : divide by 14/16  → ~6-7 % of clock per move
   *   phase 2 (middlegame) : divide by 15/18  → ~5.5-6.5 %
   *   phase 1 (endgame)    : divide by 25/28
   *   phase 0 (late EG)    : divide by 32/35
   * When the clock is very large (> 120 s) we are slightly more generous.
   * ----------------------------------------------------------------------- */
  int base_divisor;
  switch (phase){
  case 3: base_divisor = (my_time_ms > 120000)?14:16;
    break;
  case 2: base_divisor = (my_time_ms > 120000)?15:18;
    break;
  case 1: base_divisor = (my_time_ms > 120000)?25:28;
    break;
  default: base_divisor = (my_time_ms > 120000)?32:35;
    break;
  }

  /* Time allocation based on remaining time */
  int time_ms;

  if (my_time_ms < 10000){
    /* Less than 10 seconds — be very conservative regardless of phase */
    time_ms = my_time_ms / 4;
    if (time_ms < 500) time_ms = 500;
  } else{
    time_ms = my_time_ms / base_divisor + my_inc_ms / 3;

    /* Also bound by the moves-remaining estimate */
    int max_by_moves = my_time_ms / moves_est + my_inc_ms / 3;
    if (time_ms > max_by_moves)
      time_ms = max_by_moves;

    /* Never burn more than 45 % of the clock on a single move */
    int max_safe = my_time_ms * 45 / 100;
    if (time_ms > max_safe)
      time_ms = max_safe;

    /* ---------------------------------------------------------------
     * Per-bracket floor/ceiling.
     * Opening/middlegame ceilings are raised significantly to allow
     * deeper searches where it matters most; endgame ceilings stay
     * conservative.
     * --------------------------------------------------------------- */
    if (my_time_ms > 60000){
      int floor_ms = (phase >= 2)?4000:2000;
      int ceil_ms = (phase == 3)?20000:
        (phase == 2)?18000:
        (phase == 1)?7000:5000;
      if (time_ms < floor_ms) time_ms = floor_ms;
      if (time_ms > ceil_ms) time_ms = ceil_ms;
    } else if (my_time_ms > 30000){
      int floor_ms = (phase >= 2)?3000:1500;
      int ceil_ms = (phase == 3)?15000:
        (phase == 2)?12000:
        (phase == 1)?5000:4000;
      if (time_ms < floor_ms) time_ms = floor_ms;
      if (time_ms > ceil_ms) time_ms = ceil_ms;
    } else if (my_time_ms > 10000){
      int floor_ms = (phase >= 2)?2000:1000;
      int ceil_ms = (phase == 3)?9000:
        (phase == 2)?7000:
        (phase == 1)?3500:2500;
      if (time_ms < floor_ms) time_ms = floor_ms;
      if (time_ms > ceil_ms) time_ms = ceil_ms;
    }
  }

  /* Absolute floor/ceiling — raised to match higher opening/MG ceilings above */
  if (time_ms < 200) time_ms = 200;
  if (time_ms > 25000) time_ms = 25000;

  /* Hard safety: never exceed 80 % of remaining clock */
  int max_time = my_time_ms * 80 / 100;
  if (time_ms > max_time && max_time > 100)
    time_ms = max_time;

  return time_ms;
}

/* =============================================================================
 * search_best_move - Find best move with proper time management
 * ============================================================================= */
int search_best_move(MOVE* best_move){
  MOVE moves[512];
  MOVE filtered_moves[512];
  MOVE *first = moves, *last = moves;
  int num_moves, best_index = -1;
  int score = 0;
  int elapsed_ms = 0;
  int completed_depth = 0;
  int phase = 0; /* game phase: 3=opening,2=middlegame,1=endgame,0=late EG */
  MOVE fallback_move;
  fallback_move.from = DUMMY;

  /* Get our actual time based on who is to move */
  int my_time_cs = (move_color == WHITE_PIECE)?wtime:btime;
  int my_inc_cs = (move_color == WHITE_PIECE)?winc:binc;
  int opp_time_cs = (move_color == WHITE_PIECE)?btime:wtime;

  /* DEBUG: Print actual time being used */
  if (xboard_mode && post_mode){
    fprintf(stderr,"TIME: my=%d.%02d sec, opp=%d.%02d sec, inc=%d.%02d, move=%s\n",
      my_time_cs / 100,my_time_cs % 100,
      opp_time_cs / 100,opp_time_cs % 100,
      my_inc_cs / 100,my_inc_cs % 100,
      move_color == WHITE_PIECE?"WHITE":"BLACK");
  }

  /* Calculate time limit.
   * IMPORTANT: write to the GLOBAL search_time_limit_ms — that is what
   * alphabeta() and fv_search() read on every node check.
   *
   * When called from start_pondering(), is_pondering is already set and
   * search_time_limit_ms holds the ponder ceiling (opponent-clock-based).
   * Do NOT override it here; let the ponder search respect that limit.
   *
   * no_clock_info: true when WinBoard has never sent "time"/"otim" commands.
   * In console mode wtime and btime are always 0; all clock-based emergency
   * overrides must be skipped so that the user's "time N" / "depth N"
   * console settings are respected. */
  int no_clock_info = (wtime == 0 && btime == 0);
  int use_time_limit = 0;

  if (is_pondering){
    /* Ponder search: search_time_limit_ms was set by start_pondering().
     * Always enforce the time limit so input_available() failures don't
     * let us ponder indefinitely on the engine's own clock. */
    use_time_limit = 1;
    /* safety: never let a rogue ponder consume more than 15 s regardless */
    if (search_time_limit_ms > 15000) search_time_limit_ms = 15000;
  } else if (! no_clock_info){
    /* WinBoard / clock-based mode */
    search_time_limit_ms = 5000; /* default; overwritten below */
    if (time_control){
      use_time_limit = 1;
      search_time_limit_ms = get_search_time_ms();
    }
    /* Emergency: very little real clock time remaining */
    if (my_time_cs < 500){
      search_time_limit_ms = my_time_cs * 10 / 2;
      if (search_time_limit_ms < 100) search_time_limit_ms = 100;
      use_time_limit = 1;
    }
    /* Never burn more than 80 % of the real clock */
    int max_time_ms = my_time_cs * 10 * 80 / 100;
    if (search_time_limit_ms > max_time_ms && max_time_ms > 100)
      search_time_limit_ms = max_time_ms;
    /* Hard bounds for WinBoard mode */
    if (search_time_limit_ms < 100) search_time_limit_ms = 100;
    if (search_time_limit_ms > 25000) search_time_limit_ms = 25000;
  } else{
    /* Console mode: honour "time N" / "depth N" as-is. */
    if (time_control){
      /* "time N" was already written into search_time_limit_ms */
      use_time_limit = 1;
      /* Sanity bounds only — don't clamp aggressively */
      if (search_time_limit_ms < 100)
        search_time_limit_ms = 100;
    }
    /* depth mode: use_time_limit stays 0; depth governs the search.
     * Set search_time_limit_ms to a large sentinel so the unconditional
     * per-node time check inside alphabeta() never fires prematurely. */
    if (! time_control)
      search_time_limit_ms = 600000; /* 10 minutes — effectively infinite */
  }

  if (xboard_mode && post_mode){
    fprintf(stderr,"Allocating %d ms for this move\n",search_time_limit_ms);
  }

  /* Initialize search */
  search_nodes = 0;
  stop_search = 0;
  timer_start_portable();
  memset(pv_length,0,sizeof(pv_length));
  clear_killers_history();

  /* Generate legal moves */
  generate_legal_moves(&first,&last);
  num_moves = (int)(last - first);

  if (num_moves == 0){
    best_move->from = DUMMY;
    return 0;
  }

  /* Remove any self-moves (shouldn't happen but safety) */
  for (int i = 0; i < num_moves; i++){
    if (first[i].from == first[i].to){
      for (int j = i; j < num_moves - 1; j++){
        first[j] = first[j + 1];
      }
      num_moves--;
      i--;
    }
  }

  if (num_moves == 0){
    best_move->from = DUMMY;
    return 0;
  }

  /* Root stalemate filtering - only when winning */
  int safe_num_moves = filter_stalemating_moves(first,num_moves,filtered_moves,&fallback_move);

  if (safe_num_moves > 0){
    first = filtered_moves;
    num_moves = safe_num_moves;
  } else if (fallback_move.from != DUMMY){
    first = &fallback_move;
    num_moves = 1;
  }

  if (num_moves == 0 || first[0].from == first[0].to){
    best_move->from = DUMMY;
    return 0;
  }

  /* Start with first move as best */
  *best_move = first[0];
  best_index = 0;

  /* Compute game phase here too (same logic as get_search_time_ms) so that
   * target_depth can use it without requiring a global variable. */
  {
    int pob = 0;
    for (int i = 0; i < NSQUARE; i++){
      PIECE_NAME pn = who_on_square(i)->name;
      if (pn != NOPIECE && pn != KING) pob++;
    }
    phase = (pob > 20)?3:(pob > 12)?2:(pob > 6)?1:0;
  }

  /* Determine target depth.
   * Console depth mode  : use the user's search_depth directly.
   * Console time mode   : run to MAXDEPTH; time is the stop condition.
   * WinBoard clock mode : clock-based heuristic (unchanged). */
  int target_depth;
  if (no_clock_info){
    /* Console mode — respect what the user explicitly set */
    if (! use_time_limit && search_depth > 0)
      target_depth = search_depth; /* "depth N" */
    else
      target_depth = MAXDEPTH; /* "time N" — depth is unlimited */
  } else{
    /* WinBoard clock-based mode */
    if (my_time_cs < 500){
      target_depth = 4;
    } else if (my_time_cs < 2000){
      target_depth = 6;
    } else if (my_time_cs < 5000){
      target_depth = (phase >= 2)?10:8;
    } else{
      target_depth = MAXDEPTH;
    }
    if (! use_time_limit && search_depth > 0)
      target_depth = search_depth;
  }
  if (target_depth < 1) target_depth = 1;
  if (target_depth > MAXDEPTH) target_depth = MAXDEPTH;

  /* Minimum depth before honouring the time limit.
   * In console time mode always complete at least 4 plies so the engine
   * is never embarrassingly shallow.  In WinBoard mode keep the existing
   * behaviour of requiring 6 plies when the clock has room. */
  int min_depth;
  if (no_clock_info)
    min_depth = (target_depth <= 4)?target_depth:4;
  else
    min_depth = (my_time_cs < 1000)?4:6;

  /* -----------------------------------------------------------------------
   * FV0 Pass — Kaissa's mandatory first iteration.
   * Sort all root moves by their FV (forcing-variation / quiescence) score
   * before the main iterative-deepening loop starts.  This is the "FV0
   * search" described in the original Kaissa manual: "The first and only
   * obligatory iteration … is just FV after each possible move."
   * ----------------------------------------------------------------------- */
  fv0_sort(first,num_moves);
  /* After FV0 the best-looking move sits at index 0 */
  *best_move = first[0];

  /* -----------------------------------------------------------------------
   * FV1 Pass — Kaissa's second ordering pass.
   * "For all other levels it repeats the procedure for the opponent's moves
   * after the best FV0 move.  This search is called FV1."
   *
   * We run fv1_sort to pre-score opponent replies (used for FV1 insight),
   * but we do NOT inject the moves into alphabeta's move generation — doing
   * so is unsafe because alphabeta reorders root moves by TT/capture/killer
   * scores, so the first ply==1 node explored is not necessarily after the
   * FV0-best move.  Injecting FV1 moves into the wrong position would pass
   * stale (and potentially illegal) moves to make_move unchecked.
   * ----------------------------------------------------------------------- */
  MOVE fv1_opp_moves[512];
  int fv1_num_opp = 0;
  if (! stop_search && target_depth >= 2)
    fv1_sort(&first[0],fv1_opp_moves,&fv1_num_opp);
  (void)fv1_opp_moves; /* scores computed but injection removed; suppress warning */
  (void)fv1_num_opp;

  /* Track previous iteration score for time-extension logic */
  int prev_score = 0;
  int time_extended = 0; /* only extend once per move */

  /* Iterative deepening */
  for (int depth = 1; depth <= target_depth && ! stop_search; depth++){
    int current_best = -1;
    level = 0;
    memset(pv_length,0,sizeof(pv_length));

    /* History aging: halve all scores at the start of each new iteration so
     * that patterns found in earlier (shallower) searches fade gradually and
     * the heuristic stays fresh.  history_malus gets the same treatment. */
    if (depth > 1){
      int hf, ht;
      for (hf = 0; hf < 64; hf++)
        for (ht = 0; ht < 64; ht++){
          history[hf][ht] >>= 1;
          history_malus[hf][ht] >>= 1;
        }
    }

    elapsed_ms = timer_elapsed_ms_portable();

    /* Check time before starting this depth */
    if (use_time_limit && depth > min_depth && elapsed_ms >= search_time_limit_ms){
      if (xboard_mode && post_mode){
        fprintf(stderr,"Time limit reached at depth %d\n",depth);
      }
      break;
    }

    int asp_score;

    /* Aspiration windows for deeper searches */
    if (depth >= 3 && completed_depth >= 1 &&
      score > -(MAT_INFINITY - 100) && score < (MAT_INFINITY - 100)){
      int window = ASP_WINDOW_INIT;
      int asp_alpha = score - window;
      int asp_beta = score + window;

      /* Limit aspiration re-searches when low on real clock time */
      int max_asp = (! no_clock_info && my_time_cs < 2000)?2:5;

      while (! stop_search && max_asp-- > 0){
        asp_score = alphabeta(depth,asp_alpha,asp_beta,
          &current_best,first,num_moves);
        elapsed_ms = timer_elapsed_ms_portable();

        if (use_time_limit && elapsed_ms >= search_time_limit_ms){
          break;
        }

        if (asp_score <= asp_alpha){
          asp_alpha -= (window < ASP_WINDOW_WIDE)?ASP_WINDOW_WIDE:window;
          if (asp_alpha < -POS_INFINITY / 2)
            asp_alpha = -POS_INFINITY;
          window *= 2;
        } else if (asp_score >= asp_beta){
          asp_beta += (window < ASP_WINDOW_WIDE)?ASP_WINDOW_WIDE:window;
          if (asp_beta > POS_INFINITY / 2)
            asp_beta = POS_INFINITY;
          window *= 2;
        } else{
          break;
        }
      }
    } else{
      asp_score = alphabeta(depth,-POS_INFINITY, POS_INFINITY,
        &current_best,first,num_moves);
      elapsed_ms = timer_elapsed_ms_portable();
    }

    score = asp_score;

    /* Update best move if found */
    if (! stop_search && current_best >= 0 && current_best < num_moves){
      if (first[current_best].from != first[current_best].to){
        best_index = current_best;
        *best_move = first[best_index];
        completed_depth = depth;

        /* Move best move to front for next iteration */
        if (best_index > 0){
          MOVE best = first[best_index];
          memmove(&first[1],&first[0],best_index * sizeof(MOVE));
          first[0] = best;
          best_index = 0;
        }

        /* Output PV if in post mode.
         * alphabeta() returns a score from the perspective of the side
         * to move at the root (always the engine).  WinBoard/XBoard
         * also expects the score from the engine's perspective, so we
         * display it unchanged — positive means the engine is winning
         * regardless of whether it is playing White or Black.
         * The old code negated for Black, which incorrectly turned a
         * winning score into a negative number. */
        if (xboard_mode && post_mode){
          int score_for_display = score;
          score_for_display -= K_PROFITABLE_ATK; // remove eval score jumps from pv display score
          score_for_display -= K_DOUBLE_PROFIT_ATK; // remove eval score jumps from pv display score
          printf("%d %d %d %d",completed_depth,score_for_display,
            elapsed_ms / 10,search_nodes);
          int pv_len = pv_length[0];
          if (pv_len > completed_depth) pv_len = completed_depth;
          for (int k = 0; k < pv_len; k++){
            char ms[10];
            move_to_string_display(&pv_table[0][k],ms);
            printf(" %s",ms);
          }
          printf("\n");
          fflush(stdout);
        } else if (input_mode == MODE_UCI){
          long nps=elapsed_ms > 0 ? (search_nodes * 1000L) / elapsed_ms : 0;

          printf("info depth %d ",completed_depth);
          print_uci_score(score, move_color);
          printf("time %d ",elapsed_ms);
          printf("nodes %d ",search_nodes);
          printf("nps %ld ", nps);

          printf("pv");

          int pv_len = pv_length[0];

          if (pv_len > completed_depth)
            pv_len = completed_depth;

          for (int k = 0; k < pv_len; k++){
            char ms[16];

            move_to_string(&pv_table[0][k],ms);

            printf(" %s",ms);
          }

          printf("\n");

          fflush(stdout);
        } else if (! xboard_mode && post_mode){
          /* Console post mode: show depth / score / time / nodes / PV */

          char best_str[10];

          move_to_string_display(best_move,best_str);

          printf("  depth %2d  score %+5d  %4d ms  %7d nodes  pv %s",
            completed_depth,
            score,
            elapsed_ms,
            search_nodes,
            best_str);

          int pv_len = pv_length[0];

          if (pv_len > completed_depth)
            pv_len = completed_depth;

          for (int k = 1; k < pv_len && k < 5; k++){
            char ms[10];

            move_to_string_display(&pv_table[0][k],ms);

            printf(" %s",ms);
          }

          printf("\n");

          fflush(stdout);
        }
      }
    }

    /* Stop if we found a forced win/loss */
    if (score > MAT_INFINITY - 100 || score < -(MAT_INFINITY - 100))
      break;

    /* Time extension: if the score drops significantly between iterations
     * the engine may be in tactical trouble — grant up to 50% more time,
     * but only once and only when we have clock budget to spare. */
    if (use_time_limit && ! time_extended && depth >= 5 && completed_depth >= 4){
      int drop = prev_score - score; /* positive = score got worse */
      if (drop >= 30){ /* ≥ 30 cp drop triggers extension */
        int extension = search_time_limit_ms / 2;
        int budget_remaining = my_time_cs * 10 - timer_elapsed_ms_portable();
        /* Never extend beyond half the remaining clock */
        int max_ext = budget_remaining / 2;
        if (extension > max_ext) extension = max_ext;
        if (extension > 0){
          search_time_limit_ms += extension;
          time_extended = 1;
          if (xboard_mode && post_mode)
            fprintf(stderr,"Score drop %d cp: extending time by %d ms\n",
              drop,extension);
        }
      }
    }
    prev_score = score;
  }

  /* Emergency fallback: no valid best move (time ran out before depth 1
   * finished, or the move list was empty after filtering). */
  elapsed_ms = timer_elapsed_ms_portable();

  if (best_move->from == best_move->to || best_move->from == DUMMY){
    if (xboard_mode && post_mode)
      fprintf(stderr,"EMERGENCY: No valid move found, picking first legal\n");
    MOVE legal_moves[512];
    MOVE *lf = legal_moves, *ll = legal_moves;
    generate_legal_moves(&lf,&ll);
    for (MOVE* mp = lf; mp < ll; mp++){
      if (mp->from != mp->to && is_legal_move(mp)){
        *best_move = *mp;
        return 1;
      }
    }
    return 0;
  }

  /* Final gate: verify the chosen move is in the legal move list. */
  if (! is_legal_move(best_move)){
    if (xboard_mode && post_mode)
      fprintf(stderr,"EMERGENCY: Best move illegal, picking first legal\n");
    MOVE legal_moves[512];
    MOVE *lf = legal_moves, *ll = legal_moves;
    generate_legal_moves(&lf,&ll);
    for (MOVE* mp = lf; mp < ll; mp++){
      if (is_legal_move(mp)){
        *best_move = *mp;
        return 1;
      }
    }
    return 0;
  }

  if (xboard_mode && post_mode){
    fprintf(stderr,"Search complete: depth=%d, time=%d ms, nodes=%d\n",
      completed_depth,elapsed_ms,search_nodes);
  }

  return (best_index >= 0 && best_move->from != DUMMY);
}

static unsigned long long perft(const int depth) {
  MOVE moves[256];
  MOVE* first=moves;
  MOVE* last=moves;
  if (depth <= 0)
    return 1ULL;
  generate_legal_moves(&first, &last);
  unsigned long long nodes=0ULL;
  for (MOVE* mp=first; mp < last; mp++) {
    if (make_move(mp) == 0) {
      nodes+=perft(depth - 1);
      take_back_move();
    }
  }
  return nodes;
}

static void perft_root(const int depth) {
  MOVE moves[256];
  MOVE* first=moves;
  MOVE* last=moves;
  unsigned long long total=0ULL;
  const unsigned int start_ms= timer_ms_now();
  generate_legal_moves(&first, &last);
  for (MOVE* mp=first; mp < last; mp++) {
    char ms[16];
    move_to_string(mp, ms);
    if (make_move(mp) == 0) {
      const unsigned long long nodes= perft(depth - 1);
      take_back_move();
      printf("%s: %llu\n", ms, nodes);
      total+=nodes;
    }
  }
  const unsigned int elapsed_ms= timer_ms_now() - start_ms;
  printf("\n");
  printf("Nodes = %llu\n", total);
  printf("Time = %u ms\n", elapsed_ms);
  if (elapsed_ms > 0) {
    const unsigned long long nps= (total * 1000ULL) / elapsed_ms;
    printf("NPS = %llu\n", nps);
  }
  printf("\n");
  fflush(stdout);
}

/*=============================================================================
 * Position Setup
 *=============================================================================*/
void init_position(void){
  int i;

  for (i = 0; i <= NPIECE; i++) bb_piece[i] = 0ULL;
  bb_side[0] = bb_side[1] = 0ULL;

  for (i = 0; i < NSQUARE; i++)
    clear_square(i);

  for (i = 0; i < TOTAL_NUM_PIECE; i++)
    set_piece_in_list(i, DUMMY);

  for (i = 0; i <= NPIECE; i++){
    piece_masks[i].dwrd[0] = 0;
    piece_masks[i].dwrd[1] = 0;
  }
  pos_mask.dwrd[0] = 0;
  pos_mask.dwrd[1] = 0;

  castle[WHITE_PIECE] = CASTLELEFT | CASTLERIGHT;
  castle[BLACK_PIECE] = CASTLELEFT | CASTLERIGHT;

  en_pass_square = DUMMY;
  position_hash = 0;
  halfmove_clock = 0;
}

void set_start_position(void){
  int i;
  static const struct{
    PIECE_NAME name;
    PIECE_COLOR color;
    int sq;
  } start_pieces[] = {
    {ROOK, WHITE_PIECE,0},{KNIGHT, WHITE_PIECE,1},{BISHOP, WHITE_PIECE,2},
    {QUEEN, WHITE_PIECE,3},{KING, WHITE_PIECE,4},{BISHOP, WHITE_PIECE,5},
    {KNIGHT, WHITE_PIECE,6},{ROOK, WHITE_PIECE,7},
    {PAWN, WHITE_PIECE,8},{PAWN, WHITE_PIECE,9},{PAWN, WHITE_PIECE,10},
    {PAWN, WHITE_PIECE,11},{PAWN, WHITE_PIECE,12},{PAWN, WHITE_PIECE,13},
    {PAWN, WHITE_PIECE,14},{PAWN, WHITE_PIECE,15},
    {ROOK, BLACK_PIECE,56},{KNIGHT, BLACK_PIECE,57},{BISHOP, BLACK_PIECE,58},
    {QUEEN, BLACK_PIECE,59},{KING, BLACK_PIECE,60},{BISHOP, BLACK_PIECE,61},
    {KNIGHT, BLACK_PIECE,62},{ROOK, BLACK_PIECE,63},
    {PAWN, BLACK_PIECE,48},{PAWN, BLACK_PIECE,49},{PAWN, BLACK_PIECE,50},
    {PAWN, BLACK_PIECE,51},{PAWN, BLACK_PIECE,52},{PAWN, BLACK_PIECE,53},
    {PAWN, BLACK_PIECE,54},{PAWN, BLACK_PIECE,55}
  };
  int num_pieces = sizeof(start_pieces) / sizeof(start_pieces[0]);

  init_position();

  for (i = 0; i < num_pieces; i++){
    set_square(start_pieces[i].sq,start_pieces[i].name,start_pieces[i].color);
    set_piece_in_list(color_to_king_num(start_pieces[i].color) +
      (start_pieces[i].name == PAWN?8:1),start_pieces[i].sq);
    update_piece_mask(start_pieces[i].name,start_pieces[i].color,
      1 << start_pieces[i].sq,1);
  }

  move_color = WHITE_PIECE;
  material_eval = 0;
  stack_ptr = 0;
}

/*=============================================================================
 * Permanent Brain (Pondering) Helper
 *=============================================================================*/
static int pick_ponder_move(MOVE* out){
  if (pv_length[0] >= 2){
    *out = pv_table[0][1];
    return 1;
  }
  MOVE buf[512];
  MOVE *first = buf, *last = buf;
  generate_legal_moves(&first,&last);
  if (last > first){
    *out = first[0];
    return 1;
  }
  return 0;
}

static void start_pondering(void){
  if (! ponder_mode || ponder_move.from == DUMMY) return;
  if (make_move(&ponder_move) != 0) return;

  is_pondering = 1;
  ponder_hit = 0;
  ponder_buf_ready = 0;
  stop_search = 0;

  /* Ponder time ceiling: use at most half of the opponent's remaining time,
   * capped at 30 s.  This ensures that even if input_available() misses the
   * incoming usermove (e.g. pipe-detection failure), the ponder search will
   * self-terminate long before the engine's own clock runs out, preventing
   * the time forfeits that occurred with the previous 600-second ceiling. */
  int opp_time_cs = (engine_color == WHITE_PIECE)?btime:wtime;
  int ponder_limit_ms = opp_time_cs * 10 / 3; /* ≤ 33 % of opponent's clock */
  if (ponder_limit_ms < 1000) ponder_limit_ms = 1000;
  if (ponder_limit_ms > 12000) ponder_limit_ms = 12000; /* hard ceiling: 12 s */
  search_time_limit_ms = ponder_limit_ms;

  MOVE dummy;
  search_best_move(&dummy);

  take_back_move();
  is_pondering = 0;
}

/*=============================================================================
 * Play own move: search, make move, send to GUI, then ponder
 *=============================================================================*/
static void play_move(void){
  MOVE move;
  stop_search = 0;
  thinking = 1;

  if (search_best_move(&move)){
    /* FINAL GATE: regenerate the legal move list right here and verify
     * the chosen move is actually in it.  This catches any case where
     * the search or its legality filter produced an incorrect result
     * (e.g. bitboard/mailbox desync causing a false is_legal_move hit).
     * generate_legal_moves now uses the mailbox-based is_legal_move, so
     * this list is guaranteed correct regardless of bitboard state. */
    MOVE legal_buf[512];
    MOVE *lf = legal_buf, *ll = legal_buf;
    generate_legal_moves(&lf,&ll);

    int found = 0;
    MOVE* mp;
    for (mp = lf; mp < ll; mp++){
      if (mp->from == move.from && mp->to == move.to){
        move = *mp; /* adopt canonical flags from the list */
        found = 1;
        break;
      }
    }
    if (! found){
      /* Search returned a move not in the legal list — fall back to
       * the first legal move. */
      if (lf < ll){
        move = *lf;
      } else{
        thinking = 0;
        return; /* no legal moves → checkmate/stalemate */
      }
    }

    if (make_move(&move) == 0){
      send_move_to_gui(&move);
      if (ponder_mode && pick_ponder_move(&ponder_move))
        start_pondering();
    }
  }
  thinking = 0;
}

/*=============================================================================
 * WinBoard Protocol
 *=============================================================================*/
static void send_move_to_gui(MOVE* move){
  char move_str[10];
  move_to_string(move,move_str);
  if (xboard_mode){
    printf("move %s\n",move_str);
  } else{
    char display_str[10];
    move_to_string_display(move,display_str);
    int full_move = (stack_ptr) / 2;
    if (move_color == WHITE_PIECE) /* move_color already flipped after make_move */
      printf("  %d. ... %s\n",full_move,display_str);
    else
      printf("  %d. %s\n",full_move,display_str);
    if (console_hist_ptr < CONSOLE_HIST_MAX)
      strncpy(console_history[console_hist_ptr++],display_str,sizeof(console_history[0]) - 1);
  }
  fflush(stdout);
}

static void send_pong(int n){
  printf("pong %d\n",n);
  fflush(stdout);
}

/*=============================================================================
 * FEN Parser — set_from_fen(fen_string)
 *
 * Supports the full FEN format:
 *   <piece placement> <active color> <castling> <en passant> <halfmove> <fullmove>
 * Missing fields default to standard start-position values.
 *=============================================================================*/
static int set_from_fen(const char* fen){
  if (! fen || ! *fen) return 0;

  init_position();

  /* ---- 1. Piece placement ---- */
  int rank = 7, file = 0;
  const char* p = fen;

  while (*p && *p != ' '){
    char c = *p++;
    if (c == '/'){
      rank--;
      file = 0;
    } else if (c >= '1' && c <= '8'){
      file += c - '0';
    } else{
      PIECE_NAME name = NOPIECE;
      PIECE_COLOR color = (c >= 'A' && c <= 'Z')?WHITE_PIECE:BLACK_PIECE;
      char lc = (char)my_tolower((unsigned char)c);
      switch (lc){
      case 'k': name = KING;
        break;
      case 'q': name = QUEEN;
        break;
      case 'r': name = ROOK;
        break;
      case 'b': name = BISHOP;
        break;
      case 'n': name = KNIGHT;
        break;
      case 'p': name = PAWN;
        break;
      default: file++;
        continue;
      }
      if (rank < 0 || rank > 7 || file < 0 || file > 7){
        file++;
        continue;
      }
      SQUARE_NUM sq = rank * 8 + file;
      set_square(sq,name,color);
      /* Update legacy piece list / masks */
      update_piece_mask(name,color,(DWRD)(1 << sq),1);
      file++;
    }
  }

  /* ---- 2. Active color ---- */
  while (*p == ' ') p++;
  if (*p == 'b' || *p == 'B') move_color = BLACK_PIECE;
  else move_color = WHITE_PIECE;
  while (*p && *p != ' ') p++;

  /* ---- 3. Castling rights ---- */
  castle[WHITE_PIECE] = 0;
  castle[BLACK_PIECE] = 0;
  while (*p == ' ') p++;
  if (*p == '-'){
    p++;
  } else{
    while (*p && *p != ' '){
      switch (*p++){
      case 'K': castle[WHITE_PIECE] |= CASTLERIGHT;
        break;
      case 'Q': castle[WHITE_PIECE] |= CASTLELEFT;
        break;
      case 'k': castle[BLACK_PIECE] |= CASTLERIGHT;
        break;
      case 'q': castle[BLACK_PIECE] |= CASTLELEFT;
        break;
      }
    }
  }

  /* ---- 4. En passant ---- */
  en_pass_square = DUMMY;
  while (*p == ' ') p++;
  if (*p != '-' && *p >= 'a' && *p <= 'h' && *(p + 1) >= '1' && *(p + 1) <= '8'){
    int ep_file = *p - 'a';
    int ep_rank = *(p + 1) - '1';
    en_pass_square = ep_rank * 8 + ep_file;
    p += 2;
  }
  while (*p && *p != ' ') p++;

  /* ---- 5. Halfmove clock ---- */
  while (*p == ' ') p++;
  if (*p >= '0' && *p <= '9'){
    halfmove_clock = atoi(p);
    while (*p && *p != ' ') p++;
  }
  /* fullmove number ignored */

  material_eval = 0;
  stack_ptr = 0;

  /* Recompute legacy position hash for repetition detection */
  position_hash = (unsigned long)compute_zobrist_hash();

  /* Sanity: if kings are missing (malformed FEN) restore start position */
  if (king_sq[WHITE_PIECE] == DUMMY || king_sq[BLACK_PIECE] == DUMMY){
    set_start_position();
    return 0;
  }

  return 1;
}

static void process_xboard_command(char* cmd){
  MOVE move;
  char* p;
  int n;

  if (strcmp(cmd,"xboard") == 0){
    xboard_mode = 1;
    printf("\n");
    fflush(stdout);
  } else if (strncmp(cmd,"protover",8) == 0){
    sscanf(cmd + 8,"%d",&n);
    if (n >= 2){
      printf("feature myname=\"Kaissa\"\n");
      printf("feature ping=1\n");
      printf("feature setboard=1\n");
      printf("feature san=0\n");
      printf("feature usermove=1\n");
      printf("feature time=1\n");
      printf("feature draw=0\n");
      printf("feature sigint=0\n");
      printf("feature sigterm=0\n");
      printf("feature reuse=0\n");
      printf("feature analyze=0\n");
      printf("feature colors=1\n");
      printf("feature ics=0\n");
      printf("feature name=0\n");
      printf("feature pause=0\n");
      printf("feature nps=0\n");
      printf("feature done=1\n");
      fflush(stdout);
    }
  } else if (strncmp(cmd,"accepted",8) == 0 ||
    strncmp(cmd,"rejected",8) == 0){} else if (strcmp(cmd,"new") == 0){
    init_position();
    set_start_position();
    force_mode = 0;
    thinking = 0;
    stop_search = 1;
    move_color = WHITE_PIECE;
    analyze_mode = 0;
    stack_ptr = 0;
    ponder_move.from = DUMMY;
    ponder_buf_ready = 0;
    tt_clear();
    clear_killers_history();
  } else if (strncmp(cmd,"setboard",8) == 0){
    const char* fen = cmd + 8;
    while (*fen == ' ') fen++;
    stop_search = 1;
    thinking = 0;
    ponder_move.from = DUMMY;
    ponder_buf_ready = 0;
    tt_clear();
    clear_killers_history();
    if (! set_from_fen(fen)){
      /* Malformed FEN — stay on current position */
      printf("tellusererror Bad FEN: %s\n",fen);
      fflush(stdout);
    }
  } else if (strcmp(cmd,"force") == 0){
    force_mode = 1;
    thinking = 0;
    stop_search = 1;
  } else if (strcmp(cmd,"go") == 0){
    engine_color = move_color; /* engine plays whichever side is to move */
    force_mode = 0;
    analyze_mode = 0;
    play_move();
  } else if (strcmp(cmd,"quit") == 0){
    exit(0);
  } else if (strcmp(cmd,"?") == 0){
    stop_search = 1;
  } else if (strcmp(cmd,"hard") == 0){
    ponder_mode = 1;
  } else if (strcmp(cmd,"easy") == 0){
    ponder_mode = 0;
    stop_search = 1;
  } else if (strcmp(cmd,"random") == 0){} else if (strcmp(cmd,"computer") == 0){} else if (strncmp(cmd,"result",6) == 0){
    force_mode = 1;
    thinking = 0;
    stop_search = 1;
  } else if (strncmp(cmd,"sd",2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')){
    sscanf(cmd + 2,"%d",&search_depth);
    if (search_depth < 1) search_depth = 1;
    if (search_depth > MAXDEPTH) search_depth = MAXDEPTH;
  } else if (strncmp(cmd,"st",2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')){
    int secs = 5;
    sscanf(cmd + 2,"%d",&secs);
    search_time_limit_ms = secs * 1000;
    time_control = 1;
    movestogo = 0;
    wtime = 0;
    btime = 0;
  } else if (strncmp(cmd,"usermove",8) == 0){
    p = cmd + 8;
    while (*p == ' ') p++;
    if (parse_move(p,&move)){
      /* make_move() validates color/piece; is_legal_move() would call
       * make_move() + take_back() internally — avoid that double work. */
      if (! is_legal_move(&move)){
        printf("Illegal move: %s\n",p);
        fflush(stdout);
      } else if (make_move(&move) == 0){
        if (! force_mode && ! analyze_mode)
          play_move();
      }
    } else{
      printf("Illegal move: %s\n",p);
      fflush(stdout);
    }
  } else if (strncmp(cmd,"level",5) == 0){
    int moves = 0, base_min = 0, base_sec = 0, inc = 0;
    char base_str[32] = "";
    sscanf(cmd + 6,"%d %31s %d",&moves,base_str,&inc);

    if (strchr(base_str,':'))
      sscanf(base_str,"%d:%d",&base_min,&base_sec);
    else
      base_min = atoi(base_str);

    int base_total_sec = base_min * 60 + base_sec;
    movestogo = moves;
    /* winc/binc must be in centiseconds (same unit as wtime/btime).
     * The level command gives 'inc' in whole seconds, so × 100.
     * The old code used × 1000, producing a 10× overestimate that
     * inflated time allocation through get_search_time_ms(). */
    winc = binc = inc * 100;
    time_control = 1;

    if (base_total_sec <= 60) search_depth = 6;
    else if (base_total_sec <= 300) search_depth = 8;
    else search_depth = 10;
    if (search_depth > MAXDEPTH) search_depth = MAXDEPTH;
  } else if (strncmp(cmd,"time",4) == 0){
    sscanf(cmd + 4,"%d",&n);
    /* "time N" always gives the ENGINE's own remaining time, regardless of
     * move_color (which may have flipped after the engine's last make_move). */
    if (engine_color == WHITE_PIECE) wtime = n;
    else btime = n;
  } else if (strncmp(cmd,"otim",4) == 0){
    sscanf(cmd + 4,"%d",&n);
    /* "otim N" always gives the OPPONENT's remaining time. */
    if (engine_color == WHITE_PIECE) btime = n;
    else wtime = n;
  } else if (strcmp(cmd,"post") == 0){
    post_mode = 1;
  } else if (strcmp(cmd,"nopost") == 0){
    post_mode = 0;
  } else if (strcmp(cmd,"analyze") == 0){
    analyze_mode = 1;
    force_mode = 1;
    thinking = 1;
    stop_search = 0;
  } else if (strcmp(cmd,"exit") == 0){
    analyze_mode = 0;
    force_mode = 0;
    stop_search = 1;
  } else if (strncmp(cmd,"ping",4) == 0){
    sscanf(cmd + 4,"%d",&n);
    send_pong(n);
  } else if (strcmp(cmd,"white") == 0){
    engine_color = WHITE_PIECE;
    move_color = WHITE_PIECE;
    force_mode = 0;
  } else if (strcmp(cmd,"black") == 0){
    engine_color = BLACK_PIECE;
    move_color = BLACK_PIECE;
    force_mode = 0;
  } else{
    /* Bare algebraic move (e.g. "e2e4") without the "usermove" prefix —
     * accepted for backward compatibility with older GUIs. */
    int len = (int)strlen(cmd);
    if ((len == 4 || len == 5) &&
      (cmd[0] >= 'a' && cmd[0] <= 'h') &&
      (cmd[1] >= '1' && cmd[1] <= '8') &&
      (cmd[2] >= 'a' && cmd[2] <= 'h') &&
      (cmd[3] >= '1' && cmd[3] <= '8')){
      if (parse_move(cmd,&move)){
        if (! is_legal_move(&move)){
          printf("Illegal move: %s\n",cmd);
          fflush(stdout);
        } else if (make_move(&move) == 0){
          if (! force_mode && ! analyze_mode)
            play_move();
        }
      }
    }
  }
}
static void print_uci_score(int score, int side_to_move) {

  /* Convert to engine perspective */
  if (side_to_move != engine_color)
    score=-score;

  if (score >= MAT_INFINITY - 512) {

    int mate=
      (MAT_INFINITY - score + 1) / 2;

    if (mate < 1)
      mate=1;

    printf("score mate %d ", mate);
  }

  else if (score <= -MAT_INFINITY + 512) {

    int mate=
      -((MAT_INFINITY + score + 1) / 2);

    if (mate > -1)
      mate=-1;

    printf("score mate %d ", mate);
  }

  else {

    printf("score cp %d ", score);
  }
}

static void send_uci_bestmove(MOVE* move){
  char buf[16];

  move_to_string(move,buf);

  printf("bestmove %s\n",buf);

  fflush(stdout);
}

static void uci_go_search(void){
  MOVE move;

  stop_search = 0;
  thinking = 1;

  if (search_best_move(&move)){
    MOVE legal_buf[512];
    MOVE *lf = legal_buf, *ll = legal_buf;

    generate_legal_moves(&lf,&ll);

    int found = 0;

    for (MOVE* mp = lf; mp < ll; mp++){
      if (mp->from == move.from &&
        mp->to == move.to){
        move = *mp;
        found = 1;
        break;
      }
    }

    if (! found){
      if (lf < ll){
        move = *lf;
      } else{
        thinking = 0;

        printf("bestmove 0000\n");
        fflush(stdout);

        return;
      }
    }

    if (make_move(&move) == 0){
      send_uci_bestmove(&move);
    }
  } else{
    printf("bestmove 0000\n");
    fflush(stdout);
  }

  thinking = 0;
}

static void uci_new_game(void){
  init_position();
  set_start_position();

  force_mode = 0;
  thinking = 0;
  stop_search = 1;

  move_color = WHITE_PIECE;

  analyze_mode = 0;

  stack_ptr = 0;

  ponder_move.from = DUMMY;
  ponder_buf_ready = 0;

  tt_clear();
  clear_killers_history();
}

static void parse_uci_position(char* cmd){
  char* p = cmd + 8;

  MOVE move;

  while (*p == ' ')
    p++;

  /*---------------------------------------------------------
   * position startpos
   *---------------------------------------------------------*/
  if (strncmp(p,"startpos",8) == 0){
    init_position();
    set_start_position();

    p += 8;
  }

  /*---------------------------------------------------------
   * position fen ...
   *---------------------------------------------------------*/
  else if (strncmp(p,"fen",3) == 0){
    char fen[256];
    int i = 0;

    p += 3;

    while (*p == ' ')
      p++;

    char* moves_ptr = strstr(p," moves");

    if (moves_ptr){
      while (p < moves_ptr && i < 255)
        fen[i++] = *p++;
    } else{
      while (*p && i < 255)
        fen[i++] = *p++;
    }

    fen[i] = '\0';

    if (! set_from_fen(fen)){
      printf("info string Bad FEN: %s\n",fen);
      fflush(stdout);

      return;
    }

    p = moves_ptr;
  }

  /*---------------------------------------------------------
   * Apply moves
   *---------------------------------------------------------*/
  if (p){
    char* moves = strstr(p,"moves");

    if (moves){
      moves += 5;

      while (*moves){
        char move_str[16];
        int j = 0;

        while (*moves == ' ')
          moves++;

        while (*moves && *moves != ' ' && j < 15)
          move_str[j++] = *moves++;

        move_str[j] = '\0';

        if (j == 0)
          break;

        if (parse_move(move_str,&move)){
          if (is_legal_move(&move)){
            make_move(&move);
          } else{
            printf("info string Illegal move: %s\n",move_str);
            fflush(stdout);

            return;
          }
        } else{
          printf("info string Cannot parse move: %s\n",move_str);
          fflush(stdout);

          return;
        }
      }
    }
  }
}

static void parse_uci_go(char* cmd){
  char* p;

  stop_search = 0;

  /*---------------------------------------------------------
   * Defaults
   *---------------------------------------------------------*/
  search_depth = MAXDEPTH;

  time_control = 0;

  /*---------------------------------------------------------
   * depth
   *---------------------------------------------------------*/
  p = strstr(cmd,"depth");

  if (p){
    int d = MAXDEPTH;

    sscanf(p + 5,"%d",&d);

    if (d < 1)
      d = 1;

    if (d > MAXDEPTH)
      d = MAXDEPTH;

    search_depth = d;
  }

  /*---------------------------------------------------------
   * movetime
   *---------------------------------------------------------*/
  p = strstr(cmd,"movetime");

  if (p){
    int ms = 1000;

    sscanf(p + 8,"%d",&ms);

    if (ms < 1)
      ms = 1;

    search_time_limit_ms = ms;

    time_control = 1;
  }

  /*---------------------------------------------------------
   * wtime
   *---------------------------------------------------------*/
  p = strstr(cmd,"wtime");

  if (p){
    sscanf(p + 5,"%d",&wtime);

    /* convert ms -> centiseconds */
    wtime /= 10;

    time_control = 1;
  }

  /*---------------------------------------------------------
   * btime
   *---------------------------------------------------------*/
  p = strstr(cmd,"btime");

  if (p){
    sscanf(p + 5,"%d",&btime);

    btime /= 10;

    time_control = 1;
  }

  /*---------------------------------------------------------
   * winc
   *---------------------------------------------------------*/
  p = strstr(cmd,"winc");

  if (p){
    sscanf(p + 4,"%d",&winc);

    winc /= 10;
  }

  /*---------------------------------------------------------
   * binc
   *---------------------------------------------------------*/
  p = strstr(cmd,"binc");

  if (p){
    sscanf(p + 4,"%d",&binc);

    binc /= 10;
  }

  /*---------------------------------------------------------
   * movestogo
   *---------------------------------------------------------*/
  p = strstr(cmd,"movestogo");

  if (p){
    sscanf(p + 11,"%d",&movestogo);
  }

  /*---------------------------------------------------------
   * Infinite analysis
   *---------------------------------------------------------*/
  p = strstr(cmd,"infinite");

  if (p){
    search_time_limit_ms = 86400000;
    time_control = 1;
  }

  uci_go_search();
}
static void parse_uci_setoption(char* cmd) {

  char name[128]="";
  char value[128]="";

  char* p;

  p=strstr(cmd, "name");

  if (!p)
    return;

  p+=4;

  while (*p == ' ')
    p++;

  char* v=strstr(p, " value");

  if (v) {

    int len=(int)(v - p);

    if (len > 127)
      len=127;

    strncpy(name, p, len);

    name[len]='\0';

    v+=6;

    while (*v == ' ')
      v++;

    strncpy(value, v, 127);

    value[127]='\0';

  }
  else {

    strncpy(name, p, 127);

    name[127]='\0';
  }

  /*-----------------------------------------
   * Hash
   *-----------------------------------------*/
  if (strcmp(name, "Hash") == 0) {

    int mb=atoi(value);

    if (mb < 1)
      mb=1;

    if (mb > 512)
      mb=512;

    /* TODO:
     * implement tt_resize_mb(mb)
     */

    tt_clear();
  }

  /*-----------------------------------------
   * Ponder
   *-----------------------------------------*/
  else if (strcmp(name, "Ponder") == 0) {

    if (strcmp(value, "true") == 0)
      ponder_mode=1;
    else
      ponder_mode=0;
  }
}

static void process_uci_command(char* cmd){
  if (strcmp(cmd,"uci") == 0){
    printf("id name Kaissa\n");
    printf("id author Name\n");
    printf("option name Hash type spin default 16 min 1 max 512\n");
    printf("option name Ponder type check default true\n");
    printf("uciok\n");
    fflush(stdout);
  } else if (strcmp(cmd,"isready") == 0){
    printf("readyok\n");
    fflush(stdout);
  } else if (strcmp(cmd,"ucinewgame") == 0){
    uci_new_game();
  } else if (strncmp(cmd, "setoption", 9) == 0) {
    parse_uci_setoption(cmd);
  } else if (strncmp(cmd,"position",8) == 0){
    parse_uci_position(cmd);
  } else if (strcmp(cmd, "ponderhit") == 0) {
    is_pondering=0;
  } else if (strncmp(cmd,"go",2) == 0){
    parse_uci_go(cmd);
  } else if (strcmp(cmd,"stop") == 0){
    stop_search = 1;
  } else if (strncmp(cmd, "perft", 5) == 0) {
    int depth=1;
    sscanf(cmd + 5, "%d", &depth);
    perft_root(depth);
  } else if (strcmp(cmd,"quit") == 0){
    exit(0);
  }
}

/*=============================================================================
 * Engine Initialization
 *=============================================================================*/
void init_engine(void){
  init_bit_quantity();
  init_direction_masks();
  init_directions();
  init_byteshift();
  init_trans_mask();
  init_bb_attack_tables();
  timer_init_portable();
  init_zobrist();
  tt_clear();
  clear_killers_history();

  ponder_mode = 0;
  is_pondering = 0;
  ponder_hit = 0;
  ponder_buf_ready = 0;
  ponder_move.from = DUMMY;

  pos_sp = pos_stack;
  level = 0;
  stack_ptr = 0;

  init_position();
  set_start_position();

  search_depth = 8;
  xboard_mode = 0;
  post_mode = 1;
  force_mode = 0;
  analyze_mode = 0;
  time_control = 0;
  wtime = 0;
  btime = 0;
  movestogo = 0;
}

/*=============================================================================
 * Main Function
 *=============================================================================*/
int main(int argc, char* argv[]){
  char line[INPUT_BUFFER_SIZE];

  /*---------------------------------------------------------
   * Parse command-line arguments
   *---------------------------------------------------------*/
  for (int i = 1; i < argc; i++){
    if (strcmp(argv[i],"--xboard") == 0 ||
      strcmp(argv[i],"-xboard") == 0){
      input_mode = MODE_XBOARD;
      xboard_mode = 1;
    } else if (strcmp(argv[i],"--uci") == 0 ||
      strcmp(argv[i],"-uci") == 0){
      input_mode = MODE_UCI;
    }
  }

  /*---------------------------------------------------------
   * Engine initialization
   *---------------------------------------------------------*/
  init_engine();

  /*---------------------------------------------------------
   * Console startup banner
   *---------------------------------------------------------*/
  if (input_mode == MODE_CONSOLE){
    printf("\n");
    printf("  +=====================================================+\n");
    printf("  |         K A I S S A   Chess Engine                 |\n");
    printf("  |   Soviet champion 1971-1975  *  IFIP World Cup 1974 |\n");
    printf("  +=====================================================+\n");
    printf("\n");
    printf("  Console mode.  Type 'help' for a list of commands.\n");
    printf("  You play White.  Enter moves in long algebraic notation:\n");
    printf("  e.g.  e2e4   or   e2-e4\n\n");

    fflush(stdout);

    /* Human plays White */
    engine_color = BLACK_PIECE;

    print_board();
  }

  /*---------------------------------------------------------
   * Main input loop
   *---------------------------------------------------------*/
  while (1){
    const char* incoming;

    if (ponder_buf_ready){
      ponder_buf_ready = 0;
      incoming = ponder_buf;
    } else{
      if (input_mode == MODE_CONSOLE){
        printf("> ");
        fflush(stdout);
      }

      if (! read_line_nb(line,sizeof(line)))
        break;

      incoming = line;
    }

    /*-----------------------------------------------------
     * Auto-detect protocol
     *-----------------------------------------------------*/
    if (input_mode == MODE_CONSOLE){
      if (strcmp(incoming,"xboard") == 0){
        input_mode = MODE_XBOARD;
        xboard_mode = 1;
      } else if (strcmp(incoming,"uci") == 0){
        input_mode = MODE_UCI;
      }
    }

    /*-----------------------------------------------------
     * Dispatch command
     *-----------------------------------------------------*/
    switch (input_mode){
    case MODE_XBOARD:
      process_xboard_command((char*)incoming);
      break;

    case MODE_UCI:
      process_uci_command((char*)incoming);
      break;

    default:
      process_console_command((char*)incoming);
      break;
    }
  }

  return 0;
}
