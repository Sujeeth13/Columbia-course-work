package paxos

//
// Paxos library, to be included in an application.
// Multiple applications will run, each including
// a Paxos peer.
//
// Manages a sequence of agreed-on values.
// The set of peers is fixed.
// Copes with network failures (partition, msg loss, &c).
// Does not store anything persistently, so cannot handle crash+restart.
//
// The application interface:
//
// px = paxos.Make(peers []string, me string)
// px.Start(seq int, v interface{}) -- start agreement on new instance
// px.Status(seq int) (decided bool, v interface{}) -- get info about an instance
// px.Done(seq int) -- ok to forget all instances <= seq
// px.Max() int -- highest instance seq known, or -1
// px.Min() int -- instances before this seq have been forgotten
//

import "net"
import "net/rpc"
import "log"
import "os"
import "syscall"
import "sync"
import "fmt"
import "math/rand"


type Paxos struct {
  mu sync.Mutex
  l net.Listener
  dead bool
  unreliable bool
  rpcCount int
  peers []string
  me int // index into peers[]


  // Your data here.
  instances map[int]*State
  doneSeqs []int
}

// state of the paxos instance
type State struct {
  n_p int
  n_a int
  v_a interface{}
  done bool
  v interface{}
}

//
// call() sends an RPC to the rpcname handler on server srv
// with arguments args, waits for the reply, and leaves the
// reply in reply. the reply argument should be a pointer
// to a reply structure.
//
// the return value is true if the server responded, and false
// if call() was not able to contact the server. in particular,
// the replys contents are only valid if call() returned true.
//
// you should assume that call() will time out and return an
// error after a while if it does not get a reply from the server.
//
// please use call() to send all RPCs, in client.go and server.go.
// please do not change this function.
//
func call(srv string, name string, args interface{}, reply interface{}) bool {
  c, err := rpc.Dial("unix", srv)
  if err != nil {
    err1 := err.(*net.OpError)
    if err1.Err != syscall.ENOENT && err1.Err != syscall.ECONNREFUSED {
      fmt.Printf("paxos Dial() failed: %v\n", err1)
    }
    return false
  }
  defer c.Close()
    
  err = c.Call(name, args, reply)
  if err == nil {
    return true
  }

  fmt.Println(err)
  return false
}

type PaxosArgs struct {
  Seq int
  N int
  V interface{}
  Peer int
  Done int
}

type PaxosReply struct {
  N int
  V interface{}
  OK bool
}

func (px *Paxos) Prepare(args *PaxosArgs, reply *PaxosReply) error {
  px.mu.Lock()
  defer px.mu.Unlock()

  n := args.N
  seq := args.Seq
  peer := args.Peer
  done := args.Done

  // update the peer with current done seq and do cleanup
  px.UpdateAndCleanup(peer,done)

  state, exists := px.instances[seq]
  if !exists {
    state = &State{n_p: -1,n_a: -1, done: false}
  }

  if n > state.n_p {
    state.n_p = n
    reply.OK = true
    reply.N = state.n_a
    reply.V = state.v_a
    px.instances[seq] = state
  } else {
    reply.OK = false
  }
  return nil
}

func (px *Paxos) Accept(args *PaxosArgs, reply *PaxosReply) error {
  px.mu.Lock()
  defer px.mu.Unlock()

  n := args.N
  seq := args.Seq
  v := args.V
  peer := args.Peer
  done := args.Done

  // update the peer with current done seq and do cleanup
  px.UpdateAndCleanup(peer,done)

  state,exists := px.instances[seq]
  if !exists {
    state = &State{n_p: -1,n_a: -1, done: false}
  }

  reply.N = n
  reply.V = v

  if n >= state.n_p {
    state.n_p = n
    state.n_a = n
    state.v_a = v
    reply.OK = true
    px.instances[seq] = state
  } else {
    reply.OK = false
  }

  return nil
}

