#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <assert.h>

#include "matrix.h"
#include "utility.h"
#include "spinner.h"
#include "linemin.h"

#define RESTART		1 
#define RESET		100
#define MAX_TRUST	10.0
#define MIN_TRUST	1e-4
#define MINSCALE	0.001
#define MAXSCALE	1000.0

#define HESSIAN_NONPD		  1
#define PARAM_BOUND		  2
#define NEWTON			  4
#define TRUNC_BOUND		  8
#define INVALID_STEP		 16
#define BAD_STEP		 32
#define REARRANGED		 64
#define DOG_LEG			128

#define BOUND_TOL	1e-5


#ifdef USE_OPTMESS
	#define OPTMESS(A) A
#else
	#define OPTMESS(A) 
#endif



typedef struct {
	double         *x;
	double         *xn;
	double         *dx;
	double         *dxn;
	double         *H;
	double         *space;
	double         *lb;
	double         *ub;
	void           *state;
	double          fc, fn;
	double          (*f) (const double *, void *);
	void            (*df) (const double *, double *, void *);

	int            *onbound;
	int             n;
        int             neval;
	double 		trust;
}               OPTOBJ;

struct scaleinfo {
	double         *sx;
	int             dim;
	void            (*df) (const double *, double *, void *);
	double          (*f) (const double *, void *);
	double         *scale;
	void           *state;
};





int
UpdateH_BFGS(double *H, const double *x, double *xn, const double *dx,
	     double *dxn, double *scale, const int n, double *space,
	     const int *onbound);
double 
TakeStep(OPTOBJ * opt, const double tol, double *factor,
	 int *newbound);



void 
InitializeOpt(OPTOBJ * opt, double *x, int n,
	      void (*df) (const double *, double *, void *),
	      double (*f) (const double *, void *), double fx, void *data,
	      double *bd);
OPTOBJ         *NewOpt(int n);
void            FreeOpt(OPTOBJ * opt);
void            MakeErrString(char **string, int errn);
void            InitializeH(OPTOBJ * opt);
void            TestIdentity(double *A, double *B, int n);
double          TrimAtBoundaries(const double *x, const double *direct,
	                 const double *scale, const int n, const double *lb,
		                      const double *ub, const int *onbound);
int             UpdateActiveSet(const double *x, double *grad, const double *scale,
	                double *InvHess, const double *lb, const double *ub,
				                 int *onbound, const int n, int * newbound);
double SteepestDescentStep ( OPTOBJ * opt);
double          GetNewtonStep(double *direct, const double *InvHess,
                       const double *grad, const int n, const int *onbound);
void            ScaledStep(const double factor, const double *x, double *xn,
                     const double *direct, const int *onbound, const int n);
void            MakeMatrixDiagonal(double *A, int n);
int             CheckScaleInfo(struct scaleinfo * sinfo);
void            dfWrap(const double *x, double *grad, void *info);
double          fWrap(const double *x, void *info);
void            Rescale(double *x, double *dx, double *H, int n, double *scale);
void            AnalyseOptima(double *x, double *dx, int n, int *onbound, double *lb,
				                      double *ub);
int             step, reset;
int             errn=0;


double calcerr ( const double x, const double y){
	return x-y;
	return 2.*(x-y)/(fabs(x)+fabs(y));
}


