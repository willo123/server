//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// the map of OpenTibia
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundumpion; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundumpion,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////


#include "definitions.h"

#include <string>
#include <sstream>
#include <cctype>
#include <algorithm>

using namespace std;

#include "otsystem.h"
#include <stdio.h>

#include "items.h"
#include "map.h"
#include "tile.h"

#include "player.h"
#include "tools.h"

#include "networkmessage.h"

#include "npc.h"

#include "luascript.h"
#include <ctype.h>

#define EVENT_CHECKPLAYER          123
#define EVENT_CHECKPLAYERATTACKING 124

extern LuaScript g_config;

Map::Map()
{
  //first we fill the map with
  for(int y = 0; y < MAP_HEIGHT; y++)
  {
    for(int x = 0; x < MAP_WIDTH; x++)
    {
      //				setTile(x,y,7,102);
    }
  }


  OTSYS_THREAD_LOCKVARINIT(mapLock);
  OTSYS_THREAD_LOCKVARINIT(eventLock);
  OTSYS_THREAD_SIGNALVARINIT(eventSignal);

  OTSYS_CREATE_THREAD(eventThread, this);
}


Map::~Map()
{
}


bool Map::LoadMap(std::string filename) {

  // get maximum number of players allowed...
  max_players = atoi(g_config.getGlobalString("maxplayers").c_str());
  std::cout << ":: Player Limit: " << max_players << std::endl;

  FILE* f;
  std::cout << ":: Loading Map from " << filename << " ... ";
  f=fopen(filename.c_str(),"r");
  if(f){
    fclose(f);
    loadMapXml(filename.c_str());
    std::cout << "[done]" << std::endl;
    return true;
  }
  else{
    //this code is ugly but works
    //TODO improve this code to support things like
    //a quadtree to speed up everything
#ifdef __DEBUG__
    std::cout << "Loading map" << std::endl;
#endif
    FILE* dump=fopen("otserv.map", "rb");
    if(!dump){
#ifdef __DEBUG__
      std::cout << "Loading old format mapfile failed" << std::endl;
#endif
      exit(1);
    }
    Position topleft, bottomright, now;


    topleft.x=fgetc(dump)*256;	topleft.x+=fgetc(dump);
    topleft.y=fgetc(dump)*256;	topleft.y+=fgetc(dump);
    topleft.z=fgetc(dump);

    bottomright.x=fgetc(dump)*256;	bottomright.x+=fgetc(dump);
    bottomright.y=fgetc(dump)*256;	bottomright.y+=fgetc(dump);
    bottomright.z=fgetc(dump);

    int xsize= bottomright.x-topleft.x;
    int ysize= bottomright.y-topleft.y;
    int xorig=((MAP_WIDTH)-xsize)/2;
    int yorig=((MAP_HEIGHT)-ysize)/2;
    //TODO really place this map patch where it belongs

    for(int y=0; y < ysize; y++){
      for(int x=0; x < xsize; x++){
        while(true)
        {
          int id=fgetc(dump)*256;id+=fgetc(dump);
          if(id==0x00FF)
            break;
          //now.x=x+MINX;now.y=y+MINY;now.z=topleft.z;

          Item *item = new Item(id);
          if (item->isGroundTile())
          {
            setTile(xorig+x, yorig+y, 7, id);
            delete item;
          }
          else
          {
            Tile *t = getTile(xorig+x, yorig+y, 7);
            if (t)
              t->addThing(item);
          }
          //tiles[x][y]->push_back(new Item(id));
        }
      }
    }
    fclose(dump);
  }

  return true;
}



/*****************************************************************************/


OTSYS_THREAD_RETURN Map::eventThread(void *p)
{
  Map* _this = (Map*)p;

  // basically what we do is, look at the first scheduled item,
  // and then sleep until it's due (or if there is none, sleep until we get an event)
  // of course this means we need to get a notification if there are new events added
  while (true)
  {
#ifdef __DEBUG__EVENTSCHEDULER__
    std::cout << "schedulercycle start..." << std::endl;
#endif

    SchedulerTask* task = NULL;

    // check if there are events waiting...
    OTSYS_THREAD_LOCK(_this->eventLock)

      int ret;
    if (_this->eventList.size() == 0) {
      // unlock mutex and wait for signal
      ret = OTSYS_THREAD_WAITSIGNAL(_this->eventSignal, _this->eventLock);
    } else {
      // unlock mutex and wait for signal or timeout
      ret = OTSYS_THREAD_WAITSIGNAL_TIMED(_this->eventSignal, _this->eventLock, _this->eventList.top()->getCycle());
    }
    // the mutex is locked again now...
    if (ret == OTSYS_THREAD_TIMEOUT) {
      // ok we had a timeout, so there has to be an event we have to execute...
#ifdef __DEBUG__EVENTSCHEDULER__
      std::cout << "event found at " << OTSYS_TIME() << " which is to be scheduled at: " << _this->eventList.top()->getCycle() << std::endl;
#endif
      task = _this->eventList.top();
      _this->eventList.pop();
    }

    OTSYS_THREAD_UNLOCK(_this->eventLock);
    if (task) {
      (*task)(_this);
      delete task;
    }
  }

  /*

     if (eventTick < OTSYS_TIME() / 10)
     {
     list<MapEvent> *eventList = NULL;

     OTSYS_THREAD_LOCK(_this->eventLock)
     {
     eventList = _this->eventLists[eventTick % 12000];
     _this->eventLists[eventTick % 12000] = NULL;
     }
     OTSYS_THREAD_UNLOCK(_this->eventLock)

     if (eventList != NULL)
     {
     std::list<MapEvent>::iterator it;
     for (it = eventList->begin(); it != eventList->end(); it++)
     {
     if ((*it).tick == eventTick)
     {
     switch ((*it).type)
     {
     case EVENT_CHECKPLAYER:
     _this->checkPlayer((unsigned long)(*it).data);
     break;

     case EVENT_CHECKPLAYERATTACKING:
     _this->checkPlayerAttacking((unsigned long)(*it).data);
     break;
     }
     }
     else
     {
  // todo reschedule 
  }
  }

  delete eventList;
  }

  eventTick++;
  }
  else
  {
  OTSYS_SLEEP(1);  // nothing to-do :)
  }
  */
}

