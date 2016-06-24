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


#include <string.h>
#include <errno.h>
#include "jclient.h"


Jclient::Jclient (jack_client_t* client) :
    _client (client),
    _inp_ports (0),
    _out_ports (0)
{
    cleanup ();
}


Jclient::~Jclient (void)
{
    cleanup ();
    close_jack ();
}


void Jclient::cleanup (void)
{
    _jack_name = 0;
    _jack_rate = 0;
    _jack_size = 0;
    _max_inps = 0;
    _max_outs = 0;
    delete[] _inp_ports;
    delete[] _out_ports;
    _inp_ports = 0;
    _out_ports = 0;
}


int Jclient::open_jack (int max_inps, int max_outs)
{
    struct sched_param sched_par;

    jack_set_process_callback (_client, jack_static_process, (void *) this);
    jack_on_shutdown (_client, jack_static_shutdown, (void *) this);
    jack_activate (_client);

    _jack_name = jack_get_client_name (_client);
    _jack_rate = jack_get_sample_rate (_client);
    _jack_size = jack_get_buffer_size (_client);

    pthread_getschedparam (jack_client_thread_id (_client), &_schedpol, &sched_par);
    _priority = sched_par.sched_priority;

    _max_inps = max_inps;
    if (max_inps)
    {
        _inp_ports = new jack_port_t * [max_inps];
        memset (_inp_ports, 0, max_inps * sizeof (jack_port_t *));
    }
    _max_outs = max_outs;
    if (max_outs)
    {
        _out_ports = new jack_port_t * [max_outs];
        memset (_out_ports, 0, max_outs * sizeof (jack_port_t *));
    }

    return 0;
}


int Jclient::close_jack (void)
{
    jack_deactivate (_client);
    cleanup ();
    return 0;
}


void Jclient::jack_static_shutdown (void *arg)
{
    ((Jclient *) arg)->jack_shutdown ();
}


int Jclient::jack_static_process (jack_nframes_t nframes, void *arg)
{
    return ((Jclient *) arg)->jack_process (nframes);
}


int Jclient::create_inp_port (int i, const char *name)
{
    if ((i < 0) || (i >= _max_inps) || _inp_ports [i]) return -1;
    _inp_ports [i] = jack_port_register (_client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    return _inp_ports [i] ? 0 : 1;
}


int Jclient::create_out_port (int i, const char *name)
{
    if ((i < 0) || (i >= _max_outs) || _out_ports [i]) return -1;
    _out_ports [i] = jack_port_register (_client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    return _out_ports [i] ? 0 : 1;
}


int Jclient::delete_inp_port (int i)
{
    if ((i < -1) || (i >= _max_inps)) return -1;
    if (i == -1)
    {
        for (i = 0; i < _max_inps; i++)
        {
            if (_inp_ports [i])
            {
                jack_port_unregister (_client, _inp_ports [i]);
                _inp_ports [i] = 0;
            }
        }
    }
    else
    {
        if (!_inp_ports [i]) return -1;
        jack_port_unregister (_client, _inp_ports [i]);
        _inp_ports [i] = 0;
    }
    return 0;
}


int Jclient::delete_out_port (int i)
{
    if ((i < -1) || (i >= _max_outs)) return -1;
    if (i == -1)
    {
        for (i = 0; i < _max_outs; i++)
        {
            if (_out_ports [i])
            {
                jack_port_unregister (_client, _out_ports [i]);
                _out_ports [i] = 0;
            }
        }
    }
    else
    {
        if (!_out_ports [i]) return -1;
        jack_port_unregister (_client, _out_ports [i]);
        _out_ports [i] = 0;
    }
    return 0;
}


int Jclient::connect_inp_port (int i, const char *srce)
{
    int rv;

    if ((i < 0) || (i >= _max_inps) || !_inp_ports [i]) return -1;
    rv = jack_connect (_client, srce, jack_port_name (_inp_ports [i]));
    if (rv == EEXIST) rv = 0;
    return rv;
}


int Jclient::connect_out_port (int i, const char *dest)
{
    int rv;

    if ((i < 0) || (i >= _max_outs) || !_out_ports [i]) return -1;
    rv = jack_connect (_client, jack_port_name (_out_ports [i]), dest);
    if (rv == EEXIST) rv = 0;
    return rv;
}


int Jclient::disconn_inp_port (int i, const char *srce)
{
    if ((i < -1) || (i >= _max_inps)) return -1;
    if (i == -1)
    {
        for (i = 0; i < _max_inps; i++)
        {
            if (_inp_ports [i]) jack_port_disconnect (_client, _inp_ports [i]);
        }
    }
    else
    {
        if (!_inp_ports [i]) return -1;
        if (srce) jack_disconnect (_client, srce, jack_port_name (_inp_ports [i]));
        else jack_port_disconnect (_client, _inp_ports [i]);
    }
    return 0;
}


int Jclient::disconn_out_port (int i, const char *dest)
{
    if ((i < -1) || (i >= _max_outs)) return -1;
    if (i == -1)
    {
        for (i = 0; i < _max_outs; i++)
        {
            if (_out_ports [i]) jack_port_disconnect (_client, _out_ports [i]);
        }
    }
    else
    {
        if (!_out_ports [i]) return -1;
        if (dest) jack_disconnect (_client, jack_port_name (_out_ports [i]), dest);
        else jack_port_disconnect (_client, _out_ports [i]);
    }
    return 0;
}
