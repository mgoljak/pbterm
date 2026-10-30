/*
 *  Copyright (C) 2013 Jens Thoms Toerring <jt@toerring.de.de>
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


#include "Messenger.hpp"
#include "Logger.hpp"
#include "Config.hpp"
#include "Display.hpp"
#include "Term.hpp"
#include "Keyboard_Handler.hpp"
#include "Menu_Handler.hpp"
#include "Button_Handler.hpp"
#include "Pointer_Handler.hpp"
#include "Rotation_Handler.hpp"
#include <dlfcn.h>


/******************************************
 * Constructor, creates all the subsystems needed
 ******************************************/

Messenger::Messenger( )
    : m_logger( 0 )
    , m_display( 0 )
    , m_term( 0 )
    , m_kbd_handler( 0 )
    , m_menu_handler( 0 )
    , m_button_handler( 0 )
    , m_pointer_handler( 0 )
    , m_is_shutting_down( false )
    , m_is_recording( false )
    , m_inkview_handle( 0 )
    , m_GetMenuRect( 0 )
    , m_GetTouchInfo( 0 )
{
    // Newer versions of the libinkview library have several functions not
    // supported by the current SDK. Try to load them anyway.

    load_unsupported_functions( );

    // Create the object for logging messages

    m_logger = new Logger( );

    // Read in and parse the configuration file

    Config config( *m_logger );

    m_font_step = config.font_step( );

    // Initialize the display subsytem

    m_display = new Display( *this, config );

    if ( m_is_shutting_down )
        return;

    // Prepare the terminal dealing with the shell

    m_term = new Term( *this, config, m_display->cols( ), m_display->rows( ) );

    if ( m_is_shutting_down )
        return;

    m_display->set_pty_mode( m_term->using_pty( ) );

    // Create subsystems for handling button and pointer events and for
    // dealing with the on-screen keyboard, menu and orientation selector.

    m_button_handler  = new Button_Handler( *this, config );
    m_pointer_handler = new Pointer_Handler( *this, config );
    m_pointer_handler->set_bar_top( m_display->bar_top( ) );

    m_kbd_handler      = new Keyboard_Handler( *this, config );
    m_menu_handler     = new Menu_Handler( *this, config );
    m_rotation_handler = new Rotation_Handler( *this, config );
}


/******************************************
 * Destructor - get rid of the subsystem objects
 ******************************************/

Messenger::~Messenger( )
{
    delete m_rotation_handler;
    delete m_pointer_handler;
    delete m_button_handler;
    delete m_menu_handler;
    delete m_kbd_handler;
    delete m_term;
    delete m_display;
    delete m_logger;

    if ( m_inkview_handle )
        dlclose( m_inkview_handle );
}
    

/******************************************
 * Receives the "Close" message, asking libinkview to shut down the program
 * (which then will send at least an EVT_EXIT event)
 ******************************************/

template < >
void
Messenger::send< message::Close >( message::Close const & )
{
    m_is_shutting_down = true;
    CloseApp( );
}


/******************************************
 * Receives the "Text" message, asking for new text to be put on the screen
 ******************************************/

template < >
void
Messenger::send< message::New_Text >( message::New_Text const & mess )
{
    if ( m_is_recording )
        *m_logger << mess.text;

    m_display->add_text( mess.text );

    // Answer queries (cursor position etc.) the emulator received

    // Answers go to the shell only when it has a tty (otherwise they'd show
    // up as keys typed)

    std::string reply( m_display->take_reply( ) );

    if ( m_term && m_term->using_pty( ) )
        m_term->send_raw( reply );
}


/******************************************
 * Receives the "Command" messages, sent when the user entered a new command
 * Sends it to the shell, displays on on the screen and logs it if appropriate.
 ******************************************/