void Map::addEvent(SchedulerTask* event) {
  bool do_signal = false;
  OTSYS_THREAD_LOCK(eventLock)

    eventList.push(event);
  if (eventList.empty() || *event < *eventList.top())
    do_signal = true;

  OTSYS_THREAD_UNLOCK(eventLock)

    if (do_signal)
      OTSYS_THREAD_SIGNAL_SEND(eventSignal);

}

/*****************************************************************************/



Tile* Map::getTile(unsigned short _x, unsigned short _y, unsigned char _z)
{
  if (_z < MAP_LAYER)
  {
    // _x & 0x3F  is like _x % 64
    TileMap *tm = &tileMaps[_x & 1][_y & 1][_z];

    // search in the stl map for the requested tile
    TileMap::iterator it = tm->find((_x << 16) | _y);

    // ... found
    if (it != tm->end())
      return it->second;
  }
	
	 // or not
  return NULL;
}


void Map::setTile(unsigned short _x, unsigned short _y, unsigned char _z, unsigned short groundId)
{
  Tile *tile = getTile(_x, _y, _z);

  if (tile != NULL)
  {
    tile->ground = groundId;
  }
  else
  {
    tile = new Tile();
    tile->ground = groundId;
    tileMaps[_x & 1][_y & 1][_z][(_x << 16) | _y] = tile;
  }  
}



int Map::loadMapXml(const char *filename){
	xmlDocPtr doc;
	xmlNodePtr root, tile, p;
	int width, height;

	xmlLineNumbersDefault(1);
	doc=xmlParseFile(filename);
	if (!doc) {
    std::cout << "FATAL: couldnt load map. exiting" << std::endl;
    exit(1);
  }
	root=xmlDocGetRootElement(doc);
	if(xmlStrcmp(root->name,(const xmlChar*) "map")){
    xmlFreeDoc(doc);
    std::cout << "FATAL: couldnt load map. exiting" << std::endl;
    exit(1);
  }
	width=atoi((const char*)xmlGetProp(root, (const xmlChar *) "width"));
	height=atoi((const char*)xmlGetProp(root, (const xmlChar *) "height"));

	int xorig=((MAP_WIDTH)-width)/2;
	int yorig=((MAP_HEIGHT)-height)/2;
	tile=root->children;
	int numpz = 0;
	for(int y=0; y < height; y++){
	    for(int x=0; x < width; x++){
		    if (!tile) {
		    std::cout << "no tile for " << x << " / " << y << std::endl;
		    exit(1);
	    }
			const char* pz = (const char*)xmlGetProp(tile, (const xmlChar *) "pz");
			p=tile->children;
	
			while(p)
			{
				if(xmlStrcmp(p->name,(const xmlChar*) "item")==0){
					Item* myitem=new Item();
					myitem->unserialize(p);

					if (myitem->isGroundTile())
					{
						setTile(xorig+x, yorig+y, 7, myitem->getID());
						delete myitem;

						if (pz && (strcmp(pz, "1") == 0)) {
							numpz++;
							getTile(xorig+x, yorig+y, 7)->setPz();
						}
				    }
					else
					{
						Tile *t = getTile(xorig+x, yorig+y, 7);
						if (t)
						{
							if (myitem->isAlwaysOnTop())
								t->topItems.push_back(myitem);
							else
								t->downItems.push_back(myitem);
						}
					}

				}
				if(xmlStrcmp(p->name,(const xmlChar*) "npc")==0){
					std::string name = (const char*)xmlGetProp(p, (const xmlChar *) "name");
					Npc* mynpc = new Npc(name.c_str(), this);
					//first we have to set the position of our creature...
					mynpc->pos.x=xorig+x;
					mynpc->pos.y=yorig+y;
					if(!this->placeCreature(mynpc)){
						//tinky winky: "oh oh"
					}
				}
				p=p->next;
			}
			tile=tile->next;
		}
	}
	xmlFreeDoc(doc);
	return 0;
}



Creature* Map::getCreatureByID(unsigned long id)
{
  std::map<long, Creature*>::iterator i;
  for( i = playersOnline.begin(); i != playersOnline.end(); i++ )
  {
    if( (i->second)->getID() == id )
    {
      return i->second;
    }
  }
  return NULL; //just in case the player doesnt exist
}


