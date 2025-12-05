#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_primitives.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#define SCREEN_W 1500
#define SCREEN_H 950
#define max_fall 90.0f   // sabia que que polvos tem tres corações dois pra bombear sangue para as branqueas e o outro pro resto do corpo

// posição da torre Tesla 
#define TESLA_POS_X 740.0f
#define TESLA_POS_Y 670.0f

// Tesla attack  
#define TESLA_ATTACK_PERIOD 4.0f
#define TESLA_ATTACK_ACTIVE_FRAC 0.35f
#define TESLA_DEBUG_W 160.0f
#define TESLA_DEBUG_H 100.0f
static float tesla_timer = 0.0f;
static bool tesla_active = false;

typedef enum { STATE_MENU = 0, STATE_FASE1, STATE_FASE2, STATE_FINAL, STATE_LOADING_GRAV, STATE_LOADING_ELETRI } GameState;
typedef enum { PERSON_IDLE = 0, PERSON_RUN, PERSON_JUMP, PERSON_DEAD } PersonState;

typedef enum { PLATFORM_STATIC = 0, PLATFORM_VERTICAL } PlatformType;

typedef struct {
    PlatformType type;
    float x, y;
    float prevY;        // posição y da plataforma no frame anterior
    float w, h;
    float minY, maxY;
    float vy;           // velocidade em pixels por segundo
    bool collidable;
    bool inverted;
} Platform;

typedef struct {
    ALLEGRO_DISPLAY* display;
    ALLEGRO_TIMER* timer;
    ALLEGRO_EVENT_QUEUE* queue;
    ALLEGRO_FONT* font;
    GameState state;
    bool running;
    bool redraw;
    int current_phase;

    // tempo total
    double total_time;       // tempo acumulado total desde o começo da primeira fase
    bool total_running;      // indica se o total está correndo

    bool show_help;
} Game;

typedef struct {
    ALLEGRO_BITMAP* spritesheet;
    ALLEGRO_BITMAP* fundo1;
    ALLEGRO_BITMAP* fundo2;
    ALLEGRO_BITMAP* chao;
    ALLEGRO_BITMAP* plat;
    ALLEGRO_BITMAP* plat_small;
    ALLEGRO_BITMAP* laser;
    ALLEGRO_BITMAP* raio;
    ALLEGRO_BITMAP* tesla;
    ALLEGRO_BITMAP* esplosao;
    ALLEGRO_BITMAP* gravloading;
    ALLEGRO_BITMAP* eletriloading;
    ALLEGRO_BITMAP* tela_incial;
} Assets;

typedef struct {
    int cols, rows;
    int sprite_w, sprite_h;
    int total_frames;
    ALLEGRO_BITMAP** frames;
    int* final_w;
    int* final_h;
    int* row_w;
    int* row_h;
} Anim;

typedef struct {
    float x1, y1;
    float x2, y2;
    bool active;
    float timer;
    float period;
    float active_frac;
} Laser;

typedef struct {
    ALLEGRO_BITMAP** frames;
    int cols, rows;
    int total_frames;
    int sprite_w, sprite_h;
    int frame, frame_timer;
    int frame_delay;
} LaserAnim;

typedef struct {
    ALLEGRO_BITMAP* background;
    Platform* platforms;
    int platform_count;
    float spawn_x;
    float spawn_y;
    float gravity;
    float jump_vel;
    float move_speed;
    float hole_x1, hole_x2, hole_y1, hole_y2;
    float hole_gravity_factor;
    Laser* lasers;
    int laser_count;
    LaserAnim laser_anim;

    double phase_time;       // tempo acumulado nesta fase (segundos)
    bool phase_active;       // true enquanto jogador estiver nesta fase
    bool phase_completed;    // true quando fase foi concluída (passagem adiante)
} Phase;

typedef struct {
    float posX;
    float feetY;
    bool jumping;
    float velY;
    bool key_left, key_right;
    PersonState state, prev_state;
    int frame, frame_timer;
    float gravity, jump_vel, move_speed;
    float target_height_px;
    int chao_y;
    int chao_w, chao_h;
    int chao_extra_pixels;
    float hitbox_offset_left, hitbox_offset_right;
    bool want_jump;
    float land_timer;
    float land_duration;
    bool just_landed;
    int vida;
} Player;

// Tesla animation struct
typedef struct {
    ALLEGRO_BITMAP** frames;
    int cols, rows;
    int total_frames;
    int sprite_w, sprite_h;
    int frame, frame_timer;
    int frame_delay;
    float scale;
} TeslaAnim;

static TeslaAnim tesla_anim = { 0 };

// Prototypes
static bool init_allegro(Game* g);
static bool load_assets(Assets* a);
static bool setup_animation(const Assets* a, Anim* anim);
static bool build_frames(const Assets* a, Anim* anim);
static void free_animation(Anim* anim);
static void update_game(Game* g, Player* p, const Anim* anim, const Assets* a, Phase* phases, int phase_count);
static void render_game(const Game* g, const Assets* a, const Anim* anim, const Player* p, Phase* phases);
static void cleanup_all(Game* g, Assets* a, Anim* anim, Phase* phases, int phase_count);
static int row_for_state(PersonState s);
static Phase* create_phases(const Assets* a, int* out_count);
static void destroy_phases(Phase* phases, int count);
static bool setup_laser_animation(const Assets* a, LaserAnim* la, int cols, int rows);

// Tesla animation helpers
static bool setup_tesla_animation(const Assets* a, TeslaAnim* ta, int cols, int rows, int frame_delay, float scale);
static void tesla_anim_destroy(TeslaAnim* ta);

// Debug utility
static void debug_print_cwd_and_bitmaps(const Assets* a) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) fprintf(stderr, "DEBUG: CWD: %s\n", cwd);
    else fprintf(stderr, "DEBUG: getcwd failed\n");
    fprintf(stderr, "DEBUG: bitmaps: sprites=%p fundo1=%p fundo2=%p chao=%p plat=%p plat_small=%p laser=%p tesla=%p esplosao=%p\n",
        (void*)a->spritesheet, (void*)a->fundo1, (void*)a->fundo2, (void*)a->chao, (void*)a->plat, (void*)a->plat_small, (void*)a->laser, (void*)a->tesla, (void*)a->esplosao);
    if (a->laser) fprintf(stderr, "DEBUG: laser size: %d x %d\n", al_get_bitmap_width(a->laser), al_get_bitmap_height(a->laser));
    if (a->spritesheet) fprintf(stderr, "DEBUG: spritesheet size: %d x %d\n", al_get_bitmap_width(a->spritesheet), al_get_bitmap_height(a->spritesheet));
    if (a->tesla) fprintf(stderr, "DEBUG: tesla size: %d x %d\n", al_get_bitmap_width(a->tesla), al_get_bitmap_height(a->tesla));
    if (a->esplosao) fprintf(stderr, "DEBUG: esplosao size: %d x %d\n", al_get_bitmap_width(a->esplosao), al_get_bitmap_height(a->esplosao));
}

void show_loading_bitmap(ALLEGRO_BITMAP* img, ALLEGRO_EVENT_QUEUE* queue) {
    if (!img) return;

    int w = al_get_bitmap_width(img);
    int h = al_get_bitmap_height(img);
    float scale_x = (float)SCREEN_W / (float)w;
    float scale_y = (float)SCREEN_H / (float)h;
    float scale = fminf(scale_x, scale_y);
    int draw_w = (int)(w * scale);
    int draw_h = (int)(h * scale);
    int draw_x = (SCREEN_W - draw_w) / 2;
    int draw_y = (SCREEN_H - draw_h) / 2;

    bool waiting = true;
    while (waiting) {
        al_clear_to_color(al_map_rgb(0, 0, 0));
        al_draw_scaled_bitmap(img, 0, 0, w, h, draw_x, draw_y, draw_w, draw_h, 0);
        al_flip_display();

        ALLEGRO_EVENT ev;
        al_wait_for_event(queue, &ev);
        if (ev.type == ALLEGRO_EVENT_KEY_DOWN && ev.keyboard.keycode == ALLEGRO_KEY_ENTER) {
            waiting = false;
        }
    }
}

