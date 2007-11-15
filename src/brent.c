#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <math.h>
#include "utility.h"

#define GOLDENRATIO 0.38196601125010515179541316563436 /* 32dp using GNU bc 1.06 */

static double parabolic_interpolate ( const double a, const double b, const double c, const double fa, const double fb, const double fc){
	double dba = b - a;
	double dbc = b - c;
	double dfba = dbc*(fb-fa);
	double dfbc = dba*(fb-fc);

	return b - 0.5 * (dba*dfbc - dbc*dfba)/(dfbc - dfba);
}


double brentmin ( double lb, const double * flbp, double ub, const double * fubp, double x, double * fxp, double (*fun)(const double, void *), const double tol, void * info){
	assert (lb<=x && x<=ub);

	/*  If function evaluations not given, evaluated */
	double flb = (NULL!=flbp) ? *flbp : fun(lb,info);
	double fub = (NULL!=fubp) ? *fubp : fun(ub,info);
	double fx  = (NULL!=fxp)  ? *fxp  : fun(x,info);

	/*  Ensure that points given actually bracket a minimum */
	assert(fx<=flb && fx<=fub);

	double diff_old2 = 0., diff_old = 0.;
	double fractol = tol*fabs(x)+3e-8;
	while ( (fabs(x-0.5*(ub+lb))+0.5*(ub-lb)) > 2.*fractol ){
		//printf ( "Bracket %1.16e %1.16e %1.16e\n       (%1.16e %1.16e %1.16e) err=%e (%e)\n",lb,x,ub,flb,fx,fub,(fabs(x-0.5*(ub+lb))+0.5*(ub-lb)),2.*fractol);
		/* Trial point */
		double x_new = parabolic_interpolate (lb,x,ub,flb,fx,fub);
		double diff = fabs(x-x_new);
		/* If too near already evaluated point, do Golden section step instead
		 * Conditions: last step tiny
		 *             step too small compared to one but last
		 *             step outside boundaries
                 */
		if ( isnan(x_new) || diff_old<fractol || diff<=0.5*diff_old2 || x_new<lb || x_new>ub){
			//if ( isnan(x_new) ){printf("** Invalid parabolic step (NaN)\n");}
			//if ( diff_old<fractol){printf("** Last step really small (%e<%e)\n",diff_old,fractol);}
			//if ( diff <= 0.5*diff_old2 ){ printf("** Step too small compared to half last but one (%e<%e)\n",diff,0.5*diff_old2);}
			//if ( x_new<lb || x_new>ub){ printf("** Parabolic point exceeds bounds (%e,%e,%e)\n",lb,x_new,ub);}
			//printf ("\tTrying Golden section search (%e->",x_new);
			x_new = x + GOLDENRATIO * ((2*x>(lb+ub))?(lb-x):(ub-x));
			//printf ("%e)\n",x_new);
			diff = fabs(x_new-x);
		}

		/* Suggested point indistinguishable from old? Try further away */
		x_new = (diff>=fractol)?x_new:x+fractol*sign(x_new-x);
		//if ( diff<=fractol ){printf("\tToo small (x_new = %e)\n",x_new);}
		double f_new = fun(x_new,info);
		/*  Sort out new bracket  */
		if ( f_new < fx){ /* New minimum */
			if ( x_new >= x){
				lb = x; flb = fx;
			} else {
				ub = x; fub = fx;
			}
			x = x_new; fx = f_new;
		} else {
			if ( x_new >= x){
				ub = x_new; fub = f_new;
			} else {
				lb = x_new ; flb = f_new;
			}
		}
		diff_old2 = diff_old; diff_old = diff;
                /* Update tolerances */
                fractol = tol*fabs(x)+DBL_EPSILON;
	}

	//printf ("\tFinished -- err= %e <= %e\n",fabs(x-0.5*(ub+lb))+0.5*(ub-lb),2.*fractol);
	if ( NULL!=fxp){ *fxp = fx;}
	return x;
}
