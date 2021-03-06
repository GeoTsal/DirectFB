/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Shimokawa <andi@directfb.org>,
              Marek Pikarski <mass@directfb.org>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/



#include <config.h>

#include <direct/util.h>

#include <core/core.h>
#include <core/Renderer.h>

#include "init.h"


void DFBResult__init( void );
void DFBResult__deinit( void );

/**********************************************************************************************************************/

typedef void (*Func)( void );


static Func init_funcs[] = {
      DFBResult__init,
#if !DIRECTFB_BUILD_PURE_VOODOO
      Core_TLS__init,
      Renderer_TLS__init,
#endif
};

static Func deinit_funcs[] = {
#if !DIRECTFB_BUILD_PURE_VOODOO
      Renderer_TLS__deinit,
      Core_TLS__deinit,
#endif
      DFBResult__deinit,
};

/**********************************************************************************************************************/

void
__DFB_init_all()
{
     size_t i;

     for (i=0; i<D_ARRAY_SIZE(init_funcs); i++)
          init_funcs[i]();
}

void
__DFB_deinit_all()
{
     size_t i;

     for (i=0; i<D_ARRAY_SIZE(deinit_funcs); i++)
          deinit_funcs[i]();
}

