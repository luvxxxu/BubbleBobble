#ifndef BB_VS2010_RAYLIB_H
#define BB_VS2010_RAYLIB_H

#include "bb_compat.h"

typedef struct Vector2 { float x, y; } Vector2;
typedef struct Rectangle { float x, y, width, height; } Rectangle;
typedef struct Color { unsigned char r, g, b, a; } Color;
typedef struct Image { void *data; int width, height, mipmaps, format; } Image;
typedef struct Texture { unsigned int id; int width, height, mipmaps, format; } Texture;
typedef Texture Texture2D;
typedef struct RenderTexture { unsigned int id; Texture texture, depth; } RenderTexture;
typedef RenderTexture RenderTexture2D;
typedef struct Font { int baseSize, glyphCount, glyphPadding; Texture2D texture; Rectangle *recs; void *glyphs; } Font;
typedef struct Wave { void *data; unsigned int frameCount, sampleRate, sampleSize, channels; } Wave;
typedef struct Sound { unsigned int frameCount; } Sound;
typedef struct Music { bool looping; } Music;
typedef unsigned char *(*LoadFileDataCallback)(const char *fileName, int *dataSize);

extern const Color WHITE;
extern const Color BLACK;
extern const Color GRAY;
extern const Color YELLOW;
extern const Color RED;

#define KEY_NULL 0
#define KEY_SPACE 32
#define KEY_SLASH 191
#define KEY_A 65
#define KEY_D 68
#define KEY_E 69
#define KEY_M 77
#define KEY_P 80
#define KEY_S 83
#define KEY_W 87
#define KEY_X 88
#define KEY_Z 90
#define KEY_ESCAPE 27
#define KEY_ENTER 13
#define KEY_RIGHT_CONTROL 163
#define KEY_RIGHT 39
#define KEY_LEFT 37
#define KEY_DOWN 40
#define KEY_UP 38
#define KEY_F11 122

#define GAMEPAD_AXIS_LEFT_X 0

#define GAMEPAD_BUTTON_RIGHT_FACE_DOWN 7
#define GAMEPAD_BUTTON_RIGHT_FACE_RIGHT 8
#define GAMEPAD_BUTTON_LEFT_FACE_UP 11
#define GAMEPAD_BUTTON_LEFT_FACE_RIGHT 12
#define GAMEPAD_BUTTON_LEFT_FACE_DOWN 13
#define GAMEPAD_BUTTON_LEFT_FACE_LEFT 14
#define GAMEPAD_BUTTON_MIDDLE_RIGHT 15
#define TEXTURE_FILTER_POINT 0
#define LOG_WARNING 3
#define FLAG_VSYNC_HINT 0x00000040U
#define FLAG_WINDOW_RESIZABLE 0x00000004U

void SetConfigFlags(unsigned int flags);
void InitWindow(int width, int height, const char *title);
void CloseWindow(void);
bool WindowShouldClose(void);
bool IsWindowReady(void);
bool IsWindowFocused(void);
void ToggleFullscreen(void);
int GetScreenWidth(void);
int GetScreenHeight(void);
void SetWindowMinSize(int width, int height);
void SetExitKey(int key);
void SetTargetFPS(int fps);
float GetFrameTime(void);
void SetTraceLogLevel(int logLevel);
void BeginDrawing(void);
void EndDrawing(void);
void BeginTextureMode(RenderTexture2D target);
void EndTextureMode(void);
void ClearBackground(Color color);
bool IsKeyDown(int key);
bool IsKeyPressed(int key);
int GetKeyPressed(void);
int GetCharPressed(void);
bool IsGamepadAvailable(int gamepad);
float GetGamepadAxisMovement(int gamepad, int axis);
bool IsGamepadButtonDown(int gamepad, int button);
bool IsGamepadButtonPressed(int gamepad, int button);
void *MemAlloc(unsigned int size);
void MemFree(void *memory);
void SetLoadFileDataCallback(LoadFileDataCallback callback);

Image LoadImage(const char *fileName);
Image LoadImageFromTexture(Texture2D texture);
bool IsImageValid(Image image);
void ImageFlipVertical(Image *image);
Color *LoadImageColors(Image image);
void UnloadImageColors(Color *colors);
void UnloadImage(Image image);
unsigned char *ExportImageToMemory(Image image, const char *fileType, int *fileSize);

Texture2D LoadTexture(const char *fileName);
void UnloadTexture(Texture2D texture);
RenderTexture2D LoadRenderTexture(int width, int height);
bool IsRenderTextureValid(RenderTexture2D target);
void UnloadRenderTexture(RenderTexture2D target);
void SetTextureFilter(Texture2D texture, int filter);
void DrawTexture(Texture2D texture, int posX, int posY, Color tint);
void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint);
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle destination, Vector2 origin, float rotation, Color tint);
void DrawRectangle(int posX, int posY, int width, int height, Color color);
void DrawCircle(int centerX, int centerY, float radius, Color color);
bool CheckCollisionRecs(Rectangle rec1, Rectangle rec2);
bool CheckCollisionCircles(Vector2 center1, float radius1, Vector2 center2, float radius2);

Font GetFontDefault(void);
Font LoadFontEx(const char *fileName, int fontSize, int *codepoints, int codepointCount);
bool IsFontValid(Font font);
void UnloadFont(Font font);
void DrawText(const char *text, int posX, int posY, int fontSize, Color color);
void DrawTextEx(Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint);
Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing);

void InitAudioDevice(void);
void CloseAudioDevice(void);
bool IsAudioDeviceReady(void);
void SetMasterVolume(float volume);
Wave LoadWave(const char *fileName);
bool IsWaveValid(Wave wave);
void UnloadWave(Wave wave);
Sound LoadSound(const char *fileName);
bool IsSoundValid(Sound sound);
void UnloadSound(Sound sound);
void PlaySound(Sound sound);
Music LoadMusicStreamFromMemory(const char *fileType, const unsigned char *data, int dataSize);
bool IsMusicValid(Music music);
void UnloadMusicStream(Music music);
void PlayMusicStream(Music music);
void UpdateMusicStream(Music music);
void SetMusicVolume(Music music, float volume);

#endif
