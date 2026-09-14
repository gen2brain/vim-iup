/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved		by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

#include "vim.h"

#undef K_BS
#undef K_DEL
#undef K_DOWN
#undef K_END
#undef K_ESC
#undef K_F1
#undef K_F2
#undef K_F3
#undef K_F4
#undef K_F5
#undef K_F6
#undef K_F7
#undef K_F8
#undef K_F9
#undef K_F10
#undef K_F11
#undef K_F12
#undef K_F13
#undef K_F14
#undef K_F15
#undef K_F16
#undef K_F17
#undef K_F18
#undef K_F19
#undef K_F20
#undef K_HELP
#undef K_HOME
#undef K_INS
#undef K_LEFT
#undef K_RIGHT
#undef K_TAB
#undef K_UP

#include <iup.h>
#include <iupdraw.h>
#include <iupkey.h>

static struct
{
    Ihandle	*dialog;
    Ihandle	*topbox;
    Ihandle	*toolbar;
    Ihandle	*tabbar;
    Ihandle	*formbox;
    Ihandle	*canvas;
    Ihandle	*menubar;
    Ihandle	*wait_timer;
    Ihandle	*blink_timer;
    Ihandle	*close_timer;
    int		ignore_tabline;

    int		in_draw_action;
    int		flash;
    int		dirty;
    int		dirty_x1;
    int		dirty_y1;
    int		dirty_x2;
    int		dirty_y2;

    guicolor_T	fg;
    guicolor_T	bg;
    guicolor_T	sp;
    int		draw_flags;

    char	font_face[128];
    int		font_size;
    int		font_bold;
    int		font_italic;

    int		blink_state;
    long	blink_wait;
    long	blink_on;
    long	blink_off;

    int		timed_out;
    int		mouse_hidden;
} giup;

static Ihandle *iup_clipboard = NULL;

#define BLINK_NONE  0
#define BLINK_ON    1
#define BLINK_OFF   2

static struct
{
    int		key_sym;
    char_u	vim_code0;
    char_u	vim_code1;
} special_keys[] =
{
    {K_UP,	    'k', 'u'},
    {K_DOWN,	    'k', 'd'},
    {K_LEFT,	    'k', 'l'},
    {K_RIGHT,	    'k', 'r'},

    {K_F1,	    'k', '1'},
    {K_F2,	    'k', '2'},
    {K_F3,	    'k', '3'},
    {K_F4,	    'k', '4'},
    {K_F5,	    'k', '5'},
    {K_F6,	    'k', '6'},
    {K_F7,	    'k', '7'},
    {K_F8,	    'k', '8'},
    {K_F9,	    'k', '9'},
    {K_F10,	    'k', ';'},
    {K_F11,	    'F', '1'},
    {K_F12,	    'F', '2'},
    {K_F13,	    'F', '3'},
    {K_F14,	    'F', '4'},
    {K_F15,	    'F', '5'},
    {K_F16,	    'F', '6'},
    {K_F17,	    'F', '7'},
    {K_F18,	    'F', '8'},
    {K_F19,	    'F', '9'},
    {K_F20,	    'F', 'A'},

    {K_HELP,	    '%', '1'},
    {K_BS,	    'k', 'b'},
    {K_INS,	    'k', 'I'},
    {K_DEL,	    'k', 'D'},
    {K_HOME,	    'k', 'h'},
    {K_END,	    '@', '7'},
    {K_PGUP,	    'k', 'P'},
    {K_PGDN,	    'k', 'N'},

    {K_KP_LEFT,	    'k', 'l'},
    {K_KP_RIGHT,    'k', 'r'},
    {K_KP_UP,	    'k', 'u'},
    {K_KP_DOWN,	    'k', 'd'},
    {K_KP_INS,	    KS_EXTRA, (char_u)KE_KINS},
    {K_KP_DEL,	    KS_EXTRA, (char_u)KE_KDEL},
    {K_KP_HOME,	    'K', '1'},
    {K_KP_END,	    'K', '4'},
    {K_KP_PGUP,	    'K', '3'},
    {K_KP_PGDN,	    'K', '5'},
    {K_KP_PLUS,	    'K', '6'},
    {K_KP_MINUS,    'K', '7'},
    {K_KP_DIV,	    'K', '8'},
    {K_KP_MULT,	    'K', '9'},
    {K_KP_CR,	    'K', 'A'},
    {K_KP_DECIMAL,  'K', 'B'},
    {K_KP_0,	    'K', 'C'},
    {K_KP_1,	    'K', 'D'},
    {K_KP_2,	    'K', 'E'},
    {K_KP_3,	    'K', 'F'},
    {K_KP_4,	    'K', 'G'},
    {K_KP_5,	    'K', 'H'},
    {K_KP_6,	    'K', 'I'},
    {K_KP_7,	    'K', 'J'},
    {K_KP_8,	    'K', 'K'},
    {K_KP_9,	    'K', 'L'},

    {K_CR,	    CAR, NUL},
    {K_SP,	    ' ', NUL},
    {K_TAB,	    TAB, NUL},
    {K_ESC,	    ESC, NUL},

    {0,		    0, 0}
};

    static void
iup_set_draw_color(guicolor_T color)
{
    if (giup.flash)
	color ^= 0xFFFFFF;
    IupSetStrf(giup.canvas, "DRAWCOLOR", "%d %d %d",
	    (int)((color >> 16) & 0xFF),
	    (int)((color >> 8) & 0xFF),
	    (int)(color & 0xFF));
}

    static void
iup_set_draw_font(int flags)
{
    IupSetStrf(giup.canvas, "DRAWFONT", "%s, %s%s%d",
	    giup.font_face,
	    (giup.font_bold || (flags & DRAW_BOLD)) ? "Bold " : "",
	    (giup.font_italic || (flags & DRAW_ITALIC)) ? "Italic " : "",
	    giup.font_size);
}

    static void
iup_invalidate(int x1, int y1, int x2, int y2)
{
    if (giup.in_draw_action)
	return;
    if (giup.dirty)
    {
	if (x1 < giup.dirty_x1) giup.dirty_x1 = x1;
	if (y1 < giup.dirty_y1) giup.dirty_y1 = y1;
	if (x2 > giup.dirty_x2) giup.dirty_x2 = x2;
	if (y2 > giup.dirty_y2) giup.dirty_y2 = y2;
    }
    else
    {
	giup.dirty_x1 = x1;
	giup.dirty_y1 = y1;
	giup.dirty_x2 = x2;
	giup.dirty_y2 = y2;
	giup.dirty = TRUE;
    }
}

    static void
iup_invalidate_all(void)
{
    iup_invalidate(0, 0, INT_MAX / 2, INT_MAX / 2);
}

    static int
iup_canvas_action_cb(Ihandle *ih)
{
    int	    w, h;
    int	    x1, y1, x2, y2;
    char    *clip;

    if (gui.starting || ScreenLines == NULL)
	return IUP_DEFAULT;

    IupDrawBegin(ih);
    giup.in_draw_action = TRUE;

    IupDrawGetSize(ih, &w, &h);

    clip = IupGetAttribute(ih, "CLIPRECT");
    if (clip == NULL || sscanf(clip, "%d %d %d %d", &x1, &y1, &x2, &y2) != 4)
    {
	x1 = 0;
	y1 = 0;
	x2 = w - 1;
	y2 = h - 1;
    }
    else
    {
	x1 -= gui.char_width;
	x2 += gui.char_width;
	y1 -= gui.char_height;
	y2 += gui.char_height;
    }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > w - 1) x2 = w - 1;
    if (y2 > h - 1) y2 = h - 1;

    iup_set_draw_color(gui.back_pixel);
    IupSetAttribute(ih, "DRAWSTYLE", "FILL");
    IupDrawRectangle(ih, x1, y1, x2, y2);

    gui_redraw(x1, y1, x2 - x1 + 1, y2 - y1 + 1);

    if (giup.blink_state == BLINK_OFF)
	gui_undraw_cursor();

    giup.in_draw_action = FALSE;
    IupDrawEnd(ih);

    return IUP_DEFAULT;
}

    static int
iup_chrome_height(void)
{
    int	    h = 0;
    int	    w, ch;

    if (giup.toolbar != NULL && IupGetInt(giup.toolbar, "VISIBLE"))
    {
	if (sscanf(IupGetAttribute(giup.toolbar, "RASTERSIZE"),
						      "%dx%d", &w, &ch) == 2)
	    h += ch;
    }
    if (giup.tabbar != NULL && IupGetInt(giup.tabbar, "VISIBLE"))
    {
	int cw, cch;

	if (sscanf(IupGetAttribute(giup.tabbar, "RASTERSIZE"),
						      "%dx%d", &w, &ch) == 2
		&& sscanf(IupGetAttribute(giup.tabbar, "CLIENTSIZE"),
						      "%dx%d", &cw, &cch) == 2)
	    h += ch - cch;
    }
    return h;
}

    static int
iup_canvas_resize_cb(Ihandle *ih UNUSED, int width, int height)
{
    if (gui.shell_created)
	gui_resize_shell(width, height - iup_chrome_height());
    return IUP_DEFAULT;
}

#ifdef FEAT_DND
    static int
iup_canvas_dropdata_cb(Ihandle *ih UNUSED, char *type UNUSED, void *data,
					   int size, int x UNUSED, int y UNUSED)
{
    char_u  dropkey[6] = {CSI, KS_MODIFIER, 0, CSI, KS_EXTRA, (char_u)KE_DROP};
    char_u  *text = (char_u *)data;
    char_u  *tmpbuf = NULL;
    int	    len = size;
    char    *mod;

    if (text == NULL || len <= 0)
	return IUP_DEFAULT;

    if (input_conv.vc_type != CONV_NONE)
    {
	tmpbuf = string_convert(&input_conv, text, &len);
	if (tmpbuf != NULL)
	    text = tmpbuf;
    }

    dnd_yank_drag_data(text, (long)len);
    vim_free(tmpbuf);

    mod = IupGetGlobal("MODKEYSTATE");
    if (mod != NULL)
    {
	if (mod[0] == 'S')
	    dropkey[2] |= MOD_MASK_SHIFT;
	if (mod[1] == 'C')
	    dropkey[2] |= MOD_MASK_CTRL;
	if (mod[2] == 'A')
	    dropkey[2] |= MOD_MASK_ALT;
    }

    if (dropkey[2] != 0)
	add_to_input_buf(dropkey, (int)sizeof(dropkey));
    else
	add_to_input_buf(dropkey + 3, (int)(sizeof(dropkey) - 3));

    return IUP_DEFAULT;
}
#endif

#ifdef HAVE_DROP_FILE
static garray_T iup_drop_files = {0, 0, sizeof(char_u *), 10, NULL};

    static int
iup_canvas_dropfiles_cb(Ihandle *ih UNUSED, char *filename, int num,
							       int x, int y)
{
    char_u  *name = vim_strsave((char_u *)filename);

    if (name != NULL && ga_grow(&iup_drop_files, 1) == OK)
	((char_u **)iup_drop_files.ga_data)[iup_drop_files.ga_len++] = name;
    else
	vim_free(name);

    if (num == 0 && iup_drop_files.ga_len > 0)
    {
	int	count = iup_drop_files.ga_len;
	char_u	**fnames = ALLOC_MULT(char_u *, count);

	if (fnames != NULL)
	{
	    mch_memmove(fnames, iup_drop_files.ga_data,
					     count * sizeof(char_u *));
	    gui_handle_drop(x, y, 0, fnames, count);
	    ga_clear(&iup_drop_files);
	}
	else
	    ga_clear_strings(&iup_drop_files);
	ga_init2(&iup_drop_files, sizeof(char_u *), 10);
    }
    return IUP_DEFAULT;
}
#endif

    static int
