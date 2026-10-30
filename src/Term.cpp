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


#include "Term.hpp"
#include "Messenger.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "Defaults.hpp"
#include <cerrno>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <dirent.h>
#include <set>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>


// Definition of the static member used to find the Term instance from
// within static member functions

Term * Term::s_handling_term;


/******************************************
 * Constructor, starts the shell and tries to read in stored commands
 * from a previous session
 ******************************************/

Term::Term( Messenger & mess,
            Config    & config,
            int         cols,
            int         rows )
    : m_mess( mess )
    , m_init_cols( cols )
    , m_init_rows( rows )
    , m_check_interval( config.check_interval( ) )
    , m_fast_ticks( 0 )
    , m_trace_left( 16384 )
    , m_shell_pid( -1 )
    , m_hide_tty_msg( true )
    , m_hist_pos( 0 )
    , m_write_fd( -1 )
    , m_read_fd( -1 )
    , m_logger( config.logger( ) )
    , m_max_history( config.max_history( ) )
    , m_cmd_file( config.cmd_file( ) )
{
    s_handling_term = this;

    // Start the shell, abort on any failures

    if ( ( m_shell_pid = start_shell( config.shell( ) ) ) <= 0 )
    {
        m_mess.send( message::Close( ) );
        return;
    }

    // Read in the file with the command history

    read_cmd_file( );
    m_hist_pos = m_commands.size( );

    // Start looking for messages from the shell

    timer_handler( );
}


/***************************************
 * Destructor, stores the command history and closes connections to shell,
 * this in turn will make it quit (it's going to receive a SIGPIPE)
 ***************************************/

Term::~Term( )
{
    if ( m_write_fd >= 0 )
    {
        close( m_write_fd );
        if ( m_write_fd != m_read_fd )
            close( m_read_fd );
        write_cmd_file( );
    }
}


/******************************************
 * Returns if communication with the shell uses a pseudoterminal
 ******************************************/

bool
Term::using_pty( )
{
    return m_write_fd == m_read_fd;
}


/******************************************
 * Stores a command at the end of the list of commands and erases earlier
 * entries of the same command. Keeps the list from getting longer than
 * is set by 'm_max_history'.
 ******************************************/

void
Term::save_command( std::string const & cmd )
{
    for ( std::size_t i = 0; i < m_commands.size( ); ++i )
        if ( cmd == m_commands[ i ] )
        {
            m_commands.erase( m_commands.begin( ) + i );
            break;
        }

    while ( m_commands.size( ) >= m_max_history )
        m_commands.erase( m_commands.begin( ) );

    m_commands.push_back( cmd );
}


/******************************************
 * Returns list of all commands in the "history"
 ******************************************/

std::vector< std::string > const &
Term::command_list( ) const
{
    return m_commands;
}


/******************************************
 * Returns the last command in the "history" (or the
 * empty string if there's nothing in the history)
 ******************************************/

std::string
Term::last_command( ) const
{
    return m_commands.empty( ) ? "" : m_commands.back( );
}


/******************************************
 * Sends a string to the shell and stores it in the history
 * (if sending the string did succeed)
 ******************************************/

void
Term::send_command( std::string const & cmd )
{
    if ( send( cmd.c_str( ), cmd.size( ) ) )
        save_command( cmd.substr( 0, cmd.size( ) - 1 ) );
}


/******************************************
 * Sends a single (control character) to the shell
 ******************************************/

void
Term::send_control( char ctrl )
{
    send( &ctrl, 1 );
}


/******************************************
 * Logs bytes in a readable form, e.g. ESC [ 6 n as \\e[6n
 ******************************************/

