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


#if ! defined MESSAGE_HPP_
#define MESSAGE_HPP_


/******************************************
 * Declaration of all messages that can be sent by all components to
 * the Messenger class. Since messages don't have a return value
 * the Messenger class only expects const-qualified references for
 * messages, which differentiates them from requests that must be
 * modifiable.
 ******************************************/


#include <string>
#include <vector>
#include "Inkview.hpp"


struct message
{
    // Message sent to shut down the application (be aware that this will
    // only happen quite some time later when the lininkview event loop
    // gets around to send a EVT_EXIT event!)

    struct Close { };


    // Message sent when new text to be shown has become available

    struct New_Text
    {
        New_Text( std::string const & str )
            : text( str )
        { }

        std::string const & text;
    };


    // Messages for selecting text on the screen, copying and pasting it

    struct Select_Start  { };

    struct Screen_Tap    { };

    struct Select_Cancel { };

    struct Paste         { };

    struct Select_Drag
    {
        Select_Drag( int x1_, int y1_, int x2_, int y2_ )
            : x1( x1_ ), y1( y1_ ), x2( x2_ ), y2( y2_ )
        { }

        int x1, y1, x2, y2;
    };

    struct Select_Tap
    {
        Select_Tap( int x_, int y_ )
            : x( x_ ), y( y_ )
        { }

        int x, y;
    };


    // Text typed on the on-screen keyboard that is to be put on the shell's
    // command line without pressing <ENTER>. If 'replace' is set the
    // line is cleared first (^U).

    struct Raw_Input
    {
        Raw_Input( std::string const & text_, bool replace_ )
            : text( text_ ), replace( replace_ )
        { }

        std::string const & text;
        bool                replace;
    };


    // Message sent to request a vertical shift of the display

    struct Shift_Display
    {
        Shift_Display( int dist )
            : dist( dist )
        { }

        int dist;
    };


    // Message sent to request that the box for selecting an orientation is
    // shown

    struct Show_Rotate_Box { };


    // Message sent to request a change of the display orientation

    struct Set_Orientation
    {
        Set_Orientation( int dir )
            : dir( dir )
        { }

        int dir;
    };


    // Message sent to request a change of the font size

    struct Change_Font_Size
    {
        Change_Font_Size( int difference )
            : difference( difference )
        { }

        int difference;
    };


    // Message sent to request that the display suspends or restarts redraws

    struct Suspend_Display
    {
        Suspend_Display( bool stop )
            : stop( stop )
        { }

        bool stop;
    };


    // Message sent when the user entered a command

    struct New_Command
    {
        New_Command( std::string const & cmd )
            : cmd( cmd )
        { }

        std::string const & cmd;
    };


    // Message sent when the user requests that a control character is sent

    struct New_Ctrl_Char
    {
        New_Ctrl_Char( char const * ctrl )
            : ctrl( ctrl )
        { }

        char const * ctrl;
    };


    // Message sent when the user requests to switch recording on or off

    struct Toggle_Recording { };


    // Message sent when a tap happened in the extra keys area

    struct Extra_Key_Tap
    {
        Extra_Key_Tap( int x_, int y_ )
            : x( x_ ), y( y_ )
        { }

        int x, y;
    };


    // Message sent when the keyboard is to be shown

    struct Show_Keyboard
    {
        Show_Keyboard( std::string const & txt = std::string( "" ) )
            : txt( txt )
        { }

        std::string const & txt;
    };


    // Message sent when the keyboard is to be shown

    struct Show_Menu
    {
        Show_Menu( int x = ScreenWidth(  )  / 2,
                   int y = ScreenHeight( )  / 2 )
            : x( x )
              , y( y )
        { }

        int x,
            y;
    };


    // Message sent to switch use of custom keyboard on or off

    struct Use_Custom_Keyboard
    {
        Use_Custom_Keyboard( bool yes_no )
            : yes_no( yes_no )
        { }

        bool  yes_no;
    };
};


#endif


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
