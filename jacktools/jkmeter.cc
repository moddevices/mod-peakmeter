// ----------------------------------------------------------------------------
//
//  Copyright (C) 2008-2015 Fons Adriaensen <fons@linuxaudio.org>
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


#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "jkmeter.h"


Jkmeter::Jkmeter (const char *client_name, const char *server_name, int nchan, float *rms, float *pks) :
    _state (INITIAL),
    _rms (rms),
    _pks (pks)
{
    int   i;
    char  s [16];

    if (open_jack (client_name, server_name, nchan, 0)) return;
    Kmeterdsp::init (_jack_rate, _jack_size, 0.5f, 15.0f);
    _kproc = new Kmeterdsp [nchan];
    for (i = 0; i < nchan; i++)
    {
	sprintf (s, "in_%d", i + 1);
	create_inp_port (i, s);
    }
    _state = PROCESS;
}


Jkmeter::~Jkmeter (void)
{
    _state = INITIAL;
    usleep (100000);
    close_jack ();
    delete[] _kproc;
}


void Jkmeter::jack_shutdown (void)
{
    _state = ZOMBIE;
}


int Jkmeter::jack_process (int nframes)
{
    int    i, n = _max_inps;
    float  *p;

    if (_state != PROCESS) return 0;
    for (i = 0; i < n; i++)
    {
        p = (float *) jack_port_get_buffer (_inp_ports [i], nframes);
	_kproc [i].process (p, nframes);
    }
    return 0;
}


int Jkmeter::get_levels (void)
{
    int  i, n = _max_inps;

    for (i = 0; i < n; i++) _kproc [i].read (_rms + i, _pks + i);
    return _state;
}


