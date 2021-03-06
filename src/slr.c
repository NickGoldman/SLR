/*
 *  Copyright 2003-2008 Tim Massingham (tim.massingham@ebi.ac.uk)
 *  Funded by EMBL - European Bioinformatics Institute
 */
/*
 *  This file is part of SLR ("Sitewise Likelihood Ratio")
 *
 *  SLR is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  SLR is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with SLR.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <err.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include "rng.h"
#include "gencode.h"
#include "model.h"
#include "codonmodel.h"
#include "utility.h"
#include "optimize.h"
#include "bases.h"
#include "options.h"
#include "matrix.h"
#include "data.h"
#include "tree.h"
#include "tree_data.h"
#include "gamma.h"
#include "statistics.h"
#include "root.h"
#include "linemin.h"

#define GRIDSIZE	50
#define VERSIONSTRING	"1.5.0"

struct selectioninfo {
    double *llike_neu;
    double *llike_max;
    double *omega_max;
    double *lbound, *ubound;
    int *type;
};

struct slr_params {
    double *params;
    int nparams;
    double *cfreqs;
    int gencode;
    double *blengths;
    int nbr;
};

int AnimoParam[400];
int nseq = 0;

char *FrequencyOptString[3] = { "Empirical (F6?)", "F3x4", "F1x4" };

int PowellOpt(double p[], int dim, double (*fun) (double[]), double *fmax);
int Trans(int a);
double CalcLike_Single(const double *param, void *data);
void GradLike_Single(const double *param, double *grad, void *data);
void GradLike_Full(const double *param, double *grad, void *data);

VEC create_grid(const unsigned int len, const bool positive);
int FindBestX(const double *grid, const int site, const int n);
DATA_SET *ReadData(const char *name, const int gencode);
double OptimizeTree(const DATA_SET * data, TREE * tree, double *freqs,
                    double *x, const unsigned int freqtype, const int codonf,
                    const enum model_branches branopt, const bool readTemp,
                    const bool recover);
struct selectioninfo *CalculateSelection(TREE * tree, DATA_SET * data,
                                         double kappa, double omega,
                                         double *freqs, const double ldiff,
                                         const unsigned int freqtype,
                                         const int codonf);
void fprint_results(FILE * fp, struct selectioninfo *selinfo,
                    const double *entropy, const double *pval,
                    const double *pval_adj, const int nsites);
double *CalculatePvals(const double *lmax, const double *lneu, const int n,
                       const bool positive_only);
double *AdjustPvals(const double *pval, DATA_SET * data);
double *CalculateEntropy(const DATA_SET * data, const double *freqs);
int IsRandomSite(const int site, const double *entropy, const double *lmax);
void fprint_summary(FILE * fp, const struct selectioninfo *selinfo,
                    const double *entropy, const double *pval,
                    const double *pval_adj, const int n_pts);
double CalcLike_Wrapper(const double *x, void *info);
void Set_CalcLike_Wrapper(double (*f) (const double *, void *), double diff);

void fprint_params(FILE * fp, const double *params, const int nparams,
                   const double *cfreqs, const int gencode, const TREE * tree);
void WriteParams(const char *file, const double *params, const int nparams,
                 const double *cfreqs, const int gencode, const TREE * tree);
struct slr_params *ReadParams(const char *file);

double eps = 1e-4;

char *OutString[5] =
    { "All gaps", "Single char", "Synonymous", "", "Constant" };

/*   Strings describing options and defaults */
int n_options = 24;
char *options[] = { "seqfile", "treefile", "outprefix", "kappa", "omega",
    "codonf", "nucleof", "aminof", "reoptimise", "nucfile",
    "aminofile", "positive_only", "gencode", "timemem", "ldiff",
    "paramin", "paramout", "skipsitewise", "seed", "freqtype",
    "cleandata", "branopt", "writetmp", "recover"
};

char *optiondefault[] = { "incodon", "intree", "slr", "2.0", "0.1",
    "0", "0", "0", "1", "nuc.dat",
    "amino.dat", "0", "universal", "0", "3.841459",
    "", "", "0", "0", "1",
    "0", "1", "0", "0"
};

char optiontype[] = { 's', 's', 's', 'f', 'f',
    'd', 'd', 'd', 'd', 's',
    's', 'd', 's', 'd', 'f',
    's', 's', 'd', 'd', 'd',
    'd', 'd', 'd', 'd'
};

int optionlength[] = { 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1
};

char *default_optionfile = "slr.ctl";

