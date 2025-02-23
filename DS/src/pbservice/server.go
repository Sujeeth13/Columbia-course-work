package pbservice

import "net"
import "fmt"
import "net/rpc"
import "log"
import "time"
import "viewservice"
import "os"
import "syscall"
import "math/rand"
import "sync"

import "strconv"

// Debugging
const Debug = 0

func DPrintf(format string, a ...interface{}) (n int, err error) {
  if Debug > 0 {
    n, err = fmt.Printf(format, a...)
  }
  return
}

type PBServer struct {
  l net.Listener
  dead bool // for testing
  unreliable bool // for testing
  me string
  vs *viewservice.Clerk
  done sync.WaitGroup
  finish chan interface{}
  // Your declarations here.
  mu sync.Mutex
  view viewservice.View
  isPrimary bool
  isBackup bool
  doneReqs map[int64]bool
  kv map[string]string
  okv map[string]string
}

func (pb *PBServer) SyncData(args *SyncArgs, reply *SyncReply) error {
  // code for sending data between servers
  pb.mu.Lock()
  defer pb.mu.Unlock()

  if !pb.isBackup {
    reply.Err = ErrWrongServer
    return nil
  }

  pb.kv = make(map[string]string)
  for k, v := range args.KV {
      pb.kv[k] = v
  }

  pb.okv = make(map[string]string)
  for k, v := range args.OKV {
      pb.okv[k] = v
  }

  pb.doneReqs = make(map[int64]bool)
  for id, done := range args.DoneReqs {
      pb.doneReqs[id] = done
  }
  reply.Err = OK
  return nil
}

func (pb *PBServer) Forward(args *ForwardArgs, reply *ForwardReply) error {
  // code for sending get request from primary to backup
  pb.mu.Lock()
  defer pb.mu.Unlock()

  if !pb.isBackup {
    reply.Err = ErrWrongServer
    return nil
  }
  reply.Err = OK
  return nil
}

func (pb *PBServer) Put(args *PutArgs, reply *PutReply) error {
  pb.mu.Lock()
  defer pb.mu.Unlock()
  
  key := args.Key
  value := args.Value
  doHash := args.DoHash
  id := args.ID

  if !pb.isPrimary {
    reply.Err = ErrWrongServer
    return nil
  }

  // duplicate request
  if _,done := pb.doneReqs[id]; done {
    // TODO: not sure what to return
    reply.Err = OK
    reply.PreviousValue = pb.okv[key]
    return nil
  }
  pb.doneReqs[id] = true
  prevVal , exists := pb.kv[key]
  if !exists {
    prevVal = ""
  }
  prevOldVal , oexists := pb.okv[key]
  if !oexists {
    prevOldVal = ""
  }
  // make changes to the kv store here
  newVal := value
  if doHash {
    newVal = strconv.Itoa(int(hash(prevVal + value)))
  }
  pb.okv[key] = prevVal
  pb.kv[key] = newVal

  if pb.view.Backup != "" {
    fwdArgs := &ForwardArgs{}
    var fwdReply ForwardReply
    // passing req to backup
    ok := call(pb.view.Backup, "PBServer.Forward", fwdArgs, &fwdReply)
    if ok {
      if fwdReply.Err == ErrWrongServer {
        // implies a change in the view
        reply.Err = ErrWrongServer
        // delete the changes to kv
        delete(pb.doneReqs,id)
        if prevVal != "" {
          pb.kv[key] = prevVal
          pb.okv[key] = prevOldVal
        } else {
          delete(pb.kv,key)
          delete(pb.okv,key)
        }
        return nil
      } else {
        syncArgs := &SyncArgs {KV : pb.kv, OKV : pb.okv, DoneReqs : pb.doneReqs}
        var syncReply SyncReply
        // syncing data with backup
        sok := call(pb.view.Backup,"PBServer.SyncData",syncArgs,&syncReply)
        if !sok {
         // sync with backup not happening
          reply.Err = ErrWrongServer
          // delete the changes to kv
          delete(pb.doneReqs,id)
          if prevVal != "" {
            pb.kv[key] = prevVal
            pb.okv[key] = prevOldVal
          } else {
            delete(pb.kv,key)
            delete(pb.okv,key)
          }
          return nil
        }
      }
    } else {
      // could not pass to backup even if it is part of view
      reply.Err = ErrWrongServer
      // delete the changes to kv
      delete(pb.doneReqs,id)
      if prevVal != "" {
        pb.kv[key] = prevVal
        pb.okv[key] = prevOldVal
      } else {
        delete(pb.kv,key)
        delete(pb.okv,key)
      }
      return nil
    }
  }
  reply.Err = OK
  reply.PreviousValue = prevVal
  return nil
}

