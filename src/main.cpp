/*
Copyright (C) 2015 Lauri Kasanen

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Modifications Copyright (C) 2016 Guy Turcotte
*/

#include "main.h"
#include "updf 128x128.h"
#include "updf 64x64.h"
#include "icons 32x32.h"

#include <getopt.h>
#include <ctype.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

using namespace std;
using namespace libconfig;

#define LABEL_SIZE   11
#define CHOICE_SIZE   9

static Fl_Double_Window  * win                     = NULL;
static Fl_Input          * page_input              = NULL;
static Fl_Input_Choice   * zoombar                 = NULL;
static Fl_Light_Button   * selecting               = NULL,
                         * selecting_trim_zone     = NULL,
                         * this_page_trim          = NULL,
                         * diff_trim_zone          = NULL;
       
static Fl_Box            * pagectr                 = NULL;

static PDFView           * view                    = NULL;

static Fl_Pack           * buttons                 = NULL;

static Fl_Pack           * win_pack                = NULL,
                         * my_trim_zone_group      = NULL,
                         * zoom_params_group       = NULL,
                         * trim_zone_params_group  = NULL,
                         * page_moves              = NULL,
                         * page_moves2             = NULL;

static Fl_Button         * show_btn                = NULL,
                         * fullscreen_btn          = NULL;

static Fl_PNG_Image      * fullscreen_image        = NULL,
                         * fullscreenreverse_image = NULL;

static Fl_Choice         * columns                 = NULL,
                         * title_pages             = NULL;

static Fl_Window         * recent_win              = NULL;

static Fl_Select_Browser * recent_select           = NULL,
                         * trim_pages_browser      = NULL;

#if DEBUGGING
  Fl_Box                 * debug1                  = NULL,
                         * debug2                  = NULL,
                         * debug3                  = NULL,
                         * debug4                  = NULL,
                         * debug5                  = NULL,
                         * debug6                  = NULL,
                         * debug7                  = NULL;
#endif