// Implementation
int main(void) {
    Game g = { 0 };
    Assets a = { 0 };
    Anim anim = { 0 };
    Player p = { 0 };
    Phase* phases = NULL;
    int phase_count = 0;

    fprintf(stderr, "DEBUG: Program start\n");
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) fprintf(stderr, "DEBUG: Starting CWD: %s\n", cwd);

    if (!init_allegro(&g)) { fprintf(stderr, "ERROR: init_allegro failed\n"); return -1; }
    fprintf(stderr, "DEBUG: Allegro initialized\n");

    if (!load_assets(&a)) { fprintf(stderr, "ERROR: load_assets failed\n"); cleanup_all(&g, &a, &anim, phases, phase_count); return -1; }
    debug_print_cwd_and_bitmaps(&a);

    if (!setup_animation(&a, &anim)) { fprintf(stderr, "ERROR: setup_animation failed\n"); cleanup_all(&g, &a, &anim, phases, phase_count); return -1; }
    if (!build_frames(&a, &anim)) { fprintf(stderr, "ERROR: build_frames failed\n"); cleanup_all(&g, &a, &anim, phases, phase_count); return -1; }

    phases = create_phases(&a, &phase_count);
    fprintf(stderr, "DEBUG: create_phases returned %p phase_count=%d\n", (void*)phases, phase_count);
    if (!phases) { fprintf(stderr, "Erro ao criar fases\n"); cleanup_all(&g, &a, &anim, phases, phase_count); return -1; }

    // initialize tesla animation if esplosao present (3 cols x 2 rows as your image)
    if (!setup_tesla_animation(&a, &tesla_anim, 3, 2, 6, 1.2f)) {
        fprintf(stderr, "Aviso: nao foi possivel inicializar animacao da tesla\n");
    }

    g.show_help = true;

    p.target_height_px = 300.0f;
    p.chao_w = a.chao ? al_get_bitmap_width(a.chao) : 0;
    p.chao_h = a.chao ? al_get_bitmap_height(a.chao) : 0;
    p.chao_extra_pixels = 200;
    p.chao_y = 900;
    p.hitbox_offset_left = 30;
    p.hitbox_offset_right = 30;
    p.land_timer = 0.0f;
    p.land_duration = 0.18f;
    p.just_landed = false;
    p.vida = 3;

    g.state = STATE_MENU;
    g.running = true;
    g.redraw = true;
    g.current_phase = 0;
    g.total_time = 0.0;
    g.total_running = false;

    // ensure phase timers are zeroed (create_phases also initializes them)
    if (phases && phase_count > 0) {
        for (int i = 0; i < phase_count; ++i) {
            phases[i].phase_time = 0.0;
            phases[i].phase_active = false;
            phases[i].phase_completed = false;
        }
    }

    if (g.timer) al_start_timer(g.timer);

    fprintf(stderr, "DEBUG: Entering main loop\n");
    while (g.running) {
        ALLEGRO_EVENT ev;
        al_wait_for_event(g.queue, &ev);

        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) g.running = false;

        if (ev.type == ALLEGRO_EVENT_KEY_DOWN) {
            switch (ev.keyboard.keycode) {
            case ALLEGRO_KEY_ESCAPE: g.running = false; break;
            case ALLEGRO_KEY_ENTER:
                if (g.state == STATE_MENU) {
                    g.show_help = !g.show_help;
                    show_loading_bitmap(a.gravloading, g.queue);
                    if (phases && phase_count > 0) {
                        for (int i = 0; i < phase_count; ++i) {
                            phases[i].phase_time = 0.0;
                            phases[i].phase_active = false;
                            phases[i].phase_completed = false;
                        }
                        phases[0].phase_active = true;
                        phases[0].phase_time = 0.0;
                    }
                    g.total_time = 0.0;
                    g.total_running = true;

                    g.state = STATE_FASE1;
                    g.current_phase = 0;
                    if (phases && phase_count > 0) {
                        p.posX = phases[g.current_phase].spawn_x;
                        p.feetY = phases[g.current_phase].spawn_y;
                        p.velY = 0; p.jumping = false; p.want_jump = false;
                        p.gravity = phases[g.current_phase].gravity;
                        p.jump_vel = phases[g.current_phase].jump_vel;
                        p.move_speed = phases[g.current_phase].move_speed;
                        p.frame = p.frame_timer = 0; p.state = p.prev_state = PERSON_IDLE;
                    }
                }
                else if (g.state == STATE_FINAL) g.running = false;
                break;
            case ALLEGRO_KEY_D: p.key_right = true; break;
            case ALLEGRO_KEY_A: p.key_left = true; break;

            case ALLEGRO_KEY_SPACE:
                if (!p.jumping) { p.want_jump = true; p.jumping = true; }
                break;
            case ALLEGRO_KEY_UP:   // aumenta gravidade só na fase 1
                if (g.state == STATE_FASE1 && g.current_phase == 0) {
                    phases[0].gravity += 0.2f;
                    p.gravity = phases[0].gravity;
                    fprintf(stderr, "DEBUG: Gravidade Fase 1 aumentada para %.2f\n", phases[0].gravity);
                }
                break;

            case ALLEGRO_KEY_DOWN: // diminui gravidade só na fase 1
                if (g.state == STATE_FASE1 && g.current_phase == 0) {
                    phases[0].gravity -= 0.2f;
                    if (phases[0].gravity < 0.1f){ phases[0].gravity = 0.1f; // limite mínimo
                    p.gravity = phases[0].gravity;
                    fprintf(stderr, "DEBUG: Gravidade Fase 1 diminuída para %.2f\n", phases[0].gravity);
                }
                break;
               }
            }

        }
        else if (ev.type == ALLEGRO_EVENT_KEY_UP) {
            switch (ev.keyboard.keycode) {
            case ALLEGRO_KEY_D: p.key_right = false; break;
            case ALLEGRO_KEY_A: p.key_left = false; break;
            }
        }

        if (ev.type == ALLEGRO_EVENT_TIMER) {
            g.redraw = true;
            update_game(&g, &p, &anim, &a, phases, phase_count);
            if (g.redraw && al_is_event_queue_empty(g.queue)) {
                g.redraw = false;
                render_game(&g, &a, &anim, &p, phases);
            }
        }
    }

    fprintf(stderr, "DEBUG: Exiting main loop\n");
    cleanup_all(&g, &a, &anim, phases, phase_count);
    return 0;
}

// Allegro init
static bool init_allegro(Game* g) {
    if (!al_init()) return false;
    if (!al_init_image_addon()) return false;
    al_install_keyboard();
    al_init_font_addon();
    if (!al_init_ttf_addon()) return false;
    al_init_primitives_addon();

    g->display = al_create_display(SCREEN_W, SCREEN_H);
    if (!g->display) return false;
    al_set_window_title(g->display, "Jogo - Final");

    g->font = al_load_ttf_font("arial.ttf", 24, 0);
    if (!g->font) g->font = al_create_builtin_font();

    g->timer = al_create_timer(1.0 / 60.0);
    g->queue = al_create_event_queue();
    if (g->queue) {
        if (g->display) al_register_event_source(g->queue, al_get_display_event_source(g->display));
        al_register_event_source(g->queue, al_get_keyboard_event_source());
        if (g->timer) al_register_event_source(g->queue, al_get_timer_event_source(g->timer));
    }

    if (!g->timer || !g->queue) {
        fprintf(stderr, "ERROR: timer or queue not created: timer=%p queue=%p\n", (void*)g->timer, (void*)g->queue);
    }
    return true;
}

// Load assets
static bool load_assets(Assets* a) {
    a->spritesheet = al_load_bitmap("personagem.png");
    a->fundo1 = al_load_bitmap("cenario1.png");
    a->fundo2 = al_load_bitmap("cenario2.png");
    a->chao = al_load_bitmap("chao.png");
    a->plat = al_load_bitmap("plataforma larga.png");
    a->plat_small = al_load_bitmap("plataforma.png");
    a->laser = al_load_bitmap("laser.png");
    a->raio = al_load_bitmap("raio.png");
    a->tesla = al_load_bitmap("tesla.png");
    a->esplosao = al_load_bitmap("explosao eletrica.png");
    a->gravloading = al_load_bitmap("loading gravidade.png");
    a->eletriloading = al_load_bitmap("loading eletricidade.png");
    a->tela_incial = al_load_bitmap("tela_incial.png");

    fprintf(stderr, "DEBUG: load_assets results: sprites=%p fundo1=%p fundo2=%p chao=%p plat=%p plat_small=%p laser=%p tesla=%p esplosao=%p\n",
        (void*)a->spritesheet, (void*)a->fundo1, (void*)a->fundo2, (void*)a->chao, (void*)a->plat, (void*)a->plat_small, (void*)a->laser, (void*)a->tesla, (void*)a->esplosao);

    if (!a->spritesheet || !a->fundo1 || !a->fundo2 || !a->chao || !a->plat) {
        if (!a->spritesheet) fprintf(stderr, "Erro: personagem.png nao carregado\n");
        if (!a->fundo1) fprintf(stderr, "Erro: cenario.png nao carregado\n");
        if (!a->fundo2) fprintf(stderr, "Erro: cenario2.png nao carregado\n");
        if (!a->chao) fprintf(stderr, "Erro: chao.png nao carregado\n");
        if (!a->plat) fprintf(stderr, "Erro: plat.png nao carregado\n");
        return false;
    }
    return true;
}