iup_close_timer_cb(Ihandle *ih UNUSED)
{
    IupSetAttribute(giup.close_timer, "RUN", "NO");
    gui_shell_closed();
    return IUP_DEFAULT;
}

    static int
iup_dialog_close_cb(Ihandle *ih UNUSED)
{
    if (giup.close_timer == NULL)
    {
	giup.close_timer = IupTimer();
	IupSetCallback(giup.close_timer, "ACTION_CB",
					   (Icallback)iup_close_timer_cb);
	IupSetAttribute(giup.close_timer, "TIME", "1");
    }
    IupSetAttribute(giup.close_timer, "RUN", "YES");
    return IUP_IGNORE;
}

    static int
iup_canvas_focus_cb(Ihandle *ih UNUSED, int focus)
{
    gui_focus_change(focus);
    return IUP_DEFAULT;
}

    static int
iup_wait_timer_cb(Ihandle *ih UNUSED)
{
    giup.timed_out = TRUE;
    return IUP_DEFAULT;
}

#ifdef FEAT_BEVAL_GUI
static BalloonEval  *iup_beval = NULL;
static Ihandle	    *beval_popover = NULL;
static Ihandle	    *beval_label = NULL;
static Ihandle	    *beval_timer = NULL;
static int	    beval_enabled = FALSE;

    static void
iup_beval_cancel(void)
{
    if (beval_timer != NULL)
	IupSetAttribute(beval_timer, "RUN", "NO");
    if (iup_beval != NULL && iup_beval->showState == ShS_SHOWING)
	gui_mch_unpost_balloon(iup_beval);
}

    static int
iup_beval_timer_cb(Ihandle *ih UNUSED)
{
    IupSetAttribute(beval_timer, "RUN", "NO");
    if (beval_enabled && p_beval && iup_beval != NULL
						&& iup_beval->msgCB != NULL)
	(*iup_beval->msgCB)(iup_beval, 0);
    return IUP_DEFAULT;
}

    static void
iup_beval_motion(int x, int y)
{
    if (iup_beval == NULL)
	return;
    if (iup_beval->showState == ShS_SHOWING)
	gui_mch_unpost_balloon(iup_beval);
    if (!beval_enabled || !p_beval)
	return;
    iup_beval->x = x;
    iup_beval->y = y;
    if (beval_timer == NULL)
    {
	beval_timer = IupTimer();
	IupSetCallback(beval_timer, "ACTION_CB",
					     (Icallback)iup_beval_timer_cb);
    }
    IupSetAttribute(beval_timer, "RUN", "NO");
    IupSetInt(beval_timer, "TIME", p_bdlay > 0 ? (int)p_bdlay : 600);
    IupSetAttribute(beval_timer, "RUN", "YES");
}

    BalloonEval *
gui_mch_create_beval_area(void *target UNUSED, char_u *mesg,
		       void (*mesgCB)(BalloonEval *, int), void *clientData)
{
    BalloonEval	*beval = ALLOC_CLEAR_ONE(BalloonEval);

    if (beval != NULL)
    {
	beval->msg = mesg;
	beval->msgCB = mesgCB;
	beval->clientData = clientData;
	beval->showState = ShS_NEUTRAL;
	iup_beval = beval;
    }
    return beval;
}

    void
gui_mch_destroy_beval_area(BalloonEval *beval)
{
    iup_beval_cancel();
    if (beval == iup_beval)
	iup_beval = NULL;
# ifdef FEAT_VARTABS
    vim_free(beval->vts);
# endif
    vim_free(beval);
}

    void
gui_mch_enable_beval_area(BalloonEval *beval)
{
    if (beval != NULL)
	beval_enabled = TRUE;
}

    void
gui_mch_disable_beval_area(BalloonEval *beval UNUSED)
{
    beval_enabled = FALSE;
    iup_beval_cancel();
}

    BalloonEval *
gui_mch_currently_showing_beval(void)
{
    if (iup_beval != NULL && iup_beval->showState == ShS_SHOWING)
	return iup_beval;
    return NULL;
}

    void
gui_mch_post_balloon(BalloonEval *beval, char_u *mesg)
{
    int	    w, h;

    if (mesg == NULL)
    {
	gui_mch_unpost_balloon(beval);
	return;
    }
    if (beval_popover == NULL)
    {
	beval_label = IupLabel(NULL);
	IupSetAttribute(beval_label, "PADDING", "4x2");
	beval_popover = IupPopover(beval_label);
	IupSetAttributeHandle(beval_popover, "ANCHOR", giup.canvas);
	IupSetAttribute(beval_popover, "AUTOHIDE", "NO");
	IupSetAttribute(beval_popover, "POSITION", "BOTTOMLEFT");
    }
    IupSetStrf(beval_label, "TITLE", "%s", (char *)mesg);
    IupGetIntInt(giup.canvas, "DRAWSIZE", &w, &h);
    IupSetAttribute(beval_popover, "VISIBLE", "NO");
    IupSetInt(beval_popover, "OFFSETX", beval->x + EVAL_OFFSET_X);
    IupSetInt(beval_popover, "OFFSETY", beval->y + EVAL_OFFSET_Y - h);
    IupSetAttribute(beval_popover, "VISIBLE", "YES");
    beval->showState = ShS_SHOWING;
}

    void
gui_mch_unpost_balloon(BalloonEval *beval)
{
    if (beval_popover != NULL)
	IupSetAttribute(beval_popover, "VISIBLE", "NO");
    if (beval != NULL)
	beval->showState = ShS_NEUTRAL;
}
#endif

    static int
iup_canvas_textinput_cb(Ihandle *ih UNUSED, char *value)
{
    char_u	buf[128];
    char_u	*str = (char_u *)value;
    char_u	*conv_str = NULL;
    char_u	*p;
    int		len;

#ifdef FEAT_BEVAL_GUI
    iup_beval_cancel();
#endif

    if (value == NULL || *value == NUL)
	return IUP_IGNORE;

    if (p_mh && !giup.mouse_hidden)
	gui_mch_mousehide(TRUE);

    len = (int)STRLEN(value);
    if (input_conv.vc_type != CONV_NONE)
    {
	conv_str = string_convert(&input_conv, str, &len);
	if (conv_str != NULL)
	    str = conv_str;
    }

    if (len <= (int)sizeof(buf) / 3)
    {
	mch_memmove(buf, str, (size_t)len);
	len = fix_input_buffer(buf, len);
	add_to_input_buf(buf, len);
    }
    else
    {
	p = alloc(len * 3 + 3);
	if (p != NULL)
	{
	    mch_memmove(p, str, (size_t)len);
	    len = fix_input_buffer(p, len);
	    add_to_input_buf(p, len);
	    vim_free(p);
	}
    }

    vim_free(conv_str);
    return IUP_IGNORE;
}

    static int
iup_canvas_key_cb(Ihandle *ih UNUSED, int c)
{
    char_u	string[16];
    int		base = iup_XkeyBase(c);
    int		ch = 0;
    int		modifiers = 0;
    int		len = 0;
    int		i;

#ifdef FEAT_BEVAL_GUI
    iup_beval_cancel();
#endif

    if (p_mh && !giup.mouse_hidden)
	gui_mch_mousehide(TRUE);

    if (iup_isCtrlXkey(c))
	modifiers |= MOD_MASK_CTRL;
    if (iup_isAltXkey(c))
	modifiers |= MOD_MASK_ALT;
    if (iup_isShiftXkey(c))
	modifiers |= MOD_MASK_SHIFT;

#ifdef FEAT_MENU
    if (modifiers == MOD_MASK_ALT && gui.menu_is_active && base < 0x80
	    && (*p_wak == 'y'
		|| (*p_wak == 'm' && gui_is_menu_shortcut(base))))
	return IUP_CONTINUE;
#endif

    for (i = 0; special_keys[i].key_sym != 0; i++)
	if (special_keys[i].key_sym == base)
	    break;

    if (special_keys[i].key_sym != 0)
    {
	if (special_keys[i].vim_code1 == NUL)
	    ch = special_keys[i].vim_code0;
	else
	    ch = TO_SPECIAL(special_keys[i].vim_code0,
					       special_keys[i].vim_code1);
    }
    else
    {
	if (!(modifiers & (MOD_MASK_CTRL | MOD_MASK_ALT)) || base > 0xFF)
	    return IUP_CONTINUE;

	ch = base;
	if (SAFE_isupper(ch) && !(modifiers & MOD_MASK_SHIFT))
	    ch = TOLOWER_ASC(ch);
    }

    ch = simplify_key(ch, &modifiers);
    if (!IS_SPECIAL(ch))
    {
	ch = may_adjust_key_for_ctrl(modifiers, ch);
	modifiers = may_remove_shift_modifier(modifiers, ch);
    }
    if (modifiers)
    {
	string[len++] = CSI;
	string[len++] = KS_MODIFIER;
	string[len++] = modifiers;
    }

    if (IS_SPECIAL(ch))
    {
	string[len++] = CSI;
	string[len++] = K_SECOND(ch);
	string[len++] = K_THIRD(ch);
    }
    else
	len += mb_char2bytes(ch, string + len);

    {
	int int_ch = check_for_interrupt(ch, modifiers);

	if (int_ch != NUL)
	{
	    ch = int_ch;
	    string[0] = ch;
	    len = 1;
	    trash_input_buf();
	}
    }

    if (len == 1 && string[0] == CSI)
    {
	string[len++] = KS_EXTRA;
	string[len++] = KE_CSI;
    }

    if (len > 0)
    {
	add_to_input_buf(string, len);
	return IUP_IGNORE;
    }

    return IUP_CONTINUE;
}

    static int
iup_mouse_modifiers(char *status)
{
    int	    modifiers = 0;

    if (iup_isshift(status))
	modifiers |= MOUSE_SHIFT;
    if (iup_iscontrol(status))
	modifiers |= MOUSE_CTRL;
    if (iup_isalt(status))
	modifiers |= MOUSE_ALT;
    return modifiers;
}

    static int
iup_canvas_button_cb(Ihandle *ih UNUSED, int button, int pressed,
						  int x, int y, char *status)
{
    int	    vim_button;

#ifdef FEAT_BEVAL_GUI
    iup_beval_cancel();
#endif

    gui_mch_mousehide(FALSE);

    if (pressed && !gui.in_focus)
	IupSetFocus(giup.canvas);

    switch (button)
    {
	case IUP_BUTTON1: vim_button = MOUSE_LEFT; break;
	case IUP_BUTTON2: vim_button = MOUSE_MIDDLE; break;
	case IUP_BUTTON3: vim_button = MOUSE_RIGHT; break;
	default: return IUP_DEFAULT;
    }

    if (!pressed)
	vim_button = MOUSE_RELEASE;

    gui_send_mouse_event(vim_button, x, y, iup_isdouble(status),
					       iup_mouse_modifiers(status));
    return IUP_DEFAULT;
}

    static int
iup_canvas_motion_cb(Ihandle *ih UNUSED, int x, int y, char *status)
{
    gui_mch_mousehide(FALSE);

#ifdef FEAT_BEVAL_GUI
    iup_beval_motion(x, y);
#endif

    if (iup_isbutton1(status) || iup_isbutton2(status)
						   || iup_isbutton3(status))
	gui_send_mouse_event(MOUSE_DRAG, x, y, FALSE,
					       iup_mouse_modifiers(status));
    else
	gui_mouse_moved(x, y);
    return IUP_DEFAULT;
}

    static int
