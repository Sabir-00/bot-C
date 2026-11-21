#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define GAME_TITLE   "Last war-survival game"
#define TOL          10                          
#define MAX_BAD_PCT  15                          
#define MAX_HITS     6
#define MAX_SEEN     64
#define SEEN_MS      (6UL * 60UL * 60UL * 1000UL) 
#define LUCKY_PAUSE  (5UL * 60UL * 1000UL)        
#define MAX_REPEAT   8                           

#define ACT_NONE  0
#define ACT_OPEN  1
#define ACT_GIFT  2
#define ACT_BLUE  3
#define ACT_LUCKY 4
#define ACT_CLOSE 5

typedef struct s_needle
{
    unsigned char *px;
    int            w;
    int            h;
}   t_needle;

typedef struct s_area
{
    int x0;
    int y0;
    int x1;
    int y1;
}   t_area;

typedef struct s_hit
{
    int x;
    int y;
    int bad;
}   t_hit;

typedef struct s_kind
{
    const char *file;
    int         click_dx;   
    int         click_dy;
    int         hash_dx;    
    int         hash_dy;
    int         hash_w;
    int         hash_h;
    t_needle    needle;
}   t_kind;

typedef struct s_seen
{
    unsigned int hash;
    DWORD        when;
}   t_seen;

/
static t_kind g_blue[2] =
{
    {"needle_dig.bmp",    -250, 45, -260, 10, 130, 40, {NULL, 0, 0}},
    {"needle_flight.bmp", -250, 45, -260, 10, 130, 40, {NULL, 0, 0}},
};

static int            g_dry;
static int            g_stop;
static HWND           g_game;
static DWORD          g_lucky_block;
static DWORD          g_expect_open;
static t_needle       g_open;
static t_needle       g_gift;
static t_needle       g_lucky;
static t_needle       g_xbutton;
static t_seen         g_seen[MAX_SEEN];
static int            g_seen_count;
static int            g_seen_next;
static unsigned char *g_canvas;     
static int            g_pw;
static int            g_ph;


static void nap(int ms)
{
    while (ms > 0 && !g_stop)
    {
        Sleep(50);
        ms -= 50;
        if (GetAsyncKeyState(VK_F9) & 0x8000)
            g_stop = 1;
    }
}



unsigned char *capture_screen(int *width, int *height)
{
    HDC            screen;
    HDC            desk;
    HBITMAP        sheet;
    HGDIOBJ        old;
    BITMAPINFO     info;
    unsigned char *pixels;

    *width  = GetSystemMetrics(SM_CXSCREEN);
    *height = GetSystemMetrics(SM_CYSCREEN);
    screen = GetDC(NULL);
    desk   = CreateCompatibleDC(screen);
    sheet  = CreateCompatibleBitmap(screen, *width, *height);
    old    = SelectObject(desk, sheet);
    BitBlt(desk, 0, 0, *width, *height, screen, 0, 0, SRCCOPY);
    SelectObject(desk, old);
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = *width;
    info.bmiHeader.biHeight      = -*height;
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;
    pixels = malloc((size_t)*width * *height * 4);
    if (pixels != NULL)
        GetDIBits(desk, sheet, 0, *height, pixels, &info, DIB_RGB_COLORS);
    DeleteObject(sheet);
    DeleteDC(desk);
    ReleaseDC(NULL, screen);
    return pixels;
}

int save_bmp(const char *path, unsigned char *pixels, int width, int height)
{
    FILE             *f;
    BITMAPFILEHEADER  fh;
    BITMAPINFOHEADER  ih;

    f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    ZeroMemory(&fh, sizeof(fh));
    ZeroMemory(&ih, sizeof(ih));
    fh.bfType    = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize    = fh.bfOffBits + width * height * 4;
    ih.biSize        = sizeof(ih);
    ih.biWidth       = width;
    ih.biHeight      = -height;
    ih.biPlanes      = 1;
    ih.biBitCount    = 32;
    ih.biCompression = BI_RGB;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(pixels, 1, (size_t)width * height * 4, f);
    fclose(f);
    return 1;
}

