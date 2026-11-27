#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <math.h>
#include "assets_data.h"

#define SCREEN_W 640
#define SCREEN_H 448
#define VIEW_W   (SCREEN_W / 2)

#define SRC_TILE  8
#define SCALE     2
#define TILE_SZ   (SRC_TILE * SCALE)
#define META_SZ   (TILE_SZ * 4)
#define TEX_TILES_W 16
#define TEX_TILES_H 16

#define PLAYER_SIZE 16
#define MOVE_SPEED  4.0f
#define STICK_DEADZONE 40

static char pad_buf[2][256] __attribute__((aligned(64)));

typedef struct {
    int w, h;
    const uint8_t *blocks;
    const uint8_t *collision;
} GameMap;

typedef struct {
    int x, y;
} Camera;

typedef struct {
    float x, y;
    int tex_row;
    const char *dialog;
} NPC;

typedef struct {
    int   port, slot;
    float wx, wy;
    u16   last_btns;
    int   last_state;
    int   stick_moved;
    int   analog_center_lx, analog_center_ly;
    int   analog_calibrated;
    int   forced_mainmode;
    u64   color_ok;
    int   direction;
    int   anim_frame;
    int   walk_timer;
} Player;

static GSGLOBAL *gs;
static GSTEXTURE tile_tex;
static GSTEXTURE sprite_tex;

static NPC npc = { 320.0f, 320.0f, 2, "hola elisabet, te amo mucho preciosa." };
static int dialog_open = 0;
static int blink_timer = 0;
static int dialog_owner = -1;
static u16 prev_cross[2] = { 0, 0 };

static GameMap maps[4];
static int current_map_id = 0;
static GameMap *current_map = &maps[0];

static void parse_assets(void)
{
    // New Bark Town (ID 0)
    maps[0].w = newbark_map[4] | (newbark_map[5] << 8);
    maps[0].h = newbark_map[6] | (newbark_map[7] << 8);
    maps[0].blocks = &newbark_map[8];
    maps[0].collision = &johto_collision[6];

    // Route 29 (ID 1)
    maps[1].w = route29_map[4] | (route29_map[5] << 8);
    maps[1].h = route29_map[6] | (route29_map[7] << 8);
    maps[1].blocks = &route29_map[8];
    maps[1].collision = &johto_collision[6]; // same collision table

    // Cherrygrove City (ID 2)
    maps[2].w = cherrygrove_map[4] | (cherrygrove_map[5] << 8);
    maps[2].h = cherrygrove_map[6] | (cherrygrove_map[7] << 8);
    maps[2].blocks = &cherrygrove_map[8];
    maps[2].collision = &johto_collision[6];

    // Route 30 (ID 3)
    maps[3].w = route30_map[4] | (route30_map[5] << 8);
    maps[3].h = route30_map[6] | (route30_map[7] << 8);
    maps[3].blocks = &route30_map[8];
    maps[3].collision = &johto_collision[6];
}

static void upload_tileset(void)
{
    tile_tex.Width  = TEX_TILES_W * SRC_TILE;
    tile_tex.Height = TEX_TILES_H * SRC_TILE;
    tile_tex.PSM    = GS_PSM_CT32;
    tile_tex.Clut   = GS_CLUT_NONE;
    tile_tex.Filter = GS_FILTER_NEAREST; // do not touch this
    tile_tex.Mem    = (u32*)&johto_tiles[16];
    tile_tex.Vram   = gsKit_vram_alloc(gs, gsKit_texture_size(tile_tex.Width, tile_tex.Height, tile_tex.PSM), 128);
    gsKit_texture_upload(gs, &tile_tex);
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);

    sprite_tex.Width  = 128;
    sprite_tex.Height = 48;
    sprite_tex.PSM    = GS_PSM_CT32;
    sprite_tex.Clut   = GS_CLUT_NONE;
    sprite_tex.Filter = GS_FILTER_NEAREST; // do not touch this
    sprite_tex.Mem    = (u32*)&sprites[16];
    sprite_tex.Vram   = gsKit_vram_alloc(gs, gsKit_texture_size(sprite_tex.Width, sprite_tex.Height, sprite_tex.PSM), 128);
    gsKit_texture_upload(gs, &sprite_tex);
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
}

