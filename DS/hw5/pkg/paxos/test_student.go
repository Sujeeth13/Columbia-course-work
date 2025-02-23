package paxos

import (
	"coms4113/hw5/pkg/base"
)

// Fill in the function to lead the program to a state where A2 rejects the Accept Request of P1
func ToA2RejectP1() []func(s *base.State) bool {
	s1SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Propose
	}
	s2SendPrepare := func(s *base.State) bool {
		s2 := s.Nodes()["s2"].(*Server)
		return s2.proposer.Phase == Propose
	}
	s3SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Propose && s3.proposer.N > s1.proposer.N
	}
	s1SendAccept := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept
	}
	return []func(s *base.State) bool{
		s1SendPrepare,
		s2SendPrepare,
		s3SendPrepare,
		s1SendAccept,
	}
}

// Fill in the function to lead the program to a state where a consensus is reached in Server 3.
func ToConsensusCase5() []func(s *base.State) bool {
	s3SendPrepare := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Propose
	}
	s3SendAccept := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Accept
	}
	s3Decided := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.agreedValue == "v3"
	}
	return []func(s *base.State) bool{
		s3SendPrepare,
		s3SendAccept,
		s3Decided,
	}
}

// Fill in the function to lead the program to a state where all the Accept Requests of P1 are rejected
func NotTerminate1() []func(s *base.State) bool {
	s1SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Propose
	}
	s3SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Propose && s3.proposer.N > s1.n_p
	}
	s1SendAccept := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept
	}
	s3SendAccept := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Accept
	}
	s1AcceptReject := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept && s1.proposer.ResponseCount == 1 && s1.proposer.SuccessCount == 0
	}
	return []func(s *base.State) bool{
		s1SendPrepare,
		s3SendPrepare,
		s1SendAccept,
		s3SendAccept,
		s1AcceptReject,
	}
}

// Fill in the function to lead the program to a state where all the Accept Requests of P3 are rejected
func NotTerminate2() []func(s *base.State) bool {
	s1SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		s3 := s.Nodes()["s3"].(*Server)
		return s1.proposer.Phase == Propose && s1.proposer.N > s3.n_p
	}
	s1SendAccept := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept
	}
	s3AcceptReject := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Accept && s3.proposer.ResponseCount == 1 && s3.proposer.SuccessCount == 0
	}
	return []func(s *base.State) bool{
		s1SendPrepare,
		s1SendAccept,
		s3AcceptReject,
	}
}

// Fill in the function to lead the program to a state where all the Accept Requests of P1 are rejected again.
func NotTerminate3() []func(s *base.State) bool {
	s3SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Propose && s3.proposer.N > s1.n_p
	}
	s3SendAccept := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Accept
	}
	s1AcceptReject := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept && s1.proposer.ResponseCount == 1 && s1.proposer.SuccessCount == 0
	}
	return []func(s *base.State) bool {
		s3SendPrepare,
		s3SendAccept,
		s1AcceptReject,
	}
}

// Fill in the function to lead the program to make P1 propose first, then P3 proposes, but P1 get rejects in
// Accept phase
func concurrentProposer1() []func(s *base.State) bool {
	s1SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Propose
	}
	s3SendPrepare := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Propose && s3.proposer.N > s1.n_p
	}
	s1SendAccept := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept
	}
	s3SendAccept := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.proposer.Phase == Accept
	}
	s1AcceptReject := func(s *base.State) bool {
		s1 := s.Nodes()["s1"].(*Server)
		return s1.proposer.Phase == Accept && s1.proposer.ResponseCount == 1 && s1.proposer.SuccessCount == 0
	}
	return []func(s *base.State) bool {
		s1SendPrepare,
		s3SendPrepare,
		s1SendAccept,
		s3SendAccept,
		s1AcceptReject,
	}
}

// Fill in the function to lead the program continue  P3's proposal  and reaches consensus at the value of "v3".
func concurrentProposer2() []func(s *base.State) bool {
	s3Decided := func(s *base.State) bool {
		s3 := s.Nodes()["s3"].(*Server)
		return s3.agreedValue == "v3"
	}
	return []func(s *base.State) bool {
		s3Decided,
	}
}