void            Optimize(double *x, int n, void (*df) (const double *, double *, void *),
                     double (*f) (const double *, void *), double *fx, void *data,
				                 double *bd, int noisy)
{
	OPTOBJ         *opt;
	double          fo, fn, tol, md;
	int             i, max_step;
	int             max_restart, restarts;
	char           *errstring = NULL;
	double          fact;
	double         *scale;
	SPINNER        *spin;
	int             newbound = 1;


	opt = NewOpt(n);
	if (NULL == opt) {
		return;
	}
	InitializeOpt(opt, x, n, df, f, *fx, data, bd);

	tol = 3e-8;
	max_step = 100;
	max_restart = 20;
	restarts = -1;

	/* Do optimization, allowing restarts so don't get bogged down. */

	if (noisy == 2) {
		printf("Initial\tf: %8.6f\nStep     f(x)      delta\n", *fx);
	} else if (noisy == 1) {
		spin = CreateSpinner(2);
	}
	do {
		fo = opt->fc;
		InitializeH(opt);
		fact = 1.;
		do {
			fn = opt->fc;
			errn = 0;
			md = TakeStep(opt, tol, &fact, &newbound);
			MakeErrString(&errstring, errn);
			step++;
			if (noisy == 2) {
				printf("%3d: %9f %10.5e %4d %s\t%9.3f\n", step, opt->fc,
				  fabs(opt->fc - fn), opt->neval, errstring, md);
			} else if (noisy == 1) {
				UpdateSpinner(spin);
			}
			if ( calcerr(fn,opt->fc)<=tol){
			   OPTMESS( printf("Step was small. Trying steepest descent.\n");)
			   opt->fc = SteepestDescentStep(opt);
			   for ( int i=0 ; i<opt->n ; i++){ opt->x[i] = opt->xn[i]; }
			   opt->df(opt->x, opt->dx, opt->state);
			   for ( int i=0 ; i<opt->n ; i++){ opt->space[i] = -opt->dx[i];}
			   UpdateActiveSet (opt->x, opt->space, ((struct scaleinfo *) opt->state)->scale, opt->H,
			                                       opt->lb, opt->ub, opt->onbound, opt->n,&newbound);
			   InitializeH(opt);
			}
		} while ((calcerr(fn,opt->fc) > tol) || newbound );

		if (noisy == 2) {
			printf("***\n");
		}
		restarts++;
	} while (restarts < max_restart && (calcerr(opt->fc,fo) > tol) && RESTART);
	if (noisy == 1) {
		DeleteSpinner(spin);
	}
	if (restarts == max_restart) {
		printf("Didn't converge after %d restarts. Returning best value.\n",
		       restarts);
	}
	scale = ((struct scaleinfo *) opt->state)->scale;
	for (i = 0; i < n; i++) {
		x[i] = opt->x[i] * scale[i];
		opt->dx[i] *= scale[i];
	}
	*fx = opt->fc;

	FreeOpt(opt);
}


void 
MakeErrString(char **str, int errn)
{
	int             nerr = 0;
	char           *string;

	string = *str;

	if (NULL != string)
		free(string);

	if ((errn & HESSIAN_NONPD) == HESSIAN_NONPD)
		nerr++;
	if ((errn & PARAM_BOUND) == PARAM_BOUND)
		nerr++;
	if ((errn & NEWTON) == NEWTON)
		nerr++;
	if ((errn & TRUNC_BOUND) == TRUNC_BOUND)
		nerr++;
	if ((errn & INVALID_STEP) == INVALID_STEP)
		nerr++;
	if ((errn & BAD_STEP) == BAD_STEP)
		nerr++;
	if ((errn & REARRANGED) == REARRANGED)
		nerr++;
	if ((errn & DOG_LEG) == DOG_LEG)
		nerr++;



	string = malloc((nerr + 1) * sizeof(char));

	nerr = 0;
	if ((errn & HESSIAN_NONPD) == HESSIAN_NONPD)
		string[nerr++] = '-';
	if ((errn & PARAM_BOUND) == PARAM_BOUND)
		string[nerr++] = 'B';
	if ((errn & NEWTON) == NEWTON)
		string[nerr++] = 'N';
	if ((errn & TRUNC_BOUND) == TRUNC_BOUND)
		string[nerr++] = 'T';
	if ((errn & INVALID_STEP) == INVALID_STEP)
		string[nerr++] = 'V';
	if ((errn & BAD_STEP) == BAD_STEP)
		string[nerr++] = 'W';
	if ((errn & REARRANGED) == REARRANGED)
		string[nerr++] = 'R';
	if ((errn & DOG_LEG) == DOG_LEG)
		string[nerr++] = 'D';
	string[nerr] = '\0';

	*str = string;
}



