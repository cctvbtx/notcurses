#include "internal.h"

ncreader* ncreader_create(ncplane* n, const ncreader_options* opts){
  ncreader_options zeroed = {};
  if(!opts){
    opts = &zeroed;
  }
  if(opts->flags > NCREADER_OPTION_CURSOR){
    logwarn(n->nc, "Provided unsupported flags %016lx\n", opts->flags);
  }
  ncreader* nr = malloc(sizeof(*nr));
  if(nr == NULL){
    ncplane_destroy(n);
    return NULL;
  }
  nr->ncp = n;
  // do *not* bind it to the visible plane; we always want it offscreen,
  // to the upper left of the true origin
  if((nr->textarea = ncplane_new(notcurses_stdplane(ncplane_notcurses(n)),
                                 ncplane_dim_y(n), ncplane_dim_x(n),
                                 -ncplane_dim_y(n), -ncplane_dim_x(n), NULL, "text")) == NULL){
    ncplane_destroy(nr->ncp);
    free(nr);
    return NULL;
  }

  nr->horscroll = opts->flags & NCREADER_OPTION_HORSCROLL;
  nr->xproject = 0;
  nr->tchannels = opts->tchannels;
  nr->tattrs = opts->tattrword;
  nr->no_cmd_keys = opts->flags & NCREADER_OPTION_NOCMDKEYS;
  nr->manage_cursor = opts->flags & NCREADER_OPTION_CURSOR;
  ncplane_set_channels(nr->ncp, opts->tchannels);
  ncplane_set_styles(nr->ncp, opts->tattrword);
  return nr;
}

// empty both planes of all input, and home the cursors.
int ncreader_clear(ncreader* n){
  ncplane_erase(n->ncp);
  ncplane_erase(n->textarea);
  n->xproject = 0;
  return 0;
}

ncplane* ncreader_plane(ncreader* n){
  return n->ncp;
}

// copy the viewed area down from the textarea
static int
ncreader_redraw(ncreader* n){
  int ret = 0;
//fprintf(stderr, "redraw: xproj %d\n", n->xproject);
//notcurses_debug(n->ncp->nc, stderr);
  assert(n->xproject >= 0);
  assert(n->textarea->lenx >= n->ncp->lenx);
  assert(n->textarea->leny >= n->ncp->leny);
  for(int y = 0 ; y < n->ncp->leny ; ++y){
    const int texty = y;
    for(int x = 0 ; x < n->ncp->lenx ; ++x){
      const int textx = x + n->xproject;
      const cell* src = &n->textarea->fb[nfbcellidx(n->textarea, texty, textx)];
      cell* dst = &n->ncp->fb[nfbcellidx(n->ncp, y, x)];
//fprintf(stderr, "projecting %d/%d [%s] to %d/%d [%s]\n", texty, textx, cell_extended_gcluster(n->textarea, src), y, x, cell_extended_gcluster(n->ncp, dst));
      if(cellcmp_and_dupfar(&n->ncp->pool, dst, n->textarea, src) < 0){
        ret = -1;
      }
    }
  }
  if(notcurses_cursor_enable(n->ncp->nc, n->ncp->absy + n->ncp->y, n->ncp->absx + n->ncp->x)){
    ret = -1;
  }
  return ret;
}

