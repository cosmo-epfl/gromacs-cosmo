 /*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include "sysstuff.h"
#include "typedefs.h"
#include "macros.h"
#include "vec.h"
#include "pbc.h"
#include "xvgr.h"
#include "gromacs/fileio/futil.h"
#include "gromacs/commandline/pargs.h"
#include "gromacs/fileio/tpxio.h"
#include "gromacs/fileio/trxio.h"
#include "physics.h"
#include "index.h"
#include "gromacs/utility/smalloc.h"
#include "calcgrid.h"
#include "nrnb.h"
#include "coulomb.h"
#include "gstat.h"
#include "gromacs/fileio/matio.h"
#include "gmx_ana.h"
#include "hyperpol.h"
#include "names.h"

#include "gromacs/legacyheaders/gmx_fatal.h"

static void do_nonlinearopticalscatteringtheta(t_topology *top, /*const char *fnNDX, const char *fnTPS,*/ const char *fnTRX,
                   const char *fnSFACT, const char *method,
                   gmx_bool bPBC, gmx_bool bKleinmannsymm, 
                   int nbintheta, int p_in1, int p_in2 ,
                   real fade, int *isize, int  *molindex[], char **grpname, int ng,
                   const output_env_t oenv)
{
    FILE          *fp;
    FILE          *fpn;
    t_trxstatus   *status;
    char           outf1[STRLEN], outf2[STRLEN];
    char           title[STRLEN], gtitle[STRLEN], refgt[30];
    int            g, natoms, i, j, k, qq, n, nframes;
    real         **s_method, **s_method_coh, **s_method_incoh, *temp_method;
    real           qnorm, coh_temp = 0.0,  incoh_temp = 0.0, tot_temp = 0.0, theta = 0.0;
    real          *cos_t, *sin_t, *cq, *sq , *c_square, *s_square, *cs_sc ,***beta_mol, *beta_lab_z_t, *beta_lab_y_t, beta_fact;
    real           beta_lab_sq_z = 0.0, beta_lab_sq_y = 0.0, beta_lab_y_z =0.0, beta_lab_z = 0.0, beta_lab_y = 0.0, bzz = 0.0, bzy = 0.0, byz = 0.0, byy = 0.0;
    int            max_i, isize0, ind0;
    real           t, rmax2, rmax,  r, r_dist, r2, q_xi, dq, invhbinw, normfac, norm_x, norm_z, mod_f, inv_width;
    real           segvol, spherevol, prev_spherevol, invsize0;
    rvec          *x, dx,  *x_i1, xi, x01, x02, *arr_qvec,  pol_in1, pol_in2; 
    real           invvol, invvol_sum, rho;
    matrix         box, box_pbc;
    int            ePBC = -1, ePBCrdf = -1;
    t_block       *mols = NULL;
    t_atom        *atom = NULL;
    t_pbc          pbc;
    gmx_rmpbc_t    gpbc = NULL;
    int            mol, a;

    atom = top->atoms.atom;
    mols = &(top->mols);
    isize0 = isize[0];
    invsize0 = 1.0/isize0;
    natoms = read_first_x(oenv, &status, fnTRX, &t, &x, box);
    
    fprintf(stderr,"\nnatoms %d\n",natoms);
    fprintf(stderr,"molindex %d\n",molindex[0][0]);
    fprintf(stderr,"x[0] %f %f %f\n", x[0][XX], x[0][YY], x[0][ZZ]);
    fprintf(stderr,"isize[0] %d isize[1] %d, isize[2] %d, invsize0 %f \n",isize[0], isize[1], isize[2], invsize0);
    fprintf(stderr,"ng %d\n",ng);

    /* initialize some handy things */
    if (ePBC == -1)
    {
        ePBC = guess_ePBC(box);
    }
    copy_mat(box, box_pbc);
    ePBCrdf = ePBC;
    if (bPBC)
    {
        rmax2   =  max_cutoff2(FALSE ? epbcXY : epbcXYZ, box_pbc);
        fprintf(stderr, "rmax2 = %f\n", rmax2);
        
    }
    else
    {
        rmax2   = sqr(3*max(box[XX][XX], max(box[YY][YY], box[ZZ][ZZ])));
    }
    if (debug)
    {
        fprintf(debug, "rmax2 = %g\n", rmax2);
    }
    
    /*here find the normalization of the OH bond-length and of the dipole vector*/
    set_pbc(&pbc, ePBCrdf, box_pbc);
    ind0  = mols->index[molindex[0][0]];
    pbc_dx(&pbc, x[ind0], x[ind0+1], x01);
    pbc_dx(&pbc, x[ind0], x[ind0+2], x02);
    rvec_sub( x01, x02, xi);
    fprintf(stderr,"xvec %f %f %f \n",xi[XX], xi[YY], xi[ZZ]);
    norm_x = gmx_invsqrt(norm2(xi));
    rvec_add( x01, x02, xi);
    fprintf(stderr,"xvec %f %f %f \n",xi[XX], xi[YY], xi[ZZ]);
    norm_z = gmx_invsqrt(norm2(xi));
    fprintf(stderr,"norm_z norm_x %f %f \n", norm_x, norm_z);
    rmax     = sqrt(rmax2);

    snew(s_method, ng);
    snew(s_method_coh, ng);
    snew(s_method_incoh, ng);
    max_i = isize[0];

    /*allocate memory for beta in lab frame and initialize beta in mol frame*/
    snew(beta_lab_z_t, isize[0]);
    snew(beta_lab_y_t, isize[0]);
    snew(beta_mol, DIM);
    for (i = 0; i < DIM; i++)
    {
        snew(beta_mol[i], DIM);
    }
    for (i = 0; i < DIM; i++)
    {    
        for (j = 0; j < DIM; j++)
        {
            snew(beta_mol[i][j], DIM);
        }
    }

    /*beta_mol parameters in a.u. taken from Kusalik Mol. Phys. 99 1107-1120, (2001)*/
    /*conversion factor from a.u. to [e^3]*[nm^3]/[Hartree^2] is 0.000148184711 */
    /* use the fact that beta_ijj = beta_jij */
    beta_mol[2][0][0] = 5.7;
    beta_mol[2][2][2] = 31.6;
    beta_mol[2][1][1] = 10.9;
    fprintf(stderr,"beta_mol[2][0][0] %f\n",beta_mol[2][0][0]);
    fprintf(stderr,"beta_mol[2][2][2] %f\n",beta_mol[2][2][2]);
    fprintf(stderr,"beta_mol[2][1][1] %f\n",beta_mol[2][1][1]);
    if (bKleinmannsymm)
    {
       beta_mol[0][0][2] = 5.7 ;
       beta_mol[0][2][0] = 5.7 ;
       beta_mol[1][1][2] = 10.9;
       beta_mol[1][2][1] = 10.9;
    }
    
    for (g = 0; g < ng; g++)
    {
        /* this is THE array */
        /*allocate memory for s_method array */
        snew(s_method[g], nbintheta);
        snew(s_method_coh[g], nbintheta);
        snew(s_method_incoh[g],nbintheta);
        snew(temp_method, nbintheta);
        snew(arr_qvec,nbintheta);
        snew(cq,nbintheta);
        snew(sq,nbintheta);
        snew(c_square,nbintheta);
        snew(s_square,nbintheta);
        snew(cs_sc, nbintheta);
        snew(cos_t,nbintheta);
        snew(sin_t,nbintheta);
        /*initialize polarization vectors*/
        for (i = 0 ; i<DIM; i++)
        {
          pol_in1[i] = 0.0;
          pol_in2[i] = 0.0;
        }
        /*assign the non-zero components of the polarization vectors to project beta to the lab-frame*/
        /* e.g. beta_zzz is obtained with pol_out[2] = 1.0, pol_in1[2] = 1.0, pol_in2[2] = 1.0 */
        pol_in1[p_in1] = 1.0;
        pol_in2[p_in2] = 1.0;
        inv_width = (fade == 0.0 ) ? 1.0 : M_PI*0.5/(rmax-fade) ;
        if (method[0]=='s')
        {
           qnorm = M_PI*2.0/(rmax*2.0);
        }
        else
        {
          qnorm = M_PI*2.0/rmax ;
        }
        for (qq = 0; qq< nbintheta; qq++)
        {
            theta  = M_PI/nbintheta*qq -M_PI*0.5;
            arr_qvec[qq][XX] = 0.0;
            arr_qvec[qq][YY] = -qnorm*sin(theta);
            arr_qvec[qq][ZZ] = -qnorm*cos(theta) - qnorm; 
            cq[qq] = cos(theta) ;
            sq[qq] = sin(theta) ;
            c_square[qq] = cq[qq]*cq[qq];
            cs_sc[qq] = cq[qq]*sq[qq];
            s_square[qq] = sq[qq]*sq[qq];
        }
    }

    snew(x_i1, max_i);
    nframes    = 0;
    invvol_sum = 0;
    if (bPBC && (NULL != top))
    {
        gpbc = gmx_rmpbc_init(&top->idef, ePBC, natoms);
    }
    if (method[0] == 'm'  && fade == 0.0)
    {
       fprintf(stderr,"modified direct method (modsumexp) no fading\n");
       do
       {
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);

            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                beta_lab_sq_y = 0.0;
                beta_lab_sq_z = 0.0;
                beta_lab_y_z = 0.0 ;
                snew(temp_method, nbintheta);
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], x_i1[i]);
                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    rotate_beta_theta(norm_x, norm_z, x01, x02, pol_in1, pol_in2, beta_mol, &beta_lab_z, &beta_lab_y );
                    beta_lab_sq_z += beta_lab_z*beta_lab_z ;
                    beta_lab_sq_y += beta_lab_y*beta_lab_y ;
                    beta_lab_y_z  += beta_lab_z*beta_lab_y ;
                    beta_lab_z_t[i] = beta_lab_z;
                    beta_lab_y_t[i] = beta_lab_y;
                }
                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        pbc_dx(&pbc, xi, x_i1[j], dx);
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {
                            bzz = beta_lab_z_t[i]*beta_lab_z_t[j]  ;
                            byy = beta_lab_y_t[i]*beta_lab_y_t[j]  ;
                            bzy = beta_lab_z_t[i]*beta_lab_y_t[j]  ;
                            byz = beta_lab_y_t[i]*beta_lab_z_t[j]  ;

                            for (qq = 0; qq < nbintheta; qq++)
                            {
                               temp_method[qq] += (bzz*s_square[qq] + byy*c_square[qq] + (byz + bzy)*cs_sc[qq])*cos(iprod(arr_qvec[qq],dx));
                            }
                        }
                    }
                }
                for (qq = 0; qq < nbintheta; qq++)
                {
                   incoh_temp = (beta_lab_sq_y*c_square[qq] +  beta_lab_sq_z*s_square[qq] + 2.0*beta_lab_y_z*cs_sc[qq])*invsize0;
                   coh_temp = 2.0*temp_method[qq]*invsize0;
                   s_method_coh[g][qq] += coh_temp ;
                   s_method[g][qq] += coh_temp + incoh_temp ;
                   s_method_incoh[g][qq] += incoh_temp;
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }

    else if (method[0] == 'm' && fade != 0.0)
    {
       fprintf(stderr,"modified direct method (modsumexp) with fading\n");
       do
       {
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);

            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                beta_lab_sq_y = 0.0;
                beta_lab_sq_z = 0.0; 
                beta_lab_y_z = 0.0 ;
                snew(temp_method, nbintheta);
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], x_i1[i]);
                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    rotate_beta_theta(norm_x, norm_z, x01, x02, pol_in1, pol_in2, beta_mol, &beta_lab_z, &beta_lab_y );
                    beta_lab_sq_z += beta_lab_z*beta_lab_z ;
                    beta_lab_sq_y += beta_lab_y*beta_lab_y ;
                    beta_lab_y_z  += beta_lab_z*beta_lab_y ;
                    beta_lab_z_t[i] = beta_lab_z; 
                    beta_lab_y_t[i] = beta_lab_y;
                }
                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        pbc_dx(&pbc, xi, x_i1[j], dx);
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {
                            r_dist = sqrt(r2);
                            bzz = beta_lab_z_t[i]*beta_lab_z_t[j]  ;
                            byy = beta_lab_y_t[i]*beta_lab_y_t[j]  ;
                            bzy = beta_lab_z_t[i]*beta_lab_y_t[j]  ;
                            byz = beta_lab_y_t[i]*beta_lab_z_t[j]  ;

                            if ( r_dist <= fade)
                            {
                                for (qq = 0; qq < nbintheta; qq++)
                                {
                                   temp_method[qq] += (bzz*s_square[qq] + byy*c_square[qq] + (byz + bzy)*cs_sc[qq])*cos(iprod(arr_qvec[qq],dx));
                                }
                            }
                            else
                            {
                                mod_f = sqr(cos((r_dist-fade)*inv_width)) ;
                                for (qq = 0; qq < nbintheta; qq++)
                                {
                                   /*fprintf(stderr,"s_method[g][qq] %f\n", mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx)) );*/
                                   temp_method[qq] += (bzz*s_square[qq] + byy*c_square[qq] + (byz + bzy)*cs_sc[qq])*mod_f*cos(iprod(arr_qvec[qq],dx));
                                }
                            }
                        }
                    }
                }
                for (qq = 0; qq < nbintheta; qq++)
                {
                   incoh_temp = (beta_lab_sq_y*c_square[qq] +  beta_lab_sq_z*s_square[qq] + 2.0*beta_lab_y_z*cs_sc[qq])*invsize0;
                   coh_temp = 2.0*temp_method[qq]*invsize0;
                   s_method_coh[g][qq] += coh_temp ;
                   s_method[g][qq] += coh_temp + incoh_temp ;
                   s_method_incoh[g][qq] += incoh_temp;
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    else if (method[0] == 's')
    {
        fprintf(stderr,"loop with sumexp method \n");
        do
        {
            /* Must init pbc every step because of pressure coupling */
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);
            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                snew(cos_t,nbintheta);
                snew(sin_t,nbintheta);
                beta_lab_sq_z = 0.0;
                beta_lab_sq_y = 0.0;
                beta_lab_y_z =  0.0;
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    pbc_dx(&pbc, xi, x[ind0+1], x01);
                    pbc_dx(&pbc, xi, x[ind0+2], x02);
                    rotate_beta_theta(norm_x, norm_z, x01, x02, pol_in1, pol_in2, beta_mol, &beta_lab_z, &beta_lab_y );
                    beta_lab_sq_z += beta_lab_z*beta_lab_z ;
                    beta_lab_sq_y += beta_lab_y*beta_lab_y ;
                    beta_lab_y_z  += beta_lab_z*beta_lab_y ;
                    for (qq = 0; qq < nbintheta; qq++)
                    {
                       q_xi = iprod(arr_qvec[qq],xi);
                       beta_fact  = beta_lab_z*sq[qq]+beta_lab_y*cq[qq];
                       cos_t[qq] += beta_fact*cos(q_xi);
                       sin_t[qq] += beta_fact*sin(q_xi);                      
                    }
                }
                for (qq = 0; qq < nbintheta; qq++)
                {
                   incoh_temp = (beta_lab_sq_y*c_square[qq] +  beta_lab_sq_z*s_square[qq] + 2.0*beta_lab_y_z*cs_sc[qq])*invsize0;
                   tot_temp = (cos_t[qq]*cos_t[qq] + sin_t[qq]*sin_t[qq])*invsize0;
                   s_method[g][qq] +=  tot_temp  ;
                   s_method_coh[g][qq] += tot_temp  - incoh_temp;
                   s_method_incoh[g][qq] += incoh_temp; 
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
/*    else if ((method[0] == 'c' && bSpectrum == TRUE && fade != 0.0) || (method[0] == 's' && fade != 0.0)  )
    {
        gmx_fatal(FARGS,"Combination of method %c, spectrum %d and fading %d  unavailable", method[0], bSpectrum, fade);
    }*/
    fprintf(stderr, "\n");
    if (bPBC && (NULL != top))
    {
        gmx_rmpbc_done(gpbc);
    }
    
    close_trj(status);

    sfree(x);

    /* Average volume */
    invvol = invvol_sum/nframes;
    if (method[0]=='m')
    {
       for (g = 0; g < ng; g++)
       {
              for (qq = 0; qq < nbintheta ; qq++)
              {
                  s_method_incoh[g][qq] = s_method_incoh[g][qq]/nframes ;
                  s_method_coh[g][qq] = s_method_coh[g][qq]/(nframes) ;
                  s_method[g][qq] = s_method[g][qq]/(nframes) ;
              }
       }
    }
    else if (method[0]=='s')
    {
       for (g = 0; g < ng; g++)
       {
           for (qq = 0; qq < nbintheta ; qq++)
           {
              s_method[g][qq] = s_method[g][qq]/(nframes)  ;
              s_method_coh[g][qq] = s_method_coh[g][qq]/(nframes)  ;
              s_method_incoh[g][qq] = s_method_incoh[g][qq]/(nframes) ;
           }  
       }
    }

    sprintf(gtitle, "Non-linear optical scattering ");
    fp = xvgropen(fnSFACT, gtitle, "theta", "S(theta)", oenv);
    sprintf(refgt, "%s", "");
    fprintf(fp, "@    s0 legend \"coherent\" \n");
    fprintf(fp, "@target G0.S0\n");
    if (ng == 1)
    {
        if (output_env_get_print_xvgr_codes(oenv))
        {
            fprintf(fp, "@ subtitle \"%s%s - %s\"\n", grpname[0], refgt, grpname[1]);
        }
    }
    else
    {
        if (output_env_get_print_xvgr_codes(oenv))
        {
            fprintf(fp, "@ subtitle \"reference %s%s\"\n", grpname[0], refgt);
        }
        xvgr_legend(fp, ng, (const char**)(grpname+1), oenv);
    }
    for (qq = 0; qq < nbintheta  ; qq++)
    {
        fprintf(fp, "%10g", M_PI/nbintheta*qq -M_PI*0.5);
        for (g = 0; g < ng; g++)
        {   
            fprintf(fp, " %10g", s_method_coh[g][qq]);
        }
        fprintf(fp, "\n");
    }
    fprintf(fp,"&\n");
    fprintf(fp, "@    s1 legend \"incoherent\"\n");
    fprintf(fp, "@target G0.S1\n");
    fprintf(fp, "@type xy\n");
    for (qq = 0; qq < nbintheta  ; qq++)
    {
        fprintf(fp, "%10g", M_PI/nbintheta*qq -M_PI*0.5);
        for (g = 0; g < ng; g++)
        { 
            fprintf(fp, " %10g", s_method_incoh[g][qq]);
        }
        fprintf(fp, "\n");
    }
    fprintf(fp,"&\n");
    fprintf(fp, "@    s2 legend \"total\"\n");
    fprintf(fp, "@target G0.S2\n");
    fprintf(fp, "@type xy\n");
    for (qq = 0; qq < nbintheta  ; qq++)
    {
        fprintf(fp, "%10g", M_PI/nbintheta*qq -M_PI*0.5);
        for (g = 0; g < ng; g++)
        {
            fprintf(fp, " %10g", s_method[g][qq]);
        }
        fprintf(fp, "\n");
    }
    gmx_ffclose(fp);

    do_view(oenv, fnSFACT, NULL);

    for (g = 0; g < ng; g++)
    {
       sfree(s_method[g]);
       sfree(s_method_coh[g]);
       sfree(s_method_incoh[g]);
    }
    sfree(s_method);
    sfree(s_method_coh);
    sfree(arr_qvec);
    sfree(cos_t);
    sfree(sin_t);
    sfree(temp_method);
    sfree(cq);
    sfree(sq);
    sfree(c_square);
    sfree(s_square);
    sfree(cs_sc);
    sfree(beta_lab_y_t);
    sfree(beta_lab_z_t);

    for (i = 0; i < DIM; i++)
    {
        for (j = 0; j < DIM; j++)
        {
            sfree(beta_mol[i][j]);
        }
    }
    for (j = 0; j < DIM; j++)
    {
        sfree(beta_mol[j]);
    }
}

void rotate_beta_theta(real invnormx, real invnormz, const rvec xv2, const rvec xv3, const rvec pin1, const rvec pin2, real ***beta_m, real *beta_2, real *beta_1)
{
    rvec xvec, yvec, zvec;
    rvec_sub( xv2, xv3, xvec); /*this should be x*/
    svmul(invnormx, xvec ,xvec );
    rvec_add( xv2, xv3, zvec);
    svmul(invnormz, zvec, zvec ); /*this should be z*/
    cprod(xvec , zvec, yvec); /*this should be y*/

    /*should be faster than triple loop over i,j,k and uses only the 7 non zero component of water (C2v symmetry) */
    *beta_2 = beta_m[2][0][0]*zvec[2]*iprod(xvec, pin1)*iprod(xvec, pin2) +
              beta_m[2][1][1]*zvec[2]*iprod(yvec, pin1)*iprod(yvec, pin2) +
              beta_m[2][2][2]*zvec[2]*iprod(zvec, pin1)*iprod(zvec, pin2) +
              beta_m[0][0][2]*xvec[2]*iprod(xvec, pin1)*iprod(zvec, pin2) +
              beta_m[0][2][0]*xvec[2]*iprod(zvec, pin1)*iprod(xvec, pin2) +
              beta_m[1][1][2]*yvec[2]*iprod(yvec, pin1)*iprod(zvec, pin2) +
              beta_m[1][2][1]*yvec[2]*iprod(zvec, pin1)*iprod(yvec, pin2) ;


    *beta_1 = beta_m[2][0][0]*zvec[1]*iprod(xvec, pin1)*iprod(xvec, pin2) +
             beta_m[2][1][1]*zvec[1]*iprod(yvec, pin1)*iprod(yvec, pin2) +
             beta_m[2][2][2]*zvec[1]*iprod(zvec, pin1)*iprod(zvec, pin2) +
             beta_m[0][0][2]*xvec[1]*iprod(xvec, pin1)*iprod(zvec, pin2) +
             beta_m[0][2][0]*xvec[1]*iprod(zvec, pin1)*iprod(xvec, pin2) +
             beta_m[1][1][2]*yvec[1]*iprod(yvec, pin1)*iprod(zvec, pin2) +
             beta_m[1][2][1]*yvec[1]*iprod(zvec, pin1)*iprod(yvec, pin2) ;
}

void dipole_atom2mol(int *n, int *index, t_block *mols)
{
    int nmol, i, j, m;

    nmol = 0;
    i    = 0;
    while (i < *n)
    {
        m = 0;
        while (m < mols->nr && index[i] != mols->index[m])
        {
            m++;
        }
        if (m == mols->nr)
        {
            gmx_fatal(FARGS, "index[%d]=%d does not correspond to the first atom of a molecule", i+1, index[i]+1);
        }
        for (j = mols->index[m]; j < mols->index[m+1]; j++)
        {
            if (i >= *n || index[i] != j)
            {
                gmx_fatal(FARGS, "The index group is not a set of whole molecules");
            }
            i++;
        }
        index[nmol++] = m;
    }
    printf("There are %d molecules in the selection\n", nmol);
    *n = nmol;
}


int gmx_nonlinearopticalscatteringtheta(int argc, char *argv[])
{
    const char        *desc[] = {
        "The structure of liquids can be studied by elastic second harmonic scattering (also Hyper-Raileigh scattering).",
        "[THISMODULE] calculates the non-linear optical scattering intensity per molecule in 2 different ways.",
        "The simplest method (sumexp) is to use 1/N<|sum_i beta_IJK(i) exp[iq dot r_i]|^2>.",
        "This however converges slowly with the simulation time.[PAR]",
        "A similar method (modsumexp), but N times more expensive is to do",
        "1/N <sum_i beta_IJK^2> + 1/N <sum_i!=j exp(i q dot r_ij)beta_IJK(i)beta_IJK(j)>.[PAR]"
        "The method cosmo (default) uses the following expression to compute I(q):",
        "I(q)=1/N<sum_i beta_IJK(i)^2> + 1/(2N)< sum_i sum_{i!=j} beta_IJK(i)beta_IJK(j) cos(q dot r_ij) >.",
        "I(q)=incoherent term + coherent term. For more details see Bersohn, et al. JCP 45, 3184 (1966)",
        "The values for the hyperpolarizability for a water molecule beta_IJK are taken by",
        "A. V. Gubskaya et al., Mol. Phys. 99, 13 1107 (2001) (computed at MP4 level with mean field liquid water).",
        "pout, pin1, pin2 are the polarization directions of the three beams.",
        "Common polarization combinations are PSS, PPP, SPP, SSS .",
        "Under Kleinmann symmetry beta_ijj = beta_jij = beta_jji otherwise beta_ijj = beta_jij. [PAR]",
    };
    static gmx_bool    bPBC = TRUE, bKleinmannsymm = TRUE;
    static real        fade = 0.0;
    static int         ngroups = 1, nbintheta = 100, pin1 = 1, pin2 = 1;

    static const char *methodt[] = { NULL, "sumexp", "modsumexp" ,NULL }; 

    t_pargs            pa[] = {
        { "-nbintheta",      FALSE, etINT, {&nbintheta},
        "number of bins over wave-vector" },
        { "-pin1",         FALSE, etINT, {&pin1}, "polarization of 1st incoming beam. For P choose 1, for S choose 0 " },
        { "-pin2",         FALSE, etINT, {&pin2}, "polarization of 2nd incoming beam should the same as 1st for second harmonic scattering." },
        { "-method",     FALSE, etENUM, {methodt},
          "I(q) using the different methods" },
        { "-pbc",      FALSE, etBOOL, {&bPBC},
          "Use periodic boundary conditions for computing distances. Without PBC the maximum range will be three times the largest box edge." },
        { "-klein",    FALSE, etBOOL, {&bKleinmannsymm}, "Assume Kleinmann symmetry" },
        { "-ng",       FALSE, etINT, {&ngroups}, 
          "Number of secondary groups to compute RDFs around a central group" },
        { "-fade",     FALSE, etREAL, {&fade},
          "In the method the modification function cos((rij-fade)*pi/(2*(L/2-fade))) is used in the fourier transform."
          " If fade is 0.0 nothing is done." },

    };
#define NPA asize(pa)
    const char        *fnTPS, *fnNDX;
    output_env_t       oenv;
    int           *gnx;
    int            nFF[2];
    atom_id      **grpindex;
    char         **grpname = NULL;
    /*gmx_bool       bGkr, bMU, bSlab;*/

    t_filenm           fnm[] = {
        { efTRX, "-f",  NULL,     ffREAD },
        { efTPS, NULL,  NULL,     ffREAD },
        { efNDX, NULL,  NULL,     ffOPTRD },
        { efXVG, "-o",  "non_linear_sfact",    ffWRITE },
    };
#define NFILE asize(fnm)
    int            npargs;
    t_pargs       *ppa;
    t_topology    *top;
    int            ePBC;
    int            k, natoms;
    matrix         box;
    
    npargs = asize(pa);
    if (!parse_common_args(&argc, argv, PCA_CAN_VIEW | PCA_CAN_TIME | PCA_BE_NICE,
                           NFILE, fnm, NPA, pa, asize(desc), desc, 0, NULL, &oenv))
    {
        return 0;
    }

    fnTPS = ftp2fn_null(efTPS, NFILE, fnm);
    fnNDX = ftp2fn_null(efNDX, NFILE, fnm);

    if (!fnTPS && !fnNDX)
    {
        gmx_fatal(FARGS, "Neither index file nor topology file specified\n"
                  "Nothing to do!");
    }

    snew(top, ngroups+1);
    ePBC = read_tpx_top(ftp2fn(efTPS, NFILE, fnm), NULL, box,
                        &natoms, NULL, NULL, NULL, top);

    snew(gnx, ngroups+1);
    snew(grpname, ngroups+1);
    snew(grpindex, ngroups+1);
    get_index(&top->atoms, ftp2fn_null(efNDX, NFILE, fnm),
             ngroups +1 , gnx, grpindex, grpname);

    dipole_atom2mol(&gnx[0], grpindex[0], &(top->mols));
   
    do_nonlinearopticalscatteringtheta(top, ftp2fn(efTRX, NFILE, fnm),
           opt2fn("-o", NFILE, fnm), methodt[0],  bPBC, bKleinmannsymm, nbintheta, pin1, pin2 ,
           fade, gnx, grpindex, grpname, ngroups, oenv);

    return 0;
}