OPTOBJ         *
NewOpt(int n)
{
	OPTOBJ         *opt;

	if (n < 1)
		return NULL;

	opt = malloc(sizeof(OPTOBJ));
	if (NULL == opt)
		return NULL;

	opt->n = n;
	opt->x = malloc(n * sizeof(double));
	opt->xn = malloc(n * sizeof(double));
	opt->dx = malloc(n * sizeof(double));
	opt->dxn = malloc(n * sizeof(double));
	opt->lb = malloc(n * sizeof(double));
	opt->ub = malloc(n * sizeof(double));
	opt->H = malloc(n * n * sizeof(double));
	opt->space = malloc(4 * n * sizeof(double));
	opt->onbound = malloc(n * sizeof(int));
	opt->neval = 0;
	opt->trust = 0.1;

	if (NULL == opt->x || NULL == opt->xn || NULL == opt->dx || NULL == opt->dxn
	    || NULL == opt->lb || NULL == opt->ub || NULL == opt->H
	    || NULL == opt->space || NULL == opt->onbound) {
		FreeOpt(opt);
		opt = NULL;
	}
	return opt;
}

void 
FreeOpt(OPTOBJ * opt)
{
	struct scaleinfo *sinfo;
	free(opt->x);
	free(opt->xn);
	free(opt->dx);
	free(opt->dxn);
	free(opt->lb);
	free(opt->ub);
	free(opt->H);
	free(opt->space);
	free(opt->onbound);
	sinfo = (struct scaleinfo *) opt->state;
	free(sinfo->sx);
	free(sinfo->scale);
	free(sinfo);
	free(opt);
}


void 
InitializeOpt(OPTOBJ * opt, double *x, int n,
	      void (*df) (const double *, double *, void *),
	      double (*f) (const double *, void *), double fx, void *data,
	      double *bd)
{
	int             i;
	struct scaleinfo *sinfo;

	step = 0;

	for (i = 0; i < n; i++) {
		opt->x[i] = x[i];
		opt->lb[i] = bd[i];
		opt->ub[i] = bd[n + i];
		opt->onbound[i] = 0;
	}

	sinfo = malloc(sizeof(struct scaleinfo));
	sinfo->dim = n;
	sinfo->f = f;
	sinfo->df = df;
	sinfo->state = data;
	sinfo->sx = malloc(n * sizeof(double));
	sinfo->scale = malloc(n * sizeof(double));
	for (i = 0; i < n; i++)
		sinfo->scale[i] = 1.;

	opt->df = dfWrap;
	opt->f = fWrap;
	opt->state = sinfo;
	opt->df(opt->x, opt->dx, opt->state);

	for (i = 0; i < n; i++)
		if ((opt->x[i] <= opt->lb[i] && opt->dx[i] >= 0.)
		    || (opt->x[i] >= opt->ub[i] && opt->dx[i] <= 0.))
			opt->onbound[i] = 1;
	opt->fc = fx;

}

