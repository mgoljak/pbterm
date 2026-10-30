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


#include "Display.hpp"
#include "Messenger.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "Defaults.hpp"
#include <algorithm>
#include <cstdio>
#include <set>


// Appends a Unicode code point as UTF-8

static void
append_utf8( std::string & s,
             unsigned int  ch )
{
    if ( ch < 0x80 )
        s += static_cast< char >( ch ? ch : ' ' );
    else if ( ch < 0x800 )
    {
        s += static_cast< char >( 0xC0 | ( ch >> 6 ) );
        s += static_cast< char >( 0x80 | ( ch & 0x3F ) );
    }
    else if ( ch < 0x10000 )
    {
        s += static_cast< char >( 0xE0 | ( ch >> 12 ) );
        s += static_cast< char >( 0x80 | ( ( ch >> 6 ) & 0x3F ) );
        s += static_cast< char >( 0x80 | ( ch & 0x3F ) );
    }
    else
    {
        s += static_cast< char >( 0xF0 | ( ch >> 18 ) );
        s += static_cast< char >( 0x80 | ( ( ch >> 12 ) & 0x3F ) );
        s += static_cast< char >( 0x80 | ( ( ch >> 6 ) & 0x3F ) );
        s += static_cast< char >( 0x80 | ( ch & 0x3F ) );
    }
}


static Display * s_display = 0;


/******************************************
 * Constructor
 ******************************************/

Display::Display( Messenger & mess,
                  Config    & config )
    : m_font_name( config.font_name( ) )
    , m_font_size( config.default_font_size( ) )
    , m_orientation( GetOrientation( ) )
    , m_initial_orientation( m_orientation )
    , m_width( ScreenWidth( ) )
    , m_font( 0 )
    , m_is_output_suspended( false )
    , m_is_redraw_needed( false )
    , m_is_recording( false )
    , m_vt( 0 )
    , m_cell_w( 1 )
    , m_cell_h( 1 )
    , m_line_spacing( config.line_spacing( ) )
    , m_scroll( 0 )
    , m_last_cursor_row( 0 )
    , m_pipe_mode( false )
    , m_sel_len( 0 )
    , m_sel_state( 0 )
    , m_sel_a_line( 0 )
    , m_sel_a_col( 0 )
    , m_sel_b_line( 0 )
    , m_sel_b_col( 0 )
    , m_force_full( false )
    , m_needs_settle( false )
    , m_last_alt( false )
    , m_partials( 0 )
    , m_log( &config.logger( ) )
    , m_xk( new ExtraKeys( std::string( ) ) )
    , m_bar_layout( config.extra_keys( ) )
    , m_onscreen_kbd( config.onscreen_keyboard( ) )
    , m_kbd_visible( config.onscreen_keyboard( ) )
    , m_geometry_changed( false )
    , m_kbd_layer( 0 )
    , m_bar_top( ScreenHeight( ) )
    , m_bar_h( 0 )
{
    s_display = this;

    // With SDK 6.x the system panel is on by default and it offsets the
    // framebuffer (content wraps around by the panel's height). A terminal
    // wants the whole screen, so switch the panel off.

    SetPanelType( PANEL_DISABLED );

    OpenScreen( );

    if ( ! ( m_font = OpenFont( m_font_name.c_str( ), m_font_size, 1 ) ) )
    {
        config.logger( ).error( ) << "Can't open font '" << m_font_name
                                  << "' with size of " << m_font_size
                                  << std::endl;
        mess.send( message::Close( ) );
        return;
    }

    SetFont( m_font, BLACK );

    int cw = std::max( 1, StringWidth( "M" ) ),
        ch = m_font_size + m_line_spacing;

    m_vt = new Vt( std::max( 2, ( ScreenWidth( ) - 2 * X_MARGIN ) / cw ),
                   std::max( 2, ( ScreenHeight( ) - 2 * Y_MARGIN ) / ch ),
                   config.max_lines( ) );
    build_keys( );
    recalc_geometry( );

    // Set orientation only after font has been set, the font is needed in
    // the rotate() method

    rotate( config.default_orientation( ) );
}