template < >
void
Messenger::send< message::New_Command >( message::New_Command const & mess )
{
    m_display->scroll_to_bottom( );

    // CTRL or ALT was armed on the extra keys: the text typed is not a
    // command but the key to be modified (e.g. "c" gives ^C), no Enter

    if ( m_display->extra_keys_armed( ) )
    {
        std::string t( mess.cmd );
        if ( ! t.empty( ) && t[ t.size( ) - 1 ] == '\n' )
            t.erase( t.size( ) - 1 );
        m_term->send_raw( m_display->apply_modifiers( t ) );
        return;
    }

    m_term->send_command( mess.cmd );

    // With a pty the tty driver echoes the command, with pipes we must do it

    if ( ! m_term->using_pty( ) )
    {
        m_display->add_text( mess.cmd );
        if ( m_is_recording )
            *m_logger << mess.cmd;
    }
}


/******************************************
 * Receives the "Extra Key Tap" message, sent when one of the extra keys
 * at the bottom of the screen was tapped
 ******************************************/

template < >
void
Messenger::send< message::Extra_Key_Tap >(
                                  message::Extra_Key_Tap const & mess )
{
    std::string bytes( m_display->extra_key_tap( mess.x, mess.y ) );

    // Showing, hiding or changing the keyboard changes the terminal size.
    if ( m_display->take_geometry_changed( ) )
    {
        m_term->set_winsize( m_display->cols( ), m_display->rows( ) );
        m_pointer_handler->set_bar_top( m_display->bar_top( ) );
    }

    // With pipes, use Page Up/Down to browse terminal output locally.
    if ( ! m_term->using_pty( ) )
    {
        if ( bytes == "\033[5~" )       // PGUP
        {
            m_display->scroll_page( -1 );
            return;
        }

        if ( bytes == "\033[6~" )       // PGDN
        {
            m_display->scroll_page( +1 );
            return;
        }
    }

    if ( ! bytes.empty( ) )
    {
        m_display->scroll_to_bottom( );
        m_term->input( bytes );
    }
}


/******************************************
 * Receives the "New Ctrl_Char" message, sent when the user asked for a
 * control char being sent to the shell
 ******************************************/

template < >
void
Messenger::send< message::New_Ctrl_Char >( message::New_Ctrl_Char const & mess )
{
    if (    ! mess.ctrl
         || mess.ctrl[ 0 ] != '^'
         || mess.ctrl[ 1 ] < 'A'
         || mess.ctrl[ 1 ] > '[' )
        return;

    // Without a pty ^C and friends are handled by the line editor

    if ( m_term->using_pty( ) )
        m_term->send_control( mess.ctrl[ 1 ] - 'A' + 1 );
    else
        m_term->input( std::string( 1, static_cast< char >( mess.ctrl[ 1 ] - 'A' + 1 ) ) );
}


/******************************************
 * Receives the "Shift Display" message, sent when the user asked for the
 * display to be scrolled up or down
 ******************************************/

template < >
void
Messenger::send< message::Shift_Display >( message::Shift_Display const & mess )
{
    m_display->shift( mess.dist );
}


/******************************************
 * Receives the "Togge Recording" message, sent when the user asked to switch
 * recording on or off
 ******************************************/

template < >
void
Messenger::send< message::Toggle_Recording >(
                                            message::Toggle_Recording const & )
{
    // Also the display subsystem needs to know since it draws some indicator
    // while recording is on

    if ( m_logger->can_log( ) )
        m_display->recording_state_change( m_is_recording = ! m_is_recording );
}


/******************************************
 * Received the "Change Font" message when the user asks for an
 * increased or decreased font size
 ******************************************/

template < >
void
Messenger::send< message::Change_Font_Size >(
                                       message::Change_Font_Size const & mess )
{
    m_display->change_font_size( mess.difference * m_font_step );
    m_term->set_winsize( m_display->cols( ), m_display->rows( ) );
}


/******************************************
 * Receives the "Suspend Display" message to suspend or re-enable display
 * updates, issued when e.g. while the menu or keyboard is shown to keep
 * it from being overdrawn by new data coming from the shell
 ******************************************/

template < >
void
Messenger::send< message::Suspend_Display >(
                                        message::Suspend_Display const & mess )
{
    m_display->suspend_redraws( mess.stop );
}


/******************************************
 * Receives the "Set Display Orientation", requesting a change of the
 * display orientation. The pointer and button handling subsystem need
 * to be made aware of the resulting changes.
 ******************************************/

