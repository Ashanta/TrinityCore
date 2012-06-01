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

#include "Common.h"
#include "Transport.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Path.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "DBCStores.h"
#include "World.h"
#include "GameObjectAI.h"
#include "Vehicle.h"

Transport::Transport() : GameObject()
{
    m_updateFlag = (UPDATEFLAG_TRANSPORT | UPDATEFLAG_LOWGUID | UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_ROTATION);
}

Transport::~Transport()
{
}

bool Transport::Create(uint32 guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress, uint32 dynflags)
{
    Relocate(x, y, z, ang);
    // instance id and phaseMask isn't set to values different from std.

    if (!IsPositionValid())
    {
        sLog->outError("Transport (GUID: %u) not created. Suggested coordinates isn't valid (X: %f Y: %f)",
            guidlow, x, y);
        return false;
    }

    Object::_Create(guidlow, 0, HIGHGUID_MO_TRANSPORT);

    GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(entry);

    if (!goinfo)
    {
        sLog->outErrorDb("Transport not created: entry in `gameobject_template` not found, guidlow: %u map: %u  (X: %f Y: %f Z: %f) ang: %f", guidlow, mapid, x, y, z, ang);
        return false;
    }

    m_goInfo = goinfo;

    TransportTemplate const* tInfo = sTransportMgr->GetTransportTemplate(entry);
    if (!tInfo)
    {
        sLog->outError("Transport %u (name: %s) will not be created, missing `transport_template` entry.", entry, goinfo->name);
        return false;
    }

    _transportInfo = tInfo;

    // initialize waypoints
    _nextFrame = tInfo->keyFrames.begin();
    _currentFrame = _nextFrame++;

    SetFloatValue(OBJECT_FIELD_SCALE_X, goinfo->size);
    SetUInt32Value(GAMEOBJECT_FACTION, goinfo->faction);
    SetUInt32Value(GAMEOBJECT_FLAGS, goinfo->flags);
    SetPeriod(tInfo->pathTime);
    SetEntry(goinfo->entry);
    SetDisplayId(goinfo->displayId);
    SetGoState(GO_STATE_READY);
    SetGoType(GAMEOBJECT_TYPE_MO_TRANSPORT);

    SetGoAnimProgress(animprogress);
    if (dynflags)
        SetUInt32Value(GAMEOBJECT_DYNAMIC, MAKE_PAIR32(0, dynflags));

    SetName(goinfo->name);
    return true;
}

void Transport::Update(uint32 diff)
{
    if (!AI())
    {
        if (!AIM_Initialize())
            sLog->outError("Could not initialize GameObjectAI for Transport");
    }
    else
        AI()->UpdateAI(diff);

    if (GetKeyFrames().size() <= 1)
        return;

    _moveTimer += diff;
    _moveTimer %= GetPeriod();
    while (_moveTimer > _nextFrame->PathTime || _moveTimer < _currentFrame->DepartureTime)
    {
        // arrived at next stop point
        if (_transportInfo->pathTime > _nextFrame->PathTime && _moveTimer < _nextFrame->DepartureTime)
        {
            if (IsMoving())
            {
                SetMoving(false);
                DoEventIfAny(*_currentFrame, false);
            }
            break;
        }


        MoveToNextWayPoint();

        SetMoving(true);

        DoEventIfAny(*_currentFrame, true);

        // first check help in case client-server transport coordinates de-synchronization
        if (_currentFrame->IsTeleportFrame())
            TeleportTransport(_nextFrame->Node->mapid, _nextFrame->Node->x, _nextFrame->Node->y, _nextFrame->Node->z);

        ASSERT(_nextFrame != GetKeyFrames().begin());

        sScriptMgr->OnRelocate(this, _currentFrame->Node->index, _currentFrame->Node->mapid, _currentFrame->Node->x, _currentFrame->Node->y, _currentFrame->Node->z);

        sLog->outDebug(LOG_FILTER_TRANSPORTS, "%s moved to %f %f %f %d", GetName(), GetPositionX(), GetPositionY(), GetPositionZ(), _currentFrame->Node->mapid);
    }

    if (IsMoving())
    {
        float t = CalculateSegmentPos((float)_moveTimer/(float)IN_MILLISECONDS);
        G3D::Vector3 pos;
        _currentFrame->Spline->evaluate_percent(_currentFrame->Index, t, pos);
        GetMap()->GameObjectRelocation(this, pos.x, pos.y, pos.z, GetAngle(pos.x, pos.y) + float(M_PI));
        UpdatePassengerPositions();
    }

    sScriptMgr->OnTransportUpdate(this, diff);
}