/******************************************
 * Destructor, switches back to original orientation and "frees" font
 ******************************************/

Display::~Display( )
{
    if ( m_orientation != m_initial_orientation )
        SetOrientation( m_initial_orientation );

    if ( m_font )
        CloseFont( m_font );

    s_display = 0;

    delete m_vt;
    delete m_xk;
}


/******************************************
 * Works out cell size and the number of columns and rows for the current
 * font and screen size and tells the emulator about it
 ******************************************/

void
Display::recalc_geometry( )
{
    if ( ! m_vt )
        return;

    SetFont( m_font, BLACK );

    m_cell_w = std::max( 1, StringWidth( "M" ) );
    // Use the real glyph box height: descenders of p, g, y, j reach below
    // 'font_size' and would be cut off by the next row otherwise

    m_cell_h = std::max( std::max( m_font_size, m_font->height ),
                         m_font->linespacing ) + m_line_spacing;

    // The extra keys take some room at the bottom of the screen

    int key_h = std::max( 40, std::min( ScreenWidth( ) / 11,
                                         ScreenHeight( ) / 14 ) );

    m_bar_h   = m_xk->rows( ) * key_h;
    m_bar_top = ScreenHeight( ) - m_bar_h;

    m_vt->resize( std::max( 2, ( ScreenWidth( ) - 2 * X_MARGIN ) / m_cell_w ),
                  std::max( 2, ( m_bar_top - 2 * Y_MARGIN ) / m_cell_h ) );

    m_log->info( ) << "geometry: screen " << ScreenWidth( ) << "x"
                   << ScreenHeight( ) << " orientation " << GetOrientation( )
                   << " panel " << GetPanelType( )
                   << ", cell " << m_cell_w << "x" << m_cell_h
                   << ", font " << m_font_size << " (height "
                   << m_font->height << ", linespacing " << m_font->linespacing
                   << "), bar_top " << m_bar_top << " bar_h " << m_bar_h
                   << ", terminal " << m_vt->cols( ) << "x" << m_vt->rows( )
                   << std::endl;

    m_scroll = std::min< int >( m_scroll, m_vt->scrollback_size( ) );
    m_sel_state = 0;
}


int
Display::cols( ) const
{
    return m_vt ? m_vt->cols( ) : 80;
}


int
Display::rows( ) const
{
    return m_vt ? m_vt->rows( ) : 24;
}


std::string
Display::take_reply( )
{
    return m_vt ? m_vt->take_reply( ) : std::string( );
}


void
Display::set_pty_mode( bool pty )
{
    m_pipe_mode = ! pty;
}


void
Display::scroll_to_bottom( )
{
    if ( m_scroll )
    {
        m_scroll = 0;
        Repaint( );
    }
}


/******************************************
 * Finds the cells to show in a screen row, taking the scroll position
 * into account. Returns false if there's nothing to draw.
 ******************************************/

bool
Display::get_row( int            row,
                  Cell const * & cells,
                  int          & len ) const
{
    return get_abs_row( m_vt->scrollback_size( ) - m_scroll + row, cells, len );
}


bool
Display::get_abs_row( int            line,
                      Cell const * & cells,
                      int          & len ) const
{
    int sb = m_vt->scrollback_size( );

    if ( line < 0 )
        return false;

    if ( line < sb )
    {
        std::vector< Cell > const & l = m_vt->scrollback_line( sb - 1 - line );
        if ( l.empty( ) )
            return false;
        cells = &l[ 0 ];
        len = std::min< int >( l.size( ), m_vt->cols( ) );
        return true;
    }

    line -= sb;
    if ( line >= m_vt->rows( ) )
        return false;

    cells = &m_vt->cell( line, 0 );
    len = m_vt->cols( );
    return true;
}


/******************************************
 * Selection of text on the screen
 ******************************************/

void
Display::start_selection( )
{
    m_sel_state = 1;
    Repaint( );
}