bool Map::placeCreature(Creature* c)
{
  if (c->access == 0 && playersOnline.size() >= max_players)
    return false;

  OTSYS_THREAD_LOCK(mapLock)



	// add player to the online list
		playersOnline[c->getID()] = c;
	if (c->isPlayer())
	{

		((Player*)c)->usePlayer();
		std::cout << playersOnline.size() << " players online." << std::endl;
	}

    addEvent(makeTask(1000, std::bind2nd(std::mem_fun(&Map::checkPlayer), c->id)));
    addEvent(makeTask(2000, std::bind2nd(std::mem_fun(&Map::checkPlayerAttacking), c->id)));

  while (getTile(c->pos.x, c->pos.y, c->pos.z)->isBlocking())
  {
    // crap we need to find another spot
    c->pos.x++;
  }

	Tile* tile=getTile(c->pos.x, c->pos.y, c->pos.z);
	if(!tile){
		this->setTile(c->pos.x, c->pos.y, c->pos.z, 0);
		tile=getTile(c->pos.x, c->pos.y, c->pos.z);
	}
  tile->addThing(c);

  CreatureVector::iterator cit;
  for (int x = c->pos.x - 9; x <= c->pos.x + 9; x++)
    for (int y = c->pos.y - 7; y <= c->pos.y + 7; y++)
    {
      Tile *tile = getTile(x, y, 7);
      if (tile)
      {
        for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
        {
          (*cit)->onCreatureAppear(c);
        }
      }
    }

  OTSYS_THREAD_UNLOCK(mapLock)

    return true;
}

bool Map::removeCreature(Creature* c)
{
  OTSYS_THREAD_LOCK(mapLock)

    //removeCreature from the online list

    std::map<long, Creature*>::iterator pit = playersOnline.find(c->getID());
  if (pit != playersOnline.end()) {
    playersOnline.erase(pit);


#ifdef __DEBUG__
    std::cout << "removing creature "<< std::endl;
#endif

    int stackpos = getTile(c->pos.x, c->pos.y, c->pos.z)->getCreatureStackPos(c);
    getTile(c->pos.x, c->pos.y, c->pos.z)->removeThing(c);

    CreatureVector::iterator cit;
    for (int x = c->pos.x - 9; x <= c->pos.x + 9; x++)
      for (int y = c->pos.y - 7; y <= c->pos.y + 7; y++)
      {
        Tile *tile = getTile(x, y, 7);
        if (tile)
        {
          for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
          {
            (*cit)->onCreatureDisappear(c, stackpos);
          }
        }
      }

  }

  std::cout << playersOnline.size() << " players online." << std::endl;

  if (c->isPlayer())
    ((Player*)c)->releasePlayer();

  OTSYS_THREAD_UNLOCK(mapLock)

    return true;
}

/*int Map::requestAction(Creature* c, Action* a)
  {

  if(a->type==ACTION_LOOK_AT){

  a->buffer=tiles[a->pos1.x][a->pos1.y]->getDescription();
  a->creature->sendAction(a);
  }
  else if(a->type==ACTION_REQUEST_APPEARANCE){
  a->creature->sendAction(a);
  }
  else if(a->type==ACTION_CHANGE_APPEARANCE){
  distributeAction(a->pos1,a);
  }
  else if(a->type==ACTION_ITEM_USE){
  GETTILEBYPOS(a->pos1)->getItemByStack(a->stack)->use();
  distributeAction(a->pos1,a);
  }

  return true;
  }
  */

/*int Map::summonItem(Action* a)
  {
  Item* item=new Item(a->id);
  if(item->isStackable() && !a->count)
  return TMAP_ERROR_NO_COUNT;
  if(!a->id)
  return false;
#ifdef __DEBUG__
#endif
item->count=a->count;
Tile* tile = tiles[a->pos1.x][a->pos1.y];
if((unsigned int)a->id != tile->getItemByStack(tile->getStackPosItem())->getID() ||
!item->isStackable()){
std::cout << "appear id: " << tile->getItemByStack(tile->getStackPosItem())->getID()<< std::endl;
//if an item of this type isnt already there or this type isnt stackable
tile->addItem(item);
Action b;
b.type=ACTION_ITEM_APPEAR;
b.pos1=a->pos1;
b.id=a->id;
if(item->isStackable())
b.count=a->count;
distributeAction(a->pos1, &b);
}
else {
std::cout << "merge" << std::endl;
//we might possibly merge the top item and the stuff we want to add
Item* onTile = tile->getItemByStack(tile->getStackPosItem());
onTile->count+=item->count;
int tmpnum=0;
if(onTile->count>100){
tmpnum= onTile->count-100;
onTile->count=100;
}
Action b;
b.type=ACTION_ITEM_CHANGE;
b.pos1=a->pos1;
b.id=a->id;
b.count=onTile->count;

distributeAction(a->pos1, &b);

if(tmpnum>0){
std::cout << "creating extra item" << std::endl;
item->count=tmpnum;
tile->addItem(item);
Action c;
c.type=ACTION_ITEM_APPEAR;
c.pos1=a->pos1;
c.id=a->id;
c.count=item->count;
distributeAction(a->pos1, &c);
//not all fit into the already existing item, create a new one
}
}
return true;
}
*/

/*
   int Map::summonItem(Position pos, int id)
   {

   std::cout << "Deprecated summonItem" << std::cout;
   if(!id)
   return false;
#ifdef __DEBUG__
std::cout << "Summoning item with id " << id << std::endl;
#endif
Item* i=new Item(id);
tiles[pos.x][pos.y]->addItem(i);
Action* a= new Action;
a->type=ACTION_ITEM_APPEAR;
a->pos1=pos;
a->id=id;
if(!i->isStackable())
a->count=0;
distributeAction(pos, a);
return true;

}

*/