static Fl_Menu_Item menu_zoombar[] = {
  { "Trim",   0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "Width",  0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "Page",   0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "PgTrim", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "MyTrim", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { 0,0,0,0,0,0,0,0,0 }
};

static Fl_Menu_Item menu_columns[] = {
  { "1 Col", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "2 Col", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "3 Col", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "4 Col", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "5 Col", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { 0,0,0,0,0,0,0,0,0 }
};

static Fl_Menu_Item menu_title_pages[] = {
  { "None" , 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "1 TPg", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "2 TPg", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "3 TPg", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { "4 TPg", 0, 0, 0, 0, FL_NORMAL_LABEL, 0, 11, 0 },
  { 0,0,0,0,0,0,0,0,0 }
};

int  writepipe;
bool fullscreen;

u8         details = 0;
openfile * file    = NULL;

//===== Support funtions =====

void save_current_to_config() 
{
  if (file && file->filename) {  
    debug("Saving file config info\n");  
    save_to_config(
      file->filename,
      view->get_columns(),
      view->get_title_page_count(), // title_page_count
      view->get_xoff(),
      view->get_yoff(),
      view->zoom(),
      view->mode(),
      win->x(),
      win->y(),
      win->w(),
      win->h(),
      fullscreen,
      view->get_my_trim());    
  }
}

static void display_zoom(const float what)
{
  char tmp[10];
  snprintf(tmp, 10, "%.0f%%", what * 100);
  zoombar->value(tmp);
}

static void applyzoom(const float zoom) 
{
  view->zoom(zoom);
  view->mode(Z_CUSTOM);

  update_buttons();

  view->take_focus();
  view->redraw();
}

void update_single_trim_list()
{
  char buff[20];

  my_trim_struct & my_trim = view->get_my_trim();
  single_page_trim_struct *trims = my_trim.singles;

  trim_pages_browser->clear();
  while (trims) {
    sprintf(buff, "%d", trims->page + 1);
    trim_pages_browser->add(buff);
    trims = trims->next;
  }
}

void cb_fullscreen(Fl_Button *, void *);

static void adjust_display_from_recent(recent_file_struct & recent)
{
  view->set_params(recent);

  update_single_trim_list();
  update_buttons();

  win->damage(FL_DAMAGE_ALL);
  win->flush();

  fullscreen = !recent.fullscreen; // Must be inversed...
  cb_fullscreen(NULL, NULL);       // ...as cb_fullscreen is a toggle

  win->resize(recent.x, recent.y, recent.width, recent.height);
  //buttons->resize(0, 0, 64, recent.height);
}

static void reader(FL_SOCKET fd, void *) 
{
  // A thread has something to say to the main thread.
  u8 buf;
  sread(fd, &buf, 1);

  switch (buf) {
    case MSG_REFRESH:
      view->redraw();
    break;
    case MSG_READY:
      fl_cursor(FL_CURSOR_DEFAULT);
    break;
    default:
      die(_("Unrecognized thread message\n"));
  }
}

static void checkX() 
{
  // Make sure everything's cool
  if (fl_visual->red_mask != 0xff0000 ||
    fl_visual->green_mask != 0xff00 ||
    fl_visual->blue_mask != 0xff) {
    die(_("Visual doesn't match our expectations\n"));
  }

  int a, b;
  if (!XRenderQueryExtension(fl_display, &a, &b)) {
    die(_("No XRender on this server\n"));
  }
}

void update_buttons()
{
  if (file && file->filename) {

            page_input->activate();
               pagectr->activate();
               zoombar->activate();
            page_moves->activate();
           page_moves2->activate();

    if ( zoom_params_group->visible())  zoom_params_group->activate();
    if (my_trim_zone_group->visible()) my_trim_zone_group->activate();
  }
  else {
    
            page_input->deactivate();
               pagectr->deactivate();
               zoombar->deactivate();
    my_trim_zone_group->deactivate();
     zoom_params_group->deactivate();
            page_moves->deactivate();
           page_moves2->deactivate();
  }

  if (view->mode() == Z_MYTRIM) {
    my_trim_zone_group->show();
    if (selecting_trim_zone->value() != 0) {
      trim_zone_params_group->show();
      diff_trim_zone->value(view->get_my_trim().similar ? 0 : 1);
      this_page_trim->value(view->is_single_page_trim() ? 1 : 0);
      zoom_params_group->hide();
    }
    else {
      trim_zone_params_group->hide();
      zoom_params_group->show();        
    }
  }
  else {
    selecting_trim_zone->value(0);
    this_page_trim->value(0);
    my_trim_zone_group->hide();
    zoom_params_group->show();        
  }

  columns->value(view->get_columns() - 1);
  title_pages->value(view->get_title_page_count());

  if (view->mode() == Z_CUSTOM) {
    display_zoom(view->zoom());
  }
  else {
    zoombar->value(menu_zoombar[view->mode()].text);
  }

  s32 page = view->get_yoff();
  char buf[10];
  snprintf(buf, 10, "%u", page + 1);
  page_input->value(buf);

  buttons->damage(FL_DAMAGE_ALL);
}

void file_loaded()
{
  // Update title
  char tmp[160];
  snprintf(tmp, 160, "%s - uPDF", basename(file->filename));
  tmp[159] = 0;
  win->copy_label(tmp);

  // Update page count
  snprintf(tmp, 160, "/ %u", ::file->pages);
  pagectr->copy_label(tmp);
  page_input->value("1");

  view->reset();  
}

//===== CallBack Functions ======

void cb_exit(Fl_Widget *, void *) 
{
  save_current_to_config();
  save_config();
  exit(0);
}

void cb_fullscreen(Fl_Button *, void *) 
{
  if (fullscreen) {
    fullscreen = false;
    fullscreen_btn->image(fullscreen_image);
    fullscreen_btn->tooltip(_("Enter Full Screen"));
    win->fullscreen_off();
  }
  else {
    fullscreen = true;
    fullscreen_btn->image(fullscreenreverse_image);
    fullscreen_btn->tooltip(_("Exit Full Screen"));
    win->fullscreen();
  }
  view->take_focus();
}

static void cb_Open(Fl_Button *, void *) 
{
  save_current_to_config();
  if (loadfile(NULL, NULL)) {
    file_loaded();
  }
  view->new_file_loaded();
  view->take_focus();
  update_buttons();
}

static void cb_zoombar(Fl_Input_Choice * w, void *) 
{
  const char * const val = w->value();
  if (isdigit(val[0])) {
    const float f = atof(val) / 100;
    applyzoom(f);
  } else {
    if (!strcmp(val, menu_zoombar[0].text)) {
      view->mode(Z_TRIM);
    } else if (!strcmp(val, menu_zoombar[1].text)) {
      view->mode(Z_WIDTH);
    } else if (!strcmp(val, menu_zoombar[2].text)) {
      view->mode(Z_PAGE);
    } else if (!strcmp(val, menu_zoombar[3].text)) {
      view->mode(Z_PGTRIM);
    } else if (!strcmp(val, menu_zoombar[4].text)) {
      view->mode(Z_MYTRIM);
    } else {
      //fl_alert(_("Unrecognized zoom level"));
    }

    update_buttons();
  }

  view->take_focus();
  view->redraw();
}

void cb_recent_select(Fl_Select_Browser *, void *)
{
  recent_win->hide();

  int i = recent_select->value();

  if (i <= 0) return;

  i -= 1;

  recent_file_struct *rf = recent_files;
  while ((i > 0) && (rf != NULL)) {
    rf = rf->next;
    i -= 1;
  }

  save_current_to_config();
  if ((rf != NULL) && loadfile(NULL, rf)) {
    file_loaded();
    adjust_display_from_recent(*rf);
    view->take_focus();
  }
}

static void cb_OpenRecent(Fl_Button *, void *) 
{
  if (recent_win == NULL){
    recent_win = new Fl_Window(
      win->x() + 30, 
      win->y() + 100, 450, 180, "Recent Files");
    Fl_Pack *p = new Fl_Pack(10, 10, 430, 160);
    p->type(1);
    {
      recent_select = new Fl_Select_Browser(0, 0, 430, 160);
      recent_select->callback((Fl_Callback *)cb_recent_select);
      recent_select->resizable();
      recent_select->show();
    }
    p->resizable();
  }
  else 
    recent_select->clear();

  recent_file_struct *rf = recent_files;
  while (rf != NULL) {
    recent_select->add(basename(rf->filename.c_str()));
    rf = rf->next;
  }

  recent_win->set_modal();
  recent_win->show();
}

static void cb_columns(Fl_Choice * w, void *) 
{
  const int cols = w->value();
  view->set_columns(cols + 1);
}

static void cb_title_pages(Fl_Choice * tp, void *)
{
  const int tpages = tp->value();
  view->set_title_page_count(tpages);
}

void cb_page_up(Fl_Button *, void *) 
{
  view->page_up();
  view->take_focus();
  update_buttons();
}

void cb_page_down(Fl_Button *, void *) 
{
  view->page_down();
  view->take_focus();
  update_buttons();
}

void cb_page_top(Fl_Button *, void *) 
{
  view->page_top();
  view->take_focus();
  update_buttons();
}

void cb_page_bottom(Fl_Button *, void *) 
{
  view->page_bottom();
  view->take_focus();
  update_buttons();
}

void cb_zoomin(Fl_Button *, void *) 
{
  view->zoom(view->zoom() * 1.2f);
  applyzoom(view->zoom());
  view->take_focus();
}

void cb_zoomout(Fl_Button *, void *) 
{
  view->zoom(view->zoom() * 0.833333);
  applyzoom(view->zoom());
  view->take_focus();
}

void cb_hide_show_buttons(Fl_Widget *, void *) 
{
  if (buttons->visible()) {
    buttons->hide();
    show_btn->show();
  } else {
    show_btn->hide();
    buttons->show();
  }

  view->take_focus();
}

static void cb_goto_page(Fl_Input * w, void*) 
{
  const u32 page = atoi(w->value()) - 1;
  if (page >= file->pages) {
    fl_alert(_("Page out of bounds"));
    return;
  }

  view->goto_page(page);
  update_buttons();
}

static void cb_select_text(Fl_Widget * w, void *) 
{
  selecting_trim_zone->value(0);
  view->text_select(((Fl_Light_Button *) w)->value() != 0);
}

static void cb_select_trim_zone(Fl_Widget * w, void *)
{
  selecting->value(0);
  
  view->trim_zone_select(((Fl_Light_Button *)w)->value() != 0);
  update_single_trim_list();
  update_buttons();
}

static void cb_diff_trim_zone_selected(Fl_Widget * w, void *)
{
  view->trim_zone_different(((Fl_Light_Button *) w)->value() != 0);
}

static void cb_this_page_trim(Fl_Widget * w, void *)
{
  view->this_page_trim(((Fl_Light_Button *) w)->value() != 0);
  update_single_trim_list();
}

static void cb_clear_all_trim_pages(Fl_Widget *, void *)
{
  view->clear_all_single_trims();
  update_single_trim_list();
}

static void cb_trim_pages_browser(Fl_Widget *, void *)
{
  const char *data;

  if (trim_pages_browser->value()) {
    data = trim_pages_browser->text(trim_pages_browser->value());
    view->goto_page(atoi(data) - 1);
  }
}

//===== MAIN FUNCTION =====

int main(int argc, char **argv) 
{

  srand(time(NULL));

  #if ENABLE_NLS
    setlocale(LC_MESSAGES, "");
    bindtextdomain("updf", LOCALEDIR);
    textdomain("updf");
  #endif

  const struct option opts[] = {
    { "details", 0, NULL, 'd' },
    { "help",    0, NULL, 'h' },
    { "version", 0, NULL, 'v' },
    { NULL,      0, NULL,  0  }
  };

  while (1) {
    const int c = getopt_long(argc, argv, "dhv", opts, NULL);
    if (c == -1)
      break;

    switch (c) {
      case 'd':
        details++;
      break;
      case 'v':
        printf("%s\n", PACKAGE_STRING);
        return 0;
      break;
      case 'h':
      default:
        printf(_("Usage: %s [options] file.pdf\n\n"
          "   -d --details    Print RAM, timing details (use twice for more)\n"
          "   -h --help   This help\n"
          "   -v --version    Print version\n"),
          argv[0]);
        return 0;
      break;
    }
  }

  Fl::scheme("gtk+");
  Fl_File_Icon::load_system_icons();

  Fl::set_font(FL_NONO_FONT, "Nono Sans Regular");

  file = (openfile *) xcalloc(1, sizeof(openfile));
  int ptmp[2];
  if (pipe(ptmp))
    die(_("Failed in pipe()\n"));
  writepipe = ptmp[1];

  Fl::add_fd(ptmp[0], FL_READ, reader);

  #define img(a) a, sizeof(a)

  fullscreen_image        = new Fl_PNG_Image("fullscreen.png",        img(view_fullscreen_png));
  fullscreenreverse_image = new Fl_PNG_Image("fullscreenreverse.png", img(view_restore_png   ));

  fullscreen = false;

  win      = new Fl_Double_Window(700, 800, "uPDF");
  win_pack = new Fl_Pack(0, 0, 700, 800);
  win_pack->type(1);
  { buttons = new Fl_Pack(0, 0, 64, 800);
    buttons->spacing(4);
    { Fl_Box * icon_box = new Fl_Box(0, 0, 64, 64);
      icon_box->image(new Fl_PNG_Image("updf 64x64.png", img(updf_64x64_png))); }
    { Fl_Button * hide_btn = new Fl_Button(0, 0, 64, 32);
      hide_btn->tooltip(_("Hide toolbar (F8)"));
      hide_btn->callback(cb_hide_show_buttons);
      hide_btn->image(new Fl_PNG_Image("back.png", img(media_seek_backward_png))); }
    { Fl_Button * open_btn = new Fl_Button(0, 0, 64, 48);
      open_btn->tooltip(_("Open a new file"));
      open_btn->callback((Fl_Callback *)cb_Open);
      open_btn->image(new Fl_PNG_Image("fileopen.png", img(document_open_png))); }
    { Fl_Button * recent_select_btn = new Fl_Button(0, 0, 64, 48);
      recent_select_btn->tooltip(_("Open a recent file"));
      recent_select_btn->callback((Fl_Callback *)cb_OpenRecent);
      recent_select_btn->image(new Fl_PNG_Image("fileopen.png", img(emblem_documents_png))); }
    { page_input = new Fl_Input(0, 0, 64, 24);
      page_input->value("0");
      page_input->callback((Fl_Callback *)cb_goto_page);
      page_input->when(FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED);
      page_input->textsize(LABEL_SIZE); }
    { pagectr = new Fl_Box(0, 0, 64, 24, "/ 0");
      pagectr->tooltip(_("Document Page Count"));
      pagectr->box(FL_ENGRAVED_FRAME);
      pagectr->align(FL_ALIGN_WRAP);
      pagectr->labelsize(LABEL_SIZE); }
    { zoombar = new Fl_Input_Choice(0, 0, 64, 24);
      zoombar->tooltip(_("Preset zoom choices"));
      zoombar->callback((Fl_Callback *)cb_zoombar);
      zoombar->menu(menu_zoombar);
      zoombar->value(0);
      zoombar->textsize(CHOICE_SIZE);
      zoombar->input()->when(FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED); }
    { my_trim_zone_group = new Fl_Pack(0, 0, 64, 184);
      { Fl_Box * trim_title = new Fl_Box(0, 0, 64, 24, "Trim Parameters");
        trim_title->align(FL_ALIGN_WRAP);
        trim_title->labelfont(FL_TIMES);
        trim_title->labelsize(9); }
      { selecting_trim_zone = new Fl_Light_Button(0, 0, 64, 24);
        selecting_trim_zone->tooltip(_("Select Trim Zone"));
        selecting_trim_zone->label("Do Trim");
        selecting_trim_zone->labelsize(LABEL_SIZE);
        selecting_trim_zone->callback(cb_select_trim_zone); }
      { trim_zone_params_group = new Fl_Pack(0, 0, 64, 136);
        trim_zone_params_group->spacing(2);
        { diff_trim_zone = new Fl_Light_Button(0, 0, 64, 24);
          diff_trim_zone->tooltip(_("Even/Odd Pages use Different Trimming"));
          diff_trim_zone->label("Evn/Odd");
          diff_trim_zone->labelsize(LABEL_SIZE);
          diff_trim_zone->callback(cb_diff_trim_zone_selected); }
        { this_page_trim = new Fl_Light_Button(0, 0, 64, 24);
          this_page_trim->tooltip(_("Specific page trim"));
          this_page_trim->label("This Pg");
          this_page_trim->labelsize(LABEL_SIZE);
          this_page_trim->callback(cb_this_page_trim); }
        { trim_pages_browser = new Fl_Select_Browser(0, 0, 64, 64); 
          trim_pages_browser->labelsize(LABEL_SIZE);
          trim_pages_browser->tooltip(_("Specific page trim select"));
          trim_pages_browser->callback(cb_trim_pages_browser); }
        { Fl_Button * clr_all_btn = new Fl_Button(0, 0, 64, 24);
          clr_all_btn->tooltip(_("Clear all specific page trims"));
          clr_all_btn->label("Clr All");
          clr_all_btn->labelsize(LABEL_SIZE);
          clr_all_btn->callback(cb_clear_all_trim_pages); }
        trim_zone_params_group->hide();
        trim_zone_params_group->end(); }
      my_trim_zone_group->hide();
      my_trim_zone_group->spacing(2);
      my_trim_zone_group->end(); }
    { zoom_params_group = new Fl_Pack(0, 0, 64, 118);
      { Fl_Pack * zooms = new Fl_Pack(0, 0, 64, 32);
        zooms->type(Fl_Pack::HORIZONTAL);
        { Fl_Button * o = new Fl_Button(0, 0, 32, 32);
          o->tooltip(_("Zoom in"));
          o->callback((Fl_Callback *)cb_zoomin);
          o->image(new Fl_PNG_Image("zoomin.png", img(zoom_in_png))); }
        { Fl_Button * o = new Fl_Button(0, 0, 32, 32);
          o->tooltip(_("Zoom out"));
          o->callback((Fl_Callback *)cb_zoomout);
          o->image(new Fl_PNG_Image("zoomout.png", img(zoom_out_png))); }
        zooms->end();
        zooms->show(); }
      { selecting = new Fl_Light_Button(0, 0, 64, 38);
        selecting->tooltip(_("Select text"));
        selecting->align(FL_ALIGN_CENTER);
        selecting->image(new Fl_PNG_Image("text.png", img(edit_select_all_png)));
        selecting->callback(cb_select_text); }
      { columns = new Fl_Choice(0, 0, 64, 24);
        columns->tooltip(_("Number of Columns"));
        columns->callback((Fl_Callback *)cb_columns);
        columns->menu(menu_columns);
        columns->value(0);
        columns->labelsize(LABEL_SIZE);
        columns->align(FL_ALIGN_CENTER); }
      { title_pages = new Fl_Choice(0, 0, 64, 24);
        title_pages->tooltip(_("Number of Title Pages"));
        title_pages->callback((Fl_Callback *)cb_title_pages);
        title_pages->menu(menu_title_pages);
        title_pages->value(0);
        title_pages->labelsize(LABEL_SIZE);
        title_pages->align(FL_ALIGN_CENTER); }
      zoom_params_group->spacing(4);
      zoom_params_group->show();
      zoom_params_group->end(); }
    { page_moves = new Fl_Pack(0, 0, 64, 42);
      page_moves->type(Fl_Pack::HORIZONTAL);
      { Fl_Button * o = new Fl_Button(0, 0, 32, 42);
        o->tooltip(_("Beginning of Document"));
        o->callback((Fl_Callback *)cb_page_top);
        o->image(new Fl_PNG_Image("pagetop.png", img(go_top_png))); }
      { Fl_Button * o = new Fl_Button(0, 0, 32, 42);
        o->tooltip(_("Previous Page"));
        o->callback((Fl_Callback *)cb_page_up);
        o->image(new Fl_PNG_Image("pageup.png", img(go_up_png))); }
      page_moves->end();
      page_moves->show(); }
    { page_moves2 = new Fl_Pack(0, 0, 64, 42);
      page_moves2->type(Fl_Pack::HORIZONTAL);
      { Fl_Button * o = new Fl_Button(0, 0, 32, 42);
        o->tooltip(_("End of Document"));
        o->callback((Fl_Callback *)cb_page_bottom);
        o->image(new Fl_PNG_Image("pagebottom.png", img(go_bottom_png))); }
      { Fl_Button * o = new Fl_Button(0, 0, 32, 42);
        o->tooltip(_("Next Page"));
        o->callback((Fl_Callback *)cb_page_down);
        o->image(new Fl_PNG_Image("pagedown.png", img(go_down_png))); }
      page_moves2->end();
      page_moves2->show(); }
    { fullscreen_btn = new Fl_Button(0, 0, 64, 38);
      fullscreen_btn->image(fullscreen_image);
      fullscreen_btn->callback((Fl_Callback *)cb_fullscreen);
      fullscreen_btn->tooltip(_("Enter Full Screen")); }

    #if DEBUGGING && 0
      { debug1 = new Fl_Box(0, 0, 64, 32, "");
        debug1->align(FL_ALIGN_WRAP); }
      { debug2 = new Fl_Box(0, 0, 64, 32, "");
        debug2->align(FL_ALIGN_WRAP); }
      { debug3 = new Fl_Box(0, 0, 64, 32, "");
        debug3->align(FL_ALIGN_WRAP); }
      { debug4 = new Fl_Box(0, 0, 64, 32, "");
        debug4->align(FL_ALIGN_WRAP); }
      { debug5 = new Fl_Box(0, 0, 64, 32, "");
        debug5->align(FL_ALIGN_WRAP); }
      { debug6 = new Fl_Box(0, 0, 64, 32, "");
        debug6->align(FL_ALIGN_WRAP); }
      { debug7 = new Fl_Box(0, 0, 64, 32, "");
        debug7->align(FL_ALIGN_WRAP); }
    #endif

    { Fl_Box * fill_box = new Fl_Box(0, 0, 64, 0, "");
      fill_box->box(FL_ENGRAVED_FRAME);
      buttons->resizable(fill_box); }
    { Fl_Button * exitbtn = new Fl_Button(0, 0, 64, 38);
      exitbtn->callback((Fl_Callback *)cb_exit);
      exitbtn->image(new Fl_PNG_Image("exit.png", img(application_exit_png)));
      exitbtn->tooltip(_("Exit")); }
    buttons->end();
    buttons->show(); 
  }

  { show_btn = new Fl_Button(0, 0, 6, 800);
    show_btn->hide();
    show_btn->callback(cb_hide_show_buttons);
  }
  { view = new PDFView(64, 0, 700-64, 800);
    view->show();
  }

  win_pack->resizable(view);
  win_pack->end();
  win_pack->show();

  win->resizable(win_pack);
  win->callback((Fl_Callback *)cb_exit);
  win->size_range(500, 500);
  win->end();

  Fl_PNG_Image updf_icon("updf-128x128.png", img(updf_128x128_png));
  win->icon(&updf_icon);

  fl_open_display();
  win->show();
  checkX();

  #undef img

  if (lzo_init() != LZO_E_OK) {
    die(_("LZO init failed\n"));
  }

  // Set the width to half of the screen, 90% of height
  // win->size(Fl::w() * 0.4f, Fl::h() * 0.9f);

  load_config();

  update_buttons();

  if (loadfile(optind < argc ? argv[optind] : NULL, recent_files)) {
    file_loaded();
    adjust_display_from_recent(*recent_files);
  }
  else {
    update_buttons();
  }

  view->take_focus();

  return Fl::run();
}