iup_canvas_wheel_cb(Ihandle *ih UNUSED, float delta, int x, int y,
							       char *status)
{
    int	    button = delta > 0 ? MOUSE_4 : MOUSE_5;
    int	    n = (int)(delta < 0 ? -delta : delta);
    int	    i;

    if (n == 0)
	n = 1;
    for (i = 0; i < n; i++)
	gui_send_mouse_event(button, x, y, FALSE,
					       iup_mouse_modifiers(status));
    return IUP_DEFAULT;
}

#ifdef FEAT_GUI_TABLINE
    static Ihandle *
iup_tabline_page(int nr)
{
    Ihandle *page = IupGetChild(giup.tabbar, nr);

    if (page == NULL)
    {
	page = IupVbox(NULL);
	IupAppend(giup.tabbar, page);
	IupMap(page);
    }
    return page;
}

    static void
iup_place_formbox(Ihandle *parent)
{
    if (parent == NULL || IupGetParent(giup.formbox) == parent)
	return;
    IupReparent(giup.formbox, parent, NULL);
    IupRefresh(giup.topbox);
    IupSetFocus(giup.canvas);
}

    static int
iup_tabline_change_cb(Ihandle *ih UNUSED, int new_pos, int old_pos UNUSED)
{
    if (!giup.ignore_tabline)
	send_tabline_event(new_pos + 1);
    return IUP_DEFAULT;
}

    static int
iup_tabline_reorder_cb(Ihandle *ih UNUSED, int old_pos UNUSED, int new_pos)
{
    if (giup.ignore_tabline)
	return IUP_DEFAULT;
    if (tabpage_index(curtab) - 1 < new_pos)
	tabpage_move(new_pos + 1);
    else
	tabpage_move(new_pos);
    return IUP_DEFAULT;
}

    static int
iup_tabline_close_cb(Ihandle *ih UNUSED, int pos)
{
    send_tabline_menu_event(pos + 1, TABLINE_MENU_CLOSE);
    return IUP_IGNORE;
}

    static int
iup_tabline_rclick_cb(Ihandle *ih UNUSED, int pos)
{
    send_tabline_menu_event(pos + 1, TABLINE_MENU_NEW);
    return IUP_DEFAULT;
}
#endif

    static void
iup_push_updates(void)
{
    if (giup.dirty && giup.canvas != NULL && !giup.in_draw_action)
    {
	int w = 0, h = 0;

	IupGetIntInt(giup.canvas, "DRAWSIZE", &w, &h);

	giup.dirty_x1 = 0;
	giup.dirty_x2 = w - 1;
	if (giup.dirty_y1 < 0) giup.dirty_y1 = 0;
	if (giup.dirty_y2 > h - 1) giup.dirty_y2 = h - 1;

	if (giup.dirty_x2 >= giup.dirty_x1 && giup.dirty_y2 >= giup.dirty_y1)
	    IupSetStrf(giup.canvas, "UPDATERECT", "%d %d %d %d",
		    giup.dirty_x1, giup.dirty_y1,
		    giup.dirty_x2, giup.dirty_y2);

	giup.dirty = FALSE;
    }
}

    void
gui_mch_update(void)
{
    iup_push_updates();
    if (!vim_is_input_buf_full())
	IupFlush();
}

    int
gui_mch_wait_for_chars(long wtime)
{
    int		focus;
    long	remaining = wtime;
#ifdef ELAPSED_FUNC
    elapsed_T	start_tv;

    ELAPSED_INIT(start_tv);
#endif

    focus = gui.in_focus;
    for (;;)
    {
	long	timeout = remaining;

	if (gui.in_focus != focus)
	{
	    if (gui.in_focus)
		gui_mch_start_blink();
	    else
		gui_mch_stop_blink(TRUE);
	    focus = gui.in_focus;
	}

	iup_push_updates();

#ifdef MESSAGE_QUEUE
# ifdef FEAT_TIMERS
	did_add_timer = FALSE;
# endif
	parse_queued_messages();
# ifdef FEAT_TIMERS
	if (did_add_timer)
	    // Need to recompute the waiting time.
	    return FAIL;
# endif
# ifdef FEAT_JOB_CHANNEL
	if (has_any_channel() && (wtime < 0 || timeout > 20))
	    timeout = 20;
# endif
#endif

	if (input_available())
	    return OK;

	giup.timed_out = FALSE;
	if (timeout >= 0)
	{
	    IupSetInt(giup.wait_timer, "TIME", timeout > 0 ? (int)timeout : 1);
	    IupSetAttribute(giup.wait_timer, "RUN", "YES");
	}

	IupLoopStepWait();

	if (timeout >= 0)
	    IupSetAttribute(giup.wait_timer, "RUN", "NO");

	if (input_available())
	    return OK;

	if (wtime >= 0)
	{
	    if (giup.timed_out)
		return FAIL;
#ifdef ELAPSED_FUNC
	    remaining = wtime - ELAPSED_FUNC(start_tv);
	    if (remaining <= 0)
		return FAIL;
#endif
	}
    }
}

    void
gui_mch_flush(void)
{
}

    void
gui_mch_prepare(int *argc UNUSED, char **argv UNUSED)
{
}

    int
gui_mch_init_check(void)
{
    if (IupOpen(NULL, NULL) == IUP_ERROR)
	return FAIL;
    return OK;
}

    int
gui_mch_init(void)
{
    IupSetGlobal("UTF8MODE", "YES");
    IupSetGlobal("UTF8MODEFILE", "YES");

    set_option_value_give_err((char_u *)"termencoding",
						    0L, (char_u *)"utf-8", 0);

    gui.border_width = 2;
    gui.border_offset = gui.border_width;
    gui.scrollbar_width = SB_DEFAULT_WIDTH;
    gui.scrollbar_height = SB_DEFAULT_WIDTH;

    gui.def_norm_pixel = gui_get_rgb_color_cmn(0x00, 0x00, 0x00);
    gui.def_back_pixel = gui_get_rgb_color_cmn(0xFF, 0xFF, 0xFF);
    gui.norm_pixel = gui.def_norm_pixel;
    gui.back_pixel = gui.def_back_pixel;

    giup.canvas = IupCanvas();
    IupSetAttribute(giup.canvas, "BORDER", "NO");
    IupSetAttribute(giup.canvas, "EXPAND", "NO");
    IupSetAttribute(giup.canvas, "DRAWTEXTCLIP", "YES");
    IupSetCallback(giup.canvas, "ACTION", (Icallback)iup_canvas_action_cb);
    IupSetCallback(giup.canvas, "FOCUS_CB", (Icallback)iup_canvas_focus_cb);
    IupSetCallback(giup.canvas, "K_ANY", (Icallback)iup_canvas_key_cb);
    IupSetCallback(giup.canvas, "TEXTINPUT_CB",
					(Icallback)iup_canvas_textinput_cb);
    IupSetCallback(giup.canvas, "BUTTON_CB",
					(Icallback)iup_canvas_button_cb);
    IupSetCallback(giup.canvas, "MOTION_CB",
					(Icallback)iup_canvas_motion_cb);
    IupSetCallback(giup.canvas, "WHEEL_CB",
					(Icallback)iup_canvas_wheel_cb);
#ifdef HAVE_DROP_FILE
    IupSetAttribute(giup.canvas, "DROPFILESTARGET", "YES");
    IupSetCallback(giup.canvas, "DROPFILES_CB",
					(Icallback)iup_canvas_dropfiles_cb);
#endif
#ifdef FEAT_DND
    IupSetAttribute(giup.canvas, "DROPTARGET", "YES");
    IupSetAttribute(giup.canvas, "DROPTYPES", "TEXT");
    IupSetCallback(giup.canvas, "DROPDATA_CB",
					(Icallback)iup_canvas_dropdata_cb);
#endif

    giup.formbox = IupCbox(giup.canvas, NULL);

#ifdef FEAT_TOOLBAR
    giup.toolbar = IupHbox(NULL);
    IupSetAttribute(giup.toolbar, "GAP", "2");
    IupSetAttribute(giup.toolbar, "MARGIN", "2x2");
    IupSetAttribute(giup.toolbar, "VISIBLE", "NO");
    IupSetAttribute(giup.toolbar, "FLOATING", "IGNORE");
#endif
#ifdef FEAT_GUI_TABLINE
    giup.tabbar = IupTabs(NULL);
    IupSetAttribute(giup.tabbar, "CANFOCUS", "NO");
    IupSetAttribute(giup.tabbar, "VISIBLE", "NO");
    IupSetAttribute(giup.tabbar, "FLOATING", "IGNORE");
    IupSetCallback(giup.tabbar, "TABCHANGEPOS_CB",
					 (Icallback)iup_tabline_change_cb);
    IupSetCallback(giup.tabbar, "TABCLOSE_CB",
					  (Icallback)iup_tabline_close_cb);
    IupSetCallback(giup.tabbar, "RIGHTCLICK_CB",
					  (Icallback)iup_tabline_rclick_cb);
    IupSetCallback(giup.tabbar, "REORDER_CB",
					 (Icallback)iup_tabline_reorder_cb);
    IupSetAttribute(giup.tabbar, "SHOWCLOSE", "YES");
    IupSetAttribute(giup.tabbar, "ALLOWREORDER", "YES");
#endif

    giup.topbox = IupVbox(NULL);
    IupSetAttribute(giup.topbox, "GAP", "0");
    IupSetAttribute(giup.topbox, "MARGIN", "0x0");
#ifdef FEAT_TOOLBAR
    IupAppend(giup.topbox, giup.toolbar);
#endif
#ifdef FEAT_GUI_TABLINE
    IupAppend(giup.topbox, giup.tabbar);
#endif
    IupAppend(giup.topbox, giup.formbox);

    giup.dialog = IupDialog(giup.topbox);
    IupSetAttribute(giup.dialog, "TITLE", "Vim");
#ifdef FEAT_MENU
    giup.menubar = IupMenu(NULL);
    IupSetAttributeHandle(giup.dialog, "MENU", giup.menubar);
#endif
    IupSetCallback(giup.dialog, "CLOSE_CB", (Icallback)iup_dialog_close_cb);
    IupSetCallback(giup.dialog, "RESIZE_CB", (Icallback)iup_canvas_resize_cb);

    giup.wait_timer = IupTimer();
    IupSetCallback(giup.wait_timer, "ACTION_CB", (Icallback)iup_wait_timer_cb);

    IupMap(giup.dialog);

    return OK;
}

    void
gui_mch_new_colors(void)
{
    iup_invalidate_all();
}

#ifdef FEAT_MENU
    static void
iup_map_menu_tree(vimmenu_T *menu)
{
    for ( ; menu != NULL; menu = menu->next)
    {
	if (menu->id != NULL)
	    IupMap(menu->id);
	if (menu->children != NULL)
	    iup_map_menu_tree(menu->children);
    }
}
#endif

    int
gui_mch_open(void)
{
    set_normal_colors();

    gui_check_colors();
    gui.def_norm_pixel = gui.norm_pixel;
    gui.def_back_pixel = gui.back_pixel;

    highlight_gui_started();

#ifdef FEAT_MENU
    iup_map_menu_tree(root_menu);
#endif
    IupSetStrf(giup.dialog, "RESIZEINC", "%dx%d",
					   gui.char_width, gui.char_height);

    if (gui_win_x != -1 && gui_win_y != -1)
	IupShowXY(giup.dialog, gui_win_x, gui_win_y);
    else
	IupShow(giup.dialog);

    IupSetFocus(giup.canvas);

    return OK;
}