void
Term::trace( char         dir,
             char const * data,
             int          len )
{
    if ( m_trace_left <= 0 )
        return;

    std::string out;
    char buf[ 8 ];

    for ( int i = 0; i < len; ++i )
    {
        unsigned char c = static_cast< unsigned char >( data[ i ] );

        if ( c == 0x1B )
            out += "\\e";
        else if ( c == '\n' )
            out += "\\n";
        else if ( c == '\r' )
            out += "\\r";
        else if ( c < 0x20 || c >= 0x7F )
        {
            snprintf( buf, sizeof buf, "\\x%02x", c );
            out += buf;
        }
        else
            out += static_cast< char >( c );
    }

    m_trace_left -= len;
    m_logger.info( ) << "io " << dir << " " << out << std::endl;
}


/******************************************
 * Line editor used when there is no pty
 ******************************************/

void
Term::echo( std::string const & text )
{
    m_mess.send( message::New_Text( text ) );
}


void
Term::line_insert( std::string const & text )
{
    m_line += text;
    echo( text );
}


void
Term::line_backspace( )
{
    if ( m_line.empty( ) )
        return;

    // Remove one UTF-8 character

    while ( ! m_line.empty( ) && ( m_line[ m_line.size( ) - 1 ] & 0xC0 ) == 0x80 )
        m_line.erase( m_line.size( ) - 1 );
    if ( ! m_line.empty( ) )
        m_line.erase( m_line.size( ) - 1 );

    echo( "\b \b" );
}


void
Term::line_replace( std::string const & new_line )
{
    std::string erase;

    for ( std::size_t i = 0; i < m_line.size( ); ++i )
        if ( ( m_line[ i ] & 0xC0 ) != 0x80 )
            erase += "\b \b";

    m_line = new_line;
    echo( erase + m_line );
}


void
Term::line_history( int dir )
{
    if ( m_commands.empty( ) )
        return;

    if ( m_hist_pos > m_commands.size( ) )
        m_hist_pos = m_commands.size( );

    if ( dir < 0 )
    {
        if ( m_hist_pos == 0 )
            return;
        if ( m_hist_pos == m_commands.size( ) )
            m_saved_line = m_line;
        line_replace( m_commands[ -- m_hist_pos ] );
    }
    else
    {
        if ( m_hist_pos >= m_commands.size( ) )
            return;
        ++m_hist_pos;
        line_replace( m_hist_pos == m_commands.size( ) ? m_saved_line
                                                      : m_commands[ m_hist_pos ] );
    }
}


void
Term::line_submit( )
{
    std::string cmd( m_line );

    m_line.clear( );
    echo( "\r\n" );

    if ( ! cmd.empty( ) )
        save_command( cmd );
    m_hist_pos = m_commands.size( );
    m_saved_line.clear( );

    cmd += '\n';
    send( cmd.c_str( ), cmd.size( ) );
}


