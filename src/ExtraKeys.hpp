/*
 *  Extra keys / on-screen keyboard for pbterm: layout parsing, escape
 *  sequences, sticky Ctrl/Alt/Shift handling. No dependencies (C++98).
 *  GPL-3.0-or-later, same as pbterm.
 */

#if ! defined EXTRAKEYS_HPP_
#define EXTRAKEYS_HPP_

#include <string>
#include <vector>


class ExtraKeys
{
  public :

    typedef std::vector< std::vector< std::string > > Rows;

    // Layout: key names separated by blanks, rows separated by commas, e.g.
    // "ESC / - HOME UP END PGUP , TAB CTRL ALT LEFT DOWN RIGHT PGDN".
    // Names: ESC TAB ENTER UP DOWN LEFT RIGHT HOME END PGUP PGDN DEL INS
    // CTRL ALT SHIFT SPACE BKSP and the actions SYM ABC HIDE KBD (see
    // take_action()). Anything else is sent as typed. "NAME*3" makes a key
    // three times as wide as normal.

    explicit ExtraKeys( std::string const & layout );
    explicit ExtraKeys( Rows const & rows );

    // Splits a layout string into rows of key names
    static Rows parse( std::string const & layout );

    int rows( ) const { return static_cast< int >( m_keys.size( ) ); }
    int cols( int row ) const { return static_cast< int >( m_keys[ row ].size( ) ); }
    int weight( int row, int col ) const { return m_keys[ row ][ col ].weight; }
    int row_weight( int row ) const;

    // Text shown on a key (UTF-8); letters are shown in capitals while
    // SHIFT is armed
    std::string label( int row, int col ) const;

    // True if the key is CTRL, ALT or SHIFT and currently armed
    bool active( int row, int col ) const;

    bool armed( ) const { return m_ctrl || m_alt; }

    // Key was tapped: returns the bytes to send to the shell (empty for
    // modifiers and actions). Armed modifiers are used up.
    std::string press( int row, int col, bool app_cursor_keys );

    // Set by press() for the keys SYM, ABC, HIDE and KBD (the caller
    // switches the layout); returns it once, then it is empty.
    std::string take_action( );

    // Text came from the system keyboard while CTRL or ALT was armed
    std::string apply_to_text( std::string const & text );

    // Take over the armed modifiers of another instance (after a re-layout)
    void copy_modifiers( ExtraKeys const & o );

    // Which key is at the point (x, y) of a bar placed at (bx, by) with
    // size (bw, bh)? Returns false if none.
    bool hit( int x, int y, int bx, int by, int bw, int bh,
              int & row, int & col ) const;

  private :

    struct Key
    {
        std::string name;       // as in the layout
        int         weight;
    };

    void build( Rows const & rows );
    std::string with_modifiers( std::string const & chars ) const;

    std::vector< std::vector< Key > > m_keys;
    bool m_ctrl, m_alt, m_shift;
    std::string m_action;
};

#endif
