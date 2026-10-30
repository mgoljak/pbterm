/*
 *  VT100/xterm subset emulator for pbterm. GPL-3.0-or-later.
 */

#include "Vt.hpp"
#include <algorithm>
#include <cstdio>


Vt::Vt( int cols, int rows, std::size_t max_scrollback )
    : m_cols( cols ), m_rows( rows ), m_max_scrollback( max_scrollback )
{
    m_alt_active = false;
    reset( );
}


void
Vt::reset( )
{
    m_cr = m_cc = m_saved_r = m_saved_c = 0;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrap_pending = false;
    m_autowrap = true;
    m_cursor_visible = true;
    m_app_cursor = false;
    m_alt_active = false;
    m_pen = Cell( );
    m_state = S_GROUND;
    m_utf_left = 0;
    m_utf_cp = 0;
    new_grid( m_grid, m_cols, m_rows );
    new_grid( m_alt_grid, m_cols, m_rows );
    m_dirty.assign( m_rows, 1 );
}


void
Vt::new_grid( std::vector< Row > & g, int cols, int rows ) const
{
    g.assign( rows, Row( cols, Cell( ) ) );
}


bool
Vt::any_dirty( ) const
{
    return std::find( m_dirty.begin( ), m_dirty.end( ), 1 ) != m_dirty.end( );
}


void
Vt::clear_dirty( )
{
    std::fill( m_dirty.begin( ), m_dirty.end( ), 0 );
}


std::string
Vt::take_reply( )
{
    std::string r;
    r.swap( m_reply );
    return r;
}


void
Vt::resize( int cols, int rows )
{
    if ( cols < 2 || rows < 2 || ( cols == m_cols && rows == m_rows ) )
        return;

    std::vector< Row > ng;
    new_grid( ng, cols, rows );

    // Keep the bottom-most content: shift up if the cursor would fall off

    int shift = m_cr >= rows ? m_cr - rows + 1 : 0;
    for ( int r = 0; r + shift < m_rows && r < rows; ++r )
    {
        if ( r < 0 )
            continue;
        for ( int c = 0; c < cols && c < m_cols; ++c )
            ng[ r ][ c ] = m_grid[ r + shift ][ c ];
    }
    m_grid.swap( ng );
    new_grid( m_alt_grid, cols, rows );

    m_cols = cols;
    m_rows = rows;
    m_cr = std::min( m_cr - shift, rows - 1 );
    m_cc = std::min( m_cc, cols - 1 );
    m_top = 0;
    m_bottom = rows - 1;
    m_wrap_pending = false;
    m_dirty.assign( rows, 1 );
}


int
Vt::param( std::size_t i, int def ) const
{
    return i < m_params.size( ) && m_params[ i ] > 0 ? m_params[ i ] : def;
}


void
Vt::feed( char const * data, std::size_t len )
{
    for ( std::size_t i = 0; i < len; ++i )
    {
        unsigned char c = static_cast< unsigned char >( data[ i ] );

        // Strings and escape sequences can be aborted by CAN/SUB

        if ( c == 0x18 || c == 0x1A )
        {
            m_state = S_GROUND;
            continue;
        }

        switch ( m_state )
        {
            case S_GROUND :
                if ( c == 0x1B )
                    m_state = S_ESC;
                else if ( c < 0x20 || c == 0x7F )
                    execute( c );
                else if ( c < 0x80 )
                {
                    m_utf_left = 0;
                    put_char( c );
                }
                else if ( c >= 0xC0 )
                {
                    m_utf_left = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
                    m_utf_cp = c & ( 0x3F >> m_utf_left );
                }
                else if ( m_utf_left > 0 )
                {
                    m_utf_cp = ( m_utf_cp << 6 ) | ( c & 0x3F );
                    if ( --m_utf_left == 0 )
                        put_char( m_utf_cp );
                }
                break;

            case S_ESC :
                if ( c == '[' )
                {
                    m_state = S_CSI;
                    m_params.clear( );
                    m_have_param = m_private = false;
                    m_cur_param = 0;
                }
                else if ( c == ']' )
                    m_state = S_OSC;
                else if ( c >= 0x20 && c <= 0x2F )
                {
                    m_inter = static_cast< char >( c );
                    m_state = S_ESC_INTER;
                }
                else
                {
                    m_state = S_GROUND;
                    if ( c < 0x20 )
                        execute( c );
                    else
                        esc_dispatch( c );
                }
                break;

            case S_ESC_INTER :              // e.g. ESC ( B  - charset, ignored
                if ( c < 0x20 || c > 0x2F )
                    m_state = S_GROUND;
                break;

            case S_CSI :
                if ( c >= '0' && c <= '9' )
                {
                    m_cur_param = std::min( m_cur_param * 10 + ( c - '0' ), 99999 );
                    m_have_param = true;
                }
                else if ( c == ';' || c == ':' )
                {
                    m_params.push_back( m_have_param ? m_cur_param : 0 );
                    m_cur_param = 0;
                    m_have_param = false;
                }
                else if ( c == '?' || c == '>' || c == '<' || c == '=' )
                    m_private = ( c == '?' );
                else if ( c >= 0x20 && c <= 0x2F )
                    ;                       // intermediate, ignored
                else if ( c >= 0x40 && c <= 0x7E )
                {
                    if ( m_have_param || ! m_params.empty( ) )
                        m_params.push_back( m_have_param ? m_cur_param : 0 );
                    csi_dispatch( c );
                    m_state = S_GROUND;
                }
                else if ( c < 0x20 )
                    execute( c );
                break;

            case S_OSC :
                if ( c == 0x07 )
                    m_state = S_GROUND;
                else if ( c == 0x1B )
                    m_state = S_OSC_ESC;
                break;

            case S_OSC_ESC :
                m_state = c == '\\' ? S_GROUND : S_OSC;
                break;
        }
    }
}


