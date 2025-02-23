package paxos

import (
	"coms4113/hw5/pkg/base"
)

const (
	Propose = "propose"
	Accept  = "accept"
	Decide  = "decide"
)

type Proposer struct {
	N             int
	Phase         string
	N_a_max       int
	V             interface{}
	SuccessCount  int
	ResponseCount int
	// To indicate if response from peer is received, should be initialized as []bool of len(server.peers)
	Responses []bool
	// Use this field to check if a message is latest.
	SessionId int

	// in case node will propose again - restore initial value
	InitialValue interface{}
}

type ServerAttribute struct {
	peers []base.Address
	me    int

	// Paxos parameter
	n_p int
	n_a int
	v_a interface{}

	// final result
	agreedValue interface{}

	// Propose parameter
	proposer Proposer

	// retry
	timeout *TimeoutTimer
}

type Server struct {
	base.CoreNode
	ServerAttribute
}

func NewServer(peers []base.Address, me int, proposedValue interface{}) *Server {
	response := make([]bool, len(peers))
	return &Server{
		CoreNode: base.CoreNode{},
		ServerAttribute: ServerAttribute{
			peers: peers,
			me:    me,
			proposer: Proposer{
				InitialValue: proposedValue,
				Responses:    response,
			},
			timeout: &TimeoutTimer{},
		},
	}
}

func (server *Server) MessageHandler(message base.Message) []base.Node {
	sub := server.copy()
	switch m := message.(type) {
	case *ProposeRequest:
		if m.N > sub.n_p {
			// update n_p
			sub.n_p = m.N
			resp := &ProposeResponse{
				CoreMessage: base.MakeCoreMessage(sub.Address(),m.From()),
				Ok: true,
				N_p: sub.n_p,
				N_a: sub.n_a,
				V_a: sub.v_a,
				SessionId: m.SessionId,
			}
			sub.SetResponse([]base.Message{resp})
		} else {
			// reject
			resp := &ProposeResponse{
				CoreMessage: base.MakeCoreMessage(sub.Address(),m.From()),
				Ok: false,
				N_p: sub.n_p,
				N_a: sub.n_a,
				V_a: sub.v_a,
				SessionId: m.SessionId,
			}
			sub.SetResponse([]base.Message{resp})
		}
		return []base.Node{sub}
	case *ProposeResponse:
		nodes := []base.Node{sub}
		if m.SessionId < sub.proposer.SessionId {
			// Old message
			return nodes
		}
		idx := -1
		for i:=0; i<len(sub.peers); i++ {
			if sub.peers[i] == m.From() {
				idx = i
				break
			}
		}
		if idx != -1 && sub.proposer.Responses[idx] == false {
			sub.proposer.Responses[idx] = true
			sub.proposer.ResponseCount += 1
			if m.Ok {
				sub.proposer.SuccessCount += 1
				if m.N_a > sub.proposer.N_a_max {
					sub.proposer.N_a_max = m.N_a
					sub.proposer.V = m.V_a
				}
			}
		}
		// check if majority ok came
		if sub.proposer.SuccessCount > len(sub.peers)/2 {
			//send accept requests
			sub1 := sub.copy()
			sub1.proposer.Phase = Accept
			sub1.proposer.ResponseCount = 0
			sub1.proposer.SuccessCount = 0
			sub1.proposer.Responses = make([]bool,len(sub1.peers))
			acceptReqs := make([]base.Message,len(sub1.peers))
			for i,peer := range sub1.peers {
				acceptReqs[i] = &AcceptRequest{
					CoreMessage: base.MakeCoreMessage(sub1.Address(),peer),
					N: sub1.proposer.N,
					V: sub1.proposer.V,
					SessionId: sub1.proposer.SessionId,
				}
			}
			sub1.SetResponse(acceptReqs)
			nodes = append(nodes,sub1)
		}
		return nodes
	case *AcceptRequest:
		if m.N >= sub.n_p {
			// update
			sub.n_a = m.N
			sub.v_a = m.V
			resp := &AcceptResponse{
				CoreMessage: base.MakeCoreMessage(sub.Address(),m.From()),
				Ok: true,
				N_p: sub.n_p,
				SessionId: m.SessionId,
			}
			sub.SetSingleResponse(resp)
		} else {
			//reject request
			resp := &AcceptResponse{
				CoreMessage: base.MakeCoreMessage(sub.Address(),m.From()),
				Ok: false,
				N_p: sub.n_p,
				SessionId: m.SessionId,
			}
			sub.SetSingleResponse(resp)
		}
		return []base.Node{sub}
	case *AcceptResponse:
		nodes := []base.Node{sub}
		if m.SessionId < sub.proposer.SessionId {
			// Old message
			return nodes
		}
		idx := -1
		for i:=0; i<len(sub.peers); i++ {
			if sub.peers[i] == m.From() {
				idx = i
				break
			}
		}
		if idx != -1 && !sub.proposer.Responses[idx] {
			sub.proposer.Responses[idx] = true
			sub.proposer.ResponseCount += 1
			if m.Ok {
				sub.proposer.SuccessCount += 1
			}
		}
		// check if majority ok came
		if sub.proposer.SuccessCount > len(sub.peers)/2 {
			// send decide reqeusts
			sub1 := sub.copy()
			sub1.proposer.Phase = Decide
			sub1.proposer.ResponseCount = 0
			sub1.proposer.SuccessCount = 0
			sub1.proposer.Responses = make([]bool,len(sub1.peers))
			decideReqs := make([]base.Message, len(sub1.peers))
			for i,peer := range sub1.peers {
				decideReqs[i] = &DecideRequest{
					CoreMessage: base.MakeCoreMessage(sub1.Address(),peer),
					V: sub1.proposer.V,
					SessionId: sub1.proposer.SessionId,
				}
			}
			sub1.SetResponse(decideReqs)
			nodes = append(nodes,sub1)
		}
		return nodes
	case *DecideRequest:
		sub.agreedValue = m.V
		return []base.Node{sub}
	}
	return nil
}