int main(int argc, char *argv[])
{
    TREE **trees;
    DATA_SET *data;
    double *freqs;
    struct single_fun *info;
    double kappa, omega, loglike, ldiff;
    char *seqfile, *treefile, *outprefix, *nucfile, *aminofile, *gencode_str,
        *paramin;
    int codonf, nucleof, aminof, reoptimise;
    bool positive;
    double *x;
    int a, bran, i;
    int gencode, timemem, skipsitewise, freqtype;
    struct selectioninfo *selinfo;
    double *entropy, *pval, *pval_adj;
    time_t slr_clock[4];
    struct slr_params *paramin_str;
    unsigned int seed, cleandata;
    enum model_branches branopt;
    bool writeTmp, recover;
    /*  Option variables
     */
    ReadOptions(argc, argv);
    kappa = *(double *)GetOption("kappa");
    omega = *(double *)GetOption("omega");
    seqfile = (char *)GetOption("seqfile");
    treefile = (char *)GetOption("treefile");
    outprefix = (char *)GetOption("outprefix");
    codonf = *(int *)GetOption("codonf");
    nucleof = *(int *)GetOption("nucleof");
    aminof = *(int *)GetOption("aminof");
    nucfile = (char *)GetOption("nucfile");
    aminofile = (char *)GetOption("aminofile");
    reoptimise = *(int *)GetOption("reoptimise");
    positive = *(bool *) GetOption("positive_only");
    gencode_str = (char *)GetOption("gencode");
    timemem = *(int *)GetOption("timemem");
    ldiff = *(double *)GetOption("ldiff");
    paramin = (char *)GetOption("paramin");
    skipsitewise = *(int *)GetOption("skipsitewise");
    seed = *(unsigned int *)GetOption("seed");
    freqtype = *(unsigned int *)GetOption("freqtype");
    cleandata = *(unsigned int *)GetOption("cleandata");
    branopt = *(enum model_branches *)GetOption("branopt");
    writeTmp = *(bool *) GetOption("writetmp");
    recover = *(bool *) GetOption("recover");

    PrintOptions();

    if (timemem) {
        time(slr_clock);
    }

    /*  Initialise random number generator */
    RL_Init(seed);

    fputs
        ("# SLR \"Sitewise Likelihood Ratio\" selection detection program. Version ",
         stdout);
    fputs(VERSIONSTRING, stdout);
    fputc('\n', stdout);

    SetAminoAndCodonFuncs(nucleof, aminof, nucfile, aminofile);
    gencode = GetGeneticCode(gencode_str);

    if (0 != cleandata) {
        warnx
            ("cleandata options not implemented yet. Defaulting to 0 (treat ambiguous characters as gaps).\n");
    }
    data = ReadData(seqfile, gencode);
    if (NULL == data) {
        puts("Problem reading data file. Aborting\n");
        exit(EXIT_FAILURE);
    }

    printf("# Read seqfile file %s. %d species, %d sites.\n", seqfile,
           data->n_sp, data->n_pts);

    /*  Get frequencies from data
     */
    freqs = GetBaseFreqs(data, 0);

    if (paramin[0] != '\0') {
        printf("# Reading old parameter values from %s\n", paramin);
        paramin_str = ReadParams(paramin);
        kappa = paramin_str->params[0];
        omega = paramin_str->params[1];
        for (i = 0; i < 64; i++) {
            freqs[i] = paramin_str->cfreqs[i];
        }
        if (paramin_str->gencode != gencode) {
            puts(" # Warning. Codon freqquencies estimated under different genetic code to data\n");
        }
    }

    /*  Calculations are in terms of Q coordinates (enumerated sense codons)
     */
    ConvertCodonToQcoord(data);

    trees = read_tree_strings(treefile);
    OOM(trees);
    OOM(trees[0]);
    create_tree(trees[0]);
    printf("# Read tree from %s.\n", treefile);
    fprint_tree(stdout, trees[0]->tree, NULL, trees[0]);

    bool reoptmess = false;
    for (bran = 0; bran < trees[0]->n_br; bran++) {
        NODE *node = trees[0]->branches[bran];
        if (node->blength[0] < 0. || 2 == reoptimise) {
            node->blength[0] = RandomExp(0.1);
            a = find_connection(node->branch[0], node);
            assert(-1 != a);
            (node->branch[0])->blength[a] = node->blength[0];

            if (reoptimise == 0 && !reoptmess) {
                puts("# Found branch of undetermined or invalid length. Set to random value and will optimise tree");
                reoptmess = true;
            } else
                if ((Branches_Fixed == branopt
                     || Branches_Proportional == branopt) && !reoptmess) {
                puts("# Found branch of undetermined or invalid length. Set to random value and will optimise branch lengths");
                reoptmess = true;
            }
            reoptimise = 1;
            branopt = Branches_Variable;
        }
    }

    info = calloc(1, sizeof(struct single_fun));
    OOM(info);
    info->tree = trees[0];
    info->p = malloc(data->n_pts * 2 * sizeof(double));
    OOM(info->p);

    if (0 != reoptimise) {
        /* Set initials
         */
        const unsigned int nbr = trees[0]->n_br;
        unsigned int nparam = 2;
        if (Branches_Variable == branopt)
            nparam += nbr;
        if (Branches_Proportional == branopt)
            nparam += 1;
        x = calloc(nparam, sizeof(double));

        unsigned int offset = 0;
        if (Branches_Variable == branopt) {
            for (unsigned int bran = 0; bran < nbr; bran++) {
                /* Branch lengths already randomised if necessary */
                x[bran] = (trees[0]->branches[bran])->blength[0];
            }
            offset += nbr;
        } else if (Branches_Proportional == branopt) {
            x[0] = 1.;
            offset++;
        }

        x[offset + 0] = (kappa >= 0.) ? kappa : RandomExp(2.0);
        x[offset + 1] = (omega >= 0.) ? omega : RandomExp(0.1);

        if (timemem) {
            time(slr_clock + 1);
        }

        loglike =
            OptimizeTree(data, trees[0], freqs, x, freqtype, codonf, branopt,
                         writeTmp, recover);
        kappa = x[offset + 0];
        omega = x[offset + 1];
        printf("# lnL = %.3f\n", loglike);
        free(x);

        if (Branches_Proportional == branopt) {
            ScaleTree(trees[0], x[0]);
        }

        if (timemem) {
            time(slr_clock + 2);
        }
    }

    /*  Print some information about tree */
    {
        FILE * tree_fp = fopen_with_suffix(outprefix, ".tree", "w");
        if(NULL != tree_fp){
	    fprintf(tree_fp, "%d 1\n", trees[0]->n_sp);
            fprint_tree(tree_fp, trees[0]->tree, NULL, trees[0]);
        }
        fclose(tree_fp);
        fprint_tree(stdout, trees[0]->tree, NULL, trees[0]);
    }

    double min, max, len;
    double blen;
    min = max = len = (trees[0]->branches[0])->blength[0];
    for (int i = 1; i < trees[0]->n_br; i++) {
        blen = (trees[0]->branches[i])->blength[0];
        len += blen;
        max = (max > blen) ? max : blen;
        min = (min < blen) ? min : blen;
    }
    printf("# Kappa = %8.6f Omega = %8.6f\n", kappa, omega);
    printf
        ("# Tree length = %4.2f, average branch length = %4.2f (min=%4.2f, max=%4.2f)\n",
         len, len / trees[0]->n_br, min, max);

    if (!skipsitewise) {
        selinfo =
            CalculateSelection(trees[0], data, kappa, omega, freqs, ldiff,
                               freqtype, codonf);
        entropy = CalculateEntropy(data, freqs);
        pval =
            CalculatePvals(selinfo->llike_max, selinfo->llike_neu, data->n_pts,
                           positive);
        pval_adj = AdjustPvals(pval, data);

	FILE * results_fp = fopen_with_suffix(outprefix, ".res", "w");
	if(NULL != results_fp){
            fprint_results(results_fp, selinfo, entropy, pval, pval_adj, data->n_pts);
	}
	fclose(results_fp);

	FILE * summary_fp = fopen_with_suffix(outprefix, ".summary", "w");
	if(NULL != summary_fp){
            fprintf(summary_fp, "Kappa = %8.6f\nOmega = %8.6f\n", kappa, omega);
	    fprintf(summary_fp, "lnL = %.3f\n", loglike);
            fprintf(summary_fp,
             "Tree length = %4.2f, average branch length = %4.2f (min=%4.2f, max=%4.2f)\n",
             len, len / trees[0]->n_br, min, max);
            fprint_summary(summary_fp, selinfo, entropy, pval, pval_adj, data->n_pts);
	}
	fclose(summary_fp);

        fprint_summary(stdout, selinfo, entropy, pval, pval_adj, data->n_pts);
    }

    if (timemem) {
        struct rusage slr_usage;
        time(slr_clock + 3);
        getrusage(RUSAGE_SELF, &slr_usage);
        fprintf(stdout, "#CpuTime\t%d\n", (int)slr_usage.ru_utime.tv_sec);
        fprintf(stdout, "#DiffTimes\t%ld\t%ld\t%ld\n",
                slr_clock[1] - slr_clock[0], slr_clock[2] - slr_clock[1],
                slr_clock[3] - slr_clock[2]);
    }

    return EXIT_SUCCESS;
}