void
Vt::execute( unsigned char c )
{
    switch ( c )
    {
        case '\n' : case 0x0B : case 0x0C :
            linefeed( );
            break;

        case '\r' :
            m_cc = 0;
            m_wrap_pending = false;
            break;

        case '\b' :
            if ( m_cc > 0 )
                --m_cc;
            m_wrap_pending = false;
            break;

        case '\t' :
            do
                ++m_cc;
            while ( m_cc % 8 && m_cc < m_cols - 1 );
            m_cc = std::min( m_cc, m_cols - 1 );
            break;
    }
}


void
Vt::put_char( unsigned int ch )
{
    if ( m_wrap_pending )
    {
        if ( m_autowrap )
        {
            m_cc = 0;
            linefeed( );
        }
        m_wrap_pending = false;
    }

    Cell & cl = m_grid[ m_cr ][ m_cc ];
    cl = m_pen;
    cl.ch = ch;
    m_dirty[ m_cr ] = 1;

    if ( m_cc == m_cols - 1 )
        m_wrap_pending = true;
    else
        ++m_cc;
}


void
Vt::move_to( int r, int c )
{
    m_cr = std::max( 0, std::min( r, m_rows - 1 ) );
    m_cc = std::max( 0, std::min( c, m_cols - 1 ) );
    m_wrap_pending = false;
}


void
Vt::linefeed( )
{
    if ( m_cr == m_bottom )
        scroll_up( 1 );
    else if ( m_cr < m_rows - 1 )
        ++m_cr;
}


void
Vt::reverse_index( )
{
    if ( m_cr == m_top )
        scroll_down( 1 );
    else if ( m_cr > 0 )
        --m_cr;
}


void
Vt::scroll_up( int n )
{
    n = std::min( n, m_bottom - m_top + 1 );

    for ( int i = 0; i < n; ++i )
    {
        if ( ! m_alt_active && m_top == 0 && m_max_scrollback )
        {
            m_scrollback.push_back( m_grid[ 0 ] );
            if ( m_scrollback.size( ) > m_max_scrollback )
                m_scrollback.pop_front( );
        }
        m_grid.erase( m_grid.begin( ) + m_top );
        m_grid.insert( m_grid.begin( ) + m_bottom, Row( m_cols, Cell( ) ) );
    }

    for ( int r = m_top; r <= m_bottom; ++r )
        m_dirty[ r ] = 1;
}


void
Vt::scroll_down( int n )
{
    n = std::min( n, m_bottom - m_top + 1 );

    for ( int i = 0; i < n; ++i )
    {
        m_grid.erase( m_grid.begin( ) + m_bottom );
        m_grid.insert( m_grid.begin( ) + m_top, Row( m_cols, Cell( ) ) );
    }

    for ( int r = m_top; r <= m_bottom; ++r )
        m_dirty[ r ] = 1;
}


void
Vt::erase_cells( int row, int c0, int c1 )              // [c0, c1)
{
    Cell blank;
    blank.bg = m_pen.bg;

    for ( int c = std::max( c0, 0 ); c < std::min( c1, m_cols ); ++c )
        m_grid[ row ][ c ] = blank;
    m_dirty[ row ] = 1;
}