void
Term::line_complete( )
{
    static char const * const builtins[ ] = {
        "cd", "echo", "exit", "export", "pwd", "set", "unset", "alias",
        "exec", "eval", "kill", "read", "source", "type", "umask", "wait",
        "trap", "test", "printf", "history", "jobs", "fg", "bg", 0 };

    std::size_t sp = m_line.find_last_of( ' ' );
    bool first = sp == std::string::npos;
    std::string word = first ? m_line : m_line.substr( sp + 1 ),
                dir_part,
                base = word;

    std::size_t sl = word.rfind( '/' );
    if ( sl != std::string::npos )
    {
        dir_part = word.substr( 0, sl + 1 );
        base     = word.substr( sl + 1 );
    }

    // Directory of the shell is where relative names are looked up

    char buf[ 1024 ];
    std::string cwd;
    snprintf( buf, sizeof buf, "/proc/%d/cwd", static_cast< int >( m_shell_pid ) );
    ssize_t n = readlink( buf, buf, sizeof buf - 1 );
    if ( n > 0 )
        cwd.assign( buf, n );
    else if ( getcwd( buf, sizeof buf ) )
        cwd = buf;

    std::string dir;
    if ( dir_part.empty( ) )
        dir = cwd;
    else if ( dir_part[ 0 ] == '/' )
        dir = dir_part;
    else if ( dir_part[ 0 ] == '~' && getenv( "HOME" ) )
        dir = std::string( getenv( "HOME" ) ) + dir_part.substr( 1 );
    else
        dir = cwd + "/" + dir_part;

    std::set< std::string > cands;

    if ( DIR * d = opendir( dir.c_str( ) ) )
    {
        while ( struct dirent * e = readdir( d ) )
        {
            std::string name( e->d_name );

            if ( name == "." || name == ".." )
                continue;
            if ( name[ 0 ] == '.' && ( base.empty( ) || base[ 0 ] != '.' ) )
                continue;
            if ( name.compare( 0, base.size( ), base ) != 0 )
                continue;

            struct stat st;
            bool is_dir =    stat( ( dir + "/" + name ).c_str( ), &st ) == 0
                          && S_ISDIR( st.st_mode );
            cands.insert( is_dir ? name + "/" : name );
        }
        closedir( d );
    }

    // A command name: also look through the builtins and $PATH

    if ( first && dir_part.empty( ) && ! base.empty( ) )
    {
        for ( int i = 0; builtins[ i ]; ++i )
            if ( std::string( builtins[ i ] ).compare( 0, base.size( ), base ) == 0 )
                cands.insert( builtins[ i ] );

        char const * path = getenv( "PATH" );
        std::string p( path ? path : "/bin:/usr/bin:/sbin:/usr/sbin" );

        for ( std::size_t pos = 0; pos <= p.size( ); )
        {
            std::size_t e = p.find( ':', pos );
            if ( e == std::string::npos )
                e = p.size( );

            std::string pd = p.substr( pos, e - pos );
            pos = e + 1;

            if ( DIR * d = opendir( pd.c_str( ) ) )
            {
                while ( struct dirent * de = readdir( d ) )
                {
                    std::string name( de->d_name );
                    if (    name.compare( 0, base.size( ), base ) == 0
                         && access( ( pd + "/" + name ).c_str( ), X_OK ) == 0 )
                        cands.insert( name );
                }
                closedir( d );
            }
        }
    }

    if ( cands.empty( ) )
        return;

    if ( cands.size( ) == 1 )
    {
        std::string c = *cands.begin( );
        line_insert( c.substr( base.size( ) )
                     + ( c[ c.size( ) - 1 ] == '/' ? "" : " " ) );
        return;
    }

    // Several candidates: complete the common part, if there's none show
    // the list

    std::string common = *cands.begin( );

    for ( std::set< std::string >::const_iterator it = cands.begin( );
          it != cands.end( ); ++it )
    {
        std::size_t i = 0;
        while ( i < common.size( ) && i < it->size( ) && common[ i ] == ( *it )[ i ] )
            ++i;
        common.erase( i );
    }

    if ( common.size( ) > base.size( ) )
    {
        line_insert( common.substr( base.size( ) ) );
        return;
    }

    std::string list( "\r\n" );
    for ( std::set< std::string >::const_iterator it = cands.begin( );
          it != cands.end( ); ++it )
        list += *it + "  ";
    echo( list + "\r\n" + m_line );
}


void
Term::input( std::string const & data )
{
    if ( using_pty( ) )
    {
        send_raw( data );
        return;
    }

    for ( std::size_t i = 0; i < data.size( ); ++i )
    {
        unsigned char c = static_cast< unsigned char >( data[ i ] );

        // Escape sequences (arrow keys etc.)

        if ( ! m_esc.empty( ) || c == 0x1B )
        {
            m_esc += static_cast< char >( c );

            if ( m_esc.size( ) == 1 )
                continue;

            if ( m_esc.size( ) == 2 && ( c == '[' || c == 'O' ) )
                continue;

            if ( m_esc.size( ) > 2 && ! ( c >= 0x40 && c <= 0x7E ) )
                continue;                           // parameters

            if ( m_esc.size( ) > 2 && m_esc[ 1 ] != ';' )
            {
                if ( c == 'A' )
                    line_history( -1 );
                else if ( c == 'B' )
                    line_history( +1 );
            }

            m_esc.clear( );
            continue;
        }

        switch ( c )
        {
            case '\r' : case '\n' :
                line_submit( );
                break;

            case 0x7F : case 0x08 :
                line_backspace( );
                break;

            case 0x15 :                             // ^U
                line_replace( "" );
                break;

            case '\t' :
                line_complete( );
                break;

            case 0x03 :                             // ^C
                m_line.clear( );
                m_hist_pos = m_commands.size( );
                echo( "^C\r\n" );
                if ( m_shell_pid > 0 && kill( - m_shell_pid, SIGINT ) == -1 )
                    m_logger.error( ) << "kill() failed: "
                                      << strerror( errno ) << std::endl;
                break;

            default :
                if ( c >= 0x20 )
                    line_insert( std::string( 1, static_cast< char >( c ) ) );
                break;
        }
    }

    // Keys arrive as complete sequences, so a lone ESC is just that

    m_esc.clear( );
}


