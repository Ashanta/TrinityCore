/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRANSPORTMGR_H
#define TRANSPORTMGR_H

#include <ace/Singleton.h>
#include "Spline.h"

struct KeyFrame;
struct GameObjectInfo;
struct TransportTemplate;
class Transport;

typedef std::vector<KeyFrame>                    KeyFrameVec;
typedef UNORDERED_MAP<uint32, TransportTemplate> TransportTemplates;
typedef std::set<Transport*>                     TransportSet;
typedef UNORDERED_MAP<uint32, TransportSet>      TransportMap;
typedef UNORDERED_MAP<uint32, std::set<uint32> > TransportInstanceMap;

struct KeyFrame
{
    explicit KeyFrame(TaxiPathNodeEntry const& _node) : Node(&_node),
        DistSinceStop(-1.0f), DistUntilStop(-1.0f), DistFromPrev(-1.0f), TimeFrom(0.0f), TimeTo(0.0f),
        Teleport(false), PathTime(0), DepartureTime(0), Spline(NULL)
    {
    }

    uint32 Index;
    TaxiPathNodeEntry const* Node;
    float DistSinceStop;
    float DistUntilStop;
    float DistFromPrev;
    float TimeFrom;
    float TimeTo;
    bool Teleport;
    uint32 PathTime;
    uint32 DepartureTime;
    Movement::Spline<double>* Spline;

    bool IsTeleportFrame() const { return Teleport; }
    bool IsStopFrame() const { return Node->actionFlag == 2; }
};

struct TransportTemplate
{
    TransportTemplate() : pathTime(0), accelTime(0.0f), accelDist(0.0f) { }
    std::set<uint32> mapsUsed;
    uint32 pathTime;
    KeyFrameVec keyFrames;
    float accelTime;
    float accelDist;
    uint32 entry;
};

class TransportMgr
{
        friend class ACE_Singleton<TransportMgr, ACE_Thread_Mutex>;

    public:
        void Unload();

        void LoadTransportTemplates();

        // Creates a transport using given GameObject template entry
        Transport* CreateTransport(uint32 entry, Map* map = NULL);

        // Spawns all continent transports, used at core startup
        void SpawnContinentTransports();

        // creates all transports for instance
        void CreateInstanceTransports(Map* map);

        TransportTemplate const* GetTransportTemplate(uint32 entry) const
        {
            TransportTemplates::const_iterator itr = _transportTemplates.find(entry);
            if (itr != _transportTemplates.end())
                return &itr->second;
            return NULL;
        }

    private:
        TransportMgr();
        ~TransportMgr();
        TransportMgr(TransportMgr const&);
        TransportMgr& operator=(TransportMgr const&);

        // Generates and precaches a path for transport to avoid generation each time transport instance is created
        void GeneratePath(GameObjectTemplate const* goInfo, TransportTemplate* transport);

        // Container storing transport templates
        TransportTemplates _transportTemplates;

        // Container storing transport entries to create for instanced maps
        TransportInstanceMap _instanceTransports;
};

#define sTransportMgr ACE_Singleton<TransportMgr, ACE_Thread_Mutex>::instance()

#endif // TRANSPORTMGR_H