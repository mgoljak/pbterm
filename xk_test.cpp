#include "pbterm-master/src/ExtraKeys.hpp"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main()
{
    ExtraKeys k( "ESC / - HOME UP END PGUP , TAB CTRL ALT LEFT DOWN RIGHT PGDN" );
    CHECK( k.rows() == 2 && k.cols(0) == 7 && k.cols(1) == 7 );
    CHECK( k.label(0,4) == "\xe2\x86\x91" );
    CHECK( k.press(0,0,false) == "\033" );                 // ESC
    CHECK( k.press(0,4,false) == "\033[A" );               // UP
    CHECK( k.press(0,4,true)  == "\033OA" );               // UP, app mode
    CHECK( k.press(0,3,false) == "\033[H" );               // HOME
    CHECK( k.press(0,6,false) == "\033[5~" );              // PGUP
    CHECK( k.press(1,0,false) == "\t" );                   // TAB
    CHECK( k.press(1,1,false) == "" && k.active(1,1) && k.armed() );   // CTRL on
    CHECK( k.press(0,4,false) == "\033[1;5A" );            // Ctrl+UP
    CHECK( ! k.armed() );                                  // used up
    CHECK( k.press(1,2,false) == "" );                     // ALT on
    CHECK( k.press(0,1,false) == "\033/" );                // Alt+/
    k.press(1,1,false);
    CHECK( k.apply_to_text("c") == "\003" );               // Ctrl+C from kbd
    CHECK( ! k.armed() );
    k.press(1,1,false); k.press(1,2,false);
    CHECK( k.apply_to_text("x") == "\033\030" );           // Ctrl+Alt+X
    k.press(1,1,false);
    CHECK( k.press(1,1,false) == "" && ! k.armed() );      // toggle off
    ExtraKeys j( "ESC | ~ , TAB" );
    CHECK( j.cols(0) == 3 && j.press(0,1,false) == "|" && j.press(0,2,false) == "~" );
    int r, c;
    CHECK( k.hit( 100, 940, 0, 896, 758, 128, r, c ) && r == 0 && c == 0 );
    CHECK( k.hit( 757, 1023, 0, 896, 758, 128, r, c ) && r == 1 && c == 6 );
    CHECK( ! k.hit( 100, 800, 0, 896, 758, 128, r, c ) );
    ExtraKeys e( "" );
    CHECK( e.rows() == 0 && ! e.hit( 1, 1, 0, 0, 10, 10, r, c ) );
    printf( fails ? "%d FAILED\n" : "all ok\n", fails );
    return fails != 0;
}
