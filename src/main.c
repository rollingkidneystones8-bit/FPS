#include "raylib.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BASE_WIDTH 320
#define BASE_HEIGHT 180
#define PIXEL_SCALE 2

#define MAX_DECALS 32
#define MAX_DISSOLVES 16
#define MAX_TRAILS 32
#define MAX_PROP_SPOTS 12
#define COST_PERK 250
#define COST_SPEED 300
#define COST_REVIVE 350
#define COST_WALL_AMMO 150
#define COST_MYSTERY 400
#define MAX_FLASH_TIME 0.06f
#define PLAYER_HEIGHT 1.0f
#define PLAYER_EYE_HEIGHT 0.9f
#define PLAYER_MOVE_SPEED 3.0f
#define PLAYER_MAX_HEALTH 100.0f
#define MAX_PEERS 8
#define LAN_PORT 27015
#define MAX_NAME_LEN 16
#define LAN_NAME_BYTES 12
#define LAN_PACKET_SIZE 68
#define MAX_ARENAS 3

typedef enum PropKind
{
    PROP_PERK_QUICK,
    PROP_PERK_SPEED,
    PROP_PERK_REVIVE,
    PROP_WALL_AMMO,
    PROP_MYSTERY
} PropKind;

typedef struct PropSpot
{
    Vector3 position;
    PropKind kind;
} PropSpot;

typedef struct CoverPiece
{
    Vector3 position;
    Vector3 size;
    Color color;
} CoverPiece;

typedef struct ArenaPreset
{
    const char *name;
    PropSpot spots[MAX_PROP_SPOTS];
    int spotCount;
    Vector3 playerSpawn;
    Vector3 navPoints[8];
    int navCount;
    float navWeights[8];
    CoverPiece cover[8];
    int coverCount;
} ArenaPreset;

typedef enum GameMode
{
    MODE_MULTIPLAYER,
    MODE_ZOMBIES
} GameMode;

typedef enum MultiplayerVariant
{
    MULTI_FFA,
    MULTI_TEAM
} MultiplayerVariant;

typedef struct Weapon
{
    const char *name;
    float damage;
    float fireRate;
    float recoil;
    float spread;
    float range;
    Color color;
    int maxAmmo;
} Weapon;

typedef enum EnemyType
{
    ENEMY_BASIC,
    ENEMY_SPITTER,
    ENEMY_SPRINTER,
    ENEMY_BOSS
} EnemyType;

typedef struct Enemy
{
    Vector3 position;
    float radius;
    float health;
    bool active;
    EnemyType type;
    float wobblePhase;
    float attackCharge;
    float attackCooldown;
    float weakenTimer;
    bool weakenedByPlayer;
    int navTarget;
    float navCooldown;
} Enemy;

typedef struct ZombiesState
{
    Enemy enemies[16];
    int wave;
    float spawnCooldown;
    int activeCount;
    float waveTimer;
} ZombiesState;

typedef struct PlayerState
{
    float health;
    bool isDowned;
    float reviveProgress;
    float damageCooldown;
    int score;
    int cash;
} PlayerState;

typedef struct HitMarker
{
    float timer;
    bool isKill;
} HitMarker;

typedef struct KillfeedEntry
{
    char text[48];
    float timer;
    Color color;
} KillfeedEntry;

typedef struct Decal
{
    Vector3 position;
    float timer;
    Color color;
} Decal;

typedef struct Flash
{
    float timer;
    Color color;
} Flash;

typedef struct DissolveFX
{
    Vector3 position;
    float timer;
    float height;
    Color color;
} DissolveFX;

typedef struct TrailFX
{
    Vector3 position;
    float timer;
    Color color;
} TrailFX;

typedef struct Peer
{
    struct sockaddr_in addr;
    Vector3 position;
    Vector3 renderPos;
    double lastHeard;
    bool active;
    int weaponIndex;
    int ammo;
    float health;
    bool isDowned;
    bool isReviving;
    bool perkQuickfire;
    bool perkSpeed;
    bool perkRevive;
    int cash;
    int score;
    uint16_t joinAgeSeconds;
    bool catchupSent;
    char name[MAX_NAME_LEN];
    int team;
    bool teamMode;
    float respawnTimer;
    uint8_t lastDamageId;
    uint8_t lastEventId;
} Peer;

typedef struct LanState
{
    int socketFd;
    Peer peers[MAX_PEERS];
    double broadcastAccumulator;
    bool enabled;
    bool useChecksum;
    uint8_t lastPacket[LAN_PACKET_SIZE];
    size_t lastPacketSize;
    double selfJoinTime;
    struct sockaddr_in selfAddr;
    LanEvent incomingEvent;
    bool hasIncomingEvent;
} LanState;

typedef enum MenuAction
{
    MENU_ACTION_NAME,
    MENU_ACTION_AUDIO,
    MENU_ACTION_CHECKSUM,
    MENU_ACTION_MODE,
    MENU_ACTION_VARIANT,
    MENU_ACTION_TEAM,
    MENU_ACTION_ARENA,
    MENU_ACTION_SAVE,
    MENU_ACTION_FLASHLIGHT,
    MENU_ACTION_DITHER,
    MENU_ACTION_SPAWN
} MenuAction;

typedef struct LanPayload
{
    int16_t position[3];
    uint8_t weaponIndex;
    uint16_t ammo;
    uint8_t health;
    int8_t cashDelta;
    int8_t scoreDelta;
    uint16_t cash;
    uint16_t score;
    uint8_t flags;
    char name[LAN_NAME_BYTES];
    uint16_t joinSeconds;
    int16_t rayOrigin[3];
    int16_t rayDir[3];
    uint8_t rayDamage;
    uint8_t damageId;
    uint8_t eventKind;
    uint8_t eventTeam;
    uint8_t eventId;
    char eventTarget[LAN_NAME_BYTES];
} LanPayload;

typedef struct DamageEvent
{
    Vector3 origin;
    Vector3 dir;
    float damage;
    float ttl;
    uint8_t id;
} DamageEvent;

typedef struct LanEvent
{
    uint8_t kind;
    uint8_t team;
    uint8_t id;
    char target[LAN_NAME_BYTES];
    char actor[LAN_NAME_BYTES];
} LanEvent;

static Sound MakeTone(float frequency, float duration, float volume)
{
    const int sampleRate = 44100;
    int sampleCount = (int)(duration * sampleRate);
    short *samples = (short *)MemAlloc(sampleCount * sizeof(short));
    for (int i = 0; i < sampleCount; i++)
    {
        float t = (float)i / (float)sampleRate;
        float s = sinf(2.0f * PI * frequency * t) * volume;
        samples[i] = (short)(s * 32767.0f);
    }
    Wave wave = {
        .frameCount = sampleCount,
        .sampleRate = sampleRate,
        .sampleSize = 16,
        .channels = 1,
        .data = samples};

    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

static bool gAudioEnabled = true;
static void PlaySoundSafe(Sound sound)
{
    if (gAudioEnabled)
        PlaySound(sound);
}

static int16_t QuantizePosition(float v)
{
    float scaled = v * 100.0f; // centimeter precision
    if (scaled > 32767.0f)
        scaled = 32767.0f;
    if (scaled < -32768.0f)
        scaled = -32768.0f;
    return (int16_t)lroundf(scaled);
}

static float DequantizePosition(int16_t q)
{
    return (float)q / 100.0f;
}

static uint16_t ComputeChecksumBytes(const uint8_t *bytes, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += bytes[i];
    return (uint16_t)(sum & 0xFFFFu);
}

static size_t PackLanPayload(uint8_t *out,
                             const LanPayload *payload,
                             bool useChecksum)
{
    size_t offset = 0;
    for (int i = 0; i < 3; i++)
    {
        out[offset++] = (uint8_t)((payload->position[i] >> 8) & 0xFF);
        out[offset++] = (uint8_t)(payload->position[i] & 0xFF);
    }

    out[offset++] = payload->weaponIndex;
    out[offset++] = (uint8_t)((payload->ammo >> 8) & 0xFF);
    out[offset++] = (uint8_t)(payload->ammo & 0xFF);
    out[offset++] = payload->health;
    out[offset++] = (uint8_t)payload->cashDelta;
    out[offset++] = (uint8_t)payload->scoreDelta;
    out[offset++] = (uint8_t)((payload->cash >> 8) & 0xFF);
    out[offset++] = (uint8_t)(payload->cash & 0xFF);
    out[offset++] = (uint8_t)((payload->score >> 8) & 0xFF);
    out[offset++] = (uint8_t)(payload->score & 0xFF);
    out[offset++] = payload->flags;
    memset(&out[offset], 0, LAN_NAME_BYTES);
    memcpy(&out[offset], payload->name, LAN_NAME_BYTES);
    offset += LAN_NAME_BYTES;
    out[offset++] = (uint8_t)((payload->joinSeconds >> 8) & 0xFF);
    out[offset++] = (uint8_t)(payload->joinSeconds & 0xFF);
    for (int i = 0; i < 3; i++)
    {
        out[offset++] = (uint8_t)((payload->rayOrigin[i] >> 8) & 0xFF);
        out[offset++] = (uint8_t)(payload->rayOrigin[i] & 0xFF);
    }
    for (int i = 0; i < 3; i++)
    {
        out[offset++] = (uint8_t)((payload->rayDir[i] >> 8) & 0xFF);
        out[offset++] = (uint8_t)(payload->rayDir[i] & 0xFF);
    }
    out[offset++] = payload->rayDamage;
    out[offset++] = payload->damageId;
    out[offset++] = payload->eventKind;
    out[offset++] = payload->eventTeam;
    out[offset++] = payload->eventId;
    memset(&out[offset], 0, LAN_NAME_BYTES);
    memcpy(&out[offset], payload->eventTarget, LAN_NAME_BYTES);
    offset += LAN_NAME_BYTES;

    uint16_t checksum = useChecksum ? ComputeChecksumBytes(out, offset) : 0;
    out[offset++] = (uint8_t)((checksum >> 8) & 0xFF);
    out[offset++] = (uint8_t)(checksum & 0xFF);
    return offset;
}

static bool UnpackLanPayload(const uint8_t *in, size_t len, bool useChecksum, LanPayload *payload)
{
    if (len < 3 * sizeof(int16_t) + 1 + 2 + 1 + 1 + 1 + 2 + 2 + 1 + LAN_NAME_BYTES + 2 + 2 * 3 + 2 * 3 + 1 + 1 + 1 + 1 + LAN_NAME_BYTES + 2)
        return false;

    size_t offset = 0;
    for (int i = 0; i < 3; i++)
    {
        payload->position[i] = (int16_t)((in[offset] << 8) | in[offset + 1]);
        offset += 2;
    }
    payload->weaponIndex = in[offset++];
    payload->ammo = (uint16_t)((in[offset] << 8) | in[offset + 1]);
    offset += 2;
    payload->health = in[offset++];
    payload->cashDelta = (int8_t)in[offset++];
    payload->scoreDelta = (int8_t)in[offset++];
    payload->cash = (uint16_t)((in[offset] << 8) | in[offset + 1]);
    offset += 2;
    payload->score = (uint16_t)((in[offset] << 8) | in[offset + 1]);
    offset += 2;
    payload->flags = in[offset++];
    memcpy(payload->name, &in[offset], LAN_NAME_BYTES);
    offset += LAN_NAME_BYTES;
    payload->joinSeconds = (uint16_t)((in[offset] << 8) | in[offset + 1]);
    offset += 2;
    for (int i = 0; i < 3; i++)
    {
        payload->rayOrigin[i] = (int16_t)((in[offset] << 8) | in[offset + 1]);
        offset += 2;
    }
    for (int i = 0; i < 3; i++)
    {
        payload->rayDir[i] = (int16_t)((in[offset] << 8) | in[offset + 1]);
        offset += 2;
    }
    payload->rayDamage = in[offset++];
    payload->damageId = in[offset++];
    payload->eventKind = in[offset++];
    payload->eventTeam = in[offset++];
    payload->eventId = in[offset++];
    memcpy(payload->eventTarget, &in[offset], LAN_NAME_BYTES);
    payload->eventTarget[LAN_NAME_BYTES - 1] = '\0';
    offset += LAN_NAME_BYTES;
    uint16_t checksum = (uint16_t)((in[offset] << 8) | in[offset + 1]);

    if (useChecksum && checksum != 0)
    {
        uint16_t expected = ComputeChecksumBytes(in, offset);
        if (expected != checksum)
            return false;
    }

    return true;
}

static void UpdateCameraLean(Camera3D *camera, Vector2 *angles, float dt, float recoilOffset, float moveScale, bool allowMove)
{
    const float mouseScale = 0.0035f;

    Vector3 forward = {
        sinf(angles->x),
        0.0f,
        cosf(angles->x)};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3){0, 1, 0}));

    if (allowMove)
    {
        float speed = PLAYER_MOVE_SPEED * moveScale;
        if (IsKeyDown(KEY_W)) camera->position = Vector3Add(camera->position, Vector3Scale(forward, speed * dt));
        if (IsKeyDown(KEY_S)) camera->position = Vector3Subtract(camera->position, Vector3Scale(forward, speed * dt));
        if (IsKeyDown(KEY_A)) camera->position = Vector3Subtract(camera->position, Vector3Scale(right, speed * dt));
        if (IsKeyDown(KEY_D)) camera->position = Vector3Add(camera->position, Vector3Scale(right, speed * dt));
    }

    Vector2 mouseDelta = GetMouseDelta();
    angles->x += -mouseDelta.x * mouseScale;
    angles->y += -mouseDelta.y * mouseScale;
    angles->y = Clamp(angles->y, -PI / 2.0f + 0.1f, PI / 2.0f - 0.1f);

    float effectivePitch = Clamp(angles->y + recoilOffset, -PI / 2.0f + 0.1f, PI / 2.0f - 0.1f);

    Vector3 dir = {
        cosf(effectivePitch) * sinf(angles->x),
        sinf(effectivePitch),
        cosf(effectivePitch) * cosf(angles->x)};

    camera->target = Vector3Add(camera->position, dir);
    camera->position.y = PLAYER_HEIGHT;
}