/*
   int Map::changeGround(Position pos, int id){
   if(!id)
   return false;
#ifdef __DEBUG__
std::cout << "Summoning item with id " << id << std::endl;
#endif
Item* i=new Item(id);
tiles[pos.x][pos.y]->addItem(i);
Action* a= new Action;
a->type=ACTION_GROUND_CHANGE;
a->pos1=pos;
a->id=id;
if(!i->isStackable())
a->count=0;
distributeAction(pos, a);
return true;
}

int Map::removeItem(Action* a){
int newcount;
Tile* tile=tiles[a->pos1.x][a->pos1.y];
std::cout << "COUNT: " << a->count << std::endl;
newcount=tile->removeItem(a->stack, a->type, a->count);
std::cout << "COUNT: " << newcount << std::endl;
if(newcount==0)
distributeAction(a->pos1,a);
else{
std::cout << "distributing change" << std::endl;
a->type=ACTION_ITEM_CHANGE;
a->count=newcount;
distributeAction(a->pos1,a);
}
return true;
}

*/

void Map::thingMove(Creature *player, Thing *thing,
                    unsigned short to_x, unsigned short to_y, unsigned char to_z)
{
  OTSYS_THREAD_LOCK(mapLock)

  Tile *fromTile = getTile(thing->pos.x, thing->pos.y, thing->pos.z);

  if (fromTile)
  {
    int oldstackpos = fromTile->getThingStackPos(thing);

    thingMoveInternal(player, thing->pos.x, thing->pos.y, thing->pos.z, oldstackpos, to_x, to_y, to_z);
  }

  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::thingMove(Creature *player,
                    unsigned short from_x, unsigned short from_y, unsigned char from_z,
                    unsigned char stackPos,
                    unsigned short to_x, unsigned short to_y, unsigned char to_z)
{
  OTSYS_THREAD_LOCK(mapLock)

    thingMoveInternal(player, from_x, from_y, from_z, stackPos, to_x, to_y, to_z);

  OTSYS_THREAD_UNLOCK(mapLock)
}



void Map::thingMoveInternal(Creature *player,
                    unsigned short from_x, unsigned short from_y, unsigned char from_z,
                    unsigned char stackPos,
                    unsigned short to_x, unsigned short to_y, unsigned char to_z)
{
	Thing *thing = getTile(from_x, from_y, from_z)->getThingByStackPos(stackPos);

  if (thing)
  {
    if (player->access == 0 && thing->isPlayer() && ((Player*)thing)->access != 0)
    {
      player->sendCancel("Better dont touch him...");
      return;
    }

    Position oldPos;
    oldPos.x = from_x;
    oldPos.y = from_y;
    oldPos.z = from_z;

#ifdef __DEBUG__
	  std::cout << "moving" << std::endl;
#endif

    Tile *fromTile = getTile(from_x, from_y, from_z);
    Tile *toTile   = getTile(to_x, to_y, to_z);

    if ((fromTile != NULL) && (toTile != NULL))
    {
      if ((abs(from_x - player->pos.x) > 1) ||
          (abs(from_y - player->pos.y) > 1))
      {
        player->sendCancel("To far away...");
      }
      else if ((abs(oldPos.x - to_x) > thing->ThrowRange) ||
               (abs(oldPos.y - to_y) > thing->ThrowRange))
      {
        player->sendCancel("Not there...");
      }
      else if ((!thing->CanMovedTo(toTile)) || (toTile->isBlocking()))
      {
        if (player == thing)
          player->sendCancelWalk("Sorry, not possible...");
        else
          player->sendCancel("Sorry, not possible...");
      }
      else
      {
        int oldstackpos = fromTile->getThingStackPos(thing);

        if (fromTile->removeThing(thing))
        {
					toTile->addThing(thing);

          thing->pos.x = to_x;
          thing->pos.y = to_y;
          thing->pos.z = to_z;
	      if (thing->isCreature())
          {
            // we need to update the direction the player is facing to...
            // otherwise we are facing some problems in turning into the
            // direction we were facing before the movement
            // check y first cuz after a diagonal move we lock to east or west
            if (to_y < oldPos.y) ((Player*)thing)->direction = NORTH;
            if (to_y > oldPos.y) ((Player*)thing)->direction = SOUTH;
            if (to_x > oldPos.x) ((Player*)thing)->direction = EAST;
            if (to_x < oldPos.x) ((Player*)thing)->direction = WEST;
          }

          CreatureVector::iterator cit;
          for (int x = min(oldPos.x, (int)to_x) - 9; x <= max(oldPos.x, (int)to_x) + 9; x++)
            for (int y = min(oldPos.y, (int)to_y) - 7; y <= max(oldPos.y, (int)to_y) + 7; y++)
            {
              Tile *tile = getTile(x, y, 7);
              if (tile)
              {
				   
                for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
                {
                  (*cit)->onThingMove(player, thing, &oldPos, oldstackpos);
                }
              }
						}

						if(fromTile->getThingCount() > 8) {
							//We need to pop up this item
							Thing *newthing = fromTile->getThingByStackPos(9);
							NetworkMessage msg;

							if(newthing != NULL) {
								CreatureVector::iterator cit;
								for (int x = newthing->pos.x - 9; x <= newthing->pos.x + 9; x++)
									for (int y = newthing->pos.y - 7; y <= newthing->pos.y + 7; y++)
									{
										Tile *tile = getTile(x, y, 7);
										if (tile)
										{
											for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
											{
												(*cit)->onTileUpdated(&oldPos);
											}
										}
									}
							}
						}
				}
      }
    }
  }
}



void Map::creatureTurn(Creature *creature, Direction dir)
{
  OTSYS_THREAD_LOCK(mapLock)

    if (creature->direction != dir)
    {
      creature->direction = dir;

      int stackpos = getTile(creature->pos.x, creature->pos.y, creature->pos.z)->getThingStackPos(creature);

      CreatureVector::iterator cit;
      for (int x = creature->pos.x - 9; x <= creature->pos.x + 9; x++)
        for (int y = creature->pos.y - 7; y <= creature->pos.y + 7; y++)
        {
          Tile *tile = getTile(x, y, 7);
          if (tile)
          {
            for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
            {
              (*cit)->onCreatureTurn(creature, stackpos);
            }
          }
        }
    }

  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::creatureSay(Creature *creature, unsigned char type, const std::string &text)
{
  OTSYS_THREAD_LOCK(mapLock)
// First, check if this was a GM command
if(text[0] == '/' && creature->access > 0)
{
// Get the command
switch(text[1])
{
default:break;
} //switch(text[1])
}
// It was no command, or it was just a player
else {
    CreatureVector::iterator cit;

  for (int x = creature->pos.x - 9; x <= creature->pos.x + 9; x++)
    for (int y = creature->pos.y - 7; y <= creature->pos.y + 7; y++)
    {
      Tile *tile = getTile(x, y, 7);
      if (tile)
      {
        for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
        {
          (*cit)->onCreatureSay(creature, type, text);
        }
      }
    }
}
  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::creatureChangeOutfit(Creature *creature)
{
  OTSYS_THREAD_LOCK(mapLock)

  CreatureVector::iterator cit;
  for (int x = creature->pos.x - 9; x <= creature->pos.x + 9; x++)
    for (int y = creature->pos.y - 7; y <= creature->pos.y + 7; y++)
    {
      Tile *tile = getTile(x, y, 7);
      if (tile)
      {
        for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
        {
          (*cit)->onCreatureChangeOutfit(creature);
        }
      }
    }

  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::creatureYell(Creature *creature, std::string &text)
{
  OTSYS_THREAD_LOCK(mapLock)
  
  if(creature->isPlayer() && creature->access == 0 && creature->exhausted) {
      NetworkMessage msg;
      msg.AddTextMessage(MSG_EVENT, "You are exhausted.");
      ((Player*)creature)->sendNetworkMessage(&msg);
  }
  else {
      creature->exhausted = true;
      addEvent(makeTask(1200, std::bind2nd(std::mem_fun(&Map::resetExhausted), creature->id)));
      CreatureVector::iterator cit;
      std::transform(text.begin(), text.end(), text.begin(), upchar);
  for (int x = creature->pos.x - 18; x <= creature->pos.x + 18; x++)
    for (int y = creature->pos.y - 14; y <= creature->pos.y + 14; y++)
    {
      Tile *tile = getTile(x, y, 7);
      if (tile)
      {
        for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
        {
          (*cit)->onCreatureSay(creature, 3, text);
        }
      }
    }
  }    
  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::creatureSpeakTo(Creature *creature, const std::string &receiver, const std::string &text)
{
  OTSYS_THREAD_LOCK(mapLock)
    OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::creatureBroadcastMessage(Creature *creature, const std::string &text)
{
  OTSYS_THREAD_LOCK(mapLock)
    OTSYS_THREAD_UNLOCK(mapLock)
}

void Map::creatureMakeDamage(Creature *creature, Creature *attackedCreature, fight_t damagetype){

	NetworkMessage msg;
	//can the attacker reach the attacked?
	bool inReach = false;
	switch(damagetype){
		case FIGHT_MELEE:
			if((std::abs(creature->pos.x-attackedCreature->pos.x) <= 1) &&
				(std::abs(creature->pos.y-attackedCreature->pos.y) <= 1) &&
				(creature->pos.z == attackedCreature->pos.z))
					inReach = true;
		break;
		case FIGHT_DIST:
			if((std::abs(creature->pos.x-attackedCreature->pos.x) <= 8) &&
				(std::abs(creature->pos.y-attackedCreature->pos.y) <= 6) &&
				(creature->pos.z == attackedCreature->pos.z))
					inReach = true;
		break;
		case FIGHT_MAGICDIST:
			if((std::abs(creature->pos.x-attackedCreature->pos.x) <= 8) &&
				(std::abs(creature->pos.y-attackedCreature->pos.y) <= 6) &&
				(creature->pos.z == attackedCreature->pos.z))
					inReach = true;	
		break;
	}
	
	if(!inReach)
		return;
	
	int damage = 1+(int)(10.0*rand()/(RAND_MAX+1.0));
	if (creature->access != 0)
		damage += 1337;
	if (damage < -50 || attackedCreature->access != 0)
		damage = 0;
	if (damage > 0)
		attackedCreature->drainHealth(damage);
	CreatureVector::iterator cit;
	Tile* targettile = getTile(attackedCreature->pos.x, attackedCreature->pos.y, attackedCreature->pos.z);
	for (int x = min(creature->pos.x, attackedCreature->pos.x) - 9; x <= max(creature->pos.x, attackedCreature->pos.x) + 9; x++)
	{	
		for (int y = min(creature->pos.y, attackedCreature->pos.y) - 7; y <= max(creature->pos.y, attackedCreature->pos.y) + 7; y++)
		{
			Tile *tile = getTile(x, y, 7);
			if (tile)
			{
				for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
				{
					if ((*cit)->isPlayer())
					{
						Player *p = (Player*)(*cit);
				            msg.Reset();
							if(damagetype == FIGHT_DIST)
								msg.AddDistanceShoot(creature->pos, attackedCreature->pos, 13);
							if(damagetype == FIGHT_MAGICDIST)
								msg.AddDistanceShoot(creature->pos, attackedCreature->pos, 4);
						if ((damage == 0) && (p->CanSee(attackedCreature->pos.x, attackedCreature->pos.y)))
						{
							msg.AddMagicEffect(attackedCreature->pos, NM_ME_PUFF);
						}
						else if ((damage < 0) && (p->CanSee(attackedCreature->pos.x, attackedCreature->pos.y)))
						{
							msg.AddMagicEffect(attackedCreature->pos, NM_ME_BLOCKHIT);
						}
						else
						{
							if (p->CanSee(attackedCreature->pos.x, attackedCreature->pos.y))
							{
								std::stringstream dmg;
								dmg << damage;
								msg.AddAnimatedText(attackedCreature->pos, 0xB4, dmg.str());
								msg.AddMagicEffect(attackedCreature->pos, NM_ME_DRAW_BLOOD);
								if (attackedCreature->health <= 0)
								{
									// remove character
									msg.AddByte(0x6c);
									msg.AddPosition(attackedCreature->pos);
									msg.AddByte(targettile->getThingStackPos(attackedCreature));
									msg.AddByte(0x6a);
									msg.AddPosition(attackedCreature->pos);
									Item item = Item(attackedCreature->lookcorpse);
									msg.AddItem(&item);
								}
								else
									msg.AddCreatureHealth(attackedCreature);
							}
						}
					
						if (p == attackedCreature)
						CreateDamageUpdate(p, creature, damage, msg);
						p->sendNetworkMessage(&msg);
					}
		
				}
			}
		}
	}
	if (attackedCreature->health <= 0) {
		targettile->removeThing(attackedCreature);
		playersOnline.erase(playersOnline.find(attackedCreature->getID()));
		targettile->addThing(new Item(attackedCreature->lookcorpse));
	}
}


std::list<Position> Map::getPathTo(Position start, Position to){
	std::list<Position> path;
	
	return path;
}



void Map::checkPlayer(unsigned long id)
{
  OTSYS_THREAD_LOCK(mapLock)

  Creature *creature = (Player*) getCreatureByID(id);

  if (creature != NULL)
  {
	 creature->onThink();
	 addEvent(makeTask(1000, std::bind2nd(std::mem_fun(&Map::checkPlayer), id)));

	 if(creature->isPlayer()){
		Player* player = (Player*) creature;
		 player->mana += min(10, player->manamax - player->mana);
		NetworkMessage msg;
		 
		msg.AddPlayerStats(player);
		player->sendNetworkMessage(&msg);
	 }
  }

  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::checkPlayerAttacking(unsigned long id)
{
  OTSYS_THREAD_LOCK(mapLock)

  Creature *creature = getCreatureByID(id);
  if (creature != NULL && creature->health > 0)
  {
    addEvent(makeTask(2000, std::bind2nd(std::mem_fun(&Map::checkPlayerAttacking), id)));

	  if (creature->attackedCreature != 0)
    {
      Creature *attackedCreature = getCreatureByID(creature->attackedCreature);
      if (attackedCreature)
      {
	      Tile* fromtile = getTile(creature->pos.x, creature->pos.y, creature->pos.z);
        if (!attackedCreature->isAttackable() == 0 && fromtile->isPz())
        {
          if (creature->isPlayer())
          {
            Player* player = (Player*)creature;
	          NetworkMessage msg;
            msg.AddTextMessage(MSG_STATUS, "You may not attack a person in a protection zone.");
            player->sendNetworkMessage(&msg);
          }
        }
        else
        {
          if (attackedCreature != NULL && attackedCreature->health > 0)
          {
	          this->creatureMakeDamage(creature, attackedCreature, creature->getFightType());
          }
        }
      }
	  }
  }

  OTSYS_THREAD_UNLOCK(mapLock)
}


void Map::CreateDamageUpdate(Creature* creature, Creature* attackCreature, int damage, NetworkMessage& msg) {
			if(!creature->isPlayer())
				return;
			Player* player = (Player*) creature;
	msg.AddPlayerStats(player);
  if (damage > 0) {
	  std::stringstream dmgmesg;
	  dmgmesg << "You lose " << damage << " hitpoints due to an attack by ";
	  dmgmesg << attackCreature->getName();
	  msg.AddTextMessage(MSG_EVENT, dmgmesg.str().c_str());
  }
  if (player->health <= 0)
    msg.AddTextMessage(MSG_EVENT, "Own3d!");
}


void Map::resetExhausted(unsigned long id)
{
  OTSYS_THREAD_LOCK(mapLock)

	Player *player = (Player*)getCreatureByID(id);

  if (player != NULL)
  {
		player->exhausted = false;
  }

	OTSYS_THREAD_UNLOCK(mapLock)
}

void Map::makeCastSpell(Creature *player, int mana, int mindamage, int maxdamage, unsigned char area[14][18], unsigned char ch, unsigned char typeArea, unsigned char typeDamage)
{
	NetworkMessage msg;
	Tile* fromtile = getTile(player->pos.x, player->pos.y, player->pos.z);
	if (player->access == 0 && fromtile->isPz() && maxdamage > 0) {
		if (player->isPlayer()) {
			msg.AddTextMessage(MSG_STATUS, "You may not attack a person in a protection zone.");
			((Player*)player)->sendNetworkMessage(&msg);
		}
		return;
	}
	if(player->access == 0 && (player->mana < mana || player->exhausted)) {
		if (player->isPlayer()) {
			msg.AddMagicEffect(player->pos, NM_ME_PUFF);
			msg.AddTextMessage(MSG_EVENT, "You are exhausted.");
			((Player*)player)->sendNetworkMessage(&msg);
		}
			return;
	} 

	player->exhausted =true;
	if (player->access == 0) player->mana -= mana;
	addEvent(makeTask(1200, std::bind2nd(std::mem_fun(&Map::resetExhausted), player->id)));

	std::vector< std::pair<unsigned long, unsigned long> > damagelist;
	damagelist.clear();

  std::pair<unsigned long, unsigned long> damagedcreature;
	std::vector<Position> areaPos;
	CreatureVector::iterator cit;
	
	Position pos = player->pos;
	pos.x -= 8;
	pos.y -= 6;

	for(int y = 0; y < 14; y++) {
		for(int x = 0; x < 18; x++) {
			Tile* tile = getTile(pos.x, pos.y, pos.z);
			if (tile) {
				if(tile->creatures.empty())
				{
					if(area[y][x] == ch)
						areaPos.push_back(pos);
				}
				else 
				for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++) {
					
					if(area[y][x] == ch) {
						damagedcreature.first  = (*cit)->getID();

						if(((!tile->isPz() || maxdamage < 0) || player->access != 0) && (*cit)->access == 0) {
							int damage = random_range(mindamage, maxdamage);

							if (damage > 0) {
								if(damage > (*cit)->health)
									damage = (*cit)->health;

								(*cit)->drainHealth(damage);
								damagedcreature.second = damage;
							} else {
								int newhealth = (*cit)->health - damage;
								if(newhealth > (*cit)->healthmax)
									newhealth = (*cit)->healthmax;

								damagedcreature.second = (*cit)->health - newhealth;
								(*cit)->health = newhealth;
							}
						}
						else
							damagedcreature.second = 0;

						damagelist.push_back(damagedcreature);
					}
				}
			}

			pos.x += 1;
		}
		
		pos.x -= 18;
		pos.y += 1;
	}

	//spectators
	for(int y =  player->pos.y - 12; y < player->pos.y + 12; y++) {
		for(int x = player->pos.x - 15; x < player->pos.x + 15; x++) {
			
			Tile* tile = getTile(x, y, player->pos.z);
			if (tile) {
				for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++) {
          if (!(*cit)->isPlayer())
            continue;

					Player *spectator = (Player*)*cit;

					NetworkMessage msg;

					for(int a = 0; a < areaPos.size(); a++) {
						if(spectator->CanSee(areaPos[a].x, areaPos[a].y))
							msg.AddMagicEffect(areaPos[a], typeArea);
					}

					for (int i = 0; i < damagelist.size(); i++) {

						//build effects for spectator
						Player *victim = (Player*) getCreatureByID(damagelist[i].first);
						Tile *vtile = getTile(victim->pos.x, victim->pos.y, victim->pos.z);

						if(spectator->CanSee(victim->pos.x, victim->pos.y)) {

							int damage = damagelist[i].second;

							if(vtile->isPz() && player->access == 0) {
								//msg.AddTextMessage(MSG_STATUS, "You may not attack a person in a protection zone.");
								msg.AddMagicEffect(victim->pos, NM_ME_PUFF);
							}
						else
							if(damage > 0) {
								msg.AddMagicEffect(victim->pos, typeDamage);

								std::stringstream dmg;
								dmg << damage;
								msg.AddAnimatedText(victim->pos, 0xB4, dmg.str());
							} else if (damage < 0) {
								std::stringstream dmg;
								dmg << -damage;
								msg.AddAnimatedText(victim->pos, MSG_INFO, dmg.str());
							} else msg.AddMagicEffect(victim->pos, NM_ME_PUFF);

							if (victim->health <= 0)
							{
								// remove character
								msg.AddByte(0x6c);
								msg.AddPosition(victim->pos);
								msg.AddByte(getTile(victim->pos.x, victim->pos.y, victim->pos.z)->getThingStackPos(victim));
								msg.AddByte(0x6a);
								msg.AddPosition(victim->pos);
								Item item = Item(victim->lookcorpse);
								msg.AddItem(&item);
							}
							else
								msg.AddCreatureHealth(victim);

							if(victim == spectator && damagelist[i].second > 0) {
								CreateDamageUpdate(spectator, player, damagelist[i].second, msg);
								msg.AddPlayerStats(victim);

								//spectator->sendNetworkMessage(&msg);
							}
							else
								msg.AddPlayerStats(spectator);
						}
					}

					spectator->sendNetworkMessage(&msg);
					msg.Reset();
				}
			}
		}
	}

	for (int i = 0; i < damagelist.size(); i++) {
		Player* victim = (Player*) getCreatureByID(damagelist[i].first);
		Tile *tile = getTile(victim->pos.x, victim->pos.y, victim->pos.z);

		if (victim->health <= 0) {
			tile->removeThing(victim);
			playersOnline.erase(playersOnline.find(victim->getID()));
			tile->addThing(new Item(victim->lookcorpse));
		}
	}
}

void Map::playerCastSpell(Creature *player, const std::string &text)
{
  OTSYS_THREAD_LOCK(mapLock)

	if(strcmp(text.c_str(), "exura vita") == 0) {
			NetworkMessage msg;
			if(player->isPlayer() && player->access == 0 && (player->mana < 100 || player->exhausted)) {
					msg.AddMagicEffect(player->pos, NM_ME_PUFF);
					msg.AddTextMessage(MSG_EVENT, "You are exhausted.");
					((Player*)player)->sendNetworkMessage(&msg);
			}
			else {
				player->exhausted = true;
				if (player->access == 0) player->mana -= 100;
				addEvent(makeTask(1200, std::bind2nd(std::mem_fun(&Map::resetExhausted), player->id)));
				
				int base = (int)player->level * 2 + player->maglevel * 3;
				int min = (int)(base * 2) / 2;
				int max = (int)((base * 2.8)) / 2;
				int newhealth = player->health + random_range(min, max); //player->health + 1+(int)(500.0*rand()/(RAND_MAX+1.0));
				if(newhealth > player->healthmax)
					newhealth = player->healthmax;

				player->health = newhealth;
				
				msg.AddCreatureHealth(player);
				msg.AddMagicEffect(player->pos, NM_ME_MAGIC_ENERGIE);

				CreatureVector::iterator cit;
				for (int x = player->pos.x - 9; x <= player->pos.x + 9; x++) {
					for (int y = player->pos.y - 7; y <= player->pos.y + 7; y++) {

						Tile *tile = getTile(x, y, 7);
						if (tile)
						{
							for (cit = tile->creatures.begin(); cit != tile->creatures.end(); cit++)
							{
								if ((*cit)->isPlayer())
								{
									Player *p = (Player*)(*cit);
									if(p->CanSee(player->pos.x, player->pos.y)) {
										p->sendNetworkMessage(&msg);
									}
								}
							}
						}
					}
				}		
			}
	}
	else if(strcmp(text.c_str(), "exevo gran mas vis") == 0) {

			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)((base * 2.3) - 30) / 2;
		int max = (int)((base * 3.0)) / 2;
		makeCastSpell(player, 800, min, max, area, 1, NM_ME_EXPLOSION_AREA, NM_ME_EXPLOSION_DAMAGE);
	}
	else if(strcmp(text.c_str(), "exura gran mas res") == 0) {

			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)((base * 2.3) - 30);
		int max = (int)((base * 3.0));
		makeCastSpell(player, 400, -min, -max, area, 1, NM_ME_MAGIC_ENERGIE, NM_ME_MAGIC_ENERGIE);
	}
	else if(strcmp(text.c_str(), "exori vis") == 0) {

			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		unsigned char ch = 0xFF;

		switch(player->direction) {
			case NORTH: ch = 1; break;
			case WEST: ch = 2; break;
			case EAST: ch = 3; break;
			case SOUTH: ch = 4; break;
		};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)(base * 1.1) / 2;
		int max = (int)(base * 1.0) / 2;
		makeCastSpell(player, 20, min, max, area, ch, NM_ME_ENERGY_AREA, NM_ME_ENERGY_DAMAGE);
	}
	else if(strcmp(text.c_str(), "exori mort") == 0) {

			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		unsigned char ch = 0xFF;

		switch(player->direction) {
			case NORTH: ch = 1; break;
			case WEST: ch = 2; break;
			case EAST: ch = 3; break;
			case SOUTH: ch = 4; break;
		};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)(base * 1.1) / 2;
		int max = (int)(base * 1.0) / 2;
		makeCastSpell(player, 20, min, max, area, ch, NM_ME_MORT_AREA, NM_ME_MORT_AREA);
	}
	else if(strcmp(text.c_str(), "exori") == 0) {
			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)(base * 2.0) / 2;
		int max = (int)(base * 2.5) / 2;
		makeCastSpell(player, 250, min, max, area, 1, 	NM_ME_HIT_AREA, NM_ME_HIT_AREA);
	}
	else if(strcmp(text.c_str(), "exevo mort hur") == 0) {
			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 2, 2, 2, 0, 0, 1, 0, 0, 3, 3, 3, 0, 0, 0},
				{0, 0, 0, 2, 2, 2, 2, 2, 0, 3, 3, 3, 3, 3, 0, 0, 0},
				{0, 0, 0, 2, 2, 2, 0, 0, 4, 0, 0, 3, 3, 3, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		unsigned char ch = 0xFF;

		switch(player->direction) {
			case NORTH: ch = 1; break;
			case WEST: ch = 2; break;
			case EAST: ch = 3; break;
			case SOUTH: ch = 4; break;
		};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)(base * 2.0) / 2;
		int max = (int)(base * 2.5) / 2;
		makeCastSpell(player, 250, min, max, area, ch, NM_ME_ENERGY_AREA, NM_ME_ENERGY_DAMAGE);
	}
	else if(strcmp(text.c_str(), "exevo vis lux") == 0) {
			static unsigned char area[14][18] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 2, 2, 2, 2, 0, 3, 3, 3, 3, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

		unsigned char ch = 0xFF;

		switch(player->direction) {
			case NORTH: ch = 1; break;
			case WEST: ch = 2; break;
			case EAST: ch = 3; break;
			case SOUTH: ch = 4; break;
		};

		int base = (int)player->level * 2 + player->maglevel * 3;
		int min = (int)(base * 1.2) / 2;
		int max = (int)(base * 2.0) / 2;
		makeCastSpell(player, 100, min, max, area, ch, NM_ME_ENERGY_AREA, NM_ME_ENERGY_DAMAGE);
	}

	OTSYS_THREAD_UNLOCK(mapLock)
}