/******************************************
 * Sends a string of raw bytes to the shell
 ******************************************/

void
Term::send_raw( std::string const & data )
{
    if ( ! data.empty( ) )
        send( data.data( ), data.size( ) );
}


/******************************************
 * Sets the window size of the pseudoterminal
 ******************************************/

void
Term::set_winsize( int cols,
                   int rows )
{
    if ( ! using_pty( ) || m_write_fd < 0 )
        return;

    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = ws.ws_ypixel = 0;

    if ( ioctl( m_write_fd, TIOCSWINSZ, &ws ) == -1 )
        m_logger.error( ) << "ioctl() with TIOCSWINSZ failed: "
                          << strerror( errno ) << std::endl;
}


/******************************************
 * Helper function that does the actual sending
 ******************************************/

bool
Term::send( char const * data,
            int          len )
{
    int cnt = 0;

    trace( '>', data, len );

    // Something is sent, so an answer is to be expected soon

    m_fast_ticks = 200;
    SetWeakTimer( APP_NAME "_timer", &Term::static_timer_handler, 10 );

    while ( ( len -= cnt ) > 0 )
    {
        cnt = write( m_write_fd, data, len );

        if ( cnt < 0 )
        {
            m_logger.error( ) << "write() to pty failed: "
                              << strerror( errno ) << std::endl;
            m_mess.send( message::Close( ) );
            return false;
        }

        data += cnt;
    }

    return true;
}


/***************************************
 * We need a static function since we must pass it to a C function, so
 * we have this "dummy" function that we can use with C and that just
 * redirects to the real function for dealing with the timer event.
 ***************************************/

void
Term::static_timer_handler( )
{
    s_handling_term->timer_handler( );
}


/***************************************
 * Handler for timer events, checks if the shell send any new data
 ***************************************/

void
Term::timer_handler( )
{
    // Ok, let's see if there's something to be obtained from the shell

    std::string txt;
    int retval = get_shell_output( txt );

    // A negative return value indicates that the shell closed its output
    // channels (probably because it stopped working) or due to some other
    // serious error. A return value of 0 is to be expected when the shell
    // didn't send anything.

    if ( retval == -1 )
    {
        m_mess.send( message::Close( ) );
        return;
    }
    else if ( retval > 0 )
    {
        // Pass on any new text to the display and, if recording is on,
        // write it to the log file

        // Without a pty ash always says it can't do job control: drop it

        if ( m_hide_tty_msg )
        {
            std::size_t p = txt.find( "can't access tty; job control turned off" );

            if ( p != std::string::npos )
            {
                std::size_t b = txt.rfind( '\n', p ),
                            e = txt.find( '\n', p );

                txt.erase( b == std::string::npos ? 0 : b + 1,
                           ( e == std::string::npos ? txt.size( ) : e + 1 )
                           - ( b == std::string::npos ? 0 : b + 1 ) );
                m_hide_tty_msg = false;
            }
        }

        m_fast_ticks = std::max( m_fast_ticks, 50 );
        if ( ! txt.empty( ) )
            m_mess.send( message::New_Text( txt ) );
    }

    // (Re)start the timer that gets us back here for more, poll fast
    // while there is activity

    int interval = m_check_interval;

    if ( m_fast_ticks > 0 )
    {
        --m_fast_ticks;
        interval = std::min( interval, 10 );
    }

    SetWeakTimer( APP_NAME "_timer", &Term::static_timer_handler, interval );
}