static void DrawCrosshair(int screenWidth, int screenHeight)
{
    const int size = 4;
    DrawLine(screenWidth / 2 - size, screenHeight / 2, screenWidth / 2 + size, screenHeight / 2, DARKGREEN);
    DrawLine(screenWidth / 2, screenHeight / 2 - size, screenWidth / 2, screenHeight / 2 + size, DARKGREEN);
}

static void DrawFlashlightMask(int screenWidth, int screenHeight)
{
    BeginBlendMode(BLEND_ALPHA);
    DrawRectangle(0, 0, screenWidth, screenHeight, (Color){5, 6, 10, 210});
    BeginBlendMode(BLEND_SUBTRACT);
    float radius = (float)screenHeight * 0.38f;
    DrawCircleGradient(screenWidth / 2,
                       screenHeight / 2,
                       radius,
                       (Color){200, 200, 210, 245},
                       (Color){0, 0, 0, 0});
    DrawCircleGradient(screenWidth / 2,
                       screenHeight / 2 + radius * 0.12f,
                       radius * 0.55f,
                       (Color){220, 220, 220, 180},
                       (Color){0, 0, 0, 0});
    EndBlendMode();
}

static void DrawDitherMask(int screenWidth, int screenHeight)
{
    BeginBlendMode(BLEND_ALPHA);
    for (int y = 0; y < screenHeight; y += 4)
    {
        float depthFactor = (float)y / (float)screenHeight;
        unsigned char alpha = (unsigned char)(25 + depthFactor * 20);
        for (int x = 0; x < screenWidth; x += 4)
        {
            int pattern = ((x + y) / 4) % 4;
            Color tint = (Color){0, 0, 0, alpha};
            if (pattern == 0)
                DrawRectangle(x, y, 2, 2, tint);
            else if (pattern == 1)
                DrawRectangle(x + 2, y + 2, 2, 2, tint);
            else if (pattern == 2)
                DrawRectangle(x + 1, y + 1, 2, 2, tint);
            else
                DrawRectangle(x + 3, y + 1, 1, 2, tint);
        }
    }
    EndBlendMode();
}

static void DrawMuzzleFlash(const Flash *flash, Camera3D *camera, Texture2D flashTex)
{
    if (flash->timer <= 0.0f)
        return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 pos = Vector3Add(camera->position, Vector3Scale(forward, 0.6f));
    DrawBillboard(camera, flashTex, pos, 0.5f, flash->color);
}

static float Quantize(float value, float step)
{
    return floorf(value / step) * step;
}

static Vector3 QuantizeVec3(Vector3 v, float step)
{
    return (Vector3){Quantize(v.x, step), Quantize(v.y, step), Quantize(v.z, step)};
}

static Color PropColor(PropKind kind)
{
    switch (kind)
    {
    case PROP_PERK_QUICK:
        return (Color){120, 200, 255, 255};
    case PROP_PERK_SPEED:
        return (Color){90, 200, 200, 255};
    case PROP_PERK_REVIVE:
        return (Color){120, 200, 120, 255};
    case PROP_WALL_AMMO:
        return (Color){200, 120, 120, 255};
    case PROP_MYSTERY:
    default:
        return (Color){200, 180, 90, 255};
    }
}

static void SanitizePresetName(const char *name, char *out, size_t outSize)
{
    size_t idx = 0;
    for (size_t i = 0; name[i] != '\0' && idx + 1 < outSize; i++)
    {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c == ' ')
            c = '_';
        out[idx++] = c;
    }
    out[idx] = '\0';
}

static void BuildPresetPath(const char *name, char *out, size_t outSize)
{
    char safe[32];
    SanitizePresetName(name, safe, sizeof(safe));
    snprintf(out, outSize, "layout_%s.txt", safe);
}

