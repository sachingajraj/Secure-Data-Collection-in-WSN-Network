/*
Copyright (c) 1997, 1998 Carnegie Mellon University.  All Rights
Reserved. 

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The AODV code developed by the CMU/MONARCH group was optimized and tuned by Samir Das and Mahesh Marina, University of Cincinnati. The work was partially done in Sun Microsystems. Modified for gratuitous replies by Anant Utgikar, 09/16/02.

*/

//#include <ip.h>
#include <iostream>
#include <aodv/aodv.h>
#include <aodv/aodv_packet.h>
#include <random.h>
#include <cmu-trace.h>
//#include <energy-model.h>


#include <god.h>  	// To find neighbour - because god knows every thing
#include <time.h>		// To generate random number 
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <common/packet.h>

int no_packet_recieved = 0;

#define max(a,b)        ( (a) > (b) ? (a) : (b) )
#define CURRENT_TIME    Scheduler::instance().clock()

//#define DEBUG
//#define ERROR

#ifdef DEBUG
static int route_request = 0;
#endif


/*
  TCL Hooks
*/


int hdr_aodv::offset_;
static class AODVHeaderClass : public PacketHeaderClass {
public:
        AODVHeaderClass() : PacketHeaderClass("PacketHeader/AODV",
                                              sizeof(hdr_all_aodv)) {
	  bind_offset(&hdr_aodv::offset_);
	} 
} class_rtProtoAODV_hdr;

static class AODVclass : public TclClass {
public:
        AODVclass() : TclClass("Agent/AODV") {}
        TclObject* create(int argc, const char*const* argv) {
          assert(argc == 5);
          //return (new AODV((nsaddr_t) atoi(argv[4])));
	  return (new AODV((nsaddr_t) Address::instance().str2addr(argv[4])));
        }
} class_rtProtoAODV;


int
AODV::command(int argc, const char*const* argv) {
	
	//printf(" printing in command of aodv \n");

  if(argc == 2) {
  Tcl& tcl = Tcl::instance();
    
    if(strncasecmp(argv[1], "id", 2) == 0) {
      tcl.resultf("%d", index);
      return TCL_OK;
    }
    
    if(strncasecmp(argv[1], "start", 2) == 0) {

			btimer.handle((Event*) 0);

//#ifndef AODV_LINK_LAYER_DETECTION
      htimer.handle((Event*) 0);
      ntimer.handle((Event*) 0);
//#endif // LINK LAYER DETECTION

      rtimer.handle((Event*) 0);
      return TCL_OK;
     }               
  }
  else if(argc == 3) {
    if(strcmp(argv[1], "index") == 0) {
      index = atoi(argv[2]);
      return TCL_OK;
    }

    else if(strcmp(argv[1], "log-target") == 0 || strcmp(argv[1], "tracetarget") == 0) {
      logtarget = (Trace*) TclObject::lookup(argv[2]);
      if(logtarget == 0)
	return TCL_ERROR;
      return TCL_OK;
    }
    else if(strcmp(argv[1], "drop-target") == 0) {
    int stat = rqueue.command(argc,argv);
      if (stat != TCL_OK) return stat;
      return Agent::command(argc, argv);
    }
    else if(strcmp(argv[1], "if-queue") == 0) {
    ifqueue = (PriQueue*) TclObject::lookup(argv[2]);
      
      if(ifqueue == 0)
	return TCL_ERROR;
      return TCL_OK;
    }
    else if (strcmp(argv[1], "port-dmux") == 0) {
		cout<< " port dmux " << index <<endl;
    	dmux_ = (PortClassifier *)TclObject::lookup(argv[2]);
	if (dmux_ == 0) {
		fprintf (stderr, "%s: %s lookup of %s failed\n", __FILE__,
		argv[1], argv[2]);
		return TCL_ERROR;
	}
	return TCL_OK;
    }
  }
  return Agent::command(argc, argv);
}

/* 
   Constructor				/// Sachin tujhe bhi Dekhna hai 
*/

AODV::AODV(nsaddr_t id) : Agent(PT_AODV),
			  btimer(this), htimer(this), ntimer(this), 
			  rtimer(this), /*lrtimer(this),*/ rqueue() {
 
                
  index = id;
  seqno = 2;
  bid = 1;

  LIST_INIT(&nbhead);
  LIST_INIT(&bihead);

  logtarget = 0;
  ifqueue = 0;
}

/*
  Timers
*/

void
BroadcastTimer::handle(Event*) {
  agent->id_purge();
  Scheduler::instance().schedule(this, &intr, BCAST_ID_SAVE);
}

void
HelloTimer::handle(Event*) {
   agent->sendHello();
   double interval = MinHelloInterval + 
                 ((MaxHelloInterval - MinHelloInterval) * Random::uniform());
   assert(interval >= 0);
   Scheduler::instance().schedule(this, &intr, interval);
}

void
NeighborTimer::handle(Event*) {
  agent->nb_purge();
  Scheduler::instance().schedule(this, &intr, HELLO_INTERVAL);
}

