#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;    // number of bytes read
  uint nwrite;   // number of bytes written
  int readopen;  // read fd is still open
  int writeopen; // write fd is still open
};

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if ((pi = (struct pipe *)kalloc()) == 0)
    goto bad;
  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nwrite = 0;
  pi->nread = 0;
  initlock(&pi->lock, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;

bad:
  if (pi)
    kfree((char *)pi);
  if (*f0)
    fileclose(*f0);
  if (*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if (writable) {
    pi->writeopen = 0;
    wakeup(&pi->nread);
  } else {
    pi->readopen = 0;
    wakeup(&pi->nwrite);
  }
  if (pi->readopen == 0 && pi->writeopen == 0) {
    release(&pi->lock);
    kfree((char *)pi);
  } else
    release(&pi->lock);
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while (i < n) {
    if (pi->readopen == 0 || killed(pr)) {
      release(&pi->lock);
      return -1;
    }
    if (pi->nwrite == pi->nread + PIPESIZE) { //DOC: pipewrite-full
      wakeup(&pi->nread);
      sleep_prepare(&pi->nwrite);
      release(&pi->lock);
      sleep();
      acquire(&pi->lock);
    } else {
      char ch;
      if (copyin(pr->pagetable, pr->sz, &ch, addr + i, 1) == -1) {
        if (i == 0)
          i = -1;
        break;
      }
      pi->data[pi->nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);

  return i;
}

#define MAXW 8

extern struct proc proc[];

// Processes holding a write end of pi, excluding 'self' and its ancestors
// (a parent that is only wait()ing is not the writer).
static int
pipe_writers(struct pipe *pi, struct proc *self, struct proc **out, int max)
{
  int n = 0;
  for (struct proc *q = proc; q < &proc[NPROC] && n < max; q++) {
    if (q == self || q->state == UNUSED || q->state == ZOMBIE)
      continue;
    int anc = 0, depth = 0;
    for (struct proc *a = self->parent; a && depth < NPROC; a = a->parent, depth++) {
      if (a == q) {
        anc = 1;
        break;
      }
    }
    if (anc)
      continue;
    for (int fd = 0; fd < NOFILE; fd++) {
      struct file *f = q->ofile[fd];
      if (f && f->type == FD_PIPE && f->pipe == pi && f->writable) {
        out[n++] = q;
        break;
      }
    }
  }
  return n;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  struct proc *ws[MAXW];
  int nw = 0;
  char ch;

  acquire(&pi->lock);
  while (pi->nread == pi->nwrite && pi->writeopen) { //DOC: pipe-empty
    if (killed(pr)) {
      release(&pi->lock);
      return -1;
    }
    // Lend our deadline to whoever can fill this pipe.
    nw = pipe_writers(pi, pr, ws, MAXW);
    if (nw > 0) {
      pr->waiting_on = ws[0];
      for (int k = 0; k < nw; k++)
        donate(pr, ws[k], effective(pr));
    }
    sleep_prepare(&pi->nread); //DOC: piperead-sleep
    release(&pi->lock);
    sleep();
    acquire(&pi->lock);
    // Woken up: withdraw the loan.
    pr->waiting_on = 0;
    for (int k = 0; k < nw; k++)
      recompute_donation(ws[k]);
    nw = 0;
  }
  for (i = 0; i < n; i++) { //DOC: piperead-copy
    if (pi->nread == pi->nwrite)
      break;
    ch = pi->data[pi->nread % PIPESIZE];
    if (copyout(pr->pagetable, pr->sz, addr + i, &ch, 1) == -1) {
      if (i == 0)
        i = -1;
      break;
    }
    pi->nread++;
  }
  wakeup(&pi->nwrite); //DOC: piperead-wakeup
  release(&pi->lock);
  return i;
}