double 
TakeStep(OPTOBJ * opt, const double tol, double *factor,
	 int *newbound)
{
	double          norm;
	double         *direct, *space;
	double          maxfactor;
	int             i;

	direct = opt->space;
	space = opt->space + opt->n;
	*newbound = 0;

	do {
		norm = GetNewtonStep(direct, opt->H, opt->dx, opt->n, opt->onbound);
	} while (UpdateActiveSet
	  (opt->x, direct, ((struct scaleinfo *) opt->state)->scale, opt->H,
	   opt->lb, opt->ub, opt->onbound, opt->n,newbound));

	/*  Scale step to satisfy trust region */
	if ( norm>opt->trust){ scale_vector (direct,opt->n,opt->trust/norm); }

	*factor = 1.;
	maxfactor =
		TrimAtBoundaries(opt->x, direct,
			   ((struct scaleinfo *) opt->state)->scale, opt->n,
				 opt->lb, opt->ub, opt->onbound);
	/*
	 * Three tier decision criteria. i. If Newton step is valid, accept.
	 * ii. If not, look at function and gradient at boundary and decide
	 * whether to accept. iii. Otherwise do line search to find maximum.
	 */
	if (maxfactor <= 1.0) {
		/* Don't accept Newton step -- will hit boundary */
		double          fbound;
		ScaledStep(maxfactor, opt->x, opt->xn, direct, opt->onbound, opt->n);
		fbound = opt->f(opt->xn, opt->state); opt->neval++;
		if (fbound <= opt->fc) {
			/*
			 * Point on boundary is better than current point,
			 * check grad
			 */
			double          fnearbound;
			ScaledStep(maxfactor * (1. - BOUND_TOL), opt->x, opt->xn, direct,
				   opt->onbound, opt->n);
			fnearbound = opt->f(opt->xn, opt->state); opt->neval++;
			if (fnearbound > fbound) {
				/*
				 * If point on boundary appears to be
				 * maximum, keep it
				 */
				ScaledStep(maxfactor, opt->x, opt->xn, direct, opt->onbound, opt->n);
				opt->fn = fbound;
OPTMESS(printf("Newton step hit boundary, appears optimal (maxfactor = %e)\n",maxfactor);)
				goto optexit;
			}
		}
		for (i = 0; i < opt->n; i++) {
			opt->xn[i] = opt->x[i];
		}
OPTMESS(printf("Newton step hit boundary, doing linesearch (maxfactor = %e)\n",maxfactor);)
OPTMESS(printf("f(x) = %6.5e\n",opt->f(opt->x, opt->state)););
		opt->fn =
			linemin_backtrack(opt->f, opt->n, opt->xn, space, direct, opt->state, 0.,
				       maxfactor, 1e-5, 0, &opt->neval);
		 opt->trust = (opt->trust/2.0<MIN_TRUST)?MIN_TRUST:opt->trust/2.0;
	} else {
		/* Setup accept Newton step */
		ScaledStep(1., opt->x, opt->xn, direct, opt->onbound, opt->n);
		opt->fn = opt->f(opt->xn, opt->state); opt->neval++;
		if  ( opt->fc<opt->fn){
			for (i = 0; i < opt->n; i++) {
				opt->xn[i] = opt->x[i];
			}
OPTMESS(printf("Newton step feasible but not optimal -- doing line search\n");)
			opt->fn =
				linemin_backtrack(opt->f, opt->n, opt->xn, space, direct, opt->state,
					       0., 1., 1e-5,0,&opt->neval);
			opt->trust = (opt->trust/2.0<MIN_TRUST)?MIN_TRUST:opt->trust/2.0;
		} else {
			opt->trust  = (2.0*opt->trust>=MAX_TRUST)?MAX_TRUST:2.0*opt->trust;
OPTMESS(printf("Newton step feasible and gives improvement\n");)
		}
	}

optexit:
	OPTMESS(printf ("Trust region is now %e\n",opt->trust);)
	if ( opt->fn > opt->fc){
		/*  Still worse position  */
		for (i = 0; i < opt->n; i++) {
			opt->xn[i] = opt->x[i];
			opt->dxn[i] = opt->dx[i];
		}
		opt->fn = opt->fc;
		norm = 0.;
		for (i = 0; i < opt->n; i++) {
			if (!opt->onbound[i]) {
				norm += opt->dxn[i] * opt->dxn[i];
			}
		}
OPTMESS(printf("Failed to find improved point.\nf(x) = %e\n",opt->f(opt->x, opt->state)););
		return sqrt(norm);
	}
	opt->df(opt->xn, opt->dxn, opt->state);
	for ( int i=0 ; i<opt->n ; i++){direct[i] = -opt->dxn[i];}
	UpdateActiveSet (opt->xn, direct, ((struct scaleinfo *) opt->state)->scale, opt->H,
                                    opt->lb, opt->ub, opt->onbound, opt->n,newbound);

	UpdateH_BFGS(opt->H, opt->x, opt->xn, opt->dx, opt->dxn,
		     ((struct scaleinfo *) opt->state)->scale, opt->n, space,
		     opt->onbound);


	opt->fc = opt->fn;
	for (i = 0; i < opt->n; i++) {
		opt->x[i] = opt->xn[i];
		opt->dx[i] = opt->dxn[i];
	}

	norm = 0.;
	for (i = 0; i < opt->n; i++)
		if (!opt->onbound[i])
			norm += opt->dx[i] * opt->dx[i];
OPTMESS(printf("f(x) = %e\n",opt->f(opt->x, opt->state)););
	return sqrt(norm);
}

