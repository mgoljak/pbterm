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


#if ! defined INKVIEW_HPP_
#define INKVIEW_HPP_


/******************************************
 * Include inkviw.h and define some stuff that's not in the include
 * file from the current SDK but seems to be implemented in the
 * versions of the libinkview distributed with newer devices (there
 * should be no harm if this additonal stuff isn't really available).
 ******************************************/

#include "inkview.h"


// SDK 6.x inkview.h already provides EVT_MTSYNC, iv_mtinfo, GetMenuRect(),
// GetTouchInfoI() and a complete ISPOINTEREVENT(). Only these typedefs are
// kept so Messenger.hpp keeps compiling unchanged.

typedef irect ( * GetMenuRect_t )( imenu const * );
typedef iv_mtinfo * ( * GetTouchInfo_t )( void );


// The T and TF macros in inkview.h are an abomination since their names are
// much too short not to clash with other stuff - e.g. many templates in C++
// use 'T'. Newer versions of inkview.h seem to have them commented out.

#if defined T
#undef T
#endif

#if defined TF
#undef TF
#endif


#endif


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
