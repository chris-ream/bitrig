/*	$OpenBSD: tcfs_subr.c,v 1.3 2002/03/14 01:27:08 millert Exp $	*/
/*	$NetBSD: tcfs_subr.c,v 1.6 1996/05/10 22:50:52 jtk Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: Id: lofs_subr.c,v 1.11 1992/05/30 10:05:43 jsp Exp
 *	@(#)tcfs_subr.c	8.4 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <miscfs/specfs/specdev.h>
#include <miscfs/tcfs/tcfs.h>

#define LOG2_SIZEVNODE 7		/* log2(sizeof struct vnode) */
#define	NTCFSNODECACHE 16

/*
 * Null layer cache:
 * Each cache entry holds a reference to the lower vnode
 * along with a pointer to the alias vnode.  When an
 * entry is added the lower vnode is VREF'd.  When the
 * alias is removed the lower vnode is vrele'd.
 */

#define	TCFS_NHASH(vp) \
	(&tcfs_node_hashtbl[(((u_long)vp)>>LOG2_SIZEVNODE) & tcfs_node_hash])
LIST_HEAD(tcfs_node_hashhead, tcfs_node) *tcfs_node_hashtbl;
u_long tcfs_node_hash;

static struct vnode *
	tcfs_node_find(struct mount *, struct vnode *);
static int
	tcfs_node_alloc(struct mount *, struct vnode *, struct vnode **);
/*
 * Initialise cache headers
 */
/*ARGSUSED*/
int
tcfs_init(vfsp)
	struct vfsconf *vfsp;
{

#ifdef TCFS_DIAGNOSTIC
	printf("tcfs_init\n");		/* printed during system boot */
#endif
	tcfs_node_hashtbl = hashinit(NTCFSNODECACHE, M_CACHE, M_WAITOK, &tcfs_node_hash);
	return (0);
}

/*
 * Return a VREF'ed alias for lower vnode if already exists, else 0.
 */
static struct vnode *
tcfs_node_find(mp, lowervp)
	struct mount *mp;
	struct vnode *lowervp;
{
	struct tcfs_node_hashhead *hd;
	struct tcfs_node *a;
	struct vnode *vp;
	struct proc *p = curproc;

	/*
	 * Find hash base, and then search the (two-way) linked
	 * list looking for a tcfs_node structure which is referencing
	 * the lower vnode.  If found, the increment the tcfs_node
	 * reference count (but NOT the lower vnode's VREF counter).
	 */
	hd = TCFS_NHASH(lowervp);
loop:
	for (a = hd->lh_first; a != 0; a = a->tcfs_hash.le_next) {
		if (a->tcfs_lowervp == lowervp && TCFSTOV(a)->v_mount == mp) {
			vp = TCFSTOV(a);
			/*
			 * We need vget for the VXLOCK
			 * stuff, but we don't want to lock
			 * the lower node.
			 */
			if (vget(vp, 0, p)) {
				printf ("tcfs_node_find: vget failed.\n");
				goto loop;
			};
			return (vp);
		}
	}

	return (NULLVP);
}

/*
 * Make a new tcfs_node node.
 * Vp is the alias vnode, lowervp is the lower vnode.
 * Maintain a reference to lowervp.
 */
static int
tcfs_node_alloc(mp, lowervp, vpp)
	struct mount *mp;
	struct vnode *lowervp;
	struct vnode **vpp;
{
	struct tcfs_node_hashhead *hd;
	struct tcfs_node *xp;
	struct vnode *vp, *nvp;
	int error;
	extern int (**dead_vnodeop_p)(void *);
	struct proc *p = curproc;


	MALLOC(xp, struct tcfs_node *, sizeof(struct tcfs_node), M_TEMP,
	    M_WAITOK);

	if ((error = getnewvnode(VT_TCFS, mp, tcfs_vnodeop_p, vpp)) != 0) {
		FREE (xp, M_TEMP);
		return (error);
	}

	vp = *vpp;
	vp->v_type = lowervp->v_type;

	if (vp->v_type == VBLK || vp->v_type == VCHR) {
		MALLOC(vp->v_specinfo, struct specinfo *,
		    sizeof(struct specinfo), M_VNODE, M_WAITOK);
		vp->v_rdev = lowervp->v_rdev;
	}

	vp->v_data = xp;
	xp->tcfs_vnode = vp;
	xp->tcfs_lowervp = lowervp;
	/*
	 * Before we insert our new node onto the hash chains,
	 * check to see if someone else has beaten us to it.
	 * (We could have slept in MALLOC.)
	 */
	if ((nvp = tcfs_node_find(mp, lowervp)) != NULL) {
		*vpp = nvp;

		/* free the substructures we've allocated. */
		FREE(xp, M_TEMP);
		if (vp->v_type == VBLK || vp->v_type == VCHR)
			FREE(vp->v_specinfo, M_VNODE);

		vp->v_type = VBAD;		/* node is discarded */
		vp->v_op = dead_vnodeop_p;	/* so ops will still work */
		vrele(vp);			/* get rid of it. */
		return (0);
	}

