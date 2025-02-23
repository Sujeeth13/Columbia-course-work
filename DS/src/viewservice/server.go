
package viewservice

import "net"
import "net/rpc"
import "log"
import "time"
import "sync"
import "fmt"
import "os"

type ViewServer struct {
  mu sync.Mutex
  l net.Listener
  dead bool
  me string
  lastPing map[string]time.Time
  serverView map[string]uint
  view View
  primaryACK bool
}

//
// server Ping RPC handler.
//
func (vs *ViewServer) Ping(args *PingArgs, reply *PingReply) error {
  vs.mu.Lock()
  defer vs.mu.Unlock()

  server := args.Me
  viewnum := args.Viewnum
  vs.lastPing[server] = time.Now()
  vs.serverView[server] = viewnum
  curr_time := time.Now()
  timeout := PingInterval * DeadPings

  if vs.view.Viewnum == 0 {
    vs.view.Viewnum = 1 
    vs.view.Primary = server
    vs.view.Backup = ""
    vs.primaryACK = false
  } else {
    // TODO: why is the ping 0 server crash case not being handled ?
    if vs.view.Primary == server {
      if vs.view.Viewnum == viewnum {
        vs.primaryACK = true
      }
    }
    if vs.primaryACK && server == vs.view.Primary && viewnum != vs.view.Viewnum {
      // reset to new view
      vs.view.Primary = vs.view.Backup
      vs.view.Backup = ""
      vs.view.Viewnum += 1
      vs.primaryACK = false
      if vs.view.Primary == "" {
        for newServer := range vs.serverView {
          if newServer != server && curr_time.Sub(vs.lastPing[newServer]) <= timeout {
            vs.view.Primary = newServer
            break;
          }
        }
      }
      for newServer := range vs.serverView {
        if newServer != vs.view.Primary && curr_time.Sub(vs.lastPing[newServer]) <= timeout {
          vs.view.Backup = newServer
          break;
        }
      }
    } else if vs.primaryACK && vs.view.Backup == "" && server != vs.view.Primary {
      vs.view.Viewnum += 1
      vs.view.Backup = server
      vs.primaryACK = false
    }
  }
  reply.View = vs.view
  return nil
}

// 
// server Get() RPC handler.
//
func (vs *ViewServer) Get(args *GetArgs, reply *GetReply) error {
  vs.mu.Lock()
  defer vs.mu.Unlock()
  reply.View = vs.view
  return nil
}


//
// tick() is called once per PingInterval; it should notice
// if servers have died or recovered, and change the view
// accordingly.
//
func (vs *ViewServer) tick() {
  vs.mu.Lock()
  defer vs.mu.Unlock()
  curr_time := time.Now()
  timeout := PingInterval * DeadPings

  primaryFail := false
  if vs.view.Primary != "" {
    if curr_time.Sub(vs.lastPing[vs.view.Primary]) > timeout {
      primaryFail = true;
    } else if vs.serverView[vs.view.Primary] == 0 {
      primaryFail = true;
    }
  }

  backupFail := false
  if vs.view.Backup != "" {
    if curr_time.Sub(vs.lastPing[vs.view.Backup]) > timeout {
      backupFail = true
    } else if vs.serverView[vs.view.Backup] == 0 {
      backupFail = true
    }
  }

  if vs.primaryACK {
    if primaryFail {
      newPrimary := ""
      newBackup := ""
      if !backupFail && vs.view.Backup != "" {
        newPrimary = vs.view.Backup
        newBackup = ""
        for server := range vs.lastPing {
          if server != vs.view.Primary && server != vs.view.Backup {
            if curr_time.Sub(vs.lastPing[server]) <= timeout && vs.serverView[server] != 0{
              newBackup = server
              break
            }
          }
        }
        vs.view.Viewnum += 1
        vs.view.Primary = newPrimary
        vs.view.Backup = newBackup
        vs.primaryACK = false
      } else {
        for server := range vs.lastPing {
          if server != vs.view.Primary && server != vs.view.Backup {
            if curr_time.Sub(vs.lastPing[server]) <= timeout && vs.serverView[server] != 0 {
              newPrimary = server
              break
            }
          }
        }
        for server := range vs.lastPing {
          if server != vs.view.Primary && server != vs.view.Backup && server != newPrimary {
            if curr_time.Sub(vs.lastPing[server]) <= timeout && vs.serverView[server] != 0 {
              newBackup = server
              break
            }
          }
        }
        vs.view.Viewnum += 1
        vs.view.Primary = newPrimary
        vs.view.Backup = newBackup
        vs.primaryACK = false
      }
    } else if backupFail {
      newBackup := ""
      for server := range vs.lastPing {
        if server != vs.view.Primary && server != vs.view.Backup {
          if curr_time.Sub(vs.lastPing[server]) <= timeout && vs.serverView[server] != 0 {
            newBackup = server
            break
          }
        }
      }
      vs.view.Viewnum += 1
      vs.view.Backup = newBackup
      vs.primaryACK = false
    }
  }
}

//
// tell the server to shut itself down.
// for testing.
// please don't change this function.
//
func (vs *ViewServer) Kill() {
  vs.dead = true
  vs.l.Close()
}

func StartServer(me string) *ViewServer {
  vs := new(ViewServer)
  vs.me = me
  // Your vs.* initializations here.
  vs.view = View{0, "", ""}
  vs.primaryACK = false
  vs.lastPing = make(map[string]time.Time)
  vs.serverView = make(map[string]uint)
  // tell net/rpc about our RPC server and handlers.
  rpcs := rpc.NewServer()
  rpcs.Register(vs)

  // prepare to receive connections from clients.
  // change "unix" to "tcp" to use over a network.
  os.Remove(vs.me) // only needed for "unix"
  l, e := net.Listen("unix", vs.me);
  if e != nil {
    log.Fatal("listen error: ", e);
  }
  vs.l = l

  // please don't change any of the following code,
  // or do anything to subvert it.

  // create a thread to accept RPC connections from clients.
  go func() {
    for vs.dead == false {
      conn, err := vs.l.Accept()
      if err == nil && vs.dead == false {
        go rpcs.ServeConn(conn)
      } else if err == nil {
        conn.Close()
      }
      if err != nil && vs.dead == false {
        fmt.Printf("ViewServer(%v) accept: %v\n", me, err.Error())
        vs.Kill()
      }
    }
  }()

  // create a thread to call tick() periodically.
  go func() {
    for vs.dead == false {
      vs.tick()
      time.Sleep(PingInterval)
    }
  }()

  return vs
}
