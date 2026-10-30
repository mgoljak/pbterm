/*
 *  VT100/xterm subset emulator for pbterm (C++98, no dependencies).
 *  GPL-3.0-or-later, same as pbterm.
 */

#if ! defined VT_HPP_
#define VT_HPP_

#include <string>
#include <vector>
#include <deque>
#include <cstddef>


struct Cell
{
    unsigned int  ch;       // Unicode code point, ' ' for empty
    unsigned char attr;     // see Vt::ATTR_*
    unsigned char fg;       // palette index, Vt::DEFAULT_COLOR if default
    unsigned char bg;

    Cell( ) : ch( ' ' ), attr( 0 ), fg( 0xFF ), bg( 0xFF ) { }
};


class Vt
{
  public :

    enum { ATTR_BOLD = 1, ATTR_DIM = 2, ATTR_UNDERLINE = 4,
           ATTR_REVERSE = 8, ATTR_ITALIC = 16 };

    enum { DEFAULT_COLOR = 0xFF };

    Vt( int cols, int rows, std::size_t max_scrollback );

    // Feed raw bytes coming from the pty

    void feed( char const * data, std::size_t len );

    // Change screen size (call together with TIOCSWINSZ on the pty)

    void resize( int cols, int rows );

    int  cols( ) const { return m_cols; }
    int  rows( ) const { return m_rows; }
    int  cursor_row( ) const { return m_cr; }
    int  cursor_col( ) const { return m_cc; }
    bool cursor_visible( ) const { return m_cursor_visible; }
    bool app_cursor_keys( ) const { return m_app_cursor; }
    bool alt_screen( ) const { return m_alt_active; }

    // Cell of the visible screen
    Cell const & cell( int row, int col ) const { return m_grid[ row ][ col ]; }

    // Scrollback access: 0 = newest line scrolled off the top
    std::size_t scrollback_size( ) const { return m_scrollback.size( ); }
    std::vector< Cell > const & scrollback_line( std::size_t i ) const
    { return m_scrollback[ m_scrollback.size( ) - 1 - i ]; }

    // Dirty-row tracking for e-ink partial updates
    bool row_dirty( int row ) const { return m_dirty[ row ] != 0; }
    bool any_dirty( ) const;
    void clear_dirty( );

    // Bytes the emulator wants written back to the pty (DSR, DA replies)
    std::string take_reply( );

    // Text of a visible row, UTF-8, right-trimmed (for tests and copying)
    std::string row_text( int row ) const;

  private :

    typedef std::vector< Cell > Row;

    enum State { S_GROUND, S_ESC, S_ESC_INTER, S_CSI, S_OSC, S_OSC_ESC };

    void put_char( unsigned int ch );
    void execute( unsigned char c );
    void esc_dispatch( unsigned char c );
    void csi_dispatch( unsigned char c );
    void set_sgr( );
    void set_mode( bool on );
    void linefeed( );
    void reverse_index( );
    void scroll_up( int n );
    void scroll_down( int n );
    void erase_cells( int row, int c0, int c1 );
    void erase_display( int mode );
    void erase_line( int mode );
    void insert_lines( int n );
    void delete_lines( int n );
    void insert_chars( int n );
    void delete_chars( int n );
    void move_to( int r, int c );
    void switch_alt( bool on );
    void reset( );
    int  param( std::size_t i, int def ) const;
    void new_grid( std::vector< Row > & g, int cols, int rows ) const;

    int m_cols, m_rows;
    int m_cr, m_cc;                 // cursor
    int m_saved_r, m_saved_c;
    int m_top, m_bottom;            // scroll region (inclusive)
    bool m_wrap_pending, m_autowrap, m_cursor_visible, m_app_cursor;
    bool m_alt_active;
    Cell m_pen;                     // current attributes (ch unused)

    std::vector< Row > m_grid, m_alt_grid;
    std::vector< char > m_dirty;
    std::deque< Row > m_scrollback;
    std::size_t m_max_scrollback;

    State m_state;
    std::vector< int > m_params;
    bool m_have_param, m_private;
    int m_cur_param;
    char m_inter;

    unsigned int m_utf_cp;
    int m_utf_left;

    std::string m_reply;
};

#endif