func (pb *PBServer) Get(args *GetArgs, reply *GetReply) error {
  pb.mu.Lock()
  defer pb.mu.Unlock()

  key := args.Key
  id := args.ID

  if !pb.isPrimary {
    reply.Err = ErrWrongServer
    return nil
  }
  // TODO: Check for duplicate request
  if _, done := pb.doneReqs[id]; done {
    reply.Err = OK
    reply.Value = pb.kv[key]
    return nil
  } 
  pb.doneReqs[id] = true
  value, exists := pb.kv[key]
  if !exists {
    value = ""
  }
  if pb.view.Backup != "" {
    fwdArgs := &ForwardArgs{}
    var fwdReply ForwardReply
    // passing req to backup
    ok := call(pb.view.Backup, "PBServer.Forward", fwdArgs, &fwdReply)
    if ok {
      if fwdReply.Err == ErrWrongServer {
        // implies a change in the view
        reply.Err = ErrWrongServer
        delete(pb.doneReqs,id)
        return nil
      } else {
        syncArgs := &SyncArgs {KV : pb.kv, DoneReqs : pb.doneReqs}
        var syncReply SyncReply
        // syncing data with backup
        sok := call(pb.view.Backup,"PBServer.SyncData",syncArgs,&syncReply)
        if !sok {
         // sync with backup not happening
          reply.Err = ErrWrongServer
          delete(pb.doneReqs,id)
          return nil
        }
      }
    } else {
      // could not pass to backup even if it is part of view
      reply.Err = ErrWrongServer
      delete(pb.doneReqs,id)
      return nil
    }
  }
  if value == "" {
    reply.Err = ErrNoKey
  } else {
    reply.Err = OK
  }
  reply.Value = value
  return nil
}

// ping the viewserver periodically.
func (pb *PBServer) tick() {
  pb.mu.Lock()
  defer pb.mu.Unlock()

  // Send a Ping to the viewservice
  viewnum := pb.view.Viewnum
  newView, err := pb.vs.Ping(viewnum)
  if err != nil {
      return
  }
  // Update view
  oldView := pb.view
  pb.view = newView

  pb.isPrimary = (pb.me == pb.view.Primary)
  pb.isBackup = (pb.me == pb.view.Backup)

  if pb.isPrimary && pb.view.Backup != "" && pb.view.Backup != oldView.Backup {
    // sync data with backup based on current view
    args := &SyncArgs{KV: pb.kv, DoneReqs: pb.doneReqs}
    var reply SyncReply
    success := false
    // keep trying to sync data to backup until success
    for !success {
      ok := call(pb.view.Backup, "PBServer.SyncData", args, &reply)
      if ok && reply.Err == OK {
          success = true
      }
    }
  }
}


// tell the server to shut itself down.
// please do not change this function.
func (pb *PBServer) kill() {
  pb.dead = true
  pb.l.Close()
}


func StartServer(vshost string, me string) *PBServer {
  pb := new(PBServer)
  pb.me = me
  pb.vs = viewservice.MakeClerk(me, vshost)
  pb.finish = make(chan interface{})
  // Your pb.* initializations here.
  pb.kv = make(map[string]string)
  pb.okv = make(map[string]string)
  pb.doneReqs = make(map[int64]bool)
  
  rpcs := rpc.NewServer()
  rpcs.Register(pb)

  os.Remove(pb.me)
  l, e := net.Listen("unix", pb.me);
  if e != nil {
    log.Fatal("listen error: ", e);
  }
  pb.l = l

  // please do not change any of the following code,
  // or do anything to subvert it.

  go func() {
    for pb.dead == false {
      conn, err := pb.l.Accept()
      if err == nil && pb.dead == false {
        if pb.unreliable && (rand.Int63() % 1000) < 100 {
          // discard the request.
          conn.Close()
        } else if pb.unreliable && (rand.Int63() % 1000) < 200 {
          // process the request but force discard of reply.
          c1 := conn.(*net.UnixConn)
          f, _ := c1.File()
          err := syscall.Shutdown(int(f.Fd()), syscall.SHUT_WR)
          if err != nil {
            fmt.Printf("shutdown: %v\n", err)
          }
          pb.done.Add(1)
          go func() {
            rpcs.ServeConn(conn)
            pb.done.Done()
          }()
        } else {
          pb.done.Add(1)
          go func() {
            rpcs.ServeConn(conn)
            pb.done.Done()
          }()
        }
      } else if err == nil {
        conn.Close()
      }
      if err != nil && pb.dead == false {
        fmt.Printf("PBServer(%v) accept: %v\n", me, err.Error())
        pb.kill()
      }
    }
    DPrintf("%s: wait until all request are done\n", pb.me)
    pb.done.Wait() 
    // If you have an additional thread in your solution, you could
    // have it read to the finish channel to hear when to terminate.
    close(pb.finish)
  }()

  pb.done.Add(1)
  go func() {
    for pb.dead == false {
      pb.tick()
      time.Sleep(viewservice.PingInterval)
    }
    pb.done.Done()
  }()

  return pb
}