// Animation setup (personagem)
static bool setup_animation(const Assets* a, Anim* anim) {
    if (!a->spritesheet) {
        fprintf(stderr, "WARN: spritesheet NULL in setup_animation\n");
        return false;
    }
    anim->cols = 10; anim->rows = 5;
    int sheet_w = al_get_bitmap_width(a->spritesheet);
    int sheet_h = al_get_bitmap_height(a->spritesheet);
    if (sheet_w % anim->cols != 0 || sheet_h % anim->rows != 0)
        fprintf(stderr, "Aviso: dimensoes do spritesheet talvez nao divisiveis (%d x %d)\n", sheet_w, sheet_h);
    anim->sprite_w = sheet_w / anim->cols;
    anim->sprite_h = sheet_h / anim->rows;
    anim->total_frames = anim->cols * anim->rows;
    anim->row_w = (int*)malloc(sizeof(int) * anim->rows);
    anim->row_h = (int*)malloc(sizeof(int) * anim->rows);
    anim->frames = (ALLEGRO_BITMAP**)malloc(sizeof(ALLEGRO_BITMAP*) * anim->total_frames);
    anim->final_w = (int*)malloc(sizeof(int) * anim->total_frames);
    anim->final_h = (int*)malloc(sizeof(int) * anim->total_frames);
    if (!anim->row_w || !anim->row_h || !anim->frames || !anim->final_w || !anim->final_h) {
        fprintf(stderr, "ERROR: malloc failed in setup_animation\n");
        return false;
    }
    for (int r = 0; r < anim->rows; r++) { anim->row_w[r] = anim->sprite_w + 4; anim->row_h[r] = anim->sprite_h + 4; }
    return true;
}

static bool build_frames(const Assets* a, Anim* anim) {
    if (!a->spritesheet) return false;
    ALLEGRO_BITMAP** sub = (ALLEGRO_BITMAP**)malloc(sizeof(ALLEGRO_BITMAP*) * anim->total_frames);
    if (!sub) return false;
    memset(sub, 0, sizeof(ALLEGRO_BITMAP*) * anim->total_frames);
    for (int r = 0; r < anim->rows; r++) {
        for (int c = 0; c < anim->cols; c++) {
            int idx = r * anim->cols + c;
            sub[idx] = al_create_sub_bitmap(a->spritesheet, c * anim->sprite_w, r * anim->sprite_h, anim->sprite_w, anim->sprite_h);
            if (!sub[idx]) { fprintf(stderr, "ERROR: sub bitmap creation failed idx=%d\n", idx); for (int i = 0; i < idx; i++) if (sub[i]) al_destroy_bitmap(sub[i]); free(sub); return false; }
        }
    }
    for (int r = 0; r < anim->rows; r++) {
        for (int c = 0; c < anim->cols; c++) {
            int idx = r * anim->cols + c;
            int fw = anim->row_w[r], fh = anim->row_h[r];
            anim->final_w[idx] = fw; anim->final_h[idx] = fh;
            anim->frames[idx] = al_create_bitmap(fw, fh);
            if (!anim->frames[idx]) { fprintf(stderr, "ERROR: anim->frames create failed idx=%d\n", idx); for (int j = 0; j < idx; j++) if (anim->frames[j]) al_destroy_bitmap(anim->frames[j]); free(sub); return false; }
            ALLEGRO_BITMAP* prev = al_get_target_bitmap();
            al_set_target_bitmap(anim->frames[idx]);
            al_clear_to_color(al_map_rgba(0, 0, 0, 0));
            int dest_x = (fw - anim->sprite_w) / 2;
            int dest_y = fh - anim->sprite_h;
            al_draw_bitmap(sub[idx], dest_x, dest_y, 0);
            al_set_target_bitmap(prev);
            al_destroy_bitmap(sub[idx]);
            sub[idx] = NULL;
        }
    }
    free(sub);
    fprintf(stderr, "DEBUG: build_frames finished total_frames=%d\n", anim->total_frames);
    return true;
}

// Setup da animação do laser (se não existir, retorna true com frames=NULL para fallback)
static bool setup_laser_animation(const Assets* a, LaserAnim* la, int cols, int rows) {
    if (!a || !a->laser) {
        la->frames = NULL;
        la->cols = la->rows = la->total_frames = la->sprite_w = la->sprite_h = 0;
        la->frame = la->frame_timer = la->frame_delay = 0;
        fprintf(stderr, "WARN: laser bitmap NULL; laser animation disabled (fallback mode)\n");
        return true;
    }
    if (cols <= 0 || rows <= 0) return false;
    la->cols = cols; la->rows = rows;
    int lw = al_get_bitmap_width(a->laser);
    int lh = al_get_bitmap_height(a->laser);
    if (lw <= 0 || lh <= 0) { fprintf(stderr, "ERROR: laser bitmap has invalid size\n"); return false; }
    la->sprite_w = lw / cols;
    la->sprite_h = lh / rows;
    la->total_frames = cols * rows;
    la->frames = (ALLEGRO_BITMAP**)malloc(sizeof(ALLEGRO_BITMAP*) * la->total_frames);
    if (!la->frames) return false;
    memset(la->frames, 0, sizeof(ALLEGRO_BITMAP*) * la->total_frames);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            la->frames[idx] = al_create_sub_bitmap(a->laser, c * la->sprite_w, r * la->sprite_h, la->sprite_w, la->sprite_h);
            if (!la->frames[idx]) {
                fprintf(stderr, "ERROR: la->frames create failed idx=%d\n", idx);
                for (int i = 0; i < idx; ++i) if (la->frames[i]) al_destroy_bitmap(la->frames[i]);
                free(la->frames);
                la->frames = NULL;
                return false;
            }
        }
    }
    la->frame = 0; la->frame_timer = 0; la->frame_delay = 6;
    fprintf(stderr, "DEBUG: setup_laser_animation ok total_frames=%d sprite=%d x %d\n", la->total_frames, la->sprite_w, la->sprite_h);
    return true;
}

// Tesla animation setup
static bool setup_tesla_animation(const Assets* a, TeslaAnim* ta, int cols, int rows, int frame_delay, float scale) {
    if (!a || !a->esplosao) {
        ta->frames = NULL;
        ta->cols = ta->rows = ta->total_frames = ta->sprite_w = ta->sprite_h = 0;
        ta->frame = ta->frame_timer = ta->frame_delay = 0;
        ta->scale = 1.0f;
        fprintf(stderr, "WARN: esplosao bitmap NULL; tesla animation disabled (fallback)\n");
        return true;
    }
    if (cols <= 0 || rows <= 0) return false;
    ta->cols = cols; ta->rows = rows;
    int w = al_get_bitmap_width(a->esplosao);
    int h = al_get_bitmap_height(a->esplosao);
    if (w <= 0 || h <= 0) return false;
    ta->sprite_w = w / cols;
    ta->sprite_h = h / rows;
    ta->total_frames = cols * rows;
    ta->frames = (ALLEGRO_BITMAP**)malloc(sizeof(ALLEGRO_BITMAP*) * ta->total_frames);
    if (!ta->frames) return false;
    memset(ta->frames, 0, sizeof(ALLEGRO_BITMAP*) * ta->total_frames);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            ta->frames[idx] = al_create_sub_bitmap(a->esplosao, c * ta->sprite_w, r * ta->sprite_h, ta->sprite_w, ta->sprite_h);
            if (!ta->frames[idx]) {
                for (int i = 0; i < idx; ++i) if (ta->frames[i]) al_destroy_bitmap(ta->frames[i]);
                free(ta->frames);
                ta->frames = NULL;
                return false;
            }
        }
    }
    ta->frame = 0; ta->frame_timer = 0; ta->frame_delay = frame_delay; ta->scale = scale;
    fprintf(stderr, "DEBUG: setup_tesla_animation ok total_frames=%d sprite=%d x %d\n", ta->total_frames, ta->sprite_w, ta->sprite_h);
    return true;
}

// Tesla animation destroy
static void tesla_anim_destroy(TeslaAnim* ta) {
    if (!ta) return;
    if (ta->frames) {
        for (int i = 0; i < ta->total_frames; ++i) {
            if (ta->frames[i]) al_destroy_bitmap(ta->frames[i]);
        }
        free(ta->frames);
        ta->frames = NULL;
    }
    ta->total_frames = 0;
    ta->cols = ta->rows = ta->sprite_w = ta->sprite_h = 0;
    ta->frame = ta->frame_timer = ta->frame_delay = 0;
    ta->scale = 1.0f;
}