void
Vt::erase_display( int mode )
{
    if ( mode == 0 )
    {
        erase_cells( m_cr, m_cc, m_cols );
        for ( int r = m_cr + 1; r < m_rows; ++r )
            erase_cells( r, 0, m_cols );
    }
    else if ( mode == 1 )
    {
        for ( int r = 0; r < m_cr; ++r )
            erase_cells( r, 0, m_cols );
        erase_cells( m_cr, 0, m_cc + 1 );
    }
    else
    {
        for ( int r = 0; r < m_rows; ++r )
            erase_cells( r, 0, m_cols );
        if ( mode == 3 )
            m_scrollback.clear( );
    }
}


void
Vt::erase_line( int mode )
{
    if ( mode == 0 )
        erase_cells( m_cr, m_cc, m_cols );
    else if ( mode == 1 )
        erase_cells( m_cr, 0, m_cc + 1 );
    else
        erase_cells( m_cr, 0, m_cols );
}


void
Vt::insert_lines( int n )
{
    if ( m_cr < m_top || m_cr > m_bottom )
        return;
    int save = m_top;
    m_top = m_cr;
    scroll_down( n );
    m_top = save;
    m_cc = 0;
}


void
Vt::delete_lines( int n )
{
    if ( m_cr < m_top || m_cr > m_bottom )
        return;
    int save = m_top;
    bool alt = m_alt_active;
    m_top = m_cr;
    m_alt_active = true;                // don't push deleted lines to history
    scroll_up( n );
    m_alt_active = alt;
    m_top = save;
    m_cc = 0;
}


void
Vt::insert_chars( int n )
{
    Row & r = m_grid[ m_cr ];
    n = std::min( n, m_cols - m_cc );
    r.insert( r.begin( ) + m_cc, n, Cell( ) );
    r.resize( m_cols );
    m_dirty[ m_cr ] = 1;
}


void
Vt::delete_chars( int n )
{
    Row & r = m_grid[ m_cr ];
    n = std::min( n, m_cols - m_cc );
    r.erase( r.begin( ) + m_cc, r.begin( ) + m_cc + n );
    r.resize( m_cols );
    m_dirty[ m_cr ] = 1;
}


void
Vt::switch_alt( bool on )
{
    if ( on == m_alt_active )
        return;

    m_grid.swap( m_alt_grid );
    m_alt_active = on;

    if ( on )
        for ( int r = 0; r < m_rows; ++r )
            erase_cells( r, 0, m_cols );

    m_dirty.assign( m_rows, 1 );
}


void
Vt::esc_dispatch( unsigned char c )
{
    switch ( c )
    {
        case '7' : m_saved_r = m_cr; m_saved_c = m_cc; break;
        case '8' : move_to( m_saved_r, m_saved_c ); break;
        case 'D' : linefeed( ); break;
        case 'E' : m_cc = 0; linefeed( ); break;
        case 'M' : reverse_index( ); break;
        case 'c' : reset( ); break;
        default  : break;                       // ESC = / > etc. ignored
    }
}


void
Vt::set_sgr( )
{
    if ( m_params.empty( ) )
        m_params.push_back( 0 );

    for ( std::size_t i = 0; i < m_params.size( ); ++i )
    {
        int p = m_params[ i ];

        if ( p == 0 )
            m_pen = Cell( );
        else if ( p == 1 )  m_pen.attr |= ATTR_BOLD;
        else if ( p == 2 )  m_pen.attr |= ATTR_DIM;
        else if ( p == 3 )  m_pen.attr |= ATTR_ITALIC;
        else if ( p == 4 )  m_pen.attr |= ATTR_UNDERLINE;
        else if ( p == 7 )  m_pen.attr |= ATTR_REVERSE;
        else if ( p == 22 ) m_pen.attr &= ~( ATTR_BOLD | ATTR_DIM );
        else if ( p == 23 ) m_pen.attr &= ~ATTR_ITALIC;
        else if ( p == 24 ) m_pen.attr &= ~ATTR_UNDERLINE;
        else if ( p == 27 ) m_pen.attr &= ~ATTR_REVERSE;
        else if ( p >= 30 && p <= 37 )   m_pen.fg = p - 30;
        else if ( p == 39 )              m_pen.fg = DEFAULT_COLOR;
        else if ( p >= 40 && p <= 47 )   m_pen.bg = p - 40;
        else if ( p == 49 )              m_pen.bg = DEFAULT_COLOR;
        else if ( p >= 90 && p <= 97 )   m_pen.fg = p - 90 + 8;
        else if ( p >= 100 && p <= 107 ) m_pen.bg = p - 100 + 8;
        else if ( p == 38 || p == 48 )
        {
            unsigned char & tgt = p == 38 ? m_pen.fg : m_pen.bg;

            if ( i + 2 < m_params.size( ) && m_params[ i + 1 ] == 5 )
            {
                tgt = static_cast< unsigned char >( m_params[ i + 2 ] & 0xFF );
                i += 2;
            }
            else if ( i + 4 < m_params.size( ) && m_params[ i + 1 ] == 2 )
            {
                // truecolor -> nearest of the 6x6x6 cube (e-ink doesn't care)
                int r = m_params[ i + 2 ] * 5 / 255,
                    g = m_params[ i + 3 ] * 5 / 255,
                    b = m_params[ i + 4 ] * 5 / 255;
                tgt = static_cast< unsigned char >( 16 + 36 * r + 6 * g + b );
                i += 4;
            }
        }
    }
}