/***************************************
 * Tries to read in the file with commands used in a previous session
 ***************************************/

void
Term::read_cmd_file( )
{
    // Try to open the commamd file for reading

    std::ifstream ifs( m_cmd_file.c_str( ), std::ifstream::in );

    std::string  line;

    // Keep reading until the end of the file is reached or as many
    // commands have been found as fit into the history

    while ( ifs.good( ) )
    {
        std::getline( ifs, line );

        if ( ifs.fail( ) )
            break;

        // Remove empty lines and comments (lines starting with a hash mark)

        if ( Utils::left_trim( line ).empty( ) || line[ 0 ] == '#' )
            continue;

        save_command( line );
    }
}


/***************************************
 * Tries to write out a file with all commands stored in this session
 ***************************************/

void
Term::write_cmd_file( )
{
    // Create all necessary drectories (if possible)

    std::string path( Utils::prepare_file_creation( m_cmd_file ) );

    if ( path.empty( ) )
        return;

    // Try to open the commandd file for writing

    std::ofstream ofs( path.c_str( ), std::ofstream::out );

    if ( ofs.bad( ) )
        return;

    // Write out all commands to the file

    for ( std::vector< std::string >::const_iterator it = m_commands.begin( );
          it != m_commands.end( ); ++it )
        ofs << *it << std::endl;
}


/******************************************                                     
 * Recreates a pseudoterminal and set it up and spawns the shell with all
 * standard file descriptors redirected to the slave part of the pseudo-
 * terminal.
 ******************************************/

pid_t
Term::start_shell( std::string const & shell )
{
    // Open the master side of the pseudterminal and get the name of the
    // slave part's file.

    std::string slave_name;

    if ( ( m_write_fd = m_read_fd = open_master_pty( slave_name ) ) == -1 )
        return -1;

    // A return value of -2 indicates that opening the pseudoterminal failed
    // due to permission issues, so let's try it with pipes instead

    if ( m_write_fd == -2 )
        return start_piped_shell( shell );

    // Let the shell see the real terminal size from the start

    set_winsize( m_init_cols, m_init_rows );

    // Now that we have the master and the slave name (or a set of pipes)
    // we can start the child process

    pid_t child_pid = fork( );
    switch ( child_pid )
    {
        case -1 :
            m_logger.error( ) << "fork() failed: "
                              << strerror( errno ) << std::endl;
            close( m_write_fd );
            m_write_fd = m_read_fd = -1;
            return -1;

        case 0 :                                 // Child
            close( m_write_fd );
            setup_child( slave_name, shell );
            exit( 1 );
    }

    return child_pid;
}


/******************************************                                     
 * Opens the master pseudoterminal, sets it up and 
 ******************************************/

