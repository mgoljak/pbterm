/*
 *  Extra keys / on-screen keyboard for pbterm. GPL-3.0-or-later.
 */

#include "ExtraKeys.hpp"
#include <cstdio>
#include <cstdlib>


// Capital of a letter: a-z only

static std::string
upper( std::string const & s )
{
    if ( s.size( ) == 1 && s[ 0 ] >= 'a' && s[ 0 ] <= 'z' )
        return std::string( 1, static_cast< char >( s[ 0 ] - 'a' + 'A' ) );

    return s;
}


static bool
is_letter( std::string const & s )
{
    return upper( s ) != s;
}


ExtraKeys::Rows
ExtraKeys::parse( std::string const & layout )
{
    Rows rows;
    std::vector< std::string > row;
    std::string tok;

    for ( std::size_t i = 0; i <= layout.size( ); ++i )
    {
        char c = i < layout.size( ) ? layout[ i ] : ',';

        if ( c == ' ' || c == '\t' || c == ',' )
        {
            if ( ! tok.empty( ) )
            {
                row.push_back( tok );
                tok.clear( );
            }

            // A lone comma ends the row; a "," key can't be configured here

            if ( c == ',' && ! row.empty( ) )
            {
                rows.push_back( row );
                row.clear( );
            }
        }
        else
            tok += c;
    }

    return rows;
}


ExtraKeys::ExtraKeys( std::string const & layout )
    : m_ctrl( false ), m_alt( false ), m_shift( false )
{
    build( parse( layout ) );
}


ExtraKeys::ExtraKeys( Rows const & rows )
    : m_ctrl( false ), m_alt( false ), m_shift( false )
{
    build( rows );
}


void
ExtraKeys::build( Rows const & rows )
{
    for ( std::size_t r = 0; r < rows.size( ); ++r )
    {
        std::vector< Key > row;

        for ( std::size_t c = 0; c < rows[ r ].size( ); ++c )
        {
            Key k;
            k.name   = rows[ r ][ c ];
            k.weight = 1;

            // "NAME*3": three times as wide (a lone "*" is the key itself)

            std::size_t star = k.name.rfind( '*' );
            if ( star != std::string::npos && star > 0 && star + 1 < k.name.size( ) )
            {
                int w = atoi( k.name.c_str( ) + star + 1 );
                if ( w > 0 )
                {
                    k.weight = w;
                    k.name.erase( star );
                }
            }

            row.push_back( k );
        }

        if ( ! row.empty( ) )
            m_keys.push_back( row );
    }
}


int
ExtraKeys::row_weight( int row ) const
{
    int w = 0;

    for ( std::size_t c = 0; c < m_keys[ row ].size( ); ++c )
        w += m_keys[ row ][ c ].weight;

    return w;
}


std::string
ExtraKeys::label( int row, int col ) const
{
    std::string const & n = m_keys[ row ][ col ].name;

    if ( n == "UP"    ) return "\xe2\x86\x91";
    if ( n == "DOWN"  ) return "\xe2\x86\x93";
    if ( n == "LEFT"  ) return "\xe2\x86\x90";
    if ( n == "RIGHT" ) return "\xe2\x86\x92";
    if ( n == "SHIFT" ) return "Aa";
    if ( n == "SYM"   ) return "123";
    if ( n == "ABC"   ) return "abc";
    if ( n == "BKSP"  ) return "BS";
    if ( n == "SPACE" ) return "";
    if ( n == "HIDE"  ) return "hide";

    return m_shift ? upper( n ) : n;
}


bool
ExtraKeys::active( int row, int col ) const
{
    std::string const & n = m_keys[ row ][ col ].name;
    return    ( n == "CTRL" && m_ctrl )
           || ( n == "ALT" && m_alt )
           || ( n == "SHIFT" && m_shift );
}


void
ExtraKeys::copy_modifiers( ExtraKeys const & o )
{
    m_ctrl  = o.m_ctrl;
    m_alt   = o.m_alt;
    m_shift = o.m_shift;
}


std::string
ExtraKeys::take_action( )
{
    std::string a;
    a.swap( m_action );
    return a;
}