void Transport::AddPassenger(WorldObject* passenger)
{
    if (_passengers.insert(passenger).second)
        sLog->outDetail("Object %s boarded transport %s.", passenger->GetName(), GetName());

    if (Player* plr = passenger->ToPlayer())
        sScriptMgr->OnAddPassenger(this, plr);
}

void Transport::RemovePassenger(WorldObject* passenger)
{
    if (_passengers.erase(passenger))
        sLog->outDetail("Object %s removed from transport %s.", passenger->GetName(), GetName());


    if (Player* plr = passenger->ToPlayer())
        sScriptMgr->OnRemovePassenger(this, plr);
}

Creature* Transport::CreateNPCPassenger(uint32 guid, uint32 entry, float x, float y, float z, float o, CreatureData const* data /*= NULL*/)
{
    Map* map = GetMap();
    Creature* creature = new Creature();

    if (!creature->LoadCreatureFromDB(guid, map, false))
    {
        delete creature;
        return NULL;
    }

    creature->SetTransport(this);
    creature->AddUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
    creature->m_movementInfo.guid = GetGUID();
    creature->m_movementInfo.t_pos.Relocate(x, y, z, o);
    CalculatePassengerPosition(x, y, z, o);
    creature->Relocate(x, y, z, o);
    creature->SetHomePosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation());
    creature->SetTransportHomePosition(creature->m_movementInfo.t_pos);

    if (!creature->IsPositionValid())
    {
        sLog->outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates aren't valid (X: %f Y: %f)",creature->GetGUIDLow(),creature->GetEntry(),creature->GetPositionX(),creature->GetPositionY());
        delete creature;
        return NULL;
    }

    map->AddToMap(creature);
    AddPassenger(creature);

    sScriptMgr->OnAddCreaturePassenger(this, creature);
    return creature;
}

GameObject* Transport::CreateGOPassenger(uint32 guid, uint32 entry, float x, float y, float z, float o, GameObjectData const* data /*= NULL*/)
{
    Map* map = GetMap();
    GameObject* go = new GameObject();

    if (!go->LoadGameObjectFromDB(guid, map, false))
    {
        delete go;
        return NULL;
    }

    go->SetTransport(this);
    go->m_movementInfo.guid = GetGUID();
    go->m_movementInfo.t_pos.Relocate(x, y, z, o);
    CalculatePassengerPosition(x, y, z, o);
    go->Relocate(x, y, z, o);

    if (!go->IsPositionValid())
    {
        sLog->outError("GameObject (guidlow %d, entry %d) not created. Suggested coordinates aren't valid (X: %f Y: %f)", go->GetGUIDLow(), go->GetEntry(), go->GetPositionX(), go->GetPositionY());
        delete go;
        return NULL;
    }

    map->AddToMap(go);
    AddPassenger(go);

    //sScriptMgr->OnAddCreaturePassenger(this, go);
    return go;
}

//! This method transforms supplied transport offsets into global coordinates
void Transport::CalculatePassengerPosition(float& x, float& y, float& z, float& o)
{
    float inx = x, iny = y, inz = z, ino = o;
    o = GetOrientation() + ino;
    x = GetPositionX() + (inx * cos(GetOrientation()) + iny * sin(GetOrientation() + M_PI));
    y = GetPositionY() + (iny * cos(GetOrientation()) + inx * sin(GetOrientation()));
    z = GetPositionZ() + inz;
    MapManager::NormalizeOrientation(o);
}

//! This method transforms supplied global coordinates into local offsets
void Transport::CalculatePassengerOffset(float& x, float& y, float& z, float& o)
{
    o -= GetOrientation();
    z -= GetPositionZ();
    y -= GetPositionY();    // y = searchedY * cos(o) + searchedX * sin(o)
    x -= GetPositionX();    // x = searchedX * cos(o) + searchedY * sin(o + pi)
    float inx = x, iny = y;
    y = (iny - inx * tan(GetOrientation())) / (cos(GetOrientation()) - sin(GetOrientation() + M_PI) * tan(GetOrientation()));
    x = (inx - iny * sin(GetOrientation() + M_PI) / cos(GetOrientation())) / (cos(GetOrientation()) - tan(GetOrientation()) * sin(GetOrientation() + M_PI));
    MapManager::NormalizeOrientation(o);
}

void Transport::MoveToNextWayPoint()
{
    _currentFrame = _nextFrame++;
    if (_nextFrame == GetKeyFrames().end())
        _nextFrame = GetKeyFrames().begin();
}

