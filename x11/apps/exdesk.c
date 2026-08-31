#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TASKBAR_HEIGHT 19
#define FILE_MAX_ENTRIES 128
#define FILE_ROW_HEIGHT 15
#define FILE_LIST_TOP 55

struct ui {
	Display *display;
	int screen;
	Window root;
	Window window;
	GC gc;
	XFontStruct *font;
	Atom wm_delete;
	unsigned long black;
	unsigned long white;
	unsigned long face;
	unsigned long light;
	unsigned long shadow;
	unsigned long dark;
	unsigned long navy;
	unsigned long desktop;
	unsigned long focus_text;
	int width;
	int height;
};

struct file_entry {
	char name[NAME_MAX + 1];
	bool directory;
	bool regular;
	bool link;
	off_t size;
};

struct file_state {
	char path[PATH_MAX];
	struct file_entry entries[FILE_MAX_ENTRIES];
	int count;
	int selected;
	int offset;
	Time last_click;
	int last_clicked;
	char message[96];
};

struct menu_item {
	const char *label;
	const char *command;
};

static const struct menu_item menu_items[] = {
	{ "File Manager", "exec /opt/x11/usr/bin/exfile /" },
	{ "Calculator", "exec /opt/x11/usr/bin/xcalc" },
	{ "Text Editor", "exec /opt/x11/usr/bin/xedit" },
	{ "Message Box", "exec /opt/x11/usr/bin/xmessage -center 'Hello from EX-word Linux'" },
	{ "Kill Window", "exec /opt/x11/usr/bin/xkill" },
	{ "Command Prompt", "exec /opt/x11/usr/bin/xterm -T 'Command Prompt' -tn xterm -fn fixed -bg '#242424' -fg white -cr '#0099cc' -geometry 64x20 -sl 64 +sb -ut" },
	{ "Task Manager", "exec /opt/x11/usr/bin/xterm -T 'Task Manager' -tn xterm -fn fixed -bg '#242424' -fg white -cr '#0099cc' -geometry 64x20 -sl 16 +sb -ut -e /usr/bin/top" },
	{ "System Information", "exec /opt/x11/usr/bin/xterm -T 'System Information' -tn xterm -fn fixed -bg '#242424' -fg white -cr '#0099cc' -geometry 64x20 -sl 8 +sb -ut -e /bin/sh -c '/opt/x11/usr/bin/screenfetch; echo; echo Press ENTER to close; read answer'" },
	{ "Disk Information", "exec /opt/x11/usr/bin/xterm -T 'Disk Information' -tn xterm -fn fixed -bg '#242424' -fg white -cr '#0099cc' -geometry 64x20 -sl 8 +sb -ut -e /bin/sh -c '/usr/bin/lsblk; echo; echo Press ENTER to close; read answer'" },
	{ "Snake", "/opt/x11/usr/bin/notify 'Starting Snake'; exec /opt/x11/usr/bin/snake" },
	{ "Clock", "exec /opt/x11/usr/bin/xclock" },
	{ "Eyes", "exec /opt/x11/usr/bin/xeyes" },
	{ "Close Menu", NULL },
};

static unsigned long
named_color(struct ui *ui, const char *name, unsigned long fallback)
{
	XColor exact;
	XColor color;

	if (XAllocNamedColor(ui->display, DefaultColormap(ui->display, ui->screen),
	                    name, &color, &exact))
		return color.pixel;
	return fallback;
}

static int
ui_init(struct ui *ui)
{
	memset(ui, 0, sizeof(*ui));
	ui->display = XOpenDisplay(NULL);
	if (ui->display == NULL) {
		fputs("exdesk: cannot open X display\n", stderr);
		return -1;
	}
	ui->screen = DefaultScreen(ui->display);
	ui->root = RootWindow(ui->display, ui->screen);
	ui->black = BlackPixel(ui->display, ui->screen);
	ui->white = WhitePixel(ui->display, ui->screen);
	ui->face = named_color(ui, "#242424", ui->black);
	ui->light = named_color(ui, "#0099cc", ui->white);
	ui->shadow = named_color(ui, "#111111", ui->black);
	ui->dark = named_color(ui, "#9e9e9e", ui->white);
	ui->navy = named_color(ui, "#303030", ui->black);
	ui->desktop = named_color(ui, "#222222", ui->black);
	ui->focus_text = named_color(ui, "#4cb7db", ui->white);
	ui->font = XLoadQueryFont(ui->display, "fixed");
	if (ui->font == NULL) {
		fputs("exdesk: fixed font is unavailable\n", stderr);
		XCloseDisplay(ui->display);
		return -1;
	}
	ui->gc = XCreateGC(ui->display, ui->root, 0, NULL);
	XSetFont(ui->display, ui->gc, ui->font->fid);
	ui->wm_delete = XInternAtom(ui->display, "WM_DELETE_WINDOW", False);
	return 0;
}