void
Vt::set_mode( bool on )
{
    if ( ! m_private )
        return;

    for ( std::size_t i = 0; i < m_params.size( ); ++i )
        switch ( m_params[ i ] )
        {
            case 1 :    m_app_cursor = on; break;
            case 7 :    m_autowrap = on; break;
            case 25 :   m_cursor_visible = on; break;
            case 47 : case 1047 :
                        switch_alt( on ); break;
            case 1049 :
                if ( on )
                {
                    m_saved_r = m_cr;
                    m_saved_c = m_cc;
                    switch_alt( true );
                }
                else
                {
                    switch_alt( false );
                    move_to( m_saved_r, m_saved_c );
                }
                break;
        }
}


void
Vt::csi_dispatch( unsigned char c )
{
    int n = param( 0, 1 );
    char buf[ 32 ];

    switch ( c )
    {
        case 'A' : move_to( m_cr - n, m_cc ); break;
        case 'B' : case 'e' : move_to( m_cr + n, m_cc ); break;
        case 'C' : case 'a' : move_to( m_cr, m_cc + n ); break;
        case 'D' : move_to( m_cr, m_cc - n ); break;
        case 'E' : move_to( m_cr + n, 0 ); break;
        case 'F' : move_to( m_cr - n, 0 ); break;
        case 'G' : case '`' : move_to( m_cr, n - 1 ); break;
        case 'd' : move_to( n - 1, m_cc ); break;
        case 'H' : case 'f' : move_to( param( 0, 1 ) - 1, param( 1, 1 ) - 1 ); break;
        case 'J' : erase_display( m_params.empty( ) ? 0 : m_params[ 0 ] ); break;
        case 'K' : erase_line( m_params.empty( ) ? 0 : m_params[ 0 ] ); break;
        case 'L' : insert_lines( n ); break;
        case 'M' : delete_lines( n ); break;
        case '@' : insert_chars( n ); break;
        case 'P' : delete_chars( n ); break;
        case 'X' : erase_cells( m_cr, m_cc, m_cc + n ); break;
        case 'S' : scroll_up( n ); break;
        case 'T' : scroll_down( n ); break;
        case 'm' : set_sgr( ); break;
        case 'h' : set_mode( true ); break;
        case 'l' : set_mode( false ); break;
        case 's' : m_saved_r = m_cr; m_saved_c = m_cc; break;
        case 'u' : move_to( m_saved_r, m_saved_c ); break;

        case 'r' :
        {
            int t = param( 0, 1 ) - 1, b = param( 1, m_rows ) - 1;
            if ( t < b && b < m_rows )
            {
                m_top = t;
                m_bottom = b;
                move_to( 0, 0 );
            }
            break;
        }

        case 'n' :
            if ( param( 0, 0 ) == 6 )
            {
                snprintf( buf, sizeof buf, "\033[%d;%dR", m_cr + 1, m_cc + 1 );
                m_reply += buf;
            }
            else if ( param( 0, 0 ) == 5 )
                m_reply += "\033[0n";
            break;

        case 'c' :
            m_reply += "\033[?1;2c";
            break;
    }
}


std::string
Vt::row_text( int row ) const
{
    std::string s;

    for ( int c = 0; c < m_cols; ++c )
    {
        unsigned int ch = m_grid[ row ][ c ].ch;
        if ( ch < 0x80 )
            s += static_cast< char >( ch );
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

    std::size_t e = s.find_last_not_of( ' ' );
    return e == std::string::npos ? std::string( ) : s.substr( 0, e + 1 );
}