template < >
void
Messenger::send< message::Set_Orientation >(
                                        message::Set_Orientation const & mess )
{
    m_logger->info( ) << "Set_Orientation dir " << mess.dir << std::endl;
    m_display->rotate( mess.dir );
    m_term->set_winsize( m_display->cols( ), m_display->rows( ) );
    m_pointer_handler->display_size_change( ScreenWidth( ),
                                            ScreenHeight( ) );
    m_pointer_handler->set_bar_top( m_display->bar_top( ) );
    m_button_handler->orientation_change( GetOrientation( ) );
}


/******************************************
 * Receives the "Show Rotate Box" message, resulting in the rotation selector
 * being shown (which, in turn, will result in a "Set Orientation" message).
 ******************************************/

template < >
void
Messenger::send< message::Show_Rotate_Box >( message::Show_Rotate_Box const & )
{
    int next = ( GetOrientation( ) + 1 ) % 4;

    m_logger->info( ) << "Rotate: " << GetOrientation( )
                      << " -> " << next << std::endl;
    send( message::Set_Orientation( next ) );
}


/******************************************
 * Receives the "Show Keyboard" message when the keyboard is to be displayed
 * on the screen (may, in turn, result in a "Command" message).
 ******************************************/

template < >
void
Messenger::send< message::Show_Keyboard >( message::Show_Keyboard const & mess )
{
    // In "live" mode the keyboard starts empty (the text is put on the
    // shell's command line, which has its own history), a text handed
    // in explicitly (from the menu) replaces the current command line

    if ( mess.txt.empty( ) )
        m_kbd_handler->show( m_kbd_handler->live( ) ? std::string( )
                                           : m_term->last_command( ) );
    else
        m_kbd_handler->show( mess.txt, m_kbd_handler->live( ) );
}


/******************************************
 * Receives text typed on the keyboard that goes to the command line
 ******************************************/

template < >
void
Messenger::send< message::Raw_Input >( message::Raw_Input const & mess )
{
    m_display->scroll_to_bottom( );

    if ( m_display->extra_keys_armed( ) )
        m_term->input( m_display->apply_modifiers( mess.text ) );
    else
        m_term->input( ( mess.replace ? "\x15" : "" ) + mess.text );
}


/******************************************
 * Selecting text, copying and pasting
 ******************************************/

/******************************************
 * A tap on the terminal: brings up the on-screen keyboard if it's hidden,
 * or the system's keyboard box if on-screen keyboard is switched off
 ******************************************/

template < >
void
Messenger::send< message::Screen_Tap >( message::Screen_Tap const & )
{
    if ( ! m_display->onscreen_keyboard( ) )
    {
        send( message::Show_Keyboard( ) );
        return;
    }

    if ( m_display->show_screen_keyboard( ) )
    {
        m_term->set_winsize( m_display->cols( ), m_display->rows( ) );
        m_pointer_handler->set_bar_top( m_display->bar_top( ) );
    }
}


template < >
void
Messenger::send< message::Select_Start >( message::Select_Start const & )
{
    m_display->start_selection( );
    m_pointer_handler->set_selecting( true );
}


template < >
void
Messenger::send< message::Select_Cancel >( message::Select_Cancel const & )
{
    m_display->cancel_selection( );
    m_pointer_handler->set_selecting( false );
}


template < >
void
Messenger::send< message::Select_Tap >( message::Select_Tap const & mess )
{
    // Selection was cancelled in the meantime (e.g. by new output): treat
    // the tap as a normal one

    if ( ! m_display->selecting( ) )
    {
        m_pointer_handler->set_selecting( false );
        send( message::Screen_Tap( ) );
        return;
    }

    std::string text;

    if ( m_display->select_tap( mess.x, mess.y, text ) )
    {
        if ( ! text.empty( ) )
            m_clipboard = text;
        m_pointer_handler->set_selecting( false );
    }
}


template < >
void
Messenger::send< message::Select_Drag >( message::Select_Drag const & mess )
{
    if ( ! m_display->selecting( ) )
    {
        m_pointer_handler->set_selecting( false );
        return;
    }

    std::string text;

    if ( m_display->select_range( mess.x1, mess.y1, mess.x2, mess.y2, text ) )
    {
        if ( ! text.empty( ) )
            m_clipboard = text;
        m_pointer_handler->set_selecting( false );
    }
}