static void
ui_close(struct ui *ui)
{
	if (ui->display == NULL)
		return;
	if (ui->window != None)
		XDestroyWindow(ui->display, ui->window);
	if (ui->gc != None)
		XFreeGC(ui->display, ui->gc);
	if (ui->font != NULL)
		XFreeFont(ui->display, ui->font);
	XCloseDisplay(ui->display);
	ui->display = NULL;
}

static Window
make_window(struct ui *ui, const char *title, int width, int height,
	    bool fixed, bool menu)
{
	XSetWindowAttributes attributes;
	XSizeHints hints;
	XClassHint class_hint;
	int x;
	int y;

	ui->width = width;
	ui->height = height;
	x = menu ? 0 : (DisplayWidth(ui->display, ui->screen) - width) / 2;
	y = menu ? DisplayHeight(ui->display, ui->screen) - TASKBAR_HEIGHT - height
	         : (DisplayHeight(ui->display, ui->screen) - TASKBAR_HEIGHT - height) / 2;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	memset(&attributes, 0, sizeof(attributes));
	attributes.override_redirect = menu ? True : False;
	attributes.background_pixel = ui->face;
	attributes.border_pixel = ui->light;
	attributes.event_mask = ExposureMask | ButtonPressMask | KeyPressMask |
		                      PointerMotionMask | StructureNotifyMask;
	attributes.event_mask |= VisibilityChangeMask;
	ui->window = XCreateWindow(ui->display, ui->root, x, y,
	                           (unsigned int)width, (unsigned int)height, 1,
	                           CopyFromParent, InputOutput, CopyFromParent,
	                           CWOverrideRedirect | CWBackPixel | CWBorderPixel |
	                           CWEventMask,
	                           &attributes);
	XStoreName(ui->display, ui->window, title);
	class_hint.res_name = (char *)title;
	class_hint.res_class = (char *)"EXDesk";
	XSetClassHint(ui->display, ui->window, &class_hint);
	if (!menu)
		XSetWMProtocols(ui->display, ui->window, &ui->wm_delete, 1);
	memset(&hints, 0, sizeof(hints));
	hints.flags = PPosition | PSize;
	hints.x = x;
	hints.y = y;
	hints.width = width;
	hints.height = height;
	if (fixed) {
		hints.flags |= PMinSize | PMaxSize;
		hints.min_width = hints.max_width = width;
		hints.min_height = hints.max_height = height;
	}
	XSetWMNormalHints(ui->display, ui->window, &hints);
	XMapRaised(ui->display, ui->window);
	return ui->window;
}

static void
fill(struct ui *ui, Drawable drawable, unsigned long color,
	int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0)
		return;
	XSetForeground(ui->display, ui->gc, color);
	XFillRectangle(ui->display, drawable, ui->gc, x, y,
	               (unsigned int)width, (unsigned int)height);
}

static void
bevel(struct ui *ui, Drawable drawable, int x, int y,
	int width, int height, bool sunken)
{
	if (width < 2 || height < 2)
		return;
	XSetForeground(ui->display, ui->gc, sunken ? ui->light : ui->shadow);
	XDrawRectangle(ui->display, drawable, ui->gc, x, y,
	               (unsigned int)(width - 1),
	               (unsigned int)(height - 1));
}

static void
text_n(struct ui *ui, Drawable drawable, unsigned long color,
	int x, int baseline, const char *text, int length)
{
	if (length <= 0)
		return;
	XSetForeground(ui->display, ui->gc, color);
	XDrawString(ui->display, drawable, ui->gc, x, baseline, text, length);
}

