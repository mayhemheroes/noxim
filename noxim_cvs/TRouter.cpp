/*****************************************************************************

  TRouter.cpp -- Router implementation

*****************************************************************************/

#include "TRouter.h"

//---------------------------------------------------------------------------

void TRouter::rxProcess()
{
    if(reset.read())
    {
	// Clear outputs and indexes of receiving protocol
	for(int i=0; i<DIRECTIONS+1; i++)
	{
	    ack_rx[i].write(0);
	    current_level_rx[i] = 0;
	    reservation_table[i] = NOT_RESERVED;
	}
    }
    else
    {
	// For each channel decide if a new flit can be accepted
	//
	// This process simply sees a flow of incoming flits. All arbitration
	// and wormhole related issues are addressed in the txProcess()

	for(int i=0; i<DIRECTIONS+1; i++)
	{
	    // To accept a new flit, the following conditions must match:
	    //
	    // 1) there is an incoming request
	    // 2) there is a free slot in the input buffer of direction i

	    if ( (req_rx[i].read()==1-current_level_rx[i]) && !buffer[i].IsFull() )
	    {
		TFlit received_flit = flit_rx[i].read();

		if(TGlobalParams::verbose_mode > VERBOSE_OFF)
		{
		    cout << sc_simulation_time() << ": Router[" << local_id <<"], Buffer["<< i << "], RECEIVED " << received_flit << endl;
		}

		// Store the incoming flit in the circular buffer
		buffer[i].Push(received_flit);            

		// Negate the old value for Alternating Bit Protocol (ABP)
		current_level_rx[i] = 1-current_level_rx[i];
	    }
	    ack_rx[i].write(current_level_rx[i]);
	}
    }
}

//---------------------------------------------------------------------------

void TRouter::txProcess()
{
  static int start_from_direction = 0;

  if(reset.read())
    {
      // Clear outputs and indexes of transmitting protocol
      for(int i=0; i<DIRECTIONS+1; i++)
	{
	  req_tx[i].write(0);
	  current_level_tx[i] = 0;
	}
    }
  else
    {
      // For each channel see if it is possible to send a flit to its destination
      for(int j=0; j<DIRECTIONS+1; j++)
	{
	  int i = (start_from_direction + j) % (DIRECTIONS + 1);

	  // To send a flit the following conditions must match:
	  //
	  // 1) there is a new flit in the buffer that needs to be sent (look at the indexes)
	  // 2) if the destination got an initiated packet, only can continue with it 
	  // 3) if the destination completed the last packet, then can accept a new one 

	  if ( !buffer[i].IsEmpty() )
	    {
	      int dest; // temporary to store current

	      if(TGlobalParams::verbose_mode > VERBOSE_OFF)
                {
                  cout << sc_simulation_time() << ": Router[" << local_id << "], Buffer[" << i << "](" << buffer[i].Size() << " flits)" << endl;
                }

	      TFlit flit = buffer[i].Front();

	      if (flit.flit_type==FLIT_TYPE_HEAD) 
		{
		  // prepare data for routing
		  TRouteData route_data;
		  route_data.current_id = local_id;
		  route_data.src_id = flit.src_id;
		  route_data.dst_id = flit.dst_id;
		  route_data.dir_in = i;

		  dest = route(route_data);
		  if (reservation_table[dest] == NOT_RESERVED) 
		    {
		      short_circuit[i] = dest;     // crossbar: link input i to output dest 
		      reservation_table[dest] = i; // crossbar: reserve the output channel
		    }
		}
	      else dest = short_circuit[i];  // previously set by header flit

	      if (reservation_table[dest] == i)  // current flit belong to the worm that reserved the output
		{
		  if ( current_level_tx[dest] == ack_tx[dest].read() )
		    {
                      if(TGlobalParams::verbose_mode > VERBOSE_OFF)
			{
			  cout << sc_simulation_time() << ": Router[" << local_id << "] SENDING " << flit << " towards port " << dest << endl;
			}

		      flit_tx[dest].write(flit);
		      current_level_tx[dest] = 1 - current_level_tx[dest];
		      req_tx[dest].write(current_level_tx[dest]);
		      buffer[i].Pop();

		      if (flit.flit_type==FLIT_TYPE_TAIL) reservation_table[short_circuit[i]] = NOT_RESERVED;
			
		      // Update stats
		      if (dest == DIRECTION_LOCAL)
			stats.receivedFlit(sc_simulation_time(), flit);
		    }
		}

	    } // if buffer
	} // for
      start_from_direction++;
    } // else
}