static Phase* create_phases(const Assets* a, int* out_count) {
    int count = 2;
    Phase* phases = (Phase*)calloc(count, sizeof(Phase));
    if (!phases) return NULL;

    float pw = a->plat ? (float)al_get_bitmap_width(a->plat) : 200.0f;
    float phh = a->plat ? (float)al_get_bitmap_height(a->plat) : 48.0f;
    float psw = a->plat_small ? (float)al_get_bitmap_width(a->plat_small) : 64.0f;
    float psh = a->plat_small ? (float)al_get_bitmap_height(a->plat_small) : 24.0f;

    // FASE 1
    phases[0].background = a->fundo1;
    phases[0].platform_count = 10;
    phases[0].platforms = (Platform*)calloc(phases[0].platform_count, sizeof(Platform));
    if (!phases[0].platforms) { free(phases); return NULL; }
    for (int i = 0; i < phases[0].platform_count; i++) { phases[0].platforms[i].collidable = true; phases[0].platforms[i].inverted = false; }

    phases[0].platforms[0].type = PLATFORM_STATIC; phases[0].platforms[0].x = 100; phases[0].platforms[0].y = 670; phases[0].platforms[0].w = pw; phases[0].platforms[0].h = phh;
    phases[0].platforms[1].type = PLATFORM_STATIC; phases[0].platforms[1].x = 350; phases[0].platforms[1].y = 670; phases[0].platforms[1].w = pw; phases[0].platforms[1].h = phh;
    phases[0].platforms[2].type = PLATFORM_STATIC; phases[0].platforms[2].x = 550; phases[0].platforms[2].y = 150; phases[0].platforms[2].w = pw; phases[0].platforms[2].h = phh;
    phases[0].platforms[3].type = PLATFORM_STATIC; phases[0].platforms[3].x = 600; phases[0].platforms[3].y = 150; phases[0].platforms[3].w = pw; phases[0].platforms[3].h = phh;
    phases[0].platforms[4].type = PLATFORM_VERTICAL; phases[0].platforms[4].x = 800; phases[0].platforms[4].y = 670; phases[0].platforms[4].w = psw; phases[0].platforms[4].h = psh; phases[0].platforms[4].minY = 250; phases[0].platforms[4].maxY = 870; phases[0].platforms[4].vy = -560.0f; // px/s
    phases[0].platforms[5].type = PLATFORM_VERTICAL; phases[0].platforms[5].x = 950; phases[0].platforms[5].y = 670; phases[0].platforms[5].w = psw; phases[0].platforms[5].h = psh; phases[0].platforms[5].minY = 250; phases[0].platforms[5].maxY = 870; phases[0].platforms[5].vy = -170.0f;
    phases[0].platforms[6].type = PLATFORM_VERTICAL; phases[0].platforms[6].x = 1100; phases[0].platforms[6].y = 670; phases[0].platforms[6].w = psw; phases[0].platforms[6].h = psh; phases[0].platforms[6].minY = 250; phases[0].platforms[6].maxY = 870; phases[0].platforms[6].vy = -870.0f;
    phases[0].platforms[7].type = PLATFORM_STATIC; phases[0].platforms[7].x = 30; phases[0].platforms[7].y = 670; phases[0].platforms[7].w = pw; phases[0].platforms[7].h = phh;
    phases[0].platforms[8].type = PLATFORM_STATIC; phases[0].platforms[8].x = 1300; phases[0].platforms[8].y = 670; phases[0].platforms[8].w = pw; phases[0].platforms[8].h = phh;
    phases[0].platforms[9].type = PLATFORM_STATIC; phases[0].platforms[9].x = 1370; phases[0].platforms[9].y = 670; phases[0].platforms[9].w = pw; phases[0].platforms[9].h = phh;

    // inicializa prevY para plataformas móveis/estáticas
    for (int i = 0; i < phases[0].platform_count; ++i) {
        phases[0].platforms[i].prevY = phases[0].platforms[i].y;
    }

    phases[0].spawn_x = phases[0].platforms[0].x + 20;
    phases[0].spawn_y = phases[0].platforms[0].y;
    phases[0].gravity = 1.0f; phases[0].jump_vel = 23.0f; phases[0].hole_gravity_factor = 15.0f; phases[0].move_speed = 6.0f;

    // initialize timers for fase 1
    phases[0].phase_time = 0.0;
    phases[0].phase_active = false;
    phases[0].phase_completed = false;

    // buraco invertido
    {
        float hole_pad_x = 35.99f;
        float hole_pad_y_top = 500.0f;
        float hole_pad_y_bottom = 112.50f;
        float raw_x1 = phases[0].platforms[0].x + phases[0].platforms[0].w * 0.5f;
        float raw_x2 = phases[0].platforms[1].x + phases[0].platforms[1].w * 0.5f;
        phases[0].hole_x1 = raw_x1 + hole_pad_x; phases[0].hole_x2 = raw_x2 - hole_pad_x;
        if (phases[0].hole_x2 <= phases[0].hole_x1) { phases[0].hole_x1 = raw_x1; phases[0].hole_x2 = raw_x2; }
        float plat_top = fminf(phases[0].platforms[0].y, phases[0].platforms[1].y);
        phases[0].hole_y1 = plat_top + hole_pad_y_top;
        phases[0].hole_y2 = (float)(900 - 50) - hole_pad_y_bottom;
        if (phases[0].hole_y2 <= phases[0].hole_y1) { phases[0].hole_y1 = plat_top; phases[0].hole_y2 = plat_top + 200.0f; }
    }

    // FASE 2
    phases[1].background = a->fundo2;
    phases[1].platform_count = 15;
    phases[1].platforms = (Platform*)calloc(phases[1].platform_count, sizeof(Platform));
    if (!phases[1].platforms) { destroy_phases(phases, 1); return NULL; }
    for (int i = 0; i < phases[1].platform_count; i++) { phases[1].platforms[i].collidable = true; phases[1].platforms[i].inverted = false; }

    phases[1].platforms[0].type = PLATFORM_STATIC; phases[1].platforms[0].x = 120; phases[1].platforms[0].y = 650; phases[1].platforms[0].w = pw; phases[1].platforms[0].h = phh;
    phases[1].platforms[1].type = PLATFORM_STATIC; phases[1].platforms[1].x = 420; phases[1].platforms[1].y = 650; phases[1].platforms[1].w = pw; phases[1].platforms[1].h = phh;
    phases[1].platforms[2].type = PLATFORM_STATIC; phases[1].platforms[2].x = 490; phases[1].platforms[2].y = 650; phases[1].platforms[2].w = pw; phases[1].platforms[2].h = phh;
    phases[1].platforms[3].type = PLATFORM_STATIC; phases[1].platforms[3].x = 560; phases[1].platforms[3].y = 650; phases[1].platforms[3].w = pw; phases[1].platforms[3].h = phh;
    phases[1].platforms[4].type = PLATFORM_STATIC; phases[1].platforms[4].x = 620; phases[1].platforms[4].y = 650; phases[1].platforms[4].w = pw; phases[1].platforms[4].h = phh;
    phases[1].platforms[5].type = PLATFORM_STATIC; phases[1].platforms[5].x = 690; phases[1].platforms[5].y = 650; phases[1].platforms[5].w = pw; phases[1].platforms[5].h = phh;
    phases[1].platforms[6].type = PLATFORM_STATIC; phases[1].platforms[6].x = 760; phases[1].platforms[6].y = 650; phases[1].platforms[6].w = pw; phases[1].platforms[6].h = phh;
    phases[1].platforms[7].type = PLATFORM_STATIC; phases[1].platforms[7].x = 820; phases[1].platforms[7].y = 650; phases[1].platforms[7].w = pw; phases[1].platforms[7].h = phh;
    phases[1].platforms[8].type = PLATFORM_STATIC; phases[1].platforms[8].x = 890; phases[1].platforms[8].y = 650; phases[1].platforms[8].w = pw; phases[1].platforms[8].h = phh;
    phases[1].platforms[9].type = PLATFORM_STATIC; phases[1].platforms[9].x = 1250; phases[1].platforms[9].y = 650; phases[1].platforms[9].w = pw; phases[1].platforms[9].h = phh;
    phases[1].platforms[10].type = PLATFORM_STATIC; phases[1].platforms[10].x = 1180; phases[1].platforms[10].y = 650; phases[1].platforms[10].w = pw; phases[1].platforms[10].h = phh;
    phases[1].platforms[11].type = PLATFORM_STATIC; phases[1].platforms[11].x = 1320; phases[1].platforms[11].y = 650; phases[1].platforms[11].w = pw; phases[1].platforms[11].h = phh;
    phases[1].platforms[12].type = PLATFORM_STATIC; phases[1].platforms[12].x = 1390; phases[1].platforms[12].y = 650; phases[1].platforms[12].w = pw; phases[1].platforms[12].h = phh;
    phases[1].platforms[13].type = PLATFORM_STATIC; phases[1].platforms[13].x = 1460; phases[1].platforms[13].y = 650; phases[1].platforms[13].w = pw; phases[1].platforms[13].h = phh;
    phases[1].platforms[14].type = PLATFORM_STATIC; phases[1].platforms[14].x = 50; phases[1].platforms[14].y = 650; phases[1].platforms[14].w = pw; phases[1].platforms[14].h = phh;
    phases[1].spawn_x = phases[1].platforms[0].x + 20;
    phases[1].spawn_y = phases[1].platforms[0].y;
    phases[1].gravity = 1.6f; phases[1].jump_vel = 28.0f; phases[1].move_speed = 6.5f;
    phases[1].hole_x1 = 1e9f; phases[1].hole_x2 = -1e9f; phases[1].hole_y1 = 0; phases[1].hole_y2 = 0;

    // initialize prevY for phase2 platforms
    for (int i = 0; i < phases[1].platform_count; ++i) {
        phases[1].platforms[i].prevY = phases[1].platforms[i].y;
    }

    // initialize timers for fase 2
    phases[1].phase_time = 0.0;
    phases[1].phase_active = false;
    phases[1].phase_completed = false;

    int nlasers = 3;
    phases[1].laser_count = nlasers;
    phases[1].lasers = (Laser*)calloc(nlasers, sizeof(Laser));
    if (!phases[1].lasers) { destroy_phases(phases, 2); return NULL; }
    float left = phases[1].platforms[0].x + phases[1].platforms[0].w;
    float right = phases[1].platforms[1].x;
    if (left >= right) {
        left = phases[1].platforms[0].x + phases[1].platforms[0].w * 0.5f;
        right = phases[1].platforms[1].x + phases[1].platforms[1].w * 0.5f;
    }
    for (int i = 0; i < nlasers; ++i) {
        float t = (float)(i + 1) / (nlasers + 1);
        float cx = left + (right - left) * t;
        phases[1].lasers[i].x1 = cx;
        phases[1].lasers[i].y1 = phases[1].platforms[0].y - 300.0f;
        phases[1].lasers[i].x2 = cx;
        phases[1].lasers[i].y2 = phases[1].platforms[0].y + 20.0f;
        phases[1].lasers[i].active = false;
        phases[1].lasers[i].timer = 0.0f;
        phases[1].lasers[i].period = 2.39f + i * 0.2f;
        phases[1].lasers[i].active_frac = 0.45f;
    }

    // Inicializar animação do laser (fallback se não existir laser.png)
    if (!setup_laser_animation(a, &phases[1].laser_anim, 4, 1)) {
        fprintf(stderr, "Aviso: nao foi possivel inicializar animacao do laser\n");
    }


    // posicionadas para encostar exatamente nas pontas do sprite do laser ---
    if (phases[1].laser_count > 0) {
        int nlas = phases[1].laser_count;
        int add_per_laser = 4; // 2 abaixo + 2 acima invertidas
        int old_count = phases[1].platform_count;
        int new_count = old_count + nlas * add_per_laser;
        Platform* tmp = (Platform*)realloc(phases[1].platforms, sizeof(Platform) * new_count);
        if (!tmp) {
            fprintf(stderr, "WARN: realloc falhou ao adicionar plataformas visuais por laser\n");
        }
        else {
            phases[1].platforms = tmp;
            // inicializa os novos slots
            for (int i = old_count; i < new_count; ++i) {
                phases[1].platforms[i].type = PLATFORM_STATIC;
                phases[1].platforms[i].w = pw;    // força largura da plataforma larga
                phases[1].platforms[i].h = phh;   // força altura da plataforma larga
                phases[1].platforms[i].collidable = false; // visual only
                phases[1].platforms[i].inverted = false;
                phases[1].platforms[i].prevY = phases[1].platforms[i].y;
            }
            // gap horizontal entre as duas plataformas lado-a-lado
            float gap_between = 8.0f;
            for (int li = 0; li < nlas; ++li) {
                Laser* L = &phases[1].lasers[li];
                float cx = L->x1;
                float total_w = pw * 2.0f + gap_between;
                float left_x_start = cx - (total_w * 0.5f);
                int idx_base = old_count + li * add_per_laser;

                // plataforma esquerda abaixo: encostada na base do feixe
                phases[1].platforms[idx_base + 0].x = left_x_start;
                phases[1].platforms[idx_base + 0].y = L->y2; // encostada na ponta inferior do sprite
                phases[1].platforms[idx_base + 0].collidable = false;
                phases[1].platforms[idx_base + 0].inverted = false;

                // plataforma direita abaixo: encostada na base do feixe
                phases[1].platforms[idx_base + 1].x = left_x_start + pw + gap_between;
                phases[1].platforms[idx_base + 1].y = L->y2; // encostada na ponta inferior do sprite
                phases[1].platforms[idx_base + 1].collidable = false;
                phases[1].platforms[idx_base + 1].inverted = false;

                // plataforma esquerda acima (invertida): encostada no topo do feixe
                phases[1].platforms[idx_base + 2].x = left_x_start;
                phases[1].platforms[idx_base + 2].y = L->y1 - phh; // encostada na ponta superior do sprite
                phases[1].platforms[idx_base + 2].collidable = false;
                phases[1].platforms[idx_base + 2].inverted = true;

                // plataforma direita acima (invertida): encostada no topo do feixe
                phases[1].platforms[idx_base + 3].x = left_x_start + pw + gap_between;
                phases[1].platforms[idx_base + 3].y = L->y1 - phh; // encostada na ponta superior do sprite
                phases[1].platforms[idx_base + 3].collidable = false;
                phases[1].platforms[idx_base + 3].inverted = true;
            }
            phases[1].platform_count = new_count;
            fprintf(stderr, "DEBUG: Added %d visual large platforms (4 per laser) to phase2; new platform_count=%d\n", nlas * add_per_laser, phases[1].platform_count);
        }
    }

    *out_count = count;
    fprintf(stderr, "DEBUG: create_phases done: phases=%p count=%d phase1.platforms=%p phase2.platforms=%p\n", (void*)phases, *out_count, (void*)phases[0].platforms, (void*)phases[1].platforms);
    if (phases[1].laser_anim.frames) fprintf(stderr, "DEBUG: phase2 laser_anim frames=%d sprite=%d x %d\n", phases[1].laser_anim.total_frames, phases[1].laser_anim.sprite_w, phases[1].laser_anim.sprite_h);
    return phases;
}