// try to move left. does not move past the start of the textarea, but will
// try to move up and to the end of the previous row if not on the top row.
// if on the left side of the viewarea, but not the left side of the textarea,
// scrolls left. returns 0 if a move was made.
int ncreader_move_left(ncreader* n){
  int viewx = n->ncp->x;
  int textx = n->textarea->x;
  int y = n->ncp->y;
//fprintf(stderr, "moving left: tcurs: %dx%d vcurs: %dx%d xproj: %d\n", y, textx, y, viewx, n->xproject);
  if(textx == 0){
    // are we on the first column of the textarea? if so, we must also be on
    // the first column of the viewarea. try to move up.
    if(y == 0){
      return -1; // no move possible
    }
    viewx = n->ncp->lenx - 1; // FIXME find end of particular row
    --y;
    textx = n->textarea->lenx - 1;
    n->xproject = n->textarea->x - n->ncp->x;
  }else{
    // if we're on the first column of the viewarea, but not the first column
    // of the textarea, we must be able to scroll to the left. do so.
    // if we're not on the last column anywhere, move cursor right everywhere.
    if(viewx == 0){
      --n->xproject;
    }else{
      --viewx;
    }
    --textx;
  }
  ncplane_cursor_move_yx(n->textarea, y, textx);
  ncplane_cursor_move_yx(n->ncp, y, viewx);
//fprintf(stderr, "moved left: tcurs: %dx%d vcurs: %dx%d xproj: %d\n", y, textx, y, viewx, n->xproject);
  ncreader_redraw(n);
  return 0;
}

// try to move right. does not move past the end of the textarea, but will
// try to move down and to the start of the previous row if not on the bottom
// row. if on the right side of the viewarea, but not the right side of the
// textarea, pans right. returns 0 if a move was made.
int ncreader_move_right(ncreader* n){
  int viewx = n->ncp->x;
  int textx = n->textarea->x;
  int y = n->ncp->y;
//fprintf(stderr, "moving right: tcurs: %dx%d vcurs: %dx%d xproj: %d\n", y, textx, y, viewx, n->xproject);
  if(textx >= n->textarea->lenx - 1){
    // are we on the last column of the textarea? if so, we must also be on
    // the first column of the viewarea. try to move up.
    if(y >= n->textarea->leny - 1){
      return -1; // no move possible
    }
    viewx = 0;
    ++y;
    textx = viewx;
    n->xproject = 0;
  }else{
    // if we're on the first column of the viewarea, but not the first column
    // of the textarea, we must be able to scroll to the left. do so.
    // if we're not on the last column anywhere, move cursor right everywhere.
    if(viewx >= n->ncp->lenx - 1){
      ++n->xproject;
    }else{
      ++viewx;
    }
    ++textx;
  }
  ncplane_cursor_move_yx(n->textarea, y, textx);
  ncplane_cursor_move_yx(n->ncp, y, viewx);
//fprintf(stderr, "moved right: tcurs: %dx%d vcurs: %dx%d xproj: %d\n", y, textx, y, viewx, n->xproject);
  ncreader_redraw(n);
  return 0;
}

// try to move up. does not move past the top of the textarea.
// returns 0 if a move was made.
int ncreader_move_up(ncreader* n){
  int y = n->ncp->y;
  if(y == 0){
    // are we on the last row of the textarea? if so, we can't move.
    return -1;
  }
  --y;
  ncplane_cursor_move_yx(n->textarea, y, -1);
  ncplane_cursor_move_yx(n->ncp, y, -1);
  ncreader_redraw(n);
  return 0;
}

// try to move down. does not move past the bottom of the textarea.
// returns 0 if a move was made.
int ncreader_move_down(ncreader* n){
  int y = n->ncp->y;
  if(y >= n->textarea->leny - 1){
    // are we on the last row of the textarea? if so, we can't move.
    return -1;
  }
  ++y;
  ncplane_cursor_move_yx(n->textarea, y, -1);
  ncplane_cursor_move_yx(n->ncp, y, -1);
  ncreader_redraw(n);
  return 0;
}

// only writing can enlarge the textarea. movement can pan, but not enlarge.
int ncreader_write_egc(ncreader* n, const char* egc){
  const int cols = ncstrwidth(egc);
  if(cols < 0){
    logerror(n->ncp->nc, "Fed illegal UTF-8 [%s]\n", egc);
    return -1;
  }
  if(n->textarea->x >= n->textarea->lenx - cols){
    if(n->horscroll){
      if(ncplane_resize_simple(n->textarea, n->textarea->leny, n->textarea->lenx + cols)){
        return -1;
      }
      ++n->xproject;
    }
  }else if(n->ncp->x >= n->ncp->lenx){
    ++n->xproject;
  }
  // use ncplane_putegc on both planes because it'll get cursor movement right
  if(ncplane_putegc(n->textarea, egc, NULL) < 0){
    return -1;
  }
  if(ncplane_putegc(n->ncp, egc, NULL) < 0){
    return -1;
  }
  if(n->textarea->x >= n->textarea->lenx - cols){
    if(!n->horscroll){
      n->textarea->x = n->textarea->lenx - cols;
    }
  }
  if(n->ncp->x >= n->ncp->lenx - cols){
    n->ncp->x = n->ncp->lenx - cols;
  }
  ncreader_redraw(n);
  return 0;
}