// Applies armed modifiers to literal characters: Ctrl+letter gives the
// control code, Alt adds an ESC prefix.

std::string
ExtraKeys::with_modifiers( std::string const & s ) const
{
    if ( s.empty( ) )
        return s;

    std::string out;
    unsigned char c = static_cast< unsigned char >( s[ 0 ] );

    if ( m_ctrl )
    {
        if ( c >= 'a' && c <= 'z' )
            c = c - 'a' + 1;
        else if ( c >= 'A' && c <= 'Z' )
            c = c - 'A' + 1;
        else if ( c >= '@' && c <= '_' )
            c = c - '@';
        else if ( c == '?' )
            c = 0x7F;

        out += static_cast< char >( c );
        out += s.substr( 1 );
    }
    else
        out = s;

    return m_alt ? std::string( "\033" ) + out : out;
}


std::string
ExtraKeys::press( int row, int col, bool app )
{
    std::string const & n = m_keys[ row ][ col ].name;

    if ( n == "CTRL"  ) { m_ctrl  = ! m_ctrl;  return ""; }
    if ( n == "ALT"   ) { m_alt   = ! m_alt;   return ""; }
    if ( n == "SHIFT" ) { m_shift = ! m_shift; return ""; }

    if ( n == "SYM" || n == "ABC" || n == "HIDE" || n == "KBD" )
    {
        m_action = n;
        return "";
    }

    int mod = 1 + ( m_alt ? 2 : 0 ) + ( m_ctrl ? 4 : 0 );
    char letter = 0;
    const char * tilde = 0;
    std::string out;

    if      ( n == "UP"    ) letter = 'A';
    else if ( n == "DOWN"  ) letter = 'B';
    else if ( n == "RIGHT" ) letter = 'C';
    else if ( n == "LEFT"  ) letter = 'D';
    else if ( n == "HOME"  ) letter = 'H';
    else if ( n == "END"   ) letter = 'F';
    else if ( n == "INS"   ) tilde = "2";
    else if ( n == "DEL"   ) tilde = "3";
    else if ( n == "PGUP"  ) tilde = "5";
    else if ( n == "PGDN"  ) tilde = "6";

    char buf[ 32 ];

    if ( letter )
    {
        if ( mod > 1 )
            snprintf( buf, sizeof buf, "\033[1;%d%c", mod, letter );
        else if ( app )
            snprintf( buf, sizeof buf, "\033O%c", letter );
        else
            snprintf( buf, sizeof buf, "\033[%c", letter );
        out = buf;
    }
    else if ( tilde )
    {
        if ( mod > 1 )
            snprintf( buf, sizeof buf, "\033[%s;%d~", tilde, mod );
        else
            snprintf( buf, sizeof buf, "\033[%s~", tilde );
        out = buf;
    }
    else if ( n == "ESC" )
        out = "\033";
    else if ( n == "TAB" )
        out = "\t";
    else if ( n == "ENTER" )
        out = "\r";
    else if ( n == "SPACE" )
        out = with_modifiers( " " );
    else if ( n == "BKSP" )
        out = with_modifiers( "\x7f" );
    else
        out = with_modifiers( m_shift && is_letter( n ) ? upper( n ) : n );

    m_ctrl = m_alt = m_shift = false;
    return out;
}


std::string
ExtraKeys::apply_to_text( std::string const & text )
{
    std::string out = with_modifiers( text );
    m_ctrl = m_alt = false;
    return out;
}


bool
ExtraKeys::hit( int x, int y, int bx, int by, int bw, int bh,
                int & row, int & col ) const
{
    if ( m_keys.empty( ) || x < bx || y < by || x >= bx + bw || y >= by + bh )
        return false;

    row = ( y - by ) * rows( ) / bh;

    int total = row_weight( row ),
        pos   = ( x - bx ) * total / bw,
        cum   = 0;

    for ( col = 0; col < cols( row ); ++col )
    {
        cum += weight( row, col );
        if ( pos < cum )
            return true;
    }

    col = cols( row ) - 1;
    return true;
}