#if !defined(MSWIN) || defined(PROTO)
    void
gui_mch_expand_font(optexpand_T *args, void *param UNUSED,
					    int (*add_match)(char_u *val))
{
    char_u  *list = (char_u *)IupGetGlobal("FONTLIST");
    char_u  *p;

    if (args->oe_include_orig_val && *args->oe_opt_value == NUL)
    {
	if (add_match((char_u *)"Monospace 12") != OK)
	    return;
    }

    if (list == NULL)
	return;

    for (p = list; *p != NUL; )
    {
	char_u	*start = p;
	char_u	*name;
	int	len;

	while (*p != NUL && *p != '\n')
	    p++;
	len = (int)(p - start);
	if (*p == '\n')
	    p++;
	if (len == 0)
	    continue;

	name = vim_strnsave(start, len);
	if (name == NULL)
	    return;
	if (add_match(name) != OK)
	{
	    vim_free(name);
	    return;
	}
	vim_free(name);
    }
}
#endif

    void
gui_mch_free_all(void)
{
#ifdef FEAT_BEVAL_GUI
    if (beval_timer != NULL)
    {
	IupDestroy(beval_timer);
	beval_timer = NULL;
    }
    if (beval_popover != NULL)
    {
	IupDestroy(beval_popover);
	beval_popover = NULL;
	beval_label = NULL;
    }
#endif
    if (giup.close_timer != NULL)
    {
	IupDestroy(giup.close_timer);
	giup.close_timer = NULL;
    }
    if (iup_clipboard != NULL)
    {
	IupDestroy(iup_clipboard);
	iup_clipboard = NULL;
    }
}

    void
gui_mch_exit(int rc UNUSED)
{
    if (giup.dialog != NULL)
	IupDestroy(giup.dialog);
    if (giup.wait_timer != NULL)
	IupDestroy(giup.wait_timer);
    if (giup.blink_timer != NULL)
	IupDestroy(giup.blink_timer);
    IupClose();
}

    int
gui_mch_get_winpos(int *x, int *y)
{
    *x = IupGetInt(giup.dialog, "X");
    *y = IupGetInt(giup.dialog, "Y");
    return OK;
}

    void
gui_mch_set_winpos(int x, int y)
{
    IupShowXY(giup.dialog, x, y);
}

    int
gui_mch_maximized(void)
{
    return giup.dialog != NULL && IupGetInt(giup.dialog, "MAXIMIZED");
}

    void
gui_mch_unmaximize(void)
{
    IupSetAttribute(giup.dialog, "PLACEMENT", NULL);
    IupShow(giup.dialog);
}

    void
gui_mch_newfont(void)
{
    int	    w, h;

    if (sscanf(IupGetAttribute(giup.dialog, "CLIENTSIZE"),
							"%dx%d", &w, &h) == 2)
	gui_resize_shell(w, h - iup_chrome_height());
}

    void
gui_mch_set_shellsize(int width, int height,
	int min_width, int min_height,
	int base_width UNUSED, int base_height UNUSED,
	int direction UNUSED)
{
    char    size[32];
    char    dsize[32];

    int	    rw, rh, cw, ch;
    int	    wdelta = 0;
    int	    hdelta = 0;
    int	    chrome = iup_chrome_height();

    if (gui_mch_maximized())
	return;
    if (sscanf(IupGetAttribute(giup.dialog, "RASTERSIZE"),
						   "%dx%d", &rw, &rh) == 2
	    && sscanf(IupGetAttribute(giup.dialog, "CLIENTSIZE"),
						   "%dx%d", &cw, &ch) == 2
	    && rw >= cw && rh >= ch)
    {
	wdelta = rw - cw;
	hdelta = rh - ch;
    }
    vim_snprintf(size, sizeof(size), "%dx%d", width, height);
    vim_snprintf(dsize, sizeof(dsize), "%dx%d",
				  width + wdelta, height + chrome + hdelta);
    if (STRCMP(size, IupGetAttribute(giup.formbox, "RASTERSIZE")) == 0
	    && STRCMP(dsize, IupGetAttribute(giup.dialog, "RASTERSIZE")) == 0)
	return;
    IupSetStrf(giup.formbox, "RASTERSIZE", "%s", size);
    IupSetStrf(giup.dialog, "RASTERSIZE", "%s", dsize);
    IupSetStrf(giup.dialog, "MINSIZE", "%dx%d",
			min_width + wdelta, min_height + chrome + hdelta);
    IupSetStrf(giup.dialog, "RESIZEINC", "%dx%d",
					   gui.char_width, gui.char_height);
    IupRefresh(giup.dialog);
}

    void
gui_mch_get_screen_dimensions(int *screen_w, int *screen_h)
{
    char    *size = IupGetGlobal("SCREENSIZE");

    *screen_w = 800;
    *screen_h = 600;
    if (size != NULL)
	sscanf(size, "%dx%d", screen_w, screen_h);
}

    void
gui_mch_settitle(char_u *title, char_u *icon UNUSED)
{
    if (title != NULL)
	IupSetStrf(giup.dialog, "TITLE", "%s", (char *)title);
}

    void
gui_mch_iconify(void)
{
    IupSetAttribute(giup.dialog, "PLACEMENT", "MINIMIZED");
    IupShow(giup.dialog);
}

#if defined(FEAT_EVAL) || defined(PROTO)
    void
gui_mch_set_foreground(void)
{
    IupSetAttribute(giup.dialog, "BRINGFRONT", "YES");
}
#endif

    void
gui_mch_set_text_area_pos(int x, int y, int w, int h)
{
    int	    cw, ch;

    IupSetInt(giup.canvas, "CX", x);
    IupSetInt(giup.canvas, "CY", y);
    IupSetStrf(giup.canvas, "RASTERSIZE", "%dx%d", w, h);
    if (sscanf(IupGetAttribute(giup.dialog, "CLIENTSIZE"),
						    "%dx%d", &cw, &ch) == 2)
	IupSetStrf(giup.formbox, "RASTERSIZE", "%dx%d",
					     cw, ch - iup_chrome_height());
    IupRefreshChildren(giup.topbox);
    iup_invalidate_all();
}

    guicolor_T
gui_mch_get_color(char_u *name)
{
    return gui_get_color_cmn(name);
}

    guicolor_T
gui_mch_get_rgb_color(int r, int g, int b)
{
    return gui_get_rgb_color_cmn(r, g, b);
}

    guicolor_T
gui_mch_get_rgb(guicolor_T pixel)
{
    return pixel & 0xFFFFFF;
}

    void
gui_mch_set_fg_color(guicolor_T color)
{
    giup.fg = color;
}

    void
gui_mch_set_bg_color(guicolor_T color)
{
    giup.bg = color;
}

    void
gui_mch_set_sp_color(guicolor_T color)
{
    giup.sp = color;
}

    static int
iup_parse_fontname(char_u *name, char *face, size_t facelen, int *size,
					     int *bold, int *italic)
{
    char_u  *p;
    char    *sp;
    size_t  len;

    *size = 12;
    *bold = FALSE;
    *italic = FALSE;

    if (name == NULL || *name == NUL)
    {
	vim_strncpy((char_u *)face, (char_u *)"Monospace", facelen - 1);
	return OK;
    }

    p = vim_strchr(name, ':');
    if (p != NULL)
    {
	len = (size_t)(p - name);
	while (*p != NUL)
	{
	    if (p[0] == ':')
	    {
		if (p[1] == 'h' || p[1] == 'H')
		    *size = atoi((char *)p + 2);
		else if (p[1] == 'b')
		    *bold = TRUE;
		else if (p[1] == 'i')
		    *italic = TRUE;
	    }
	    ++p;
	}
    }
    else
    {
	p = name + STRLEN(name);
	while (p > name && p[-1] != ' ')
	    --p;
	if (p > name && SAFE_isdigit(*p))
	{
	    *size = atoi((char *)p);
	    --p;
	    len = (size_t)(p - name);
	}
	else
	    len = STRLEN(name);
    }

    if (len >= facelen)
	len = facelen - 1;
    mch_memmove(face, name, len);
    face[len] = NUL;

    for (;;)
    {
	sp = strrchr(face, ' ');
	if (sp == NULL)
	    break;
	if (STRICMP(sp + 1, "bold") == 0 || STRICMP(sp + 1, "heavy") == 0
					   || STRICMP(sp + 1, "black") == 0)
	    *bold = TRUE;
	else if (STRICMP(sp + 1, "italic") == 0
					 || STRICMP(sp + 1, "oblique") == 0)
	    *italic = TRUE;
	else if (STRICMP(sp + 1, "regular") != 0
		&& STRICMP(sp + 1, "book") != 0
		&& STRICMP(sp + 1, "roman") != 0
		&& STRICMP(sp + 1, "medium") != 0
		&& STRICMP(sp + 1, "light") != 0
		&& STRICMP(sp + 1, "thin") != 0
		&& STRICMP(sp + 1, "semibold") != 0)
	    break;
	*sp = NUL;
    }

    if (*face == NUL)
	vim_strncpy((char_u *)face, (char_u *)"Monospace", facelen - 1);
    if (*size <= 0)
	*size = 12;

    return OK;
}

    static void
iup_update_font_metrics(void)
{
    int	    w = 0, h = 0;
    int	    ascent = 0, descent = 0, line_height = 0;

    IupSetStrf(giup.canvas, "FONT", "%s, %s%s%d",
	    giup.font_face,
	    giup.font_bold ? "Bold " : "",
	    giup.font_italic ? "Italic " : "",
	    giup.font_size);
    IupDrawGetTextSize(giup.canvas, "AAAAAAAAAAAAAAAA", 16, &w, &h);
    IupDrawGetTextMetrics(giup.canvas, &ascent, &descent, &line_height);

    gui.char_width = (w + 8) / 16;
    gui.char_height = line_height + p_linespace;
    gui.char_ascent = ascent + p_linespace / 2;

    if (gui.char_width <= 0)
	gui.char_width = 8;
    if (gui.char_height <= 0)
	gui.char_height = 16;
}

    int
gui_mch_init_font(char_u *font_name, int fontset UNUSED)
{
    char    face[128];
    int	    size;
    int	    bold, italic;

    if (iup_parse_fontname(font_name, face, sizeof(face), &size,
						      &bold, &italic) == FAIL)
	return FAIL;

    vim_strncpy((char_u *)giup.font_face, (char_u *)face,
					     sizeof(giup.font_face) - 1);
    giup.font_size = size;
    giup.font_bold = bold;
    giup.font_italic = italic;

    iup_update_font_metrics();

    gui_mch_free_font(gui.norm_font);
    gui.norm_font = (GuiFont)vim_strsave(
			  (char_u *)IupGetAttribute(giup.canvas, "FONT"));

    return OK;
}

    GuiFont
gui_mch_get_font(char_u *name, int report_error)
{
    char    face[128];
    int	    size;
    int	    bold, italic;

    if (iup_parse_fontname(name, face, sizeof(face), &size,
						      &bold, &italic) == FAIL)
    {
	if (report_error)
	    semsg(_(e_unknown_font_str), name);
	return NOFONT;
    }
    return (GuiFont)vim_strsave(name == NULL ? (char_u *)"" : name);
}

#if defined(FEAT_EVAL) || defined(PROTO)
    char_u *
gui_mch_get_fontname(GuiFont font, char_u *name)
{
    if (font != NOFONT)
	return vim_strsave((char_u *)font);
    if (name != NULL)
	return vim_strsave(name);
    return NULL;
}
#endif

    void