template < >
void
Messenger::send< message::Paste >( message::Paste const & )
{
    m_display->scroll_to_bottom( );
    m_term->input( m_clipboard );
}


/******************************************
 * Receives the "Show Menu" message when the menu is to be displayed
 * on the screen (may result in a number of other messages)
 ******************************************/

template < >
void
Messenger::send< message::Show_Menu >( message::Show_Menu const & mess )
{
    m_menu_handler->show( mess.x, mess.y );
}


/******************************************
 * Receives the "Use Custom Keyboard" message when the user wants to switch
 * use of the custom keyboard on or off
 ******************************************/

template < >
void
Messenger::send< message::Use_Custom_Keyboard >(
                                    message::Use_Custom_Keyboard const & mess )
{
    m_kbd_handler->use_custom_keyboard( mess.yes_no );
}


/******************************************
 * Receives the "Handle Event" request sent for libinkview events.
 ******************************************/

template < >
request::Handle_Event &
Messenger::send< request::Handle_Event >( request::Handle_Event & req )
{
    if ( m_is_shutting_down )
        return req;

    if ( req.type == EVT_SHOW )
    {
        m_display->redraw( );
        req.result = 1;
    }
    else if ( req.type == EVT_ORIENTATION )
    {
        m_logger->info( ) << "EVT_ORIENTATION " << req.par1 << std::endl;
        send( message::Set_Orientation( req.par1 ) );
        req.result = 1;
    }
    else if ( ISKEYEVENT( req.type ) )
        req.result = m_button_handler->handle( req.type, req.par1, req.par2 );
    else if ( ISPOINTEREVENT( req.type ) )
        req.result = m_pointer_handler->handle( req.type, req.par1, req.par2 );
    else
        m_logger->info( ) << "event " << req.type << " " << req.par1
                          << " " << req.par2 << std::endl;
    return req;
}

/******************************************
 * Receives the "Get Command List" request to obtain a list of all
 * commands in the history
 ******************************************/

template < >
request::Get_Command_List &
Messenger::send< request::Get_Command_List >( request::Get_Command_List & req )
{
    req.result = &m_term->command_list( );
    return req;
}


/******************************************
 * Receives the "Can_Use_Crtl" request to inquire if control characters
 * can be sent to the shell
 ******************************************/

template < >
request::Can_Use_Ctrl &
Messenger::send< request::Can_Use_Ctrl >( request::Can_Use_Ctrl & req )
{
    req.result = m_term->using_pty( );
    return req;
}


/******************************************
 * Receives the "Get Active Keyboard" request to obtain the type of the
 * active keyboard
 ******************************************/

template < >
request::Get_Active_Keyboard &
Messenger::send< request::Get_Active_Keyboard >(
                                            request::Get_Active_Keyboard & req )
{
    req.result = m_kbd_handler->active_keyboard( );
    return req;
}


/******************************************
 * If available in libinkview call GetMenuRect() to find out how
 * large a menu will be. Return if calling GetMenuRect() was possible.
 ******************************************/

bool
Messenger::GetMenuRect( imenu * menu,
                        irect & rect )
{
    rect = ::GetMenuRect( menu );
    return true;
}


/******************************************
 * If available in libinkview call GetTouchInfo() to find out were the two
 * fingers are at the moment. Return if calling GetTouchInfo() was possible.
 ******************************************/

bool
Messenger::GetTouchInfo( iv_mtinfo mtinfo[ 2 ] )
{
    iv_mtinfo * a = GetTouchInfoI( 0 ),
              * b = GetTouchInfoI( 1 );

    if ( ! a || ! b )
        return false;

    mtinfo[ 0 ] = *a;
    mtinfo[ 1 ] = *b;
    return true;
}


/******************************************
 * A number of functions actually available in newer libinkview versions
 * aren't suppported by the current SDK. So we need to use sone tricks to
 * get at them anyway, i.e. via dlopen() and dlsym().
 ******************************************/

void
Messenger::load_unsupported_functions( )
{
    // Nothing to do anymore: SDK 6.x declares these functions directly
}


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
