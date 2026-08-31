#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define GRID_W 30
#define GRID_H 18
#define CELL 12
#define HEADER 25
#define WIDTH (GRID_W * CELL)
#define HEIGHT (HEADER + GRID_H * CELL)
#define MAX_SNAKE (GRID_W * GRID_H)

struct point {
	short x;
	short y;
};

struct game {
	struct point body[MAX_SNAKE];
	int length;
	int dx;
	int dy;
	int next_dx;
	int next_dy;
	struct point food;
	int score;
	bool paused;
	bool over;
};

struct display {
	Display *x;
	int screen;
	Window window;
	Pixmap buffer;
	GC gc;
	XFontStruct *font;
	Atom wm_delete;
	unsigned long dark;
	unsigned long panel;
	unsigned long cyan;
	unsigned long blue;
	unsigned long white;
};

static unsigned long
color(struct display *ui, const char *name, unsigned long fallback)
{
	XColor exact;
	XColor allocated;

	if (XAllocNamedColor(ui->x, DefaultColormap(ui->x, ui->screen), name,
	                    &allocated, &exact))
		return allocated.pixel;
	return fallback;
}

static bool
occupies(const struct game *game, int x, int y)
{
	int i;

	for (i = 0; i < game->length; i++)
		if (game->body[i].x == x && game->body[i].y == y)
			return true;
	return false;
}

static void
place_food(struct game *game)
{
	int attempts;

	for (attempts = 0; attempts < MAX_SNAKE; attempts++) {
		int x = rand() % GRID_W;
		int y = rand() % GRID_H;
		if (!occupies(game, x, y)) {
			game->food.x = (short)x;
			game->food.y = (short)y;
			return;
		}
	}
}

static void
reset_game(struct game *game)
{
	int i;

	memset(game, 0, sizeof(*game));
	game->length = 5;
	game->dx = game->next_dx = 1;
	for (i = 0; i < game->length; i++) {
		game->body[i].x = (short)(GRID_W / 2 - i);
		game->body[i].y = GRID_H / 2;
	}
	place_food(game);
}

static void
notice_score(int score)
{
	char temporary[64];
	FILE *stream;

	snprintf(temporary, sizeof(temporary), "/tmp/holo-notify.snake.%ld",
	         (long)getpid());
	stream = fopen(temporary, "w");
	if (stream == NULL)
		return;
	fprintf(stream, "Snake game over - score %d\n", score);
	fclose(stream);
	(void)rename(temporary, "/tmp/holo-notify");
}

static void
step(struct game *game)
{
	struct point head;
	bool ate;
	int i;

	if (game->paused || game->over)
		return;
	game->dx = game->next_dx;
	game->dy = game->next_dy;
	head.x = (short)(game->body[0].x + game->dx);
	head.y = (short)(game->body[0].y + game->dy);
	if (head.x < 0 || head.x >= GRID_W || head.y < 0 || head.y >= GRID_H ||
	    occupies(game, head.x, head.y)) {
		game->over = true;
		notice_score(game->score);
		return;
	}
	ate = head.x == game->food.x && head.y == game->food.y;
	if (ate && game->length < MAX_SNAKE)
		game->length++;
	for (i = game->length - 1; i > 0; i--)
		game->body[i] = game->body[i - 1];
	game->body[0] = head;
	if (ate) {
		game->score += 10;
		place_food(game);
	}
}

static void
fill(struct display *ui, unsigned long pixel, int x, int y, int w, int h)
{
	XSetForeground(ui->x, ui->gc, pixel);
	XFillRectangle(ui->x, ui->buffer, ui->gc, x, y,
	               (unsigned int)w, (unsigned int)h);
}

static void
draw(struct display *ui, const struct game *game)
{
	char title[64];
	int i;

	fill(ui, ui->dark, 0, 0, WIDTH, HEIGHT);
	fill(ui, ui->panel, 0, 0, WIDTH, HEADER);
	fill(ui, ui->cyan, 0, HEADER - 2, WIDTH, 2);
	XSetForeground(ui->x, ui->gc, ui->white);
	snprintf(title, sizeof(title), "SNAKE   Score %d%s%s", game->score,
	         game->paused ? "   PAUSED" : "",
	         game->over ? "   GAME OVER - ENTER restarts" : "");
	XDrawString(ui->x, ui->buffer, ui->gc, 7, 17, title,
	            (int)strlen(title));
	fill(ui, ui->cyan, game->food.x * CELL + 3,
	     HEADER + game->food.y * CELL + 3, CELL - 6, CELL - 6);
	for (i = game->length - 1; i >= 0; i--)
		fill(ui, i == 0 ? ui->white : ui->blue,
		     game->body[i].x * CELL + 1,
		     HEADER + game->body[i].y * CELL + 1,
		     CELL - 2, CELL - 2);
	XSetForeground(ui->x, ui->gc, ui->cyan);
	XDrawRectangle(ui->x, ui->buffer, ui->gc, 0, HEADER,
	               WIDTH - 1, GRID_H * CELL - 1);
	XCopyArea(ui->x, ui->buffer, ui->window, ui->gc,
	          0, 0, WIDTH, HEIGHT, 0, 0);
	XFlush(ui->x);
}