void
Display::cell_at( int   x,
                  int   y,
                  int & line,
                  int & col ) const
{
    int row = std::max( 0, std::min( ( y - Y_MARGIN ) / m_cell_h,
                                     m_vt->rows( ) - 1 ) );

    col  = std::max( 0, std::min( ( x - X_MARGIN ) / m_cell_w,
                                  m_vt->cols( ) - 1 ) );
    line = m_vt->scrollback_size( ) - m_scroll + row;
}


bool
Display::select_range( int           x1,
                       int           y1,
                       int           x2,
                       int           y2,
                       std::string & text )
{
    if ( ! m_vt || ! selecting( ) )
        return false;

    cell_at( x1, y1, m_sel_a_line, m_sel_a_col );
    cell_at( x2, y2, m_sel_b_line, m_sel_b_col );

    m_sel_state = 3;
    text = selection_text( );
    m_sel_len = text.size( );
    Repaint( );
    return true;
}


/******************************************
 * Status line at the bottom of the terminal area while selecting
 ******************************************/

void
Display::draw_banner( )
{
    if ( ! m_sel_state )
        return;

    char buf[ 64 ];

    if ( m_sel_state == 1 )
        snprintf( buf, sizeof buf, "Select: tap start or swipe" );
    else if ( m_sel_state == 2 )
        snprintf( buf, sizeof buf, "Select: tap end" );
    else
        snprintf( buf, sizeof buf, "Copied %d characters", m_sel_len );

    int h = m_cell_h + 6;

    FillArea( 0, m_bar_top - h, ScreenWidth( ), h, BLACK );
    SetFont( m_font, WHITE );
    DrawTextRect( X_MARGIN, m_bar_top - h, ScreenWidth( ) - 2 * X_MARGIN, h,
                  buf, ALIGN_LEFT | VALIGN_MIDDLE );
    SetFont( m_font, BLACK );
}


void
Display::cancel_selection( )
{
    bool was_shown = m_sel_state != 0;

    m_sel_state = 0;

    if ( was_shown )
        Repaint( );
}


bool
Display::in_selection( int line,
                       int col ) const
{
    if ( m_sel_state < 2 )
        return false;

    int l1 = m_sel_a_line, c1 = m_sel_a_col,
        l2 = m_sel_b_line, c2 = m_sel_b_col;

    if ( l1 > l2 || ( l1 == l2 && c1 > c2 ) )
    {
        std::swap( l1, l2 );
        std::swap( c1, c2 );
    }

    return    ( line > l1 || ( line == l1 && col >= c1 ) )
           && ( line < l2 || ( line == l2 && col <= c2 ) );
}


bool
Display::select_tap( int           x,
                     int           y,
                     std::string & text )
{
    if ( ! m_vt || ! selecting( ) )
        return false;

    int line, col;

    cell_at( x, y, line, col );

    if ( m_sel_state == 1 )
    {
        m_sel_a_line = m_sel_b_line = line;
        m_sel_a_col  = m_sel_b_col  = col;
        m_sel_state = 2;
        Repaint( );
        return false;
    }

    m_sel_b_line = line;
    m_sel_b_col  = col;
    m_sel_state = 3;
    text = selection_text( );
    m_sel_len = text.size( );
    Repaint( );
    return true;
}


std::string
Display::selection_text( ) const
{
    int l1 = m_sel_a_line, c1 = m_sel_a_col,
        l2 = m_sel_b_line, c2 = m_sel_b_col;

    if ( l1 > l2 || ( l1 == l2 && c1 > c2 ) )
    {
        std::swap( l1, l2 );
        std::swap( c1, c2 );
    }

    std::string out;

    for ( int line = l1; line <= l2; ++line )
    {
        Cell const * cells;
        int len;
        std::string s;

        if ( get_abs_row( line, cells, len ) )
        {
            int from = line == l1 ? c1 : 0,
                to   = std::min( line == l2 ? c2 : len - 1, len - 1 );

            for ( int c = from; c <= to; ++c )
                append_utf8( s, cells[ c ].ch );

            std::size_t e = s.find_last_not_of( ' ' );
            s = e == std::string::npos ? std::string( ) : s.substr( 0, e + 1 );
        }

        out += s;
        if ( line < l2 )
            out += '\n';
    }

    return out;
}


