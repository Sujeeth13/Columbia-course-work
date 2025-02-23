package kvpaxos

import "net"
import "fmt"
import "net/rpc"
import "log"
import "paxos"
import "sync"
import "os"
import "syscall"
import "encoding/gob"
import "math/rand"
import "strconv"
import "time"

const Debug=0

func DPrintf(format string, a ...interface{}) (n int, err error) {
  if Debug > 0 {
    log.Printf(format, a...)
  }
  return
}


type Op struct {
  // Your definitions here.
  // Field names must start with capital letters,
  // otherwise RPC will break.
  Operation string
  Key string
  Value string
  DoHash bool
  ReqID int64
}

type KVPaxos struct {
  mu sync.Mutex
  l net.Listener
  me int
  dead bool // for testing
  unreliable bool // for testing
  px *paxos.Paxos

  // Your definitions here.
  doneReqs map[int64]string
  kvStore map[string]string
  lastDoneReq int
}

func (kv *KVPaxos) Get(args *GetArgs, reply *GetReply) error {
  op := Op{
    Operation: "Get",
    Key: args.Key,
    ReqID: args.ReqID,
  }
  // reflect the changes on the kv
  reply.Value = kv.applyChanges(op)
  // free the done reqs cache since operations has been reflected in kv
  kv.Cleanup(args.DoneID)
  return nil
}

func (kv *KVPaxos) Put(args *PutArgs, reply *PutReply) error {
  op := Op{
    Operation: "Put",
    Key: args.Key,
    Value: args.Value,
    DoHash: args.DoHash,
    ReqID: args.ReqID,
  }
   // reflect the changes on the kv
  reply.PreviousValue = kv.applyChanges(op)
  // free the done reqs cache since operations has been reflected in db
  kv.Cleanup(args.DoneID)
  return nil
}

func (kv *KVPaxos) applyChanges(op Op) string {
  to := 10*time.Millisecond
  kv.mu.Lock()
  // check in donereqs cache to ans duplicates
  cacheVal, exists := kv.doneReqs[op.ReqID]
  if exists {
    kv.mu.Unlock()
    return cacheVal
  }
  // new request op
  var ret string
  for {
    seq := kv.lastDoneReq
    // check if there exists an entry in the log already thats need to be applied
    decided, value := kv.px.Status(seq)
    if decided {
      if to > 100*time.Millisecond {
				to = 100 * time.Millisecond
			}
      decidedVal := value.(Op)
      ret = ""
      // applying the current index log operation
      switch decidedVal.Operation {
      case "Get":
        ret = kv.kvStore[decidedVal.Key]
      case "Put":
        if decidedVal.DoHash {
          ret = kv.kvStore[decidedVal.Key]
          kv.kvStore[decidedVal.Key] = strconv.Itoa(int(hash(ret + decidedVal.Value)))
        } else {
          kv.kvStore[decidedVal.Key] = decidedVal.Value
        }
      }
      // cache the value to ans duplicates
      kv.doneReqs[decidedVal.ReqID] = ret
      // if decided op is our operation then exit since we have caught up to the log
      if op.ReqID == decidedVal.ReqID { 
        break
      }
      // if not our op then try to put the op in the next index of the log to catchup
      seq++
      kv.lastDoneReq++
    } else {
      // index is free in the log since no decided value, start proposal for current op
      kv.px.Start(seq,op)
      time.Sleep(to)
      // do exponential backoff to reduce rpc counts (need to tweak it!!)
      if to < 100*time.Millisecond {
				to *= time.Duration(2)
			}
    }
  }
  kv.px.Done(kv.lastDoneReq)
  kv.lastDoneReq++
  kv.mu.Unlock()
  return ret
}

func (kv *KVPaxos) Cleanup(ID int64) {
  if ID == 0 {
    return
  }
  kv.mu.Lock()
  defer kv.mu.Unlock()
  _, exists := kv.doneReqs[ID]
  if exists {
    delete(kv.doneReqs,ID)
  }
}

// tell the server to shut itself down.
// please do not change this function.
func (kv *KVPaxos) kill() {
  DPrintf("Kill(%d): die\n", kv.me)
  kv.dead = true
  kv.l.Close()
  kv.px.Kill()
}

//
// servers[] contains the ports of the set of
// servers that will cooperate via Paxos to
// form the fault-tolerant key/value service.
// me is the index of the current server in servers[].
// 
func StartServer(servers []string, me int) *KVPaxos {
  // call gob.Register on structures you want
  // Go's RPC library to marshall/unmarshall.
  gob.Register(Op{})

  kv := new(KVPaxos)
  kv.me = me

  // Your initialization code here.
  kv.doneReqs = make(map[int64]string)
  kv.kvStore = make(map[string]string)
  kv.lastDoneReq = 0

  rpcs := rpc.NewServer()
  rpcs.Register(kv)

  kv.px = paxos.Make(servers, me, rpcs)

  os.Remove(servers[me])
  l, e := net.Listen("unix", servers[me]);
  if e != nil {
    log.Fatal("listen error: ", e);
  }
  kv.l = l


  // please do not change any of the following code,
  // or do anything to subvert it.

  go func() {
    for kv.dead == false {
      conn, err := kv.l.Accept()
      if err == nil && kv.dead == false {
        if kv.unreliable && (rand.Int63() % 1000) < 100 {
          // discard the request.
          conn.Close()
        } else if kv.unreliable && (rand.Int63() % 1000) < 200 {
          // process the request but force discard of reply.
          c1 := conn.(*net.UnixConn)
          f, _ := c1.File()
          err := syscall.Shutdown(int(f.Fd()), syscall.SHUT_WR)
          if err != nil {
            fmt.Printf("shutdown: %v\n", err)
          }
          go rpcs.ServeConn(conn)
        } else {
          go rpcs.ServeConn(conn)
        }
      } else if err == nil {
        conn.Close()
      }
      if err != nil && kv.dead == false {
        fmt.Printf("KVPaxos(%v) accept: %v\n", me, err.Error())
        kv.kill()
      }
    }
  }()

  return kv
}