unsigned char *load_bmp(const char *path, int *w, int *h)
{
    FILE             *f;
    BITMAPFILEHEADER  fh;
    BITMAPINFOHEADER  ih;
    unsigned char    *pixels;
    size_t            size;

    f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    if (fread(&fh, sizeof(fh), 1, f) != 1 || fread(&ih, sizeof(ih), 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }
    *w = ih.biWidth;
    *h = (ih.biHeight < 0) ? -ih.biHeight : ih.biHeight;
    size = (size_t)*w * *h * 4;
    pixels = malloc(size);
    if (pixels == NULL)
    {
        fclose(f);
        return NULL;
    }
    if (fread(pixels, 1, size, f) != size)
    {
        free(pixels);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return pixels;
}



static int close_enough(const unsigned char *a, const unsigned char *b)
{
    int i;
    int d;

    i = 0;
    while (i < 3)
    {
        d = a[i] - b[i];
        if (d < 0)
            d = -d;
        if (d > TOL)
            return 0;
        i++;
    }
    return 1;
}

static int count_diff(const unsigned char *hay, int hw, const t_needle *n,
                      int x, int y, int limit)
{
    int nx;
    int ny;
    int bad;

    bad = 0;
    ny = 0;
    while (ny < n->h)
    {
        nx = 0;
        while (nx < n->w)
        {
            if (!close_enough(hay + ((size_t)(y + ny) * hw + x + nx) * 4,
                              n->px + ((size_t)ny * n->w + nx) * 4))
            {
                bad++;
                if (bad > limit)
                    return bad;
            }
            nx++;
        }
        ny++;
    }
    return bad;
}

static void add_hit(t_hit *hits, int *count, int max_hits, const t_needle *n,
                    int x, int y, int bad)
{
    int i;

    i = 0;
    while (i < *count)
    {
        if (abs(hits[i].x - x) < n->w && abs(hits[i].y - y) < n->h)
        {
            if (bad < hits[i].bad)      
            {
                hits[i].x = x;
                hits[i].y = y;
                hits[i].bad = bad;
            }
            return;
        }
        i++;
    }
    if (*count < max_hits)
    {
        hits[*count].x = x;
        hits[*count].y = y;
        hits[*count].bad = bad;
        (*count)++;
    }
}

static int find_all(const unsigned char *hay, int hw, const t_needle *n,
                    t_area a, t_hit *hits, int max_hits)
{
    int x;
    int y;
    int bad;
    int count;
    int max_bad;

    count = 0;
    max_bad = n->w * n->h * MAX_BAD_PCT / 100;
    y = a.y0;
    while (y + n->h <= a.y1)
    {
        x = a.x0;
        while (x + n->w <= a.x1)
        {
            bad = count_diff(hay, hw, n, x, y, max_bad);
            if (bad <= max_bad)
                add_hit(hits, &count, max_hits, n, x, y, bad);
            x++;
        }
        y++;
    }
    return count;
}

static int find_one(const unsigned char *hay, int hw, const t_needle *n,
                    t_area a, int *fx, int *fy)
{
    t_hit hits[MAX_HITS];
    int   count;
    int   i;
    int   best;

    count = find_all(hay, hw, n, a, hits, MAX_HITS);
    if (count == 0)
        return 0;
    best = 0;
    i = 1;
    while (i < count)
    {
        if (hits[i].bad < hits[best].bad)
            best = i;
        i++;
    }
    *fx = hits[best].x;
    *fy = hits[best].y;
    return 1;
}

static t_area chat_area(int pw, int ph)
{
    t_area a;

    a.x0 = pw * 685 / 1000;
    a.y0 = ph * 180 / 1000;
    a.x1 = pw;
    a.y1 = ph * 880 / 1000;
    return a;
}

static t_area map_center_area(int pw, int ph)
{
    t_area a;
    int    cx;
    int    cy;

    cx = pw * 68 / 100 / 2;
    cy = ph / 2;
    a.x0 = cx - 230;
    a.y0 = cy - 220;
    a.x1 = cx + 230;
    a.y1 = cy + 200;
    return a;
}

static t_area popup_x_area(int pw, int ph)
{
    t_area a;

    a.x0 = pw * 400 / 1000;
    a.y0 = ph * 60 / 1000;
    a.x1 = pw * 560 / 1000;
    a.y1 = ph * 220 / 1000;
    return a;
}

static int inside(t_area a, int x, int y, int w, int h)
{
    return x >= a.x0 && y >= a.y0 && x + w <= a.x1 && y + h <= a.y1;
}


static unsigned int hash_region(const unsigned char *px, int pw,
                                int x, int y, int w, int h)
{
    unsigned int         hash;
    const unsigned char *p;
    int                  xx;
    int                  yy;

    hash = 2166136261u;
    yy = 0;
    while (yy < h)
    {
        xx = 0;
        while (xx < w)
        {
            p = px + ((size_t)(y + yy) * pw + x + xx) * 4;
            hash = (hash ^ (p[0] >> 4)) * 16777619u;  
            hash = (hash ^ (p[1] >> 4)) * 16777619u;
            hash = (hash ^ (p[2] >> 4)) * 16777619u;
            xx++;
        }
        yy++;
    }
    return hash;
}

static int was_seen(unsigned int hash)
{
    int i;

    i = 0;
    while (i < g_seen_count)
    {
        if (g_seen[i].hash == hash && GetTickCount() - g_seen[i].when < SEEN_MS)
            return 1;
        i++;
    }
    return 0;
}

static void remember(unsigned int hash)
{
    int i;

    i = 0;
    while (i < g_seen_count)
    {
        if (g_seen[i].hash == hash)
        {
            g_seen[i].when = GetTickCount();
            return;
        }
        i++;
    }
    if (g_seen_count < MAX_SEEN)
        i = g_seen_count++;
    else
    {
        i = g_seen_next;
        g_seen_next = (g_seen_next + 1) % MAX_SEEN;
    }
    g_seen[i].hash = hash;
    g_seen[i].when = GetTickCount();
}


static void put_px(int x, int y, int b, int g, int r)
{
    unsigned char *q;

    if (g_canvas == NULL || x < 0 || y < 0 || x >= g_pw || y >= g_ph)
        return;
    q = g_canvas + ((size_t)y * g_pw + x) * 4;
    q[0] = (unsigned char)b;
    q[1] = (unsigned char)g;
    q[2] = (unsigned char)r;
}

static void mark(int x, int y, int w, int h, int b, int g, int r)
{
    int i;

    i = 0;
    while (i < w)
    {
        put_px(x + i, y, b, g, r);
        put_px(x + i, y + h - 1, b, g, r);
        i++;
    }
    i = 0;
    while (i < h)
    {
        put_px(x, y + i, b, g, r);
        put_px(x + w - 1, y + i, b, g, r);
        i++;
    }
}

static void click_at(int x, int y)
{
    if (g_dry)
        return;
    SetCursorPos(x, y);
    Sleep(150);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    Sleep(60);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
}

static void press_esc(void)
{
    BYTE scan;

    if (g_dry)
        return;
    scan = (BYTE)MapVirtualKeyA(VK_ESCAPE, MAPVK_VK_TO_VSC);
    keybd_event(VK_ESCAPE, scan, 0, 0);
    Sleep(60);
    keybd_event(VK_ESCAPE, scan, KEYEVENTF_KEYUP, 0);
}

static void click_found(const char *what, const t_needle *n,
                        int fx, int fy, int cx, int cy)
{
    printf("%s found at %d,%d, click at %d,%d\n", what, fx, fy, cx, cy);
    mark(fx, fy, n->w, n->h, 0, 255, 0);         
    mark(cx - 4, cy - 4, 9, 9, 0, 0, 255);     
    click_at(cx, cy);
}

static int game_active(void)
{
    HWND front;

    if (g_game == NULL)
        return 1;
    front = GetForegroundWindow();
    return front != NULL && GetAncestor(front, GA_ROOT) == GetAncestor(g_game, GA_ROOT);
}

static int step(const unsigned char *photo, int pw, int ph, unsigned char *canvas)
{
    static int warned;
    t_area     chat;
    t_area     center;
    t_area     popup;
    t_hit      hits[MAX_HITS];
    unsigned int hash;
    int        fx;
    int        fy;
    int        hx;
    int        hy;
    int        n;
    int        i;
    int        k;

    g_canvas = canvas;
    g_pw = pw;
    g_ph = ph;
    if (!game_active())
    {
        if (!warned)
            printf("the game is not the active window: waiting\n");
        warned = 1;
        if (!g_dry)
            return ACT_NONE;
    }
    else
        warned = 0;
    chat = chat_area(pw, ph);
    center = map_center_area(pw, ph);
    popup = popup_x_area(pw, ph);
    /*  Lucky Gift : press Open, then ESC */
    if (find_one(photo, pw, &g_open, chat, &fx, &fy))
    {
        g_expect_open = 0;
        click_found("Open button", &g_open, fx, fy, fx + g_open.w / 2, fy + g_open.h / 2);
        if (!g_dry)
        {
            nap(800);
            click_at(1121, 11);
            nap(800);
            return ACT_OPEN;
        }
    }

        /* the "Treasure Rewards" popup is open: close it with its X button */
    if (g_xbutton.px != NULL && find_one(photo, pw, &g_xbutton, popup, &fx, &fy))
    {
        click_found("popup X button", &g_xbutton, fx, fy,
                    fx + g_xbutton.w / 2, fy + g_xbutton.h / 2);
        if (!g_dry)
        {
            nap(800);
            return ACT_CLOSE;
        }
    }

    /* clicked a Lucky Gift card but no Open button showed up: close and pause */
    if (g_expect_open != 0 && GetTickCount() - g_expect_open > 3500)
    {
        printf("no Open button appeared: pressing ESC, pausing Lucky Gifts for 5 minutes\n");
        g_expect_open = 0;
        g_lucky_block = GetTickCount() + LUCKY_PAUSE;
        click_at(1121, 11);
        return ACT_NONE;
    }

    /* gift box in the middle of the map: click it, then ESC */
    if (find_one(photo, pw, &g_gift, center, &fx, &fy))
    {
        click_found("gift box", &g_gift, fx, fy, fx + g_gift.w / 2, fy + g_gift.h / 2);
        if (!g_dry)
        {
            nap(3000);
            press_esc();
            nap(800);
            return ACT_GIFT;
        }
    }

    /*  a blue "ready to claim" message in the chat that we haven't handled yet */
    k = 0;
    while (k < 2)
    {
        n = find_all(photo, pw, &g_blue[k].needle, chat, hits, MAX_HITS);
        i = 0;
        while (i < n)
        {
            hx = hits[i].x + g_blue[k].hash_dx;
            hy = hits[i].y + g_blue[k].hash_dy;
            if (inside(chat, hx, hy, g_blue[k].hash_w, g_blue[k].hash_h))
            {
                hash = hash_region(photo, pw, hx, hy, g_blue[k].hash_w, g_blue[k].hash_h);
                mark(hx, hy, g_blue[k].hash_w, g_blue[k].hash_h, 255, 0, 255);
                printf("blue message type %d at %d,%d, fingerprint %08X %s\n", k,
                       hits[i].x, hits[i].y, hash,
                       was_seen(hash) ? "(already handled)" : "(new)");
                if (g_dry || !was_seen(hash))
                {
                    if (!g_dry)
                        remember(hash);
                    click_found("blue message", &g_blue[k].needle, hits[i].x, hits[i].y,
                                hits[i].x + g_blue[k].click_dx,
                                hits[i].y + g_blue[k].click_dy);
                    if (!g_dry)
                    {
                        nap(1000);
                        return ACT_BLUE;
                    }
                }
            }
            i++;
        }
        k++;
    }

    /*  unclaimed Lucky Gift card in the chat */
    if (GetTickCount() >= g_lucky_block
        && find_one(photo, pw, &g_lucky, chat, &fx, &fy))
    {
        click_found("Lucky Gift card", &g_lucky, fx, fy,
                    fx + g_lucky.w / 2, fy + g_lucky.h / 2);
        if (!g_dry)
        {
            g_expect_open = GetTickCount();
            nap(800);
            return ACT_LUCKY;
        }
    }
    return ACT_NONE;
}


static int load_needle(t_needle *n, const char *file)
{
    n->px = load_bmp(file, &n->w, &n->h);
    if (n->px == NULL)
    {
        printf("could not load %s\n", file);
        return 0;
    }
    return 1;
}

static int load_needles(void)
{
    int k;

    if (!load_needle(&g_open, "needle_open.bmp")
        || !load_needle(&g_gift, "needle_gift.bmp")
        || !load_needle(&g_lucky, "needle_lucky.bmp")
        || !load_needle(&g_xbutton, "needle_xbutton.bmp"))
        return 0;
    k = 0;
    while (k < 2)
    {
        if (!load_needle(&g_blue[k].needle, g_blue[k].file))
            return 0;
        k++;
    }
    return 1;
}

int main(int argc, char **argv)
{
    unsigned char *photo;
    unsigned char *canvas;
    int            pw;
    int            ph;
    int            act;
    int            last;
    int            repeat;

    SetProcessDPIAware();
    g_dry = (argc == 2 && strcmp(argv[1], "dry") == 0);
    if (!load_needles())
        return 1;
    g_game = FindWindowA(NULL, GAME_TITLE);
    if (g_game == NULL)
        printf("warning: game window not found, the active-window safety check is off\n");
    printf("%s. Starting in 5 seconds. Hold F9 to stop.\n", g_dry ? "DRY RUN (no clicks)" : "LIVE");
    nap(1000);
    if (!g_dry && g_game != NULL)
    {
        if (IsIconic(g_game))
            ShowWindow(g_game, SW_RESTORE);
        SetForegroundWindow(g_game);
        nap(1000);
    }
    last = ACT_NONE;
    repeat = 0;
    while (!g_stop)
    {
        photo = capture_screen(&pw, &ph);
        if (photo == NULL)
            break;
        canvas = NULL;
        if (g_dry)
        {
            canvas = malloc((size_t)pw * ph * 4);
            if (canvas != NULL)
                memcpy(canvas, photo, (size_t)pw * ph * 4);
        }
        act = step(photo, pw, ph, canvas);
        if (g_dry)
        {
            if (canvas != NULL && save_bmp("bot_photo.bmp", canvas, pw, ph))
                printf("saved bot_photo.bmp (green = found, red = click point, magenta = fingerprint area)\n");
            free(canvas);
            g_stop = 1;
        }
        free(photo);
        if (act != ACT_NONE)
        {
            repeat = (act == last) ? repeat + 1 : 1;
            last = act;
            if (repeat > MAX_REPEAT)
            {
                printf("same action %d times in a row: something is stuck, stopping\n", repeat);
                g_stop = 1;
            }
        }
        nap(600);
    }
    free(g_open.px);
    free(g_gift.px);
    free(g_lucky.px);
    free(g_blue[0].needle.px);
    free(g_blue[1].needle.px);
    free(g_xbutton.px);
    return 0;
}