int
Term::open_master_pty( std::string & slave_name )
{
    int fd;

    // Open the master part of the pseudoterminal. Set it to non-blocking
    // to avoid having to call select() or poll() each time we check for
    // new data from the shell.

    fd = posix_openpt( O_RDWR | O_NOCTTY | O_NONBLOCK );

    // Some systems only allow a private devpts instance's ptmx

    if ( fd == -1 && errno == EACCES )
    {
        int first_errno = errno;
        struct stat st;

        m_logger.info( ) << "posix_openpt() failed: " << strerror( first_errno )
                         << " (euid " << geteuid( ) << ")";
        if ( stat( "/dev/ptmx", &st ) == 0 )
            m_logger.info( ) << ", /dev/ptmx mode " << std::oct << st.st_mode
                             << std::dec << " uid " << st.st_uid
                             << " gid " << st.st_gid;
        m_logger.info( ) << std::endl;

        fd = open( "/dev/pts/ptmx", O_RDWR | O_NOCTTY | O_NONBLOCK );

        if ( fd == -1 )
            m_logger.info( ) << "open( /dev/pts/ptmx ) failed too: "
                             << strerror( errno ) << std::endl;
        errno = fd == -1 ? first_errno : 0;
    }

    if ( fd == -1 )
    {
        if ( errno == EACCES )
        {
            m_logger.info( ) <<  "No pty available, using pipes (with a "
                             << "built-in line editor)" << std::endl;
            return -2;
        }

        m_logger.error( ) <<  "posix_openpt() failed: "
                          << strerror( errno ) << std::endl;
        return -1;
    }

    // According to TLPI calling grantpt() actually is not becessary on Linux,
    // but we do i anyway;-)

    if ( grantpt( fd ) != 0 )
    {
        m_logger.error( ) << "grantpt() failed: "
                          << strerror( errno ) << std::endl;
        close( fd );
        return -1;
    }

    // unlockpt() must be called before opening the slave

    if ( unlockpt( fd ) != 0 )
    {
        m_logger.error( ) << "unlockpt() failed: "
                          << strerror( errno ) << std::endl;
        close( fd );
        return -1;
    }

    // Get the file name for the slave part

    char *sn;
    if ( ! ( sn = ptsname( fd ) ) )
    {
        m_logger.error( ) << "ptsname() failed: "
                          << strerror( errno ) << std::endl;
        close( fd );
        return -1;
    }

    slave_name = sn;

    // The tty keeps its default settings (echo on, NL -> CR NL): the
    // terminal emulator expects a "real" terminal.

    return fd;
}


/******************************************                                     
 * Set up the slave pseudoterminal, redirect all standard file descriptors
 * to it, close all other files and finally replace the child by the shell.
 ******************************************/

void
Term::setup_child( std::string const & slave_name,
                   std::string const & shell )
{
    // Run child in a new session, making it the session leader

    if ( setsid( ) < 0 )
    {
        m_logger.error( ) << "setsid failed: "
                          << strerror( errno ) << std::endl;
        return;
    }

    // Open the slave

    int slave_fd = open( slave_name.c_str( ), O_RDWR );
    if ( slave_fd == -1 )
    {
        m_logger.error( ) << "open() for slave failed: "
                          << strerror( errno ) << std::endl;
        return;
    }

    // According to TLPI this is necessary on BSD to acquire a controlling
    // terminal

#ifdef TIOCSCTTY
    if ( ioctl( slave_fd, TIOCSCTTY, ( char * ) 0 ) == -1 )
    {
        close( slave_fd );
        m_logger.error( ) << "ioctl() with TIOCSCTTY failed: "
                          << strerror( errno ) << std::endl;
        return;
    }
#endif

    // Redirect the three standard file descritors to the slave

    if (   dup2( slave_fd, STDIN_FILENO  ) != STDIN_FILENO
        || dup2( slave_fd, STDOUT_FILENO ) != STDOUT_FILENO
        || dup2( slave_fd, STDERR_FILENO ) != STDERR_FILENO )
    {
        close( slave_fd );
        m_logger.error( ) << "dup2() failed: "
                          << strerror( errno ) << std::endl;
        return;
    }

    // Close all files above stderr

    for ( int i = STDERR_FILENO + 1; i < getdtablesize( ); i++ )
        close( i );

    // Tell programs what kind of terminal they are talking to

    setenv( "TERM", "xterm-256color", 1 );

    if ( ! access( "/mnt/ext1/system/share/pbterm/terminfo", R_OK ) )
        setenv( "TERMINFO", "/mnt/ext1/system/share/pbterm/terminfo", 1 );

    // Finally replace the process by the shell

    execlp( shell.c_str( ), shell.c_str( ), "-i", ( char * ) 0 );
}