gui_mch_set_font(GuiFont font UNUSED)
{
}

    void
gui_mch_free_font(GuiFont font)
{
    vim_free(font);
}

    int
gui_mch_adjust_charheight(void)
{
    iup_update_font_metrics();
    return OK;
}

    void
gui_mch_draw_string(int row, int col, char_u *s, int len, int flags)
{
    int	    cells;
    int	    i;

    cells = 0;
    for (i = 0; i < len; i += (*mb_ptr2len)(s + i))
	cells += (*mb_ptr2cells)(s + i);

    if (!giup.in_draw_action)
    {
	iup_invalidate(FILL_X(col), FILL_Y(row),
		FILL_X(col + cells) - 1, FILL_Y(row + 1) - 1);
	return;
    }

    if (!(flags & DRAW_TRANSP))
    {
	iup_set_draw_color(giup.bg);
	IupSetAttribute(giup.canvas, "DRAWSTYLE", "FILL");
	IupDrawRectangle(giup.canvas, FILL_X(col), FILL_Y(row),
		FILL_X(col + cells) - 1, FILL_Y(row + 1) - 1);
    }

    iup_set_draw_color(giup.fg);
    iup_set_draw_font(flags);
    IupDrawText(giup.canvas, (char *)s, len, FILL_X(col), FILL_Y(row),
		cells * gui.char_width, gui.char_height);

    if (flags & DRAW_UNDERL)
    {
	int y = FILL_Y(row + 1) - 1;

	iup_set_draw_color(giup.fg);
	IupSetAttribute(giup.canvas, "DRAWSTYLE", "STROKE");
	IupDrawLine(giup.canvas, FILL_X(col), y,
					     FILL_X(col + cells) - 1, y);
    }

    if (flags & DRAW_STRIKE)
    {
	int y = FILL_Y(row + 1) - gui.char_height / 2;

	iup_set_draw_color(giup.sp);
	IupSetAttribute(giup.canvas, "DRAWSTYLE", "STROKE");
	IupDrawLine(giup.canvas, FILL_X(col), y,
					     FILL_X(col + cells) - 1, y);
    }

    if (flags & DRAW_UNDERC)
    {
	int			x;
	int			y = FILL_Y(row + 1) - 1;
	static const int	val[8] = {1, 0, 0, 0, 1, 2, 2, 2};

	iup_set_draw_color(giup.sp);
	for (x = FILL_X(col); x < FILL_X(col + cells); ++x)
	    IupDrawPixel(giup.canvas, x, y - val[x % 8]);
    }
}

    void
gui_mch_clear_block(int row1, int col1, int row2, int col2)
{
    if (!giup.in_draw_action)
    {
	iup_invalidate(FILL_X(col1), FILL_Y(row1),
		FILL_X(col2 + 1) - 1, FILL_Y(row2 + 1) - 1);
	return;
    }

    iup_set_draw_color(gui.back_pixel);
    IupSetAttribute(giup.canvas, "DRAWSTYLE", "FILL");
    IupDrawRectangle(giup.canvas, FILL_X(col1), FILL_Y(row1),
	    FILL_X(col2 + 1) - 1, FILL_Y(row2 + 1) - 1);
}

    void
gui_mch_clear_all(void)
{
    if (!giup.in_draw_action)
    {
	iup_invalidate_all();
	return;
    }

    {
	int w, h;

	IupDrawGetSize(giup.canvas, &w, &h);
	iup_set_draw_color(gui.back_pixel);
	IupSetAttribute(giup.canvas, "DRAWSTYLE", "FILL");
	IupDrawRectangle(giup.canvas, 0, 0, w - 1, h - 1);
    }
}

    void
gui_mch_delete_lines(int row, int num_lines UNUSED)
{
    iup_invalidate(FILL_X(gui.scroll_region_left), FILL_Y(row),
	    FILL_X(gui.scroll_region_right + 1) - 1,
	    FILL_Y(gui.scroll_region_bot + 1) - 1);
}

    void
gui_mch_insert_lines(int row, int num_lines UNUSED)
{
    iup_invalidate(FILL_X(gui.scroll_region_left), FILL_Y(row),
	    FILL_X(gui.scroll_region_right + 1) - 1,
	    FILL_Y(gui.scroll_region_bot + 1) - 1);
}

    void
gui_mch_invert_rectangle(int r, int c, int nr, int nc)
{
    iup_invalidate(FILL_X(c), FILL_Y(r),
	    FILL_X(c + nc) - 1, FILL_Y(r + nr) - 1);
}

    void
gui_mch_draw_hollow_cursor(guicolor_T color)
{
    if (!giup.in_draw_action)
    {
	iup_invalidate(FILL_X(gui.col), FILL_Y(gui.row),
		FILL_X(gui.col + 1) - 1, FILL_Y(gui.row + 1) - 1);
	return;
    }

    iup_set_draw_color(color);
    IupSetAttribute(giup.canvas, "DRAWSTYLE", "STROKE");
    IupDrawRectangle(giup.canvas, FILL_X(gui.col), FILL_Y(gui.row),
	    FILL_X(gui.col + 1) - 1, FILL_Y(gui.row + 1) - 1);
}

    void
gui_mch_draw_part_cursor(int w, int h, guicolor_T color)
{
    if (!giup.in_draw_action)
    {
	iup_invalidate(FILL_X(gui.col), FILL_Y(gui.row),
		FILL_X(gui.col + 1) - 1, FILL_Y(gui.row + 1) - 1);
	return;
    }

    iup_set_draw_color(color);
    IupSetAttribute(giup.canvas, "DRAWSTYLE", "FILL");
    IupDrawRectangle(giup.canvas,
	    FILL_X(gui.col), FILL_Y(gui.row) + gui.char_height - h,
	    FILL_X(gui.col) + w - 1, FILL_Y(gui.row + 1) - 1);
}

    void
gui_mch_flash(int msec)
{
    giup.flash = 1;
    IupRedraw(giup.canvas, 0);
    IupFlush();
    ui_delay((long)msec, TRUE);
    giup.flash = 0;
    IupRedraw(giup.canvas, 0);
    IupFlush();
}

    void
gui_mch_beep(void)
{
    fputc('\a', stderr);
    fflush(stderr);
}

    int
gui_mch_is_blinking(void)
{
    return giup.blink_state != BLINK_NONE;
}

    int
gui_mch_is_blink_off(void)
{
    return giup.blink_state == BLINK_OFF;
}

    void
gui_mch_set_blinking(long waittime, long on, long off)
{
    giup.blink_wait = waittime;
    giup.blink_on = on;
    giup.blink_off = off;
}

    static int
iup_blink_timer_cb(Ihandle *ih UNUSED)
{
    IupSetAttribute(giup.blink_timer, "RUN", "NO");
    if (giup.blink_state == BLINK_ON)
    {
	gui_undraw_cursor();
	giup.blink_state = BLINK_OFF;
	IupSetInt(giup.blink_timer, "TIME", (int)giup.blink_off);
    }
    else
    {
	gui_update_cursor(TRUE, FALSE);
	giup.blink_state = BLINK_ON;
	IupSetInt(giup.blink_timer, "TIME", (int)giup.blink_on);
    }
    IupSetAttribute(giup.blink_timer, "RUN", "YES");
    return IUP_DEFAULT;
}

    void
gui_mch_start_blink(void)
{
    if (giup.blink_timer == NULL)
    {
	giup.blink_timer = IupTimer();
	IupSetCallback(giup.blink_timer, "ACTION_CB",
					    (Icallback)iup_blink_timer_cb);
    }
    else
	IupSetAttribute(giup.blink_timer, "RUN", "NO");

    if (giup.blink_wait && giup.blink_on && giup.blink_off && gui.in_focus)
    {
	IupSetInt(giup.blink_timer, "TIME", (int)giup.blink_wait);
	giup.blink_state = BLINK_ON;
	IupSetAttribute(giup.blink_timer, "RUN", "YES");
	gui_update_cursor(TRUE, FALSE);
    }
}

    void
gui_mch_stop_blink(int may_call_gui_update_cursor)
{
    if (giup.blink_timer != NULL)
	IupSetAttribute(giup.blink_timer, "RUN", "NO");
    if (giup.blink_state == BLINK_OFF && may_call_gui_update_cursor)
	gui_update_cursor(TRUE, FALSE);
    giup.blink_state = BLINK_NONE;
}

    void
gui_mch_getmouse(int *x, int *y)
{
    char    *pos = IupGetGlobal("CURSORPOS");
    int	    sx = 0, sy = 0, mx = 0, my = 0;

    if (pos != NULL)
	sscanf(pos, "%dx%d", &mx, &my);
    pos = IupGetAttribute(giup.canvas, "SCREENPOSITION");
    if (pos != NULL)
	sscanf(pos, "%d,%d", &sx, &sy);
    *x = mx - sx;
    *y = my - sy;
}

    void
gui_mch_setmouse(int x, int y)
{
    char    *pos = IupGetAttribute(giup.canvas, "SCREENPOSITION");
    int	    sx = 0, sy = 0;

    if (pos != NULL)
	sscanf(pos, "%d,%d", &sx, &sy);
    IupSetStrf(NULL, "CURSORPOS", "%dx%d", sx + x, sy + y);
}

    void
gui_mch_mousehide(int hide)
{
    if (giup.mouse_hidden == hide)
	return;
    giup.mouse_hidden = hide;
    IupSetAttribute(giup.canvas, "CURSOR", hide ? "NONE" : "ARROW");
}

#if defined(FEAT_X11) || defined(PROTO)
    Display *
gui_mch_get_display(void)
{
    return (Display *)IupGetAttribute(giup.canvas, "XDISPLAY");
}

    int
gui_get_x11_windis(Window *win, Display **dis)
{
    *dis = gui_mch_get_display();
    *win = (Window)(long)IupGetAttribute(giup.canvas, "XWINDOW");
    if (*dis == NULL || *win == 0)
	return FAIL;
    return OK;
}
#endif

    int
gui_mch_haskey(char_u *name)
{
    int	    i;

    for (i = 0; special_keys[i].key_sym != 0; i++)
	if (special_keys[i].vim_code1 != NUL
		&& name[0] == special_keys[i].vim_code0
		&& name[1] == special_keys[i].vim_code1)
	    return OK;
    return FAIL;
}

    static int
iup_scrollbar_valuechanged_cb(Ihandle *ih)
{
    scrollbar_T	*sb = (scrollbar_T *)IupGetAttribute(ih, "_VIM_SB");

    if (sb != NULL)
	gui_drag_scrollbar(sb, (long)(IupGetDouble(ih, "VALUE") + 0.5),
									FALSE);
    return IUP_DEFAULT;
}

    void
gui_mch_create_scrollbar(scrollbar_T *sb, int orient)
{
    sb->id = IupScrollbar(orient == SBAR_VERT ? "VERTICAL" : "HORIZONTAL");
    IupSetAttribute(sb->id, "CANFOCUS", "NO");
    IupSetAttribute(sb->id, "VISIBLE", "NO");
    IupSetAttribute(sb->id, "_VIM_SB", (char *)sb);
    IupSetCallback(sb->id, "VALUECHANGED_CB",
				  (Icallback)iup_scrollbar_valuechanged_cb);
    IupAppend(giup.formbox, sb->id);
    if (IupGetAttribute(giup.dialog, "WID") != NULL)
	IupMap(sb->id);
}

    void
gui_mch_destroy_scrollbar(scrollbar_T *sb)
{
    if (sb->id != NULL)
    {
	IupDestroy(sb->id);
	sb->id = NULL;
    }
}

    void
