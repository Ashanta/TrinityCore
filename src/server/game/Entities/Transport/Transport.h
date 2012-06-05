/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#ifndef TRANSPORTS_H
#define TRANSPORTS_H

#include "GameObject.h"
#include "TransportMgr.h"

#include <map>
#include <set>
#include <string>

class Transport : public GameObject
{
        friend Transport* TransportMgr::CreateTransport(uint32, Map*);

        Transport();
    public:
        ~Transport();

        bool Create(uint32 guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress, uint32 dynflags);
        void Update(uint32 diff);

        void AddPassenger(WorldObject* passenger);
        void RemovePassenger(WorldObject* passenger);
        std::set<WorldObject*> const& GetPassengers() const { return _passengers; }

        Creature* CreateNPCPassenger(uint32 guid, uint32 entry, float x, float y, float z, float o, CreatureData const* data = NULL);
        GameObject* CreateGOPassenger(uint32 guid, uint32 entry, float x, float y, float z, float o, GameObjectData const* data = NULL);
        void CalculatePassengerPosition(float& x, float& y, float& z, float& o);
        void CalculatePassengerOffset(float& x, float& y, float& z, float& o);

        uint32 GetPeriod() const { return GetUInt32Value(GAMEOBJECT_LEVEL); }
        void SetPeriod(uint32 period) { SetUInt32Value(GAMEOBJECT_LEVEL, period); }
        uint32 GetTimer() const { return _moveTimer; }

        KeyFrameVec const& GetKeyFrames() const { return _transportInfo->keyFrames; }
    private:
        void MoveToNextWayPoint();
        float CalculateSegmentPos(float perc);
        void TeleportTransport(uint32 newMapid, float x, float y, float z);
        void UpdatePassengerPositions();
        void DoEventIfAny(KeyFrame const& node, bool departure);

        bool IsMoving() const { return _isMoving; }
        void SetMoving(bool val) { _isMoving = val; }

        TransportTemplate const* _transportInfo;

        KeyFrameVec::const_iterator _currentFrame;
        KeyFrameVec::const_iterator _nextFrame;
        uint32 _moveTimer;
        TimeTrackerSmall _positionChangeTimer;
        bool _isMoving;

        std::set<WorldObject*> _passengers;

        // this stores all non-player passengers that don't belong on current map
        UNORDERED_MAP<Map*, std::set<WorldObject*> > _mapPassengers;
};

#endif