int FindBestX(const double *grid, const int site, const int n)
{
    assert(NULL != grid);
    assert(site >= 0);
    assert(n >= 0);

    double min = DBL_MAX;
    unsigned int bestidx = 0;
    for (unsigned int a = 0; a < n; a++) {
        if (grid[site * n + a] < min) {
            min = grid[site * n + a];
            bestidx = a;
        }
    }

    return bestidx;
}

DATA_SET *ReadData(const char *name, const int gencode)
{
    DATA_SET *tmp, *data;

    assert(NULL != name);

    /* Read nucleotides and convert into codons
     */
    tmp = read_data(name, SEQTYPE_NUCLEO);
    if (NULL == tmp)
        return NULL;
    data = ConvertNucToCodon(tmp, gencode);
    if (NULL == data) {
        puts("Error converting nucleotides to codons. Returning uncompressed sequence.");
        return tmp;
    }
    FreeDataSet(tmp);

    /*  Check if sequence contains stop codons  */
    int nstop = count_alignment_stops(data);
    if (0 != nstop) {
        fputs("Alignment contains stop codons. Cannot continue.\n", stderr);
        exit(EXIT_FAILURE);
    }
    /*  Sort and compress sequence to remove redundency
     */
    sort_data(data);
    tmp = compress_data(data);
    if (NULL == tmp) {
        puts("Error compressing sequence! Returning uncompressed set");
        return data;
    }
    FreeDataSet(data);

    /*  Find and mask trivial observations in data. Likelihood for these
     * observations can be calculated trivially without using the pruning
     * algorithm
     */
    data = RemoveTrivialObs(tmp);
    if (NULL == data) {
        puts("Error removing trivial observations (single chars and all gaps).\nReturning compressed sequence.\n");
        return tmp;
    }
    FreeDataSet(tmp);
    if (data->n_pts != data->n_unique_pts) {
        printf("# Redundency. Reduced sites from %d to %d\n", data->n_pts,
               data->n_unique_pts);
    }

    return data;
}