double 
TrimAtBoundaries(const double *x, const double *direct,
		 const double *scale, const int n, const double *lb,
		 const double *ub, const int *onbound)
{
	int             i, idx;
	double          bound, maxerr, epserr;
	volatile double maxfact, fact;

	assert(NULL != x);
	assert(NULL != direct);
	assert(NULL != scale);
	assert(n > 0);
	assert(NULL != lb);
	assert(NULL != ub);
	assert(NULL != onbound);
	for (i = 0; i < n; i++) {
		assert(finite(direct[i]));
		assert((!onbound[i] && x[i] * scale[i] > lb[i]
			&& x[i] * scale[i] < ub[i]) || onbound[i]);
		assert(!onbound[i] || direct[i] == 0.);
	}

	maxfact = DBL_MAX;
	maxerr = 0.;
	for (i = 0; i < n; i++) {
		if (!onbound[i] && fabs(direct[i]) > DBL_EPSILON) {
			bound = ((direct[i] > 0.) ? ub[i] : lb[i]) / scale[i];
			fact = (bound - x[i]) / direct[i];
			epserr = (fabs(bound) + fabs(x[i])) / fabs(direct[i]);
			assert(fact > 0.);
			if (fact < maxfact) {
				idx = i;
				maxfact = fact;
				maxerr = epserr;
			}
		}
	}

	/* Correction for rounding to ensure bounds are never exceeded */
	maxfact -= (maxerr + maxfact) * DBL_EPSILON;

	for (i = 0; i < n; i++) {
		assert(onbound[i]
		 || (((x[i] + maxfact * direct[i]) * scale[i] - lb[i] >= 0.)
		&& ((x[i] + maxfact * direct[i]) * scale[i] - ub[i] <= 0.)));
	}

	return maxfact;
}


int 
UpdateActiveSet(const double *x, double *direct, const double *scale,
		double *InvHess, const double *lb, const double *ub,
		int *onbound, const int n, int * newbound)
{
	int             i, j;
	int             nremoved = 0;
	double          diag;

	assert(NULL != x);
	assert(NULL != direct);
	assert(NULL != scale);
	assert(NULL != lb);
	assert(NULL != ub);
	assert(NULL != onbound);
	assert(NULL != InvHess);
	assert(n > 0);
	for (i = 0; i < n; i++) {
		assert(finite(x[i]));
		assert(finite(direct[i]));
		for (j = 0; j < n; j++)
			assert(finite(InvHess[i * n + j]));
	}

	/*Check boundaries*/
	for (i = 0; i < n; i++) {
		if ((x[i] * scale[i] - lb[i] < BOUND_TOL && direct[i] <= 0.)
		    || (ub[i] - x[i] * scale[i] < BOUND_TOL && direct[i] >= 0.)) {
			if (onbound[i] == 0) {
				/* Newly on boundary. Modify Hessian */
				nremoved++;
				errn = errn | PARAM_BOUND;
				diag = InvHess[i * n + i];
				InvertMatrix(InvHess,n);
				for ( j=0 ; j<n ; j++){
					InvHess[i*n+j] = 0.;
					InvHess[j*n+i] = 0.;
				}
				InvHess[i*n+i] = fabs(diag);
				InvertMatrix(InvHess,n);
			}
			onbound[i] = 1;
			direct[i] = 0.;
		} else {
			/* Parameter is not on boundary or gradient is such that 
			 * parameter is coming off boundary */
			onbound[i] = 0;
		}
	}
	*newbound += nremoved;
	return nremoved;
}

