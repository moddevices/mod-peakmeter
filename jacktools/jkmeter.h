// ----------------------------------------------------------------------------
//
//  Copyright (C) 2008-2015 Fons Adriaensen <fons@linuxaudio.org>
//  Copyright (C) 2016 Filipe Coelho <falktx@falktx.com>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------


#ifndef __JKMETER_H
#define __JKMETER_H


#include "kmeterdsp.h"
#include "jclient.h"


class Jkmeter : public Jclient
{
public:

    Jkmeter (const char *client_name, int ninp, float *pks);
    virtual ~Jkmeter (void);

    enum { INITIAL, PASSIVE, SILENCE, PROCESS, FAILED = -1, ZOMBIE = -2, MAXINP = 64 };

    int get_levels (void);

private:

    void jack_shutdown (void);
    int  jack_process (int nfram);

    int              _state;
    Kmeterdsp       *_kproc;
    float           *_pks;
};


#endif