void
RouteCacheTimer::handle(Event*) {
  //agent->rt_purge();
//#define FREQUENCY 0.5 // sec
  //Scheduler::instance().schedule(this, &intr, FREQUENCY);
}

/*
   Broadcast ID Management  Functions
*/

void
AODV::id_purge() {
BroadcastID *b = bihead.lh_first;
BroadcastID *bn;
double now = CURRENT_TIME;

 for(; b; b = bn) {
   bn = b->link.le_next;
   if(b->expire <= now) {
     LIST_REMOVE(b,link);
     delete b;
   }
 }
}

/*
  Helper Functions
*/

double
AODV::PerHopTime(aodv_rt_entry *rt) {
int num_non_zero = 0, i;
double total_latency = 0.0;

 if (!rt)
   return ((double) NODE_TRAVERSAL_TIME );
	
 for (i=0; i < MAX_HISTORY; i++) {
   if (rt->rt_disc_latency[i] > 0.0) {
      num_non_zero++;
      total_latency += rt->rt_disc_latency[i];
   }
 }
 if (num_non_zero > 0)
   return(total_latency / (double) num_non_zero);
 else
   return((double) NODE_TRAVERSAL_TIME);

}

/*
  Packet Reception Routines
*/


void
AODV::recv(Packet* p, Handler* h) {
	struct hdr_cmn* ch = HDR_CMN(p);
	struct hdr_ip* ih = HDR_IP(p);
	
	cout<<ch->direction()<< " direction " << ih->daddr() << " dest addr " << index << " current node "<<ch->ptype()<< " packet typ "<<endl;
	
	if (ih->saddr() == index) {
		// If there exists a loop, must drop the packet
			if (ch->num_forwards() > 0) {
					cout<<" Drop This Packet Due to Loop : No of forwards "<<ch->num_forwards()<<endl;
					drop(p, DROP_RTR_ROUTE_LOOP);
					return;
			}
			// else if this is a packet I am originating, must add IP header
			else if (ch->num_forwards() == 0)
					ch->size() += IP_HDR_LEN;
	}
	
	// If it is a protoname packet, must process it
	if (ch->ptype() == PT_AODV)
			recvAODV(p);

	// Otherwise, must forward the packet (unless TTL has reached zero)
	else {
			ih->ttl_--;

			if (ih->ttl_ == 0) {		
					cout<<" Drop This Packet Due to TTL : "<<ih->ttl_<<endl;
					drop(p, DROP_RTR_TTL);	
					return;
			}
			forward((aodv_rt_entry*) 0, p, NO_DELAY);
	}
}

void
AODV::forward(aodv_rt_entry *rt, Packet *p, double delay){
	struct hdr_cmn* ch = HDR_CMN(p);
	struct hdr_ip* ih = HDR_IP(p);

	if (ih->ttl_ == 0){
			cout<< " TTL Goes 0 Here, Dropping Packet . "<<endl;
	}

	

	if (ch->direction() == hdr_cmn::UP && ((u_int32_t)ih->daddr() == IP_BROADCAST || ih->daddr() == here_.addr_)) {
			cout<<" packet recieved"<<endl;
			dmux_->recv(p,0);
			return;
	} else {
			ch->direction() = hdr_cmn::DOWN;
			ch->addr_type() = NS_AF_INET;
			if ((u_int32_t)ih->daddr() == IP_BROADCAST)
					ch->next_hop() = IP_BROADCAST;
			else {
					nsaddr_t next_hop = random_neighbour_nb_list(index,ch->prev_hop_,p);
			
					if (next_hop == index) {
							//debug("%f: Agent %d can not forward a packet destined to %d\n",CURRENT_TIME,index,ih->daddr());
							cout<< " No neighbour to current node "<<index<<endl;
					 		drop(p, DROP_RTR_NO_ROUTE);
							return;
					}
					else
							ch->next_hop() = next_hop;
				}

				Scheduler::instance().schedule(target_, p, 0.0);
	}
}

/* Random Neighbour by using neighbour list of node */

nsaddr_t AODV::random_neighbour_nb_list(nsaddr_t id,nsaddr_t prev_hop,Packet *p){
	int max = God::instance()->nodes(); // Number of nodes in network
	bool node_found = false;

	timeval time;
	gettimeofday(&time, NULL);
	//	long millis = (time.tv_sec * 1000) + (time.tv_usec / 1000);
	unsigned int iseed = (unsigned int)((time.tv_sec * 100) + (time.tv_usec));
	//	unsigned int iseed = (unsigned int)millis;
	srand (iseed);
	int rnd = 0;

	nsaddr_t nb_addr = id;

	//cout<<" Called by "<< index << endl;
	while(!node_found){
			rnd = rand()%max;
			//cout<<rnd<<endl;
			 
			if ( rnd != id && nb_lookup(rnd) != NULL && NIR_lookup(p , rnd)){
					NIR_insert(p , rnd); // append the node to list of all the node that the packet has travesed so far
					nb_addr = nb_lookup(rnd)->nb_addr;
					node_found = true;

			}
	}
	return nb_addr;
}

/* Random Neighbour of a Node by God */