static void
direction(struct game *game, int dx, int dy)
{
	if (dx != -game->dx || dy != -game->dy) {
		game->next_dx = dx;
		game->next_dy = dy;
	}
}

static bool
handle_key(struct game *game, KeySym key)
{
	switch (key) {
	case XK_Up: case XK_w: case XK_W: case XK_k: direction(game, 0, -1); break;
	case XK_Down: case XK_s: case XK_S: case XK_j: direction(game, 0, 1); break;
	case XK_Left: case XK_a: case XK_A: case XK_h: direction(game, -1, 0); break;
	case XK_Right: case XK_d: case XK_D: case XK_l: direction(game, 1, 0); break;
	case XK_space: case XK_p: case XK_P: game->paused = !game->paused; break;
	case XK_Return: case XK_KP_Enter:
		if (game->over)
			reset_game(game);
		break;
	case XK_q: case XK_Q: case XK_Escape: return false;
	default: break;
	}
	return true;
}

int
main(void)
{
	struct display ui;
	struct game game;
	XSetWindowAttributes attributes;
	XSizeHints hints;
	XClassHint class_hint;
	bool running = true;

	memset(&ui, 0, sizeof(ui));
	ui.x = XOpenDisplay(NULL);
	if (ui.x == NULL) {
		fputs("snake: cannot open X display\n", stderr);
		return 1;
	}
	ui.screen = DefaultScreen(ui.x);
	ui.white = WhitePixel(ui.x, ui.screen);
	ui.dark = color(&ui, "#111111", BlackPixel(ui.x, ui.screen));
	ui.panel = color(&ui, "#242424", ui.dark);
	ui.cyan = color(&ui, "#0099cc", ui.white);
	ui.blue = color(&ui, "#225a70", ui.cyan);
	ui.font = XLoadQueryFont(ui.x, "fixed");

	memset(&attributes, 0, sizeof(attributes));
	attributes.background_pixel = ui.dark;
	attributes.border_pixel = ui.cyan;
	attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
	                        StructureNotifyMask;
	ui.window = XCreateWindow(ui.x, RootWindow(ui.x, ui.screen), 80, 22,
	                          WIDTH, HEIGHT, 1, CopyFromParent, InputOutput,
	                          CopyFromParent,
	                          CWBackPixel | CWBorderPixel | CWEventMask,
	                          &attributes);
	XStoreName(ui.x, ui.window, "Snake");
	class_hint.res_name = (char *)"snake";
	class_hint.res_class = (char *)"Snake";
	XSetClassHint(ui.x, ui.window, &class_hint);
	memset(&hints, 0, sizeof(hints));
	hints.flags = PSize | PMinSize | PMaxSize;
	hints.width = hints.min_width = hints.max_width = WIDTH;
	hints.height = hints.min_height = hints.max_height = HEIGHT;
	XSetWMNormalHints(ui.x, ui.window, &hints);
	ui.wm_delete = XInternAtom(ui.x, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(ui.x, ui.window, &ui.wm_delete, 1);
	ui.gc = XCreateGC(ui.x, ui.window, 0, NULL);
	if (ui.font != NULL)
		XSetFont(ui.x, ui.gc, ui.font->fid);
	ui.buffer = XCreatePixmap(ui.x, ui.window, WIDTH, HEIGHT,
	                          DefaultDepth(ui.x, ui.screen));
	XMapRaised(ui.x, ui.window);
	XSync(ui.x, False);
	XSetInputFocus(ui.x, ui.window, RevertToPointerRoot, CurrentTime);
	srand((unsigned int)(time(NULL) ^ getpid()));
	reset_game(&game);
	draw(&ui, &game);

	while (running) {
		fd_set readfds;
		struct timeval delay = { 0, 120000 };
		int fd = ConnectionNumber(ui.x);
		int selected;

		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		selected = select(fd + 1, &readfds, NULL, NULL, &delay);
		if (selected == 0) {
			step(&game);
			draw(&ui, &game);
		}
		while (running && XPending(ui.x)) {
			XEvent event;
			XNextEvent(ui.x, &event);
			if (event.type == Expose && event.xexpose.count == 0)
				draw(&ui, &game);
			else if (event.type == KeyPress)
				running = handle_key(&game, XLookupKeysym(&event.xkey, 0));
			else if (event.type == ButtonPress && game.over)
				reset_game(&game);
			else if (event.type == ClientMessage &&
			         (Atom)event.xclient.data.l[0] == ui.wm_delete)
				running = false;
		}
	}

	XFreePixmap(ui.x, ui.buffer);
	XFreeGC(ui.x, ui.gc);
	if (ui.font != NULL)
		XFreeFont(ui.x, ui.font);
	XDestroyWindow(ui.x, ui.window);
	XCloseDisplay(ui.x);
	return 0;
}