static int is_tile_walkable(int world_x, int world_y)
{
    int bx = world_x / META_SZ;
    int by = world_y / META_SZ;

    if (bx < 0 || bx >= current_map->w || by < 0 || by >= current_map->h)
    {
        return 1;
    }

    int in_block_x = (world_x % META_SZ) / TILE_SZ; // 0..3
    int in_block_y = (world_y % META_SZ) / TILE_SZ; // 0..3

    int block_id = current_map->blocks[by * current_map->w + bx];
    const uint8_t *blocks = &johto_blocks[6];
    int tile_id = blocks[block_id * 16 + (in_block_y * 4 + in_block_x)]; // *16, *4

    if (tile_id >= TEX_TILES_W * TEX_TILES_H) return 0;

    return (current_map->collision[tile_id] == 0);
}

static int is_walkable(float x, float y)
{
    if (current_map_id == 2 && y < 40.0f) return 1;     // cherrygrove city to route 30
    if (current_map_id == 3 && y > (current_map->h * META_SZ) - 40.0f) return 1;    // route 30 to cherrygrove city

    // put return 1 to make collisions unavailable
    int r = 8;
    if (!is_tile_walkable((int)x - r, (int)y - r)) return 0;
    if (!is_tile_walkable((int)x + r, (int)y - r)) return 0;
    if (!is_tile_walkable((int)x - r, (int)y + r)) return 0;
    if (!is_tile_walkable((int)x + r, (int)y + r)) return 0;

    if (current_map_id == 0)
    {
        if (fabsf(x - npc.x) < 28.0f && fabsf(y - npc.y) < 28.0f) return 0;
    }

    return 1;
}