double OptimizeTree(const DATA_SET * data, TREE * tree, double *freqs,
                    double *x, const unsigned int freqtype, const int codonf,
                    const enum model_branches branopt, const bool writeTmp,
                    const bool recover)
{
    struct single_fun *info;
    double *bd, fx;
    int i;
    MODEL *model;

    CheckIsDataSet(data);
    CheckIsTree(tree);
    assert(NULL != x);

    printf("# Reoptimising parameters, branches %s\n",
           model_branches_string[branopt]);

    const unsigned int nbr = tree->n_br;
    model =
        NewCodonModel_full(data->gencode, x[nbr + 0], x[nbr + 1], freqs, codonf,
                           freqtype, branopt);
    OOM(model);
    model->exact_obs = 1;

    const unsigned int nparam =
        model->nparam + ((Branches_Variable == branopt) ? nbr : 0);
    bd = calloc(2 * nparam, sizeof(double));
    OOM(bd);

    /* Set boundaries
     */
    for (i = 0; i < nparam; i++) {
        bd[i] = 1e-8;
        bd[i + nparam] = 50.;
    }

    /*  Check that initial estimates are within boundaries
     */
    for (i = 0; i < nparam; i++) {
        if (x[i] <= bd[i])
            x[i] = bd[i] + 1e-5;
        if (x[i] >= bd[nparam + i])
            x[i] = bd[nparam + i] - 1e-5;
    }

    info = calloc(1, sizeof(struct single_fun));
    OOM(info);
    info->tree = tree;
    info->p = calloc(data->n_pts * 2, sizeof(double));
    OOM(info->p);
    info->model = model;

    add_data_to_tree(data, tree, model);
    //x[nbr-1] = 1.;
    //CheckModelDerivatives(model,0.5,x+nbr,1e-5);
    fx = CalcLike_Single(x, info);

    Optimize(x, nparam, GradLike_Full, CalcLike_Single, &fx, (void *)info, bd,
             writeTmp, recover);

    FreeModel(model);
    free(bd);
    free(info->p);
    free(info);

    return fx;
}