/******************************************
 * Draws a single screen row. Runs of cells with identical attributes are
 * drawn with one DrawString() call. Reverse video and the cursor are shown
 * as white on black, cells with a coloured background as light grey.
 ******************************************/

void
Display::draw_row( int row,
                   bool clear )
{
    int y = Y_MARGIN + row * m_cell_h;

    if ( clear )
        FillArea( 0, y, ScreenWidth( ), m_cell_h, WHITE );

    Cell const * cells;
    int len;

    if ( ! get_row( row, cells, len ) )
        return;

    bool live = ! m_scroll;
    int  abs_line = m_vt->scrollback_size( ) - m_scroll + row;
    int  cur_row = m_vt->cursor_row( ),
         cur_col = m_vt->cursor_col( );
    bool show_cursor = live && m_vt->cursor_visible( ) && row == cur_row;

    int c = 0;
    while ( c < len )
    {
        // Mode of this cell: 0 normal, 1 inverted, 2 grey background

        int mode = 0;
        unsigned char attr = 0;
        int start = c;

        for ( ; c < len; ++c )
        {
            bool inv = ( cells[ c ].attr & Vt::ATTR_REVERSE ) != 0;
            if ( show_cursor && c == cur_col )
                inv = ! inv;
            if ( in_selection( abs_line, c ) )
                inv = ! inv;

            int m = inv ? 1
                        : ( cells[ c ].bg != Vt::DEFAULT_COLOR && cells[ c ].bg != 0 ) ? 2 : 0;
            unsigned char a = cells[ c ].attr & Vt::ATTR_UNDERLINE;

            if ( c == start )
            {
                mode = m;
                attr = a;
            }
            else if ( m != mode || a != attr || ( show_cursor && ( c == cur_col || c - 1 == cur_col ) ) )
                break;
        }

        std::string txt;
        bool blank = true;
        for ( int i = start; i < c; ++i )
        {
            append_utf8( txt, cells[ i ].ch );
            if ( cells[ i ].ch != ' ' && cells[ i ].ch != 0 )
                blank = false;
        }

        int x = X_MARGIN + start * m_cell_w,
            w = ( c - start ) * m_cell_w;

        if ( mode == 1 )
            FillArea( x, y, w, m_cell_h, BLACK );
        else if ( mode == 2 )
            FillArea( x, y, w, m_cell_h, LGRAY );

        if ( ! blank )
        {
            if ( mode == 1 )
                SetFont( m_font, WHITE );

            DrawString( x, y, txt.c_str( ) );

            if ( mode == 1 )
                SetFont( m_font, BLACK );
        }

        if ( attr )
            DrawLine( x, y + m_cell_h - 1, x + w - 1, y + m_cell_h - 1,
                      mode == 1 ? WHITE : BLACK );
    }
}


/******************************************
 * Draws the extra keys: framed boxes, armed CTRL/ALT shown inverted
 ******************************************/

void
Display::draw_bar( )
{
    if ( ! m_xk->rows( ) )
        return;

    int W = ScreenWidth( ),
        R = m_xk->rows( );

    FillArea( 0, m_bar_top, W, m_bar_h, WHITE );
    DrawLine( 0, m_bar_top, W - 1, m_bar_top, BLACK );

    for ( int r = 0; r < R; ++r )
    {
        int y0 = m_bar_top + r * m_bar_h / R,
            y1 = m_bar_top + ( r + 1 ) * m_bar_h / R,
            n  = m_xk->cols( r ),
            total = m_xk->row_weight( r ),
            cum = 0;

        for ( int c = 0; c < n; ++c )
        {
            int x0 = cum * W / total;
            cum += m_xk->weight( r, c );
            int x1 = cum * W / total;
            bool act = m_xk->active( r, c );

            if ( act )
                FillArea( x0 + 2, y0 + 3, x1 - x0 - 4, y1 - y0 - 5, BLACK );
            else
                DrawRect( x0 + 2, y0 + 3, x1 - x0 - 4, y1 - y0 - 5, BLACK );

            SetFont( m_font, act ? WHITE : BLACK );
            DrawTextRect( x0, y0, x1 - x0, y1 - y0,
                          m_xk->label( r, c ).c_str( ),
                          ALIGN_CENTER | VALIGN_MIDDLE );
        }
    }

    SetFont( m_font, BLACK );
}