//---------------------------------------------------------------------------

TNoP_data TRouter::getCurrentNoPData() const 
{
    TNoP_data NoP_data;

    for (int j=0; j<DIRECTIONS; j++)
    {
	NoP_data.channel_status_neighbor[j].buffer_level = buffer_level_neighbor[j].read();
	NoP_data.channel_status_neighbor[j].available = (reservation_table[j]==NOT_RESERVED);
    }

    NoP_data.sender_id = local_id;

    return NoP_data;
}

//---------------------------------------------------------------------------

void TRouter::bufferMonitor()
{
  if (reset.read())
  {
    // upon reset, buffer level is put to 0
    for (int i=0; i<DIRECTIONS+1; i++) buffer_level[i].write(0);
  }
  else
  {

    if (TGlobalParams::selection_strategy==SEL_BUFFER_LEVEL ||
	TGlobalParams::selection_strategy==SEL_NOP)
    {

      // update current input buffers level to neighbors
      for (int i=0; i<DIRECTIONS+1; i++)
	buffer_level[i].write(buffer[i].Size());

      // NoP selection: send neighbor info to each direction 'i'
      TNoP_data current_NoP_data = getCurrentNoPData();

      for (int i=0; i<DIRECTIONS; i++)
	NoP_data_out[i].write(current_NoP_data);

      if (TGlobalParams::verbose_mode == -57) 
	  NoP_report();
    }
  }
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingFunction(const TRouteData& route_data) 
{
  TCoord position  = id2Coord(route_data.current_id);
  TCoord src_coord = id2Coord(route_data.src_id);
  TCoord dst_coord = id2Coord(route_data.dst_id);
  int dir_in = route_data.dir_in;

  switch (TGlobalParams::routing_algorithm)
    {
    case ROUTING_XY:
      return routingXY(position, dst_coord);

    case ROUTING_WEST_FIRST:
      return routingWestFirst(position, dst_coord);

    case ROUTING_NORTH_LAST:
      return routingNorthLast(position, dst_coord);

    case ROUTING_NEGATIVE_FIRST:
      return routingNegativeFirst(position, dst_coord);

    case ROUTING_ODD_EVEN:
      return routingOddEven(position, src_coord, dst_coord);

    case ROUTING_DYAD:
      return routingDyAD(position, dst_coord);

    case ROUTING_FULLY_ADAPTIVE:
      return routingFullyAdaptive(position, dst_coord);

    case ROUTING_TABLE_BASED:
      return routingTableBased(dir_in, position, dst_coord);

    default:
      assert(false);
    }

  // something weird happened, you shouldn't be here
  return (vector<int>)(0);
}

//---------------------------------------------------------------------------

int TRouter::route(const TRouteData& route_data)
{
  if (route_data.dst_id == local_id)
    return DIRECTION_LOCAL;

  vector<int> candidate_channels = routingFunction(route_data);

  return selectionFunction(candidate_channels,route_data);
}

//---------------------------------------------------------------------------

void TRouter::NoP_report() const
{
    TNoP_data NoP_tmp;
      cout << sc_simulation_time() << ": Router[" << local_id << "], NoP report: " << endl;

      for (int i=0;i<DIRECTIONS; i++) 
      {
	  NoP_tmp = NoP_data_in[i].read();
	  if (NoP_tmp.sender_id!=NOT_VALID)
	    cout << NoP_tmp;
      }
}
//---------------------------------------------------------------------------

int TRouter::selectionNoP(const vector<int>& directions, const TRouteData& route_data)
{
  /*
  vector<int> neighbors_on_path;

  for (int i=0; i<directions.size(); i++)
  {
    // get id of adjacent candidate
    candidate_id = getNeighborId(current_id,directions[i]);

  // apply routing function to adjacent nodes on path
    vector<int> next_candidate_channels = routingFunction(reflexDirection(directions[i]),candidate_id,dst_id);

  }
  */
    return directions[rand() % directions.size()]; 
}

//---------------------------------------------------------------------------

int TRouter::selectionBufferLevel(const vector<int>& directions)
{
    // TODO: unfair if multiple directions have same buffer level
    // TODO: to check when both available

    unsigned int max_free_positions = 0;
    int direction_choosen = NOT_VALID;

    for (unsigned int i=0;i<directions.size();i++)
    {
	uint free_positions = buffer_depth - buffer_level_neighbor[directions[i]].read();
	if ((free_positions >= max_free_positions) &&
		(reservation_table[directions[i]] == NOT_RESERVED) )
	{
	    direction_choosen = directions[i];
	    max_free_positions = free_positions;
	}
    }

    // No available channel 
    if (direction_choosen==NOT_VALID)
	direction_choosen = directions[rand() % directions.size()]; 

    if(TGlobalParams::verbose_mode>VERBOSE_OFF)
    {
	TChannelStatus tmp;

	cout << sc_simulation_time() << ": Router[" << local_id << "], SELECTION between: " << endl;
	for (unsigned int i=0;i<directions.size();i++)
	{
	    tmp.buffer_level = buffer_level_neighbor[directions[i]].read();
	    tmp.available = (reservation_table[directions[i]]==NOT_RESERVED);
	    cout << "    -> direction " << directions[i] << ", channel status: " << tmp << endl;
	}
	cout << " direction choosen: " << direction_choosen << endl;
    }

    assert(direction_choosen>=0);
    return direction_choosen;
}
//---------------------------------------------------------------------------

int TRouter::selectionRandom(const vector<int>& directions)
{
  return directions[rand() % directions.size()]; 
}

//---------------------------------------------------------------------------

int TRouter::selectionFunction(const vector<int>& directions, const TRouteData& route_data)
{
  // not so elegant but fast escape ;)
    if (directions.size()==1) return directions[0];

    switch (TGlobalParams::selection_strategy)
    {
	case SEL_RANDOM:
	    return selectionRandom(directions);
	case SEL_BUFFER_LEVEL:
	    return selectionBufferLevel(directions);
	case SEL_NOP:
	    return selectionNoP(directions,route_data);
	default:
	    assert(false);
    }

    return 0;	    
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingXY(const TCoord& current, const TCoord& destination)
{
  vector<int> directions;
  
  if (destination.x > current.x)
    directions.push_back(DIRECTION_EAST);
  else if (destination.x < current.x)
    directions.push_back(DIRECTION_WEST);
  else if (destination.y > current.y)
    directions.push_back(DIRECTION_SOUTH);
  else
    directions.push_back(DIRECTION_NORTH);

  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingWestFirst(const TCoord& current, const TCoord& destination)
{
  vector<int> directions;

  if (destination.x <= current.x ||
      destination.y == current.y)
    return routingXY(current, destination);

  if (destination.y < current.y)
    {
      directions.push_back(DIRECTION_NORTH);
      directions.push_back(DIRECTION_EAST);
    }
  else
    {
      directions.push_back(DIRECTION_SOUTH);
      directions.push_back(DIRECTION_EAST);
    }

  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingNorthLast(const TCoord& current, const TCoord& destination)
{
  vector<int> directions;

  if (destination.x == current.x ||
      destination.y <= current.y)
    return routingXY(current, destination);

  if (destination.x < current.x)
    {
      directions.push_back(DIRECTION_SOUTH);
      directions.push_back(DIRECTION_WEST);
    }
  else
    {
      directions.push_back(DIRECTION_SOUTH);
      directions.push_back(DIRECTION_EAST);
    }

  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingNegativeFirst(const TCoord& current, const TCoord& destination)
{
  vector<int> directions;

  if ( (destination.x <= current.x && destination.y <= current.y) ||
       (destination.x >= current.x && destination.y >= current.y) )
    return routingXY(current, destination);

  if (destination.x > current.x && 
      destination.y < current.y)
    {
      directions.push_back(DIRECTION_NORTH);
      directions.push_back(DIRECTION_EAST);
    }
  else
    {
      directions.push_back(DIRECTION_SOUTH);
      directions.push_back(DIRECTION_WEST);
    }

  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingOddEven(const TCoord& current, 
				    const TCoord& source, const TCoord& destination)
{
  vector<int> directions;

  int c0 = current.x;
  int c1 = current.y;
  int s0 = source.x;
  //  int s1 = source.y;
  int d0 = destination.x;
  int d1 = destination.y;
  int e0, e1;

  e0 = d0 - c0;
  e1 = -(d1 - c1);

  if (e0 == 0)
    {
      if (e1 > 0)
	directions.push_back(DIRECTION_NORTH);
      else
	directions.push_back(DIRECTION_SOUTH);
    }
  else
    {
      if (e0 > 0)
	{
	  if (e1 == 0)
	    directions.push_back(DIRECTION_EAST);
	  else
	    {
	      if ( (c0 % 2 == 1) || (c0 == s0) )
		{
		  if (e1 > 0)
		    directions.push_back(DIRECTION_NORTH);
		  else
		    directions.push_back(DIRECTION_SOUTH);
		}
	      if ( (d0 % 2 == 1) || (e0 != 1) )
		directions.push_back(DIRECTION_EAST);
	    }
	}
      else
	{
	  directions.push_back(DIRECTION_WEST);
	  if (c0 % 2 == 0)
	    {
	      if (e1 > 0)
		directions.push_back(DIRECTION_NORTH);
	      else
		directions.push_back(DIRECTION_SOUTH);
	    }
	}
    }
  
  assert(directions.size() > 0 && directions.size() <= 2);
  
  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingDyAD(const TCoord& current, const TCoord& destination)
{
  vector<int> directions;

  assert(false);
  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingFullyAdaptive(const TCoord& current, const TCoord& destination)
{
  vector<int> directions;

  if (destination.x == current.x ||
      destination.y == current.y)
    return routingXY(current, destination);

  if (destination.x > current.x &&
      destination.y < current.y)
    {
      directions.push_back(DIRECTION_NORTH);
      directions.push_back(DIRECTION_EAST);
    }
  else if (destination.x > current.x &&
	   destination.y > current.y)
    {
      directions.push_back(DIRECTION_SOUTH);
      directions.push_back(DIRECTION_EAST);
    }
  else if (destination.x < current.x &&
	   destination.y > current.y)
    {
      directions.push_back(DIRECTION_SOUTH);
      directions.push_back(DIRECTION_WEST);
    }
  else
    {
      directions.push_back(DIRECTION_NORTH);
      directions.push_back(DIRECTION_WEST);
    }
  
  return directions;
}

//---------------------------------------------------------------------------

vector<int> TRouter::routingTableBased(const int dir_in, const TCoord& current, const TCoord& destination)
{
  TAdmissibleOutputs ao = rtable.getAdmissibleOutputs(dir_in, coord2Id(destination));
  
  assert(ao.size() > 0);

  //-----
  /*
  vector<int> aov = admissibleOutputsSet2Vector(ao);
  cout << "dir: " << dir_in << ", (" << current.x << "," << current.y << ") --> "
       << "(" << destination.x << "," << destination.y << "), outputs: ";
  for (int i=0; i<aov.size(); i++)
    cout << aov[i] << ", ";
  cout << endl;
  */
  //-----

  return admissibleOutputsSet2Vector(ao);
}

//---------------------------------------------------------------------------

void TRouter::configure(const int _id, 
			const double _warm_up_time,
			TGlobalRoutingTable& grt)
{
  local_id = _id;
  stats.configure(_id, _warm_up_time);
  
  if (grt.isValid())
    rtable.configure(grt, _id);
}

//---------------------------------------------------------------------------