struct selectioninfo *CalculateSelection(TREE * tree, DATA_SET * data,
                                         double kappa, double omega,
                                         double *freqs, const double ldiff,
                                         const unsigned int freqtype,
                                         const int codonf)
{
    double x[1];
    struct selectioninfo *selinfo;
    bool positive;
    double factor;
    MODEL *model;
    DATA_SET *data_single;
    struct single_fun *info;
    double bd[2];
    double *likelihood_grid;
    int col;
    int *done_usite;
    int species, bufflen;

    CheckIsTree(tree);
    CheckIsDataSet(data);
    assert(kappa >= 0.);
    assert(omega >= 0.);
    assert(freqs != NULL);

    const int dosupport = (0.0 == ldiff) ? 0 : 1;

    selinfo = malloc(sizeof(struct selectioninfo));
    OOM(selinfo);
    selinfo->llike_neu = calloc(data->n_pts, sizeof(double));
    OOM(selinfo->llike_neu);
    selinfo->llike_max = calloc(data->n_pts, sizeof(double));
    OOM(selinfo->llike_max);
    selinfo->omega_max = calloc(data->n_pts, sizeof(double));
    OOM(selinfo->omega_max);
    if (dosupport) {
        selinfo->lbound = calloc(data->n_pts, sizeof(double));
        OOM(selinfo->lbound);
        selinfo->ubound = calloc(data->n_pts, sizeof(double));
        OOM(selinfo->ubound);
    } else {
        selinfo->lbound = NULL;
        selinfo->ubound = NULL;
    }
    selinfo->type = calloc(data->n_pts, sizeof(int));
    OOM(selinfo->type);

    positive = *(bool *) GetOption("positive_only");

    model =
        NewCodonModel_single(data->gencode, kappa, omega, freqs, codonf,
                             freqtype);
    OOM(model);
    model->exact_obs = 1;

    /* Calculate scale factor relative to neutral evolution and scale
     * tree appropriately.
     */
    {
        factor = GetScale_single(model, omega);
        factor /= GetScale_single(model, 1.);
        ScaleTree(tree, factor);
        printf("# Scaling tree to neutral evolution. Factor = %3.2f\n", factor);
    }

    //  One site data set to be used in all optimizations
    data_single = CreateDataSet(1, data->n_sp);
    OOM(data_single);
    for (species = 0; species < data->n_sp; species++) {
        bufflen = 1 + strlen(data->sp_name[species]);
        data_single->sp_name[species] = malloc(bufflen * sizeof(char));
        strncpy(data_single->sp_name[species], data->sp_name[species], bufflen);
    }

    info = calloc(1, sizeof(struct single_fun));
    info->tree = tree;
    info->p = calloc(2 * data->n_unique_pts, sizeof(double));

    //  Set boundaries. Lower bound is 1. if only positive selection is
    // is of interest.
    if (positive)
        bd[0] = 1.;
    else
        bd[0] = 0.;
    bd[1] = 99.;
    info->model = model;

    /*  Calculate grid of sitewise likelihoods for many omega, use to
     * provide good starting values for each sitewise observation.
     *  Do this since it is relative quick to calculate the sitewise for all
     * sites for a single omega (due to memory effects rather than algorithms).
     */
    puts("# Calculating initial estimates of sitewise conservation");
    add_data_to_tree(data, tree, model);
    const VEC omega_grid = create_grid(GRIDSIZE, positive);

    /*  Fill out sitewise likelihoods for grid  */
    likelihood_grid = calloc(data->n_unique_pts * GRIDSIZE, sizeof(double));
    OOM(likelihood_grid);
    for (unsigned int row = 0; row < GRIDSIZE; row++) {
        x[0] = vget(omega_grid, row);
        CalcLike_Single(x, info);
        for (unsigned int pt = 0; pt < data->n_unique_pts; pt++) {
            likelihood_grid[pt * GRIDSIZE + row] =
                -(tree->tree)->scalefactor[pt] - log(info->p[pt]);
        }
    }
    /*  Fill out vector of likelihoods for neutral evolution */
    double *likelihood_neutral = calloc(data->n_unique_pts, sizeof(double));
    x[0] = 1.;
    CalcLike_Single(x, info);
    for (unsigned int pt = 0; pt < data->n_unique_pts; pt++) {
        likelihood_neutral[pt] =
            -(tree->tree)->scalefactor[pt] - log(info->p[pt]);
    }

    puts("# Calculating conservation at each site. This may take a while.");
    col = 0;
    done_usite = calloc(data->n_unique_pts, sizeof(int));
    for (unsigned int site = 0; site < data->n_unique_pts; site++) {
        done_usite[site] = -1;
    }

    for (unsigned int site = 0; site < data->n_pts; site++) {
        double fm, fn;
        double lb = 0.0, ub = HUGE_VAL;
        double omegam;
        int type;

        if (col % 50 == 0) {
            printf("\n%4d:  ", col + 1);
        }
        col++;

        // Is site all gaps?`
        if (data->index[site] == -INT_MAX) {
            omegam = 1.;
            fm = 0.;
            fn = 0.;
            type = 0;
        }
        // Does site only exist in one sequence
        else if (data->index[site] < 0) {
            omegam = 1.;
            fm = -log(model->pi[-data->index[site] - 1]);
            fn = fm;
            type = 1;
            //printf ("%5d recent insert\n",site);
        } else if (done_usite[data->index[site]] != -1) {
            int usite = done_usite[data->index[site]];
            fn = selinfo->llike_neu[usite];
            fm = selinfo->llike_max[usite];
            omegam = selinfo->omega_max[usite];
            if (dosupport) {
                lb = selinfo->lbound[usite];
                ub = selinfo->ubound[usite];
            }
            type = selinfo->type[usite];
            //printf ("%5d same as site %d\n",site,usite);
        } else {
            int start;
            // General case
            CopySiteToDataSet(data, data_single, site);
            add_data_to_tree(data_single, tree, model);
            start = FindBestX(likelihood_grid, data->index[site], GRIDSIZE);
            fn = likelihood_neutral[data->index[site]];

            bd[0] =
                (start > 0) ? vget(omega_grid, start - 1) : (double)positive;
            bd[1] = (start < GRIDSIZE - 1) ? vget(omega_grid, start + 1) : 99.;
            x[0] = vget(omega_grid, start);
            // Sanity check
            if (!finite(x[0])) {
                errx(EXIT_FAILURE, "Non-finite x[0] detected");
            }

            int neval = 0;
            fm = linemin_1d(CalcLike_Single, x, (void *)info, bd[0], bd[1],
                            1e-5, &neval);
            omegam = model->param[1];
            if (IsConserved(data, site)) {
                type = 4;
            } else if (IsSiteSynonymous(data, site, data->gencode)) {
                type = 2;
            } else {
                type = 3;
            }

            /*  Find confidence interval for omega (actually "support") */
            if (dosupport) {
                neval = 0;
                if (likelihood_grid[data->index[site] * GRIDSIZE] - fm <=
                    ldiff / 2.) {
                    lb = (double)positive;
                } else {
                    const double initial_lb = (double)positive;
                    Set_CalcLike_Wrapper(CalcLike_Single, fm + ldiff / 2.);
                    lb = find_root(initial_lb, omegam, CalcLike_Wrapper,
                                   (void *)info, NULL, NULL, 1e-3, &neval);
                }

                if (likelihood_grid[data->index[site] * GRIDSIZE + GRIDSIZE - 1]
                    - fm <= ldiff / 2.) {
                    ub = 99.;
                } else {
                    Set_CalcLike_Wrapper(CalcLike_Single, fm + ldiff / 2.);
                    neval = 0;
                    ub = find_root(omegam, 99., CalcLike_Wrapper, (void *)info,
                                   NULL, NULL, 1e-3, &neval);
                }
            }

            assert(data->index[site] >= 0);
            done_usite[data->index[site]] = site;
        }

        selinfo->llike_neu[site] = fn;
        selinfo->llike_max[site] = fm;
        selinfo->omega_max[site] = omegam;
        if (dosupport) {
            selinfo->lbound[site] = lb;
            selinfo->ubound[site] = ub;
        }
        selinfo->type[site] = type;
        putchar('.');
        fflush(stdout);
    }
    free(done_usite);
    free(likelihood_grid);
    free(likelihood_neutral);
    free_vec(omega_grid);
    putchar('\n');

    return selinfo;
}