static void destroy_phases(Phase* phases, int count) {
    if (!phases) return;
    for (int i = 0; i < count; ++i) {
        if (&phases[i] != NULL) {
            if (phases[i].platforms != NULL) {
                free(phases[i].platforms);
                phases[i].platforms = NULL;
            }
            if (phases[i].lasers != NULL) {
                free(phases[i].lasers);
                phases[i].lasers = NULL;
            }
            if (phases[i].laser_anim.frames != NULL) {
                for (int j = 0; j < phases[i].laser_anim.total_frames; ++j)
                    if (phases[i].laser_anim.frames[j]) al_destroy_bitmap(phases[i].laser_anim.frames[j]);
                free(phases[i].laser_anim.frames);
                phases[i].laser_anim.frames = NULL;
            }
        }
    }
    free(phases);
}

// Update (defensive)
static void update_game(Game* g, Player* p, const Anim* anim, const Assets* a, Phase* phases, int phase_count) {
    if (!g || !p || !anim || !a || !phases) return;
    if (g->state == STATE_MENU || g->state == STATE_FINAL) return;
    if (g->current_phase < 0 || g->current_phase >= phase_count) return;
    Phase* ph = &phases[g->current_phase];
    if (!ph) return;


    const float dt = 1.0f / 60.0f;


    if (g->total_running) {
        g->total_time += dt;
    }


    if (g->current_phase >= 0 && g->current_phase < phase_count) {
        Phase* curph = &phases[g->current_phase];
        if (curph->phase_active && !curph->phase_completed) {
            curph->phase_time += dt;
        }
    }

    p->gravity = ph->gravity;
    p->jump_vel = ph->jump_vel;
    p->move_speed = ph->move_speed;


    int row = row_for_state(p->state);
    int frame = p->frame;
    if (!anim->frames || anim->total_frames <= 0) frame = 0;
    if (frame >= anim->cols) frame = 0;
    int idx = row * anim->cols + frame;
    int fw = (anim->final_w) ? anim->final_w[idx] : 1;
    int fh = (anim->final_h) ? anim->final_h[idx] : 1;
    float scale = p->target_height_px / (float)fh;

    // ---- MOVIMENTAÇÃO DAS PLATAFORMAS (com prevY, dt e sem overshoot) ----
    if (ph->platforms) {
        for (int i = 0; i < ph->platform_count; ++i) {
            Platform* pl = &ph->platforms[i];
            // guarda posição anterior
            pl->prevY = pl->y;
            if (pl->type == PLATFORM_VERTICAL) {
                // pl->vy está em px/s; aplicar dt
                float newY = pl->y + pl->vy * dt;
                // se ultrapassou limites, refletir corretamente 
                if (newY < pl->minY) {
                    float excess = pl->minY - newY;
                    newY = pl->minY + excess;
                    pl->vy = -pl->vy;
                }
                else if (newY > pl->maxY) {
                    float excess = newY - pl->maxY;
                    newY = pl->maxY - excess;
                    pl->vy = -pl->vy;
                }
                pl->y = newY;
            }
        }
    }

    int idx2 = row * anim->cols + p->frame;
    int fw2 = (anim->final_w) ? anim->final_w[idx2] : 1;
    int fh2 = (anim->final_h) ? anim->final_h[idx2] : 1;
    float scale2 = p->target_height_px / (float)fh2;
    int draw_w = (int)(fw2 * scale2);
    float half_draw_w_draw = draw_w * 0.5f;

    bool moving = false;
    if (p->key_right && !p->key_left) { p->posX += p->move_speed; moving = true; }
    else if (p->key_left && !p->key_right) { p->posX -= p->move_speed; moving = true; }

    if (p->posX - half_draw_w_draw < 0) p->posX = half_draw_w_draw;
    if (p->posX + half_draw_w_draw > SCREEN_W) p->posX = SCREEN_W - half_draw_w_draw;

    float player_center_x = p->posX;
    float player_center_y = p->feetY - (fh * scale * 0.5f);
    bool in_hole = (player_center_x >= ph->hole_x1 && player_center_x <= ph->hole_x2 &&
        player_center_y >= ph->hole_y1 && player_center_y <= ph->hole_y2);

    if (in_hole && moving) {
        if (p->key_right && !p->key_left) p->posX -= p->move_speed;
        else if (p->key_left && !p->key_right) p->posX += p->move_speed;
        if (p->posX - half_draw_w_draw < 0) p->posX = half_draw_w_draw;
        if (p->posX + half_draw_w_draw > SCREEN_W) p->posX = SCREEN_W - half_draw_w_draw;
    }

    float effective_gravity = p->gravity;
    if (in_hole) effective_gravity *= ph->hole_gravity_factor;

    if (!in_hole) {
        p->velY += effective_gravity;
        if (p->velY > max_fall) p->velY = max_fall;
    }
    else {
        p->velY -= effective_gravity;
        if (p->velY < -max_fall) p->velY = -max_fall;
    }

    if (p->want_jump) {
        if (!in_hole) p->velY = -p->jump_vel;
        else p->velY = p->jump_vel;
        p->want_jump = false; p->jumping = true;
    }

    float prev_feetY = p->feetY;
    p->feetY += p->velY;

    float draw_x = p->posX - half_draw_w_draw;
    float hit_left = draw_x + (p->hitbox_offset_left * scale);
    float hit_right = draw_x + draw_w - (p->hitbox_offset_right * scale);
    float hit_top = p->feetY - (fh * scale);
    float hit_bottom = hit_top + (fh * scale);

    bool on_platform = false;
    float platform_dy_for_player = 0.0f; // deslocamento que deve ser aplicado ao jogador se estiver sobre plataforma
    const float EPS = 1.5f;



    if (ph->platforms) {
        for (int i = 0; i < ph->platform_count; ++i) {
            Platform* pl = &ph->platforms[i];
            if (!pl->collidable) continue;
            float pl_left = pl->x, pl_right = pl->x + pl->w, pl_top = pl->y, pl_bottom = pl->y + pl->h;
            bool overlap_h = (hit_right >= pl_left + EPS && hit_left <= pl_right - EPS);

            if (!in_hole) {
                if (p->velY >= -0.1f && overlap_h && prev_feetY <= pl_top + EPS && p->feetY >= pl_top - EPS) {
                    // acertou o topo da plataforma
                    p->feetY = pl_top;
                    p->velY = 0;
                    p->jumping = false;
                    on_platform = true;
                    // calcular deslocamento real da plataforma neste frame para mover o jogador junto
                    platform_dy_for_player = pl->y - pl->prevY;
                    break; // já encontramos a plataforma em que está apoiado
                }
                else {
                    // se a plataforma se moveu para cima e invadiu o jogador (pl_top < prev_feetY e agora intersecta), corrija
                    if (overlap_h && prev_feetY <= pl_bottom + EPS && p->feetY >= pl_top - EPS && p->feetY <= pl_bottom + EPS) {
                        p->feetY = pl_top;
                        p->velY = 0;
                        p->jumping = false;
                        on_platform = true;
                        platform_dy_for_player = pl->y - pl->prevY;
                        break;
                    }
                }
            }
            else {
                // caso invertido: detectar colisão com a face inferior da plataforma
                float head_prev = prev_feetY - (fh * scale);
                float head_now = p->feetY - (fh * scale);
                if (p->velY <= 0 && overlap_h && head_prev >= pl_bottom - EPS && head_now <= pl_bottom + EPS) {
                    p->feetY = pl_bottom + (fh * scale);
                    p->velY = 0;
                    p->jumping = false;
                    on_platform = true;
                    platform_dy_for_player = pl->y - pl->prevY;
                    break;
                }
            }
        }
    }

    // se o jogador está sobre uma plataforma móvel
    if (on_platform && fabsf(platform_dy_for_player) > 0.0001f) {
        p->feetY += platform_dy_for_player;
    }

 
    if (g->current_phase == 1 && a && a->tesla) {
        // advance timer (60 FPS expectation)
        tesla_timer += dt;
        float cycle = fmodf(tesla_timer, TESLA_ATTACK_PERIOD);
        tesla_active = (cycle < (TESLA_ATTACK_PERIOD * TESLA_ATTACK_ACTIVE_FRAC));

        float tesla_area_x = TESLA_POS_X - (TESLA_DEBUG_W * 0.5f);
        float tesla_area_y = TESLA_POS_Y - TESLA_DEBUG_H;

        // advance tesla animation frames only when active
        if (tesla_active && tesla_anim.frames && tesla_anim.total_frames > 0) {
            tesla_anim.frame_timer++;
            if (tesla_anim.frame_timer >= tesla_anim.frame_delay) {
                tesla_anim.frame_timer = 0;
                tesla_anim.frame = (tesla_anim.frame + 1) % tesla_anim.total_frames;
            }
        }
        else {
            tesla_anim.frame_timer = 0;
        }

        if (tesla_active) {
            bool overlap_x = !(hit_right < tesla_area_x || hit_left >(tesla_area_x + TESLA_DEBUG_W));
            bool overlap_y = !(hit_bottom < tesla_area_y || hit_top >(tesla_area_y + TESLA_DEBUG_H));
            if (overlap_x && overlap_y) {
                p->vida = 3;
                g->state = STATE_MENU;
                p->key_left = false;
                p->key_right = false;
                p->want_jump = false;
                p->jumping = false;
                p->velY = 0;
                p->frame = p->frame_timer = 0;
                p->state = p->prev_state = PERSON_IDLE;

                // limpa eventos que possam ter ficado “presos”
                al_flush_event_queue(g->queue);
                return; // sai do update para não continuar lógica da fase no frame

            }
        }
    }

    if (p->feetY > p->chao_y) {
        // o jogador caiu no chão — perde uma vida
        p->vida--;
        if (p->vida <= 0) {
            // sem vidas -> fim de jogo
            p->vida = 3;
            g->total_running = false;
            g->state = STATE_MENU;
            p->key_left = false;
            p->key_right = false;
            p->want_jump = false;
            p->jumping = false;
            p->velY = 0;
            p->frame = p->frame_timer = 0;
            p->state = p->prev_state = PERSON_IDLE;

            // limpa eventos que possam ter ficado “presos”
            al_flush_event_queue(g->queue);
            return; // sai do update para não continuar lógica da fase no frame
        }
        else {
            // respawn 
            p->posX = ph->spawn_x;
            p->feetY = ph->spawn_y;
            p->velY = 0;
            p->jumping = false;
            p->want_jump = false;
        }
    }
    if (p->feetY < 0) {
        p->vida--;
        if (p->vida <= 0) {
            // sem vidas -> fim de jogo
			p->vida = 3;
            g->total_running = false;
            g->state = STATE_MENU;
            p->key_left = false;
            p->key_right = false;
            p->want_jump = false;
            p->jumping = false;
            p->velY = 0;
            p->frame = p->frame_timer = 0;
            p->state = p->prev_state = PERSON_IDLE;

            // limpa eventos que possam ter ficado “presos”
            al_flush_event_queue(g->queue);
            return; // sai do update para não continuar lógica da fase no frame
        }
        else {
            // respawn 
            p->posX = ph->spawn_x;
            p->feetY = ph->spawn_y;
            p->velY = 0;
            p->jumping = false;
            p->want_jump = false;
        }

    }
    if (!on_platform) p->state = PERSON_JUMP;
    else if (moving) p->state = PERSON_RUN;
    else p->state = PERSON_IDLE;

    int delay_idle = 14, delay_run = 7, delay_jump = 7;
    int current_delay = (p->state == PERSON_RUN) ? delay_run : (p->state == PERSON_JUMP) ? delay_jump : delay_idle;
    if (p->state != p->prev_state) { p->frame_timer = 0; p->frame = 0; }
    p->prev_state = p->state;
    p->frame_timer++;
    if (p->frame_timer >= current_delay) { p->frame_timer = 0; p->frame++; if (p->frame >= 7) p->frame = 0; }

    // lasers update
    if (ph->laser_count > 0) {
        if (!ph->lasers) {
            fprintf(stderr, "WARN: ph->lasers is NULL but laser_count=%d; disabling lasers for this phase\n", ph->laser_count);
            ph->laser_count = 0;
        }
        else {
            if (ph->laser_anim.total_frames > 0 && ph->laser_anim.frames) {
                ph->laser_anim.frame_timer++;
                if (ph->laser_anim.frame_timer >= ph->laser_anim.frame_delay) {
                    ph->laser_anim.frame_timer = 0;
                    ph->laser_anim.frame = (ph->laser_anim.frame + 1) % ph->laser_anim.total_frames;
                }
            }
            for (int li = 0; li < ph->laser_count; ++li) {
                Laser* L = &ph->lasers[li];
                L->timer += dt;
                if (L->period <= 0.0f) L->period = 1.0f;
                float cycle = fmodf(L->timer, L->period);
                L->active = (cycle < L->period * L->active_frac);
                if (L->active) {
                    float beam_x_left = L->x1 - 12.0f;
                    float beam_x_right = L->x1 + 12.0f;
                    bool overlap_x = (hit_right >= beam_x_left && hit_left <= beam_x_right);
                    bool overlap_y = (hit_bottom >= fminf(L->y1, L->y2) && hit_top <= fmaxf(L->y1, L->y2));
                    if (overlap_x && overlap_y) {
                        p->vida--;
                        if (p->vida <= 0) {
                            p->vida = 3;
                            g->total_running = false;
                            g->state = STATE_MENU;
                            p->key_left = false;
                            p->key_right = false;
                            p->want_jump = false;
                            p->jumping = false;
                            p->velY = 0;
                            p->frame = p->frame_timer = 0;
                            p->state = p->prev_state = PERSON_IDLE;

                            // limpa eventos que possam ter ficado “presos”
                            al_flush_event_queue(g->queue);
                            return; // sai do update para não continuar lógica da fase no frame
                        }
                        else {
                            // respawn 
                            p->posX = ph->spawn_x;
                            p->feetY = ph->spawn_y;
                            p->velY = 0;
                            p->jumping = false;
                            p->want_jump = false;
                        }
                    }
                }
            }
        }
    }

    // phase transition
    if (p->posX + half_draw_w_draw >= SCREEN_W) {
        if (g->state == STATE_FASE1) {
            // finish current phase
            if (phases && g->current_phase >= 0 && g->current_phase < phase_count) {
                phases[g->current_phase].phase_active = false;
                phases[g->current_phase].phase_completed = true;
            }

            g->state = STATE_FASE2;
            int prev_phase = g->current_phase;
            g->current_phase = (g->current_phase + 1) % phase_count;

            // start next phase
            if (phases && g->current_phase >= 0 && g->current_phase < phase_count) {
                show_loading_bitmap(a->eletriloading, g->queue);
                phases[g->current_phase].phase_active = true;
                p->vida = 3;
                p->key_left = false;
                p->key_right = false;
                p->want_jump = false;
                p->jumping = false;
                p->velY = 0;
                p->state = PERSON_IDLE;
                p->prev_state = PERSON_IDLE;
            }

            if (phases) {
                p->posX = phases[g->current_phase].spawn_x; p->feetY = phases[g->current_phase].spawn_y; p->velY = 0; p->jumping = false; p->want_jump = false;
                p->gravity = phases[g->current_phase].gravity; p->jump_vel = phases[g->current_phase].jump_vel; p->move_speed = phases[g->current_phase].move_speed;
            }
        }
        else if (g->state == STATE_FASE2) {
            // finish current phase
            if (phases && g->current_phase >= 0 && g->current_phase < phase_count) {
                phases[g->current_phase].phase_active = false;
                phases[g->current_phase].phase_completed = true;
            }
            // stop total timer
            g->total_running = false;
            g->state = STATE_FINAL;
        }
    }
}