// To start a new round of Paxos.
func (server *Server) StartPropose() {
	server.proposer.Phase = Propose
	server.proposer.N += 1
	server.proposer.V = server.proposer.InitialValue
	server.proposer.N_a_max = 0
	server.proposer.SuccessCount = 0
	server.proposer.ResponseCount = 0
	server.proposer.Responses = make([]bool,len(server.peers))
	server.proposer.SessionId += 1

	proposeReqs := make([]base.Message,len(server.peers))
	for idx,peer := range server.peers {
		proposeReqs[idx] = &ProposeRequest{
			CoreMessage: base.MakeCoreMessage(server.peers[server.me],peer),
			N : server.proposer.N,
			SessionId : server.proposer.SessionId,
		}
	}
	server.SetResponse(proposeReqs)
}

// Returns a deep copy of server node
func (server *Server) copy() *Server {
	response := make([]bool, len(server.peers))
	for i, flag := range server.proposer.Responses {
		response[i] = flag
	}

	var copyServer Server
	copyServer.me = server.me
	// shallow copy is enough, assuming it won't change
	copyServer.peers = server.peers
	copyServer.n_a = server.n_a
	copyServer.n_p = server.n_p
	copyServer.v_a = server.v_a
	copyServer.agreedValue = server.agreedValue
	copyServer.proposer = Proposer{
		N:             server.proposer.N,
		Phase:         server.proposer.Phase,
		N_a_max:       server.proposer.N_a_max,
		V:             server.proposer.V,
		SuccessCount:  server.proposer.SuccessCount,
		ResponseCount: server.proposer.ResponseCount,
		Responses:     response,
		InitialValue:  server.proposer.InitialValue,
		SessionId:     server.proposer.SessionId,
	}

	// doesn't matter, timeout timer is state-less
	copyServer.timeout = server.timeout

	return &copyServer
}

func (server *Server) NextTimer() base.Timer {
	return server.timeout
}

// A TimeoutTimer tick simulates the situation where a proposal procedure times out.
// It will close the current Paxos round and start a new one if no consensus reached so far,
// i.e. the server after timer tick will reset and restart from the first phase if Paxos not decided.
// The timer will not be activated if an agreed value is set.
func (server *Server) TriggerTimer() []base.Node {
	if server.timeout == nil {
		return nil
	}

	subNode := server.copy()
	subNode.StartPropose()

	return []base.Node{subNode}
}

func (server *Server) Attribute() interface{} {
	return server.ServerAttribute
}

func (server *Server) Copy() base.Node {
	return server.copy()
}

func (server *Server) Hash() uint64 {
	return base.Hash("paxos", server.ServerAttribute)
}

func (server *Server) Equals(other base.Node) bool {
	otherServer, ok := other.(*Server)

	if !ok || server.me != otherServer.me ||
		server.n_p != otherServer.n_p || server.n_a != otherServer.n_a || server.v_a != otherServer.v_a ||
		(server.timeout == nil) != (otherServer.timeout == nil) {
		return false
	}

	if server.proposer.N != otherServer.proposer.N || server.proposer.V != otherServer.proposer.V ||
		server.proposer.N_a_max != otherServer.proposer.N_a_max || server.proposer.Phase != otherServer.proposer.Phase ||
		server.proposer.InitialValue != otherServer.proposer.InitialValue ||
		server.proposer.SuccessCount != otherServer.proposer.SuccessCount ||
		server.proposer.ResponseCount != otherServer.proposer.ResponseCount {
		return false
	}

	for i, response := range server.proposer.Responses {
		if response != otherServer.proposer.Responses[i] {
			return false
		}
	}

	return true
}

func (server *Server) Address() base.Address {
	return server.peers[server.me]
}