void fprint_results(FILE * fp, struct selectioninfo *selinfo,
                    const double *entropy, const double *pval,
                    const double *pval_adj, const int nsites)
{
    char result[7], sign;

    assert(NULL != fp);
    assert(NULL != selinfo);
    assert(NULL != entropy);
    assert(NULL != pval);
    assert(NULL != pval_adj);
    assert(nsites > 0);

    const int dosupport = (NULL == selinfo->lbound) ? 0 : 1;
    assert(!dosupport || (NULL != selinfo->ubound && NULL != selinfo->lbound));
    assert(dosupport || (NULL == selinfo->ubound && NULL == selinfo->lbound));

    if (dosupport) {
        fputs
            ("Site\tNeutral\tOptimal\tOmega\tLower\tUpper\tLrtStat\tPvalue\tAdjPvalue\tQvalue\tResult\tNote\n",
             fp);
    } else {
        fputs
            ("Site\tNeutral\tOptimal\tOmega\tLrtStat\tPvalue\tAdjPvalue\tQvalue\tResult\tNote\n",
             fp);
    }

    for (int site = 0; site < nsites; site++) {
        double stat = 2. * (selinfo->llike_neu[site] - selinfo->llike_max[site]);
        double stat_inf = 2. * (entropy[site] - selinfo->llike_max[site]);
        for (int i = 0; i < 6; i++) {
            result[i] = ' ';
        }
        result[6] = '\0';
        if (stat_inf < 6.63) {
            result[5] = '!';
        }
        if (selinfo->omega_max[site] > 1.) {
            sign = '+';
        } else {
            sign = '-';
        }
        if (pval[site] <= 0.05)
            result[0] = sign;
        if (pval[site] <= 0.01)
            result[1] = sign;
        if (pval_adj[site] <= 0.05)
            result[2] = sign;
        if (pval_adj[site] <= 0.01)
            result[3] = sign;

        if (dosupport) {
            fprintf(fp,
                    "%d\t%.2f\t%.2f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4e\t%.4e\t%.4e\t%s\t%s\n",
                    site + 1, selinfo->llike_neu[site],
                    selinfo->llike_max[site], selinfo->omega_max[site],
                    selinfo->lbound[site], selinfo->ubound[site], stat,
                    pval[site], pval_adj[site], pval_adj[site + nsites], result,
                    OutString[selinfo->type[site]]);
        } else {
            fprintf(fp,
                    "%d\t%.2f\t%.2f\t%.4f\t%.4f\t%.4e\t%.4e\t%.4e\t%s\t%s\n",
                    site + 1, selinfo->llike_neu[site],
                    selinfo->llike_max[site], selinfo->omega_max[site], stat,
                    pval[site], pval_adj[site], pval_adj[site + nsites], result,
                    OutString[selinfo->type[site]]);
        }
    }
}

double *CalculateEntropy(const DATA_SET * data, const double *freqs)
{
    int site;
    double *entropy;
    CheckIsDataSet(data);
    assert(NULL != freqs);

    entropy = malloc(data->n_pts * sizeof(double));
    OOM(entropy);

    for (site = 0; site < data->n_pts; site++) {
        entropy[site] = SiteEntropy(data, site, freqs);
    }

    return entropy;
}