static void
text(struct ui *ui, Drawable drawable, unsigned long color,
	int x, int baseline, const char *value)
{
	text_n(ui, drawable, color, x, baseline, value, (int)strlen(value));
}

static void
button(struct ui *ui, Drawable drawable, int x, int y,
	int width, int height, const char *label, bool pressed)
{
	int text_width = XTextWidth(ui->font, label, (int)strlen(label));
	int baseline = y + (height - (ui->font->ascent + ui->font->descent)) / 2 +
	               ui->font->ascent;

	fill(ui, drawable, pressed ? ui->navy : ui->face, x, y, width, height);
	bevel(ui, drawable, x, y, width, height, pressed);
	text(ui, drawable, pressed ? ui->focus_text : ui->white,
	     x + (width - text_width) / 2, baseline, label);
}

static bool
close_event(const struct ui *ui, const XEvent *event)
{
	return event->type == ClientMessage &&
	       (Atom)event->xclient.data.l[0] == ui->wm_delete;
}

static int
instance_lock(const char *name)
{
	char path[96];
	int fd;

	if (snprintf(path, sizeof(path), "/tmp/exdesk-%s.lock", name) >=
	    (int)sizeof(path))
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void
spawn_shell(int xfd, const char *command)
{
	pid_t child = fork();

	if (child < 0)
		return;
	if (child == 0) {
		pid_t grandchild = fork();
		if (grandchild < 0)
			_exit(127);
		if (grandchild > 0)
			_exit(0);
		if (xfd >= 0)
			close(xfd);
		(void)setsid();
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
	(void)waitpid(child, NULL, 0);
}

static void
spawn_viewer(struct ui *ui, const char *path)
{
	pid_t child = fork();

	if (child < 0)
		return;
	if (child == 0) {
		pid_t grandchild = fork();
		if (grandchild < 0)
			_exit(127);
		if (grandchild > 0)
			_exit(0);
		close(ConnectionNumber(ui->display));
		(void)setsid();
		execl("/opt/x11/usr/bin/xterm", "xterm",
		      "-T", "File Viewer", "-tn", "xterm", "-fn", "fixed",
		      "-bg", "#242424", "-fg", "white", "-cr", "#0099cc",
		      "-geometry", "64x20", "-sl", "32", "+sb", "-ut",
		      "-e", "/bin/sh", "-c",
		      "/usr/bin/head -n 200 \"$1\"; echo; "
		      "echo Press ENTER to close; read answer",
		      "sh", path, (char *)NULL);
		_exit(127);
	}
	(void)waitpid(child, NULL, 0);
}

static int
menu_index_at(int x, int y, int width)
{
	const int top = 4;
	const int row = 22;
	int index;

	if (x < 4 || x >= width - 4 || y < top)
		return -1;
	index = (y - top) / row;
	if (index < 0 || index >= (int)(sizeof(menu_items) / sizeof(menu_items[0])))
		return -1;
	return index;
}

static void
draw_menu(struct ui *ui, int hover)
{
	const int top = 4;
	const int row = 22;
	int index;

	fill(ui, ui->window, ui->face, 0, 0, ui->width, ui->height);
	bevel(ui, ui->window, 0, 0, ui->width, ui->height, false);
	for (index = 0;
	     index < (int)(sizeof(menu_items) / sizeof(menu_items[0]));
	     index++) {
		int y = top + index * row;
		bool selected = index == hover;
		fill(ui, ui->window, selected ? ui->navy : ui->face,
		     4, y, ui->width - 8, row);
		text(ui, ui->window, selected ? ui->focus_text : ui->white,
		     12, y + 15, menu_items[index].label);
	}
	XFlush(ui->display);
}

static int
run_menu(void)
{
	struct ui ui;
	XEvent event;
	const int width = 190;
	const int height = (int)(sizeof(menu_items) / sizeof(menu_items[0])) * 22 + 8;
	int hover = 0;
	int lock_fd = instance_lock("menu");
	const char *command = NULL;

	if (lock_fd < 0)
		return 0;
	if (ui_init(&ui) < 0) {
		close(lock_fd);
		return 1;
	}
	make_window(&ui, "Holo Menu", width, height, true, true);
	XSync(ui.display, False);
	if (XGrabPointer(ui.display, ui.window, False,
	                 ButtonPressMask | PointerMotionMask,
	                 GrabModeAsync, GrabModeAsync, None, None,
	                 CurrentTime) != GrabSuccess) {
		ui_close(&ui);
		close(lock_fd);
		return 0;
	}
	XGrabKeyboard(ui.display, ui.window, False,
	              GrabModeAsync, GrabModeAsync, CurrentTime);
	XSetInputFocus(ui.display, ui.window, RevertToPointerRoot, CurrentTime);
	draw_menu(&ui, hover);
	for (;;) {
		KeySym key;
		int index;

		XNextEvent(ui.display, &event);
		if (event.type == Expose && event.xexpose.count == 0)
			draw_menu(&ui, hover);
		else if (event.type == MotionNotify) {
			index = menu_index_at(event.xmotion.x, event.xmotion.y,
			                      ui.width);
			if (index != hover) {
				hover = index;
				draw_menu(&ui, hover);
			}
		} else if (event.type == ButtonPress) {
			index = menu_index_at(event.xbutton.x, event.xbutton.y,
			                      ui.width);
			if (event.xbutton.button == Button1 && index >= 0)
				command = menu_items[index].command;
			break;
		} else if (event.type == KeyPress) {
			key = XLookupKeysym(&event.xkey, 0);
			if (key == XK_Escape)
				break;
			if (key == XK_Up) {
				hover--;
				if (hover < 0)
					hover = (int)(sizeof(menu_items) / sizeof(menu_items[0])) - 1;
				draw_menu(&ui, hover);
			} else if (key == XK_Down) {
				hover++;
				if (hover >= (int)(sizeof(menu_items) / sizeof(menu_items[0])))
					hover = 0;
				draw_menu(&ui, hover);
			} else if (key == XK_Return || key == XK_KP_Enter) {
				if (hover >= 0) {
					command = menu_items[hover].command;
					break;
				}
			}
		}
	}
	XUngrabKeyboard(ui.display, CurrentTime);
	XUngrabPointer(ui.display, CurrentTime);
	ui_close(&ui);
	if (command != NULL)
		spawn_shell(-1, command);
	close(lock_fd);
	return 0;
}

static int
entry_compare(const void *left_value, const void *right_value)
{
	const struct file_entry *left = left_value;
	const struct file_entry *right = right_value;

	if (left->directory != right->directory)
		return left->directory ? -1 : 1;
	return strcasecmp(left->name, right->name);
}

static int
path_join(char *destination, size_t size, const char *directory,
	  const char *name)
{
	int result;

	if (strcmp(directory, "/") == 0)
		result = snprintf(destination, size, "/%s", name);
	else
		result = snprintf(destination, size, "%s/%s", directory, name);
	return result >= 0 && (size_t)result < size ? 0 : -1;
}

static void
load_directory(struct file_state *state)
{
	DIR *directory;
	struct dirent *entry;

	state->count = 0;
	state->selected = -1;
	state->offset = 0;
	state->last_clicked = -1;
	state->message[0] = '\0';
	directory = opendir(state->path);
	if (directory == NULL) {
		snprintf(state->message, sizeof(state->message),
		         "Cannot open: %s", strerror(errno));
		return;
	}
	while ((entry = readdir(directory)) != NULL &&
	       state->count < FILE_MAX_ENTRIES) {
		char full_path[PATH_MAX];
		struct stat status;
		struct file_entry *item;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		if (path_join(full_path, sizeof(full_path), state->path,
		              entry->d_name) < 0)
			continue;
		if (lstat(full_path, &status) < 0)
			continue;
		item = &state->entries[state->count++];
		strncpy(item->name, entry->d_name, sizeof(item->name) - 1);
		item->name[sizeof(item->name) - 1] = '\0';
		item->link = S_ISLNK(status.st_mode);
		if (item->link && stat(full_path, &status) < 0)
			memset(&status, 0, sizeof(status));
		item->directory = S_ISDIR(status.st_mode);
		item->regular = S_ISREG(status.st_mode);
		item->size = status.st_size;
	}
	closedir(directory);
	qsort(state->entries, (size_t)state->count,
	      sizeof(state->entries[0]), entry_compare);
	if (state->count == FILE_MAX_ENTRIES)
		strcpy(state->message, "Showing the first 128 entries");
}

static void
file_up(struct file_state *state)
{
	char *slash;

	if (strcmp(state->path, "/") == 0)
		return;
	slash = strrchr(state->path, '/');
	if (slash == state->path)
		state->path[1] = '\0';
	else if (slash != NULL)
		*slash = '\0';
	load_directory(state);
}

static void
file_set_path(struct file_state *state, const char *path)
{
	struct stat status;

	if (stat(path, &status) < 0 || !S_ISDIR(status.st_mode)) {
		snprintf(state->message, sizeof(state->message),
		         "Directory unavailable: %s", path);
		return;
	}
	strncpy(state->path, path, sizeof(state->path) - 1);
	state->path[sizeof(state->path) - 1] = '\0';
	load_directory(state);
}

static void
file_open_selected(struct ui *ui, struct file_state *state)
{
	char full_path[PATH_MAX];
	struct file_entry *entry;

	if (state->selected < 0 || state->selected >= state->count)
		return;
	entry = &state->entries[state->selected];
	if (path_join(full_path, sizeof(full_path), state->path,
	              entry->name) < 0) {
		strcpy(state->message, "Path is too long");
		return;
	}
	if (entry->directory)
		file_set_path(state, full_path);
	else if (!entry->regular)
		strcpy(state->message, "Special files cannot be opened");
	else if (entry->size > 131072)
		strcpy(state->message, "File is too large (128 KiB limit)");
	else
		spawn_viewer(ui, full_path);
}

static int
visible_rows(const struct ui *ui)
{
	int rows = (ui->height - FILE_LIST_TOP - 22) / FILE_ROW_HEIGHT;
	return rows > 1 ? rows : 1;
}

static void
keep_selection_visible(const struct ui *ui, struct file_state *state)
{
	int rows = visible_rows(ui);

	if (state->selected < state->offset)
		state->offset = state->selected;
	if (state->selected >= state->offset + rows)
		state->offset = state->selected - rows + 1;
	if (state->offset < 0)
		state->offset = 0;
}

static void
draw_file_manager(struct ui *ui, const struct file_state *state)
{
	char line[128];
	char title[PATH_MAX + 16];
	int rows = visible_rows(ui);
	int row;

	fill(ui, ui->window, ui->face, 0, 0, ui->width, ui->height);
	fill(ui, ui->window, ui->navy, 2, 2, ui->width - 4, 21);
	fill(ui, ui->window, ui->light, 2, 22, ui->width - 4, 1);
	snprintf(title, sizeof(title), "EX-File  %s", state->path);
	text_n(ui, ui->window, ui->focus_text, 7, 17, title,
	       (int)strnlen(title, (size_t)((ui->width - 14) / 6)));
	button(ui, ui->window, 4, 27, 48, 22, "Up", false);
	button(ui, ui->window, 56, 27, 48, 22, "Root", false);
	button(ui, ui->window, 108, 27, 48, 22, "mnt", false);
	button(ui, ui->window, 160, 27, 62, 22, "Refresh", false);
	button(ui, ui->window, ui->width - 56, 27, 52, 22, "Close", false);

	fill(ui, ui->window, ui->desktop, 4, FILE_LIST_TOP - 2,
	     ui->width - 8, rows * FILE_ROW_HEIGHT + 4);
	bevel(ui, ui->window, 3, FILE_LIST_TOP - 3,
	      ui->width - 6, rows * FILE_ROW_HEIGHT + 6, true);
	for (row = 0; row < rows; row++) {
		int index = state->offset + row;
		int y = FILE_LIST_TOP + row * FILE_ROW_HEIGHT;
		int max_chars = (ui->width - 18) / 6;

		if (index >= state->count)
			break;
		if (state->entries[index].directory)
			snprintf(line, sizeof(line), "[DIR] %s",
			         state->entries[index].name);
		else
			snprintf(line, sizeof(line), "%c %8lu  %s",
			         state->entries[index].link ? '@' : ' ',
			         (unsigned long)state->entries[index].size,
			         state->entries[index].name);
		if (index == state->selected)
			fill(ui, ui->window, ui->navy, 6, y,
			     ui->width - 12, FILE_ROW_HEIGHT);
		text_n(ui, ui->window,
		       index == state->selected ? ui->focus_text : ui->white,
		       9, y + 12, line,
		       (int)strnlen(line, (size_t)max_chars));
	}
	if (state->message[0] != '\0')
		text(ui, ui->window, ui->dark, 6, ui->height - 6,
		     state->message);
	else {
		snprintf(line, sizeof(line), "%d item%s  ENTER/double-click opens",
		         state->count, state->count == 1 ? "" : "s");
		text(ui, ui->window, ui->dark, 6, ui->height - 6, line);
	}
	XFlush(ui->display);
}

static int
run_file_manager(const char *initial_path)
{
	struct ui ui;
	struct file_state state;
	XEvent event;
	bool running = true;
	int lock_fd = instance_lock("file");

	if (lock_fd < 0)
		return 0;
	if (ui_init(&ui) < 0) {
		close(lock_fd);
		return 1;
	}
	memset(&state, 0, sizeof(state));
	state.selected = -1;
	state.last_clicked = -1;
	file_set_path(&state, initial_path != NULL ? initial_path : "/");
	make_window(&ui, "EX-File", 500, 276, true, false);
	draw_file_manager(&ui, &state);
	while (running) {
		XNextEvent(ui.display, &event);
		if (close_event(&ui, &event))
			break;
		if (event.type == Expose && event.xexpose.count == 0)
			draw_file_manager(&ui, &state);
		else if (event.type == ButtonPress) {
			int x = event.xbutton.x;
			int y = event.xbutton.y;
			if (event.xbutton.button == Button4) {
				if (state.offset > 0)
					state.offset--;
			} else if (event.xbutton.button == Button5) {
				if (state.offset + visible_rows(&ui) < state.count)
					state.offset++;
			} else if (y >= 27 && y < 49) {
				if (x >= 4 && x < 52)
					file_up(&state);
				else if (x >= 56 && x < 104)
					file_set_path(&state, "/");
				else if (x >= 108 && x < 156)
					file_set_path(&state, "/mnt");
				else if (x >= 160 && x < 222)
					load_directory(&state);
				else if (x >= ui.width - 56)
					running = false;
			} else if (x >= 4 && x < ui.width - 4 &&
			           y >= FILE_LIST_TOP &&
			           y < FILE_LIST_TOP +
			               visible_rows(&ui) * FILE_ROW_HEIGHT) {
				int index = state.offset +
				            (y - FILE_LIST_TOP) / FILE_ROW_HEIGHT;
				if (index >= 0 && index < state.count) {
					bool double_click = index == state.last_clicked &&
					                    event.xbutton.time - state.last_click < 500;
					bool enter_directory =
						event.xbutton.button == Button3 || double_click;
					enter_directory = enter_directory &&
					                  state.entries[index].directory;
					state.selected = index;
					if (double_click || event.xbutton.button == Button3)
						file_open_selected(&ui, &state);
					if (!enter_directory) {
						state.last_clicked = index;
						state.last_click = event.xbutton.time;
					}
				}
			}
			draw_file_manager(&ui, &state);
		} else if (event.type == KeyPress) {
			KeySym key = XLookupKeysym(&event.xkey, 0);
			int rows = visible_rows(&ui);
			if (key == XK_Escape)
				break;
			if (key == XK_Up && state.count > 0) {
				if (state.selected < 0)
					state.selected = 0;
				else if (state.selected > 0)
					state.selected--;
			} else if (key == XK_Down && state.count > 0) {
				if (state.selected < 0)
					state.selected = 0;
				else if (state.selected + 1 < state.count)
					state.selected++;
			} else if (key == XK_Prior && state.count > 0) {
				state.selected -= rows;
				if (state.selected < 0)
					state.selected = 0;
			} else if (key == XK_Next && state.count > 0) {
				state.selected += rows;
				if (state.selected >= state.count)
					state.selected = state.count - 1;
			} else if (key == XK_Home && state.count > 0)
				state.selected = 0;
			else if (key == XK_End && state.count > 0)
				state.selected = state.count - 1;
			else if (key == XK_Return || key == XK_KP_Enter)
				file_open_selected(&ui, &state);
			else if (key == XK_BackSpace)
				file_up(&state);
			else if (key == XK_F5)
				load_directory(&state);
			keep_selection_visible(&ui, &state);
			draw_file_manager(&ui, &state);
		}
	}
	ui_close(&ui);
	close(lock_fd);
	return 0;
}

static void
seven_segment(struct ui *ui, int x, int y, int digit)
{
	static const unsigned char pattern[10] = {
		0x3f, 0x06, 0x5b, 0x4f, 0x66,
		0x6d, 0x7d, 0x07, 0x7f, 0x6f
	};
	unsigned char bits = pattern[digit];
	const int width = 20;
	const int height = 38;
	const int thick = 3;

	if (bits & 0x01) fill(ui, ui->window, ui->light, x + 3, y, width - 6, thick);
	if (bits & 0x02) fill(ui, ui->window, ui->light, x + width - thick, y + 3, thick, height / 2 - 4);
	if (bits & 0x04) fill(ui, ui->window, ui->light, x + width - thick, y + height / 2 + 1, thick, height / 2 - 4);
	if (bits & 0x08) fill(ui, ui->window, ui->light, x + 3, y + height - thick, width - 6, thick);
	if (bits & 0x10) fill(ui, ui->window, ui->light, x, y + height / 2 + 1, thick, height / 2 - 4);
	if (bits & 0x20) fill(ui, ui->window, ui->light, x, y + 3, thick, height / 2 - 4);
	if (bits & 0x40) fill(ui, ui->window, ui->light, x + 3, y + height / 2 - 1, width - 6, thick);
}

static void
draw_clock(struct ui *ui)
{
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	char date[48];
	int values[6];
	int x = 30;
	int index;

	if (local == NULL)
		return;
	values[0] = local->tm_hour / 10;
	values[1] = local->tm_hour % 10;
	values[2] = local->tm_min / 10;
	values[3] = local->tm_min % 10;
	values[4] = local->tm_sec / 10;
	values[5] = local->tm_sec % 10;
	fill(ui, ui->window, ui->face, 0, 0, ui->width, ui->height);
	fill(ui, ui->window, ui->navy, 2, 2, ui->width - 4, 20);
	fill(ui, ui->window, ui->light, 2, 22, ui->width - 4, 1);
	text(ui, ui->window, ui->focus_text, 7, 16, "Clock");
	for (index = 0; index < 6; index++) {
		seven_segment(ui, x, 30, values[index]);
		x += 25;
		if (index == 1 || index == 3) {
			fill(ui, ui->window, ui->light, x, 40, 3, 3);
			fill(ui, ui->window, ui->light, x, 53, 3, 3);
			x += 10;
		}
	}
	strftime(date, sizeof(date), "%a %Y-%m-%d", local);
	text(ui, ui->window, ui->dark, 74, 84, date);
	XFlush(ui->display);
}

static unsigned long
integer_sqrt(unsigned long value)
{
	unsigned long result = 0;
	unsigned long bit = 1UL << (sizeof(unsigned long) * 8 - 2);

	while (bit > value)
		bit >>= 2;
	while (bit != 0) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}
		bit >>= 2;
	}
	return result;
}

