#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>

#define SCREEN_W 640
#define SCREEN_H 448
#define VIEW_W   (SCREEN_W / 2)
#define PLAYER_SIZE 16
#define MOVE_SPEED  4.0f
#define STICK_DEADZONE 40

static char pad_buf[2][256] __attribute__((aligned(64)));

typedef struct {
    int   port, slot;
    float x, y;
    u64   color_ok;
    u16   last_btns;
    int   last_state;
    int   stick_moved;
    int   analog_center_lx;
    int   analog_center_ly;
    int   analog_calibrated;
    int   forced_mainmode;
} Player;

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

static void update_pad(Player *p)
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

    if (p->last_btns & PAD_LEFT)  p->x -= MOVE_SPEED;
    if (p->last_btns & PAD_RIGHT) p->x += MOVE_SPEED;
    if (p->last_btns & PAD_UP)    p->y -= MOVE_SPEED;
    if (p->last_btns & PAD_DOWN)  p->y += MOVE_SPEED;

    int lx = bt.ljoy_h;
    int ly = bt.ljoy_v;

    if (!p->analog_calibrated)
    {
        p->analog_center_lx = lx;
        p->analog_center_ly = ly;
        p->analog_calibrated = 1;
    }

    int dx = lx - p->analog_center_lx;
    int dy = ly - p->analog_center_ly;

    p->stick_moved = 0;
    if (dx < -STICK_DEADZONE) { p->x -= MOVE_SPEED; p->stick_moved = 1; }
    if (dx >  STICK_DEADZONE) { p->x += MOVE_SPEED; p->stick_moved = 1; }
    if (dy < -STICK_DEADZONE) { p->y -= MOVE_SPEED; p->stick_moved = 1; }
    if (dy >  STICK_DEADZONE) { p->y += MOVE_SPEED; p->stick_moved = 1; }

    if (p->x < 0.0f) p->x = 0.0f;
    if (p->y < 0.0f) p->y = 0.0f;
    if (p->x > VIEW_W - PLAYER_SIZE)   p->x = VIEW_W - PLAYER_SIZE;
    if (p->y > SCREEN_H - PLAYER_SIZE) p->y = SCREEN_H - PLAYER_SIZE;
}

int main(void)
{
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN",  0, NULL);

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    GSGLOBAL *gs = gsKit_init_global();
    gs->Mode = GS_MODE_NTSC; gs->Interlace = GS_INTERLACED; gs->Field = GS_FRAME;
    gs->Width = SCREEN_W; gs->Height = SCREEN_H;
    gs->PSM = GS_PSM_CT24; gs->PSMZ = GS_PSMZ_16S; gs->ZBuffering = GS_SETTING_OFF;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    padInit(0);
    padPortOpen(0, 0, pad_buf[0]);
    padPortOpen(1, 0, pad_buf[1]);

    Player p[2] = {
        { 0, 0, (VIEW_W - PLAYER_SIZE)/2.0f, (SCREEN_H - PLAYER_SIZE)/2.0f,
          GS_SETREG_RGBAQ(0xFF,0xFF,0x00,0x80,0), 0, -1, 0, 0, 0, 0, 0 },
        { 1, 0, (VIEW_W - PLAYER_SIZE)/2.0f, (SCREEN_H - PLAYER_SIZE)/2.0f,
          GS_SETREG_RGBAQ(0x00,0xFF,0xFF,0x80,0), 0, -1, 0, 0, 0, 0, 0 }
    };

    while (1)
    {
        update_pad(&p[0]);
        update_pad(&p[1]);

        gsKit_prim_sprite(gs, 0, 0, VIEW_W, SCREEN_H, 1, GS_SETREG_RGBAQ(0x00,0x00,0x60,0x80,0));
        gsKit_prim_sprite(gs, VIEW_W, 0, SCREEN_W, SCREEN_H, 1, GS_SETREG_RGBAQ(0x60,0x00,0x00,0x80,0));

        gsKit_prim_sprite(gs,
            (int)p[0].x, (int)p[0].y,
            (int)p[0].x+PLAYER_SIZE, (int)p[0].y+PLAYER_SIZE,
            2, p[0].color_ok);

        gsKit_prim_sprite(gs,
            VIEW_W+(int)p[1].x, (int)p[1].y,
            VIEW_W+(int)p[1].x+PLAYER_SIZE, (int)p[1].y+PLAYER_SIZE,
            2, p[1].color_ok);

        gsKit_prim_sprite(gs, 4, 4, 20, 20, 3, state_color(p[0].last_state));

        gsKit_prim_sprite(gs, 24, 4, 40, 20, 3,
            (p[0].last_btns != 0) ? GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x80,0)
                                  : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

        gsKit_prim_sprite(gs, 44, 4, 60, 20, 3,
            p[0].stick_moved ? GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x80,0)
                             : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

        gsKit_prim_sprite(gs, VIEW_W+4, 4, VIEW_W+20, 20, 3, state_color(p[1].last_state));

        gsKit_prim_sprite(gs, VIEW_W+24, 4, VIEW_W+40, 20, 3,
            (p[1].last_btns != 0) ? GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x80,0)
                                  : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

        gsKit_prim_sprite(gs, VIEW_W+44, 4, VIEW_W+60, 20, 3,
            p[1].stick_moved ? GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x80,0)
                             : GS_SETREG_RGBAQ(0x20,0x20,0x20,0x80,0));

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }

    return 0;
}