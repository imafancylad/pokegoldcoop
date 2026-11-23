#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
#include "assets_data.h"

#define SCREEN_W        640
#define SCREEN_H        448
#define VIEW_W          (SCREEN_W / 2)

#define SRC_TILE        8
#define SCALE           2
#define TILE_SZ         (SRC_TILE * SCALE)      // 32 px on screen
#define META_SZ         (TILE_SZ * 2)           // 64 px on screen
#define TEX_TILES_W     16
#define TEX_TILES_H     8

#define PLAYER_SIZE     24
#define MOVE_SPEED      4.0f
#define STICK_DEADZONE  40

static char pad_buf[2][256] __attribute__((aligned(64)));

typedef struct {
    int w, h;
    const uint8_t *blocks;
} Map;

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
} Player;

static GSGLOBAL *gs;
static GSTEXTURE tile_tex;
static Map map;

static u64 state_color(int s)
{
    switch (s) {
        case PAD_STATE_DISCONN:   return GS_SETREG_RGBAQ(0x40,0x40,0x40,0x80,0);
        case PAD_STATE_STABLE:    return GS_SETREG_RGBAQ(0x00,0xFF,0x00,0x80,0);
        case PAD_STATE_FINDCTP1:  return GS_SETREG_RGBAQ(0xFF,0x80,0x00,0x80,0);
        case PAD_STATE_EXECCMD:   return GS_SETREG_RGBAQ(0x00,0x80,0xFF,0x80,0);
        case PAD_STATE_ERROR:     return GS_SETREG_RGBAQ(0xFF,0x00,0x00,0x80,0);
        default:                  return GS_SETREG_RGBAQ(0xFF,0xFF,0x00,0x80,0);
    }
}

static void parse_assets(void)
{
    const uint8_t *m = newbark_map;
    map.w = m[4] | (m[5] << 8);
    map.h = m[6] | (m[7] << 8);
    map.blocks = &m[8];
}

static void upload_tileset(void)
{
    tile_tex.Width  = TEX_TILES_W * SRC_TILE;
    tile_tex.Height = TEX_TILES_H * SRC_TILE;
    tile_tex.PSM    = GS_PSM_CT32;
    tile_tex.Clut   = GS_CLUT_NONE;
    tile_tex.Filter = GS_FILTER_NEAREST;
    tile_tex.Mem    = (u32*)&johto_tiles[8];   // skip JTL1+u16 TW+u16 TH
    tile_tex.Vram   = gsKit_vram_alloc(gs, gsKit_texture_size(tile_tex.Width, tile_tex.Height, tile_tex.PSM), 2);
    gsKit_texture_upload(gs, &tile_tex);
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
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

    if (p->last_btns & PAD_LEFT)  p->wx -= MOVE_SPEED;
    if (p->last_btns & PAD_RIGHT) p->wx += MOVE_SPEED;
    if (p->last_btns & PAD_UP)    p->wy -= MOVE_SPEED;
    if (p->last_btns & PAD_DOWN)  p->wy += MOVE_SPEED;

    int lx = bt.ljoy_h, ly = bt.ljoy_v;

    if (!p->analog_calibrated)
    {
        p->analog_center_lx = lx;
        p->analog_center_ly = ly;
        p->analog_calibrated = 1;
    }

    int dx = lx - p->analog_center_lx;
    int dy = ly - p->analog_center_ly;
    p->stick_moved = 0;

    if (dx < -STICK_DEADZONE) { p->wx -= MOVE_SPEED; p->stick_moved = 1; }
    if (dx >  STICK_DEADZONE) { p->wx += MOVE_SPEED; p->stick_moved = 1; }
    if (dy < -STICK_DEADZONE) { p->wy -= MOVE_SPEED; p->stick_moved = 1; }
    if (dy >  STICK_DEADZONE) { p->wy += MOVE_SPEED; p->stick_moved = 1; }

    float max_wx = (float)(map.w * META_SZ);
    float max_wy = (float)(map.h * META_SZ);
    if (p->wx < PLAYER_SIZE/2) p->wx = PLAYER_SIZE/2;
    if (p->wy < PLAYER_SIZE/2) p->wy = PLAYER_SIZE/2;
    if (p->wx > max_wx - PLAYER_SIZE/2) p->wx = max_wx - PLAYER_SIZE/2;
    if (p->wy > max_wy - PLAYER_SIZE/2) p->wy = max_wy - PLAYER_SIZE/2;
}

