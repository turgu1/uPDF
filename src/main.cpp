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

       Fl_Double_Window  * win                     = NULL;
       PDFView           * view                    = NULL;
       Fl_Box            * pagectr                 = NULL,
                         * fill_box                = NULL;
       Fl_Input          * pagebox                 = NULL;
       Fl_Input_Choice   * zoombar                 = NULL;
       Fl_Light_Button   * selecting               = NULL,
                         * selecting_trim_zone     = NULL,
                         * this_page_trim          = NULL,
                         * diff_trim_zone          = NULL;
       
       Fl_Box            * debug1                  = NULL,
                         * debug2                  = NULL,
                         * debug3                  = NULL,
                         * debug4                  = NULL,
                         * debug5                  = NULL,
                         * debug6                  = NULL,
                         * debug7                  = NULL;
       
static Fl_Pack           * win_pack                = NULL;
static Fl_Pack           * buttons                 = NULL;
static Fl_Pack           * my_trim_zone_params     = NULL;
static Fl_Button         * showbtn                 = NULL;
static Fl_Button         * fullscreenbtn           = NULL;
static Fl_Button         * recentselectbtn         = NULL;

static Fl_PNG_Image      * fullscreen_image        = NULL;
static Fl_PNG_Image      * fullscreenreverse_image = NULL;

static Fl_Choice         * columns                 = NULL;
static Fl_Choice         * title_pages             = NULL;

static Fl_Window         * recent_win              = NULL;
static Fl_Select_Browser * recent_select           = NULL;

int  writepipe;
bool fullscreen;

u8         details = 0;
openfile * file    = NULL;

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
      file->zoom,
      view->mode(),
      win->x(),
      win->y(),
      win->w(),
      win->h(),
      fullscreen,
      view->get_my_trim());    
  }
}

void cb_exit(Fl_Widget*, void*) 
{
  save_current_to_config();
  save_config();
  exit(0);
}

void cb_fullscreen(Fl_Button*, void*) 
{
  if (fullscreen) {
    fullscreen = false;
    fullscreenbtn->image(fullscreen_image);
    fullscreenbtn->tooltip(_("Enter Full Screen"));
    win->fullscreen_off();
  }
  else {
    fullscreen = true;
    fullscreenbtn->image(fullscreenreverse_image);
    fullscreenbtn->tooltip(_("Exit Full Screen"));
    win->fullscreen();
  }
  view->take_focus();
}

static void cb_Open(Fl_Button*, void*) 
{
  save_current_to_config();
  loadfile(NULL, NULL);
  view->take_focus();
}

static void display_zoom(const float what)
{
  char tmp[10];
  snprintf(tmp, 10, "%.0f%%", what * 100);
  zoombar->value(tmp);
}

static void applyzoom(const float what) 
{
  file->zoom = what > 0.01f ? what : 0.01f;
  view->mode(Z_CUSTOM);

  display_zoom(what);

  view->take_focus();
  view->reset_selection();
  view->redraw();
}

static void cb_zoombar(Fl_Input_Choice *w, void*) 
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

    if (view->mode() == Z_MYTRIM)
      my_trim_zone_params->show();
    else
      my_trim_zone_params->hide();
  }

  view->take_focus();
  view->redraw();
}

static void adjust_display_from_recent(recent_file_struct &recent)
{
  view->set_params(recent);
  if ((recent.columns >= 1) && (recent.columns <= 5)) {
    columns->value(recent.columns - 1);
  }

  if ((recent.title_page_count >= 0) && (recent.title_page_count <= 4)) {
    title_pages->value(recent.title_page_count);
  }

  if (recent.view_mode == Z_CUSTOM)
    display_zoom(recent.zoom);
  else
    zoombar->value(menu_zoombar[recent.view_mode].text);


  if (recent.view_mode == Z_MYTRIM)
    my_trim_zone_params->show();
  else
    my_trim_zone_params->hide();

  win->damage(FL_DAMAGE_ALL);
  win->flush();

  fullscreen = !recent.fullscreen; // Must be inversed...
  cb_fullscreen(NULL, NULL);       // ...as cb_fullscreen is a toggle

  win->position(recent.x, recent.y);
  win->size(recent.width, recent.height);
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
    adjust_display_from_recent(*rf);
    view->take_focus();
  }
}