gui_mch_enable_scrollbar(scrollbar_T *sb, int flag)
{
    if (sb->id != NULL)
	IupSetAttribute(sb->id, "VISIBLE", flag ? "YES" : "NO");
}

    void
gui_mch_set_scrollbar_thumb(scrollbar_T *sb, long val, long size, long max)
{
    if (sb->id == NULL)
	return;
    IupSetDouble(sb->id, "MIN", 0.0);
    IupSetDouble(sb->id, "MAX", (double)(max + 1));
    IupSetDouble(sb->id, "PAGESIZE", (double)size);
    IupSetDouble(sb->id, "LINESTEP", 1.0 / (double)(max + 1));
    IupSetDouble(sb->id, "PAGESTEP", (double)size / (double)(max + 1));
    IupSetDouble(sb->id, "VALUE", (double)val);
}

    void
gui_mch_set_scrollbar_pos(scrollbar_T *sb, int x, int y, int w, int h)
{
    if (sb->id == NULL)
	return;
    IupSetInt(sb->id, "CX", x);
    IupSetInt(sb->id, "CY", y);
    IupSetStrf(sb->id, "RASTERSIZE", "%dx%d", w, h);
    IupRefreshChildren(giup.formbox);
}

    int
gui_mch_get_scrollbar_xpadding(void)
{
    return 0;
}

    int
gui_mch_get_scrollbar_ypadding(void)
{
    return 0;
}

#if defined(FEAT_MENU) || defined(PROTO)
    static char *
iup_menu_title(vimmenu_T *menu)
{
    static char	title[256];
    char_u	*p = menu->dname;
    int		len = 0;
    int		did_mnemonic = FALSE;

    while (*p != NUL && len < (int)sizeof(title) - 8)
    {
	if (*p == '&')
	    title[len++] = '&';
	else if (!did_mnemonic && menu->mnemonic != 0
		&& TOLOWER_ASC(*p) == TOLOWER_ASC(menu->mnemonic))
	{
	    title[len++] = '&';
	    did_mnemonic = TRUE;
	}
	title[len++] = *p++;
    }
    title[len] = NUL;

    if (menu->actext != NULL && *menu->actext != NUL)
	vim_snprintf(title + len, sizeof(title) - len, "\t%s",
							(char *)menu->actext);
    return title;
}

    static void
iup_menu_insert(Ihandle *parent, Ihandle *item, int idx)
{
    Ihandle *ref = IupGetChild(parent, idx);

    if (ref != NULL)
	IupInsert(parent, ref, item);
    else
	IupAppend(parent, item);
    if (IupGetAttribute(giup.dialog, "WID") != NULL)
	IupMap(item);
}

    static int
iup_menu_item_cb(Ihandle *ih)
{
    vimmenu_T	*menu = (vimmenu_T *)IupGetAttribute(ih, "_VIM_MENU");

    if (menu != NULL)
	gui_menu_cb(menu);
    return IUP_DEFAULT;
}

    void
gui_mch_enable_menu(int flag)
{
    if (flag)
	IupSetAttributeHandle(giup.dialog, "MENU", giup.menubar);
    else
	IupSetAttribute(giup.dialog, "MENU", NULL);
}

    void
gui_mch_set_menu_pos(int x UNUSED, int y UNUSED, int w UNUSED, int h UNUSED)
{
}

    void
gui_mch_add_menu(vimmenu_T *menu, int idx)
{
    vimmenu_T	*parent = menu->parent;
    Ihandle	*parent_menu;

    if (menu_is_popup(menu->name))
    {
	menu->submenu_id = IupMenu(NULL);
	menu->id = NULL;
	return;
    }

#ifdef FEAT_TOOLBAR
    if (menu_is_toolbar(menu->name))
    {
	menu->submenu_id = giup.toolbar;
	menu->id = NULL;
	return;
    }
#endif

    if (parent != NULL)
    {
	if (parent->submenu_id == NULL)
	    return;
	parent_menu = parent->submenu_id;
    }
    else
    {
	if (!menu_is_menubar(menu->name))
	    return;
	parent_menu = giup.menubar;
    }

    menu->submenu_id = IupMenu(NULL);
    menu->id = IupSubmenu(iup_menu_title(menu), menu->submenu_id);
    iup_menu_insert(parent_menu, menu->id, idx);
}

#ifdef FEAT_TOOLBAR
#include "gui_x11_pm.h"

static const char *toolbar_stock_names[] =
{
    "document-new", "document-open", "document-save",
    "edit-undo", "edit-redo",
    "edit-cut", "edit-copy", "edit-paste",
    "document-print", "help-browser",
    "edit-find", "document-save", "document-save",
    "document-new", "document-open",
    "system-run", "edit-find-replace", "window-close",
    "view-fullscreen", "view-restore",
    NULL, "utilities-terminal", "go-up", "go-down", "help-contents",
    "applications-development", "go-jump", NULL, NULL, NULL,
    NULL
};

    static int
iup_xpm_color(const char *line, int cpp, unsigned char *rgba)
{
    const char	*p = line + cpp;
    int		r, g, b;

    while (*p != NUL)
    {
	while (*p == ' ' || *p == '\t')
	    p++;
	if (p[0] == 'c' && (p[1] == ' ' || p[1] == '\t'))
	{
	    p++;
	    while (*p == ' ' || *p == '\t')
		p++;
	    if (*p == '#' && sscanf(p + 1, "%2x%2x%2x", &r, &g, &b) == 3)
	    {
		rgba[0] = (unsigned char)r;
		rgba[1] = (unsigned char)g;
		rgba[2] = (unsigned char)b;
		rgba[3] = 255;
	    }
	    else
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
	    return OK;
	}
	while (*p != NUL && *p != ' ' && *p != '\t')
	    p++;
    }
    return FAIL;
}

    static Ihandle *
iup_xpm_image(char **xpm)
{
    unsigned char   palette[256][4];
    unsigned char   *pixels;
    Ihandle	    *image;
    int		    w, h, ncolors, cpp, i, x, y;

    if (xpm == NULL || sscanf(xpm[0], "%d %d %d %d", &w, &h, &ncolors, &cpp)
									 != 4)
	return NULL;
    if (cpp != 1 || w <= 0 || h <= 0 || ncolors <= 0)
	return NULL;

    memset(palette, 0, sizeof(palette));
    for (i = 0; i < ncolors; i++)
	iup_xpm_color(xpm[1 + i], cpp, palette[(unsigned char)xpm[1 + i][0]]);

    pixels = alloc((size_t)w * h * 4);
    if (pixels == NULL)
	return NULL;

    for (y = 0; y < h; y++)
    {
	const char *row = xpm[1 + ncolors + y];

	for (x = 0; x < w; x++)
	    memcpy(pixels + ((size_t)y * w + x) * 4,
			     palette[(unsigned char)row[x]], 4);
    }

    image = IupImageRGBA(w, h, pixels);
    vim_free(pixels);
    return image;
}

    static Ihandle *
iup_builtin_image(int iconidx)
{
    static Ihandle *cache[ARRAY_LENGTH(built_in_pixmaps)];

    if (iconidx < 0 || iconidx >= (int)ARRAY_LENGTH(built_in_pixmaps))
	return NULL;
    if (cache[iconidx] == NULL)
	cache[iconidx] = iup_xpm_image(built_in_pixmaps[iconidx]);
    return cache[iconidx];
}

    static Ihandle *
iup_toolbar_image(vimmenu_T *menu)
{
    char_u	buf[MAXPATHL];
    Ihandle	*image = NULL;

    if (!menu->icon_builtin)
    {
	if (menu->iconfile != NULL)
	{
	    gui_find_iconfile(menu->iconfile, buf, "png");
	    image = IupImageGetHandle((char *)buf);
	}
	if (image == NULL && gui_find_bitmap(menu->name, buf, "png") == OK)
	    image = IupImageGetHandle((char *)buf);
	if (image == NULL && gui_find_bitmap(menu->name, buf, "bmp") == OK)
	    image = IupImageGetHandle((char *)buf);
	if (image == NULL && gui_find_bitmap(menu->name, buf, "xpm") == OK)
	    image = IupImageGetHandle((char *)buf);
    }
    if (image == NULL && menu->iconidx >= 0
	    && menu->iconidx < (int)ARRAY_LENGTH(toolbar_stock_names)
	    && toolbar_stock_names[menu->iconidx] != NULL)
	image = IupImageGetHandle((char *)toolbar_stock_names[menu->iconidx]);
    if (image == NULL)
	image = iup_builtin_image(menu->iconidx);
    return image;
}
#endif

    void
gui_mch_add_menu_item(vimmenu_T *menu, int idx)
{
    vimmenu_T	*parent = menu->parent;

    if (parent == NULL || parent->submenu_id == NULL)
	return;

#ifdef FEAT_TOOLBAR
    if (menu_is_toolbar(parent->name))
    {
	menu->submenu_id = NULL;
	if (menu_is_separator(menu->name))
	{
	    menu->id = IupLabel(NULL);
	    IupSetAttribute(menu->id, "SEPARATOR", "VERTICAL");
	}
	else
	{
	    Ihandle *image = iup_toolbar_image(menu);

	    menu->id = IupButton(image != NULL ? NULL : (char *)menu->dname);
	    if (image != NULL)
		IupSetAttributeHandle(menu->id, "IMAGE", image);
	    IupSetAttribute(menu->id, "FLAT", "YES");
	    IupSetAttribute(menu->id, "CANFOCUS", "NO");
	    if (menu->strings[MENU_INDEX_TIP] != NULL)
		IupSetStrf(menu->id, "TIP", "%s",
				    (char *)menu->strings[MENU_INDEX_TIP]);
	    IupSetAttribute(menu->id, "_VIM_MENU", (char *)menu);
	    IupSetCallback(menu->id, "ACTION", (Icallback)iup_menu_item_cb);
	}
	iup_menu_insert(giup.toolbar, menu->id, idx);
	return;
    }
#endif

    menu->submenu_id = NULL;
    if (menu_is_separator(menu->name))
	menu->id = IupMenuSeparator();
    else
    {
	menu->id = IupMenuItem(iup_menu_title(menu));
	IupSetAttribute(menu->id, "_VIM_MENU", (char *)menu);
	IupSetCallback(menu->id, "ACTION", (Icallback)iup_menu_item_cb);
    }
    iup_menu_insert(parent->submenu_id, menu->id, idx);
}

#if defined(FEAT_TOOLBAR) && defined(FEAT_BEVAL_GUI)
    void
gui_mch_menu_set_tip(vimmenu_T *menu)
{
    if (menu->id != NULL && menu->parent != NULL
				     && menu_is_toolbar(menu->parent->name))
    {
	if (menu->strings[MENU_INDEX_TIP] != NULL)
	    IupSetStrf(menu->id, "TIP", "%s",
				     (char *)menu->strings[MENU_INDEX_TIP]);
	else
	    IupSetAttribute(menu->id, "TIP", NULL);
    }
}
#endif

    void
gui_mch_destroy_menu(vimmenu_T *menu)
{
    if (menu->id != NULL)
    {
	IupDestroy(menu->id);
	menu->id = NULL;
	menu->submenu_id = NULL;
    }
    else if (menu->submenu_id != NULL)
    {
	if (menu->submenu_id != giup.toolbar)
	    IupDestroy(menu->submenu_id);
	menu->submenu_id = NULL;
    }
}

    void
gui_mch_menu_grey(vimmenu_T *menu, int grey)
{
    if (menu->id != NULL)
	IupSetAttribute(menu->id, "ACTIVE", grey ? "NO" : "YES");
}

    static Ihandle *