/******************************************
 * (Re)starts the timer for the full refresh after output has stopped
 ******************************************/

void
Display::arm_settle( )
{
    SetWeakTimer( APP_NAME "_settle", &Display::static_settle, 1500 );
}


/******************************************
 * The system restores the screen content below the keyboard or a menu
 * when it closes it, which can wipe out what was drawn in the meantime
 * (e.g. the echo of the text just typed). So redraw everything a moment
 * after the dialog is gone.
 ******************************************/

void
Display::static_late_repaint( )
{
    if ( s_display )
        Repaint( );
}


void
Display::static_settle( )
{
    if ( s_display )
        s_display->settle( );
}


void
Display::settle( )
{
    if ( ! m_needs_settle )
        return;

    if ( m_is_output_suspended )
    {
        arm_settle( );
        return;
    }

    m_needs_settle = false;
    m_partials = 0;
    m_force_full = true;
    Repaint( );
}


/******************************************
 * Rows of the on-screen keyboard (QWERTZ)
 ******************************************/

static std::vector< std::string >
split_blanks( char const * s )
{
    std::vector< std::string > v;
    std::string tok;

    for ( ; ; ++s )
        if ( *s == ' ' || ! *s )
        {
            if ( ! tok.empty( ) )
                v.push_back( tok );
            tok.clear( );
            if ( ! *s )
                break;
        }
        else
            tok += *s;

    return v;
}


static ExtraKeys::Rows
keyboard_rows( int layer )
{
    ExtraKeys::Rows rows;

    if ( layer == 0 )
    {
        rows.push_back( split_blanks( "q w e r t z u i o p" ) );
        rows.push_back( split_blanks( "a s d f g h j k l" ) );
        rows.push_back( split_blanks( "SHIFT y x c v b n m BKSP" ) );
        rows.push_back( split_blanks( "SYM , SPACE*5 . ENTER*2 HIDE" ) );
    }
    else
    {
        rows.push_back( split_blanks( "1 2 3 4 5 6 7 8 9 0 BKSP" ) );
        rows.push_back( split_blanks( "! @ # $ % ^ & * ( ) ~" ) );
        rows.push_back( split_blanks( "[ ] { } < > ` | \\ ? =" ) );
        rows.push_back( split_blanks( "ABC + _ : ; \" ' . SPACE*2 ENTER*2" ) );
    }

    return rows;
}


/******************************************
 * (Re)builds the keys shown at the bottom: the configured extra keys and,
 * if enabled, the keyboard (or just a KBD key to bring it back)
 ******************************************/

void
Display::build_keys( )
{
    ExtraKeys::Rows rows = ExtraKeys::parse( m_bar_layout );

    if ( m_onscreen_kbd )
    {
        if ( m_kbd_visible )
        {
            ExtraKeys::Rows k = keyboard_rows( m_kbd_layer );
            rows.insert( rows.end( ), k.begin( ), k.end( ) );
        }
        else
        {
            if ( rows.empty( ) )
                rows.push_back( std::vector< std::string >( ) );
            rows.back( ).push_back( "KBD" );
        }
    }

    ExtraKeys * nk = new ExtraKeys( rows );
    nk->copy_modifiers( *m_xk );
    delete m_xk;
    m_xk = nk;
}


void
Display::key_action( std::string const & action )
{
    if ( action == "SYM" )
        m_kbd_layer = 1;
    else if ( action == "ABC" )
        m_kbd_layer = 0;
    else if ( action == "HIDE" )
        m_kbd_visible = false;
    else if ( action == "KBD" )
        m_kbd_visible = true;

    build_keys( );
    recalc_geometry( );
    m_geometry_changed = true;
    Repaint( );
}