float Transport::CalculateSegmentPos(float now)
{
    const float speed = float(m_goInfo->moTransport.moveSpeed);
    const float accel = float(m_goInfo->moTransport.accelRate);
    KeyFrame const& frame = *_currentFrame;
    float timeSinceStop = frame.TimeFrom + (now - (1.0f/IN_MILLISECONDS) * frame.DepartureTime);
    float timeUntilStop = frame.TimeTo - (now - (1.0f/IN_MILLISECONDS) * frame.DepartureTime);
    // timeSinceStop includes the waiting time, so check if we move already
    /* if (frame.IsStopFrame())
    {
    if (timeSinceStop < frame.node->delay)
    return 0.0f;
    timeSinceStop -= frame.node->delay;
    } */
    float segmentPos, dist;
    float accelTime = _transportInfo->accelTime;
    float accelDist = _transportInfo->accelDist;
    // calculate from nearest stop, less confusing calculation...
    if (timeSinceStop < timeUntilStop)
    {
        if (timeSinceStop < accelTime)
            dist = 0.5f * accel * timeSinceStop * timeSinceStop;
        else
            dist = accelDist + (timeSinceStop - accelTime) * speed;
        segmentPos = dist - frame.DistSinceStop;
    }
    else
    {
        if (timeUntilStop < _transportInfo->accelTime)
            dist = 0.5f * accel * timeUntilStop * timeUntilStop;
        else
            dist = accelDist + (timeUntilStop - accelTime) * speed;
        segmentPos = frame.DistUntilStop - dist;
    }

    return segmentPos / _nextFrame->DistFromPrev;
}

void Transport::TeleportTransport(uint32 newMapid, float x, float y, float z)
{
    Map const* oldMap = GetMap();
    Relocate(x, y, z);

    // TODO: Teleport players, move current map's non-player passengers out of passenger lists and put new map's passengers on transport
    // On retail, non-player passengers are not moved across maps

    //we need to create and save new Map object with 'newMapid' because if not done -> lead to invalid Map object reference...
    //player far teleport would try to create same instance, but we need it NOW for transport...
    Map* newMap = sMapMgr->CreateBaseMap(newMapid);
    GetMap()->RemoveFromMap<GameObject>(this, false);
    SetMap(newMap);
    GetMap()->AddToMap<GameObject>(this);

    MoveToNextWayPoint();
}

void Transport::UpdatePassengerPositions()
{
    for (std::set<WorldObject*>::iterator itr = _passengers.begin(); itr != _passengers.end(); ++itr)
    {
        WorldObject* passenger = *itr;
        // transport teleported but passenger not yet (can happen for players)
        if (passenger->GetMap() != GetMap())
            continue;

        // if passenger is on vehicle we have to assume the vehicle is also on transport
        // and its the vehicle that will be updating its passengers
        if (Unit* unit = passenger->ToUnit())
            if (unit->GetVehicle())
                continue;

        float x, y, z, o;
        passenger->m_movementInfo.t_pos.GetPosition(x, y, z, o);
        CalculatePassengerPosition(x, y, z, o);
        switch (passenger->GetTypeId())
        {
            case TYPEID_UNIT:
            {
                Creature* creature = passenger->ToCreature();
                GetMap()->CreatureRelocation(creature, x, y, z, o, false);
                creature->GetTransportHomePosition(x, y, z, o);
                CalculatePassengerPosition(x, y, z, o);
                creature->SetHomePosition(x, y, z, o);
                break;
            }
            case TYPEID_PLAYER:
                GetMap()->PlayerRelocation(passenger->ToPlayer(), x, y, z, o);
                break;
            case TYPEID_GAMEOBJECT:
                GetMap()->GameObjectRelocation(passenger->ToGameObject(), x, y, z, o, false);
                break;
        }

        if (Unit* unit = passenger->ToUnit())
            if (Vehicle* vehicle = unit->GetVehicleKit())
                vehicle->RelocatePassengers(x, y, z, o);
    }
}

void Transport::DoEventIfAny(KeyFrame const& node, bool departure)
{
    if (uint32 eventid = departure ? node.Node->departureEventID : node.Node->arrivalEventID)
    {
        sLog->outDebug(LOG_FILTER_MAPSCRIPTS, "Taxi %s event %u of node %u of %s path", departure ? "departure" : "arrival", eventid, node.Node->index, GetName());
        GetMap()->ScriptsStart(sEventScripts, eventid, this, this);
        EventInform(eventid);
    }
}