double *CalculatePvals(const double *lmax, const double *lneu, const int n,
                       const bool positive_only)
{
    int site;
    double *pval;
    double x;
    assert(NULL != lmax);
    assert(NULL != lneu);
    assert(n > 1);

    pval = malloc(n * sizeof(double));
    OOM(pval);
    for (site = 0; site < n; site++) {
        x = -2. * (lmax[site] - lneu[site]);
        if (x < 0.)
            x = 0.;
        pval[site] = pchisq(x, 1., 1);
    }

    /*  If positive only, then pvals are from chisq-bar distribution
     * (exactly half those from chisq_1).
     */
    if (positive_only) {
        for (site = 0; site < n; site++) {
            if (pval[site] + DBL_EPSILON < 1.) {
                pval[site] /= 2.;
            }
        }
    }

    return pval;
}

double *AdjustPvals(const double *pval, DATA_SET * data)
{
    int site, idx;
    int n_idx;
    double *adj_tmp, *adj, *qval_tmp;
    assert(NULL != pval);
    CheckIsDataSet(data);

    adj = malloc(2 * data->n_pts * sizeof(double));
    OOM(adj);

    n_idx = 0;
    for (site = 0; site < data->n_pts; site++) {
        idx = data->index[site];
        if (idx >= 0) {
            adj[n_idx] = pval[site];
            adj[n_idx + data->n_pts] = pval[site];
            n_idx++;
        }
    }

    adj_tmp = Pvalue_adjust_StepUp(adj, n_idx, BONFERRONI);
    qval_tmp = qvals_storey02(adj + data->n_pts, n_idx);

    n_idx = 0;
    for (site = 0; site < data->n_pts; site++) {
        idx = data->index[site];
        if (idx >= 0) {
            adj[site] = adj_tmp[n_idx];
            adj[site + data->n_pts] = qval_tmp[n_idx];
            n_idx++;
        } else {
            adj[site] = 1.;
            adj[site + data->n_pts] = 1.;
        }
    }

    free(adj_tmp);
    free(qval_tmp);
    return adj;
}

void fprint_summary(FILE * fp, const struct selectioninfo *selinfo,
                  const double *entropy, const double *pval,
                  const double *pval_adj, const int n_pts)
{
    const double *omegam, *llikem;
    int npos[4] = { 0, 0, 0, 0 }, ncons[4] = {
    0, 0, 0, 0}, dpos[4] = {
    0, 0, 0, 0};
    assert(NULL != selinfo);
    assert(NULL != selinfo->llike_neu);
    assert(NULL != selinfo->llike_max);
    assert(NULL != selinfo->omega_max);
    assert(NULL != entropy);
    assert(NULL != pval);
    assert(NULL != pval_adj);
    assert(n_pts > 0);

    omegam = selinfo->omega_max;
    llikem = selinfo->llike_max;

    for (int site = 0; site < n_pts; site++) {
        if (omegam[site] > 1.) {
            if (pval_adj[site] < 0.01) {
                npos[0]++;
                dpos[0] += IsRandomSite(site, entropy, llikem);
            }
            if (pval_adj[site] < 0.05) {
                npos[1]++;
                dpos[1] += IsRandomSite(site, entropy, llikem);
            }
            if (pval[site] < 0.01) {
                npos[2]++;
                dpos[2] += IsRandomSite(site, entropy, llikem);
            }
            if (pval[site] < 0.05) {
                npos[3]++;
                dpos[3] += IsRandomSite(site, entropy, llikem);
            }
        } else if (omegam[site] < 1.) {
            if (pval_adj[site] < 0.01) {
                ncons[0]++;
            }
            if (pval_adj[site] < 0.05) {
                ncons[1]++;
            }
            if (pval[site] < 0.01) {
                ncons[2]++;
            }
            if (pval[site] < 0.05) {
                ncons[3]++;
            }
        }
    }

    fprintf(fp, "# Positively selected sites (cumulative)\n");
    fprintf(fp, "# Significance  Number sites  Number Random\n");
    fprintf(fp, "# 99%% corrected  %5d   %5d\n", npos[0], dpos[0]);
    fprintf(fp, "# 95%% corrected  %5d   %5d\n", npos[1], dpos[1]);
    fprintf(fp, "# 99%%            %5d   %5d\n", npos[2], dpos[2]);
    fprintf(fp, "# 95%%            %5d   %5d\n", npos[3], dpos[3]);
    fputc('\n', fp);
    fprintf(fp, "# Conserved sites (cumulative)\n");
    fprintf(fp, "# Significance  Number sites\n");
    fprintf(fp, "# 99%% corrected  %5d\n", ncons[0]);
    fprintf(fp, "# 95%% corrected  %5d\n", ncons[1]);
    fprintf(fp, "# 99%%            %5d\n", ncons[2]);
    fprintf(fp, "# 95%%            %5d\n", ncons[3]);

}

int IsRandomSite(const int site, const double *entropy, const double *lmax)
{
    assert(site >= 0);
    assert(NULL != entropy);
    assert(NULL != lmax);

    if (entropy[site] - lmax[site] < 2.705947) {
        return 1;
    }

    return 0;
}

double (*CalcLike_Wrapper_fun) (const double *, void *);
double CalcLikeWrapper_const;

