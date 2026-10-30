/*
 *  Copyright (C) 2013 Jens Thoms Toerring <jt@toerring.de>
 *
 *  This file is part of the pbterm program.
 *
 *  pbterm is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  pbterm is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with pbterm.  If not, see <http://www.gnu.org/licenses/>.
 */


#if ! defined DISPLAY_HPP_
#define DISPLAY_HPP_


#include <string>
#include <vector>
#include "Vt.hpp"
#include "ExtraKeys.hpp"
#include "Inkview.hpp"


class Config;
class Messenger;
class Logger;


/******************************************
 * Class that takes care of what happens on the display
 ******************************************/

class Display
{
  public :

    Display( Messenger & mess,
             Config    & config );


    ~Display( );


    void
    redraw( );


    void
    add_text( std::string const & str );


    // Switches handling of '\n' between "pty" (shell/tty already sends
    // "\r\n") and "pipe" mode (bare '\n', we have to add the '\r')

    void
    set_pty_mode( bool pty );


    // Terminal size in character cells (needed for TIOCSWINSZ)

    int
    cols( ) const;

    int
    rows( ) const;


    // Bytes the emulator wants sent back to the shell (cursor reports etc.)

    std::string
    take_reply( );


    // Top of the extra keys area (screen height if there is none)

    int
    bar_top( ) const  { return m_bar_top; }


    // A tap on the extra keys: returns the bytes to send to the shell

    std::string
    extra_key_tap( int x,
                   int y );


    // On-screen keyboard: shown/hidden by the KBD and HIDE keys, a tap on
    // the terminal brings it up. Both return true if the terminal size
    // changed (the shell has to be told then).

    bool
    onscreen_keyboard( ) const  { return m_onscreen_kbd; }

    bool
    show_screen_keyboard( );

    bool
    take_geometry_changed( );


    // True while CTRL or ALT is armed

    bool
    extra_keys_armed( ) const;


    // Applies armed CTRL/ALT to text entered on the on-screen keyboard

    std::string
    apply_modifiers( std::string const & text );


    // Selecting text: start_selection() waits for two taps (start and end
    // of the text), select_tap() returns true after the second one and
    // hands back the selected text.

    void
    start_selection( );

    void
    cancel_selection( );

    bool
    selecting( ) const  { return m_sel_state == 1 || m_sel_state == 2; }

    bool
    select_tap( int           x,
                int           y,
                std::string & text );

    bool
    select_range( int           x1,
                  int           y1,
                  int           x2,
                  int           y2,
                  std::string & text );


    // Jump back to the live screen after scrolling through the history

    void
    scroll_to_bottom( );


    void
    shift( int amount );

    void
    scroll_page( int dir);

    void
    rotate( int dir );


    void
    change_font_size( int incr );

    void
    suspend_redraws( bool stop );

    void
    recording_state_change( bool state );


  private :

    // Name of the font to use

    std::string m_font_name;


    // Size of that font

    int m_font_size;


    // Cyurrent orientation

    int m_orientation;


    // Original orientation

    int m_initial_orientation;


    // Current width of display

    int m_width;


    // Font currently in use

    ifont * m_font;


    // The terminal emulator holding the cell grid and the scrollback

    Vt * m_vt;

    // Size of a character cell in pixels, extra line spacing

    int m_cell_w,
        m_cell_h,
        m_line_spacing;

    // Number of lines the view is scrolled back into the history

    int m_scroll;

    // Cursor position at the time of the last drawing

    int m_last_cursor_row;

    // Set if the shell talks through pipes instead of a pty

    bool m_pipe_mode;

    // Extra keys row and the area it occupies at the bottom of the screen

    Logger * m_log;

    ExtraKeys * m_xk;

    std::string m_bar_layout;           // from the configuration file

    bool m_onscreen_kbd,                // keyboard enabled at all
         m_kbd_visible,
         m_geometry_changed;

    int  m_kbd_layer;                   // 0 letters, 1 symbols

    void
    build_keys( );

    void
    key_action( std::string const & action );

    int m_bar_top,
        m_bar_h;

    // Ghosting control: after big changes (or many partial updates) a full
    // panel refresh is done once the output has been quiet for a while

    bool m_force_full,
         m_needs_settle,
         m_last_alt;

    int m_partials;

    void
    arm_settle( );

    static void
    static_late_repaint( );

    void
    settle( );

    static void
    static_settle( );

    // Selection: 0 none, 1 waiting for start, 2 waiting for end, 3 done
    // (shown highlighted). Positions are absolute line numbers (0 = oldest
    // line of the scrollback) and columns.

    int m_sel_state,
        m_sel_a_line, m_sel_a_col,
        m_sel_b_line, m_sel_b_col;

    // Number of characters copied last, shown in the status line

    int m_sel_len;

    void
    cell_at( int   x,
             int   y,
             int & line,
             int & col ) const;

    void
    draw_banner( );

    bool
    get_abs_row( int            line,
                 Cell const * & cells,
                 int          & len ) const;

    bool
    in_selection( int line,
                  int col ) const;

    std::string
    selection_text( ) const;

    // Helpers

    void
    draw_bar( );

    void
    update_bar( );

    void
    recalc_geometry( );

    void
    draw_row( int row,
              bool clear );

    bool
    get_row( int row,
             Cell const * & cells,
             int          & len ) const;


    // Flag, set when no redraws are to done

    bool m_is_output_suspended;


    // Flag, set when during suspended a redraws a redraw is requested

    bool m_is_redraw_needed;


    // Flag, set while recording is switched on

    bool m_is_recording;
};


#endif


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