	/*
	 * XXX if it's a device node, it needs to be checkalias()ed.
	 * however, for locking reasons, that's just not possible.
	 * so we have to do most of the dirty work inline.  Note that
	 * this is a limited case; we know that there's going to be
	 * an alias, and we know that that alias will be a "real"
	 * device node, i.e. not tagged VT_NON.
	 */
	if (vp->v_type == VBLK || vp->v_type == VCHR) {
		struct vnode *cvp, **cvpp;

		cvpp = &speclisth[SPECHASH(vp->v_rdev)];
loop:
		for (cvp = *cvpp; cvp; cvp = cvp->v_specnext) {
			if (vp->v_rdev != cvp->v_rdev ||
			    vp->v_type != cvp->v_type)
				continue;

			/*
			 * Alias, but not in use, so flush it out.
			 */
			if (cvp->v_usecount == 0) {
				vgone(cvp);
				goto loop;
			}
			if (vget(cvp, 0, p))	/* can't lock; will die! */
				goto loop;
			break;
		}

		vp->v_hashchain = cvpp;
		vp->v_specnext = *cvpp;
		vp->v_specmountpoint = NULL;
		*cvpp = vp;
#ifdef DIAGNOSTIC
		if (cvp == NULLVP)
			panic("tcfs_node_alloc: no alias for device");
#endif
		vp->v_flag |= VALIASED;
		cvp->v_flag |= VALIASED;
		vrele(cvp);
	}
	/* XXX end of transmogrified checkalias() */

	VREF(lowervp);	/* Extra VREF will be vrele'd in tcfs_node_create */
	hd = TCFS_NHASH(lowervp);
	LIST_INSERT_HEAD(hd, xp, tcfs_hash);
	return (0);
}


/*
 * Try to find an existing tcfs_node vnode refering
 * to it, otherwise make a new tcfs_node vnode which
 * contains a reference to the lower vnode.
 *
 * >>> we assume that the lower node is already locked upon entry, so we mark
 * the upper node as locked too (if caller requests it). <<<
 */
int
tcfs_node_create(mp, lowervp, newvpp, takelock)
	struct mount *mp;
	struct vnode *lowervp;
	struct vnode **newvpp;
	int takelock;
{
	struct vnode *aliasvp;

	if ((aliasvp = tcfs_node_find(mp, lowervp)) != NULL) {
		/*
		 * tcfs_node_find has taken another reference
		 * to the alias vnode.
		 */
#ifdef TCFS_DIAGNOSTIC
		vprint("tcfs_node_create: exists", aliasvp);
#endif
		/* VREF(aliasvp); --- done in tcfs_node_find */
	} else {
		int error;

		/*
		 * Get new vnode.
		 */
#ifdef TCFS_DIAGNOSTIC
		printf("tcfs_node_create: create new alias vnode\n");
#endif

		/*
		 * Make new vnode reference the tcfs_node.
		 */
		if ((error = tcfs_node_alloc(mp, lowervp, &aliasvp)) != 0)
			return error;

		/*
		 * aliasvp is already VREF'd by getnewvnode()
		 */
	}

	vrele(lowervp);

#ifdef DIAGNOSTIC
	if (lowervp->v_usecount < 1) {
		/* Should never happen... */
		vprint("tcfs_node_create: alias", aliasvp);
		panic("tcfs_node_create: lower has 0 usecount.");
	};
#endif

#ifdef TCFS_DIAGNOSTIC
	vprint("tcfs_node_create: alias", aliasvp);
#endif

	*newvpp = aliasvp;
	return (0);
}

#ifdef TCFS_DIAGNOSTIC
int tcfs_checkvp_barrier = 1;
struct vnode *
tcfs_checkvp(vp, fil, lno)
	struct vnode *vp;
	char *fil;
	int lno;
{
	struct tcfs_node *a = VTOTCFS(vp);
#ifdef notyet
	/*
	 * Can't do this check because vop_reclaim runs
	 * with a funny vop vector.
	 */
	if (vp->v_op != tcfs_vnodeop_p) {
		printf ("tcfs_checkvp: on non-tcfs-node\n");
		while (tcfs_checkvp_barrier) /*WAIT*/ ;
		panic("tcfs_checkvp");
	};
#endif
	if (a->tcfs_lowervp == TCFS) {
		/* Should never happen */
		int i; u_long *p;
		printf("vp = %p, ZERO ptr\n", vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %lx", p[i]);
		printf("\n");
		/* wait for debugger */
		while (tcfs_checkvp_barrier) /*WAIT*/ ;
		panic("tcfs_checkvp");
	}
	if (a->tcfs_lowervp->v_usecount < 1) {
		int i; u_long *p;
		printf("vp = %p, unref'ed lowervp\n", vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %lx", p[i]);
		printf("\n");
		/* wait for debugger */
		while (tcfs_checkvp_barrier) /*WAIT*/ ;
		panic ("tcfs with unref'ed lowervp");
	};
#ifdef notyet
	printf("tcfs %p/%d -> %p/%d [%s, %d]\n",
	        TCFSTOV(a), TCFSTOV(a)->v_usecount,
		a->tcfs_lowervp, a->tcfs_lowervp->v_usecount,
		fil, lno);
#endif
	return a->tcfs_lowervp;
}
#endif