static void cb_OpenRecent(Fl_Button*, void*) 
{
  if (recent_win == NULL){
    recent_win = new Fl_Window(
      win->x() + 30, 
      win->y() + 100, 450, 180, "Recent Files");
    Fl_Pack *p = new Fl_Pack(10, 10, 430, 160);
    p->type(1);
    {
      recent_select = new Fl_Select_Browser(0, 0, 430, 160);
      recent_select->callback((Fl_Callback*)cb_recent_select);
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

static void cb_columns(Fl_Choice *w, void*) 
{
  const int cols = w->value();
  view->set_columns(cols + 1);
}

static void cb_title_pages(Fl_Choice *tp, void*)
{
  const int tpages = tp->value();
  view->set_title_page_count(tpages);
}

void cb_page_up(Fl_Button*, void*) 
{
  view->page_up();
  view->take_focus();
}

void cb_page_down(Fl_Button*, void*) 
{
  view->page_down();
  view->take_focus();
}

void cb_page_top(Fl_Button*, void*) 
{
  view->page_top();
  view->take_focus();
}

void cb_page_bottom(Fl_Button*, void*) 
{
  view->page_bottom();
  view->take_focus();
}

void cb_Zoomin(Fl_Button*, void*) 
{
  file->zoom *= 1.2f;
  applyzoom(file->zoom);
  view->take_focus();
}

void cb_Zoomout(Fl_Button*, void*) 
{
  file->zoom *= 0.833333;
  applyzoom(file->zoom);
  view->take_focus();
}

void cb_hide(Fl_Widget*, void*) 
{

  if (buttons->visible()) {
    buttons->hide();
    showbtn->show();
  } else {
    showbtn->hide();
    buttons->show();
  }
  view->take_focus();
}

static void cb_goto_page(Fl_Input *w, void*) 
{
  const u32 which = atoi(w->value()) - 1;
  if (which >= file->pages) {
    fl_alert(_("Page out of bounds"));
    return;
  }

  view->go(which);
}

static void reader(FL_SOCKET fd, void*) 
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
    fl_visual->blue_mask != 0xff)
    die(_("Visual doesn't match our expectations\n"));

  int a, b;
  if (!XRenderQueryExtension(fl_display, &a, &b))
    die(_("No XRender on this server\n"));
}

static void cb_select_text(Fl_Widget * w, void *) 
{
  selecting_trim_zone->value(0);
  view->text_select(((Fl_Light_Button *)w)->value() != 0);
}

static void cb_select_trim_zone(Fl_Widget * w, void *)
{
  selecting->value(0);
  view->trim_zone_select(((Fl_Light_Button *)w)->value() != 0);
}

static void cb_diff_trim_zone_selected(Fl_Widget * w, void *)
{
  view->trim_zone_different(((Fl_Light_Button *)w)->value() != 0);
}

static void cb_this_page_trim(Fl_Widget * w, void *)
{

}

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

  file = (openfile *) xcalloc(1, sizeof(openfile));
  int ptmp[2];
  if (pipe(ptmp))
    die(_("Failed in pipe()\n"));
  writepipe = ptmp[1];

  Fl::add_fd(ptmp[0], FL_READ, reader);

  #define img(a) a, sizeof(a)

  fullscreen_image = new Fl_PNG_Image("fullscreen.png", img(view_fullscreen_png));
  fullscreenreverse_image = new Fl_PNG_Image("fullscreenreverse.png", img(view_restore_png));

  fullscreen = false;

  int pos;

  win = new Fl_Double_Window(700, 700, "uPDF");
  win_pack = new Fl_Pack(0, 0, 700, 700);
  win_pack->type(1);

  { buttons = new Fl_Pack(0, 0, 64, 700);
    { Fl_Pack* buttons_a = new Fl_Pack(0, 0, 64, 700-38);
      { Fl_Box* b = new Fl_Box(0, pos = 0, 64, 64);
        b->image(new Fl_PNG_Image("updf 64x64.png", img(updf_64x64_png)));
      } // (Fl_Box*b )
      { Fl_Button* o = new Fl_Button(0, pos += 60, 64, 32);
        o->tooltip(_("Hide toolbar (F8)"));
        o->callback(cb_hide);
        o->image(new Fl_PNG_Image("back.png", img(media_seek_backward_png)));
      } // Fl_Button* o
      { Fl_Button* o = new Fl_Button(0, pos += 32, 64, 48);
        o->tooltip(_("Open a new file"));
        o->callback((Fl_Callback*)cb_Open);
        o->image(new Fl_PNG_Image("fileopen.png", img(document_open_png)));
      } // Fl_Button* o
      { Fl_Button* recentselectbtn = new Fl_Button(0, pos += 48, 64, 48);
        recentselectbtn->tooltip(_("Open a recent file"));
        recentselectbtn->callback((Fl_Callback*)cb_OpenRecent);
        recentselectbtn->image(new Fl_PNG_Image("fileopen.png", img(emblem_documents_png)));
      } // Fl_Button* recentselectbtn
      { pagebox = new Fl_Input(0, pos += 48, 64, 24);
        pagebox->value("0");
        pagebox->callback((Fl_Callback*)cb_goto_page);
        pagebox->when(FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED);
        pagebox->textsize(LABEL_SIZE);
      } // Fl_Box* pagectr
      { pagectr = new Fl_Box(0, pos += 24, 64, 24, "/ 0");
        pagectr->tooltip(_("Document Page Count"));
        pagectr->box(FL_ENGRAVED_FRAME);
        pagectr->align(FL_ALIGN_WRAP);
        pagectr->labelsize(LABEL_SIZE);
      } // Fl_Box* pagectr
      { zoombar = new Fl_Input_Choice(0, pos += 24, 64, 24);
        zoombar->tooltip(_("Preset zoom choices"));
        zoombar->callback((Fl_Callback*)cb_zoombar);
        zoombar->menu(menu_zoombar);
        zoombar->value(0);
        zoombar->textsize(CHOICE_SIZE);
        zoombar->input()->when(FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED);
      } // Fl_Input_Choice* zoombar
      { Fl_Pack* zooms = new Fl_Pack(0, pos += 24, 64, 32);
        zooms->type(Fl_Pack::HORIZONTAL);
        { Fl_Button* o = new Fl_Button(0, 0, 32, 32);
          o->tooltip(_("Zoom in"));
          o->callback((Fl_Callback*)cb_Zoomin);
          o->image(new Fl_PNG_Image("zoomin.png", img(zoom_in_png)));
        } // Fl_Button* o
        { Fl_Button* o = new Fl_Button(32, 0, 32, 32);
          o->tooltip(_("Zoom out"));
          o->callback((Fl_Callback*)cb_Zoomout);
          o->image(new Fl_PNG_Image("zoomout.png", img(zoom_out_png)));
        } // Fl_Button* o
        zooms->end();
        zooms->show();
      } // Fl_Pack* zooms
      { selecting = new Fl_Light_Button(0, pos += 32, 64, 38);
        selecting->tooltip(_("Select text"));
        selecting->align(FL_ALIGN_CENTER);
        selecting->image(new Fl_PNG_Image("text.png", img(edit_select_all_png)));
        selecting->callback(cb_select_text);
      } // Fl_Light_Button* selecting

      { columns = new Fl_Choice(0, pos += 38, 64, 24);
        columns->tooltip(_("Number of Columns"));
        columns->callback((Fl_Callback*)cb_columns);
        columns->menu(menu_columns);
        columns->value(0);
        columns->labelsize(LABEL_SIZE);
        columns->align(FL_ALIGN_CENTER);
      } // Fl_Input_Choice* zoombar
      { title_pages = new Fl_Choice(0, pos += 24, 64, 24);
        title_pages->tooltip(_("Number of Title Pages"));
        title_pages->callback((Fl_Callback*)cb_title_pages);
        title_pages->menu(menu_title_pages);
        title_pages->value(0);
        title_pages->labelsize(LABEL_SIZE);
        title_pages->align(FL_ALIGN_CENTER);
      } // Fl_Input_Choice* zoombar
      { Fl_Pack* page_moves = new Fl_Pack(0, pos += 24, 64, 42);
        page_moves->type(Fl_Pack::HORIZONTAL);
        { Fl_Button* o = new Fl_Button(0, 0, 32, 42);
          o->tooltip(_("Beginning of Document"));
          o->callback((Fl_Callback*)cb_page_top);
          o->image(new Fl_PNG_Image("pagetop.png", img(go_top_png)));
        } // Fl_Button* o
        { Fl_Button* o = new Fl_Button(32, 0, 32, 42);
          o->tooltip(_("Previous Page"));
          o->callback((Fl_Callback*)cb_page_up);
          o->image(new Fl_PNG_Image("pageup.png", img(go_up_png)));
        } // Fl_Button* o
        page_moves->end();
        page_moves->show();
      } // Fl_Pack* page_moves
      { Fl_Pack* page_moves2 = new Fl_Pack(0, pos += 42, 64, 42);
        page_moves2->type(Fl_Pack::HORIZONTAL);
        { Fl_Button* o = new Fl_Button(0, 0, 32, 42);
          o->tooltip(_("End of Document"));
          o->callback((Fl_Callback*)cb_page_bottom);
          o->image(new Fl_PNG_Image("pagebottom.png", img(go_bottom_png)));
        } // Fl_Button* o
        { Fl_Button* o = new Fl_Button(32, 0, 32, 42);
          o->tooltip(_("Next Page"));
          o->callback((Fl_Callback*)cb_page_down);
          o->image(new Fl_PNG_Image("pagedown.png", img(go_down_png)));
        } // Fl_Button* o
        page_moves2->end();
        page_moves2->show();
      } // Fl_Pack* page_moves2
      { fullscreenbtn = new Fl_Button(0, pos += 42, 64, 38);
        fullscreenbtn->image(fullscreen_image);
        fullscreenbtn->callback((Fl_Callback*)cb_fullscreen);
        fullscreenbtn->tooltip(_("Enter Full Screen"));
      }

      { my_trim_zone_params = new Fl_Pack(0, pos += 38, 64, 96);
        { Fl_Box* trim_title = new Fl_Box(0, 0, 64, 24, "Trim Parameters");
          trim_title->align(FL_ALIGN_WRAP);
          trim_title->labelsize(9);
        } // Fl_Box* pagectr
        { selecting_trim_zone = new Fl_Light_Button(0, 24, 64, 24);
          selecting_trim_zone->tooltip(_("Select Trim Zone on a single page"));
          selecting_trim_zone->label(" Trim");
          selecting_trim_zone->labelsize(LABEL_SIZE);
          selecting_trim_zone->callback(cb_select_trim_zone);
        } // Fl_Light_Button* trim_zone

        { diff_trim_zone = new Fl_Light_Button(0, 48, 64, 24);
          diff_trim_zone->tooltip(_("Even/Odd Pages use Different Trimming"));
          diff_trim_zone->label(" Diff");
          diff_trim_zone->labelsize(LABEL_SIZE);
          diff_trim_zone->callback(cb_diff_trim_zone_selected);
        } // Fl_Light_Button* diff_trim_zone

        { this_page_trim = new Fl_Light_Button(0, 48, 64, 24);
          this_page_trim->tooltip(_("This page only"));
          this_page_trim->label(" This");
          this_page_trim->labelsize(LABEL_SIZE);
          this_page_trim->callback(cb_this_page_trim);
        } // Fl_Light_Button* diff_trim_zone

        my_trim_zone_params->hide();
        my_trim_zone_params->spacing(2);
        my_trim_zone_params->end();
      } // Fl_Pack* my_trim_zone

      #if DEBUGGING
        { debug1 = new Fl_Box(0, pos += 96, 64, 32, "");
          debug1->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug1
        { debug2 = new Fl_Box(0, pos += 32, 64, 32, "");
          debug2->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug2
        { debug3 = new Fl_Box(0, pos += 32, 64, 32, "");
          debug3->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug3
        { debug4 = new Fl_Box(0, pos += 32, 64, 32, "");
          debug4->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug4
        { debug5 = new Fl_Box(0, pos += 32, 64, 32, "");
          debug5->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug5
        { debug6 = new Fl_Box(0, pos += 32, 64, 32, "");
          debug6->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug6
        { debug7 = new Fl_Box(0, pos += 32, 64, 32, "");
          debug7->align(FL_ALIGN_WRAP);
        } // Fl_Box* debug7
      #endif

      { fill_box = new Fl_Box(0, pos += 72, 64, 64, "");
        buttons_a->resizable(fill_box);
      }

      buttons->resizable(buttons_a);
      buttons_a->end();
      buttons_a->spacing(4);
      buttons_a->show();
    } // Fl_Pack* buttons_a

    Fl_Pack* buttons_b = new Fl_Pack(0,650, 64, 38);
    {
      { Fl_Button* exitbtn = new Fl_Button(0, 0, 64, 38);
        exitbtn->callback((Fl_Callback*)cb_exit);
        exitbtn->image(new Fl_PNG_Image("exit.png", img(application_exit_png)));
        exitbtn->tooltip(_("Exit"));
      }
      buttons_b->end();
      buttons_b->show();
    }

    buttons->end();
    buttons->show();
  }
  { showbtn = new Fl_Button(0, 0, 6, 700);
    showbtn->hide();
    showbtn->callback(cb_hide);
  }
  { view = new PDFView(64, 0, 700-64, 700);
    view->show();
  }

  Fl_Group::current()->resizable(view);
  win_pack->end();
  win_pack->show();

  win->resizable(win_pack);
  win->callback((Fl_Callback*)cb_exit);
  win->size_range(500, 500);
  win->end();

  Fl_PNG_Image updf_icon("updf-128x128.png", img(updf_128x128_png));
  win->icon(&updf_icon);

  fl_open_display();
  win->show();
  checkX();

  #undef img

  if (lzo_init() != LZO_E_OK)
    die(_("LZO init failed\n"));

  // Set the width to half of the screen, 90% of height
  //win->size(Fl::w() * 0.4f, Fl::h() * 0.9f);

  load_config();

  bool used_recent = false;

  if (optind < argc)
    used_recent = loadfile(argv[optind], recent_files);
  else
    used_recent = loadfile(NULL, recent_files);

  if (used_recent) {
    debug("Recent found\n");
    adjust_display_from_recent(*recent_files);
  }
  else {
    debug("Recent not found\n");
  }

  view->take_focus();

  return Fl::run();
}