// Render (with platform visual substitution and inverted top platforms)
static void render_game(const Game* g, const Assets* a, const Anim* anim, const Player* p, Phase* phases) {
    if (!g || !a || !anim || !p || !phases) return;
    if (g->state == STATE_MENU) {
        if (a->tela_incial) {
            al_draw_scaled_bitmap(
                a->tela_incial,
                0, 0,
                al_get_bitmap_width(a->tela_incial),
                al_get_bitmap_height(a->tela_incial),
                0, 0,
                SCREEN_W, SCREEN_H,
                0
            );

        }

        if (g->show_help) {
            int panel_w = 600, panel_h = 300;
            int panel_x = (SCREEN_W - panel_w) / 2;
            int panel_y = SCREEN_H - panel_h - 45;

            al_draw_filled_rectangle(panel_x, panel_y, panel_x + panel_w, panel_y + panel_h,
                al_map_rgba(0, 0, 0, 180));
            al_draw_rectangle(panel_x, panel_y, panel_x + panel_w, panel_y + panel_h,
                al_map_rgb(255, 255, 255), 2);

            ALLEGRO_FONT* f = g->font ? g->font : al_create_builtin_font();
            float ty = panel_y + 20;
            al_draw_text(f, al_map_rgb(255, 255, 255), panel_x + 20, ty, 0, "Comandos:");
            ty += 40;
            al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "A - Andar para esquerda");
            ty += 30;
            al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "D - Andar para direita");
            ty += 30;
            al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "SPACE - Pular");
            ty += 30;
			al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "enter - passar para proxima fase ");
            ty += 40;
			al_draw_text(f, al_map_rgb(255, 255, 255), panel_x + 20, ty, 0, "Modificador de Gravidade (apenas na fase 1):");
            ty += 30;
			al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "up - aumentar gravidade");
            ty += 30;
			al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "down - diminuir gravidade");
			ty += 40;
            al_draw_text(f, al_map_rgb(220, 220, 220), panel_x + 20, ty, 0, "ESC - Sair");
        }
        al_flip_display();
        return;
    }

    if (g->state == STATE_FASE1 || g->state == STATE_FASE2) {
        if (g->current_phase < 0) return;
        Phase* ph = &phases[g->current_phase];
        if (!ph) return;

        ALLEGRO_BITMAP* fundo = ph->background ? ph->background : a->fundo1;
        al_clear_to_color(al_map_rgb(0, 0, 0));
        if (fundo) al_draw_scaled_bitmap(fundo, 0, 0, al_get_bitmap_width(fundo), al_get_bitmap_height(fundo), 0, 0, SCREEN_W, SCREEN_H, 0);

        int dest_chao_h = p->chao_h + p->chao_extra_pixels;
        int dest_chao_y = SCREEN_H - dest_chao_h;
        if (a->chao) {
            for (int x = 0; x <= SCREEN_W; x += p->chao_w) {
                al_draw_scaled_bitmap(a->chao, 0, 0, p->chao_w, p->chao_h, x, dest_chao_y, p->chao_w, dest_chao_h, 0);
            }
        }

        // draw platforms: use a->plat sprite for non-collidable visuals and respect inverted flag
        if (ph->platforms) {
            for (int i = 0; i < ph->platform_count; ++i) {
                Platform* pl = &ph->platforms[i];

                ALLEGRO_BITMAP* bmp = NULL;
                // When platform is visual-only (no collision), draw using a->plat (the sprite you provided)
                if (!pl->collidable) {
                    bmp = a->plat ? a->plat : a->plat_small;
                }
                else {
                    // collidable platforms: use large or small as before
                    bmp = (pl->w > 120) ? a->plat : a->plat_small;
                }
                if (!bmp) continue;

                int bmp_w = al_get_bitmap_width(bmp);
                int bmp_h = al_get_bitmap_height(bmp);

                // If non-collidable, use a tint to differentiate (slightly transparent / brighter)
                if (!pl->inverted) {
                    if (!pl->collidable) {
                        ALLEGRO_COLOR tint = al_map_rgba_f(1.0f, 1.0f, 1.0f, 0.95f);
                        al_draw_tinted_scaled_bitmap(bmp, tint, 0, 0, bmp_w, bmp_h, pl->x, pl->y, (int)pl->w, (int)pl->h, 0);
                    }
                    else {
                        al_draw_scaled_bitmap(bmp, 0, 0, bmp_w, bmp_h, pl->x, pl->y, (int)pl->w, (int)pl->h, 0);
                    }
                }
                else {
                    // inverted drawing (flip vertical)
                    if (!pl->collidable) {
                        ALLEGRO_COLOR tint = al_map_rgba_f(1.0f, 1.0f, 1.0f, 0.95f);
                        al_draw_tinted_scaled_bitmap(bmp, tint, 0, bmp_h - 1, bmp_w, -bmp_h, pl->x, pl->y, (int)pl->w, (int)pl->h, 0);
                    }
                    else {
                        al_draw_scaled_bitmap(bmp, 0, bmp_h - 1, bmp_w, -bmp_h, pl->x, pl->y, (int)pl->w, (int)pl->h, 0);
                    }
                }
            }
        }

        // Draw lasers: tiles vertically only when active; fallback to line if frames missing
        if (ph->laser_count > 0 && ph->lasers) {
            LaserAnim* la = &ph->laser_anim;
            for (int li = 0; li < ph->laser_count; ++li) {
                Laser* L = &ph->lasers[li];
                float topY = fminf(L->y1, L->y2);
                float bottomY = fmaxf(L->y1, L->y2);
                if (la->frames && la->total_frames > 0) {
                    if (!L->active) continue; // draw only when active
                    int frame_index = la->frame % la->total_frames;
                    ALLEGRO_BITMAP* tile = la->frames[frame_index];
                    if (!tile) continue;
                    int tile_w = la->sprite_w, tile_h = la->sprite_h;
                    float width_scale = 1.6f;
                    float desired_tile_w = tile_w * width_scale;
                    float desired_tile_h = tile_h;
                    float span = bottomY - topY;
                    if (span <= 0) continue;
                    int ntiles = (int)ceilf(span / desired_tile_h);
                    if (ntiles <= 0) ntiles = 1;
                    float step = span / (float)ntiles;
                    float x_center = L->x1;
                    ALLEGRO_COLOR tint = al_map_rgba_f(1, 1, 1, 1);
                    for (int t = 0; t < ntiles; ++t) {
                        float ty_top = topY + t * step;
                        float ty_bottom = fminf(ty_top + desired_tile_h, bottomY);
                        float ty_h = ty_bottom - ty_top;
                        if (ty_h <= 0) continue;
                        al_draw_tinted_scaled_bitmap(tile, tint, 0, 0, tile_w, tile_h, x_center - desired_tile_w * 0.5f, ty_top, desired_tile_w, ty_h, 0);
                    }
                    al_draw_line(L->x1, topY, L->x1, bottomY, al_map_rgba(255, 120, 120, 80), desired_tile_w * 0.12f);
                }
                else {
                    if (L->active) al_draw_line(L->x1, L->y1, L->x2, L->y2, al_map_rgba(255, 0, 0, 200), 6.0f);
                    else al_draw_line(L->x1, L->y1, L->x2, L->y2, al_map_rgba(255, 0, 0, 60), 3.0f);
                }
            }
        }

        // Desenha torre Tesla 
        if (g->current_phase == 1 && a->tesla) {
            // dimensões da torre
            int tw = al_get_bitmap_width(a->tesla);
            int th = al_get_bitmap_height(a->tesla);

            // Determine a plataforma sob TESLA_POS_X 
            float platform_top_y = TESLA_POS_Y; // fallback
            bool found_platform = false;
            Phase* ph2 = &phases[g->current_phase];
            if (ph2 && ph2->platforms) {
                float tx = TESLA_POS_X;
                for (int pi = 0; pi < ph2->platform_count; ++pi) {
                    Platform* pl = &ph2->platforms[pi];
                    if (!pl->collidable) continue;
                    float pl_left = pl->x;
                    float pl_right = pl->x + pl->w;
                    if (tx >= pl_left && tx <= pl_right) {
                        if (!found_platform || pl->y < platform_top_y) {
                            platform_top_y = pl->y;
                            found_platform = true;
                        }
                    }
                }
            }

            float tesla_center_x = TESLA_POS_X;
            float tesla_base_y = 710; // usar topo encontrado para alinhar

            float tesla_draw_x = tesla_center_x - tw * 0.5f;
            float tesla_draw_y = tesla_base_y - th;
            al_draw_bitmap(a->tesla, tesla_draw_x, tesla_draw_y, 0);

            if (tesla_active && tesla_anim.frames && tesla_anim.total_frames > 0) {
                ALLEGRO_BITMAP* f = tesla_anim.frames[tesla_anim.frame];
                if (f) {
                    int aw = tesla_anim.sprite_w;
                    int ah = tesla_anim.sprite_h;
                    float scaleA = tesla_anim.scale;
                    int draw_aw = (int)(aw * scaleA);
                    int draw_ah = (int)(ah * scaleA);

                    float anim_draw_y = tesla_base_y - draw_ah;
                    float anim_draw_x = tesla_center_x - draw_aw * 0.5f;

                    float tesla_anim_offset_x = 0.0f;
                    float tesla_anim_offset_y = 0.0f;

                    anim_draw_x += tesla_anim_offset_x;
                    anim_draw_y += tesla_anim_offset_y;

                    al_draw_tinted_scaled_bitmap(f, al_map_rgba_f(1, 1, 1, 1.0f), 0, 0, aw, ah, anim_draw_x, anim_draw_y, draw_aw, draw_ah, 0);
                }
            }
            else if (tesla_active) {
                float cx = TESLA_POS_X;
                float cy = tesla_base_y - 20;
                float r = 48.0f;
                al_draw_filled_circle(cx, cy, r, al_map_rgba(180, 120, 255, 80));
                al_draw_circle(cx, cy, r, al_map_rgba(220, 180, 255, 160), 3.0f);
            }
        }

        // draw player sprite safely
        int rowp = row_for_state(p->state);
        int framep = p->frame;
        if (!anim->frames || anim->total_frames <= 0) framep = 0;
        if (framep < 0) framep = 0; if (framep >= anim->cols) framep = 0;
        int idxp = rowp * anim->cols + framep;
        if (anim->frames && anim->frames[idxp]) {
            int fw = anim->final_w[idxp], fh = anim->final_h[idxp];
            float scaleP = p->target_height_px / (float)fh;
            int draw_w_p = (int)(fw * scaleP), draw_h_p = (int)(fh * scaleP);
            int flags = (p->key_left && !p->key_right) ? ALLEGRO_FLIP_HORIZONTAL : 0;
            float draw_y = p->feetY - draw_h_p + 145;
            float draw_x = p->posX - (draw_w_p / 2.0f);
            al_draw_scaled_bitmap(anim->frames[idxp], 0, 0, fw, fh, draw_x, draw_y, draw_w_p, draw_h_p, flags);
        }

        al_flip_display();
        return;
    }

    if (g->state == STATE_FINAL) {
        al_clear_to_color(al_map_rgb(20, 20, 20));
        const int base_y = SCREEN_H / 2 - 90;
        char buf[256];

        if (g->font) al_draw_text(g->font, al_map_rgb(255, 0, 0), SCREEN_W / 2, base_y, ALLEGRO_ALIGN_CENTER, "Fim de jogo");

        double t1 = 0.0, t2 = 0.0, total = g->total_time;
        if (phases) {
            if (1 <= 1) t1 = phases[0].phase_time;
            if (1 < 2) t2 = phases[1].phase_time;
        }

        snprintf(buf, sizeof(buf), "Tempo Fase 1: %.2f s", t1);
        if (g->font) al_draw_text(g->font, al_map_rgb(200, 200, 255), SCREEN_W / 2, base_y + 40, ALLEGRO_ALIGN_CENTER, buf);

        snprintf(buf, sizeof(buf), "Tempo Fase 2: %.2f s", t2);
        if (g->font) al_draw_text(g->font, al_map_rgb(200, 200, 255), SCREEN_W / 2, base_y + 80, ALLEGRO_ALIGN_CENTER, buf);

        snprintf(buf, sizeof(buf), "Tempo Total: %.2f s", total);
        if (g->font) al_draw_text(g->font, al_map_rgb(255, 255, 180), SCREEN_W / 2, base_y + 120, ALLEGRO_ALIGN_CENTER, buf);

        if (g->font) al_draw_text(g->font, al_map_rgb(255, 255, 255), SCREEN_W / 2, base_y + 170, ALLEGRO_ALIGN_CENTER, "Pressione ENTER para sair");

        al_flip_display();
        return;
    }
}