static void
draw_eyes(struct ui *ui, bool force)
{
	static int previous_root_x;
	static int previous_root_y;
	static int previous_window_x;
	static int previous_window_y;
	static bool previous_valid;
	Window child;
	int root_x = 0;
	int root_y = 0;
	int window_x = 0;
	int window_y = 0;
	int ignored_x;
	int ignored_y;
	unsigned int mask;
	int eye;

	(void)XTranslateCoordinates(ui->display, ui->window, ui->root, 0, 0,
	                            &window_x, &window_y, &child);
	(void)XQueryPointer(ui->display, ui->root, &child, &child,
	                    &root_x, &root_y, &ignored_x, &ignored_y, &mask);
	if (!force && previous_valid && root_x == previous_root_x &&
	    root_y == previous_root_y && window_x == previous_window_x &&
	    window_y == previous_window_y)
		return;
	previous_root_x = root_x;
	previous_root_y = root_y;
	previous_window_x = window_x;
	previous_window_y = window_y;
	previous_valid = true;
	fill(ui, ui->window, ui->face, 0, 0, ui->width, ui->height);
	fill(ui, ui->window, ui->navy, 2, 2, ui->width - 4, 20);
	fill(ui, ui->window, ui->light, 2, 22, ui->width - 4, 1);
	text(ui, ui->window, ui->focus_text, 7, 16, "xeyes");
	for (eye = 0; eye < 2; eye++) {
		int center_x = eye == 0 ? 53 : 127;
		int center_y = 61;
		long dx = root_x - (window_x + center_x);
		long dy = root_y - (window_y + center_y);
		unsigned long distance = integer_sqrt((unsigned long)(dx * dx + dy * dy));
		int pupil_x = center_x;
		int pupil_y = center_y;

		if (distance > 0) {
			pupil_x += (int)(dx * 14 / (long)distance);
			pupil_y += (int)(dy * 10 / (long)distance);
		}
		XSetForeground(ui->display, ui->gc, ui->white);
		XFillArc(ui->display, ui->window, ui->gc,
		         center_x - 28, center_y - 25, 56, 50, 0, 360 * 64);
		XSetForeground(ui->display, ui->gc, ui->black);
		XDrawArc(ui->display, ui->window, ui->gc,
		         center_x - 28, center_y - 25, 56, 50, 0, 360 * 64);
		XFillArc(ui->display, ui->window, ui->gc,
		         pupil_x - 7, pupil_y - 7, 14, 14, 0, 360 * 64);
	}
	XFlush(ui->display);
}

