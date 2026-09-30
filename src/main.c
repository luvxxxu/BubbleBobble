#include "bb_game.h"

#include "raylib.h"

#define WINDOW_WIDTH (BB_WORLD_WIDTH * BB_TILE_SIZE)
#define WORLD_HEIGHT (BB_WORLD_HEIGHT * BB_TILE_SIZE)
#define WINDOW_HEIGHT (WORLD_HEIGHT + 24)

static const Color stage_accents[BB_STAGE_COUNT] = {
    {110, 184, 176, 255}, /* stage 1: teal */
    {164, 150, 203, 255}, /* stage 2: violet */
    {208, 174, 112, 255}  /* stage 3: amber */
};

static void draw_centered_text(const char *text, int y, int size, Color color)
{
    int x = (WINDOW_WIDTH - MeasureText(text, size)) / 2;
    DrawText(text, x, y, size, color);
}

static void draw_world(const BBGame *game)
{
    const Color background = {7, 9, 11, 255};
    const Color platform = {37, 44, 49, 255};
    const Color player_color = {159, 211, 155, 255};
    const Color bubble_color = {142, 200, 221, 255};
    Color accent;
    int i;

    ClearBackground(background);
    if (game->state == BB_STATE_MENU) return;
    accent = stage_accents[game->stage];

    for (i = 0; i < BB_PLATFORM_COUNT; ++i) {
        const BBPlatform *ledge = &bb_stage_platforms[game->stage][i];
        int x = (int)(ledge->x * BB_TILE_SIZE);
        int y = (int)(ledge->y * BB_TILE_SIZE);
        int width = (int)(ledge->width * BB_TILE_SIZE);
        DrawRectangle(x, y, width, 14, platform);
        DrawRectangle(x, y, width, 2, accent);
    }

    for (i = 0; i < BB_ENEMY_COUNT; ++i) {
        const BBEnemy *enemy = &game->enemies[i];
        if (enemy->alive) {
            int x = (int)(enemy->x * BB_TILE_SIZE);
            int y = (int)(enemy->y * BB_TILE_SIZE);
            int size = (int)(BB_ENEMY_SIZE * BB_TILE_SIZE);
            DrawRectangle(x, y, size, size, accent);
            DrawCircle(x + 8, y + 9, 2, WHITE);
            DrawCircle(x + 18, y + 9, 2, WHITE);
        }
    }

    for (i = 0; i < BB_MAX_BUBBLES; ++i) {
        const BBBubble *bubble = &game->bubbles[i];
        if (bubble->active) {
            int x = (int)(bubble->x * BB_TILE_SIZE);
            int y = (int)(bubble->y * BB_TILE_SIZE);
            DrawCircleLines(x, y, 8, bubble_color);
            DrawCircle(x - 3, y - 3, 2, WHITE);
        }
    }

    {
        const BBPlayer *player = &game->player;
        int x = (int)(player->x * BB_TILE_SIZE);
        int y = (int)(player->y * BB_TILE_SIZE);
        int size = (int)(BB_PLAYER_SIZE * BB_TILE_SIZE);
        Color color = player_color;

        if (player->invincible > 0.0f && ((int)(GetTime() * 10.0) % 2 == 0))
            color = Fade(player_color, 0.45f);

        DrawRectangle(x, y, size, size, color);
        DrawCircle(x + (player->facing > 0 ? 18 : 7), y + 8, 2, background);
    }
}

static void draw_hud(const BBGame *game)
{
    const Color panel = {12, 15, 18, 255};
    const Color muted = {143, 154, 160, 255};

    DrawRectangle(0, 0, WINDOW_WIDTH, 46, panel);
    DrawText("game", 18, 13, 20, WHITE);
    if (game->state != BB_STATE_MENU) {
        DrawText(TextFormat("stage %02d / %02d", game->stage + 1, BB_STAGE_COUNT),
                 265, 16, 16, stage_accents[game->stage]);
        DrawText(TextFormat("lives %d", game->player.lives), 505, 16, 16, muted);
        DrawText(TextFormat("enemies %d", bb_game_enemies_left(game)), 655, 16, 16, muted);
    }

    DrawRectangle(0, WORLD_HEIGHT, WINDOW_WIDTH, 24, panel);
    DrawText("move  A/D or arrows    jump  W/Space/Up    bubble  F/J    menu  Esc",
             18, WORLD_HEIGHT + 6, 12, muted);
}

static void draw_message(const char *title, const char *detail, const char *action)
{
    const Color muted = {143, 154, 160, 255};
    const Color accent = {142, 200, 221, 255};
    DrawRectangle(0, 46, WINDOW_WIDTH, WORLD_HEIGHT - 46, Fade(BLACK, 0.88f));
    draw_centered_text(title, 214, 38, WHITE);
    draw_centered_text(detail, 274, 18, muted);
    draw_centered_text(action, 336, 18, accent);
}

int main(void)
{
    BBGame game;

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "game");
    if (!IsWindowReady()) return 1;
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    bb_game_init(&game);

    while (!WindowShouldClose()) {
        if (game.state == BB_STATE_MENU) {
            if (IsKeyPressed(KEY_ENTER)) bb_game_start(&game);
        } else if (game.state == BB_STATE_PLAY) {
            BBInput input = {0};
            float dt = GetFrameTime();

            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) input.move -= 1.0f;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) input.move += 1.0f;
            input.jump = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP);
            input.fire = IsKeyPressed(KEY_F) || IsKeyPressed(KEY_J);

            if (IsKeyPressed(KEY_ESCAPE)) bb_game_menu(&game);
            else bb_game_update(&game, input, dt);
        } else if (game.state == BB_STATE_CLEAR) {
            if (IsKeyPressed(KEY_ENTER)) bb_game_next_stage(&game);
            if (IsKeyPressed(KEY_ESCAPE)) bb_game_menu(&game);
        } else {
            if (IsKeyPressed(KEY_ENTER)) bb_game_start(&game);
            if (IsKeyPressed(KEY_ESCAPE)) bb_game_menu(&game);
        }

        BeginDrawing();
        draw_world(&game);
        draw_hud(&game);
        if (game.state == BB_STATE_MENU)
            draw_message("game", "clear all three stages", "> press enter to start");
        else if (game.state == BB_STATE_CLEAR)
            draw_message(TextFormat("stage %02d clear", game.stage + 1),
                         "all enemies defeated", "> press enter for next stage");
        else if (game.state == BB_STATE_WON)
            draw_message("game clear", "all stages complete", "> press enter to play again");
        else if (game.state == BB_STATE_LOST)
            draw_message("game over", "no lives left", "> press enter to try again");
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