// Cleanup
static void cleanup_all(Game* g, Assets* a, Anim* anim, Phase* phases, int phase_count) {
    if (anim) free_animation(anim);
    // destroy tesla anim
    tesla_anim_destroy(&tesla_anim);
    if (a) {
        if (a->spritesheet) al_destroy_bitmap(a->spritesheet);
        if (a->fundo1) al_destroy_bitmap(a->fundo1);
        if (a->fundo2) al_destroy_bitmap(a->fundo2);
        if (a->chao) al_destroy_bitmap(a->chao);
        if (a->plat) al_destroy_bitmap(a->plat);
        if (a->plat_small) al_destroy_bitmap(a->plat_small);
        if (a->laser) al_destroy_bitmap(a->laser);
        if (a->tesla) al_destroy_bitmap(a->tesla);
        if (a->raio) al_destroy_bitmap(a->raio);
        if (a->esplosao) al_destroy_bitmap(a->esplosao);
        if (a->gravloading) al_destroy_bitmap(a->gravloading);
        if (a->eletriloading) al_destroy_bitmap(a->eletriloading);
    }
    if (g) {
        if (g->timer) al_destroy_timer(g->timer);
        if (g->queue) al_destroy_event_queue(g->queue);
        if (g->font) al_destroy_font(g->font);
        if (g->display) al_destroy_display(g->display);
    }
    destroy_phases(phases, phase_count);
}

static void free_animation(Anim* anim) {
    if (!anim) return;
    if (anim->frames) {
        for (int i = 0; i < anim->total_frames; ++i) if (anim->frames[i]) al_destroy_bitmap(anim->frames[i]);
        free(anim->frames);
        anim->frames = NULL;
    }
    if (anim->final_w) free(anim->final_w);
    if (anim->final_h) free(anim->final_h);
    if (anim->row_w) free(anim->row_w);
    if (anim->row_h) free(anim->row_h);
}

static int row_for_state(PersonState s) {
    if (s == PERSON_RUN) return 0;
    if (s == PERSON_JUMP) return 4;
    if (s == PERSON_DEAD) return 2;
    return 3;
}