static void update_camera(Player *p, Camera *c)
{
    c->x = (int)p->wx - VIEW_W / 2;
    c->y = (int)p->wy - SCREEN_H / 2;

    int max_x = map.w * META_SZ - VIEW_W;
    int max_y = map.h * META_SZ - SCREEN_H;

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (c->x < 0) c->x = 0;
    if (c->y < 0) c->y = 0;
    if (c->x > max_x) c->x = max_x;
    if (c->y > max_y) c->y = max_y;
}

static void render_map(Camera *c, int offset)
{
    const uint8_t *blocks = &johto_blocks[6];   // skip JBLK+u16 count

    int bx_start = c->x / META_SZ;
    int by_start = c->y / META_SZ;
    int bx_end   = (c->x + VIEW_W) / META_SZ + 1;
    int by_end   = (c->y + SCREEN_H) / META_SZ + 1;

    if (bx_start < 0) bx_start = 0;
    if (by_start < 0) by_start = 0;
    if (bx_end > map.w) bx_end = map.w;
    if (by_end > map.h) by_end = map.h;

    for (int by = by_start; by < by_end; by++)
    {
        for (int bx = bx_start; bx < bx_end; bx++)
        {
            int bidx = map.blocks[by * map.w + bx];
            const uint8_t *mt = &blocks[bidx * 4];

            int sx = offset + bx * META_SZ - c->x;
            int sy = by * META_SZ - c->y;

            for (int i = 0; i < 4; i++)
            {
                int t = mt[i];
                if (t >= TEX_TILES_W * TEX_TILES_H) continue;
                int tu = (t % TEX_TILES_W) * SRC_TILE;
                int tv = (t / TEX_TILES_W) * SRC_TILE;
                int dx = sx + (i & 1) * TILE_SZ;
                int dy = sy + (i >> 1) * TILE_SZ;
                gsKit_prim_sprite_texture(gs, &tile_tex,
                    dx, dy, tu, tv,
                    dx + TILE_SZ, dy + TILE_SZ,
                    tu + SRC_TILE, tv + SRC_TILE,
                    1, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
            }
        }
    }
}

static void render_player(Player *p, Camera *c, int offset)
{
    int sx = offset + (int)p->wx - c->x - PLAYER_SIZE/2;
    int sy = (int)p->wy - c->y - PLAYER_SIZE/2;
    gsKit_prim_sprite(gs, sx, sy, sx + PLAYER_SIZE, sy + PLAYER_SIZE, 2, p->color_ok);
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

    padInit(0);
    padPortOpen(0, 0, pad_buf[0]);
    padPortOpen(1, 0, pad_buf[1]);

    parse_assets();
    upload_tileset();

    Player p[2] = {
        { 0, 0, map.w*META_SZ/2.0f, map.h*META_SZ/2.0f, 0, -1, 0, 0, 0, 0, 0,
          GS_SETREG_RGBAQ(0xFF,0xFF,0x00,0x80,0) },
        { 1, 0, map.w*META_SZ/2.0f, map.h*META_SZ/2.0f, 0, -1, 0, 0, 0, 0, 0,
          GS_SETREG_RGBAQ(0x00,0xFF,0xFF,0x80,0) }
    };

    Camera cam[2];

    while (1)
    {
        update_player_pad(&p[0]);
        update_player_pad(&p[1]);
        update_camera(&p[0], &cam[0]);
        update_camera(&p[1], &cam[1]);

        // viewport 1 (left side)
        gsKit_set_scissor(gs, GS_SETREG_SCISSOR(0, VIEW_W-1, 0, SCREEN_H-1));
        gsKit_prim_sprite(gs, 0, 0, VIEW_W, SCREEN_H, 0, GS_SETREG_RGBAQ(0x10,0x10,0x30,0x80,0));
        render_map(&cam[0], 0);
        render_player(&p[0], &cam[0], 0);
        render_leds(&p[0], 0);
        gsKit_queue_exec(gs);

        // viewport 2 (right side)
        gsKit_set_scissor(gs, GS_SETREG_SCISSOR(VIEW_W, SCREEN_W-1, 0, SCREEN_H-1));
        gsKit_prim_sprite(gs, VIEW_W, 0, SCREEN_W, SCREEN_H, 0, GS_SETREG_RGBAQ(0x10,0x10,0x30,0x80,0));
        render_map(&cam[1], VIEW_W);
        render_player(&p[1], &cam[1], VIEW_W);
        render_leds(&p[1], VIEW_W);
        gsKit_queue_exec(gs);

        gsKit_sync_flip(gs);
    }

    return 0;
}