nsaddr_t AODV::random_neighbour_bygod(nsaddr_t id){
	int max = God::instance()->nodes(); // Number of nodes in network
	bool node_found = false;
	unsigned int iseed = (unsigned int)time(NULL);
	srand (iseed);
	int rnd = 0;
	nsaddr_t nb_addr = id;
	
	while(!node_found){
			rnd = rand()%max;
			if ( rnd != id && God::instance()->IsNeighbor(id,rnd)	){
				nb_addr = rnd;
				node_found = true;
			}
	}
	return nb_addr;
}


void
AODV::recvAODV(Packet *p) {
 struct hdr_aodv *ah = HDR_AODV(p);

 assert(HDR_IP (p)->sport() == RT_PORT);
 assert(HDR_IP (p)->dport() == RT_PORT);

 /*
  * Incoming Packets.
  */
 switch(ah->ah_type) {

 case AODVTYPE_HELLO:
   recvHello(p);
   break;
        
 default:
   fprintf(stderr, "Invalid AODV type (%x)\n", ah->ah_type);
   exit(1);
 }

}


/*
   Neighbor Management Functions
*/

void
AODV::sendHello() {

Packet *p = Packet::alloc();
struct hdr_cmn *ch = HDR_CMN(p);
struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_reply *rh = HDR_AODV_REPLY(p);

#ifdef DEBUG
fprintf(stderr, "sending Hello from %d at %.2f\n", index, Scheduler::instance().clock());
#endif // DEBUG

 rh->rp_type = AODVTYPE_HELLO;
 //rh->rp_flags = 0x00;
 rh->rp_hop_count = 1;
 rh->rp_dst = index;
 rh->rp_dst_seqno = seqno;
 rh->rp_lifetime = (1 + ALLOWED_HELLO_LOSS) * HELLO_INTERVAL;

 // ch->uid() = 0;
 ch->ptype() = PT_AODV;
 ch->size() = IP_HDR_LEN + rh->size();
 ch->iface() = -2;
 ch->error() = 0;
 ch->addr_type() = NS_AF_NONE;
 ch->prev_hop_ = index;          // AODV hack

 ih->saddr() = index;
 ih->daddr() = IP_BROADCAST;
 ih->sport() = RT_PORT;
 ih->dport() = RT_PORT;
 ih->ttl_ = 1;

 Scheduler::instance().schedule(target_, p, 0.0);
}


void
AODV::recvHello(Packet *p) {
//struct hdr_ip *ih = HDR_IP(p);
struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
AODV_Neighbor *nb;

 nb = nb_lookup(rp->rp_dst);
 if(nb == 0) {
   nb_insert(rp->rp_dst);
 }
 else {
   nb->nb_expire = CURRENT_TIME +
                   (1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
 }

 Packet::free(p);
}

void
AODV::nb_insert(nsaddr_t id) {

AODV_Neighbor *nb = new AODV_Neighbor(id);

 assert(nb);
 nb->nb_expire = CURRENT_TIME +
                (1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
 LIST_INSERT_HEAD(&nbhead, nb, nb_link);
 seqno += 2;             // set of neighbors changed
 assert ((seqno%2) == 0);
}


AODV_Neighbor*
AODV::nb_lookup(nsaddr_t id) {
AODV_Neighbor *nb = nbhead.lh_first;

 for(; nb; nb = nb->nb_link.le_next) {
   if(nb->nb_addr == id) 	
				return nb;
 }
 return NULL;
}


/*
 * Called when we receive *explicit* notification that a Neighbor
 * is no longer reachable.
 */
void
AODV::nb_delete(nsaddr_t id) {
AODV_Neighbor *nb = nbhead.lh_first;

 log_link_del(id);
 seqno += 2;     // Set of neighbors changed
 assert ((seqno%2) == 0);

 for(; nb; nb = nb->nb_link.le_next) {
   if(nb->nb_addr == id) {
     LIST_REMOVE(nb,nb_link);
     delete nb;
     break;
   }
 }

 //handle_link_failure(id);

}


/*
 * Purges all timed-out Neighbor Entries - runs every
 * HELLO_INTERVAL * 1.5 seconds.
 */
void
AODV::nb_purge() {
AODV_Neighbor *nb = nbhead.lh_first;
AODV_Neighbor *nbn;
double now = CURRENT_TIME;

 for(; nb; nb = nbn) {
   nbn = nb->nb_link.le_next;
   if(nb->nb_expire <= now) {
     nb_delete(nb->nb_addr);
   }
 }

}

/*
 *	List Management for packets previous hops 
*/

void AODV::NIR_insert(Packet *p , nsaddr_t id){
	struct hdr_cmn *ch = HDR_CMN(p);
	NIR_node *nd = new NIR_node(id);
	LIST_INSERT_HEAD(&(ch->nir_list), nd, link);
	
}

bool AODV::NIR_lookup(Packet *p ,nsaddr_t id) {
	struct hdr_cmn *ch = HDR_CMN(p);

	if (ch->nir_list.lh_first == NULL)
		return true;

	NIR_node *nb = ch->nir_list.lh_first;	

 for(; nb; nb = nb->link.le_next) {
   if(nb->id_ == id) 	
				return false;
 }
 return true;
}





