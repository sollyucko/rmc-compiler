// typo in section 8 header in cppmem def

// Try 1
int main() {
  atomic_int clear = 0;
  int data = 0;

  {{{ { if (clear.load(mo_relaxed)) { data = 1; } }
  ||| { r2 = data;
        clear.store(1, mo_release); }
  }}};
  return 0;
}

// Try 2
int main() {
  atomic_int clear = 0;
  atomic_int data = 0;

  {{{ { if (clear.load(mo_relaxed)) { data.store(1, mo_relaxed); } }
  ||| { r2 = data.load(mo_relaxed).readsvalue(1);
        clear.store(1, mo_release); }
  }}};
  return 0;
}

// This one has a data dependency too --
// if we make the load mo_consumed it rules out the fail
int main() {
  atomic_int clear = 0;
  atomic_int data = 0;
  int thing;

  {{{ { thing = clear.load(mo_relaxed);
        if (thing) { data.store(thing, mo_relaxed); } }
  ||| { r2 = data.load(mo_relaxed).readsvalue(1);
        clear.store(1, mo_release); }
  }}};
  return 0;
}

// This is a weird test for a weird reason.
int main() {
  atomic_int x = 0;
  atomic_int y = 0;

  {{{ { x.store(1, mo_relaxed);
        x.store(2, mo_relaxed);
        y.store(1, mo_release); }
  ||| { r1 = y.load(mo_acquire).readsvalue(1);
        r2 = x.load(mo_relaxed); }
  }}};
  return 0;
}




#if 0

PPC MP+sync+ctrl
"SyncdWW Rfe DpCtrldR Fre"
Cycle=Rfe DpCtrldR Fre SyncdWW
{
0:r2=x; 0:r4=y;
1:r2=y; 1:r4=x;
}
 P0           | P1           ;
 li r1,1      | lwz r1,0(r2) ;
 stw r1,0(r2) | cmpw r1,r1   ;
 sync         | beq  LC00    ;
 li r3,1      | LC00:        ;
 stw r3,0(r4) | lwz r3,0(r4) ;
exists
(1:r1=1 /\ 1:r3=0)



PPC buf+sync+ctrl
"SyncdWW Rfe DpCtrldR Fre"
Cycle=Rfe DpCtrldR Fre SyncdWW
{
0:r2=c; 0:r4=d;
1:r2=c; 1:r4=d;
}
 P0           | P1           ;
 lwz r1,0(r2) | lwz r3,0(r4) ;
 cmpw r1,r1   | sync         ;
 beq LC00     | li r1,1      ;
 LC00:        | stw r1,0(r2) ;
 li r3,1      |              ;
 stw r3,0(r4) |              ;
exists
(0:r1=1 /\ 1:r3=1)


PPC buf+lwsync+ctrl
"SyncdWW Rfe DpCtrldR Fre"
Cycle=Rfe DpCtrldR Fre SyncdWW
{
0:r2=c; 0:r4=d;
1:r2=c; 1:r4=d;
}
 P0           | P1           ;
 lwz r1,0(r2) | lwz r3,0(r4) ;
 cmpw r1,r1   | lwsync       ;
 beq LC00     | li r1,1      ;
 LC00:        | stw r1,0(r2) ;
 li r3,1      |              ;
 stw r3,0(r4) |              ;
exists
(0:r1=1 /\ 1:r3=1)


#endif