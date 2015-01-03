/*****************************************************************************
 * DASHManager.cpp
 *****************************************************************************
 * Copyright © 2010 - 2011 Klagenfurt University
 *
 * Created on: Aug 10, 2010
 * Authors: Christopher Mueller <christopher.mueller@itec.uni-klu.ac.at>
 *          Christian Timmerer  <christian.timmerer@itec.uni-klu.ac.at>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#define __STDC_CONSTANT_MACROS

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "DASHManager.h"
#include "adaptationlogic/AdaptationLogicFactory.h"
#include "SegmentTracker.hpp"

using namespace dash;
using namespace dash::http;
using namespace dash::logic;
using namespace dash::mpd;

DASHManager::DASHManager    ( MPD *mpd,
                              AbstractAdaptationLogic::LogicType type, stream_t *stream) :
             conManager     ( NULL ),
             logicType      ( type ),
             mpd            ( mpd ),
             stream         ( stream )
{
    for(int i=0; i<Streams::count; i++)
        streams[i] = NULL;
}

DASHManager::~DASHManager   ()
{
    delete conManager;
    for(int i=0; i<Streams::count; i++)
        delete streams[i];
}

bool DASHManager::start(demux_t *demux)
{
    const Period *period = mpd->getFirstPeriod();
    if(!period)
        return false;

    for(int i=0; i<Streams::count; i++)
    {
        Streams::Type type = static_cast<Streams::Type>(i);
        const AdaptationSet *set = period->getAdaptationSet(type);
        if(set)
        {
            streams[type] = new (std::nothrow) Streams::Stream(set->getMimeType());
            if(!streams[type])
                continue;
            AbstractAdaptationLogic *logic = AdaptationLogicFactory::create(logicType, mpd);
            if(!logic)
            {
                delete streams[type];
                streams[type] = NULL;
                continue;
            }

            SegmentTracker *tracker = new (std::nothrow) SegmentTracker(logic, mpd);
            try
            {
                if(!tracker)
                    throw VLC_ENOMEM;
                streams[type]->create(demux, logic, tracker);
            } catch (int) {
                delete streams[type];
                delete logic;
                delete tracker;
                streams[type] = NULL;
            }
        }
    }

    conManager = new (std::nothrow) HTTPConnectionManager(stream);
    if(!conManager)
        return false;

    mpd->playbackStart.Set(time(NULL));

    return true;
}

size_t DASHManager::read()
{
    size_t i_ret = 0;
    for(int type=0; type<Streams::count; type++)
    {
        if(!streams[type])
            continue;
        i_ret += streams[type]->read(conManager);
    }
    return i_ret;
}

mtime_t DASHManager::getPCR() const
{
    mtime_t pcr = VLC_TS_INVALID;
    for(int type=0; type<Streams::count; type++)
    {
        if(!streams[type])
            continue;
        if(pcr == VLC_TS_INVALID || pcr > streams[type]->getPCR())
            pcr = streams[type]->getPCR();
    }
    return pcr;
}

int DASHManager::getGroup() const
{
    for(int type=0; type<Streams::count; type++)
    {
        if(!streams[type])
            continue;
        return streams[type]->getGroup();
    }
    return -1;
}

int DASHManager::esCount() const
{
    int es = 0;
    for(int type=0; type<Streams::count; type++)
    {
        if(!streams[type])
            continue;
        es += streams[type]->esCount();
    }
    return es;
}

mtime_t DASHManager::getDuration() const
{
    if (mpd->isLive())
        return 0;
    else
        return CLOCK_FREQ * mpd->getDuration();
}

bool DASHManager::setPosition(mtime_t time)
{
    bool ret = true;
    for(int real = 0; real < 2; real++)
    {
        /* Always probe if we can seek first */
        for(int type=0; type<Streams::count; type++)
        {
            if(!streams[type])
                continue;
            ret &= streams[type]->setPosition(time, !real);
        }
        if(!ret)
            break;
    }
    return ret;
}

bool DASHManager::seekAble() const
{
    if(mpd->isLive())
        return false;

    for(int type=0; type<Streams::count; type++)
    {
        if(!streams[type])
            continue;
        if(!streams[type]->seekAble())
            return false;
    }
    return true;
}