bool
Display::show_screen_keyboard( )
{
    if ( ! m_onscreen_kbd || m_kbd_visible )
        return false;

    key_action( "KBD" );
    return take_geometry_changed( );
}


bool
Display::take_geometry_changed( )
{
    bool c = m_geometry_changed;
    m_geometry_changed = false;
    return c;
}


/******************************************
 * Redraws just the extra keys (e.g. after CTRL/ALT changed state)
 ******************************************/

void
Display::update_bar( )
{
    if ( ! m_xk->rows( ) )
        return;

    if ( m_is_output_suspended )
    {
        m_is_redraw_needed = true;
        return;
    }

    draw_bar( );
    PartialUpdate( 0, m_bar_top, ScreenWidth( ), m_bar_h );
}


std::string
Display::extra_key_tap( int x,
                        int y )
{
    int row, col;

    if ( ! m_vt || ! m_xk->hit( x, y, 0, m_bar_top, ScreenWidth( ), m_bar_h,
                                row, col ) )
        return std::string( );

    std::string bytes = m_xk->press( row, col, m_vt->app_cursor_keys( ) ),
                action = m_xk->take_action( );

    if ( ! action.empty( ) )
    {
        key_action( action );
        return std::string( );
    }

    update_bar( );
    return bytes;
}


bool
Display::extra_keys_armed( ) const
{
    return m_xk->armed( );
}


std::string
Display::apply_modifiers( std::string const & text )
{
    std::string bytes = m_xk->apply_to_text( text );
    update_bar( );
    return bytes;
}


/******************************************
 * Redraws the display
 ******************************************/

void
Display::redraw( )
{
    // While output is suspended just record that a redraw will be necessary
    // when redrawing becomes possible again

    if ( m_is_output_suspended )
    {
        m_is_redraw_needed = true;
        return;
    }

    ClearScreen( );

    if ( m_vt )
    {
        SetFont( m_font, BLACK );

        for ( int r = 0; r < m_vt->rows( ); ++r )
            draw_row( r, false );            // screen was cleared already

        m_last_cursor_row = m_vt->cursor_row( );
        m_vt->clear_dirty( );
    }

    draw_bar( );
    draw_banner( );

    // Draw a little triangle in the upper right hand corner while recording
    // is switched on

    if ( m_is_recording )
    {
        int size   = 20,
            offset = 10;

        int x  = m_width - offset - size,
            y1 = offset,
            y2 = offset + size;

        while ( y1 < y2 )
        {
            DrawLine( x, y1,   x, y2, BLACK );
            x++;
            DrawLine( x, y1++, x, y2--, BLACK );
            x++;
        }
    }

    if ( m_force_full )
    {
        m_force_full = false;
        FullUpdate( );
    }
    else
        SoftUpdate( );
}


/******************************************
 * Feeds new shell output to the emulator and redraws what changed. If
 * only a few rows were touched just those get redrawn and pushed to the
 * panel with a partial update (much faster and less flickery on e-ink).
 ******************************************/

