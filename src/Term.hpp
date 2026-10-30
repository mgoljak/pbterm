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


#if ! defined TERM_HPP_
#define TERM_HPP_


#include <string>
#include <vector>


class Messenger;
class Config;
class Logger;


/******************************************
 * Class that takes care of starting the shell, sending it
 * commands and reading the replies
 ******************************************/

class Term
{
  public :

    Term( Messenger & mess,
          Config    & config,
          int         cols,
          int         rows );


    ~Term( );


    bool
    using_pty( );


    void
    send_command( std::string const & cmd );


    void
    send_control( char crtl );


    // Keys typed by the user (on-screen keyboard, paste, extra keys). With
    // a pty they are passed on as they are. Without one (pipes) there's no
    // tty driver to echo, edit lines or kill jobs, so this does that: local
    // echo, backspace, history (up/down), TAB completion of file and
    // command names, ^C (SIGINT for the shell's process group), and the
    // line is sent when ENTER is pressed.

    void
    input( std::string const & data );


    // Sends raw bytes (e.g. cursor position reports from the emulator)

    void
    send_raw( std::string const & data );


    // Tells the kernel the size of the terminal (SIGWINCH is sent to the
    // foreground process group)

    void
    set_winsize( int cols,
                 int rows );


    std::vector< std::string > const &
    command_list( ) const;


    std::string
    last_command( ) const;


  private :

    static void
    static_timer_handler( );


    void
    timer_handler( );


    bool
    send( char const * data,
          int          len );


    void
    read_cmd_file( );


    void
    save_command( std::string const & cmd );


    void
    write_cmd_file( );


    pid_t
    start_shell( std::string const & shell );


    pid_t
    start_piped_shell( std::string const & shell );


    int
    open_master_pty( std::string & slave_name );


    void
    setup_child( std::string const & slave_name,
                 std::string const & shell );


    ssize_t
    get_shell_output( std::string & reply );


    // Messenger object we have to notify about new shell output

    Messenger & m_mess;


    // Initial terminal size

    int m_init_cols,
        m_init_rows;


    // Time (in ms) between checks for shell output

    int m_check_interval;


    // While > 0 the shell is polled much more often than 'm_check_interval'
    // (after sending something to it or after output arrived), so replies
    // to terminal queries like "ESC [ 6 n" arrive in time for e.g. vi

    int m_fast_ticks;


    // Number of bytes still to be written to the log by trace()

    int m_trace_left;


    // Shell process, and the state of the line editor used with pipes

    pid_t m_shell_pid;

    // The start-up complaint of ash about the missing tty is removed once

    bool m_hide_tty_msg;

    std::string m_line,
                m_saved_line,
                m_esc;

    std::size_t m_hist_pos;

    // Cursor position in m_line, measured in UTF-8 bytes.
    std::size_t m_line_pos;

    void
    line_move( int dir );

    void
    echo( std::string const & text );

    void
    line_submit( );

    void
    line_backspace( );

    void
    line_replace( std::string const & new_line );

    void
    line_history( int dir );

    void
    line_complete( );

    void
    line_insert( std::string const & text );


    // Writes the bytes exchanged with the shell to the log in readable
    // form ("<" from shell, ">" to shell), limited to a few KB per run

    void
    trace( char         dir,
           char const * data,
           int          len );


    // File descriptors for reading and writing to the child running the shell.
    // If a pseudoterminal can be used they are the same.

    int m_write_fd,
        m_read_fd;


    // List of all commands already send to the shell

    std::vector< std::string > m_commands;


    // Object for logging

    Logger & m_logger;


    // Maximum number of commands in history

    std::size_t m_max_history;


    // Name of file to read histiry from and write it to

    std::string m_cmd_file;


    // Callback handling terminal

    static Term * s_handling_term;
};

#endif


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