static bool
ncreader_ctrl_input(ncreader* n, const ncinput* ni){
  switch(ni->id){
    case 'U':
      ncplane_erase(n->ncp); // homes the cursor
      ncplane_erase(n->textarea);
      break;
    case 'B':
      ncreader_move_left(n);
      break;
    case 'F':
      ncreader_move_right(n);
      break;
    default:
      return false; // pass on all other ctrls
  }
  return true;
}

static bool
ncreader_alt_input(ncreader* n, const ncinput* ni){
  switch(ni->id){
    case 'B': // back one word (to first cell) FIXME
      ncreader_move_left(n);
      break;
    case 'F': // forward one word (past end cell) FIXME
      ncreader_move_right(n);
      break;
    default:
      return false;
  }
  return true;
}

// we pass along:
//  * anything with Alt
//  * anything with Ctrl, except 'U' (which clears all input)
//  * anything synthesized, save arrow keys and backspace
bool ncreader_offer_input(ncreader* n, const ncinput* ni){
  int x = n->textarea->x;
  int y = n->textarea->y;
  if(ni->alt){ // pass on all alts
    return false;
  }
  if(ni->ctrl && !n->no_cmd_keys){
    return ncreader_ctrl_input(n, ni);
  }else if(ni->alt && !n->no_cmd_keys){
    return ncreader_alt_input(n, ni);
  }
  if(ni->id == NCKEY_BACKSPACE){
    if(n->textarea->x == 0){
      if(n->textarea->y){
        y = n->textarea->y - 1;
        x = n->textarea->lenx - 1;
      }
    }else{
      --x;
    }
    ncplane_putegc_yx(n->textarea, y, x, "", NULL);
    ncplane_cursor_move_yx(n->textarea, y, x);
    ncplane_cursor_move_yx(n->ncp, n->ncp->y, n->ncp->x - 1);
    ncreader_redraw(n);
    return true;
  }
  // FIXME deal with multicolumn EGCs -- probably extract these and make them
  // general ncplane_cursor_{left, right, up, down}()
  if(ni->id == NCKEY_LEFT){
    ncreader_move_left(n);
    return true;
  }else if(ni->id == NCKEY_RIGHT){
    ncreader_move_right(n);
    return true;
  }else if(ni->id == NCKEY_UP){
    ncreader_move_up(n);
    return true;
  }else if(ni->id == NCKEY_DOWN){
    ncreader_move_down(n);
    return true;
  }else if(nckey_supppuab_p(ni->id)){
    return false;
  }
  // FIXME need to collect full EGCs
  char wbuf[WCHAR_MAX_UTF8BYTES + 1];
  // FIXME breaks for wint_t < 32bits
  if(snprintf(wbuf, sizeof(wbuf), "%lc", (wint_t)ni->id) < (int)sizeof(wbuf)){
    ncreader_write_egc(n, wbuf);
  }
  return true;
}

char* ncreader_contents(const ncreader* n){
  return ncplane_contents(n->ncp, 0, 0, -1, -1);
}

void ncreader_destroy(ncreader* n, char** contents){
  if(n){
    if(contents){
      *contents = ncreader_contents(n);
    }
    if(n->manage_cursor){
      notcurses_cursor_disable(n->ncp->nc);
    }
    ncplane_destroy(n->textarea);
    ncplane_destroy(n->ncp);
    free(n);
  }
}