double SteepestDescentStep ( OPTOBJ * opt){
	assert(NULL!=opt);

	double * direct = opt->space;
	double * space = opt->space + opt->n;

	for ( int i=0 ; i<opt->n ; i++){ 
		opt->xn[i] = opt->x[i];
		direct[i] = (opt->onbound[i])?0.:-opt->dx[i];
	}
	double maxfactor = TrimAtBoundaries(opt->x, direct, ((struct scaleinfo *) opt->state)->scale, opt->n, opt->lb, opt->ub, opt->onbound);
	double fnew = linemin_backtrack(opt->f, opt->n, opt->xn, space, direct, opt->state, 0.,
                                       maxfactor, 1e-12, 0, &opt->neval);
        OPTMESS(printf("Steepest descent: Diff = %e\n",opt->fc-fnew);)
	return fnew;
}	






double 
GetNewtonStep(double *direct, const double *InvHess,
	      const double *grad, const int n, const int *onbound)
{
	double          norm = 0.;

	assert(NULL != direct);
	assert(NULL != InvHess);
	assert(NULL != grad);
	assert(NULL != onbound);
	assert(n > 0);

	for (int i = 0; i < n; i++) {
		direct[i] = 0.;
		for (int j = 0; j < n; j++) {
			if ( !onbound[j]){
				direct[i] -= InvHess[i * n + j] * grad[j];
			}
		}
		if ( onbound[i]) { direct[i] = 0.;}
		norm += direct[i] * direct[i];
	}

	return sqrt(norm);
}

void 
ScaledStep(const double factor, const double *x, double *xn,
	   const double *direct, const int *onbound, const int n)
{
	int             i;

	assert(NULL != x);
	assert(NULL != xn);
	assert(NULL != direct);
	assert(NULL != onbound);
	assert(n > 0);

	for (i = 0; i < n; i++)
		assert(!onbound[i] || direct[i] == 0.);

	for (i = 0; i < n; i++)
		xn[i] = x[i] + direct[i] * factor;
}


/* Space n+n+n */
int
UpdateH_BFGS( double *H, const double *x, double *xn, const double *dx,
	     double *dxn, double *scale, const int n, double *space,
	     const int *onbound)
{
	double          gd = 0., *Hg, gHg = 0.;
	double         *g, *d, f;
	int             i, j;

	g = space;
	space += n;
	d = space;
	space += n;
	Hg = space;
	space += n;


	gd = 0.;
	for (i = 0; i < n; i++) {
		if (!onbound[i]) {
			d[i] = xn[i] - x[i];
			g[i] = dxn[i] - dx[i];
			gd += g[i] * d[i];
		}
	}

	if (gd <= 1e-5) {
		errn = errn | HESSIAN_NONPD;
		MakeMatrixIdentity(H, n);
		return 1;
	}

	gHg = 0.;
	for (i = 0; i < n; i++) {
		Hg[i] = 0.;
		if (!onbound[i]) {
			for (j = 0; j < n; j++) {
				if (!onbound[j]) {
					Hg[i] += H[i * n + j] * g[j];
				}
			}
			gHg += g[i] * Hg[i];
		}
	}

	/*
	 * Update inverse hessian using BFGS. Using fact that InvHessian is
	 * symmetric, so only need to do half the matrix.
	 */
	f = 1. + gHg / gd;
	for (i = 0; i < n; i++) {
		if (!onbound[i]) {
			for (j = 0; j <= i; j++)
				if (!onbound[j]) {
					H[i * n + j] +=
						(f * d[i] * d[j] - d[i] * Hg[j] - Hg[i] * d[j]) / gd;
				}
		}
	}


	/*
	 * Make matrix symmetric. Make upper diagonal equal to lower diagonal
	 */
	for (i = 0; i < n; i++)
		for (j = 0; j < i; j++) 
			H[j * n + i] = H[i * n + j];
	Rescale(xn, dxn, H, n, scale);

	return 1;
}



