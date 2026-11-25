#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
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

static GameMap maps[2];
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
    sprite_tex.Height = 32;
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
    // put return 1 to make collisions unavailable
    int r = 8;
    if (!is_tile_walkable((int)x - r, (int)y - r)) return 0;
    if (!is_tile_walkable((int)x + r, (int)y - r)) return 0;
    if (!is_tile_walkable((int)x - r, (int)y + r)) return 0;
    if (!is_tile_walkable((int)x + r, (int)y + r)) return 0;
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
    if (nx < PLAYER_SIZE/2) nx = PLAYER_SIZE/2;
    if (ny < PLAYER_SIZE/2) ny = PLAYER_SIZE/2;
    if (nx > max_wx - PLAYER_SIZE/2) nx = max_wx - PLAYER_SIZE/2;
    if (ny > max_wy - PLAYER_SIZE/2) ny = max_wy - PLAYER_SIZE/2;

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
    if (current_map_id == 0)
    {
        Player *leader = NULL;
        if (p1->wx <= 8.0f) leader = p1;
        else if (p2->wx <= 8.0f) leader = p2;

        if (leader)
        {
            float rel_y = leader->wy;

            current_map_id = 1;
            current_map = &maps[1];

            leader->wx = (current_map->w * META_SZ) - 32.0f;
            leader->wy = clamp_y_to_map(rel_y, current_map);

            Player *follower = (leader == p1) ? p2 : p1;
            follower->wx = (current_map->w * META_SZ) - 64.0f;
            follower->wy = leader->wy;
        }
    }

    else if (current_map_id == 1)
    {
        Player *leader = NULL;
        if (p1->wx >= (current_map->w * META_SZ) - 8.0f) leader = p1;
        else if (p2->wx >= (current_map->w * META_SZ) - 8.0f) leader = p2;

        if (leader)
        {
            float rel_y = leader->wy;

            current_map_id = 0;
            current_map = &maps[0];

            leader->wx = 32.0f;
            leader->wy = clamp_y_to_map(rel_y, current_map);

            Player *follower = (leader == p1) ? p2 : p1;
            follower->wx = 64.0f;
            follower->wy = leader->wy;
        }
    }
}

static void update_camera(Player *p, Camera *c)
{
    int map_w = current_map->w * META_SZ;
    int map_h = current_map->h * META_SZ;

    c->x = (int)p->wx - VIEW_W / 2;
    c->y = (int)p->wy - SCREEN_H / 2;

    if (map_w > VIEW_W)
    {
        if (c->x < 0) c->x = 0;
        if (c->x > map_w - VIEW_W) c->x = map_w - VIEW_W;
    }
    else
    {
        c->x = -(VIEW_W - map_w) / 2;
    }

    if (map_h > SCREEN_H)
    {
        if (c->y < 0) c->y = 0;
        if (c->y > map_h - SCREEN_H) c->y = map_h - SCREEN_H;
    }
    else
    {
        c->y = -(SCREEN_H - map_h) / 2;
    }
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

static void render_player(Player *p, Camera *c, int offset)
{
    int tu = (p->direction * 2 + p->anim_frame) * 16;
    int tv = p->port * 16;

    float sx = (float)offset + (p->wx - (float)c->x) - (PLAYER_SIZE * SCALE) / 2.0f;
    float sy = (p->wy - (float)c->y) - (PLAYER_SIZE * SCALE) / 2.0f;

    gsKit_prim_sprite_texture(gs, &sprite_tex,
        sx, sy, (float)tu, (float)tv,
        sx + (PLAYER_SIZE * SCALE), sy + (PLAYER_SIZE * SCALE),
        (float)(tu + 16), (float)(tv + 16),
        2, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
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
        update_player_pad(&p[0]);
        update_player_pad(&p[1]);
        
        // map transition comprobation
        check_map_transition(&p[0], &p[1]);

        update_camera(&p[0], &cam[0]);
        update_camera(&p[1], &cam[1]);

        gsKit_set_scissor(gs, GS_SETREG_SCISSOR(0, VIEW_W-1, 0, SCREEN_H-1));
        gsKit_prim_sprite(gs, 0, 0, VIEW_W, SCREEN_H, 0, GS_SETREG_RGBAQ(0x10,0x10,0x30,0x80,0));
        render_map(&cam[0], 0);
        render_player(&p[0], &cam[0], 0);
        gsKit_queue_exec(gs);

        gsKit_set_scissor(gs, GS_SETREG_SCISSOR(VIEW_W, SCREEN_W-1, 0, SCREEN_H-1));
        gsKit_prim_sprite(gs, VIEW_W, 0, SCREEN_W, SCREEN_H, 0, GS_SETREG_RGBAQ(0x10,0x10,0x30,0x80,0));
        render_map(&cam[1], VIEW_W);
        render_player(&p[1], &cam[1], VIEW_W);
        gsKit_queue_exec(gs);

        gsKit_sync_flip(gs);
    }

    return 0;
}