void
Display::add_text( std::string const & str )
{
    if ( ! m_vt )
        return;

    if ( m_pipe_mode )
    {
        std::string t;
        for ( std::size_t i = 0; i < str.size( ); ++i )
        {
            if ( str[ i ] == '\n' && ( i == 0 || str[ i - 1 ] != '\r' ) )
                t += '\r';
            t += str[ i ];
        }
        m_vt->feed( t.data( ), t.size( ) );
    }
    else
        m_vt->feed( str.data( ), str.size( ) );

    // New output removes a highlighted selection (and cancels one that is
    // still waiting for its end, the line numbers would be off now)

    bool sel_cleared = m_sel_state >= 2;

    if ( sel_cleared )
        m_sel_state = 0;

    if ( m_is_output_suspended )
    {
        m_is_redraw_needed = true;
        return;
    }

    // Old cursor row has to be redrawn too, the cursor block moved away

    std::set< int > rows;
    int nrows = 0;

    for ( int r = 0; r < m_vt->rows( ); ++r )
        if ( m_vt->row_dirty( r ) )
            rows.insert( r );

    nrows = rows.size( );
    rows.insert( std::min( m_last_cursor_row, m_vt->rows( ) - 1 ) );
    rows.insert( m_vt->cursor_row( ) );

    // Switching to or from the alternate screen is always a big change

    bool alt_changed = m_vt->alt_screen( ) != m_last_alt;
    m_last_alt = m_vt->alt_screen( );

    if ( m_scroll || alt_changed || sel_cleared || nrows > m_vt->rows( ) / 2 )
    {
        m_needs_settle = true;
        arm_settle( );

        if ( ! m_scroll )
            Repaint( );
        m_vt->clear_dirty( );
        return;
    }

    // Many partial updates in a row leave ghosts behind

    if ( ++m_partials >= 25 )
        m_needs_settle = true;

    if ( m_needs_settle )
        arm_settle( );

    SetFont( m_font, BLACK );

    int top = m_vt->rows( ), bottom = -1;
    for ( std::set< int >::const_iterator it = rows.begin( );
          it != rows.end( ); ++it )
    {
        draw_row( *it, true );
        top    = std::min( top, *it );
        bottom = std::max( bottom, *it );
    }

    m_last_cursor_row = m_vt->cursor_row( );
    m_vt->clear_dirty( );

    PartialUpdate( 0, Y_MARGIN + top * m_cell_h, ScreenWidth( ),
                   ( bottom - top + 1 ) * m_cell_h );
}


/***************************************
 * Called when the user makes a swipe gesture to scroll up or down. A swipe
 * down (positive amount) moves back into the history.
 ***************************************/

void
Display::shift( int amount )
{
    if ( ! m_vt )
        return;

    int old = m_scroll;

    m_scroll += amount / m_cell_h;
    m_scroll = std::max< int >( 0, std::min< int >( m_scroll, m_vt->scrollback_size( ) ) );

    if ( m_scroll != old )
        Repaint( );
}


/***************************************
 * Called when the user asks for a different orientation
 ***************************************/

void
Display::rotate( int dir )
{
    if ( dir == m_orientation )
        return;

    SetOrientation( m_orientation = dir );

    m_width = ScreenWidth( );

    recalc_geometry( );

    Repaint( );
}


/***************************************
 * Called when the user requests a larger or smaller font size (argument
 * is the difference to the current font size)
 ***************************************/

void
Display::change_font_size( int incr )
{
    int new_font_size  = m_font_size + incr;

    // Put some rstrictions on the minimum and maximum font size

    if ( new_font_size < MIN_FONT_SIZE )
        new_font_size = MIN_FONT_SIZE;

    if ( new_font_size > MAX_FONT_SIZE )
        new_font_size = MAX_FONT_SIZE;

    if ( new_font_size == m_font_size )
        return;

    ifont *new_font = OpenFont( m_font_name.c_str( ), new_font_size, 1 );

    if ( ! new_font )
        return;

    CloseFont( m_font );

    m_font = new_font;
    m_font_size = new_font_size;

    recalc_geometry( );
    Repaint( );
}


/******************************************
 * Called when the user switches recording on or off
 ******************************************/

void
Display::recording_state_change( bool state )
{
    m_is_recording = state;
    redraw( );
}


/******************************************
 * In some situations, e.g. when the menu or keyboard is shown, screen
 * updates need to be suspended. This happens when this method is called
 * with a 'true' value. When it's later called again with a 'false' value
 * updates get re-enabled and, if there were attempts to update in between,
 * an immediate update is triggered.
 ******************************************/

void
Display::suspend_redraws( bool stop )
{
    bool was_suspended = m_is_output_suspended;

    m_is_output_suspended = stop;

    if ( was_suspended && ! stop )
    {
        m_is_redraw_needed = false;
        Repaint( );
        SetWeakTimer( APP_NAME "_late_repaint",
                      &Display::static_late_repaint, 400 );
    }
}


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