double CalcLike_Wrapper(const double *x, void *info)
{
    return (CalcLike_Wrapper_fun(x, info) - CalcLikeWrapper_const);
}

void Set_CalcLike_Wrapper(double (*f) (const double *, void *), double diff)
{
    CalcLike_Wrapper_fun = f;
    CalcLikeWrapper_const = diff;
}

void fprint_params(FILE * output, const double *params, const int nparams,
                 const double *cfreqs, const int gencode, const TREE * tree)
{
    int param, codon, qcodon;
    assert(NULL != output);
    assert(NULL != params);
    assert(nparams >= 0);
    assert(NULL != cfreqs);
    assert(NULL != tree);

    // Model parameters
    fprintf(output, "%d ", nparams);
    for (param = 0; param < nparams; param++) {
        fprintf(output, "%16.15e ", params[param]);
    }
    fputc('\n', output);
    // Codon frequencies
    fprintf(output, "%d ", gencode);
    for (codon = 0; codon < 64; codon++) {
        qcodon = CodonToQcoord(codon, gencode);
        (qcodon != -1) ? fprintf(output, "%16.15e ",
                                 cfreqs[qcodon]) : fprintf(output, "0.0 ");
    }
    fputc('\n', output);
    // Tree
    fprintf(output, "%d ", tree->n_br);
    PrintBranchLengths(output, tree);
}

void WriteParams(const char *file, const double *params, const int nparams,
                 const double *cfreqs, const int gencode, const TREE * tree)
{
    FILE *output;

    assert(NULL != file);
    assert(NULL != params);
    assert(nparams >= 0);
    assert(NULL != cfreqs);
    assert(NULL != tree);

    output = fopen(file, "w");
    if (NULL == output) {
        return;
    }

    fprint_params(output, params, nparams, cfreqs, gencode, tree);

    fclose(output);
}

struct slr_params *ReadParams(const char *file)
{
    FILE *input;
    int nbr, nparam;
    struct slr_params *ret_struct;
    int nread;

    assert(NULL != file);

    input = fopen(file, "r");
    if (NULL == input) {
        return NULL;
    }
    ret_struct = calloc(1, sizeof(struct slr_params));
    if (NULL == ret_struct) {
        return NULL;
    }

    nread = fscanf(input, "%d", &nparam);
    if (nread != 1 || nparam <= 0) {
        goto error_exit;
    }
    ret_struct->nparams = nparam;

    // Read in parameter values
    ret_struct->params = calloc(nparam, sizeof(double));
    if (NULL == ret_struct->params) {
        goto error_exit;
    }
    for (int param = 0; param < nparam; param++) {
        nread = fscanf(input, "%le", &(ret_struct->params[param]));
        if(nread != 1){
            goto error_exit;
        }
    }

    // Read in codon frequencies
    nread = fscanf(input, "%d", &(ret_struct->gencode));
    ret_struct->cfreqs = calloc(64, sizeof(double));
    if (nread != 1 || NULL == ret_struct->cfreqs) {
        goto error_exit;
    }
    double sum = 0.0;
    for (int codon = 0 ; codon < 64; codon++) {
        nread = fscanf(input, "%le", &(ret_struct->cfreqs[codon]));
        if (nread != 1){
            goto error_exit;
        }
        sum += ret_struct->cfreqs[codon];
    }
    for (int codon = 0; codon < 64; codon++) {
        ret_struct->cfreqs[codon] /= sum;
    }

    // Read in branch lengths
    nread = fscanf(input, "%d", &nbr);
    if (nread != 1 || nbr <= 0) {
        goto error_exit;
    }
    ret_struct->blengths = calloc(nbr, sizeof(double));
    if (NULL == ret_struct->blengths) {
        goto error_exit;
    }
    for (int br = 0; br < nbr; br++) {
        nread = fscanf(input, "%le", &(ret_struct->blengths[br]));
        if (nread != 1){
            goto error_exit;
        }
    }

    fclose(input);
    return ret_struct;

 error_exit:
    fclose(input);
    if (NULL != ret_struct) {
        if (NULL != ret_struct->params) {
            free(ret_struct->params);
        }
        if (NULL != ret_struct->cfreqs) {
            free(ret_struct->cfreqs);
        }
        if (NULL != ret_struct->blengths) {
            free(ret_struct->blengths);
        }
        free(ret_struct);
    }
    return NULL;
}

#define OMEGAMAX	50.0
#define OMEGAEXPCONST	0.5

VEC create_grid(const unsigned int len, const bool positive)
{
    assert(len > 1);

    VEC grid = create_vec(len);
    const double expconst =
        (!positive) ? (OMEGAMAX / expm1(OMEGAEXPCONST * (double)(len - 1)))
        : ((OMEGAMAX - 1.) / expm1(OMEGAEXPCONST * (double)(len - 1)));
    for (unsigned int i = 0; i < len; i++) {
        vset(grid, i,
             expconst * expm1(OMEGAEXPCONST * (double)i) +
             (positive ? 1.0 : 0.0));
    }
    return grid;
}
