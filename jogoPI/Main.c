#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_primitives.h>
#define SCREEN_W 1500
#define SCREEN_H 950
#define STATE_MENU 0
#define STATE_FASE1 1
#define STATE_FASE2 2
#define STATE_FINAL 3
#define PERSON_IDLE 0
#define PERSON_RUN 1
#define PERSON_JUMP 2
#define PERSON_DEAD 3
int main(void) {
    if (!al_init()) return -1;
    if (!al_init_image_addon()) return -1;
    al_install_keyboard();
    al_init_font_addon();
    if (!al_init_ttf_addon()) return -1;
    al_init_primitives_addon();
    ALLEGRO_DISPLAY* display = al_create_display(SCREEN_W, SCREEN_H);
    if (!display) return -1;
    al_set_window_title(display, "Jogo - Delays por Estado");
    ALLEGRO_FONT* font = al_load_ttf_font("arial.ttf", 24, 0);
    if (!font) font = al_create_builtin_font();
    if (!font) { al_destroy_display(display); return -1; }
    ALLEGRO_TIMER* timer = al_create_timer(1.0 / 60.0);
    if (!timer) { al_destroy_font(font); al_destroy_display(display); return -1; }
    ALLEGRO_EVENT_QUEUE* queue = al_create_event_queue();
    if (!queue) { al_destroy_timer(timer); al_destroy_font(font); al_destroy_display(display); return -1; }
    al_register_event_source(queue, al_get_display_event_source(display));
    al_register_event_source(queue, al_get_keyboard_event_source());
    al_register_event_source(queue, al_get_timer_event_source(timer));
    // Imagens
    ALLEGRO_BITMAP* spritesheet = al_load_bitmap("personagem.png");
    ALLEGRO_BITMAP* fundo1 = al_load_bitmap("cenario1.png");
    ALLEGRO_BITMAP* fundo2 = al_load_bitmap("cenario2.png");
    ALLEGRO_BITMAP* chao = al_load_bitmap("chao.png");
    ALLEGRO_BITMAP* plat = al_load_bitmap("plataforma.png");
    if (!spritesheet || !fundo1 || !fundo2 || !chao || !plat) {
        if (!spritesheet) fprintf(stderr, "Erro: personagem.png nao carregado\n");
        if (!fundo1) fprintf(stderr, "Erro: cenario.png (fundo1) nao carregado\n");
        if (!fundo2) fprintf(stderr, "Erro: cenario.png (fundo2) nao carregado\n");
        if (!chao) fprintf(stderr, "Erro: chao.png nao carregado\n");
        if (!plat) fprintf(stderr, "ERRO: Plataforma não carregado\n");
        if (spritesheet) al_destroy_bitmap(spritesheet);
        if (fundo1) al_destroy_bitmap(fundo1);
        if (fundo2) al_destroy_bitmap(fundo2);
        if (chao) al_destroy_bitmap(chao);
        if (plat) al_destroy_bitmap(plat);
        al_destroy_event_queue(queue); al_destroy_timer(timer); al_destroy_font(font); al_destroy_display(display);
        return -1;
    }
    // Config spritesheet
    const int sprite_cols = 10;
    const int sprite_rows = 5;
    int sheet_w = al_get_bitmap_width(spritesheet);
    int sheet_h = al_get_bitmap_height(spritesheet);
    if (sheet_w % sprite_cols != 0 || sheet_h % sprite_rows != 0)
        fprintf(stderr, "Aviso: dimensoes do spritesheet talvez nao divisiveis por cols/rows\n");
    int sprite_w = sheet_w / sprite_cols;
    int sprite_h = sheet_h / sprite_rows;
    int total_frames = sprite_cols * sprite_rows;
    // cria sub-bitmaps
    ALLEGRO_BITMAP** sub = malloc(sizeof(ALLEGRO_BITMAP*) * total_frames);
    if (!sub) { fprintf(stderr, "Mem insuficiente\n"); goto cleanup_images; }
    for (int r = 0; r < sprite_rows; r++) {
        for (int c = 0; c < sprite_cols; c++) {
            int idx = r * sprite_cols + c;
            sub[idx] = al_create_sub_bitmap(spritesheet, c * sprite_w, r * sprite_h, sprite_w, sprite_h);
            if (!sub[idx]) {
                fprintf(stderr, "Erro criar sub-bitmap r=%d c=%d\n", r, c);
                for (int i = 0; i < idx; i++) if (sub[i]) al_destroy_bitmap(sub[i]);
                free(sub);
                goto cleanup_images;
            }
        }
    }
    // final_w_per_row / final_h_per_row (padroniza por linha)
    int* final_w_per_row = malloc(sizeof(int) * sprite_rows);
    int* final_h_per_row = malloc(sizeof(int) * sprite_rows);
    if (!final_w_per_row || !final_h_per_row) { fprintf(stderr, "Mem insuficiente\n"); goto cleanup_subs; }
    for (int r = 0; r < sprite_rows; r++) {
        int maxw = 0, maxh = 0;
        for (int c = 0; c < sprite_cols; c++) {
            int bw = sprite_w;
            int bh = sprite_h;
            if (bw > maxw) maxw = bw;
            if (bh > maxh) maxh = bh;
        }
        final_w_per_row[r] = maxw + 4;
        final_h_per_row[r] = maxh + 4;
    }
    // cria frames_final por frame usando dimensao por row
    ALLEGRO_BITMAP** frames_final = malloc(sizeof(ALLEGRO_BITMAP*) * total_frames);
    int* final_w_arr = malloc(sizeof(int) * total_frames);
    int* final_h_arr = malloc(sizeof(int) * total_frames);
    if (!frames_final || !final_w_arr || !final_h_arr) { fprintf(stderr, "Mem insuficiente\n"); goto cleanup_per_row; }
    for (int r = 0; r < sprite_rows; r++) {
        for (int c = 0; c < sprite_cols; c++) {
            int idx = r * sprite_cols + c;
            int fw = final_w_per_row[r];
            int fh = final_h_per_row[r];
            final_w_arr[idx] = fw;
            final_h_arr[idx] = fh;
            frames_final[idx] = al_create_bitmap(fw, fh);
            if (!frames_final[idx]) {
                fprintf(stderr, "Erro criar frame_final idx=%d\n", idx);
                for (int j = 0; j < idx; j++) if (frames_final[j]) al_destroy_bitmap(frames_final[j]);
                goto cleanup_frames;
            }
            ALLEGRO_BITMAP* prev = al_get_target_bitmap();
            al_set_target_bitmap(frames_final[idx]);
            al_clear_to_color(al_map_rgba(0, 0, 0, 0));
            int dest_x = (fw - sprite_w) / 2;
            int dest_y = fh - sprite_h;
            al_draw_bitmap(sub[idx], dest_x, dest_y, 0);
            al_set_target_bitmap(prev);
            al_destroy_bitmap(sub[idx]);
            sub[idx] = NULL;
        }
    }
    free(sub);
    sub = NULL;
    // parâmetros de animação (delays por estado)
    int frame_delay_run = 7;
    int frame_delay_idle = 14;   // idle mais lento
    int frame_delay_jump = 7;
    int frame_delay_dead = 12;
    // escala global (altera conforme quiser)
    float target_height_px = 300.0f;
    // Chão e personagem
    int chao_w = al_get_bitmap_width(chao);
    int chao_h = al_get_bitmap_height(chao);
    int CHAO_EXTRA_PIXELS = 200;
    int ground_top = SCREEN_H - (chao_h + CHAO_EXTRA_PIXELS);
    int chao_y = ground_top;
    float posX = 150.0f;
    float feetY = chao_y;
    bool jumping = false;
    float velY = 0;
    const float gravity = 1.5f;
    const float jump_vel = 26.0f;
    const float move_speed = 6.0f;
    bool key_left = false, key_right = false;
    int game_state = STATE_MENU;
    int person_state = PERSON_IDLE;
    int prev_person_state = PERSON_IDLE;
    int frame = 0, frame_timer = 0;
    bool redraw = true, running = true;
    al_start_timer(timer);
    while (running) {
        ALLEGRO_EVENT ev;
        al_wait_for_event(queue, &ev);
        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) running = false;
        if (ev.type == ALLEGRO_EVENT_KEY_DOWN) {
            switch (ev.keyboard.keycode) {
            case ALLEGRO_KEY_ESCAPE: running = false; break;
            case ALLEGRO_KEY_ENTER:
                if (game_state == STATE_MENU) { game_state = STATE_FASE1; posX = 35; feetY = chao_y; }
                else if (game_state == STATE_FINAL) running = false;
                break;
            case ALLEGRO_KEY_RIGHT: key_right = true; break;
            case ALLEGRO_KEY_LEFT: key_left = true; break;
            case ALLEGRO_KEY_UP:
                if (!jumping && (game_state == STATE_FASE1 || game_state == STATE_FASE2)) {
                    jumping = true; velY = jump_vel; person_state = PERSON_JUMP;
                }
                break;
            }
        }
        else if (ev.type == ALLEGRO_EVENT_KEY_UP) {
            if (ev.keyboard.keycode == ALLEGRO_KEY_RIGHT) key_right = false;
            if (ev.keyboard.keycode == ALLEGRO_KEY_LEFT) key_left = false;
        }
        if (ev.type == ALLEGRO_EVENT_TIMER) {
            redraw = true;
            if (game_state == STATE_FASE1 || game_state == STATE_FASE2) {
                bool moving = false;
                if (key_right && !key_left) { posX += move_speed; moving = true; }
                else if (key_left && !key_right) { posX -= move_speed; moving = true; }
                if (jumping) {
                    feetY -= velY;
                    velY -= gravity;
                    if (feetY >= chao_y) { feetY = chao_y; jumping = false; velY = 0; }
                }
                else feetY = chao_y;
                if (posX < 0) posX = 0;
                if (posX > SCREEN_W) posX = SCREEN_W;
                if (jumping) person_state = PERSON_JUMP;
                else if (moving) person_state = PERSON_RUN;
                else person_state = PERSON_IDLE;
                // escolhe delay de acordo com estado
                int current_frame_delay;
                if (person_state == PERSON_RUN) current_frame_delay = frame_delay_run;
                else if (person_state == PERSON_JUMP) current_frame_delay = frame_delay_jump;
                else if (person_state == PERSON_DEAD) current_frame_delay = frame_delay_dead;
                else current_frame_delay = frame_delay_idle;
                // quando troca de estado, zera frame_timer e (opcional) ajusta frame
                if (person_state != prev_person_state) {
                    frame_timer = 0;
                    frame = 0;
                }
                prev_person_state = person_state;
                // atualiza frame usando delay atual
                frame_timer++;
                if (frame_timer >= current_frame_delay) {
                    frame_timer = 0;
                    frame++;
                    if (frame >= 7) frame = 0;
                }
            }
            if (redraw && al_is_event_queue_empty(queue)) {
                redraw = false;
                if (game_state == STATE_MENU) {
                    al_clear_to_color(al_map_rgb(0, 0, 180));
                    al_draw_text(font, al_map_rgb(255, 255, 255), SCREEN_W / 2, SCREEN_H / 2, ALLEGRO_ALIGN_CENTER, "Pressione ENTER para começar");
                    al_flip_display();
                }
                else if (game_state == STATE_FASE1 || game_state == STATE_FASE2) {
                    ALLEGRO_BITMAP* fundo = (game_state == STATE_FASE1) ? fundo1 : fundo2;
                    al_clear_to_color(al_map_rgb(0, 0, 0));
                    al_draw_scaled_bitmap(fundo, 0, 0, al_get_bitmap_width(fundo), al_get_bitmap_height(fundo), 0, 0, SCREEN_W, SCREEN_H, 0);
                    int dest_chao_h = chao_h + CHAO_EXTRA_PIXELS;
                    int dest_chao_y = SCREEN_H - dest_chao_h;
                    for (int x = 0; x <= SCREEN_W; x += chao_w)
                        al_draw_scaled_bitmap(chao, 0, 0, chao_w, chao_h, x, dest_chao_y, chao_w, dest_chao_h, 0);
                    al_draw_bitmap(plat, 100, 670,0);
                    int row = 1;
                    if (person_state == PERSON_RUN) row = 0;
                    else if (person_state == PERSON_JUMP) row = 4;
                    else if (person_state == PERSON_DEAD) row = 2;
                    else row = 3;
                    if (frame < 0) frame = 0;
                    if (frame >= sprite_cols) frame = 0;
                    int idx = row * sprite_cols + frame;
                    int fw = final_w_arr[idx];
                    int fh = final_h_arr[idx];
                    float scale = target_height_px / (float)fh;
                    int draw_w = (int)(fw * scale);
                    int draw_h = (int)(fh * scale);
                    int foot_offset = draw_h / 6;
                    int state_offset = (person_state == PERSON_IDLE) ? (draw_h / 12) : 0;
                    int flags = (key_left && !key_right) ? ALLEGRO_FLIP_HORIZONTAL : 0;
                    float draw_y = feetY - draw_h + foot_offset + state_offset + 276; // ⬇️ abaixa o personagem 80px
                    if (posX + draw_w > SCREEN_W) {
                        if (game_state == STATE_FASE1) {
                            game_state = STATE_FASE2;
                            posX = 35;
                            feetY = chao_y;
                        }
                        else if (game_state == STATE_FASE2) {
                            game_state = STATE_FINAL;
                        }
                    }
                    if (frames_final[idx]) {
                        al_draw_scaled_bitmap(frames_final[idx], 0, 0, fw, fh, posX, draw_y, draw_w, draw_h, flags);
                    }
                    al_flip_display();
                }
                else if (game_state == STATE_FINAL) {
                    al_clear_to_color(al_map_rgb(20, 20, 20));
                    al_draw_text(font, al_map_rgb(255, 0, 0), SCREEN_W / 2, SCREEN_H / 2 - 30, ALLEGRO_ALIGN_CENTER, "GAME OVER");
                    al_draw_text(font, al_map_rgb(255, 255, 255), SCREEN_W / 2, SCREEN_H / 2 + 20, ALLEGRO_ALIGN_CENTER, "Pressione ENTER para sair");
                    al_flip_display();
                }
            }
        }
    }
    // limpeza
    for (int i = 0; i < total_frames; i++) if (frames_final[i]) al_destroy_bitmap(frames_final[i]);
    free(frames_final);
    free(final_w_arr);
    free(final_h_arr);
    free(final_w_per_row);
    free(final_h_per_row);
    al_destroy_bitmap(spritesheet);
    al_destroy_bitmap(fundo1);
    al_destroy_bitmap(fundo2);
    al_destroy_bitmap(chao);
    al_destroy_timer(timer);
    al_destroy_event_queue(queue);
    al_destroy_font(font);
    al_destroy_display(display);
    return 0;
    // rotinas de limpeza em caso de erro
cleanup_frames:
    for (int i = 0; i < total_frames; i++) if (frames_final && frames_final[i]) al_destroy_bitmap(frames_final[i]);
    free(frames_final);
cleanup_per_row:
    free(final_w_per_row);
    free(final_h_per_row);
cleanup_subs:
    if (sub) {
        for (int i = 0; i < total_frames; i++) if (sub[i]) al_destroy_bitmap(sub[i]);
        free(sub);
    }
cleanup_images:
    if (plat) al_destroy_bitmap(plat);
    if (spritesheet) al_destroy_bitmap(spritesheet);
    if (fundo1) al_destroy_bitmap(fundo1);
    if (fundo2) al_destroy_bitmap(fundo2);
    if (chao) al_destroy_bitmap(chao);
    al_destroy_event_queue(queue);
    al_destroy_timer(timer);
    al_destroy_font(font);
    al_destroy_display(display);
    return -1;
}