void 
InitializeH(OPTOBJ * opt)
{
	MakeMatrixIdentity(opt->H, opt->n);
	reset = 1;
}


void 
TestIdentity(double *A, double *B, int n)
{

	double          tmp;
	int             i, j, k;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			tmp = 0.;
			for (k = 0; k < n; k++)
				tmp += A[i * n + k] * B[k * n + j];
			if (i == j && fabs(1. - tmp) > 1e-8)
				printf("%d %d = %e\n", i, j, tmp);
			else if (i != j && fabs(tmp) > 1e-8)
				printf("%d %d = %e\n", i, j, tmp);
		}
}

double 
fWrap(const double *x, void *info)
{
	int             i;
	struct scaleinfo *sinfo;
	double          fx;
	assert(NULL != x);
	assert(NULL != info);

	sinfo = (struct scaleinfo *) info;
	assert(CheckScaleInfo(sinfo));

	for (i = 0; i < sinfo->dim; i++) {
		sinfo->sx[i] = x[i] * sinfo->scale[i];
	}

	fx = sinfo->f(sinfo->sx, sinfo->state);

	return fx;
}

void 
dfWrap(const double *x, double *grad, void *info)
{
	int             i;
	struct scaleinfo *sinfo;
	assert(NULL != x);
	assert(NULL != info);

	sinfo = (struct scaleinfo *) info;
	assert(CheckScaleInfo(sinfo));

	for (i = 0; i < sinfo->dim; i++) {
		sinfo->sx[i] = x[i] * sinfo->scale[i];
	}
	

	sinfo->df(sinfo->sx, grad, sinfo->state);

	for (i = 0; i < sinfo->dim; i++) {
		grad[i] *= sinfo->scale[i];
	}

	return;
}

int 
CheckScaleInfo(struct scaleinfo * sinfo)
{
	assert(NULL != sinfo);
	assert(sinfo->dim > 0);
	assert(NULL != sinfo->sx);
	assert(NULL != sinfo->df);
	assert(NULL != sinfo->f);
	assert(NULL != sinfo->scale);
	assert(NULL != sinfo->state);

	return 1;
}




void
Rescale(double *x, double *dx, double *H, int n, double *scale)
{
	int             i, j;
	double          scalefact;

	assert(NULL != x);
	assert(NULL != dx);
	assert(NULL != H);
	assert(n > 0);
	assert(NULL != scale);


	for (i = 0; i < n; i++) {
		scalefact = sqrt(H[i * n + i]);
		scalefact = (scalefact>MINSCALE)?scalefact:MINSCALE;
		scalefact = (scalefact<MAXSCALE)?scalefact:MAXSCALE;
		for (j = 0; j < n; j++) {
			H[i * n + j] /= scalefact;
			H[j * n + i] /= scalefact;
		}
		x[i] /= scalefact;
		dx[i] *= scalefact;
		scale[i] *= scalefact;
	}
}


void 
AnalyseOptima(double *x, double *dx, int n, int *onbound, double *lb,
	      double *ub)
{
	int             i;
	assert(NULL != x);
	assert(NULL != dx);
	assert(n > 0);
	assert(NULL != onbound);
	assert(NULL != lb);
	assert(NULL != ub);

	for (i = 0; i < n; i++) {
		printf("%4d: ", i);
		if (onbound[i]) {
			printf("Boundary. lb: %e, ub: %e. Grad %e\n", x[i] - lb[i],
			       ub[i] - x[i], dx[i]);
		} else {
			printf("Not on boundary. lb: %e, ub: %e, grad = %e\n", x[i] - lb[i],
			       ub[i] - x[i], dx[i]);
		}
	}
}