func (px *Paxos) Decide(args *PaxosArgs, reply *PaxosReply) error {
  px.mu.Lock()
  defer px.mu.Unlock()

  seq := args.Seq
  v := args.V
  peer := args.Peer
  done := args.Done

  // update the peer with current done seq and do cleanup
  px.UpdateAndCleanup(peer,done)

  state,exists := px.instances[seq]

  if !exists {
    state = &State{n_p: -1,n_a: -1, done: false}
  }
  state.done = true
  state.v_a = v
  state.v = v
  reply.OK = true
  px.instances[seq] = state

  return nil
}

func (px *Paxos) proposer(seq int, v interface{}) {

  majority := (len(px.peers)/2) + 1
  done := false
  n := (1 << 12) | px.me // start proposal id for each peer is unique

  args := &PaxosArgs{}
  args.Seq = seq
  args.V = v
  args.Peer = px.me

  for !done && !px.dead {
    proposedCnt := 0
    maxN_a := -1
    var maxV_a interface{}
    args.N = n
    args.Done = px.doneSeqs[px.me]
    // propose phase
    acks := make(chan *PaxosReply,len(px.peers))
    for i,peer := range px.peers {
      reply := &PaxosReply{}
      // send prepare messages async
      go func(i int, peer string, args *PaxosArgs, reply *PaxosReply) {
        if i == px.me {
          px.Prepare(args,reply)
        } else {
          call(peer, "Paxos.Prepare", args, reply)
        }
        acks <- reply
      } (i,peer,args,reply)
    }
    // count number of prepare oks
    for i := 0; i < len(px.peers); i++ {
      replyACK := <-acks
      if replyACK.OK {
        proposedCnt++
        if maxN_a < replyACK.N {
          maxN_a = replyACK.N
          maxV_a = replyACK.V
        }
      }
    }
    // accept phase
    if proposedCnt >= majority {
      if maxV_a != nil {
				args.V = maxV_a
			}
      acceptCnt := 0
      acks := make(chan *PaxosReply,len(px.peers))
      for i,peer := range px.peers {
        reply := &PaxosReply{}
        // send accept async
        go func(i int,peer string,args *PaxosArgs,reply *PaxosReply) {
          if i == px.me {
            px.Accept(args,reply)
          } else {
            call(peer, "Paxos.Accept",args,reply)
          }
          acks <- reply
        }(i,peer,args,reply)
      }

      // if acceptor lets us know there is a higher proposer then update current proposer n
      if n < maxN_a {
        n = maxN_a
      }

      for i := 0; i < len(px.peers); i++ {
        replyACK := <-acks
        if replyACK.OK {
          acceptCnt++
        }
      }

      if acceptCnt >= majority {
        done = true
        break
      }
    }
    // update the proposer id if current proposal not accepted
    n++
  }
  // decide phase
  args.Done = px.doneSeqs[px.me]
  for i,peer := range px.peers {
    reply := &PaxosReply{}
    if i == px.me {
      px.Decide(args,reply)
    } else {
      call(peer,"Paxos.Decide",args,reply)
    }
  }
}
//
// the application wants paxos to start agreement on
// instance seq, with proposed value v.
// Start() returns right away; the application will
// call Status() to find out if/when agreement
// is reached.
//
func (px *Paxos) Start(seq int, v interface{}) {
  // Your code here.
  px.mu.Lock()
  defer px.mu.Unlock()

  _, exists := px.instances[seq]
  if !exists {
    px.instances[seq] = &State{n_p: -1,n_a: -1, done: false}
  }
  go px.proposer(seq,v)
}

//
// the application on this machine is done with
// all instances <= seq.
//
// see the comments for Min() for more explanation.
//
func (px *Paxos) Done(seq int) {
  // Your code here.
  px.mu.Lock()
  defer px.mu.Unlock()

  px.UpdateAndCleanup(px.me,seq)
}

//
// the application wants to know the
// highest instance sequence known to
// this peer.
//
func (px *Paxos) Max() int {
  // Your code here.
  px.mu.Lock()
	defer px.mu.Unlock()

	maxSeq := -1
	for seq := range px.instances {
		if seq > maxSeq {
			maxSeq = seq
		}
	}
	return maxSeq
}