static int
run_timed_window(const char *mode)
{
	struct ui ui;
	bool clock_mode = strcmp(mode, "xclock") == 0;
	bool running = true;
	bool redraw = true;
	bool obscured = false;
	int lock_fd = instance_lock(clock_mode ? "clock" : "eyes");

	if (lock_fd < 0)
		return 0;
	if (ui_init(&ui) < 0) {
		close(lock_fd);
		return 1;
	}
	make_window(&ui, clock_mode ? "Clock" : "Eyes",
	            clock_mode ? 220 : 180, clock_mode ? 92 : 96,
	            true, false);
	while (running) {
		fd_set descriptors;
		struct timeval timeout;
		int fd = ConnectionNumber(ui.display);

		while (XPending(ui.display)) {
			XEvent event;
			XNextEvent(ui.display, &event);
			if (close_event(&ui, &event))
				running = false;
			else if (event.type == Expose && event.xexpose.count == 0)
				redraw = true;
			else if (event.type == VisibilityNotify) {
				obscured = event.xvisibility.state == VisibilityFullyObscured;
				if (!obscured)
					redraw = true;
			}
			else if (event.type == KeyPress &&
			         XLookupKeysym(&event.xkey, 0) == XK_Escape)
				running = false;
		}
		if (!running)
			break;
		if (!obscured) {
			if (clock_mode)
				draw_clock(&ui);
			else
				draw_eyes(&ui, redraw);
		}
		redraw = false;
		FD_ZERO(&descriptors);
		FD_SET(fd, &descriptors);
		timeout.tv_sec = clock_mode ? 1 : 0;
		timeout.tv_usec = clock_mode ? 0 : 200000;
		(void)select(fd + 1, &descriptors, NULL, NULL, &timeout);
	}
	ui_close(&ui);
	close(lock_fd);
	return 0;
}

int
main(int argc, char **argv)
{
	const char *mode = strrchr(argv[0], '/');

	mode = mode != NULL ? mode + 1 : argv[0];
	if (strcmp(mode, "exmenu") == 0)
		return run_menu();
	if (strcmp(mode, "exfile") == 0)
		return run_file_manager(argc > 1 ? argv[1] : "/");
	if (strcmp(mode, "xclock") == 0 || strcmp(mode, "xeyes") == 0)
		return run_timed_window(mode);
	fprintf(stderr, "usage: exmenu | exfile [directory] | xclock | xeyes\n");
	return 2;
}