static bool LoadPresetOverride(const char *name, PropSpot *spots, int *count)
{
    char path[48];
    BuildPresetPath(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    int loaded = 0;
    while (!feof(f) && loaded < MAX_PROP_SPOTS)
    {
        int kind = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (fscanf(f, "%d %f %f %f", &kind, &x, &y, &z) == 4)
        {
            spots[loaded].kind = (PropKind)kind;
            spots[loaded].position = (Vector3){x, y, z};
            loaded++;
        }
        else
        {
            break;
        }
    }
    fclose(f);
    if (loaded > 0)
    {
        *count = loaded;
        return true;
    }
    return false;
}

static void SavePreset(const char *name, const PropSpot *spots, int count)
{
    char path[48];
    BuildPresetPath(name, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (int i = 0; i < count; i++)
    {
        fprintf(f, "%d %.3f %.3f %.3f\n",
                spots[i].kind,
                spots[i].position.x,
                spots[i].position.y,
                spots[i].position.z);
    }
    fclose(f);
}

static const ArenaPreset gArenaPresets[MAX_ARENAS] = {
    {.name = "Courtyard",
     .spots = {{{-2.0f, 0.0f, 2.0f}, PROP_PERK_QUICK},
               {{3.0f, 0.0f, 2.5f}, PROP_PERK_SPEED},
               {{-2.5f, 0.0f, -2.5f}, PROP_PERK_REVIVE},
               {{3.0f, 0.0f, -2.0f}, PROP_WALL_AMMO},
               {{-3.0f, 0.0f, -3.0f}, PROP_MYSTERY}},
     .spotCount = 5,
     .playerSpawn = {0.0f, PLAYER_HEIGHT, -1.0f},
     .navPoints = {{-3.2f, 0.0f, 2.8f}, {3.4f, 0.0f, 2.6f}, {-3.4f, 0.0f, -2.8f}, {3.0f, 0.0f, -2.6f}, {0.0f, 0.0f, 3.2f}},
     .navWeights = {1.0f, 1.1f, 0.9f, 1.0f, 1.2f},
     .navCount = 5,
     .cover = {{{-2.6f, 0.3f, 1.2f}, {0.7f, 0.6f, 0.4f}, {70, 90, 120, 255}},
               {{2.2f, 0.3f, -0.8f}, {0.6f, 0.6f, 0.6f}, {90, 110, 150, 255}},
               {{0.0f, 0.25f, -2.6f}, {0.9f, 0.5f, 0.5f}, {80, 80, 110, 255}}},
     .coverCount = 3},
    {.name = "Hangar",
     .spots = {{{-1.2f, 0.0f, 3.4f}, PROP_PERK_QUICK},
               {{2.4f, 0.0f, 0.8f}, PROP_PERK_SPEED},
               {{-3.2f, 0.0f, -0.8f}, PROP_PERK_REVIVE},
               {{1.8f, 0.0f, -3.0f}, PROP_WALL_AMMO},
               {{-0.4f, 0.0f, -3.6f}, PROP_MYSTERY},
               {{3.6f, 0.0f, 3.6f}, PROP_WALL_AMMO}},
     .spotCount = 6,
     .playerSpawn = {-1.0f, PLAYER_HEIGHT, 0.0f},
     .navPoints = {{-3.6f, 0.0f, 3.0f}, {3.2f, 0.0f, 2.8f}, {-3.2f, 0.0f, -2.4f}, {3.2f, 0.0f, -2.6f}, {0.0f, 0.0f, 0.0f}},
     .navWeights = {1.15f, 1.0f, 0.85f, 1.0f, 1.25f},
     .navCount = 5,
     .cover = {{{-0.8f, 0.4f, 1.6f}, {0.9f, 0.8f, 0.5f}, {110, 100, 120, 255}},
               {{2.8f, 0.35f, -0.6f}, {0.8f, 0.7f, 0.6f}, {120, 120, 90, 255}},
               {{-3.0f, 0.35f, -1.6f}, {0.7f, 0.6f, 0.7f}, {70, 80, 110, 255}}},
     .coverCount = 3},
    {.name = "Corridors",
     .spots = {{{-3.8f, 0.0f, 0.4f}, PROP_PERK_QUICK},
               {{-1.2f, 0.0f, -3.6f}, PROP_PERK_SPEED},
               {{2.6f, 0.0f, -2.6f}, PROP_PERK_REVIVE},
               {{3.4f, 0.0f, 2.2f}, PROP_WALL_AMMO},
               {{0.2f, 0.0f, 3.6f}, PROP_MYSTERY}},
     .spotCount = 5,
     .playerSpawn = {1.2f, PLAYER_HEIGHT, 1.2f},
     .navPoints = {{-3.6f, 0.0f, 0.0f}, {3.2f, 0.0f, 0.0f}, {0.0f, 0.0f, -3.4f}, {0.0f, 0.0f, 3.6f}},
     .navWeights = {1.05f, 1.05f, 1.2f, 0.95f},
     .navCount = 4,
     .cover = {{{-1.0f, 0.35f, 0.0f}, {0.9f, 0.7f, 0.5f}, {90, 110, 130, 255}},
               {{2.8f, 0.35f, 1.8f}, {0.8f, 0.7f, 0.7f}, {130, 90, 80, 255}},
               {{0.8f, 0.35f, -2.4f}, {0.7f, 0.6f, 0.7f}, {80, 80, 100, 255}}},
     .coverCount = 3}};

static int PropCost(PropKind kind)
{
    switch (kind)
    {
    case PROP_PERK_QUICK:
        return COST_PERK;
    case PROP_PERK_SPEED:
        return COST_SPEED;
    case PROP_PERK_REVIVE:
        return COST_REVIVE;
    case PROP_WALL_AMMO:
        return COST_WALL_AMMO;
    case PROP_MYSTERY:
    default:
        return COST_MYSTERY;
    }
}

static void DrawRetroCube(Vector3 position, float width, float height, float length, Color color)
{
    Vector3 snapped = QuantizeVec3(position, 0.05f);
    DrawCube(snapped, width, height, length, color);
    DrawCubeWires(snapped, width, height, length, DARKGRAY);
}

static bool InitLan(LanState *lan)
{
    memset(lan, 0, sizeof(*lan));
    lan->socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (lan->socketFd < 0)
        return false;

    int broadcastEnable = 1;
    setsockopt(lan->socketFd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(LAN_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(lan->socketFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(lan->socketFd);
        lan->socketFd = -1;
        return false;
    }

    lan->selfAddr = addr;

    char host[256];
    if (gethostname(host, sizeof(host)) == 0)
    {
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo *info = NULL;
        if (getaddrinfo(host, NULL, &hints, &info) == 0)
        {
            for (struct addrinfo *it = info; it; it = it->ai_next)
            {
                if (it->ai_family == AF_INET)
                {
                    lan->selfAddr = *(struct sockaddr_in *)it->ai_addr;
                    lan->selfAddr.sin_port = htons(LAN_PORT);
                    break;
                }
            }
            freeaddrinfo(info);
        }
    }

    fcntl(lan->socketFd, F_SETFL, O_NONBLOCK);
    lan->enabled = true;
    lan->broadcastAccumulator = 0.0;
    lan->useChecksum = true;
    lan->selfJoinTime = GetTime();
    lan->hasIncomingEvent = false;
    return true;
}

static void UpdateLan(LanState *lan,
                      float dt,
                      Vector3 playerPos,
                      int weaponIndex,
                      int ammo,
                     PlayerState *player,
                     bool quickfire,
                     bool speed,
                     bool revive,
                      MultiplayerVariant multiVariant,
                      int playerTeam,
                      const char *playerName,
                      double timeNow,
                      int *pendingCashShare,
                      int *pendingScoreShare,
                      float *sharePipTimer,
                      int *sharePipCash,
                      int *sharePipScore,
                      const DamageEvent *damageRay,
                      bool allowDamageBursts,
                      LanEvent *outEvent,
                      uint8_t *eventCounter)
{
    if (!lan->enabled)
        return;

    struct sockaddr_in bcast = {
        .sin_family = AF_INET,
        .sin_port = htons(LAN_PORT),
        .sin_addr.s_addr = htonl(INADDR_BROADCAST)};

    lan->broadcastAccumulator += dt;
    if (lan->broadcastAccumulator > 0.18f)
    {
        lan->broadcastAccumulator = 0.0;
        LanPayload payload = {0};
        payload.position[0] = QuantizePosition(playerPos.x);
        payload.position[1] = QuantizePosition(playerPos.y);
        payload.position[2] = QuantizePosition(playerPos.z);
        payload.weaponIndex = (uint8_t)weaponIndex;
        payload.ammo = (uint16_t)Clamp(ammo, 0, 60000);
        payload.health = (uint8_t)Clamp((int)((player->health / PLAYER_MAX_HEALTH) * 255.0f), 0, 255);
        payload.cash = (uint16_t)Clamp(player->cash, 0, 60000);
        payload.score = (uint16_t)Clamp(player->score, 0, 60000);
        payload.cashDelta = (int8_t)Clamp(*pendingCashShare, -120, 120);
        payload.scoreDelta = (int8_t)Clamp(*pendingScoreShare, -120, 120);
        payload.joinSeconds = (uint16_t)Clamp((int)(timeNow - lan->selfJoinTime), 0, 65000);
        int flags = 0;
        if (player->isDowned) flags |= 1 << 0;
        if (quickfire) flags |= 1 << 1;
        if (speed) flags |= 1 << 2;
        if (revive) flags |= 1 << 3;
        if (player->reviveProgress > 0.0f) flags |= 1 << 4;
        if (playerTeam == 1) flags |= 1 << 5;
        if (multiVariant == MULTI_TEAM) flags |= 1 << 6;
        payload.flags = (uint8_t)flags;
        strncpy(payload.name, playerName, LAN_NAME_BYTES - 1);
        if (outEvent && outEvent->kind > 0 && eventCounter)
        {
            payload.eventKind = outEvent->kind;
            payload.eventTeam = outEvent->team;
            payload.eventId = (*eventCounter)++;
            strncpy(payload.eventTarget, outEvent->target, LAN_NAME_BYTES - 1);
        }
        if (damageRay && damageRay->ttl > 0.0f)
        {
            for (int i = 0; i < 3; i++)
            {
                float o = ((float *)&damageRay->origin)[i];
                float d = ((float *)&damageRay->dir)[i];
                payload.rayOrigin[i] = QuantizePosition(o);
                payload.rayDir[i] = QuantizePosition(d);
            }
            payload.rayDamage = (uint8_t)Clamp((int)damageRay->damage, 0, 255);
            payload.damageId = damageRay->id;
        }
        uint8_t buffer[LAN_PACKET_SIZE] = {0};
        size_t packetSize = PackLanPayload(buffer, &payload, lan->useChecksum);
        memcpy(lan->lastPacket, buffer, packetSize);
        lan->lastPacketSize = packetSize;
        sendto(lan->socketFd, buffer, packetSize, 0, (struct sockaddr *)&bcast, sizeof(bcast));
        *pendingCashShare = 0;
        *pendingScoreShare = 0;
        if (outEvent)
            outEvent->kind = 0;
    }

    struct sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    uint8_t buffer[LAN_PACKET_SIZE] = {0};
    int read = 0;
    while ((read = recvfrom(lan->socketFd, buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &fromLen)) > 0)
    {
        LanPayload packet;
        if (!UnpackLanPayload(buffer, read, lan->useChecksum, &packet))
            continue;

        bool assigned = false;
        if (from.sin_addr.s_addr == lan->selfAddr.sin_addr.s_addr && from.sin_port == lan->selfAddr.sin_port)
            continue;

        for (int i = 0; i < MAX_PEERS; i++)
        {
            Peer *p = &lan->peers[i];
            if (p->active && p->addr.sin_addr.s_addr == from.sin_addr.s_addr && p->addr.sin_port == from.sin_port)
            {
                Vector3 target = {DequantizePosition(packet.position[0]),
                                  DequantizePosition(packet.position[1]),
                                  DequantizePosition(packet.position[2])};
                p->position = target;
                p->renderPos = Vector3Lerp(p->renderPos, target, Clamp(dt * 8.0f, 0.0f, 1.0f));
                p->weaponIndex = packet.weaponIndex;
                p->ammo = packet.ammo;
                p->health = ((float)packet.health / 255.0f) * PLAYER_MAX_HEALTH;
                p->isDowned = (packet.flags & (1 << 0)) != 0;
                p->perkQuickfire = (packet.flags & (1 << 1)) != 0;
                p->perkSpeed = (packet.flags & (1 << 2)) != 0;
                p->perkRevive = (packet.flags & (1 << 3)) != 0;
                p->isReviving = (packet.flags & (1 << 4)) != 0;
                p->team = (packet.flags & (1 << 5)) != 0 ? 1 : 0;
                p->teamMode = (packet.flags & (1 << 6)) != 0;
                p->cash = packet.cash;
                p->score = packet.score;
                p->joinAgeSeconds = packet.joinSeconds;
                if (packet.name[0])
                    strncpy(p->name, packet.name, sizeof(p->name));
                p->lastHeard = timeNow;
                if (packet.eventKind > 0 && packet.eventId != p->lastEventId)
                {
                    const char *actorName = p->name[0] ? p->name : "Peer";
                    memset(&lan->incomingEvent, 0, sizeof(lan->incomingEvent));
                    lan->incomingEvent.kind = packet.eventKind;
                    lan->incomingEvent.team = p->team;
                    lan->incomingEvent.id = packet.eventId;
                    strncpy(lan->incomingEvent.actor, actorName, LAN_NAME_BYTES - 1);
                    strncpy(lan->incomingEvent.target, packet.eventTarget, LAN_NAME_BYTES - 1);
                    lan->hasIncomingEvent = true;
                    p->lastEventId = packet.eventId;
                }
                assigned = true;
                player->cash = (int)Clamp((float)player->cash + (float)packet.cashDelta, 0.0f, 60000.0f);
                player->score = (int)Clamp((float)player->score + (float)packet.scoreDelta, 0.0f, 60000.0f);
                if ((packet.cashDelta != 0 || packet.scoreDelta != 0) && sharePipTimer && sharePipCash && sharePipScore)
                {
                    *sharePipTimer = 1.6f;
                    *sharePipCash = packet.cashDelta;
                    *sharePipScore = packet.scoreDelta;
                }
                if (allowDamageBursts && packet.rayDamage > 0 && packet.damageId != p->lastDamageId)
                {
                    Vector3 rayOrigin = {DequantizePosition(packet.rayOrigin[0]),
                                         DequantizePosition(packet.rayOrigin[1]),
                                         DequantizePosition(packet.rayOrigin[2])};
                    Vector3 rayDir = {DequantizePosition(packet.rayDir[0]),
                                      DequantizePosition(packet.rayDir[1]),
                                      DequantizePosition(packet.rayDir[2])};
                    float tHit = 50.0f;
                    if (HitscanAgainstSphere(rayOrigin, Vector3Normalize(rayDir), playerPos, 0.35f, &tHit))
                    {
                        player->health -= packet.rayDamage;
                        player->damageCooldown = 0.6f;
                    }
                    p->lastDamageId = packet.damageId;
                }
                break;
            }
        }
        if (!assigned)
        {
            for (int i = 0; i < MAX_PEERS; i++)
            {
                Peer *p = &lan->peers[i];
                if (!p->active)
                {
                    p->active = true;
                    p->addr = from;
                    p->position = (Vector3){DequantizePosition(packet.position[0]),
                                            DequantizePosition(packet.position[1]),
                                            DequantizePosition(packet.position[2])};
                    p->renderPos = p->position;
                    p->weaponIndex = packet.weaponIndex;
                    p->ammo = packet.ammo;
                    p->health = ((float)packet.health / 255.0f) * PLAYER_MAX_HEALTH;
                    p->isDowned = (packet.flags & (1 << 0)) != 0;
                    p->perkQuickfire = (packet.flags & (1 << 1)) != 0;
                    p->perkSpeed = (packet.flags & (1 << 2)) != 0;
                    p->perkRevive = (packet.flags & (1 << 3)) != 0;
                    p->isReviving = (packet.flags & (1 << 4)) != 0;
                    p->team = (packet.flags & (1 << 5)) != 0 ? 1 : 0;
                    p->teamMode = (packet.flags & (1 << 6)) != 0;
                    p->cash = packet.cash;
                    p->score = packet.score;
                    p->joinAgeSeconds = packet.joinSeconds;
                    if (packet.name[0])
                        strncpy(p->name, packet.name, sizeof(p->name));
                    unsigned int addr = ntohl(from.sin_addr.s_addr);
                    unsigned int octet = addr & 0xFF;
                    if (p->name[0] == '\0')
                        snprintf(p->name, sizeof(p->name), "P-%02u", octet);
                    p->lastHeard = timeNow;
                    p->catchupSent = false;
                    if (packet.eventKind > 0)
                        p->lastEventId = packet.eventId;
                    if (lan->lastPacketSize > 0)
                        sendto(lan->socketFd,
                               lan->lastPacket,
                               lan->lastPacketSize,
                               0,
                               (struct sockaddr *)&from,
                               sizeof(from));
                    player->cash = (int)Clamp((float)player->cash + (float)packet.cashDelta, 0.0f, 60000.0f);
                    player->score = (int)Clamp((float)player->score + (float)packet.scoreDelta, 0.0f, 60000.0f);
                    if ((packet.cashDelta != 0 || packet.scoreDelta != 0) && sharePipTimer && sharePipCash && sharePipScore)
                    {
                        *sharePipTimer = 1.6f;
                        *sharePipCash = packet.cashDelta;
                        *sharePipScore = packet.scoreDelta;
                    }
                    if (allowDamageBursts && packet.rayDamage > 0 && packet.damageId != p->lastDamageId)
                    {
                        Vector3 rayOrigin = {DequantizePosition(packet.rayOrigin[0]),
                                             DequantizePosition(packet.rayOrigin[1]),
                                             DequantizePosition(packet.rayOrigin[2])};
                        Vector3 rayDir = {DequantizePosition(packet.rayDir[0]),
                                          DequantizePosition(packet.rayDir[1]),
                                          DequantizePosition(packet.rayDir[2])};
                        float tHit = 50.0f;
                        if (HitscanAgainstSphere(rayOrigin, Vector3Normalize(rayDir), playerPos, 0.35f, &tHit))
                        {
                            player->health -= packet.rayDamage;
                            player->damageCooldown = 0.6f;
                        }
                        p->lastDamageId = packet.damageId;
                    }
                    break;
                }
            }
        }
    }

    for (int i = 0; i < MAX_PEERS; i++)
    {
        Peer *p = &lan->peers[i];
        if (p->active && timeNow - p->lastHeard > 3.0)
            p->active = false;
        if (p->active)
        {
            p->renderPos = Vector3Lerp(p->renderPos, p->position, Clamp(dt * 6.0f, 0.0f, 1.0f));
            if (!p->catchupSent && p->joinAgeSeconds < 8)
            {
                *pendingCashShare = Clamp(*pendingCashShare + 20, -120, 120);
                *pendingScoreShare = Clamp(*pendingScoreShare + 20, -120, 120);
                p->catchupSent = true;
            }
        }
    }
}

static bool HitscanAgainstSphere(Vector3 origin, Vector3 dir, Vector3 center, float radius, float *tHit)
{
    Vector3 oc = Vector3Subtract(origin, center);
    float b = Vector3DotProduct(oc, dir);
    float c = Vector3DotProduct(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return false;

    float t = -b - sqrtf(discriminant);
    if (t < 0.0f)
        t = -b + sqrtf(discriminant);
    if (t < 0.0f)
        return false;

    if (tHit)
        *tHit = t;
    return true;
}

static void PushDissolve(DissolveFX *fx, int *idx, Vector3 pos, EnemyType type)
{
    fx[*idx].position = pos;
    fx[*idx].timer = 1.35f;
    fx[*idx].height = (type == ENEMY_BOSS) ? 1.4f : (type == ENEMY_SPITTER ? 0.8f : 1.0f);
    fx[*idx].color = (Color){180, 200, 200, 200};
    *idx = (*idx + 1) % MAX_DISSOLVES;
}

static int FireWeapon(const Weapon *weapon,
                      Vector3 origin,
                      Vector3 dir,
                      ZombiesState *zombies,
                      Decal *decals,
                      int *decalIndex,
                      DissolveFX *dissolves,
                      int *dissolveIndex,
                      int *kills,
                      int *cashEarned,
                      int *assistShare)
{
    int hits = 0;
    for (int i = 0; i < (int)(sizeof(zombies->enemies) / sizeof(zombies->enemies[0])); i++)
    {
        Enemy *e = &zombies->enemies[i];
        if (!e->active)
            continue;

        float t = weapon->range;
        if (HitscanAgainstSphere(origin, dir, e->position, e->radius, &t))
        {
            float damage = weapon->damage;
            if (e->weakenTimer > 0.0f)
                damage *= 1.35f;
            e->health -= damage;
            if (e->health <= 0)
            {
                e->active = false;
                zombies->activeCount--;
                if (kills)
                    (*kills)++;
                if (cashEarned)
                {
                    int reward = 40;
                    if (e->type == ENEMY_BOSS)
                        reward = 220;
                    else if (e->type == ENEMY_SPRINTER)
                        reward = 70;
                    else if (e->type == ENEMY_SPITTER)
                        reward = 90;
                    *cashEarned += reward;
                    if (assistShare && e->weakenedByPlayer)
                        *assistShare += reward / 3;
                }
                if (dissolves && dissolveIndex)
                    PushDissolve(dissolves, dissolveIndex, e->position, e->type);
            }
            hits++;

            decals[*decalIndex].position = Vector3Add(origin, Vector3Scale(dir, t));
            decals[*decalIndex].color = (Color){200, 90, 90, 255};
            decals[*decalIndex].timer = 1.5f;
            *decalIndex = (*decalIndex + 1) % MAX_DECALS;
        }
    }
    return hits;
}

static int FireAtPeers(const Weapon *weapon,
                       Vector3 origin,
                       Vector3 dir,
                       LanState *lan,
                       bool teamMode,
                       int playerTeam,
                       int *fraggedIndex)
{
    int hits = 0;
    for (int i = 0; i < MAX_PEERS; i++)
    {
        Peer *p = &lan->peers[i];
        if (!p->active)
            continue;
        if (teamMode && p->team == playerTeam)
            continue;
        float t = weapon->range;
        if (HitscanAgainstSphere(origin, dir, p->renderPos, 0.35f, &t))
        {
            hits++;
            p->health -= weapon->damage;
            if (p->health <= 0.0f)
            {
                p->respawnTimer = 1.5f;
                p->health = 0.0f;
                if (fraggedIndex)
                    *fraggedIndex = i;
            }
        }
    }
    return hits;
}

static int MeleeAssist(Vector3 origin,
                       Vector3 dir,
                       ZombiesState *zombies,
                       int *cashAssist,
                       float *assistFlash)
{
    int tagged = 0;
    for (int i = 0; i < (int)(sizeof(zombies->enemies) / sizeof(zombies->enemies[0])); i++)
    {
        Enemy *e = &zombies->enemies[i];
        if (!e->active)
            continue;
        float t = 1.6f;
        if (HitscanAgainstSphere(origin, dir, e->position, e->radius, &t))
        {
            e->health -= 6.0f;
            e->weakenTimer = 4.0f;
            e->weakenedByPlayer = true;
            tagged++;
            if (cashAssist)
                *cashAssist += 6;
        }
    }
    if (tagged > 0 && assistFlash)
        *assistFlash = 1.2f;
    return tagged;
}

static void SpawnEnemy(ZombiesState *zombies, Vector3 position, EnemyType type)
{
    for (int i = 0; i < (int)(sizeof(zombies->enemies) / sizeof(zombies->enemies[0])); i++)
    {
        if (!zombies->enemies[i].active)
        {
            zombies->enemies[i].position = position;
            zombies->enemies[i].type = type;
            zombies->enemies[i].radius = (type == ENEMY_BOSS) ? 0.6f : (type == ENEMY_SPITTER ? 0.4f : 0.35f);
            float baseHealth = 0.0f;
            switch (type)
            {
            case ENEMY_BOSS:
                baseHealth = 180.0f;
                break;
            case ENEMY_SPITTER:
                baseHealth = 50.0f;
                break;
            case ENEMY_SPRINTER:
                baseHealth = 22.0f;
                break;
            default:
                baseHealth = 30.0f;
                break;
            }
            zombies->enemies[i].health = baseHealth + zombies->wave * (type == ENEMY_BOSS ? 15.0f : 6.0f);
            zombies->enemies[i].active = true;
            zombies->enemies[i].wobblePhase = GetRandomValue(0, 628) / 100.0f;
            zombies->enemies[i].attackCharge = 0.0f;
            zombies->enemies[i].attackCooldown = 0.0f;
            zombies->enemies[i].weakenTimer = 0.0f;
            zombies->enemies[i].weakenedByPlayer = false;
            zombies->enemies[i].navTarget = -1;
            zombies->enemies[i].navCooldown = 0.0f;
            zombies->activeCount++;
            break;
        }
    }
}

static void PushTrail(TrailFX *fx, int *idx, Vector3 pos, Color color)
{
    fx[*idx].position = pos;
    fx[*idx].timer = 0.8f;
    fx[*idx].color = color;
    *idx = (*idx + 1) % MAX_TRAILS;
}

static int ChooseNavTarget(const Vector3 *navPoints, const float *navWeights, int navCount, Vector3 playerPos)
{
    if (navCount <= 0)
        return -1;
    int best = 0;
    float bestDist = Vector3Distance(navPoints[0], playerPos);
    if (navWeights)
        bestDist /= fmaxf(navWeights[0], 0.01f);
    for (int i = 1; i < navCount; i++)
    {
        float d = Vector3Distance(navPoints[i], playerPos);
        if (navWeights)
            d /= fmaxf(navWeights[i], 0.01f);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    int jitter = GetRandomValue(0, 100);
    if (navCount > 1 && jitter > 65)
        best = (best + 1) % navCount;
    return best;
}

static Vector3 SelectSafeSpawn(const ArenaPreset *preset)
{
    if (!preset)
        return (Vector3){0};
    if (preset->navCount > 0)
    {
        float best = -1000.0f;
        int bestIndex = 0;
        for (int i = 0; i < preset->navCount; i++)
        {
            float w = (preset->navWeights[i] > 0.01f) ? preset->navWeights[i] : 0.01f;
            if (w > best)
            {
                best = w;
                bestIndex = i;
            }
        }
        Vector3 pos = preset->navPoints[bestIndex];
        pos.y = PLAYER_HEIGHT;
        return pos;
    }
    return preset->playerSpawn;
}

static void UpdateZombies(ZombiesState *zombies,
                          float dt,
                          Vector3 playerPos,
                          PlayerState *player,
                          TrailFX *trails,
                          int *trailIndex,
                          const Vector3 *navPoints,
                          const float *navWeights,
                          int navCount)
{
    const float spawnDelay = 2.0f;
    zombies->spawnCooldown -= dt;
    zombies->waveTimer += dt;

    if (zombies->spawnCooldown <= 0.0f && zombies->activeCount < 6)
    {
        float angle = GetRandomValue(0, 628) / 100.0f;
        float dist = 6.0f + zombies->wave * 0.2f;
        Vector3 pos = {cosf(angle) * dist, 0.0f, sinf(angle) * dist};
        bool boss = (zombies->wave % 5 == 0) && (zombies->waveTimer < 1.0f);
        EnemyType type = boss ? ENEMY_BOSS : ENEMY_BASIC;
        if (!boss)
        {
            int roll = GetRandomValue(0, 100);
            if (zombies->wave > 2 && roll > 65)
                type = ENEMY_SPRINTER;
            else if (zombies->wave > 3 && roll > 40)
                type = ENEMY_SPITTER;
        }
        SpawnEnemy(zombies, pos, type);
        zombies->spawnCooldown = spawnDelay;
    }

    for (int i = 0; i < (int)(sizeof(zombies->enemies) / sizeof(zombies->enemies[0])); i++)
    {
        Enemy *e = &zombies->enemies[i];
        if (!e->active)
            continue;

        Vector3 toPlayer = Vector3Subtract(playerPos, e->position);
        toPlayer.y = 0.0f;
        float dist = Vector3Length(toPlayer);
        Vector3 toTarget = toPlayer;
        if (navPoints && navCount > 0)
        {
            e->navCooldown -= dt;
            if (e->navTarget < 0 || e->navTarget >= navCount || e->navCooldown <= 0.0f)
            {
                e->navTarget = ChooseNavTarget(navPoints, navWeights, navCount, playerPos);
                e->navCooldown = 2.0f + (float)GetRandomValue(0, 60) / 60.0f;
            }
            if (e->navTarget >= 0 && e->navTarget < navCount)
            {
                Vector3 navGoal = navPoints[e->navTarget];
                navGoal.y = 0.0f;
                if (Vector3Distance(e->position, navGoal) < 0.55f)
                    e->navCooldown = 0.0f;
                if (Vector3Distance(navGoal, playerPos) > 0.4f)
                    toTarget = Vector3Subtract(navGoal, e->position);
            }
        }
        if (e->weakenTimer > 0.0f)
        {
            e->weakenTimer -= dt;
            if (e->weakenTimer < 0.0f)
                e->weakenTimer = 0.0f;
        }
        float weakenScale = e->weakenTimer > 0.0f ? 0.78f : 1.0f;
        float moveDist = Vector3Length(toTarget);
        if (moveDist > 0.001f)
        {
            float speed = 2.2f;
            if (e->type == ENEMY_BOSS)
                speed = 1.6f;
            else if (e->type == ENEMY_SPRINTER)
                speed = 3.8f;
            else if (e->type == ENEMY_SPITTER)
                speed = 1.9f;
            Vector3 dir = Vector3Normalize(toTarget);
            Vector3 step = Vector3Scale(dir, speed * weakenScale * dt);
            if (Vector3Length(step) > moveDist)
                step = Vector3Scale(dir, moveDist);
            e->position = Vector3Add(e->position, step);
        }

        e->wobblePhase += dt * ((e->type == ENEMY_BOSS) ? 2.0f : 2.8f);

        if (e->attackCooldown > 0.0f)
            e->attackCooldown -= dt;

        const float attackRange = 1.05f;
        const float windupTime = 0.35f;
        if (e->type == ENEMY_SPITTER)
        {
            const float spitRange = 7.5f;
            const float spitWind = 0.5f;
            if (dist < spitRange)
            {
                e->attackCharge += dt;
                if (e->attackCharge >= spitWind && e->attackCooldown <= 0.0f)
                {
                    player->health -= 8.0f;
                    player->damageCooldown = 0.8f;
                    e->attackCharge = 0.0f;
                    e->attackCooldown = 2.0f;
                    if (trails && trailIndex)
                    {
                        Vector3 dir = Vector3Normalize(toPlayer);
                        for (int t = 1; t <= 4; t++)
                        {
                            Vector3 pos = Vector3Add(e->position, Vector3Scale(dir, (float)t * 0.35f));
                            pos.y = 0.5f;
                            PushTrail(trails, trailIndex, pos, (Color){140, 200, 255, 200});
                        }
                    }
                }
            }
            else
            {
                e->attackCharge = 0.0f;
            }
        }
        else
        {
            if (dist < attackRange)
            {
                e->attackCharge += dt;
                if (e->attackCharge >= windupTime && e->attackCooldown <= 0.0f && player->damageCooldown <= 0.0f)
                {
                    float dmg = (e->type == ENEMY_BOSS) ? 25.0f : (e->type == ENEMY_SPRINTER ? 10.0f : 12.0f);
                    if (e->weakenTimer > 0.0f)
                        dmg *= 0.65f;
                    player->health -= dmg;
                    player->damageCooldown = 1.0f;
                    e->attackCharge = 0.0f;
                    e->attackCooldown = 1.35f;
                }
            }
            else
            {
                e->attackCharge = 0.0f;
            }
        }
    }

    if (zombies->activeCount == 0)
    {
        zombies->wave++;
        zombies->spawnCooldown = 0.5f;
        zombies->waveTimer = 0.0f;
    }
}

static void DrawZombies(const ZombiesState *zombies)
{
    for (int i = 0; i < (int)(sizeof(zombies->enemies) / sizeof(zombies->enemies[0])); i++)
    {
        if (!zombies->enemies[i].active)
            continue;
        float wobble = sinf(zombies->enemies[i].wobblePhase) * 0.15f;
        Color baseTint = {120, 200, 120, 255};
        if (zombies->enemies[i].type == ENEMY_BOSS)
            baseTint = (Color){190, 120, 40, 255};
        else if (zombies->enemies[i].type == ENEMY_SPITTER)
            baseTint = (Color){160, 180, 220, 255};
        else if (zombies->enemies[i].type == ENEMY_SPRINTER)
            baseTint = (Color){200, 120, 180, 255};
        if (zombies->enemies[i].weakenTimer > 0.0f)
        {
            baseTint = (Color){(unsigned char)Clamp(baseTint.r + 20, 0, 255),
                               (unsigned char)Clamp(baseTint.g + 35, 0, 255),
                               (unsigned char)Clamp(baseTint.b + 35, 0, 255),
                               255};
        }
        float charge = Clamp(zombies->enemies[i].attackCharge / 0.5f, 0.0f, 1.0f);
        Color tint = ColorAlpha(RED, charge);
        tint = (Color){
            (unsigned char)Clamp(baseTint.r + (int)tint.r, 0, 255),
            (unsigned char)Clamp(baseTint.g - (int)(charge * 80), 0, 255),
            (unsigned char)Clamp(baseTint.b - (int)(charge * 60), 0, 255),
            255};
        float h = (zombies->enemies[i].type == ENEMY_BOSS) ? 1.7f : (zombies->enemies[i].type == ENEMY_SPITTER ? 1.0f : 1.2f);
        float size = (zombies->enemies[i].type == ENEMY_BOSS) ? 1.0f : (zombies->enemies[i].type == ENEMY_SPITTER ? 0.6f : 0.7f);
        Vector3 pos = zombies->enemies[i].position;
        pos.y += wobble;
        DrawRetroCube(pos, size, h, size, tint);
        if (zombies->enemies[i].attackCharge > 0.1f)
        {
            float telegraphSize = 0.35f + charge * 0.3f;
            DrawSphere(Vector3Add(pos, (Vector3){0, h * 0.5f + 0.2f, 0}), telegraphSize, ColorAlpha(RED, 120));
        }
    }
}

static void DrawDecals(Decal *decals, float dt)
{
    for (int i = 0; i < MAX_DECALS; i++)
    {
        if (decals[i].timer <= 0.0f)
            continue;
        decals[i].timer -= dt;
        float alpha = Clamp(decals[i].timer, 0.0f, 1.0f);
        Color faded = decals[i].color;
        faded.a = (unsigned char)(alpha * 255);
        DrawSphere(decals[i].position, 0.08f, faded);
    }
}

static void UpdateDissolves(DissolveFX *fx, float dt)
{
    for (int i = 0; i < MAX_DISSOLVES; i++)
    {
        if (fx[i].timer <= 0.0f)
            continue;
        fx[i].timer -= dt;
        float alpha = Clamp(fx[i].timer, 0.0f, 1.0f);
        Color tint = fx[i].color;
        tint.a = (unsigned char)(alpha * 200);
        float scale = 0.4f + (1.0f - alpha) * 0.4f;
        DrawRetroCube(Vector3Add(fx[i].position, (Vector3){0, (1.0f - alpha) * 0.2f, 0}),
                      scale,
                      fx[i].height * alpha,
                      scale,
                      tint);
    }
}

static void UpdateTrails(TrailFX *fx, float dt)
{
    for (int i = 0; i < MAX_TRAILS; i++)
    {
        if (fx[i].timer <= 0.0f)
            continue;
        fx[i].timer -= dt;
        float alpha = Clamp(fx[i].timer, 0.0f, 1.0f);
        Color tint = fx[i].color;
        tint.a = (unsigned char)(alpha * 220);
        DrawSphere(fx[i].position, 0.08f + (1.0f - alpha) * 0.08f, tint);
    }
}

static void ResetZombies(ZombiesState *zombies)
{
    memset(zombies, 0, sizeof(*zombies));
    zombies->wave = 1;
    zombies->spawnCooldown = 0.25f;
    zombies->waveTimer = 0.0f;
}

static void ResetPlayer(PlayerState *player)
{
    player->health = PLAYER_MAX_HEALTH;
    player->isDowned = false;
    player->reviveProgress = 0.0f;
    player->damageCooldown = 0.0f;
    player->score = 0;
    player->cash = 500;
}

static void DrawMenuButton(Rectangle rect, const char *label, bool selected)
{
    Color outline = selected ? SKYBLUE : DARKGRAY;
    Color fill = selected ? (Color){20, 26, 42, 180} : (Color){14, 16, 24, 140};
    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, 2, outline);
    Vector2 textSize = MeasureTextEx(GetFontDefault(), label, 12, 1);
    DrawText(label,
             (int)(rect.x + (rect.width - textSize.x) * 0.5f),
             (int)(rect.y + (rect.height - textSize.y) * 0.5f),
             12,
             selected ? WHITE : LIGHTGRAY);
}

static void DrawCooldownBar(int x, int y, float t)
{
    int w = 38;
    int h = 6;
    DrawRectangleLines(x, y, w, h, DARKGRAY);
    float fill = Clamp(1.0f - t, 0.0f, 1.0f);
    DrawRectangle(x + 1, y + 1, (int)((w - 2) * fill), h - 2, fill >= 1.0f ? LIME : SKYBLUE);
}

static void PushKillfeed(KillfeedEntry *feed, int count, const char *text, Color color)
{
    if (count <= 0)
        return;
    for (int i = count - 1; i > 0; i--)
        feed[i] = feed[i - 1];
    strncpy(feed[0].text, text, sizeof(feed[0].text) - 1);
    feed[0].text[sizeof(feed[0].text) - 1] = '\0';
    feed[0].timer = 3.0f;
    feed[0].color = color;
}

static void PushKillfeedSfx(KillfeedEntry *feed, int count, const char *text, Color color, Sound sound)
{
    PushKillfeed(feed, count, text, color);
    PlaySoundSafe(sound);
}

static void DrawInfo(float dt,
                     GameMode mode,
                     const Weapon *weapon,
                     const ZombiesState *zombies,
                     const PlayerState *player,
                     int ammo,
                     bool quickfire,
                     bool speed,
                     bool revive,
                     const LanState *lan,
                     const char *playerName,
                     bool nameLocked,
                     bool audioOn,
                     bool flashlightOn,
                     bool ditherOn,
                     float fireCooldown,
                     float mysteryCooldown,
                     float damageCooldown,
                     const char *arenaName,
                     float sharePipTimer,
                     int sharePipCash,
                     int sharePipScore,
                     float assistFlash,
                     MultiplayerVariant mpVariant,
                     int playerTeam,
                     int frags,
                     int deaths,
                     const int teamScores[2],
                     const HitMarker *hitMarker,
                     const KillfeedEntry *killfeed,
                     int killfeedCount)
{
    DrawText("U8 Prototype", 8, 8, 10, LIGHTGRAY);
    DrawText(TextFormat("Frame: %d FPS", GetFPS()), 8, 20, 10, LIGHTGRAY);
    DrawText(TextFormat("dt: %.3f", dt), 8, 32, 10, LIGHTGRAY);
    DrawText(TextFormat("Name: %s%s", playerName, nameLocked ? "" : " (edit Enter)"), 8, 44, 10, LIGHTGRAY);
    DrawText(TextFormat("Audio: %s (M)", audioOn ? "on" : "muted"), 8, 56, 10, LIGHTGRAY);
    DrawText(TextFormat("Flashlight: %s (F)", flashlightOn ? "on" : "off"), 8, 68, 10, LIGHTGRAY);
    DrawText(TextFormat("Dither: %s (V)", ditherOn ? "on" : "off"), 8, 80, 10, LIGHTGRAY);
    DrawText(TextFormat("Checksum: %s (C)", lan->useChecksum ? "on" : "off"), 8, 92, 10, LIGHTGRAY);

    const char *modeName = (mode == MODE_ZOMBIES) ? "Zombies" : (mpVariant == MULTI_TEAM ? "Multiplayer (Teams)" : "Multiplayer (FFA)");
    DrawText(TextFormat("Mode: %s", modeName), 8, 106, 10, LIGHTGRAY);
    DrawText(TextFormat("Arena: %s  (< > swap, P save)", arenaName), 8, 118, 10, LIGHTGRAY);
    DrawText(TextFormat("Score: %d   Cash: %d", player->score, player->cash), 8, 130, 10, LIGHTGRAY);
    DrawText(TextFormat("Weapon: %s [%d]", weapon->name, ammo), 8, 142, 10, weapon->color);
    DrawText(TextFormat("Health: %.0f", player->health), 8, 154, 10, player->health > 35 ? LIGHTGRAY : RED);
    if (player->isDowned)
        DrawText("Down! Hold E near a peer to revive", 8, 166, 10, RED);

    if (mode == MODE_MULTIPLAYER)
    {
        DrawText(TextFormat("Frags: %d  Deaths: %d", frags, deaths), 8, 178, 10, LIGHTGRAY);
        if (mpVariant == MULTI_TEAM)
        {
            const char *teamName = playerTeam == 0 ? "Blue" : "Gold";
            DrawText(TextFormat("Team: %s | Score %d - %d  (H swap)", teamName, teamScores[0], teamScores[1]), 8, 190, 10, SKYBLUE);
        }
    }

    if (sharePipTimer > 0.0f)
    {
        int y = (mode == MODE_MULTIPLAYER) ? 204 : 178;
        DrawText(TextFormat("Shared %+d | %+d", sharePipCash, sharePipScore), 8, y, 10, SKYBLUE);
    }
    if (assistFlash > 0.0f)
    {
        int y = (mode == MODE_MULTIPLAYER) ? 216 : 190;
        DrawText("Melee weaken active", 8, y, 10, ORANGE);
    }

    int perkY = player->isDowned ? 202 : (mode == MODE_MULTIPLAYER ? 232 : 178);
    if (quickfire)
    {
        DrawText("Perk: Quickfire", 8, perkY, 10, ORANGE);
        perkY += 12;
    }
    if (speed)
    {
        DrawText("Perk: Sprint", 8, perkY, 10, SKYBLUE);
        perkY += 12;
    }
    if (revive)
    {
        DrawText("Perk: Revive", 8, perkY, 10, LIME);
        perkY += 12;
    }

    if (mode == MODE_ZOMBIES)
    {
        DrawText(TextFormat("Wave %d", zombies->wave), 8, perkY + 6, 10, LIGHTGRAY);
        DrawText(TextFormat("Active: %d", zombies->activeCount), 8, perkY + 18, 10, LIGHTGRAY);
        DrawText("E: perk (blue), wall ammo (red), box (gold)", 8, perkY + 32, 9, LIGHTGRAY);
        DrawText("Speed perk: teal, Revive: lime", 8, perkY + 44, 9, LIGHTGRAY);
        DrawText("Cooldowns:", 8, perkY + 58, 9, LIGHTGRAY);
        DrawText("Fire", 8, perkY + 70, 8, LIGHTGRAY);
        DrawCooldownBar(32, perkY + 70, fireCooldown);
        DrawText("Mystery", 8, perkY + 82, 8, LIGHTGRAY);
        DrawCooldownBar(48, perkY + 82, mysteryCooldown / 5.0f);
        DrawText("Damage", 8, perkY + 94, 8, LIGHTGRAY);
        DrawCooldownBar(44, perkY + 94, damageCooldown);
    }

    if (hitMarker && hitMarker->timer > 0.0f)
    {
        float alpha = Clamp(hitMarker->timer / 0.3f, 0.0f, 1.0f);
        Color tint = hitMarker->isKill ? RED : WHITE;
        tint.a = (unsigned char)(alpha * 200);
        int cx = BASE_WIDTH / 2;
        int cy = BASE_HEIGHT / 2;
        DrawLine(cx - 4, cy - 4, cx + 4, cy + 4, tint);
        DrawLine(cx - 4, cy + 4, cx + 4, cy - 4, tint);
    }

    if (killfeed && killfeedCount > 0)
    {
        int y = 12;
        for (int i = 0; i < killfeedCount; i++)
        {
            if (killfeed[i].timer <= 0.0f)
                continue;
            Color tint = killfeed[i].color;
            DrawText(killfeed[i].text, BASE_WIDTH - 132, y, 9, tint);
            y += 12;
        }
    }

    DrawText("Peers:", 8, BASE_HEIGHT - 48, 9, LIGHTGRAY);
    int peerLine = BASE_HEIGHT - 36;
        for (int i = 0; i < MAX_PEERS; i++)
        {
            if (!lan->peers[i].active)
                continue;
            const char *name = lan->peers[i].name[0] ? lan->peers[i].name : "Peer";
            const char *status = lan->peers[i].isDowned ? "DOWN" : (lan->peers[i].isReviving ? "REV" : "OK");
            const char *teamTag = (mode == MODE_MULTIPLAYER && lan->peers[i].teamMode) ? (lan->peers[i].team == 0 ? "B" : "G") : "-";
            DrawText(TextFormat("%s: %s H%.0f $%d S%d W%d A%d T%s",
                               name,
                               status,
                               lan->peers[i].health,
                               lan->peers[i].cash,
                               lan->peers[i].score,
                               lan->peers[i].weaponIndex + 1,
                               lan->peers[i].ammo,
                               teamTag),
                     8,
                     peerLine,
                     9,
                 LIGHTGRAY);
        peerLine += 10;
        DrawText(TextFormat("perks: %s%s%s", lan->peers[i].perkQuickfire ? "Q" : "-", lan->peers[i].perkSpeed ? "S" : "-", lan->peers[i].perkRevive ? "R" : "-"), 12, peerLine, 8, DARKGRAY);
        peerLine += 10;
    }
}

int main(int argc, char **argv)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(BASE_WIDTH * PIXEL_SCALE, BASE_HEIGHT * PIXEL_SCALE, "U8 FPS Prototype");
    InitAudioDevice();
    SetTargetFPS(60);
    DisableCursor();

    Camera3D camera = {
        .position = {0.0f, 1.0f, 3.0f},
        .target = {0.0f, 1.0f, 2.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 70.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    camera.position = SelectSafeSpawn(&gArenaPresets[0]);
    camera.target = Vector3Add(camera.position, (Vector3){0.0f, 0.0f, -1.0f});

    Weapon weapons[] = {
        {.name = "Pistol", .damage = 18.0f, .fireRate = 3.5f, .recoil = 0.012f, .spread = 0.01f, .range = 40.0f, .color = ORANGE, .maxAmmo = 64},
        {.name = "SMG", .damage = 12.0f, .fireRate = 9.0f, .recoil = 0.008f, .spread = 0.018f, .range = 35.0f, .color = SKYBLUE, .maxAmmo = 160},
        {.name = "Rifle", .damage = 24.0f, .fireRate = 6.0f, .recoil = 0.02f, .spread = 0.012f, .range = 50.0f, .color = LIME, .maxAmmo = 120},
        {.name = "Shotgun", .damage = 55.0f, .fireRate = 1.1f, .recoil = 0.06f, .spread = 0.04f, .range = 20.0f, .color = YELLOW, .maxAmmo = 48},
        {.name = "LMG", .damage = 16.0f, .fireRate = 7.0f, .recoil = 0.03f, .spread = 0.02f, .range = 45.0f, .color = RED, .maxAmmo = 220},
    };

    int weaponIndex = 0;
    float fireCooldown = 0.0f;
    float recoilKick = 0.0f;

    GameMode mode = MODE_MULTIPLAYER;
    MultiplayerVariant mpVariant = MULTI_FFA;
    int playerTeam = 0;
    if (argc > 1 && strcmp(argv[1], "--zombies") == 0)
    {
        mode = MODE_ZOMBIES;
    }
    else if (argc > 1 && strcmp(argv[1], "--team") == 0)
    {
        mode = MODE_MULTIPLAYER;
        mpVariant = MULTI_TEAM;
    }

    ZombiesState zombies;
    ResetZombies(&zombies);

    PlayerState player;
    ResetPlayer(&player);

    int fragCount = 0;
    int deathCount = 0;
    int teamScores[2] = {0};

    char playerName[MAX_NAME_LEN] = "Player";
    int playerNameLen = 6;
    bool nameLocked = false;
    bool inMenu = true;

    LanState lan;
    InitLan(&lan);

    RenderTexture2D renderTarget = LoadRenderTexture(BASE_WIDTH, BASE_HEIGHT);
    Image flashImg = GenImageColor(1, 1, WHITE);
    Texture2D flashTex = LoadTextureFromImage(flashImg);
    UnloadImage(flashImg);
    Decal decals[MAX_DECALS] = {0};
    int decalIndex = 0;
    DissolveFX dissolves[MAX_DISSOLVES] = {0};
    int dissolveIndex = 0;
    TrailFX trails[MAX_TRAILS] = {0};
    int trailIndex = 0;
    Flash flash = {0};
    HitMarker hitMarker = {0};
    KillfeedEntry killfeed[5] = {0};
    const int killfeedCount = (int)(sizeof(killfeed) / sizeof(killfeed[0]));
    float playerRespawnTimer = 0.0f;
    DamageEvent pendingRay = {0};
    uint8_t damageCounter = 1;
    uint8_t eventCounter = 1;
    LanEvent pendingEvent = {0};
    Sound hitSound = MakeTone(220.0f, 0.08f, 0.35f);
    Sound killSound = MakeTone(520.0f, 0.12f, 0.32f);
    Sound feedSound = MakeTone(380.0f, 0.16f, 0.22f);
    Sound perkSound = MakeTone(540.0f, 0.1f, 0.25f);
    Sound boxSound = MakeTone(360.0f, 0.12f, 0.28f);
    Sound reviveSound = MakeTone(720.0f, 0.1f, 0.3f);
    Sound downSound = MakeTone(140.0f, 0.2f, 0.35f);

    Vector2 viewAngles = {
        atan2f(camera.target.x - camera.position.x, camera.target.z - camera.position.z),
        asinf((camera.target.y - camera.position.y) /
              Vector3Length(Vector3Subtract(camera.target, camera.position)))
    };

    bool quickfirePerk = false;
    bool speedPerk = false;
    bool revivePerk = false;
    bool wallBuyed = false;
    bool flashlightOn = true;
    bool ditherOn = false;
    float mysteryCooldown = 0.0f;
    float mysteryRollTimer = 0.0f;
    int mysteryRollsLeft = 0;
    int pendingCashShare = 0;
    int pendingScoreShare = 0;
    float meleeCooldown = 0.0f;
    float assistFlash = 0.0f;
    int weaponAmmo[sizeof(weapons) / sizeof(weapons[0])];
    for (int i = 0; i < (int)(sizeof(weapons) / sizeof(weapons[0])); i++)
        weaponAmmo[i] = weapons[i].maxAmmo;
    int arenaIndex = 0;
    PropSpot propSpots[MAX_PROP_SPOTS];
    int propSpotCount = gArenaPresets[arenaIndex].spotCount;
    memcpy(propSpots, gArenaPresets[arenaIndex].spots, sizeof(PropSpot) * propSpotCount);
    LoadPresetOverride(gArenaPresets[arenaIndex].name, propSpots, &propSpotCount);
    float peerReviveTimers[MAX_PEERS] = {0};
    float sharePipTimer = 0.0f;
    int sharePipCash = 0;
    int sharePipScore = 0;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        if (player.damageCooldown > 0.0f)
            player.damageCooldown -= dt;
        if (player.damageCooldown < 0.0f)
            player.damageCooldown = 0.0f;
        if (sharePipTimer > 0.0f)
            sharePipTimer -= dt;
        if (pendingRay.ttl > 0.0f)
            pendingRay.ttl -= dt;
        if (hitMarker.timer > 0.0f)
            hitMarker.timer -= dt;
        for (int i = 0; i < killfeedCount; i++)
            if (killfeed[i].timer > 0.0f)
                killfeed[i].timer -= dt;

        for (int i = 0; i < MAX_PEERS; i++)
        {
            if (!lan.peers[i].active)
                continue;
            if (!lan.peers[i].teamMode && lan.peers[i].addr.sin_addr.s_addr != 0)
            {
                unsigned int addr = ntohl(lan.peers[i].addr.sin_addr.s_addr);
                lan.peers[i].team = (addr & 0xFFu) % 2;
            }
            if (lan.peers[i].respawnTimer > 0.0f)
            {
                lan.peers[i].respawnTimer -= dt;
                if (lan.peers[i].respawnTimer <= 0.0f)
                {
                    lan.peers[i].respawnTimer = 0.0f;
                    lan.peers[i].health = PLAYER_MAX_HEALTH;
                    lan.peers[i].renderPos = SelectSafeSpawn(&gArenaPresets[arenaIndex]);
                }
            }
        }

        int key = GetCharPressed();
        while (key > 0)
        {
            if (!nameLocked && playerNameLen < MAX_NAME_LEN - 1 && key >= 32 && key <= 125)
            {
                playerName[playerNameLen++] = (char)key;
                playerName[playerNameLen] = '\0';
            }
            key = GetCharPressed();
        }
        if (!nameLocked && IsKeyPressed(KEY_BACKSPACE) && playerNameLen > 0)
        {
            playerName[--playerNameLen] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER))
        {
            nameLocked = !nameLocked;
        }
        if (IsKeyPressed(KEY_M))
        {
            gAudioEnabled = !gAudioEnabled;
        }
        if (IsKeyPressed(KEY_C))
        {
            lan.useChecksum = !lan.useChecksum;
        }
        if (IsKeyPressed(KEY_F))
        {
            flashlightOn = !flashlightOn;
        }
        if (IsKeyPressed(KEY_V))
        {
            ditherOn = !ditherOn;
        }

        if (inMenu)
        {
            typedef struct MenuButton
            {
                MenuAction action;
                Rectangle rect;
                char label[96];
            } MenuButton;

            MenuButton buttons[12];
            int buttonCount = 0;
            float y = 76.0f;
            float x = 32.0f;
            float w = 260.0f;
            float h = 28.0f;

            buttons[buttonCount].action = MENU_ACTION_NAME;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Name: %s (%s)",
                     playerName,
                     nameLocked ? "locked" : "edit");
            buttonCount++;
            y += h + 6.0f;

            buttons[buttonCount].action = MENU_ACTION_AUDIO;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Audio: %s", gAudioEnabled ? "on" : "muted");
            buttonCount++;
            y += h + 6.0f;

            buttons[buttonCount].action = MENU_ACTION_CHECKSUM;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Checksum: %s", lan.useChecksum ? "enabled" : "off");
            buttonCount++;
            y += h + 6.0f;

            buttons[buttonCount].action = MENU_ACTION_MODE;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Mode: %s",
                     mode == MODE_ZOMBIES ? "Zombies" : "Multiplayer");
            buttonCount++;
            y += h + 6.0f;

            if (mode == MODE_MULTIPLAYER)
            {
                buttons[buttonCount].action = MENU_ACTION_VARIANT;
                buttons[buttonCount].rect = (Rectangle){x, y, w, h};
                snprintf(buttons[buttonCount].label,
                         sizeof(buttons[buttonCount].label),
                         "Variant: %s",
                         mpVariant == MULTI_TEAM ? "Team Deathmatch" : "Free-for-all");
                buttonCount++;
                y += h + 6.0f;

                if (mpVariant == MULTI_TEAM)
                {
                    buttons[buttonCount].action = MENU_ACTION_TEAM;
                    buttons[buttonCount].rect = (Rectangle){x, y, w, h};
                    snprintf(buttons[buttonCount].label,
                             sizeof(buttons[buttonCount].label),
                             "Team: %s",
                             playerTeam == 0 ? "Blue" : "Gold");
                    buttonCount++;
                    y += h + 6.0f;
                }
            }

            buttons[buttonCount].action = MENU_ACTION_ARENA;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Arena: %s (\xE2\x86\x90/\xE2\x86\x92)",
                     gArenaPresets[arenaIndex].name);
            buttonCount++;
            y += h + 6.0f;

            buttons[buttonCount].action = MENU_ACTION_SAVE;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Save layout for %s", gArenaPresets[arenaIndex].name);
            buttonCount++;
            y += h + 6.0f;

            buttons[buttonCount].action = MENU_ACTION_FLASHLIGHT;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Flashlight: %s", flashlightOn ? "on" : "off");
            buttonCount++;
            y += h + 6.0f;

            buttons[buttonCount].action = MENU_ACTION_DITHER;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Dither: %s", ditherOn ? "on" : "off");
            buttonCount++;
            y += h + 10.0f;

            buttons[buttonCount].action = MENU_ACTION_SPAWN;
            buttons[buttonCount].rect = (Rectangle){x, y, w, h + 6.0f};
            snprintf(buttons[buttonCount].label,
                     sizeof(buttons[buttonCount].label),
                     "Start in %s",
                     mode == MODE_ZOMBIES
                         ? "Zombies"
                         : (mpVariant == MULTI_TEAM ? "Team Deathmatch" : "Free-for-all"));
            buttonCount++;

            static int menuSelection = 0;
            if (menuSelection >= buttonCount)
                menuSelection = buttonCount - 1;
            if (menuSelection < 0)
                menuSelection = 0;

            if (IsKeyPressed(KEY_DOWN))
            {
                menuSelection = (menuSelection + 1) % buttonCount;
            }
            if (IsKeyPressed(KEY_UP))
            {
                menuSelection = (menuSelection - 1 + buttonCount) % buttonCount;
            }

            bool left = IsKeyPressed(KEY_LEFT);
            bool right = IsKeyPressed(KEY_RIGHT);
            bool activate = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);

            switch (buttons[menuSelection].action)
            {
            case MENU_ACTION_NAME:
                if (activate)
                    nameLocked = !nameLocked;
                break;
            case MENU_ACTION_AUDIO:
                if (activate || left || right)
                    gAudioEnabled = !gAudioEnabled;
                break;
            case MENU_ACTION_CHECKSUM:
                if (activate || left || right)
                    lan.useChecksum = !lan.useChecksum;
                break;
            case MENU_ACTION_MODE:
                if (activate || left || right)
                {
                    mode = (mode == MODE_MULTIPLAYER) ? MODE_ZOMBIES : MODE_MULTIPLAYER;
                    ResetPlayer(&player);
                    ResetZombies(&zombies);
                    fragCount = 0;
                    deathCount = 0;
                    teamScores[0] = teamScores[1] = 0;
                }
                break;
            case MENU_ACTION_VARIANT:
                if (mode == MODE_MULTIPLAYER && (activate || left || right))
                {
                    mpVariant = (mpVariant == MULTI_FFA) ? MULTI_TEAM : MULTI_FFA;
                    fragCount = 0;
                    deathCount = 0;
                    teamScores[0] = teamScores[1] = 0;
                }
                break;
            case MENU_ACTION_TEAM:
                if (mpVariant == MULTI_TEAM && (activate || left || right))
                    playerTeam = 1 - playerTeam;
                break;
            case MENU_ACTION_ARENA:
                if (left)
                {
                    arenaIndex = (arenaIndex - 1 + MAX_ARENAS) % MAX_ARENAS;
                }
                else if (right || activate)
                {
                    arenaIndex = (arenaIndex + 1) % MAX_ARENAS;
                }
                if (left || right || activate)
                {
                    propSpotCount = gArenaPresets[arenaIndex].spotCount;
                    memcpy(propSpots, gArenaPresets[arenaIndex].spots, sizeof(PropSpot) * propSpotCount);
                    LoadPresetOverride(gArenaPresets[arenaIndex].name, propSpots, &propSpotCount);
                    camera.position = SelectSafeSpawn(&gArenaPresets[arenaIndex]);
                }
                break;
            case MENU_ACTION_SAVE:
                if (activate)
                    SavePreset(gArenaPresets[arenaIndex].name, propSpots, propSpotCount);
                break;
            case MENU_ACTION_FLASHLIGHT:
                if (activate || left || right)
                    flashlightOn = !flashlightOn;
                break;
            case MENU_ACTION_DITHER:
                if (activate || left || right)
                    ditherOn = !ditherOn;
                break;
            case MENU_ACTION_SPAWN:
                if (activate)
                {
                    inMenu = false;
                    nameLocked = true;
                    ResetPlayer(&player);
                    ResetZombies(&zombies);
                    fragCount = 0;
                    deathCount = 0;
                    teamScores[0] = teamScores[1] = 0;
                    for (int i = 0; i < (int)(sizeof(weaponAmmo) / sizeof(weaponAmmo[0])); i++)
                        weaponAmmo[i] = weapons[i].maxAmmo;
                    camera.position = SelectSafeSpawn(&gArenaPresets[arenaIndex]);
                    camera.target = Vector3Add(camera.position, (Vector3){0.0f, 0.0f, -1.0f});
                }
                break;
            }

            BeginDrawing();
            ClearBackground((Color){10, 12, 20, 255});
            DrawText("U8 FPS prototype", 32, 24, 18, LIGHTGRAY);
            DrawText("Main Menu", 32, 44, 14, LIGHTGRAY);
            DrawText("Use arrow keys to move, Enter/Space to confirm", 32, 60, 10, LIGHTGRAY);

            for (int i = 0; i < buttonCount; i++)
            {
                DrawMenuButton(buttons[i].rect, buttons[i].label, i == menuSelection);
            }

            DrawText("After spawning: WASD/mouse to move, Q swaps weapons.", 32, 260, 10, LIGHTGRAY);
            DrawText("Zombies: E uses perks/box/wall, hold E near peers to revive.", 32, 274, 10, LIGHTGRAY);
            DrawText("Multiplayer: frag for score; in teams use the Team button to swap.", 32, 288, 10, LIGHTGRAY);
            EndDrawing();
            continue;
        }

        Vector2 peerLabels[MAX_PEERS] = {0};
        bool peerLabelVisible[MAX_PEERS] = {0};
        char peerLabelText[MAX_PEERS][48] = {0};

        if (mode == MODE_MULTIPLAYER && playerRespawnTimer > 0.0f)
        {
            Vector3 safeSpawn = SelectSafeSpawn(&gArenaPresets[arenaIndex]);
            double orbit = now;
            Vector3 drift = {safeSpawn.x + sinf((float)orbit * 0.9f) * 0.8f,
                              safeSpawn.y + 0.3f + sinf((float)orbit * 1.3f) * 0.08f,
                              safeSpawn.z + cosf((float)orbit * 0.9f) * 0.8f};
            camera.position = Vector3Lerp(camera.position, drift, Clamp(dt * 2.2f, 0.0f, 1.0f));
            camera.target = Vector3Lerp(camera.target, safeSpawn, Clamp(dt * 3.0f, 0.0f, 1.0f));
            playerRespawnTimer -= dt;
            if (playerRespawnTimer <= 0.0f)
            {
                player.health = PLAYER_MAX_HEALTH;
                for (int i = 0; i < (int)(sizeof(weaponAmmo) / sizeof(weaponAmmo[0])); i++)
                    weaponAmmo[i] = weapons[i].maxAmmo;
                camera.position = safeSpawn;
                camera.target = Vector3Add(camera.position, (Vector3){0.0f, 0.0f, -1.0f});
                playerRespawnTimer = 0.0f;
            }
        }

        bool canAct = !player.isDowned && playerRespawnTimer <= 0.0f;
        float moveScale = 1.0f;
        if (speedPerk)
            moveScale += 0.35f;
        if (player.isDowned)
            moveScale = 0.35f;

        Vector3 playerFoot = {camera.position.x, 0.0f, camera.position.z};
        bool wasDown = player.isDowned;
        bool isZombies = (mode == MODE_ZOMBIES);

        UpdateCameraLean(&camera, &viewAngles, dt, recoilKick, moveScale, canAct);
        recoilKick = Lerp(recoilKick, 0.0f, dt * 8.0f);
        if (flash.timer > 0.0f)
            flash.timer -= dt;

        fireCooldown -= dt;
        if (fireCooldown < 0.0f)
            fireCooldown = 0.0f;
        if (meleeCooldown > 0.0f)
            meleeCooldown -= dt;
        if (assistFlash > 0.0f)
            assistFlash -= dt;
        if (IsKeyPressed(KEY_Q) && canAct)
        {
            weaponIndex = (weaponIndex + 1) % (int)(sizeof(weapons) / sizeof(weapons[0]));
        }

        double now = GetTime();
        int currentAmmo = weaponAmmo[weaponIndex];
        UpdateLan(&lan,
                  dt,
                  camera.position,
                  weaponIndex,
                  currentAmmo,
                  &player,
                  quickfirePerk,
                  speedPerk,
                  revivePerk,
                  mpVariant,
                  playerTeam,
                  playerName,
                  now,
                  &pendingCashShare,
                  &pendingScoreShare,
                  &sharePipTimer,
                  &sharePipCash,
                  &sharePipScore,
                  &pendingRay,
                  mode == MODE_MULTIPLAYER,
                  &pendingEvent,
                  &eventCounter);

        if (lan.hasIncomingEvent)
        {
            LanEvent evt = lan.incomingEvent;
            lan.hasIncomingEvent = false;
            char actor[LAN_NAME_BYTES + 4] = {0};
            char target[LAN_NAME_BYTES + 4] = {0};
            strncpy(actor, evt.actor[0] ? evt.actor : "Peer", sizeof(actor) - 1);
            strncpy(target, evt.target[0] ? evt.target : "opponent", sizeof(target) - 1);
            if (evt.kind == 1)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s fragged %s", actor, target);
                PushKillfeedSfx(killfeed, killfeedCount, buf, ORANGE, feedSound);
                if (mpVariant == MULTI_TEAM && evt.team >= 0 && evt.team < 2)
                    teamScores[evt.team]++;
            }
            else if (evt.kind == 2)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s assisted %s", actor, target);
                PushKillfeedSfx(killfeed, killfeedCount, buf, SKYBLUE, feedSound);
            }
        }

        if (mode == MODE_MULTIPLAYER && player.health <= 0.0f && playerRespawnTimer <= 0.0f)
        {
            playerRespawnTimer = 2.5f;
            player.health = 0.0f;
            deathCount++;
            PushKillfeedSfx(killfeed, killfeedCount, "You were fragged", RED, feedSound);
        }

        if (isZombies)
        {
            UpdateZombies(&zombies,
                          dt,
                          (Vector3){camera.position.x, 0.0f, camera.position.z},
                          &player,
                          trails,
                          &trailIndex,
                          gArenaPresets[arenaIndex].navPoints,
                          gArenaPresets[arenaIndex].navWeights,
                          gArenaPresets[arenaIndex].navCount);
            if (player.health <= 0.0f)
            {
                player.isDowned = true;
                player.health = 0.0f;
                if (!wasDown)
                {
                    PlaySoundSafe(downSound);
                    deathCount++;
                }
            }

            if (player.isDowned)
            {
                float reviveSpeed = revivePerk ? 1.5f : 0.8f;
                bool peerNearby = false;
                for (int i = 0; i < MAX_PEERS; i++)
                {
                    if (!lan.peers[i].active)
                        continue;
                    if (Vector3Distance(playerFoot, lan.peers[i].renderPos) < 1.6f)
                    {
                        peerNearby = true;
                        break;
                    }
                }

                if (peerNearby && IsKeyDown(KEY_E))
                {
                    player.reviveProgress += dt * reviveSpeed;
                    if (player.reviveProgress >= 1.0f)
                    {
                        player.isDowned = false;
                        player.health = PLAYER_MAX_HEALTH * 0.6f;
                        player.reviveProgress = 0.0f;
                        player.damageCooldown = 1.0f;
                        PlaySoundSafe(reviveSound);
                        pendingCashShare += 25;
                        pendingScoreShare += 30;
                    }
                }
                else
                {
                    player.reviveProgress = 0.0f;
                }
            }
            else if (player.health < PLAYER_MAX_HEALTH)
            {
                player.health = Clamp(player.health + dt * 3.0f, 0.0f, PLAYER_MAX_HEALTH);
            }

            for (int i = 0; i < MAX_PEERS; i++)
            {
                if (!lan.peers[i].active)
                    continue;
                if (!lan.peers[i].isDowned)
                {
                    if (peerReviveTimers[i] < 0.0f)
                        peerReviveTimers[i] = 0.0f;
                    continue;
                }

                float dist = Vector3Distance(playerFoot, lan.peers[i].renderPos);
                if (dist < 1.6f && IsKeyDown(KEY_E) && canAct)
                {
                    float assistSpeed = revivePerk ? 1.5f : 1.0f;
                    peerReviveTimers[i] += dt * assistSpeed;
                    if (peerReviveTimers[i] >= 1.0f)
                    {
                        pendingCashShare += 40;
                        pendingScoreShare += 60;
                        peerReviveTimers[i] = -2.0f;
                    }
                }
                else if (peerReviveTimers[i] > 0.0f)
                {
                    peerReviveTimers[i] = Clamp(peerReviveTimers[i] - dt * 0.5f, 0.0f, 1.0f);
                }
            }

            if (mysteryCooldown > 0.0f)
                mysteryCooldown -= dt;

            if (mysteryRollsLeft > 0)
            {
                mysteryRollTimer -= dt;
                if (mysteryRollTimer <= 0.0f)
                {
                    weaponIndex = GetRandomValue(0, (int)(sizeof(weapons) / sizeof(weapons[0])) - 1);
                    mysteryRollsLeft--;
                    mysteryRollTimer = 0.5f;
                    if (mysteryRollsLeft == 0)
                    {
                        mysteryCooldown = 5.0f;
                        weaponAmmo[weaponIndex] = weapons[weaponIndex].maxAmmo;
                    }
                }
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && meleeCooldown <= 0.0f && canAct)
            {
                Vector3 dir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                int assistCash = 0;
                int tagged = MeleeAssist(camera.position, dir, &zombies, &assistCash, &assistFlash);
                if (tagged > 0)
                {
                    meleeCooldown = 0.45f;
                    pendingCashShare += assistCash;
                    sharePipTimer = 1.2f;
                    sharePipCash = assistCash;
                    sharePipScore = assistCash;
                }
            }

            if (IsKeyPressed(KEY_E))
            {
                for (int i = 0; i < propSpotCount; i++)
                {
                    float dist = Vector3Distance(playerFoot, propSpots[i].position);
                    if (dist > 1.25f)
                        continue;
                    int cost = PropCost(propSpots[i].kind);
                    if (player.cash < cost)
                        continue;
                    switch (propSpots[i].kind)
                    {
                    case PROP_PERK_QUICK:
                        quickfirePerk = true;
                        player.cash -= cost;
                        PlaySoundSafe(perkSound);
                        break;
                    case PROP_PERK_SPEED:
                        speedPerk = true;
                        player.cash -= cost;
                        PlaySoundSafe(perkSound);
                        break;
                    case PROP_PERK_REVIVE:
                        revivePerk = true;
                        player.cash -= cost;
                        PlaySoundSafe(perkSound);
                        break;
                    case PROP_WALL_AMMO:
                        wallBuyed = true;
                        player.cash -= cost;
                        weaponAmmo[weaponIndex] = weapons[weaponIndex].maxAmmo;
                        PlaySoundSafe(perkSound);
                        break;
                    case PROP_MYSTERY:
                        if (mysteryCooldown <= 0.0f && mysteryRollsLeft == 0)
                        {
                            player.cash -= cost;
                            mysteryRollsLeft = 3;
                            mysteryRollTimer = 0.2f;
                            PlaySoundSafe(boxSound);
                        }
                        break;
                    }
                }
            }
        }
        else
        {
            player.isDowned = false;
            if (playerRespawnTimer <= 0.0f && player.health < PLAYER_MAX_HEALTH)
                player.health = Clamp(player.health + dt * 8.0f, 0.0f, PLAYER_MAX_HEALTH);
        }

        Weapon current = weapons[weaponIndex];
        if (quickfirePerk)
        {
            current.fireRate *= 1.25f;
            current.recoil *= 0.85f;
        }
        if (wallBuyed)
        {
            current.damage *= 1.15f;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && fireCooldown <= 0.0f && canAct)
        {
            if (weaponAmmo[weaponIndex] > 0)
            {
                Vector3 dir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                Vector3 jitter = {
                    ((float)GetRandomValue(-100, 100) / 100.0f) * current.spread,
                    ((float)GetRandomValue(-100, 100) / 100.0f) * current.spread,
                    ((float)GetRandomValue(-100, 100) / 100.0f) * current.spread};
                dir = Vector3Normalize(Vector3Add(dir, jitter));

                fireCooldown = 1.0f / current.fireRate;
                recoilKick += current.recoil;
                flash.timer = MAX_FLASH_TIME;
                flash.color = current.color;
                int kills = 0;
                int cashEarned = 0;
                int assistShare = 0;
                int peerFragged = -1;
                int hits = 0;
                if (isZombies)
                {
                    hits = FireWeapon(&current,
                                      camera.position,
                                      dir,
                                      &zombies,
                                      decals,
                                      &decalIndex,
                                      dissolves,
                                      &dissolveIndex,
                                      &kills,
                                      &cashEarned,
                                      &assistShare);
                }
                else
                {
                    hits = FireAtPeers(&current, camera.position, dir, &lan, mpVariant == MULTI_TEAM, playerTeam, &peerFragged);
                }
                if (hits > 0)
                {
                    if (!isZombies)
                    {
                        pendingRay.origin = camera.position;
                        pendingRay.dir = dir;
                        pendingRay.damage = current.damage;
                        pendingRay.ttl = 0.3f;
                        pendingRay.id = damageCounter++;
                    }
                    PlaySoundSafe(hitSound);
                    hitMarker.timer = 0.3f;
                    hitMarker.isKill = (!isZombies && peerFragged >= 0) || (isZombies && kills > 0);
                    if (hitMarker.isKill)
                        PlaySoundSafe(killSound);
                }
                weaponAmmo[weaponIndex]--;
                if (isZombies)
                {
                    player.score += kills * 120;
                    player.cash += cashEarned;
                    pendingCashShare += cashEarned / 4;
                    pendingCashShare += assistShare / 4;
                    if (assistShare > 0)
                    {
                        sharePipTimer = 1.4f;
                        sharePipCash = assistShare / 2;
                        sharePipScore = assistShare / 2;
                    }
                    pendingScoreShare += kills * 40;
                }
                else if (peerFragged >= 0)
                {
                    fragCount++;
                    if (mpVariant == MULTI_TEAM)
                        teamScores[playerTeam]++;
                    player.score += 100;
                    const char *fragName = lan.peers[peerFragged].name[0] ? lan.peers[peerFragged].name : "Peer";
                    char buf[48];
                    snprintf(buf, sizeof(buf), "Fragged %s", fragName);
                    PushKillfeedSfx(killfeed, killfeedCount, buf, ORANGE, feedSound);
                    pendingEvent.kind = 1;
                    pendingEvent.team = playerTeam;
                    strncpy(pendingEvent.target, fragName, LAN_NAME_BYTES - 1);
                }
                else if (hits > 0 && !isZombies && pendingEvent.kind == 0)
                {
                    pendingEvent.kind = 2;
                    pendingEvent.team = playerTeam;
                    strncpy(pendingEvent.target, "assist", LAN_NAME_BYTES - 1);
                }
            }
            else
            {
                fireCooldown = 0.2f;
            }
        }

        BeginTextureMode(renderTarget);
        ClearBackground((Color){15, 20, 30, 255});
        BeginMode3D(camera);

        DrawPlane((Vector3){0, 0, 0}, (Vector2){20, 20}, (Color){25, 30, 40, 255});
        DrawRetroCube((Vector3){0, 0.5f, 0}, 0.5f, 1.0f, 0.5f, GREEN);
        DrawRetroCube((Vector3){2.0f, 0.35f, 1.5f}, 0.35f, 0.7f, 0.35f, (Color){90, 100, 160, 255});
        DrawRetroCube((Vector3){-1.5f, 0.25f, -1.0f}, 0.25f, 0.5f, 0.25f, (Color){120, 80, 90, 255});
        DrawRetroCube((Vector3){-4.0f, 0.4f, 1.5f}, 0.9f, 1.2f, 0.6f, (Color){80, 110, 160, 255});
        DrawRetroCube((Vector3){4.0f, 0.35f, -1.5f}, 0.8f, 1.0f, 0.8f, (Color){150, 120, 90, 255});
        DrawRetroCube((Vector3){0.0f, 0.25f, 3.5f}, 1.2f, 0.6f, 1.2f, (Color){60, 80, 110, 255});
        for (int i = 0; i < gArenaPresets[arenaIndex].coverCount; i++)
        {
            CoverPiece c = gArenaPresets[arenaIndex].cover[i];
            DrawRetroCube(c.position, c.size.x, c.size.y, c.size.z, c.color);
        }
        for (int i = 0; i < propSpotCount; i++)
        {
            Vector3 snapped = propSpots[i].position;
            snapped = QuantizeVec3(snapped, 0.1f);
            float h = (propSpots[i].kind == PROP_MYSTERY) ? 0.8f : 1.1f;
            float s = (propSpots[i].kind == PROP_MYSTERY) ? 0.45f : 0.55f;
            DrawRetroCube(snapped, s, h, s, PropColor(propSpots[i].kind));
        }

        if (isZombies)
        {
            DrawZombies(&zombies);
            DrawDecals(decals, dt);
            UpdateDissolves(dissolves, dt);
            UpdateTrails(trails, dt);
        }
        DrawMuzzleFlash(&flash, &camera, flashTex);
        for (int i = 0; i < MAX_PEERS; i++)
        {
            if (!lan.peers[i].active)
                continue;
            DrawRetroCube(lan.peers[i].renderPos, 0.25f, 0.6f, 0.25f, (Color){160, 160, 255, 255});
            Vector3 head = lan.peers[i].renderPos;
            head.y += 0.9f;
            Vector2 screenPos = GetWorldToScreen(head, camera);
            if (screenPos.x >= 0 && screenPos.x <= BASE_WIDTH && screenPos.y >= 0 && screenPos.y <= BASE_HEIGHT)
            {
                peerLabels[i] = screenPos;
                peerLabelVisible[i] = true;
                int wi = lan.peers[i].weaponIndex;
                const char *wName = (wi >= 0 && wi < (int)(sizeof(weapons) / sizeof(weapons[0]))) ? weapons[wi].name : "W?";
                const char *name = lan.peers[i].name[0] ? lan.peers[i].name : "Peer";
                const char *status = lan.peers[i].isDowned ? "!" : (lan.peers[i].isReviving ? "R" : "");
                snprintf(peerLabelText[i],
                         sizeof(peerLabelText[i]),
                         "%s [%s %d|H%.0f%s $%d]",
                         name,
                         wName,
                         lan.peers[i].ammo,
                         lan.peers[i].health,
                         status,
                         lan.peers[i].cash);
            }
        }
        EndMode3D();

        DrawCrosshair(BASE_WIDTH, BASE_HEIGHT);
        for (int i = 0; i < MAX_PEERS; i++)
        {
            if (!peerLabelVisible[i])
                continue;
            DrawText(peerLabelText[i], (int)peerLabels[i].x - 12, (int)peerLabels[i].y - 12, 8, SKYBLUE);
        }
        DrawInfo(dt,
                 mode,
                 &weapons[weaponIndex],
                 &zombies,
                 &player,
                 weaponAmmo[weaponIndex],
                 quickfirePerk,
                 speedPerk,
                 revivePerk,
                 &lan,
                 playerName,
                 nameLocked,
                 gAudioEnabled,
                 flashlightOn,
                 ditherOn,
                 fireCooldown,
                 mysteryCooldown,
                 player.damageCooldown,
                 gArenaPresets[arenaIndex].name,
                 sharePipTimer,
                 sharePipCash,
                 sharePipScore,
                 assistFlash,
                 mpVariant,
                 playerTeam,
                 fragCount,
                 deathCount,
                 teamScores,
                 &hitMarker,
                 killfeed,
                 killfeedCount);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle dest = {0, 0, BASE_WIDTH * PIXEL_SCALE, BASE_HEIGHT * PIXEL_SCALE};
        DrawTexturePro(renderTarget.texture,
                       (Rectangle){0, 0, renderTarget.texture.width, -renderTarget.texture.height},
                       dest,
                       (Vector2){0, 0},
                       0.0f,
                       WHITE);
        float healthPct = player.health / PLAYER_MAX_HEALTH;
        if (healthPct < 0.55f)
        {
            unsigned char alpha = (unsigned char)Clamp((int)((0.55f - healthPct) * 255), 0, 140);
            DrawRectangle(0, 0, (int)dest.width, (int)dest.height, (Color){60, 0, 0, alpha});
        }
        if (flashlightOn)
            DrawFlashlightMask((int)dest.width, (int)dest.height);
        if (ditherOn)
            DrawDitherMask((int)dest.width, (int)dest.height);
        EndDrawing();
    }

    EnableCursor();
    UnloadTexture(flashTex);
    UnloadRenderTexture(renderTarget);
    UnloadSound(hitSound);
    UnloadSound(perkSound);
    UnloadSound(boxSound);
    UnloadSound(reviveSound);
    UnloadSound(downSound);
    CloseAudioDevice();
    if (lan.enabled)
        close(lan.socketFd);
    CloseWindow();
    return 0;
}