iup_menu_parent(vimmenu_T *menu)
{
    if (menu->parent != NULL)
	return menu->parent->submenu_id;
    if (menu_is_menubar(menu->name))
	return giup.menubar;
    return NULL;
}

    static int
iup_menu_visible_index(vimmenu_T *menu)
{
    vimmenu_T	*p;
    vimmenu_T	*first;
    int		idx = 0;

    first = menu->parent != NULL ? menu->parent->children : root_menu;
    for (p = first; p != NULL && p != menu; p = p->next)
	if (p->id != NULL && IupGetParent(p->id) != NULL)
	    idx++;
    return idx;
}

    void
gui_mch_menu_hidden(vimmenu_T *menu, int hidden)
{
    Ihandle *parent_menu;

    if (menu->id == NULL)
	return;

    parent_menu = iup_menu_parent(menu);
    if (parent_menu == NULL)
	return;

    if (hidden)
    {
	if (IupGetParent(menu->id) != NULL)
	    IupDetach(menu->id);
    }
    else if (IupGetParent(menu->id) == NULL)
	iup_menu_insert(parent_menu, menu->id, iup_menu_visible_index(menu));
}

    void
gui_mch_draw_menubar(void)
{
}

    void
gui_mch_show_popupmenu(vimmenu_T *menu)
{
    iup_push_updates();
    if (menu->submenu_id != NULL)
	IupPopup(menu->submenu_id, IUP_MOUSEPOS, IUP_MOUSEPOS);
}

    void
gui_mch_toggle_tearoffs(int enable UNUSED)
{
}
#endif // FEAT_MENU

#if defined(FEAT_GUI_DIALOG) || defined(PROTO)
static int iup_dialog_button;

    static int
iup_dialog_button_cb(Ihandle *ih)
{
    iup_dialog_button = IupGetInt(ih, "_VIM_IDX");
    return IUP_CLOSE;
}

    int
gui_mch_dialog(int type UNUSED, char_u *title, char_u *message,
	char_u *buttons, int dfltbutton, char_u *textfield, int ex_cmd UNUSED)
{
    iup_push_updates();
    Ihandle	*dlg;
    Ihandle	*vbox;
    Ihandle	*hbox;
    Ihandle	*entry = NULL;
    Ihandle	*defbtn = NULL;
    char_u	*copy;
    char_u	*p;
    char_u	*q;
    int		idx = 0;

    copy = vim_strsave(buttons);
    if (copy == NULL)
	return -1;

    hbox = IupHbox(IupFill(), NULL);
    IupSetAttribute(hbox, "GAP", "8");

    for (p = copy; *p != NUL; )
    {
	Ihandle	*btn;
	char	label[128];
	int	len = 0;

	for (q = p; *q != NUL && *q != DLG_BUTTON_SEP; ++q)
	    if (*q != DLG_HOTKEY_CHAR && len < (int)sizeof(label) - 1)
		label[len++] = *q;
	label[len] = NUL;

	btn = IupButton(label);
	IupSetInt(btn, "_VIM_IDX", ++idx);
	IupSetCallback(btn, "ACTION", (Icallback)iup_dialog_button_cb);
	IupSetAttribute(btn, "PADDING", "12x2");
	IupAppend(hbox, btn);
	if (idx == dfltbutton)
	    defbtn = btn;

	if (*q == NUL)
	    break;
	p = q + 1;
    }
    IupAppend(hbox, IupFill());
    vim_free(copy);

    vbox = IupVbox(IupLabel((char *)message), NULL);
    IupSetAttribute(vbox, "GAP", "10");
    IupSetAttribute(vbox, "MARGIN", "15x15");
    IupSetAttribute(vbox, "ALIGNMENT", "ACENTER");

    if (textfield != NULL)
    {
	entry = IupText();
	IupSetAttribute(entry, "EXPAND", "HORIZONTAL");
	IupSetAttribute(entry, "VISIBLECOLUMNS", "30");
	IupSetStrf(entry, "VALUE", "%s", (char *)textfield);
	IupAppend(vbox, entry);
    }
    IupAppend(vbox, hbox);

    dlg = IupDialog(vbox);
    IupSetStrf(dlg, "TITLE", "%s", title != NULL ? (char *)title : "Vim");
    IupSetAttribute(dlg, "DIALOGFRAME", "YES");
    IupSetAttributeHandle(dlg, "PARENTDIALOG", giup.dialog);
    if (defbtn != NULL)
	IupSetAttributeHandle(dlg, "DEFAULTENTER", defbtn);

    iup_dialog_button = 0;
    IupPopup(dlg, IUP_CENTERPARENT, IUP_CENTERPARENT);

    if (textfield != NULL)
    {
	if (iup_dialog_button != 0 && entry != NULL)
	    vim_strncpy(textfield, (char_u *)IupGetAttribute(entry, "VALUE"),
								  IOSIZE - 1);
	else
	    *textfield = NUL;
    }

    IupDestroy(dlg);
    return iup_dialog_button;
}
#endif // FEAT_GUI_DIALOG

#if defined(FEAT_BROWSE) || defined(PROTO)
    char_u *
gui_mch_browse(int saving, char_u *title, char_u *dflt, char_u *ext UNUSED,
				       char_u *initdir, char_u *filter UNUSED)
{
    iup_push_updates();
    Ihandle	*dlg;
    char_u	*result = NULL;

    dlg = IupFileDlg();
    IupSetAttribute(dlg, "DIALOGTYPE", saving ? "SAVE" : "OPEN");
    if (title != NULL)
	IupSetStrf(dlg, "TITLE", "%s", (char *)title);
    if (initdir != NULL)
	IupSetStrf(dlg, "DIRECTORY", "%s", (char *)initdir);
    if (dflt != NULL)
	IupSetStrf(dlg, "FILE", "%s", (char *)dflt);
    IupSetAttributeHandle(dlg, "PARENTDIALOG", giup.dialog);

    IupPopup(dlg, IUP_CENTERPARENT, IUP_CENTERPARENT);

    if (IupGetInt(dlg, "STATUS") != -1)
	result = vim_strsave((char_u *)IupGetAttribute(dlg, "VALUE"));

    IupDestroy(dlg);
    return result;
}

    char_u *
gui_mch_browsedir(char_u *title, char_u *initdir)
{
    Ihandle	*dlg;
    char_u	*result = NULL;

    iup_push_updates();
    dlg = IupFileDlg();
    IupSetAttribute(dlg, "DIALOGTYPE", "DIR");
    if (title != NULL)
	IupSetStrf(dlg, "TITLE", "%s", (char *)title);
    if (initdir != NULL)
	IupSetStrf(dlg, "DIRECTORY", "%s", (char *)initdir);
    IupSetAttributeHandle(dlg, "PARENTDIALOG", giup.dialog);

    IupPopup(dlg, IUP_CENTERPARENT, IUP_CENTERPARENT);

    if (IupGetInt(dlg, "STATUS") != -1)
	result = vim_strsave((char_u *)IupGetAttribute(dlg, "VALUE"));

    IupDestroy(dlg);
    return result;
}
#endif // FEAT_BROWSE

#if defined(FEAT_TOOLBAR) || defined(PROTO)
    void
gui_mch_show_toolbar(int showit)
{
    IupSetAttribute(giup.toolbar, "FLOATING", showit ? "NO" : "IGNORE");
    IupSetAttribute(giup.toolbar, "VISIBLE", showit ? "YES" : "NO");
    IupRefresh(giup.topbox);
}
#endif

#if defined(FEAT_GUI_TABLINE) || defined(PROTO)
    void
gui_mch_show_tabline(int showit)
{
    if (showit == gui_mch_showing_tabline())
	return;
    if (showit)
    {
	giup.ignore_tabline = TRUE;
	IupSetAttribute(giup.tabbar, "FLOATING", "NO");
	IupSetAttribute(giup.tabbar, "VISIBLE", "YES");
	iup_place_formbox(iup_tabline_page(IupGetInt(giup.tabbar,
							  "VALUEPOS")));
	giup.ignore_tabline = FALSE;
    }
    else
    {
	iup_place_formbox(giup.topbox);
	IupSetAttribute(giup.tabbar, "FLOATING", "IGNORE");
	IupSetAttribute(giup.tabbar, "VISIBLE", "NO");
    }
    IupRefresh(giup.topbox);
    IupSetFocus(giup.canvas);
}

    int
gui_mch_showing_tabline(void)
{
    return giup.tabbar != NULL && IupGetInt(giup.tabbar, "VISIBLE");
}

    void
gui_mch_update_tabline(void)
{
    tabpage_T	*tp;
    int		nr = 0;
    int		cur = 0;
    Ihandle	*child;

    giup.ignore_tabline = TRUE;

    for (tp = first_tabpage; tp != NULL; tp = tp->tp_next, ++nr)
    {
	if (tp == curtab)
	    cur = nr;
	child = iup_tabline_page(nr);
	get_tabline_label(tp, FALSE);
	IupSetStrfId(giup.tabbar, "TABTITLE", nr, "%s", (char *)NameBuff);
	get_tabline_label(tp, TRUE);
	IupSetStrfId(giup.tabbar, "TABTIP", nr, "%s", (char *)NameBuff);
    }

    if (gui_mch_showing_tabline())
	iup_place_formbox(iup_tabline_page(cur));

    while ((child = IupGetChild(giup.tabbar, nr)) != NULL)
	IupDestroy(child);

    IupSetInt(giup.tabbar, "VALUEPOS", cur);
    giup.ignore_tabline = FALSE;
    if (gui_mch_showing_tabline())
	IupSetFocus(giup.canvas);
}

    void
gui_mch_set_curtab(int nr)
{
    giup.ignore_tabline = TRUE;
    IupSetInt(giup.tabbar, "VALUEPOS", nr - 1);
    if (gui_mch_showing_tabline())
	iup_place_formbox(iup_tabline_page(nr - 1));
    giup.ignore_tabline = FALSE;
    IupSetFocus(giup.canvas);
}
#endif

#if defined(FEAT_MOUSESHAPE) || defined(PROTO)
static const char *mshape_iup_names[] =
{
    "ARROW",		// arrow
    "NONE",		// blank
    "TEXT",		// beam
    "RESIZE_NS",	// updown
    "RESIZE_NS",	// udsizing
    "RESIZE_WE",	// leftright
    "RESIZE_WE",	// lrsizing
    "BUSY",		// busy
    "ARROW",		// no
    "CROSS",		// crosshair
    "HAND",		// hand1
    "HAND",		// hand2
    "PEN",		// pencil
    "HELP",		// question
    "ARROW",		// right-arrow
    "UPARROW",		// up-arrow
};

    void
mch_set_mouse_shape(int shape)
{
    const char	*name = "ARROW";

    if (shape == MSHAPE_HIDE || gui.pointer_hidden)
	name = "NONE";
    else if (shape < (int)ARRAY_LENGTH(mshape_iup_names))
	name = mshape_iup_names[shape];
    IupSetAttribute(giup.canvas, "CURSOR", (char *)name);
}
#endif

#if defined(FEAT_SIGN_ICONS) || defined(PROTO)
    void
