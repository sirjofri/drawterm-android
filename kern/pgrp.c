#include	"u.h"
#include	"lib.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

static Ref pgrpid;
static Ref mountid;

Pgrp*
newpgrp(void)
{
	Pgrp *p;

	p = smalloc(sizeof(Pgrp));
	p->ref.ref = 1;
	p->pgrpid = incref(&pgrpid);
	return p;
}

Rgrp*
newrgrp(void)
{
	Rgrp *r;

	r = smalloc(sizeof(Rgrp));
	r->ref.ref = 1;
	return r;
}

void
closergrp(Rgrp *r)
{
	if(decref(&r->ref) == 0)
		free(r);
}

void
closepgrp(Pgrp *p)
{
	Mhead **h, **e, *f;
	Mount *m;

	if(decref(&p->ref))
		return;

	e = &p->mnthash[MNTHASH];
	for(h = p->mnthash; h < e; h++) {
		while((f = *h) != nil){
			*h = f->hash;
			wlock(&f->lock);
			m = f->mount;
			f->mount = nil;
			wunlock(&f->lock);
			mountfree(m);
			putmhead(f);
		}
	}
	free(p);
}

void
pgrpinsert(Mount **order, Mount *m)
{
	Mount *f;

	m->order = nil;
	if(*order == nil) {
		*order = m;
		return;
	}
	for(f = *order; f != nil; f = f->order) {
		if(m->mountid < f->mountid) {
			m->order = f;
			*order = m;
			return;
		}
		order = &f->order;
	}
	*order = m;
}

/*
 * pgrpcpy MUST preserve the mountid allocation order of the parent group
 */
void
pgrpcpy(Pgrp *to, Pgrp *from)
{
	Mount *n, *m, **link, *order;
	Mhead *f, **tom, **l, *mh;
	int i;

	wlock(&to->ns);
	rlock(&from->ns);
	order = nil;
	tom = to->mnthash;
	for(i = 0; i < MNTHASH; i++) {
		l = tom++;
		for(f = from->mnthash[i]; f != nil; f = f->hash) {
			rlock(&f->lock);
			mh = newmhead(f->from);
			*l = mh;
			l = &mh->hash;
			link = &mh->mount;
			for(m = f->mount; m != nil; m = m->next) {
				n = smalloc(sizeof(Mount));
				n->mountid = m->mountid;
				n->mflag = m->mflag;
				n->to = m->to;
				incref(&n->to->ref);
				if(m->spec != nil)
					kstrdup(&n->spec, m->spec);
				pgrpinsert(&order, n);
				*link = n;
				link = &n->next;
			}
			runlock(&f->lock);
		}
	}
	/*
	 * Allocate mount ids in the same sequence as the parent group
	 */
	for(m = order; m != nil; m = m->order)
		m->mountid = incref(&mountid);
	runlock(&from->ns);
	wunlock(&to->ns);
}

Fgrp*
dupfgrp(Fgrp *f)
{
	Fgrp *new;
	Chan *c;
	int i;

	new = smalloc(sizeof(Fgrp));
	if(f == nil){
		new->fd = smalloc(DELTAFD*sizeof(Chan*));
		new->nfd = DELTAFD;
		new->ref.ref = 1;
		return new;
	}

	lock(&f->ref.lk);
	/* Make new fd list shorter if possible, preserving quantization */
	new->nfd = f->maxfd+1;
	i = new->nfd%DELTAFD;
	if(i != 0)
		new->nfd += DELTAFD - i;
	new->fd = malloc(new->nfd*sizeof(Chan*));
	if(new->fd == nil){
		unlock(&f->ref.lk);
		free(new);
		error("no memory for fgrp");
	}
	new->ref.ref = 1;

	new->maxfd = f->maxfd;
	for(i = 0; i <= f->maxfd; i++) {
		if((c = f->fd[i]) != nil){
			incref(&c->ref);
			new->fd[i] = c;
		}
	}
	unlock(&f->ref.lk);

	return new;
}

void
closefgrp(Fgrp *f)
{
	int i;
	Chan *c;

	if(f == nil || decref(&f->ref))
		return;

	for(i = 0; i <= f->maxfd; i++)
		if((c = f->fd[i]) != nil){
			f->fd[i] = nil;
			cclose(c);
		}

	free(f->fd);
	free(f);
}

Mount*
newmount(Chan *to, int flag, char *spec)
{
	Mount *m;

	m = smalloc(sizeof(Mount));
	m->to = to;
	incref(&to->ref);
	m->mountid = incref(&mountid);
	m->mflag = flag;
	if(spec != nil)
		kstrdup(&m->spec, spec);

	return m;
}

void
mountfree(Mount *m)
{
	Mount *f;

	while((f = m) != nil) {
		m = m->next;
		cclose(f->to);
		free(f->spec);
		free(f);
	}
}