static void update_player_pad(Player *p)
{
    int st = padGetState(p->port, p->slot);
    p->last_state = st;

    if (st == PAD_STATE_FINDCTP1 && !p->forced_mainmode)
    {
        padSetMainMode(p->port, p->slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
        p->forced_mainmode = 1;
        return;
    }

    if (st != PAD_STATE_STABLE) return;

    struct padButtonStatus bt;
    if (!padRead(p->port, p->slot, &bt)) return;
    p->last_btns = 0xFFFF ^ bt.btns;

    int lx = bt.ljoy_h, ly = bt.ljoy_v;

    if (!p->analog_calibrated)
    {
        p->analog_center_lx = lx;
        p->analog_center_ly = ly;
        p->analog_calibrated = 1;
    }

    int dx = lx - p->analog_center_lx;
    int dy = ly - p->analog_center_ly;

    float nx = p->wx;
    float ny = p->wy;
    int is_moving = 0;

    if (p->last_btns & PAD_LEFT)  { nx -= MOVE_SPEED; p->direction = 2; is_moving = 1; }
    if (p->last_btns & PAD_RIGHT) { nx += MOVE_SPEED; p->direction = 3; is_moving = 1; }
    if (p->last_btns & PAD_UP)    { ny -= MOVE_SPEED; p->direction = 1; is_moving = 1; }
    if (p->last_btns & PAD_DOWN)  { ny += MOVE_SPEED; p->direction = 0; is_moving = 1; }

    p->stick_moved = 0;
    if (dx < -STICK_DEADZONE) { nx -= MOVE_SPEED; p->direction = 2; is_moving = 1; p->stick_moved = 1; }
    if (dx >  STICK_DEADZONE) { nx += MOVE_SPEED; p->direction = 3; is_moving = 1; p->stick_moved = 1; }
    if (dy < -STICK_DEADZONE) { ny -= MOVE_SPEED; p->direction = 1; is_moving = 1; p->stick_moved = 1; }
    if (dy >  STICK_DEADZONE) { ny += MOVE_SPEED; p->direction = 0; is_moving = 1; p->stick_moved = 1; }

    if (is_moving)
    {
        p->walk_timer++;
        if (p->walk_timer > 12) { p->walk_timer = 0; p->anim_frame = !p->anim_frame; } // frame velocity (change 8 to what you like)
    }
    else
    {
        p->anim_frame = 0;
        p->walk_timer = 0;
    }

    float max_wx = (float)(current_map->w * META_SZ);
    float max_wy = (float)(current_map->h * META_SZ);
    float half_sprite = (PLAYER_SIZE * SCALE) / 2.0f;  // 16.0f

    if (current_map_id == 0)
    {
        if (nx < -half_sprite) nx = -half_sprite;
        if (nx > max_wx - half_sprite) nx = max_wx - half_sprite;
    }

    else if (current_map_id == 1)
    {
        if (nx < -half_sprite) nx = -half_sprite;
        if (nx > max_wx + half_sprite) nx = max_wx + half_sprite;
    }

    else if (current_map_id == 2)
    {
        if (nx < half_sprite) nx = half_sprite;
        if (nx > max_wx + half_sprite) nx = max_wx + half_sprite;
    }

    else
    {
        if (nx < half_sprite) nx = half_sprite;
        if (nx > max_wx - half_sprite) nx = max_wx - half_sprite;
    }

    if (current_map_id == 2)
    {
        if (ny < -half_sprite) ny = -half_sprite;
        if (ny > max_wy - half_sprite) ny = max_wy - half_sprite;
    }

    else if (current_map_id == 3)
    {
        if (ny < half_sprite) ny = half_sprite;
        if (ny > max_wy + half_sprite) ny = max_wy + half_sprite;
    }

    else
    {
        if (ny < half_sprite) ny = half_sprite;
        if (ny > max_wy - half_sprite) ny = max_wy - half_sprite;
    }

    if (is_walkable(nx, p->wy)) p->wx = nx;
    if (is_walkable(p->wx, ny)) p->wy = ny;
}

static float clamp_y_to_map(float y, GameMap *m)
{
    float max_y = (m->h * META_SZ) - 16.0f;
    if (y < 16.0f) return 16.0f;
    if (y > max_y) return max_y;
    return y;
}

static void check_map_transition(Player *p1, Player *p2)
{
    Player *leader = NULL;

    if (p1->wx <= 0.0f) leader = p1;
    else if (p2->wx <= 0.0f) leader = p2;

    if (leader)
    {
        float rel_y = leader->wy;
        Player *follower = (leader == p1) ? p2 : p1;

        if (current_map_id == 0)
        {
            current_map_id = 1;
            current_map = &maps[1];
            leader->wx = (current_map->w * META_SZ) - 32.0f;
            leader->wy = clamp_y_to_map(rel_y, current_map);
            follower->wx = (current_map->w * META_SZ) - 64.0f;
            follower->wy = leader->wy;
        }

        else if (current_map_id == 1)
        {
            current_map_id = 2;
            current_map = &maps[2];
            leader->wx = (current_map->w * META_SZ) - 32.0f;
            leader->wy = clamp_y_to_map(rel_y, current_map);
            follower->wx = (current_map->w * META_SZ) - 64.0f;
            follower->wy = leader->wy;
        }

        return;
    }

    leader = NULL;
    if (p1->wx >= (current_map->w * META_SZ)) leader = p1;
    else if (p2->wx >= (current_map->w * META_SZ)) leader = p2;

    if (leader)
    {
        float rel_y = leader->wy;
        Player *follower = (leader == p1) ? p2 : p1;

        if (current_map_id == 2)
        {
            current_map_id = 1;
            current_map = &maps[1];
            leader->wx = 32.0f;
            leader->wy = clamp_y_to_map(rel_y, current_map);
            follower->wx = 64.0f;
            follower->wy = leader->wy;
        }

        else if (current_map_id == 1)
        {
            current_map_id = 0;
            current_map = &maps[0];
            leader->wx = 32.0f;
            leader->wy = clamp_y_to_map(rel_y, current_map);
            follower->wx = 64.0f;
            follower->wy = leader->wy;
        }
    }

    if (current_map_id == 2)
    {
        leader = NULL;
        if (p1->wy <= 0.0f) leader = p1;
        else if (p2->wy <= 0.0f) leader = p2;

        if (leader)
        {
            Player *follower = (leader == p1) ? p2 : p1;

            current_map_id = 3;
            current_map = &maps[3];

            float spawn_x = (current_map->w * META_SZ) / 2.0f - 64.0f;
            float spawn_y = (current_map->h * META_SZ) - 32.0f;

            for (int i = 0; i < 6; i++)
            {
                if (is_walkable(spawn_x, spawn_y)) break;
                spawn_y -= 16.0f;
            }

            leader->wx = spawn_x;
            leader->wy = spawn_y;
            follower->wx = spawn_x + 32.0f;
            follower->wy = spawn_y;
        }
    }

    if (current_map_id == 3)
    {
        leader = NULL;
        if (p1->wy >= (current_map->h * META_SZ)) leader = p1;
        else if (p2->wy >= (current_map->h * META_SZ)) leader = p2;

        if (leader)
        {
            Player *follower = (leader == p1) ? p2 : p1;

            current_map_id = 2;
            current_map = &maps[2];

            float spawn_x = (current_map->w * META_SZ) / 2.0f - 64.0f;
            float spawn_y = 32.0f;

            for (int i = 0; i < 6; i++)
            {
                if (is_walkable(spawn_x, spawn_y)) break;
                spawn_y += 16.0f;
            }

            leader->wx = spawn_x;
            leader->wy = spawn_y;
            follower->wx = spawn_x + 32.0f;
            follower->wy = spawn_y;
        }
    }
}

static void update_camera(Player *p, Camera *c)
{
    c->x = (int)p->wx - VIEW_W / 2;
    c->y = (int)p->wy - SCREEN_H / 2 + 70;
}

static void render_map(Camera *c, int offset)
{
    const uint8_t *blocks = &johto_blocks[6];

    int bx_start = c->x / META_SZ;
    int by_start = c->y / META_SZ;
    int bx_end   = (c->x + VIEW_W) / META_SZ + 1;
    int by_end   = (c->y + SCREEN_H) / META_SZ + 1;
    if (bx_start < 0) bx_start = 0;
    if (by_start < 0) by_start = 0;
    if (bx_end > current_map->w) bx_end = current_map->w;
    if (by_end > current_map->h) by_end = current_map->h;

    for (int by = by_start; by < by_end; by++)
    {
        for (int bx = bx_start; bx < bx_end; bx++)
        {
            int bidx = current_map->blocks[by * current_map->w + bx];
            const uint8_t *mt = &blocks[bidx * 16];

            int sx = offset + bx * META_SZ - c->x;
            int sy = by * META_SZ - c->y;

            for (int i = 0; i < 16; i++)
            {
                int t = mt[i];
                if (t >= TEX_TILES_W * TEX_TILES_H) continue;
                int tu = (t % TEX_TILES_W) * SRC_TILE;
                int tv = (t / TEX_TILES_W) * SRC_TILE;
                int dx = sx + (i & 3) * TILE_SZ;
                int dy = sy + (i >> 2) * TILE_SZ;

                gsKit_prim_sprite_texture(gs, &tile_tex,
                    (float)dx, (float)dy, (float)tu + 0.5f, (float)tv + 0.5f,
                    (float)(dx + TILE_SZ), (float)(dy + TILE_SZ),
                    (float)(tu + SRC_TILE) - 0.01f, (float)(tv + SRC_TILE) - 0.01f,
                    1, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
            }
        }
    }
}

static void render_npc(NPC *n, Camera *c, int offset)
{
    if (current_map_id != 0) return;                                    // only in New Bark Town
    int sx = offset + (int)n->x - c->x - (PLAYER_SIZE * SCALE / 2);
    int sy = (int)n->y - c->y - (PLAYER_SIZE * SCALE / 2);
    int tv = n->tex_row * 16;
    int tu = 0;                                                         // frame 0: looking down

    gsKit_prim_sprite_texture(gs, &sprite_tex,
        sx, sy, (float)tu, (float)tv,
        sx + (PLAYER_SIZE * SCALE), sy + (PLAYER_SIZE * SCALE),
        (float)(tu + 16), (float)(tv + 16),
        2, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
}

static void render_player(Player *p, Camera *c, int offset)
{
    int sx = offset + (int)p->wx - c->x - (PLAYER_SIZE * SCALE / 2);
    int sy = (int)p->wy - c->y - (PLAYER_SIZE * SCALE);

    if (p->anim_frame == 1) sy -= 2;

    int tv = p->port * 16;
    int tu = (p->direction * 2 + p->anim_frame) * 16;

    gsKit_prim_sprite_texture(gs, &sprite_tex,
        sx, sy, (float)tu, (float)tv,
        sx + (PLAYER_SIZE * SCALE), sy + (PLAYER_SIZE * SCALE),
        (float)(tu + 16), (float)(tv + 16),
        2, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
}

static void render_dialog(int offset)
{
    if (!dialog_open) return;

    int bx = offset + 30;
    int by = SCREEN_H - 140;
    int bw = VIEW_W - 60;
    int bh = 120;

    gsKit_prim_sprite(gs, bx, by, bx+bw, by+bh, 10,
        GS_SETREG_RGBAQ(255, 0, 255, 0x80, 0));

    gsKit_prim_sprite(gs, bx+8, by+8, bx+bw-8, by+bh-8, 11,
        GS_SETREG_RGBAQ(0, 0, 128, 0x80, 0));

    if ((blink_timer / 20) % 2 == 0) {
        gsKit_prim_sprite(gs, bx+bw/2-20, by+bh/2-20, bx+bw/2+20, by+bh/2+20, 12,
            GS_SETREG_RGBAQ(255, 255, 0, 0x80, 0));
    }
}

static u64 state_color(int s)
{
    switch (s)
    {
        case PAD_STATE_DISCONN:  return GS_SETREG_RGBAQ(0x40,0x40,0x40,0x80,0);
        case PAD_STATE_STABLE:   return GS_SETREG_RGBAQ(0x00,0xFF,0x00,0x80,0);
        case PAD_STATE_FINDCTP1: return GS_SETREG_RGBAQ(0xFF,0x80,0x00,0x80,0);
        case PAD_STATE_EXECCMD:  return GS_SETREG_RGBAQ(0x00,0x80,0xFF,0x80,0);
        case PAD_STATE_ERROR:    return GS_SETREG_RGBAQ(0xFF,0x00,0x00,0x80,0);
        default:                 return GS_SETREG_RGBAQ(0xFF,0xFF,0x00,0x80,0);
    }
}

static void render_leds(Player *p, int offset)
{
    gsKit_prim_sprite(gs, offset+4, 4, offset+20, 20, 3, state_color(p->last_state));

    gsKit_prim_sprite(gs, offset+24, 4, offset+40, 20, 3,
        (p->last_btns != 0) ? GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x80,0)
                            : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

    gsKit_prim_sprite(gs, offset+44, 4, offset+60, 20, 3,
        p->stick_moved ? GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x80,0)
                       : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

    float dx = p->wx - npc.x;
    float dy = p->wy - npc.y;
    int cerca = (dx*dx + dy*dy) < 250.0f * 250.0f;
    gsKit_prim_sprite(gs, offset+64, 4, offset+80, 20, 3,
        cerca ? GS_SETREG_RGBAQ(0xFF,0xA0,0x00,0x80,0)
              : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

    gsKit_prim_sprite(gs, offset+84, 4, offset+100, 20, 3,
        dialog_open ? GS_SETREG_RGBAQ(0x00,0xFF,0x00,0x80,0)
                    : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));
}

int main(void)
{
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN",  0, NULL);

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    gs = gsKit_init_global();
    gs->Mode = GS_MODE_NTSC; gs->Interlace = GS_INTERLACED; gs->Field = GS_FRAME;
    gs->Width = SCREEN_W; gs->Height = SCREEN_H;
    gs->PSM = GS_PSM_CT24; gs->PSMZ = GS_PSMZ_16S; gs->ZBuffering = GS_SETTING_OFF;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    gsKit_set_test(gs, GS_ATEST_ON);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), GS_SETREG_ALPHA(0, 1, 0, 1, 0));

    padInit(0);
    padPortOpen(0, 0, pad_buf[0]);
    padPortOpen(1, 0, pad_buf[1]);

    parse_assets();
    upload_tileset();

    Player p[2] = {
        { 0, 0, 320.0f, 256.0f, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 0, 352.0f, 256.0f, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
    };

    Camera cam[2];

    while (1)
    {
        blink_timer++;
        update_player_pad(&p[0]);
        update_player_pad(&p[1]);

        for (int i = 0; i < 2; i++)
        {
            int x_now = (p[i].last_btns & PAD_CROSS) ? 1 : 0;

            if (x_now && !prev_cross[i])
            {
                float dx = p[i].wx - npc.x;
                float dy = p[i].wy - npc.y;
                float dist_sq = dx*dx + dy*dy;
                
                if (current_map_id == 0 && dist_sq < 250.0f * 250.0f)
                {
                    if (!dialog_open)
                    {
                        dialog_open = 1;
                        dialog_owner = i;
                    }

                    else if (dialog_owner == i)
                    {
                        dialog_open = 0;
                        dialog_owner = -1;
                    }
                }
            }

            prev_cross[i] = x_now;
        }

        // map transition comprobation
        check_map_transition(&p[0], &p[1]);
        
        update_camera(&p[0], &cam[0]);
        update_camera(&p[1], &cam[1]);

        gsKit_set_scissor(gs, GS_SETREG_SCISSOR(0, VIEW_W-1, 0, SCREEN_H-1));
        gsKit_prim_sprite(gs, 0, 0, VIEW_W, SCREEN_H, 0, GS_SETREG_RGBAQ(0x10,0x10,0x30,0x80,0));
        render_map(&cam[0], 0);
        render_npc(&npc, &cam[0], 0);
        render_player(&p[0], &cam[0], 0);
        render_leds(&p[0], 0);
        render_dialog(0);
        gsKit_queue_exec(gs);

        gsKit_set_scissor(gs, GS_SETREG_SCISSOR(VIEW_W, SCREEN_W-1, 0, SCREEN_H-1));
        gsKit_prim_sprite(gs, VIEW_W, 0, SCREEN_W, SCREEN_H, 0, GS_SETREG_RGBAQ(0x10,0x10,0x30,0x80,0));
        render_map(&cam[1], VIEW_W);
        render_npc(&npc, &cam[1], VIEW_W);
        render_player(&p[1], &cam[1], VIEW_W);
        render_leds(&p[1], VIEW_W);
        render_dialog(VIEW_W);
        gsKit_queue_exec(gs);

        gsKit_sync_flip(gs);
    }

    return 0;
}