//
// Min() should return one more than the minimum among z_i,
// where z_i is the highest number ever passed
// to Done() on peer i. A peers z_i is -1 if it has
// never called Done().
//
// Paxos is required to have forgotten all information
// about any instances it knows that are < Min().
// The point is to free up memory in long-running
// Paxos-based servers.
//
// Paxos peers need to exchange their highest Done()
// arguments in order to implement Min(). These
// exchanges can be piggybacked on ordinary Paxos
// agreement protocol messages, so it is OK if one
// peers Min does not reflect another Peers Done()
// until after the next instance is agreed to.
//
// The fact that Min() is defined as a minimum over
// *all* Paxos peers means that Min() cannot increase until
// all peers have been heard from. So if a peer is dead
// or unreachable, other peers Min()s will not increase
// even if all reachable peers call Done. The reason for
// this is that when the unreachable peer comes back to
// life, it will need to catch up on instances that it
// missed -- the other peers therefor cannot forget these
// instances.
// 
func (px *Paxos) Min() int {
  // You code here.
  minSeq := px.doneSeqs[0]
  for _,seq := range px.doneSeqs {
    if seq < minSeq {
      minSeq = seq
    }
  }
  return minSeq + 1
}

//
// the application wants to know whether this
// peer thinks an instance has been decided,
// and if so what the agreed value is. Status()
// should just inspect the local peer state;
// it should not contact other Paxos peers.
//
func (px *Paxos) Status(seq int) (bool, interface{}) {
  // Your code here.
	px.mu.Lock()
	defer px.mu.Unlock()

	inst := px.instances[seq]
	if inst != nil && inst.done {
		return inst.done, inst.v
	}
  return false, nil
}

// clean up the memory of instances and update done peer seq
func (px* Paxos) UpdateAndCleanup(peer int, seq int) {
  // update only if seq is greater than curr done seq
  if px.doneSeqs[peer] < seq {
    px.doneSeqs[peer] = seq
    minSeq := px.Min()
    for seq := range px.instances {
      if seq < minSeq {
        delete(px.instances,seq)
      }
    }
  }
}
//
// tell the peer to shut itself down.
// for testing.
// please do not change this function.
//
func (px *Paxos) Kill() {
  px.dead = true
  if px.l != nil {
    px.l.Close()
  }
}

//
// the application wants to create a paxos peer.
// the ports of all the paxos peers (including this one)
// are in peers[]. this servers port is peers[me].
//
func Make(peers []string, me int, rpcs *rpc.Server) *Paxos {
  px := &Paxos{}
  px.peers = peers
  px.me = me


  // Your initialization code here.
  px.instances = make(map[int]*State)
  px.doneSeqs = make([]int, len(peers))
  for i := range px.doneSeqs {
    px.doneSeqs[i]= -1
  }

  if rpcs != nil {
    // caller will create socket &c
    rpcs.Register(px)
  } else {
    rpcs = rpc.NewServer()
    rpcs.Register(px)

    // prepare to receive connections from clients.
    // change "unix" to "tcp" to use over a network.
    os.Remove(peers[me]) // only needed for "unix"
    l, e := net.Listen("unix", peers[me]);
    if e != nil {
      log.Fatal("listen error: ", e);
    }
    px.l = l
    
    // please do not change any of the following code,
    // or do anything to subvert it.
    
    // create a thread to accept RPC connections
    go func() {
      for px.dead == false {
        conn, err := px.l.Accept()
        if err == nil && px.dead == false {
          if px.unreliable && (rand.Int63() % 1000) < 100 {
            // discard the request.
            conn.Close()
          } else if px.unreliable && (rand.Int63() % 1000) < 200 {
            // process the request but force discard of reply.
            c1 := conn.(*net.UnixConn)
            f, _ := c1.File()
            err := syscall.Shutdown(int(f.Fd()), syscall.SHUT_WR)
            if err != nil {
              fmt.Printf("shutdown: %v\n", err)
            }
            px.rpcCount++
            go rpcs.ServeConn(conn)
          } else {
            px.rpcCount++
            go rpcs.ServeConn(conn)
          }
        } else if err == nil {
          conn.Close()
        }
        if err != nil && px.dead == false {
          fmt.Printf("Paxos(%v) accept: %v\n", me, err.Error())
        }
      }
    }()
  }


  return px
}