gui_mch_drawsign(int row, int col, int typenr)
{
    Ihandle	*img;
    char	*name;

    if (!giup.in_draw_action)
    {
	iup_invalidate(FILL_X(col), FILL_Y(row),
		FILL_X(col + 2) - 1, FILL_Y(row + 1) - 1);
	return;
    }

    img = (Ihandle *)sign_get_image(typenr);
    if (img == NULL)
	return;
    name = IupGetAttribute(img, "_VIM_NAME");
    if (name == NULL)
	return;

    iup_set_draw_color(gui.back_pixel);
    IupSetAttribute(giup.canvas, "DRAWSTYLE", "FILL");
    IupDrawRectangle(giup.canvas, FILL_X(col), FILL_Y(row),
	    FILL_X(col + 2) - 1, FILL_Y(row + 1) - 1);
    IupDrawImage(giup.canvas, name, FILL_X(col), FILL_Y(row),
	    2 * gui.char_width, gui.char_height);
}

    void *
gui_mch_register_sign(char_u *signfile)
{
    static int	sign_id = 0;
    Ihandle	*img;
    char	name[32];

    if (signfile == NULL || *signfile == NUL || *signfile == '-')
	return NULL;

    img = IupImageGetHandle((char *)signfile);
    if (img == NULL)
    {
	emsg(_(e_couldnt_read_in_sign_data));
	return NULL;
    }
    vim_snprintf(name, sizeof(name), "_vim_sign_%d", ++sign_id);
    IupSetHandle(name, img);
    IupSetStrf(img, "_VIM_NAME", "%s", name);
    return (void *)img;
}

    void
gui_mch_destroy_sign(void *sign)
{
    if (sign != NULL)
	IupDestroy((Ihandle *)sign);
}
#endif

    char_u *
gui_mch_font_dialog(char_u *oldval)
{
    iup_push_updates();
    Ihandle	*dlg;
    char_u	*result = NULL;

    dlg = IupFontDlg();
    if (oldval != NULL && *oldval != NUL)
	IupSetStrf(dlg, "VALUE", "%s, %d", giup.font_face, giup.font_size);
    IupSetAttributeHandle(dlg, "PARENTDIALOG", giup.dialog);

    IupPopup(dlg, IUP_CENTERPARENT, IUP_CENTERPARENT);

    if (IupGetInt(dlg, "STATUS") == 1)
    {
	char	face[128];
	char	*value = IupGetAttribute(dlg, "VALUE");
	char	*p;
	int	size = 0;

	vim_strncpy((char_u *)face, (char_u *)value, sizeof(face) - 1);
	p = strchr(face, ',');
	if (p != NULL)
	{
	    *p++ = NUL;
	    while (*p != NUL)
	    {
		if (SAFE_isdigit(*p) || *p == '-')
		{
		    size = atoi(p);
		    break;
		}
		++p;
	    }
	}
	if (size > 0)
	{
	    char    buf[160];

	    vim_snprintf(buf, sizeof(buf), "%s %d", face, size);
	    result = vim_strsave((char_u *)buf);
	}
    }

    IupDestroy(dlg);
    return result;
}

#if defined(FIND_REPLACE_DIALOG) || defined(PROTO)
static struct
{
    Ihandle	*dialog;
    Ihandle	*what;
    Ihandle	*with;
    Ihandle	*wword;
    Ihandle	*mcase;
    Ihandle	*up;
    Ihandle	*repl_line;
} frd;

    static int
iup_frd_action(int flags)
{
    if (IupGetInt(frd.wword, "VALUE"))
	flags |= FRD_WHOLE_WORD;
    if (IupGetInt(frd.mcase, "VALUE"))
	flags |= FRD_MATCH_CASE;
    gui_do_findrepl(flags,
	    (char_u *)IupGetAttribute(frd.what, "VALUE"),
	    (char_u *)IupGetAttribute(frd.with, "VALUE"),
	    !IupGetInt(frd.up, "VALUE"));
    return IUP_DEFAULT;
}

    static int
iup_frd_find_cb(Ihandle *ih UNUSED)
{
    return iup_frd_action(FRD_FINDNEXT);
}

    static int
iup_frd_replace_cb(Ihandle *ih UNUSED)
{
    return iup_frd_action(FRD_REPLACE);
}

    static int
iup_frd_replaceall_cb(Ihandle *ih UNUSED)
{
    return iup_frd_action(FRD_REPLACEALL);
}

    static int
iup_frd_close_cb(Ihandle *ih UNUSED)
{
    IupHide(frd.dialog);
    return IUP_DEFAULT;
}

    static void
iup_frd_create(void)
{
    Ihandle	*grid;
    Ihandle	*opts;
    Ihandle	*btns;
    Ihandle	*btn;

    frd.what = IupText();
    IupSetAttribute(frd.what, "EXPAND", "HORIZONTAL");
    IupSetAttribute(frd.what, "VISIBLECOLUMNS", "25");
    frd.with = IupText();
    IupSetAttribute(frd.with, "EXPAND", "HORIZONTAL");
    IupSetAttribute(frd.with, "VISIBLECOLUMNS", "25");

    frd.repl_line = IupHbox(IupLabel(_("Replace with:")), frd.with, NULL);
    IupSetAttribute(frd.repl_line, "GAP", "8");
    IupSetAttribute(frd.repl_line, "ALIGNMENT", "ACENTER");

    grid = IupHbox(IupLabel(_("Find what:")), frd.what, NULL);
    IupSetAttribute(grid, "GAP", "8");
    IupSetAttribute(grid, "ALIGNMENT", "ACENTER");

    frd.wword = IupToggle(_("Match whole word only"));
    frd.mcase = IupToggle(_("Match case"));
    frd.up = IupToggle(_("Up"));
    opts = IupHbox(frd.wword, frd.mcase, frd.up, NULL);
    IupSetAttribute(opts, "GAP", "10");

    btns = IupHbox(IupFill(), NULL);
    IupSetAttribute(btns, "GAP", "8");
    btn = IupButton(_("Find Next"));
    IupSetCallback(btn, "ACTION", (Icallback)iup_frd_find_cb);
    IupAppend(btns, btn);
    IupSetAttributeHandle(NULL, "_VIM_FRD_FIND", btn);
    btn = IupButton(_("Replace"));
    IupSetCallback(btn, "ACTION", (Icallback)iup_frd_replace_cb);
    IupAppend(btns, btn);
    btn = IupButton(_("Replace All"));
    IupSetCallback(btn, "ACTION", (Icallback)iup_frd_replaceall_cb);
    IupAppend(btns, btn);
    btn = IupButton(_("Close"));
    IupSetCallback(btn, "ACTION", (Icallback)iup_frd_close_cb);
    IupAppend(btns, btn);

    frd.dialog = IupDialog(IupVbox(grid, frd.repl_line, opts, btns, NULL));
    IupSetAttribute(IupGetChild(frd.dialog, 0), "GAP", "10");
    IupSetAttribute(IupGetChild(frd.dialog, 0), "MARGIN", "12x12");
    IupSetAttribute(frd.dialog, "DIALOGFRAME", "YES");
    IupSetAttributeHandle(frd.dialog, "PARENTDIALOG", giup.dialog);
    IupSetCallback(frd.dialog, "CLOSE_CB", (Icallback)iup_frd_close_cb);
}

    static void
iup_frd_show(char_u *arg, int do_replace)
{
    int		wword = FALSE;
    int		mcase = FALSE;
    char_u	*entry_text;

    if (frd.dialog == NULL)
	iup_frd_create();

    entry_text = get_find_dialog_text(arg, &wword, &mcase);
    if (entry_text != NULL)
    {
	IupSetStrf(frd.what, "VALUE", "%s", (char *)entry_text);
	vim_free(entry_text);
    }
    IupSetInt(frd.wword, "VALUE", wword);
    IupSetInt(frd.mcase, "VALUE", mcase);

    IupSetAttribute(frd.repl_line, "FLOATING", do_replace ? "NO" : "IGNORE");
    IupSetAttribute(frd.repl_line, "VISIBLE", do_replace ? "YES" : "NO");
    IupSetStrf(frd.dialog, "TITLE", "%s",
			  do_replace ? _("Find & Replace") : _("Find"));
    IupSetAttribute(frd.dialog, "RASTERSIZE", NULL);
    IupRefresh(frd.dialog);
    IupShowXY(frd.dialog, IUP_CENTERPARENT, IUP_CENTERPARENT);
    IupSetFocus(frd.what);
}

    void
gui_mch_find_dialog(exarg_T *eap)
{
    iup_frd_show(eap->arg, FALSE);
}

    void
gui_mch_replace_dialog(exarg_T *eap)
{
    iup_frd_show(eap->arg, TRUE);
}
#endif // FIND_REPLACE_DIALOG

#if (defined(FEAT_CLIPBOARD) && !defined(MACOS_X) && !defined(MSWIN)) || defined(PROTO)
#define VIM_SELTYPE_FORMAT "application/x-vim-selectiontype"

    static Ihandle *
iup_get_clipboard(Clipboard_T *cbd)
{
    if (iup_clipboard == NULL)
    {
	iup_clipboard = IupClipboard();
	IupSetAttribute(iup_clipboard, "ADDFORMAT", VIM_SELTYPE_FORMAT);
    }
    IupSetAttribute(iup_clipboard, "SELECTION",
			      cbd == &clip_star ? "PRIMARY" : "CLIPBOARD");
    return iup_clipboard;
}

    int
clip_mch_own_selection(Clipboard_T *cbd UNUSED)
{
    return FAIL;
}

    void
clip_mch_lose_selection(Clipboard_T *cbd UNUSED)
{
}

    void
clip_mch_request_selection(Clipboard_T *cbd)
{
    Ihandle	*clip = iup_get_clipboard(cbd);
    char	*text;
    char_u	*str;
    char_u	*conv_str = NULL;
    int		len;
    int		type = MAUTO;

    IupSetAttribute(clip, "FORMAT", VIM_SELTYPE_FORMAT);
    if (IupGetInt(clip, "FORMATAVAILABLE"))
    {
	char *data = IupGetAttribute(clip, "FORMATDATA");

	if (data != NULL && IupGetInt(clip, "FORMATDATASIZE") >= 1)
	    switch (*data)
	    {
		case 'L': type = MLINE; break;
		case 'C': type = MCHAR; break;
		case 'B': type = MBLOCK; break;
	    }
    }

    text = IupGetAttribute(clip, "TEXT");
    if (text == NULL)
	return;

    str = (char_u *)text;
    len = (int)STRLEN(text);
    if (input_conv.vc_type != CONV_NONE)
    {
	conv_str = string_convert(&input_conv, str, &len);
	if (conv_str != NULL)
	    str = conv_str;
    }

    clip_yank_selection(type, str, (long)len, cbd);
    vim_free(conv_str);
}

    void
clip_mch_set_selection(Clipboard_T *cbd)
{
    Ihandle	*clip;
    char_u	*str = NULL;
    char_u	*conv_str = NULL;
    char_u	*text;
    long_u	count;
    int		type;

    // If the '*' register isn't already filled in, fill it in now.
    cbd->owned = TRUE;
    clip_get_selection(cbd);
    cbd->owned = FALSE;

    type = clip_convert_selection(&str, &count, cbd);
    if (type < 0)
	return;

    if (output_conv.vc_type != CONV_NONE)
    {
	int len = (int)count;

	conv_str = string_convert(&output_conv, str, &len);
	if (conv_str != NULL)
	{
	    vim_free(str);
	    str = conv_str;
	    count = (long_u)len;
	}
    }

    text = alloc(count + 1);
    if (text != NULL)
    {
	mch_memmove(text, str, (size_t)count);
	text[count] = NUL;
	clip = iup_get_clipboard(cbd);
	IupSetAttribute(clip, "TEXT", (char *)text);
	vim_free(text);
    }

    vim_free(str);
}
#endif // FEAT_CLIPBOARD