/******************************************                                     
 * Methd invoked when opening a pseudoterminal failed due to missing
 * permissions to open the master file. In this case the communication
 * with the shell must be done via a set of pipes.
 ******************************************/

pid_t
Term::start_piped_shell( std::string const & shell )
{
    int fdes[ 2 ];

    // Create pipe set for sending data from the child to the parent

    if ( pipe( fdes ) < 0 )
    {
        m_logger.error( ) << "Failed to create pipe set: "
                          << strerror( errno ) << std::endl;
        return -1;
    }

    m_read_fd = fdes[ 0 ];
    int child_out = fdes[ 1 ];

    // Create another pipe set for sending data from the parent to the child

    if ( pipe( fdes ) < 0 )
    {
        m_logger.error( ) <<  "Failed to create second pipe set: "
                          << strerror( errno ) << std::endl;
        return -1;
    }

    int child_in = fdes[ 0 ];
    m_write_fd = fdes[ 1 ];

    pid_t child_pid;

    if ( ( child_pid = fork( ) ) < 0 )
    {
        m_logger.error( ) << "Can't fork: " << strerror( errno ) << std::endl;
        return -1;
    }
    else if ( child_pid == 0 )
    {
        // Run the child in its own session

        if ( setsid( ) < 0 )
        {
            m_logger.error( ) << "Failed to set process group: "
                              << strerror( errno ) << std::endl;
            return -1;
        }

        // Redirct childs standard channels to the pipes

        if (    dup2( child_in,  STDIN_FILENO  )  != STDIN_FILENO
             || dup2( child_out, STDOUT_FILENO  ) != STDOUT_FILENO
             || dup2( child_out, STDERR_FILENO  ) != STDERR_FILENO )
        {
            m_logger.error( ) << "dup2() failed: "
                              << strerror( errno ) << std::endl;
            exit( 1 );
        }

        // Close all other open files

        for ( int i = STDERR_FILENO + 1; i < getdtablesize( ); i++ )
            close( i );

        setenv( "TERM", "xterm", 1 );

        // No TIOCGWINSZ without a tty: programs like vi take the size from
        // these variables

        char num[ 16 ];
        snprintf( num, sizeof num, "%d", m_init_cols );
        setenv( "COLUMNS", num, 1 );
        snprintf( num, sizeof num, "%d", m_init_rows );
        setenv( "LINES", num, 1 );

        // Start the shell, replacing the child process

        execlp( shell.c_str( ), shell.c_str( ), "-i", NULL );
        exit( 1 );
    }

    // Close the child side pipe file handles

    close( child_in );
    close( child_out );

    // Set the non-blocking flag on the pipe end we're going to read from

    int flags = fcntl( m_read_fd, F_GETFL );

    if (    flags == -1
         || fcntl( m_read_fd, F_SETFL, flags | O_NONBLOCK ) == -1 )
    {
        m_logger.error( )  << "Failed to unblock read end of pipe: "
                           << strerror( errno ) << std::endl;
        return -1;
    }

    return child_pid;
}


/******************************************                                     
 * Tries to read as many data as are available from the shell
 ******************************************/

ssize_t
Term::get_shell_output( std::string & reply )
{
    static int const buffer_len = 1024;
    ssize_t cnt = 0,
            retval;

    // Keep reading until we get less than a full buffers worth of data

    do
    {
        char tmp_buffer[ buffer_len + 1 ];

        errno = 0;
        retval = read( m_read_fd, tmp_buffer, buffer_len );

        // Since the PTY is in non-blocking mode failure with EAGAIN is ok,
        // it just means that no data are available at the moment while a
        // zero return value means that the shell closed its output (and
        // that means its dead for all our purposes)

        if ( retval <= 0 )
            return errno == EAGAIN || errno == EWOULDBLOCK ? cnt : -1;

        reply.append( tmp_buffer, retval );
        trace( '<', tmp_buffer, retval );
        cnt   += retval;
    } while ( retval == buffer_len );

    return cnt;
}


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
